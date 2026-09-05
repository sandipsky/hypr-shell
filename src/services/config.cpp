#include "services/config.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

using json = nlohmann::json;

namespace hyprshell {

namespace {

// Canonical module list with default sections; also the fallback order for
// modules a hand-edited bar.layout forgot to mention.
constexpr std::pair<const char*, Config::BarSection> kKnownModules[] = {
    {"launcher", Config::BarSection::Left},
    {"app_menu", Config::BarSection::Left},
    {"workspaces", Config::BarSection::Left},
    {"taskbar", Config::BarSection::Left},
    {"active_window", Config::BarSection::Center},
    {"network", Config::BarSection::Right},
    {"bluetooth", Config::BarSection::Right},
    {"control_center", Config::BarSection::Right},
    {"volume", Config::BarSection::Right},
    {"battery", Config::BarSection::Right},
    {"notifications", Config::BarSection::Right},
    {"clock", Config::BarSection::Right},
    {"session", Config::BarSection::Right},
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
    taskbar_ = Taskbar{};
    control_center_ = ControlCenter{};
    notif_show_badge_ = true;
    notif_hide_zero_ = false;
    notif_hide_zero_unread_ = false;
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
    notifications_ = Notifications{};
    launcher_ = Launcher{};
    app_menu_ = AppMenu{};
    session_ = Session{};
    idle_ = Idle{};
    lock_screen_ = LockScreen{};
    wallpaper_ = Wallpaper{};
    night_light_ = NightLight{};
    osd_ = Osd{};
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
        if (auto it = bar.find("control_center"); it != bar.end() && it->is_object()) {
            control_center_.show_media = it->value("show_media", true);
            control_center_.show_audio = it->value("show_audio", true);
            control_center_.show_brightness = it->value("show_brightness", false);
            control_center_.show_sysmon = it->value("show_sysmon", true);
        }
        if (auto it = bar.find("taskbar"); it != bar.end() && it->is_object()) {
            const std::string hide = it->value("hide_mode", "hidden");
            taskbar_.hide_mode = hide == "visible"       ? Taskbar::HideMode::Visible
                                 : hide == "transparent" ? Taskbar::HideMode::Transparent
                                                         : Taskbar::HideMode::Hidden;
            taskbar_.only_same_monitor = it->value("only_same_monitor", true);
            taskbar_.only_active_workspaces = it->value("only_active_workspaces", true);
            taskbar_.show_pinned_apps = it->value("show_pinned_apps", true);
            taskbar_.show_title = it->value("show_title", false);
            taskbar_.title_width = std::clamp(it->value("title_width", 120), 20, 600);
            taskbar_.smart_width = it->value("smart_width", true);
            taskbar_.max_width_percent = std::clamp(it->value("max_width_percent", 40), 10, 100);
            taskbar_.icon_scale = std::clamp(it->value("icon_scale", 0.8), 0.5, 1.0);
            taskbar_.item_gap = std::clamp(it->value("item_gap", 6), 0, 24);
        }
        if (auto it = bar.find("notifications"); it != bar.end() && it->is_object()) {
            notif_show_badge_ = it->value("show_unread_badge", true);
            notif_hide_zero_ = it->value("hide_when_zero", false);
            notif_hide_zero_unread_ = it->value("hide_when_zero_unread", false);
        }
        if (auto it = bar.find("battery"); it != bar.end() && it->is_object()) {
            battery_show_profiles_ = it->value("show_power_profiles", true);
            battery_show_brightness_ = it->value("show_brightness", true);
            battery_show_refresh_ = it->value("show_refresh_rate", true);
        }
        if (auto it = bar.find("app_menu"); it != bar.end() && it->is_object()) {
            auto& a = app_menu_;
            const std::string display = it->value("display", "icon");
            a.display = display == "icon_text" ? AppMenu::Display::IconText
                        : display == "text"    ? AppMenu::Display::Text
                                               : AppMenu::Display::Icon;
            a.icon = it->value("icon", a.icon);
            a.custom_icon = it->value("custom_icon", "");
            a.text = it->value("text", a.text);
            a.show_search = it->value("show_search", true);
            a.show_settings_button = it->value("show_settings_button", true);
            a.show_session_button = it->value("show_session_button", true);
            a.columns = std::clamp(it->value("columns", 5), 3, 8);
            a.multiline_labels = it->value("multiline_labels", false);
        }
        if (auto it = bar.find("clock"); it != bar.end() && it->is_object()) {
            clock_first_day_of_week_ = std::clamp(it->value("first_day_of_week", 0), 0, 1);
            clock_format_horizontal_ = it->value("format_horizontal", clock_format_horizontal_);
            clock_format_vertical_ = it->value("format_vertical", clock_format_vertical_);
        }
        if (auto it = j.find("notifications"); it != j.end() && it->is_object()) {
            auto& n = notifications_;
            n.enabled = it->value("enabled", true);
            n.do_not_disturb = it->value("do_not_disturb", false);
            if (it->value("density", "default") == std::string("compact"))
                n.density = Notifications::Density::Compact;
            const std::string location = it->value("location", "top_right");
            n.location = location == "top"          ? Notifications::Location::Top
                         : location == "top_left"   ? Notifications::Location::TopLeft
                         : location == "bottom"     ? Notifications::Location::Bottom
                         : location == "bottom_left" ? Notifications::Location::BottomLeft
                         : location == "bottom_right"
                             ? Notifications::Location::BottomRight
                             : Notifications::Location::TopRight;
            n.overlay_layer = it->value("overlay_layer", true);
            n.background_opacity =
                std::clamp(it->value("background_opacity", 1.0), 0.0, 1.0);
            n.respect_expire_timeout = it->value("respect_expire_timeout", false);
            n.low_duration_s = std::clamp(it->value("low_urgency_duration", 3), 1, 30);
            n.normal_duration_s =
                std::clamp(it->value("normal_urgency_duration", 8), 1, 30);
            n.critical_duration_s =
                std::clamp(it->value("critical_urgency_duration", 15), 1, 30);
            n.clear_dismissed = it->value("clear_dismissed", true);
            if (auto sub = it->find("save_to_history");
                sub != it->end() && sub->is_object()) {
                n.save_low = sub->value("low", true);
                n.save_normal = sub->value("normal", true);
                n.save_critical = sub->value("critical", true);
            }
            if (auto sub = it->find("sounds"); sub != it->end() && sub->is_object()) {
                n.sounds.enabled = sub->value("enabled", false);
                n.sounds.volume = std::clamp(sub->value("volume", 0.5), 0.0, 1.0);
                n.sounds.separate = sub->value("separate_sounds", false);
                n.sounds.low_file = sub->value("low_sound_file", "");
                n.sounds.normal_file = sub->value("normal_sound_file", "");
                n.sounds.critical_file = sub->value("critical_sound_file", "");
                n.sounds.excluded_apps =
                    sub->value("excluded_apps", n.sounds.excluded_apps);
            }
            if (auto sub = it->find("rules"); sub != it->end() && sub->is_array()) {
                for (const auto& entry : *sub) {
                    if (!entry.is_object())
                        continue;
                    Notifications::Rule rule;
                    rule.pattern = entry.value("pattern", "");
                    rule.action = entry.value("action", "block");
                    if (!rule.pattern.empty())
                        n.rules.push_back(std::move(rule));
                }
            }
        }
        if (auto it = j.find("launcher"); it != j.end() && it->is_object()) {
            auto& l = launcher_;
            l.enable_settings_search = it->value("enable_settings_search", true);
            l.enable_session_search = it->value("enable_session_search", true);
            l.enable_web_search = it->value("enable_web_search", false);
            l.show_result_count = it->value("show_result_count", true);
            l.show_all_apps = it->value("show_all_apps", true);
        }
        if (auto it = j.find("session"); it != j.end() && it->is_object()) {
            auto& s = session_;
            if (it->value("mode", "dropdown") == std::string("fullscreen"))
                s.mode = Session::Mode::Fullscreen;
            if (it->value("fullscreen_layout", "single_row") == std::string("grid"))
                s.fullscreen_layout = Session::Layout::Grid;
            if (auto items = it->find("items"); items != it->end() && items->is_object())
                for (const auto& [key, v] : items->items())
                    if (v.is_boolean())
                        s.items[key] = v.get<bool>();
        }
        if (auto it = j.find("idle"); it != j.end() && it->is_object()) {
            auto& i = idle_;
            i.enabled = it->value("enabled", true);
            i.screen_off_timeout = std::clamp(it->value("screen_off_timeout", 600), 0, 86400);
            i.lock_timeout = std::clamp(it->value("lock_timeout", 660), 0, 86400);
            i.suspend_timeout = std::clamp(it->value("suspend_timeout", 1800), 0, 86400);
            i.fade_duration = std::clamp(it->value("fade_duration", 5), 1, 60);
            i.lock_before_suspend = it->value("lock_before_suspend", true);
            i.screen_off_command = it->value("screen_off_command", "");
            i.lock_command = it->value("lock_command", "");
            i.suspend_command = it->value("suspend_command", "");
            i.resume_screen_off_command = it->value("resume_screen_off_command", "");
            i.resume_lock_command = it->value("resume_lock_command", "");
            i.resume_suspend_command = it->value("resume_suspend_command", "");
            if (auto list = it->find("custom_commands"); list != it->end() && list->is_array()) {
                for (const auto& entry : *list) {
                    if (!entry.is_object())
                        continue;
                    Idle::CustomCommand cmd;
                    cmd.timeout = std::clamp(entry.value("timeout", 0), 0, 86400);
                    cmd.command = entry.value("command", "");
                    cmd.resume_command = entry.value("resume_command", "");
                    i.custom_commands.push_back(std::move(cmd));
                }
            }
        }
        if (auto it = j.find("lock_screen"); it != j.end() && it->is_object()) {
            std::string background = it->value("background", "");
            if (!background.empty() && background[0] == '~')
                background = Glib::get_home_dir() + background.substr(1);
            lock_screen_.background = background;
            lock_screen_.blur = std::clamp(it->value("blur", 0.0), 0.0, 1.0);
        }
        if (auto it = j.find("wallpaper"); it != j.end() && it->is_object()) {
            auto& w = wallpaper_;
            const auto expand = [](std::string path) {
                if (!path.empty() && path[0] == '~')
                    path = Glib::get_home_dir() + path.substr(1);
                return path;
            };
            w.directory = expand(it->value("directory", ""));
            w.current = expand(it->value("current", ""));
            const std::string fill = it->value("fill_mode", "crop");
            using F = Wallpaper::FillMode;
            w.fill_mode = fill == "fit"       ? F::Fit
                          : fill == "stretch" ? F::Stretch
                          : fill == "center"  ? F::Center
                          : fill == "repeat"  ? F::Repeat
                                              : F::Crop;
            w.transitions_enabled = it->value("transitions_enabled", true);
            if (auto list = it->find("transitions"); list != it->end() && list->is_array()) {
                w.transitions.clear();
                for (const auto& entry : *list)
                    if (entry.is_string())
                        w.transitions.push_back(entry.get<std::string>());
            }
            w.transition_duration_ms = std::clamp(it->value("transition_duration_ms", 1500), 500, 10000);
            w.edge_smoothness = std::clamp(it->value("edge_smoothness", 0.05), 0.0, 1.0);
            w.slideshow = it->value("slideshow", false);
            w.slideshow_interval_s = std::clamp(it->value("slideshow_interval_s", 300), 60, 86400);
            w.slideshow_order = it->value("slideshow_order", "random") == "alphabetical"
                                    ? Wallpaper::Order::Alphabetical
                                    : Wallpaper::Order::Random;
        }
        if (auto it = j.find("night_light"); it != j.end() && it->is_object()) {
            auto& n = night_light_;
            n.enabled = it->value("enabled", false);
            n.forced = it->value("forced", false);
            n.night_temp = std::clamp(it->value("night_temp", 4000), 1000, 6000);
            const auto valid_time = [](const std::string& t) {
                return t.size() == 5 && t[2] == ':' && std::isdigit(static_cast<unsigned char>(t[0])) &&
                       std::isdigit(static_cast<unsigned char>(t[1])) &&
                       std::isdigit(static_cast<unsigned char>(t[3])) &&
                       std::isdigit(static_cast<unsigned char>(t[4]));
            };
            const std::string sunrise = it->value("manual_sunrise", "06:30");
            const std::string sunset = it->value("manual_sunset", "18:30");
            if (valid_time(sunrise))
                n.manual_sunrise = sunrise;
            if (valid_time(sunset))
                n.manual_sunset = sunset;
        }
        if (auto it = j.find("osd"); it != j.end() && it->is_object()) {
            osd_.enabled = it->value("enabled", true);
            const std::string location = it->value("location", "top_right");
            using L = Osd::Location;
            osd_.location = location == "top"            ? L::Top
                            : location == "top_left"     ? L::TopLeft
                            : location == "bottom"       ? L::Bottom
                            : location == "bottom_left"  ? L::BottomLeft
                            : location == "bottom_right" ? L::BottomRight
                            : location == "left"         ? L::Left
                            : location == "right"        ? L::Right
                                                         : L::TopRight;
            const std::string orientation = it->value("orientation", "auto");
            osd_.orientation = orientation == "landscape" ? Osd::Orientation::Landscape
                               : orientation == "portrait" ? Osd::Orientation::Portrait
                                                            : Osd::Orientation::Auto;
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
