#pragma once

#include <giomm.h>
#include <gtkmm.h>

#include <functional>
#include <string>
#include <vector>

namespace hyprshell {

// App launcher, Noctalia's overlay-layer launcher in list view: a fullscreen
// overlay window with a dimmed click-to-close backdrop and a centered panel —
// search input, result list, result-count footer. Providers: applications and
// the inline calculator (always on) plus settings search, session commands
// and web search (launcher.* config toggles). Toggled from the bar module or
// `hypr-shell --launcher` (bind that in hyprland.conf, e.g.
// `bind = SUPER, SPACE, exec, hypr-shell --launcher`).
class LauncherWindow : public Gtk::ApplicationWindow {
public:
    LauncherWindow();

    void toggle();
    void open();
    void close_launcher();

private:
    struct Result {
        std::string name;
        std::string description;
        std::string glyph;              // tabler glyph; empty -> use gicon
        Glib::RefPtr<Gio::Icon> gicon;  // themed app icon
        std::string app_id;             // non-empty -> pinnable application
        double score = 0.0;
        std::function<void()> activate; // runs after the window is hidden
    };

    void update_results();
    void apply_panel_layout(); // fixed height vs Spotlight-style grow-to-fit
    void animate_list_height(int target); // Spotlight mode: smooth downward growth
    void rebuild_rows();
    void select(int index, bool scroll_into_view);
    void activate_index(int index);
    bool on_key_pressed(guint keyval, guint keycode, Gdk::ModifierType state);

    // providers
    void add_app_results(const std::string& query);
    void add_calc_result(const std::string& query);
    void add_settings_results(const std::string& query);
    void add_session_results(const std::string& query);
    void add_web_result(const std::string& query);

    Gtk::Overlay overlay_;
    Gtk::Box backdrop_;
    Gtk::Box panel_{Gtk::Orientation::VERTICAL, 13};
    Gtk::Entry search_;
    Gtk::ScrolledWindow scroller_;
    Gtk::Box list_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Box footer_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Separator divider_{Gtk::Orientation::HORIZONTAL};
    Gtk::Label count_label_;

    std::vector<Result> results_;
    std::vector<Gtk::Widget*> rows_;
    int selected_ = 0;

    // screen-derived panel metrics (defaults until the first allocation)
    int panel_width_ = 0;
    int panel_max_height_ = 0;
    int panel_top_margin_ = 0;   // Spotlight mode: fixed top edge of the panel
    int spotlight_list_max_ = 0; // Spotlight mode: cap for the animated list

    // list height animation (Spotlight mode)
    bool list_anim_running_ = false;
    int list_anim_from_ = 0;
    int list_anim_target_ = 0;
    gint64 list_anim_start_us_ = 0;

    // Noctalia's ignoreMouseHover: hovering only selects rows once the mouse
    // actually moved after the panel opened (otherwise the row under the
    // resting pointer would steal the selection).
    bool mouse_active_ = false;
    bool mouse_primed_ = false;
    double mouse_x_ = 0.0, mouse_y_ = 0.0;
};

} // namespace hyprshell
