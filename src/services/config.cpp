#include "services/config.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace hyprshell {

Config& Config::get() {
    static Config instance;
    return instance;
}

Config::Config() {
    path_ = Glib::build_filename(Glib::get_user_config_dir(), "hypr-shell", "config.json");

    // Initial read is synchronous: a tiny local file, needed before the first
    // frame so the bar doesn't flash defaults. Reloads are async (FileMonitor).
    load();

    auto file = Gio::File::create_for_path(path_);
    monitor_ = file->monitor_file();
    monitor_->signal_changed().connect(
        [this](const Glib::RefPtr<Gio::File>&, const Glib::RefPtr<Gio::File>&,
               Gio::FileMonitor::Event event) {
            if (event == Gio::FileMonitor::Event::CHANGES_DONE_HINT ||
                event == Gio::FileMonitor::Event::CREATED ||
                event == Gio::FileMonitor::Event::DELETED) {
                load();
                changed_.emit();
            }
        });
}

void Config::load() {
    bar_position_ = BarPosition::Top;
    bar_height_ = 0;
    modules_.clear();

    std::string data;
    try {
        data = Glib::file_get_contents(path_);
    } catch (const Glib::Error&) {
        return; // no config file — defaults
    }

    const json j = json::parse(data, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object()) {
        g_warning("config: %s is not a JSON object — using defaults", path_.c_str());
        return;
    }

    try {
        const json bar = j.value("bar", json::object());
        if (bar.value("position", "top") == std::string("bottom"))
            bar_position_ = BarPosition::Bottom;
        bar_height_ = bar.value("height", 0);
        if (auto it = bar.find("modules"); it != bar.end() && it->is_object())
            for (const auto& [name, v] : it->items())
                if (v.is_boolean())
                    modules_[name] = v.get<bool>();
    } catch (const json::exception& e) {
        g_warning("config: %s: %s — some values fall back to defaults",
                  path_.c_str(), e.what());
    }
}

bool Config::module_enabled(const std::string& name) const {
    auto it = modules_.find(name);
    return it == modules_.end() ? true : it->second;
}

} // namespace hyprshell
