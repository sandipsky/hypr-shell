#pragma once

#include "bar/app_menu_panel.hpp"

#include <gtkmm.h>

namespace hyprshell {

// App menu bar button — Noctalia's Launcher bar widget (rocket glyph by
// default; a preset glyph, the distro logo or a custom icon via
// bar.app_menu.icon, optionally with a text label) that opens the grid app
// menu popover.
class AppMenu : public Gtk::Box {
public:
    AppMenu();
    ~AppMenu() override;

    // Open/close from a keybind (`hypr-shell --app-menu`); no-op while the
    // module is disabled or the bar is hidden.
    void toggle();

private:
    void apply_config();
    void open();

    Gtk::Overlay anchor_; // popover anchor — never the module box itself
    Gtk::Box content_{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Label glyph_;
    Gtk::Image image_;
    Gtk::Label label_;
    Gtk::Popover popover_;
    AppMenuPanel* panel_ = nullptr;
};

} // namespace hyprshell
