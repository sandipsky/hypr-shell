#include "bar/notification_panel.hpp"

#include "bar/notification_ui.hpp"

#include <string>

namespace hyprshell {

namespace {

// tabler glyphs, \u escapes so the PUA codepoints survive every tool
constexpr const char* kIconBell = "\uEA35";
constexpr const char* kIconBellOff = "\uECE9";
constexpr const char* kIconTrash = "\uEB41";
constexpr const char* kIconChevronDown = "\uEA5F";
constexpr const char* kIconChevronUp = "\uEA62";

// Show the expand chevron only once the labels really are truncated. The
// pango layout is only valid after allocation, so poll from a tick callback
// on the button itself (auto-detached when the widget goes away).
void reveal_when_truncated(Gtk::Button* button, Gtk::Label* summary, Gtk::Label* body) {
    button->add_tick_callback(
        [button, summary, body](const Glib::RefPtr<Gdk::FrameClock>&) {
            if (summary->get_allocated_width() <= 1)
                return true; // not laid out yet — keep waiting
            const bool truncated =
                summary->get_layout()->is_ellipsized() ||
                (body && body->get_visible() && body->get_layout()->is_ellipsized());
            button->set_opacity(truncated ? 1.0 : 0.0);
            button->set_sensitive(truncated);
            return false;
        });
}

} // namespace

NotificationPanel::NotificationPanel() : Gtk::Box(Gtk::Orientation::VERTICAL, 9) {
    add_css_class("notification-panel");
    // Fixed size (popover-resize gotcha): a mapped popover surface never
    // resizes on Hyprland, and this panel's content changes while open.
    set_size_request(380, 480);

    // -- header card: [bell] Notifications ....... [dnd toggle] [Clear All] ---
    auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 9);
    header->add_css_class("notif-header");
    auto* header_icon = Gtk::make_managed<Gtk::Label>(kIconBell);
    header_icon->add_css_class("notif-header-icon");
    header->append(*header_icon);
    auto* title = Gtk::make_managed<Gtk::Label>("Notifications");
    title->add_css_class("np-title");
    title->set_halign(Gtk::Align::START);
    title->set_hexpand(true);
    header->append(*title);

    auto* clear_button = Gtk::make_managed<Gtk::Button>();
    clear_button->add_css_class("notif-clear");
    clear_button->set_valign(Gtk::Align::CENTER);
    auto* clear_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto* clear_icon = Gtk::make_managed<Gtk::Label>(kIconTrash);
    clear_icon->add_css_class("notif-clear-icon");
    clear_box->append(*clear_icon);
    clear_box->append(*Gtk::make_managed<Gtk::Label>("Clear All"));
    clear_button->set_child(*clear_box);
    clear_button->signal_clicked().connect([this] {
        NotificationService::get().clear_history();
        // nothing more to see, like Noctalia
        request_close_.emit();
    });
    header->append(*clear_button);
    append(*header);

    // -- empty state (Noctalia's bell-off card) --------------------------------
    empty_card_.add_css_class("np-disabled");
    empty_card_.set_vexpand(true);
    auto* empty_inner = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    empty_inner->set_valign(Gtk::Align::CENTER);
    empty_inner->set_vexpand(true);
    auto* empty_icon = Gtk::make_managed<Gtk::Label>(kIconBellOff);
    empty_icon->add_css_class("np-disabled-icon");
    empty_inner->append(*empty_icon);
    auto* empty_title = Gtk::make_managed<Gtk::Label>("No notifications");
    empty_title->add_css_class("np-disabled-title");
    empty_inner->append(*empty_title);
    auto* empty_sub = Gtk::make_managed<Gtk::Label>(
        "Your notifications will show up here as they arrive.");
    empty_sub->add_css_class("bp-value");
    empty_sub->set_wrap(true);
    empty_sub->set_justify(Gtk::Justification::CENTER);
    empty_sub->set_max_width_chars(34);
    empty_inner->append(*empty_sub);
    empty_card_.append(*empty_inner);
    empty_card_.set_visible(false);
    append(empty_card_);

    // -- notification list ------------------------------------------------------
    scroller_.set_child(list_);
    scroller_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroller_.set_vexpand(true); // fills the panel's fixed height
    append(scroller_);

    NotificationService::get().signal_changed().connect([this] {
        if (open_)
            rebuild();
    });
    rebuild();
}

void NotificationPanel::set_open(bool open) {
    open_ = open;
    refresh_timer_.disconnect();
    if (open) {
        // opening marks everything as seen (Noctalia's updateLastSeenTs)
        NotificationService::get().update_last_seen();
        rebuild();
        // keep "N minutes ago" fresh while the panel is up
        refresh_timer_ = Glib::signal_timeout().connect(
            [this] {
                rebuild();
                return true;
            },
            30'000);
    } else {
        expanded_id_.clear();
    }
}

void NotificationPanel::rebuild() {
    auto& service = NotificationService::get();

    // deleting a card mid-scroll shouldn't jump the list back to the top
    const double scroll_pos = scroller_.get_vadjustment()->get_value();

    while (auto* child = list_.get_first_child())
        list_.remove(*child);

    const bool empty = service.history().empty();
    empty_card_.set_visible(empty);
    scroller_.set_visible(!empty);
    if (empty)
        return;

    for (const auto& n : service.history())
        add_card(n);

    Glib::signal_idle().connect_once([this, scroll_pos] {
        auto adjustment = scroller_.get_vadjustment();
        adjustment->set_value(std::min(scroll_pos, adjustment->get_upper()));
    });
}

void NotificationPanel::add_card(const NotificationService::Notification& n) {
    const bool expanded = expanded_id_ == n.id;

    auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 9);
    card->add_css_class("notif-card");

    auto* icon = make_notification_icon(n, 40);
    icon->set_valign(expanded ? Gtk::Align::START : Gtk::Align::CENTER);
    card->append(*icon);

    auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 3);
    content->set_hexpand(true);
    content->set_valign(Gtk::Align::CENTER);

    // meta row: urgency dot + app name + relative time
    auto* meta = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    if (n.urgency != 1) {
        auto* dot = Gtk::make_managed<Gtk::Box>();
        dot->add_css_class("notif-dot");
        dot->add_css_class(n.urgency == 2 ? "critical" : "low");
        dot->set_valign(Gtk::Align::CENTER);
        meta->append(*dot);
    }
    auto* app = Gtk::make_managed<Gtk::Label>(
        n.app_name.empty() ? "Unknown App" : n.app_name);
    app->add_css_class("notif-app");
    app->set_halign(Gtk::Align::START);
    app->set_ellipsize(Pango::EllipsizeMode::END);
    meta->append(*app);
    auto* time =
        Gtk::make_managed<Gtk::Label>(notification_relative_time(n.timestamp_ms));
    time->add_css_class("notif-time");
    time->set_valign(Gtk::Align::END);
    meta->append(*time);
    content->append(*meta);

    auto make_text = [expanded](const std::string& markup, const char* css,
                                int collapsed_lines) {
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_markup(markup);
        label->add_css_class(css);
        label->set_halign(Gtk::Align::START);
        label->set_xalign(0.0f);
        label->set_wrap(true);
        label->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
        if (!expanded) {
            label->set_ellipsize(Pango::EllipsizeMode::END);
            label->set_lines(collapsed_lines);
        }
        return label;
    };
    auto* summary =
        make_text(n.summary.empty() ? "No summary" : n.summary, "notif-summary", 2);
    content->append(*summary);
    Gtk::Label* body = nullptr;
    if (!n.body.empty()) {
        body = make_text(n.body, "notif-body", 3);
        content->append(*body);
    }

    if (!n.actions.empty()) {
        auto* actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        for (const auto& action : n.actions) {
            auto* button = Gtk::make_managed<Gtk::Button>(action.text);
            button->add_css_class("notif-action");
            const std::string id = n.id, key = action.key;
            button->signal_clicked().connect([this, id, key] {
                if (NotificationService::get().invoke_action(id, key))
                    request_close_.emit();
            });
            actions->append(*button);
        }
        content->append(*actions);
    }
    card->append(*content);

    // expand + delete cluster, top-right like Noctalia
    auto* buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 3);
    buttons->set_valign(Gtk::Align::START);
    auto* expand_button = Gtk::make_managed<Gtk::Button>();
    expand_button->add_css_class("notif-icon-btn");
    expand_button->set_tooltip_text(expanded ? "Click to collapse" : "Click to expand");
    auto* chevron =
        Gtk::make_managed<Gtk::Label>(expanded ? kIconChevronUp : kIconChevronDown);
    chevron->add_css_class("notif-icon-glyph");
    expand_button->set_child(*chevron);
    {
        const std::string id = n.id;
        expand_button->signal_clicked().connect([this, id, expanded] {
            expanded_id_ = expanded ? "" : id;
            rebuild();
        });
    }
    if (expanded) {
        expand_button->set_opacity(1.0);
    } else {
        expand_button->set_opacity(0.0); // shown once truncation is measured
        expand_button->set_sensitive(false);
        reveal_when_truncated(expand_button, summary, body);
    }
    buttons->append(*expand_button);

    auto* delete_button = Gtk::make_managed<Gtk::Button>();
    delete_button->add_css_class("notif-icon-btn");
    delete_button->set_tooltip_text("Delete notification");
    auto* trash = Gtk::make_managed<Gtk::Label>(kIconTrash);
    trash->add_css_class("notif-icon-glyph");
    delete_button->set_child(*trash);
    {
        const std::string id = n.id;
        delete_button->signal_clicked().connect(
            [id] { NotificationService::get().remove_from_history(id); });
    }
    buttons->append(*delete_button);
    card->append(*buttons);

    // clicking the card invokes its default action, else focuses the sender's
    // window (button clicks claim their sequence first, so they don't trigger this)
    bool has_default = false;
    for (const auto& action : n.actions)
        has_default = has_default || action.key == "default";
    const std::string id = n.id, app_name = n.app_name;
    auto click = Gtk::GestureClick::create();
    click->signal_released().connect([this, id, app_name, has_default](int, double, double) {
        auto& service = NotificationService::get();
        if (!(has_default && service.invoke_action(id, "default")))
            service.focus_sender_window(app_name);
        request_close_.emit();
    });
    card->add_controller(click);

    list_.append(*card);
}

} // namespace hyprshell
