#pragma once

#include "services/session_actions.hpp"

#include <gtkmm.h>

namespace hyprshell {

// Dropdown-style session menu (Noctalia's PowerButton list): one row per
// enabled action — glyph + label, shutdown tinted mError — shown in the bar
// session module's popover and behind the app menu's power button. Rebuilds
// itself when the config changes; the owner closes its popover and runs the
// action when signal_activate fires.
class SessionMenuList : public Gtk::Box {
public:
    SessionMenuList();

    sigc::signal<void(const SessionAction&)>& signal_activate() { return activate_; }

private:
    void rebuild();

    sigc::signal<void(const SessionAction&)> activate_;
};

} // namespace hyprshell
