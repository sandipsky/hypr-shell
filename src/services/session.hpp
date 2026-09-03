#pragma once

#include "services/session_actions.hpp"

#include <string>
#include <vector>

namespace hyprshell {

// Session menu backend shared by the bar's session module, the app menu's
// power button, the fullscreen session window and the launcher's session
// search, plus the detached-spawn helpers every "run something else" path
// uses. There is no lock screen of our own yet (phase 5): lock goes through
// logind.

// The actions session.items (config) currently enables, in menu order.
// Pointers into kSessionActions — valid for the process lifetime.
std::vector<const SessionAction*> enabled_session_actions();
void run_session_action(const SessionAction& action);

// Spawns argv detached (PATH lookup, output discarded); failures are logged.
void spawn_detached(const std::vector<std::string>& argv);

// Opens hypr-shell-settings, on a page when `page` is an HS_SETTINGS_PAGE
// tag (a Bar module subpage tag, "launcher_page", "session_page" or
// "notifications_page").
void open_settings(const std::string& page = "");

} // namespace hyprshell
