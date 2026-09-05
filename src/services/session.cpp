#include "services/session.hpp"

#include "services/config.hpp"
#include "services/hyprland.hpp"

#include <glibmm.h>

namespace hyprshell {

std::vector<const SessionAction*> enabled_session_actions() {
    std::vector<const SessionAction*> out;
    const auto& cfg = Config::get().session();
    for (const auto* action : session_actions_in_order(cfg.order))
        if (cfg.item_enabled(action->key, action->default_on))
            out.push_back(action);
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

// hypr-shell-settings installs next to hypr-shell (~/.local/bin), which the
// session PATH the shell is autostarted with does not contain — a bare name
// found nothing from the app menu / control center buttons. Look beside our
// own executable first, PATH second.
std::string settings_executable() {
    static const std::string resolved = [] {
        std::string result = "hypr-shell-settings";
        if (gchar* self = g_file_read_link("/proc/self/exe", nullptr)) {
            gchar* dir = g_path_get_dirname(self);
            const std::string sibling = std::string(dir) + "/hypr-shell-settings";
            if (g_file_test(sibling.c_str(), G_FILE_TEST_IS_EXECUTABLE))
                result = sibling;
            g_free(dir);
            g_free(self);
        }
        return result;
    }();
    return resolved;
}

void open_settings(const std::string& page) {
    if (page.empty())
        spawn_detached({settings_executable()});
    else
        spawn_detached({"env", "HS_SETTINGS_PAGE=" + page, settings_executable()});
}

} // namespace hyprshell
