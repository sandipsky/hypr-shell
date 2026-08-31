#pragma once

#include <giomm.h>
#include <sigc++/sigc++.h>

#include <array>
#include <map>
#include <string>
#include <vector>

namespace hyprshell {

// ~/.config/hypr-shell/config.json, hot-reloaded. Written by hand or by
// hypr-shell-settings; absent keys mean defaults, invalid JSON keeps defaults.
class Config {
public:
    enum class BarPosition { Top, Bottom, Left, Right };
    enum class BarSection { Left, Center, Right };
    enum class BarVisibility { Visible, Hidden, AutoHide };
    enum class WorkspacesMode { Dynamic, Fixed };

    static Config& get();

    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    BarPosition bar_position() const { return bar_position_; }
    bool bar_vertical() const {
        return bar_position_ == BarPosition::Left || bar_position_ == BarPosition::Right;
    }
    // bar.visibility (Noctalia's displayMode): always show, always hide, or
    // auto-hide (bar overlays windows and slides away; a 1px strip on the
    // bar's screen edge reveals it on hover). The two toggles below only
    // matter in auto-hide mode.
    BarVisibility bar_visibility() const { return bar_visibility_; }
    bool bar_show_on_workspace_switch() const { return bar_show_on_ws_switch_; }
    bool bar_show_when_workspace_empty() const { return bar_show_when_ws_empty_; }
    // bar.background_opacity: 0..1 alpha of the bar background (Noctalia's
    // backgroundOpacity). 0.88 matches the default theme's alpha.
    double bar_background_opacity() const { return bar_background_opacity_; }
    // Modules absent from bar.modules are enabled, so new modules default on.
    bool module_enabled(const std::string& name) const;
    // Ordered module names for one bar section, resolved from bar.layout:
    // unknown names are dropped, duplicates keep their first placement, and
    // modules missing from every list land at the end of their default section.
    const std::vector<std::string>& bar_layout(BarSection section) const {
        return layout_[static_cast<std::size_t>(section)];
    }

    // bar.workspaces.* — per-module settings (Noctalia's workspaceMode /
    // fixedWorkspaces semantics: fixed always shows 1..count plus any real
    // workspace beyond the range).
    WorkspacesMode workspaces_mode() const { return workspaces_mode_; }
    int workspaces_fixed_count() const { return workspaces_fixed_count_; }
    bool workspaces_scroll_wrap() const { return workspaces_scroll_wrap_; }

    // bar.active_window.* — Noctalia's ActiveWindow widget settings
    enum class ActiveWindowHide { Visible, Hidden, Transparent };
    enum class ActiveWindowText { Title, AppName };
    enum class ActiveWindowEmpty { Default, Desktop, None };
    ActiveWindowHide active_window_hide_mode() const { return aw_hide_; }
    bool active_window_show_title() const { return aw_show_title_; }
    ActiveWindowText active_window_title_mode() const { return aw_title_mode_; }
    ActiveWindowEmpty active_window_empty_text() const { return aw_empty_; }
    bool active_window_show_icon() const { return aw_show_icon_; }

    // bar.battery.* — which cards the battery click panel shows (all default
    // on; each also needs its backend to be available)
    bool battery_show_power_profiles() const { return battery_show_profiles_; }
    bool battery_show_brightness() const { return battery_show_brightness_; }
    bool battery_show_refresh_rate() const { return battery_show_refresh_; }

    // bar.clock.first_day_of_week: 0 = Sunday (default), 1 = Monday
    int clock_first_day_of_week() const { return clock_first_day_of_week_; }
    // strftime formats, Noctalia semantics: the horizontal one may contain
    // newlines; the vertical one's space-separated tokens render stacked.
    const std::string& clock_format_horizontal() const { return clock_format_horizontal_; }
    const std::string& clock_format_vertical() const { return clock_format_vertical_; }

    sigc::signal<void()>& signal_changed() { return changed_; }

private:
    Config();

    void load();

    std::string path_;
    Glib::RefPtr<Gio::FileMonitor> monitor_;
    BarPosition bar_position_ = BarPosition::Top;
    BarVisibility bar_visibility_ = BarVisibility::Visible;
    bool bar_show_on_ws_switch_ = true;
    bool bar_show_when_ws_empty_ = false;
    double bar_background_opacity_ = 0.88;
    std::map<std::string, bool> modules_;
    std::array<std::vector<std::string>, 3> layout_;
    WorkspacesMode workspaces_mode_ = WorkspacesMode::Dynamic;
    int workspaces_fixed_count_ = 5;
    bool workspaces_scroll_wrap_ = true;
    bool battery_show_profiles_ = true;
    bool battery_show_brightness_ = true;
    bool battery_show_refresh_ = true;
    int clock_first_day_of_week_ = 0;
    std::string clock_format_horizontal_ = "%H:%M %a, %b %d";
    std::string clock_format_vertical_ = "%H %M";
    ActiveWindowHide aw_hide_ = ActiveWindowHide::Hidden;
    bool aw_show_title_ = true;
    ActiveWindowText aw_title_mode_ = ActiveWindowText::Title;
    ActiveWindowEmpty aw_empty_ = ActiveWindowEmpty::Default;
    bool aw_show_icon_ = true;
    sigc::signal<void()> changed_;
};

} // namespace hyprshell
