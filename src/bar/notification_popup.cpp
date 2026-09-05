#include "bar/notification_popup.hpp"
#include "services/theme.hpp"

#include "bar/notification_ui.hpp"
#include "services/config.hpp"

#include <gtk4-layer-shell.h>

namespace hyprshell {

namespace {

constexpr const char* kIconClose = "\uEB55"; // tabler x

// popup card colors (the shared Noctalia color snapshot)
struct Rgb {
    double r, g, b;
};
Rgb urgency_color(int urgency) {
    const Gdk::RGBA c = Theme::get().rgba(urgency == 2 ? "mError"        // critical
                                          : urgency == 0 ? "mOnSurface" // low
                                                         : "mPrimary");
    return {c.get_red(), c.get_green(), c.get_blue()};
}

} // namespace

NotificationPopups::NotificationPopups() {
    set_decorated(false);
    add_css_class("notification-popups");

    // Layer-shell must be configured before the window is mapped. The default
    // exclusive zone (0) keeps the stack out of the bar's reserved strip.
    auto* window = GTK_WINDOW(gobj());
    gtk_layer_init_for_window(window);
    gtk_layer_set_namespace(window, "hypr-shell-notifications");

    stack_.add_css_class("notif-popup-stack");
    set_child(stack_);

    auto& service = NotificationService::get();
    service.signal_popups_changed().connect(
        sigc::mem_fun(*this, &NotificationPopups::rebuild));
    service.signal_popup_progress().connect(
        sigc::mem_fun(*this, &NotificationPopups::redraw_progress));
    Config::get().signal_changed().connect([this] {
        apply_config();
        rebuild();
    });
    apply_config();
}

void NotificationPopups::apply_config() {
    const auto& nc = Config::get().notifications();
    auto* window = GTK_WINDOW(gobj());
    using L = Config::Notifications::Location;

    gtk_layer_set_layer(window, nc.overlay_layer ? GTK_LAYER_SHELL_LAYER_OVERLAY
                                                 : GTK_LAYER_SHELL_LAYER_TOP);
    const bool top = nc.location == L::Top || nc.location == L::TopLeft ||
                     nc.location == L::TopRight;
    const bool left = nc.location == L::TopLeft || nc.location == L::BottomLeft;
    const bool right = nc.location == L::TopRight || nc.location == L::BottomRight;
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, top);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, !top);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, left);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, right);
    for (auto edge : {GTK_LAYER_SHELL_EDGE_TOP, GTK_LAYER_SHELL_EDGE_BOTTOM,
                      GTK_LAYER_SHELL_EDGE_LEFT, GTK_LAYER_SHELL_EDGE_RIGHT})
        gtk_layer_set_margin(window, edge, 9);

    // notifications.background_opacity tints only the card surface, like
    // Noctalia (content stays opaque); locale-proof alpha via g_ascii_dtostr
    if (!opacity_provider_) {
        opacity_provider_ = Gtk::CssProvider::create();
        Gtk::StyleProvider::add_provider_for_display(
            Gdk::Display::get_default(), opacity_provider_,
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    }
    char buf[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_dtostr(buf, sizeof buf, nc.background_opacity);
    opacity_provider_->load_from_data(Glib::ustring::compose(
        ".notif-popup { background-color: alpha(#131316, %1); "
        "border-color: alpha(#46464f, %1); }",
        buf));
}

void NotificationPopups::rebuild() {
    auto& service = NotificationService::get();
    const auto& nc = Config::get().notifications();

    progress_bars_.clear();
    while (auto* child = stack_.get_first_child())
        stack_.remove(*child);

    if (service.popups().empty()) {
        set_visible(false);
        return;
    }

    const bool compact = nc.density == Config::Notifications::Density::Compact;
    stack_.set_size_request(compact ? 320 : 440, -1);
    for (const auto& popup : service.popups())
        add_card(popup);

    if (!get_visible())
        present();
}

void NotificationPopups::add_card(const NotificationService::Popup& popup) {
    const auto& n = popup.n;
    const bool compact =
        Config::get().notifications().density == Config::Notifications::Density::Compact;

    auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    card->add_css_class("notif-popup");
    card->set_overflow(Gtk::Overflow::HIDDEN); // progress bar under the radius

    // 2px countdown, shrinking from both ends like Noctalia's progressBar
    auto* progress = Gtk::make_managed<Gtk::DrawingArea>();
    progress->set_content_height(2);
    const std::string id = n.id;
    const int urgency = n.urgency;
    progress->set_draw_func([id, urgency](const Cairo::RefPtr<Cairo::Context>& cr,
                                          int width, int height) {
        double value = 0.0;
        for (const auto& p : NotificationService::get().popups())
            if (p.n.id == id)
                value = p.duration_ms < 0 ? 1.0 : p.progress;
        constexpr double radius = 20.0; // matches the card's border radius
        const double full = std::max(0.0, width - 2 * radius);
        const auto [r, g, b] = urgency_color(urgency);
        cr->set_source_rgb(r, g, b);
        cr->rectangle(radius + full * (1.0 - value) / 2.0, 0, full * value, height);
        cr->fill();
    });
    card->append(*progress);
    progress_bars_.emplace_back(n.id, progress);

    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL,
                                            compact ? 6 : 12);
    row->add_css_class(compact ? "notif-popup-body-compact" : "notif-popup-content");

    auto* icon = make_notification_icon(n, compact ? 24 : 40);
    icon->set_valign(Gtk::Align::CENTER);
    row->append(*icon);

    auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 3);
    content->set_hexpand(true);
    content->set_valign(Gtk::Align::CENTER);

    if (!compact) {
        // header: urgency dot + app name + relative time (dot always shown here,
        // primary-colored for normal urgency, like the popup in Noctalia)
        auto* meta = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto* dot = Gtk::make_managed<Gtk::Box>();
        dot->add_css_class("notif-dot");
        dot->add_css_class(n.urgency == 2   ? "critical"
                           : n.urgency == 0 ? "low"
                                            : "normal");
        dot->set_valign(Gtk::Align::CENTER);
        meta->append(*dot);
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
        // keep the header text clear of the close button
        meta->set_margin_end(24);
        content->append(*meta);
    }

    auto make_text = [](const std::string& markup, const char* css, int lines,
                        int max_chars) {
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_markup(markup);
        label->add_css_class(css);
        label->set_xalign(0.0f);
        label->set_halign(Gtk::Align::START);
        label->set_wrap(true);
        label->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
        label->set_ellipsize(Pango::EllipsizeMode::END);
        label->set_lines(lines);
        // Cap the natural width so the layer surface stays at the stack's
        // requested width — but keep it realistic: a tiny cap (e.g. 1) makes
        // GTK measure the height at that sliver of width and the card
        // reserves `lines` rows even for one line of text.
        label->set_max_width_chars(max_chars);
        return label;
    };
    content->append(*make_text(n.summary.empty() ? "No summary" : n.summary,
                               "notif-popup-summary", compact ? 1 : 3,
                               compact ? 30 : 40));
    if (!n.body.empty())
        content->append(*make_text(n.body, compact ? "notif-body" : "notif-popup-text",
                                   compact ? 2 : 5, compact ? 32 : 40));

    if (!compact && !n.actions.empty()) {
        auto* actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        actions->set_margin_top(3);
        for (const auto& action : n.actions) {
            auto* button = Gtk::make_managed<Gtk::Button>(action.text);
            button->add_css_class("notif-action");
            const std::string key = action.key;
            button->signal_clicked().connect([id, key] {
                auto& service = NotificationService::get();
                if (service.invoke_action(id, key))
                    service.dismiss_popup(id);
            });
            actions->append(*button);
        }
        content->append(*actions);
    }
    row->append(*content);

    if (compact) {
        card->append(*row);
    } else {
        // close button floats at the card's top-right (Noctalia's NIconButton)
        auto* overlay = Gtk::make_managed<Gtk::Overlay>();
        overlay->set_child(*row);
        auto* close = Gtk::make_managed<Gtk::Button>();
        close->add_css_class("notif-icon-btn");
        close->set_halign(Gtk::Align::END);
        close->set_valign(Gtk::Align::START);
        close->set_margin_top(9);
        close->set_margin_end(9);
        close->set_tooltip_text("Dismiss");
        auto* x = Gtk::make_managed<Gtk::Label>(kIconClose);
        x->add_css_class("notif-icon-glyph");
        close->set_child(*x);
        close->signal_clicked().connect([id] {
            auto& service = NotificationService::get();
            if (Config::get().notifications().clear_dismissed)
                service.remove_from_history(id);
            service.dismiss_popup(id);
        });
        overlay->add_overlay(*close);
        card->append(*overlay);
    }

    // hover holds the countdown (Noctalia's pause/resumeTimeout)
    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_enter().connect(
        [id](double, double) { NotificationService::get().pause_popup(id); });
    motion->signal_leave().connect(
        [id] { NotificationService::get().resume_popup(id); });
    card->add_controller(motion);

    // click activates: default action, else focus the sender's window
    const std::string app_name = n.app_name;
    bool has_default = false;
    for (const auto& action : n.actions)
        has_default = has_default || action.key == "default";
    auto click = Gtk::GestureClick::create();
    click->signal_released().connect([id, app_name, has_default](int, double, double) {
        auto& service = NotificationService::get();
        if (!(has_default && service.invoke_action(id, "default")))
            service.focus_sender_window(app_name);
        service.dismiss_popup(id);
    });
    card->add_controller(click);

    // right-click dismisses (and per clear_dismissed deletes the history entry)
    auto right_click = Gtk::GestureClick::create();
    right_click->set_button(GDK_BUTTON_SECONDARY);
    right_click->signal_released().connect([id](int, double, double) {
        auto& service = NotificationService::get();
        if (Config::get().notifications().clear_dismissed)
            service.remove_from_history(id);
        service.dismiss_popup(id);
    });
    card->add_controller(right_click);

    stack_.append(*card);
}

void NotificationPopups::redraw_progress() {
    for (auto& [id, area] : progress_bars_)
        area->queue_draw();
}

} // namespace hyprshell
