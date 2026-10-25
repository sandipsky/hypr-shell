#pragma once

#include "services/session_actions.hpp"

#include <sigc++/sigc++.h>

#include <string>
#include <vector>

namespace hyprshell {

// Session menu backend shared by the bar's session module, the app menu's
// power button, the fullscreen session window and the launcher's session
// search, plus the detached-spawn helpers every "run something else" path
// uses, and the lock hooks: services (idle, session actions) request a lock
// through request_lock(); the lock screen (bar/lock_screen) answers it and
// reports its state back, so services never depend on UI code.

// The actions session.items (config) currently enables, in session.order.
// Pointers into kSessionActions — valid for the process lifetime.
std::vector<const SessionAction*> enabled_session_actions();
void run_session_action(const SessionAction& action);

// Ask the lock screen to lock (idle daemon, session menus, `hypr-shell --lock`,
// logind's Lock signal). No-op until the lock screen connects.
void request_lock();
// Lock, then suspend once the lock screen confirms (3 s cap) — Noctalia's
// lockAndSuspend, for the lid switch (`hypr-shell --lock-and-suspend`).
void lock_and_suspend();
sigc::signal<void()>& signal_lock_requested();
// Lock screen → services: the session lock was acquired / released.
void set_session_locked(bool locked);
bool session_locked();
sigc::signal<void(bool)>& signal_session_locked();

// Spawns argv detached (PATH lookup, output discarded); failures are logged.
void spawn_detached(const std::vector<std::string>& argv);

// Opens hypr-shell-settings, on a page when `page` is an HS_SETTINGS_PAGE
// tag (a Bar module subpage tag, "launcher_page", "session_page" or
// "notifications_page").
void open_settings(const std::string& page = "");

} // namespace hyprshell
