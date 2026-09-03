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
    if (action.command == nullptr || action.command[0] == '\0')
        Hyprland::get().dispatch("hl.dsp.exit()");
    else
        spawn_detached({"sh", "-c", action.command});
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
