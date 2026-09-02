#pragma once

#include <gtkmm.h>

namespace hyprshell {

// Launcher bar button: a search glyph (the user's pick over Noctalia's rocket)
// that toggles the app launcher. The launcher window is owned by the App, so
// the click goes through the application's "launcher" action — the same one
// `hypr-shell --launcher` triggers for keybinds.
class Launcher : public Gtk::Box {
public:
    Launcher();

private:
    Gtk::Label icon_;
};

} // namespace hyprshell
