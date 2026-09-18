#pragma once

#include "bar/app_menu_panel.hpp"
#include "bar/session_menu.hpp"

#include "bar/modules/corner_target.hpp"

#include <gtkmm.h>

namespace hyprshell {

// App menu bar button — Noctalia's Launcher bar widget (rocket glyph by
// default; a preset glyph, the distro logo or a custom icon via
// bar.app_menu.icon, optionally with a text label) that opens the grid app
// menu popover. A right click opens the session menu (bar.app_menu.
// right_click_session): a dropdown here, or the fullscreen session window
// through the app's "session" action when session.mode is "fullscreen".
class AppMenu : public Gtk::Box, public CornerTarget {
public:
    void activate_corner(bool) override { toggle(); }
    void secondary_corner(bool) override { open_session_menu(); }
    AppMenu();
    ~AppMenu() override;

    // Open/close from a keybind (`hypr-shell --app-menu`); no-op while the
    // module is disabled or the bar is hidden.
    void toggle();

private:
    void apply_config();
    void open();
    void open_session_menu();

    Gtk::Overlay anchor_; // popover anchor — never the module box itself
    Gtk::Box content_{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Label glyph_;
    Gtk::Image image_;
    Gtk::Label label_;
    Gtk::Popover popover_;
    AppMenuPanel* panel_ = nullptr;
    Gtk::Popover session_popover_; // right-click dropdown (session.mode = dropdown)
    SessionMenuList* session_list_ = nullptr;
};

} // namespace hyprshell
