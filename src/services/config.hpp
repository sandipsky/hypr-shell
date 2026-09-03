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

    // bar.bluetooth.auto_connect: reconnect every paired device when the
    // adapter powers on (default off); toggled in hypr-shell-settings.
    bool bluetooth_auto_connect() const { return bt_auto_connect_; }

    // bar.notifications.* — Noctalia's NotificationHistory widget settings
    bool notifications_show_badge() const { return notif_show_badge_; }
    bool notifications_hide_when_zero() const { return notif_hide_zero_; }
    bool notifications_hide_when_zero_unread() const { return notif_hide_zero_unread_; }

    // notifications.* (top level, like Noctalia's Settings.data.notifications):
    // the daemon + popup behavior, edited on hypr-shell-settings' own
    // Notifications page. Defaults are Noctalia's.
    struct Notifications {
        enum class Density { Default, Compact };
        enum class Location { Top, TopLeft, TopRight, Bottom, BottomLeft, BottomRight };
        struct Rule {
            std::string pattern; // substring, *glob*, or /regex/
            std::string action;  // "block" | "hide" | "mute"
        };

        bool enabled = true;
        // Baseline DND from the settings app; the shell adopts it whenever the
        // value changes, but the bell's right click still toggles it at runtime.
        bool do_not_disturb = false;
        Density density = Density::Default;
        Location location = Location::TopRight;
        bool overlay_layer = true;         // overlay vs top layer for popups
        double background_opacity = 1.0;   // popup card alpha
        bool respect_expire_timeout = false;
        int low_duration_s = 3;
        int normal_duration_s = 8;
        int critical_duration_s = 15;
        bool clear_dismissed = true; // dismissing a popup also deletes history
        bool save_low = true;        // per-urgency saveToHistory
        bool save_normal = true;
        bool save_critical = true;
        struct Sounds {
            bool enabled = false;
            double volume = 0.5;
            bool separate = false; // per-urgency files vs the normal one
            std::string low_file;
            std::string normal_file; // also the unified file
            std::string critical_file;
            std::string excluded_apps = "discord,firefox,chrome,chromium,edge";
        } sounds;
        std::vector<Rule> rules;
    };
    const Notifications& notifications() const { return notifications_; }

    // launcher.* (top level): the app launcher's optional search providers
    // and list behavior. Application search and the calculator are always on.
    struct Launcher {
        bool enable_settings_search = true; // hypr-shell-settings entries
        bool enable_session_search = true;  // lock/suspend/reboot/logout/shutdown
        bool enable_web_search = false;     // "search the web" fallback entry
        bool show_result_count = true;      // footer under the list
        bool show_all_apps = true;          // list all apps while query is empty
    };
    const Launcher& launcher() const { return launcher_; }

    // bar.app_menu.* — the grid app menu bar button (Noctalia's Launcher bar
    // widget + the launcher's grid view, shown in a bar popover)
    struct AppMenu {
        enum class Display { Icon, IconText, Text };
        Display display = Display::Icon;
        std::string icon = "rocket";      // preset key, "distro" or "custom" (app_menu_icons.hpp)
        std::string custom_icon;          // themed icon name or image path for "custom"
        std::string text = "Apps";        // label for the icon_text / text displays
        bool show_search = true;          // search box at the top of the panel
        bool show_settings_button = true; // panel button opening hypr-shell-settings
        bool show_session_button = true;  // panel dropdown: lock/suspend/reboot/logout/shutdown
        int columns = 5;                  // grid columns, 3..8
        bool multiline_labels = false;    // tile names wrap to two lines instead of one
    };
    const AppMenu& app_menu() const { return app_menu_; }

    // session.* (top level): the session menu shared by the bar's session
    // module, the app menu's power button and `hypr-shell --session`
    // (Noctalia's Settings.data.sessionMenu)
    struct Session {
        enum class Mode { Dropdown, Fullscreen }; // Noctalia's largeButtonsStyle
        enum class Layout { SingleRow, Grid };    // Noctalia's largeButtonsLayout
        Mode mode = Mode::Dropdown;
        Layout fullscreen_layout = Layout::SingleRow;
        std::map<std::string, bool> items; // session.items.<key>; absent = the action's default
        bool item_enabled(const std::string& key, bool default_on) const {
            auto it = items.find(key);
            return it == items.end() ? default_on : it->second;
        }
    };
    const Session& session() const { return session_; }

    // idle.* (top level): the idle daemon — Noctalia's Settings.data.idle.
    // Timeouts in seconds, 0 disables a stage. `enabled` defaults ON (Noctalia
    // defaults it off, but hypr-shell-settings only exposes the three
    // timeouts, so 0 is the way to switch a stage off).
    struct Idle {
        struct CustomCommand {
            int timeout = 0; // seconds
            std::string command;
            std::string resume_command;
        };
        bool enabled = true;
        int screen_off_timeout = 600; // Noctalia's defaults
        int lock_timeout = 660;
        int suspend_timeout = 1800;
        int fade_duration = 5;          // fade-to-black grace period, 1..60 s
        bool lock_before_suspend = true; // Noctalia's general.lockOnSuspend
        std::string screen_off_command, lock_command, suspend_command;
        std::string resume_screen_off_command, resume_lock_command, resume_suspend_command;
        std::vector<CustomCommand> custom_commands;
    };
    const Idle& idle() const { return idle_; }

    // lock_screen.* (top level): Noctalia's general.lockScreenWallpaper /
    // lockScreenBlur — the only two lock screen options exposed (per user).
    struct LockScreen {
        std::string background; // image path (~ expanded); empty = plain dark background
        double blur = 0.0;      // 0..1 wallpaper blur strength (Noctalia's blurMax 48px at 1)
    };
    const LockScreen& lock_screen() const { return lock_screen_; }

    // osd.* (top level): the on-screen display for volume / microphone /
    // brightness / lock-key changes (Noctalia's Settings.data.osd). Only the
    // two options hypr-shell-settings shows are configurable; auto-hide
    // (2s), overlay layer and opaque background are fixed.
    struct Osd {
        enum class Location { Top, TopLeft, TopRight, Bottom, BottomLeft, BottomRight, Left, Right };
        // Landscape = horizontal bar, Portrait = vertical column; Auto follows
        // the position like Noctalia (sides vertical, top/bottom horizontal)
        enum class Orientation { Auto, Landscape, Portrait };
        bool enabled = true;
        Location location = Location::TopRight; // Noctalia's default
        Orientation orientation = Orientation::Auto;
        bool vertical() const {
            if (orientation != Orientation::Auto)
                return orientation == Orientation::Portrait;
            return location == Location::Left || location == Location::Right;
        }
    };
    const Osd& osd() const { return osd_; }

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
    bool bt_auto_connect_ = false;
    bool notif_show_badge_ = true;
    bool notif_hide_zero_ = false;
    bool notif_hide_zero_unread_ = false;
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
    Notifications notifications_;
    Launcher launcher_;
    AppMenu app_menu_;
    Session session_;
    Idle idle_;
    LockScreen lock_screen_;
    Osd osd_;
    sigc::signal<void()> changed_;
};

} // namespace hyprshell
