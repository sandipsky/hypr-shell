#include "bar/keybindings_window.hpp"

#include "bar/frame_probe.hpp"
#include "services/hyprland.hpp"

#include <gtk4-layer-shell.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace hyprshell {

namespace {

// panel: max(42% of the screen, 780) × max(72%, 600), like the launcher's
// screen-derived box
constexpr int kMinPanelWidth = 780;
constexpr int kMinPanelHeight = 600;
constexpr int kKeysColumnWidth = 300; // keycaps column, so descriptions line up

std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string word;
    while (in >> word)
        out.push_back(word);
    return out;
}

} // namespace

KeybindingsWindow::KeybindingsWindow() {
    set_decorated(false);
    add_css_class("launcher");
    add_css_class("keybindings");

    // Layer-shell before mapping: fullscreen overlay with exclusive keyboard
    // focus (the filter entry types over any window), like the launcher.
    auto* window = GTK_WINDOW(gobj());
    gtk_layer_init_for_window(window);
    gtk_layer_set_namespace(window, "hypr-shell-keybindings");
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
    gtk_layer_set_exclusive_zone(window, -1);
    for (auto edge : {GTK_LAYER_SHELL_EDGE_TOP, GTK_LAYER_SHELL_EDGE_BOTTOM, GTK_LAYER_SHELL_EDGE_LEFT,
                      GTK_LAYER_SHELL_EDGE_RIGHT})
        gtk_layer_set_anchor(window, edge, true);

    auto backdrop_click = Gtk::GestureClick::create();
    backdrop_click->signal_released().connect([this](int, double, double) { close_window(); });
    backdrop_.add_controller(backdrop_click);

    panel_.add_css_class("launcher-panel");
    panel_.add_css_class("keybindings-panel");
    panel_.set_halign(Gtk::Align::CENTER);
    panel_.set_valign(Gtk::Align::CENTER);
    panel_.set_size_request(kMinPanelWidth, kMinPanelHeight);

    auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    header->set_margin_start(6);
    header->set_margin_end(6);
    header->set_margin_top(4);
    title_.set_text("Keyboard shortcuts");
    title_.add_css_class("kb-title");
    title_.set_halign(Gtk::Align::START);
    header->append(title_);
    subtitle_.set_text("Hyprland key bindings");
    subtitle_.add_css_class("kb-subtitle");
    subtitle_.set_halign(Gtk::Align::START);
    header->append(subtitle_);
    panel_.append(*header);

    search_.add_css_class("launcher-search");
    search_.set_placeholder_text("Filter shortcuts...");
    search_.signal_changed().connect([this] {
        if (get_visible())
            apply_filter();
    });
    panel_.append(search_);

    scroller_.set_child(list_);
    scroller_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroller_.set_overlay_scrolling(false);
    scroller_.set_vexpand(true);
    panel_.append(scroller_);

    status_.add_css_class("kb-status");
    status_.set_wrap(true);
    status_.set_justify(Gtk::Justification::CENTER);
    status_.set_margin_top(40);
    status_.set_visible(false);
    list_.append(status_);

    auto* footer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    divider_.add_css_class("launcher-divider");
    footer->append(divider_);
    count_label_.add_css_class("launcher-count");
    count_label_.set_justify(Gtk::Justification::CENTER);
    footer->append(count_label_);
    panel_.append(*footer);

    overlay_.set_child(backdrop_);
    overlay_.add_overlay(panel_);
    set_child(overlay_);

    auto key = Gtk::EventControllerKey::create();
    key->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    key->signal_key_pressed().connect(sigc::mem_fun(*this, &KeybindingsWindow::on_key_pressed),
                                      false);
    add_controller(key);

    // binds change with the config: refetch on the next open after a reload
    Hyprland::get().signal_event().connect([this](const std::string& name, const std::string&) {
        if (name == "configreloaded" && get_visible())
            refresh();
    });
}

void KeybindingsWindow::toggle() {
    if (get_visible())
        close_window();
    else
        open();
}

void KeybindingsWindow::open() {
    if (get_visible())
        return;
    search_.set_text("");
    refresh();
    log_first_frame(*this, "keybindings"); // HS_FRAME_DEBUG
    present();
    search_.grab_focus();
    // the window spans the output, so its own size is the screen's — known
    // only after the first allocation
    add_tick_callback([this](const Glib::RefPtr<Gdk::FrameClock>&) {
        if (get_width() <= 1)
            return true;
        panel_.set_size_request(std::max(kMinPanelWidth, get_width() * 42 / 100),
                                std::max(kMinPanelHeight, get_height() * 72 / 100));
        return false;
    });
}

void KeybindingsWindow::close_window() {
    set_visible(false);
}

void KeybindingsWindow::refresh() {
    const unsigned serial = ++fetch_serial_;
    loading_ = true;
    binds_.clear();
    rebuild_rows();
    fetch_keybinds([this, serial](std::vector<Keybind> binds) {
        if (serial != fetch_serial_)
            return;
        loading_ = false;
        binds_ = std::move(binds);
        rebuild_rows();
    });
}

Gtk::Widget* KeybindingsWindow::make_row(const Keybind& bind) {
    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 14);
    row->add_css_class("kb-row");

    auto* keys = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    keys->set_size_request(kKeysColumnWidth, -1);
    keys->set_valign(Gtk::Align::CENTER);
    std::vector<std::string> caps = bind.mods;
    caps.push_back(bind.key);
    for (std::size_t i = 0; i < caps.size(); ++i) {
        if (i > 0) {
            auto* plus = Gtk::make_managed<Gtk::Label>("+");
            plus->add_css_class("kb-plus");
            keys->append(*plus);
        }
        auto* cap = Gtk::make_managed<Gtk::Label>(caps[i]);
        cap->add_css_class("kb-key");
        keys->append(*cap);
    }
    row->append(*keys);

    auto* description = Gtk::make_managed<Gtk::Label>(bind.description);
    description->add_css_class("kb-desc");
    description->set_halign(Gtk::Align::START);
    description->set_hexpand(true);
    description->set_ellipsize(Pango::EllipsizeMode::END);
    description->set_xalign(0.0f);
    row->append(*description);

    if (bind.mouse || bind.locked) {
        auto* tag = Gtk::make_managed<Gtk::Label>(bind.mouse ? "mouse" : "works when locked");
        tag->add_css_class("kb-tag");
        tag->set_valign(Gtk::Align::CENTER);
        row->append(*tag);
    }
    return row;
}

void KeybindingsWindow::rebuild_rows() {
    for (auto& group : groups_) {
        list_.remove(*group.header);
        for (auto& row : group.rows)
            list_.remove(*row.widget);
    }
    groups_.clear();

    if (loading_) {
        status_.set_text("Loading…");
        status_.set_visible(true);
        count_label_.set_text("");
        return;
    }
    if (binds_.empty()) {
        status_.set_text(Hyprland::get().available()
                             ? "No key bindings found."
                             : "Hyprland is not running — key bindings are read from its IPC socket.");
        status_.set_visible(true);
        count_label_.set_text("");
        return;
    }
    status_.set_visible(false);

    // groups in order of first appearance (= the config's order)
    for (std::size_t i = 0; i < binds_.size(); ++i) {
        const auto& bind = binds_[i];
        auto it = std::find_if(groups_.begin(), groups_.end(), [&](const GroupWidget& g) {
            return static_cast<Gtk::Label*>(g.header)->get_text() == bind.group;
        });
        if (it == groups_.end()) {
            auto* header = Gtk::make_managed<Gtk::Label>(bind.group);
            header->add_css_class("kb-group");
            header->set_halign(Gtk::Align::START);
            header->set_xalign(0.0f);
            list_.append(*header);
            groups_.push_back({header, {}});
            it = groups_.end() - 1;
        }
        auto* row = make_row(bind);
        list_.append(*row);
        it->rows.push_back({row, i});
    }
    apply_filter();
}

void KeybindingsWindow::apply_filter() {
    const auto query = words(lowercase(search_.get_text()));
    std::size_t shown = 0;
    for (auto& group : groups_) {
        std::size_t visible = 0;
        for (auto& row : group.rows) {
            const auto& haystack = binds_[row.bind].search_text;
            bool match = true;
            for (const auto& word : query)
                if (haystack.find(word) == std::string::npos) {
                    match = false;
                    break;
                }
            row.widget->set_visible(match);
            if (match)
                ++visible;
        }
        group.header->set_visible(visible > 0);
        shown += visible;
    }
    if (binds_.empty())
        return;
    if (query.empty())
        count_label_.set_text(std::to_string(binds_.size()) +
                              (binds_.size() == 1 ? " shortcut" : " shortcuts"));
    else
        count_label_.set_text(shown == 0 ? "No matches"
                                         : std::to_string(shown) + " of " +
                                               std::to_string(binds_.size()) + " shortcuts");
    scroller_.get_vadjustment()->set_value(0);
}

void KeybindingsWindow::scroll_by(double delta) {
    auto adjustment = scroller_.get_vadjustment();
    adjustment->set_value(std::clamp(adjustment->get_value() + delta, adjustment->get_lower(),
                                     adjustment->get_upper() - adjustment->get_page_size()));
}

bool KeybindingsWindow::on_key_pressed(guint keyval, guint, Gdk::ModifierType) {
    switch (keyval) {
    case GDK_KEY_Escape:
        close_window();
        return true;
    case GDK_KEY_Down:
        scroll_by(48);
        return true;
    case GDK_KEY_Up:
        scroll_by(-48);
        return true;
    case GDK_KEY_Page_Down:
        scroll_by(scroller_.get_vadjustment()->get_page_size());
        return true;
    case GDK_KEY_Page_Up:
        scroll_by(-scroller_.get_vadjustment()->get_page_size());
        return true;
    default:
        return false; // the filter entry has it
    }
}

} // namespace hyprshell
