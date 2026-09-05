#pragma once

#include <gtkmm.h>

namespace hyprshell {

// Clipboard bar button: a tabler clipboard glyph that toggles the clipboard
// history window through the application's "clipboard" action — the same one
// `hypr-shell --clipboard` triggers for keybinds. Hidden while clipboard
// history is disabled or cliphist is missing (no settings of its own).
class ClipboardModule : public Gtk::Box {
public:
    ClipboardModule();

private:
    void update();

    Gtk::Label icon_;
};

} // namespace hyprshell
