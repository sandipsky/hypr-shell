#include "services/config.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

using json = nlohmann::json;

namespace hyprshell {

namespace {

// Canonical module list with default sections; also the fallback order for
// modules a hand-edited bar.layout forgot to mention.
constexpr std::pair<const char*, Config::BarSection> kKnownModules[] = {
    {"workspaces", Config::BarSection::Left},
    {"active_window", Config::BarSection::Center},
    {"network", Config::BarSection::Right},
    {"bluetooth", Config::BarSection::Right},
    {"volume", Config::BarSection::Right},
    {"battery", Config::BarSection::Right},
    {"clock", Config::BarSection::Right},
};

bool known_module(const std::string& name) {
    for (const auto& [key, section] : kKnownModules)
        if (name == key)
            return true;
    return false;
}

} // namespace

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
    bar_visibility_ = BarVisibility::Visible;
    bar_show_on_ws_switch_ = true;
    bar_show_when_ws_empty_ = false;
    bar_background_opacity_ = 0.88;
    modules_.clear();
    for (auto& section : layout_)
        section.clear();
    workspaces_mode_ = WorkspacesMode::Dynamic;
    workspaces_fixed_count_ = 5;
    workspaces_scroll_wrap_ = true;
    bt_auto_connect_ = false;
    battery_show_profiles_ = true;
    battery_show_brightness_ = true;
    battery_show_refresh_ = true;
    clock_first_day_of_week_ = 0;
    clock_format_horizontal_ = "%H:%M %a, %b %d";
    clock_format_vertical_ = "%H %M";
    aw_hide_ = ActiveWindowHide::Hidden;
    aw_show_title_ = true;
    aw_title_mode_ = ActiveWindowText::Title;
    aw_empty_ = ActiveWindowEmpty::Default;
    aw_show_icon_ = true;
    // deferred to the end of load() so the fallback runs for every early return
    struct FillUnplaced {
        std::array<std::vector<std::string>, 3>& layout;
        ~FillUnplaced() {
            for (const auto& [key, section] : kKnownModules) {
                bool placed = false;
                for (const auto& list : layout)
                    placed = placed || std::find(list.begin(), list.end(), key) != list.end();
                if (!placed)
                    layout[static_cast<std::size_t>(section)].emplace_back(key);
            }
        }
    } fill_unplaced{layout_};

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
        const std::string position = bar.value("position", "top");
        if (position == "bottom")
            bar_position_ = BarPosition::Bottom;
        else if (position == "left")
            bar_position_ = BarPosition::Left;
        else if (position == "right")
            bar_position_ = BarPosition::Right;
        const std::string visibility = bar.value("visibility", "visible");
        if (visibility == "hidden")
            bar_visibility_ = BarVisibility::Hidden;
        else if (visibility == "auto_hide")
            bar_visibility_ = BarVisibility::AutoHide;
        bar_show_on_ws_switch_ = bar.value("show_on_workspace_switch", true);
        bar_show_when_ws_empty_ = bar.value("show_when_workspace_empty", false);
        bar_background_opacity_ =
            std::clamp(bar.value("background_opacity", 0.88), 0.0, 1.0);
        if (auto it = bar.find("modules"); it != bar.end() && it->is_object())
            for (const auto& [name, v] : it->items())
                if (v.is_boolean())
                    modules_[name] = v.get<bool>();
        if (auto it = bar.find("workspaces"); it != bar.end() && it->is_object()) {
            if (it->value("mode", "dynamic") == std::string("fixed"))
                workspaces_mode_ = WorkspacesMode::Fixed;
            workspaces_fixed_count_ = std::clamp(it->value("fixed_count", 5), 1, 50);
            workspaces_scroll_wrap_ = it->value("scroll_wrap", true);
        }
        if (auto it = bar.find("active_window"); it != bar.end() && it->is_object()) {
            const std::string hide = it->value("hide_mode", "hidden");
            aw_hide_ = hide == "visible"       ? ActiveWindowHide::Visible
                       : hide == "transparent" ? ActiveWindowHide::Transparent
                                               : ActiveWindowHide::Hidden;
            aw_show_title_ = it->value("show_title", true);
            aw_title_mode_ = it->value("title_mode", "title") == std::string("appname")
                                 ? ActiveWindowText::AppName
                                 : ActiveWindowText::Title;
            const std::string empty = it->value("no_window_text", "default");
            aw_empty_ = empty == "desktop" ? ActiveWindowEmpty::Desktop
                        : empty == "none"  ? ActiveWindowEmpty::None
                                           : ActiveWindowEmpty::Default;
            aw_show_icon_ = it->value("show_icon", true);
        }
        if (auto it = bar.find("bluetooth"); it != bar.end() && it->is_object()) {
            bt_auto_connect_ = it->value("auto_connect", false);
        }
        if (auto it = bar.find("battery"); it != bar.end() && it->is_object()) {
            battery_show_profiles_ = it->value("show_power_profiles", true);
            battery_show_brightness_ = it->value("show_brightness", true);
            battery_show_refresh_ = it->value("show_refresh_rate", true);
        }
        if (auto it = bar.find("clock"); it != bar.end() && it->is_object()) {
            clock_first_day_of_week_ = std::clamp(it->value("first_day_of_week", 0), 0, 1);
            clock_format_horizontal_ = it->value("format_horizontal", clock_format_horizontal_);
            clock_format_vertical_ = it->value("format_vertical", clock_format_vertical_);
        }
        if (auto it = bar.find("layout"); it != bar.end() && it->is_object()) {
            constexpr const char* kSectionKeys[] = {"left", "center", "right"};
            for (std::size_t i = 0; i < 3; ++i) {
                const auto list = it->find(kSectionKeys[i]);
                if (list == it->end() || !list->is_array())
                    continue;
                for (const auto& entry : *list) {
                    if (!entry.is_string())
                        continue;
                    const auto name = entry.get<std::string>();
                    bool placed = false;
                    for (const auto& section : layout_)
                        placed = placed ||
                                 std::find(section.begin(), section.end(), name) != section.end();
                    if (known_module(name) && !placed)
                        layout_[i].push_back(name);
                }
            }
        }
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
