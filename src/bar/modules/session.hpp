#pragma once

#include "bar/session_menu.hpp"

#include <gtkmm.h>

namespace hyprshell {

// Session bar button — Noctalia's SessionMenu widget (power glyph). Click
// opens the session menu: a dropdown popover here, or the fullscreen session
// window (through the app's "session" action) when session.mode is
// "fullscreen".
class Session : public Gtk::Box {
public:
    Session();
    ~Session() override;

    // `hypr-shell --session` in dropdown mode; no-op while the module is
    // disabled or the bar is hidden.
    void toggle();

private:
    void open();

    Gtk::Label icon_;
    Gtk::Popover popover_;
    SessionMenuList* list_ = nullptr;
};

} // namespace hyprshell
