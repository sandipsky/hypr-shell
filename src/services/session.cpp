#include "services/session.hpp"

#include "services/config.hpp"
#include "services/hyprland.hpp"

#include <glibmm.h>

namespace hyprshell {

std::vector<const SessionAction*> enabled_session_actions() {
    std::vector<const SessionAction*> out;
    const auto& cfg = Config::get().session();
    for (const auto& action : kSessionActions)
        if (cfg.item_enabled(action.key, action.default_on))
            out.push_back(&action);
    return out;
}

void run_session_action(const SessionAction& action) {
    const std::string key = action.key;
    if (key == "lock")
        request_lock();
    else if (action.command == nullptr || action.command[0] == '\0')
        Hyprland::get().dispatch("hl.dsp.exit()"); // logout
    else
        spawn_detached({"sh", "-c", action.command});
}

namespace {
sigc::signal<void()> lock_requested_signal;
sigc::signal<void(bool)> session_locked_signal;
bool session_locked_state = false;
} // namespace

void request_lock() {
    if (lock_requested_signal.empty())
        g_warning("lock requested but no lock screen is connected");
    lock_requested_signal.emit();
}

sigc::signal<void()>& signal_lock_requested() {
    return lock_requested_signal;
}

void set_session_locked(bool locked) {
    if (session_locked_state == locked)
        return;
    session_locked_state = locked;
    session_locked_signal.emit(locked);
}

bool session_locked() {
    return session_locked_state;
}

sigc::signal<void(bool)>& signal_session_locked() {
    return session_locked_signal;
}

void lock_and_suspend() {
    static sigc::connection locked_connection;
    static sigc::connection timeout;
    const auto suspend = [] {
        locked_connection.disconnect();
        timeout.disconnect();
        spawn_detached({"sh", "-c", "systemctl suspend || loginctl suspend"});
    };
    if (session_locked()) {
        suspend();
        return;
    }
    request_lock();
    locked_connection.disconnect();
    timeout.disconnect();
    locked_connection = session_locked_signal.connect([suspend](bool locked) {
        if (locked)
            suspend();
    });
    timeout = Glib::signal_timeout().connect(
        [suspend] {
            g_warning("lock screen did not confirm the lock — suspending anyway");
            suspend();
            return false;
        },
        3000);
}

void spawn_detached(const std::vector<std::string>& argv) {
    if (argv.empty())
        return;
    try {
        Glib::spawn_async("", argv,
                          Glib::SpawnFlags::SEARCH_PATH |
                              Glib::SpawnFlags::STDOUT_TO_DEV_NULL |
                              Glib::SpawnFlags::STDERR_TO_DEV_NULL);
    } catch (const Glib::Error& e) {
        g_warning("spawning %s failed: %s", argv[0].c_str(), e.what());
    }
}

void open_settings(const std::string& page) {
    if (page.empty())
        spawn_detached({"hypr-shell-settings"});
    else
        spawn_detached({"env", "HS_SETTINGS_PAGE=" + page, "hypr-shell-settings"});
}

} // namespace hyprshell
