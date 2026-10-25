#pragma once

#include "services/session_actions.hpp"

#include <gtkmm.h>

#include <vector>

namespace hyprshell {

// Fullscreen session menu — Noctalia's largeButtonsStyle: a dimmed overlay
// layer window with 200px buttons (icon + label) in a single row or a grid
// (session.fullscreen_layout), keyboard-navigable (arrows/Tab, Enter, Esc,
// digits 1-9 pick an entry directly). Toggled by the app's "session" action
// when session.mode is "fullscreen" — from the bar module, the app menu's
// power button or `hypr-shell --session`.
class SessionWindow : public Gtk::ApplicationWindow {
public:
    SessionWindow();

    void toggle();
    void open();
    void close_menu();

private:
    void rebuild();
    void navigate(int dx, int dy); // Noctalia's navigateGrid
    void select(int index);
    void activate_index(int index);
    bool on_key_pressed(guint keyval, guint keycode, Gdk::ModifierType state);

    Gtk::Overlay overlay_;
    Gtk::Box backdrop_;
    Gtk::Grid grid_;

    std::vector<const SessionAction*> actions_;
    std::vector<Gtk::Widget*> buttons_;
    int selected_ = -1; // nothing highlighted until keyboard/mouse, like Noctalia
    int columns_ = 1;

    // Noctalia's ignoreMouseHover (see LauncherWindow)
    bool mouse_active_ = false;
    bool mouse_primed_ = false;
    double mouse_x_ = 0.0, mouse_y_ = 0.0;
};

} // namespace hyprshell
