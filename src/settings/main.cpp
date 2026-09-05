// hypr-shell-settings — configures the shell by editing
// ~/.config/hypr-shell/config.json. The running shell hot-reloads that file,
// so every change here applies live; no IPC between the two binaries.
//
// libadwaita has no official C++ bindings — the C API is called directly
// (see CLAUDE.md). Keys the settings app doesn't manage are preserved on save.

#include <adwaita.h>
#include <nlohmann/json.hpp>

#include "services/app_menu_icons.hpp"
#include "services/palette.hpp"
#include "services/session_actions.hpp"
#include "services/wallpaper_files.hpp"
#include "settings/about_page.hpp"
#include "settings/hotspot_page.hpp"
#include "settings/search.hpp"
#include "settings/vpn_page.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <memory>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

using json = nlohmann::json;

namespace {

struct ModuleInfo {
    const char* key; // key under bar.modules / bar.layout in config.json
    const char* title;
    const char* subtitle;
    int section; // default bar section: 0 left, 1 center, 2 right
};

constexpr ModuleInfo kModules[] = {
    {"launcher",      "Launcher",      "App launcher search button",  0},
    {"app_menu",      "App menu",      "Grid app menu with search",   0},
    {"workspaces",    "Workspaces",    "Hyprland workspace switcher", 0},
    {"taskbar",       "Taskbar",       "Running and pinned apps",     0},
    {"active_window", "Active window", "Focused window title",        1},
    {"network",       "Network",       "Wi-Fi / ethernet status icon", 2},
    {"bluetooth",     "Bluetooth",     "Bluetooth status icon",        2},
    {"control_center", "Control center", "Media, audio, brightness and system monitor panel", 2},
    {"volume",        "Volume",        "Output volume status icon",    2},
    {"battery",       "Battery",       "Battery status icon",          2},
    {"clipboard",     "Clipboard",     "Clipboard history button",     2},
    {"notifications", "Notifications", "Notification bell and history", 2},
    {"clock",         "Clock",         "Date and time",                2},
    {"session",       "Session",       "Power button opening the session menu", 2},
};

constexpr gsize kModuleCount = G_N_ELEMENTS(kModules);
constexpr const char* kSectionKeys[] = {"left", "center", "right"};
constexpr const char* kSectionTitles[] = {"Left section", "Center section", "Right section"};
constexpr const char* kPositions[] = {"top", "bottom", "left", "right"};
constexpr const char* kVisibilityKeys[] = {"visible", "hidden", "auto_hide"};
constexpr const char* kAwHideKeys[] = {"visible", "hidden", "transparent"};
constexpr const char* kTbHideKeys[] = {"visible", "hidden", "transparent"};
constexpr const char* kAwTextKeys[] = {"title", "appname"};
constexpr const char* kAwEmptyKeys[] = {"default", "desktop", "none"};
constexpr const char* kAmDisplayKeys[] = {"icon", "icon_text", "text"};
constexpr const char* kSmModeKeys[] = {"dropdown", "fullscreen"};
// sidebar rows -> GtkStack page names, labels and symbolic icons (GNOME
// Settings look; About last, like GNOME)
constexpr hyprshell::settings::SidebarPage kSidebarPages[] = {
    {"bar", "Bar", "focus-top-bar-symbolic"},
    {"ui_page", "User interface", "preferences-desktop-appearance-symbolic"},
    {"wallpaper_page", "Wallpaper", "preferences-desktop-wallpaper-symbolic"},
    {"night_light_page", "Night light", "night-light-symbolic"},
    {"hotspot_page", "Hotspot", "glyph:\uED1B"}, // tabler access-point
    {"vpn_page", "VPN", "glyph:\uED58"},         // tabler shield-lock
    {"launcher_page", "Launcher", "glyph:\uEC45"}, // tabler rocket, the app menu's default
    {"clipboard_page", "Clipboard", "edit-paste-symbolic"},
    {"session_page", "Session menu", "system-shutdown-symbolic"},
    {"lock_page", "Lock screen", "system-lock-screen-symbolic"},
    {"idle_page", "Idle", "alarm-symbolic"},
    {"osd_page", "On-screen display", "display-brightness-symbolic"},
    {"notifications_page", "Notifications", "preferences-system-notifications-symbolic"},
    {"about_page", "About", "help-about-symbolic"},
};
constexpr int kSidebarPageCount = G_N_ELEMENTS(kSidebarPages);
constexpr const char* kSmLayoutKeys[] = {"single_row", "grid"};
constexpr const char* kClipboardPositionKeys[] = {"center", "top_left", "top", "top_right",
                                                  "bottom_left", "bottom", "bottom_right"};
constexpr guint kClipboardPositionCount = G_N_ELEMENTS(kClipboardPositionKeys);
// wallpaper page (Noctalia's fillModeModel order / transitionsModel minus the
// two shader-only types the shell does not render)
constexpr const char* kWpFillKeys[] = {"center", "crop", "fit", "stretch", "repeat"};
constexpr guint kWpFillCount = G_N_ELEMENTS(kWpFillKeys);
constexpr const char* kWpOrderKeys[] = {"random", "alphabetical"};
struct WpTransition {
    const char* key;
    const char* label;
};
constexpr WpTransition kWpTransitions[] = {
    {"fade", "Fade"}, {"disc", "Disc"}, {"stripes", "Stripes"}, {"wipe", "Wipe"}};
constexpr gsize kWpTransitionCount = G_N_ELEMENTS(kWpTransitions);
constexpr int kWpThumbWidth = 120;  // tile image size (Noctalia: cell x 0.67)
constexpr int kWpThumbHeight = 80;
constexpr int kWpThumbCache = 384;  // cached square thumbnail, like Noctalia's
constexpr int kWpGridMaxHeight = 420; // grid scrolls beyond this
// night light: Noctalia's slider range with the day temperature fixed at 6500
// (night must stay 500 K below it)
constexpr int kNlTempMin = 1000;
constexpr int kNlTempMax = 6000;
constexpr int kNlTimeOptions = 48; // "HH:MM" every 30 minutes
constexpr gsize kSessionActionCount = G_N_ELEMENTS(hyprshell::kSessionActions);
// app menu icon dropdown: the shared presets, then Distro logo, then Custom
constexpr guint kAmPresetCount = G_N_ELEMENTS(hyprshell::kAppMenuIconPresets);
constexpr guint kAmIconDistroIndex = kAmPresetCount;
constexpr guint kAmIconCustomIndex = kAmPresetCount + 1;
// User interface page: GNOME Settings' accent swatches, plus the shell's
// default lavender first. The hex is the `ui.accent` value.
struct AccentSwatch {
    const char* name;
    const char* hex;
};
constexpr AccentSwatch kAccents[] = {
    {"Lavender", "#bfc2ff"}, {"Blue", "#3584e4"},   {"Teal", "#2190a4"},  {"Green", "#3a944a"},
    {"Yellow", "#c88800"},   {"Orange", "#ed5b00"}, {"Red", "#e62d42"},   {"Pink", "#d56199"},
    {"Purple", "#9141ac"},   {"Slate", "#6f8396"},
};
constexpr gsize kAccentCount = G_N_ELEMENTS(kAccents);
constexpr const char* kNdDensityKeys[] = {"default", "compact"};
constexpr const char* kNdLocationKeys[] = {"top",    "top_left",    "top_right",
                                           "bottom", "bottom_left", "bottom_right"};
constexpr const char* kRuleActionKeys[] = {"block", "hide", "mute"};
constexpr const char* kOsdLocationKeys[] = {"top",    "top_left",    "top_right",
                                            "bottom", "bottom_left", "bottom_right",
                                            "left",   "right"};
constexpr guint kOsdLocationCount = G_N_ELEMENTS(kOsdLocationKeys);
constexpr const char* kOsdOrientationKeys[] = {"auto", "landscape", "portrait"};
constexpr const char* kRuleActionLabels[] = {
    "Block — skips completely",
    "Hide — no popup, no sound, adds to history",
    "Mute — no sound, still shows popup and in history",
};

struct Settings;
void update_aw_row_visibility(Settings* s);
void update_am_rows(Settings* s);
void update_sm_rows(Settings* s);
void update_bar_visibility_rows(Settings* s);
void update_nd_rows(Settings* s);
void rebuild_rule_rows(Settings* s);
void update_wp_rows(Settings* s);
void update_ui_style_tiles(Settings* s);
void update_nl_rows(Settings* s);
std::string nl_time_option(guint index);
void wp_set_directory(Settings* s, const std::string& directory);
void wp_rescan(Settings* s);
void wp_update_highlight(Settings* s);
std::string wp_effective_current(Settings* s);

struct Settings {
    std::string path;           // ~/.config/hypr-shell/config.json
    json root = json::object(); // the whole file, unknown keys included
    bool loading = false;       // widgets being populated — suppress writes

    AdwComboRow* position = nullptr;
    AdwComboRow* visibility = nullptr; // Always show / Always hide / Auto hide
    AdwSwitchRow* show_ws_switch = nullptr; // auto-hide: peek on workspace switch
    AdwSwitchRow* show_ws_empty = nullptr;  // auto-hide: stay while workspace empty
    GtkAdjustment* opacity = nullptr;       // background opacity slider, 0..100 %
    AdwSwitchRow* modules[kModuleCount] = {};

    AdwComboRow* ws_mode = nullptr; // Dynamic / Fixed number
    AdwSpinRow* ws_count = nullptr;
    AdwSwitchRow* ws_wrap = nullptr;

    AdwComboRow* clock_fdow = nullptr; // Sunday / Monday
    AdwEntryRow* clock_fmt_h = nullptr;
    AdwEntryRow* clock_fmt_v = nullptr;

    AdwSwitchRow* bt_auto = nullptr; // bluetooth panel: auto-connect

    // control center subpage: which cards the panel shows
    AdwSwitchRow* cc_media = nullptr;
    AdwSwitchRow* cc_audio = nullptr;
    AdwSwitchRow* cc_brightness = nullptr;
    AdwSwitchRow* cc_sysmon = nullptr;

    // taskbar subpage (bar.taskbar): the four exposed options
    AdwComboRow* tb_hide = nullptr; // Always visible / Hide when empty / Transparent when empty
    AdwSwitchRow* tb_same_monitor = nullptr;
    AdwSwitchRow* tb_active_workspaces = nullptr;
    AdwSwitchRow* tb_pinned = nullptr;

    AdwComboRow* am_display = nullptr; // app menu: Icon / Icon and text / Text
    AdwEntryRow* am_text = nullptr;
    AdwComboRow* am_icon = nullptr;    // presets + Distro logo + Custom
    AdwEntryRow* am_custom_icon = nullptr;
    AdwSpinRow* am_columns = nullptr;
    AdwSwitchRow* am_settings_btn = nullptr;
    AdwSwitchRow* am_session_btn = nullptr;
    AdwSwitchRow* am_multiline = nullptr;
    AdwSwitchRow* am_show_search = nullptr;

    // Session menu sidebar page (top-level "session" object in config.json)
    AdwComboRow* sm_mode = nullptr;   // Dropdown / Fullscreen
    AdwComboRow* sm_layout = nullptr; // Single row / Grid (fullscreen only)
    AdwSwitchRow* sm_items[kSessionActionCount] = {};

    // Idle sidebar page (top-level "idle" object): the three stage timeouts
    AdwSpinRow* idle_screen_off = nullptr;
    AdwSpinRow* idle_lock = nullptr;
    AdwSpinRow* idle_suspend = nullptr;

    // Lock screen sidebar page (top-level "lock_screen" object): background
    // image + blur strength — the two options exposed, per user
    AdwEntryRow* lock_background = nullptr;
    GtkAdjustment* lock_blur = nullptr; // 0..100 %

    // On-screen display sidebar page (top-level "osd" object): position +
    // master switch — the two options exposed, per user
    AdwComboRow* osd_location = nullptr;
    AdwComboRow* osd_orientation = nullptr; // Automatic / Landscape / Portrait
    AdwSwitchRow* osd_enabled = nullptr;

    // Night light sidebar page (top-level "night_light" object)
    AdwSwitchRow* nl_enabled = nullptr;
    GtkWidget* nl_temp_group = nullptr;
    GtkWidget* nl_schedule_group = nullptr;
    GtkAdjustment* nl_temp = nullptr; // K
    guint nl_temp_source = 0;         // debounced save while dragging
    GtkWidget* nl_manual_label = nullptr; // "Scheduling" heading row
    AdwComboRow* nl_sunrise = nullptr;
    AdwComboRow* nl_sunset = nullptr;
    AdwSwitchRow* nl_forced = nullptr;
    bool nl_available = false; // hyprsunset found in PATH

    // User interface sidebar page (top-level "ui" object): theme
    GtkWidget* ui_style_preview[2] = {}; // Default (light) / Dark tiles' drawing areas
    GtkWidget* ui_swatches[kAccentCount] = {}; // GtkCheckButtons, one per kAccents
    GtkWidget* ui_font_button = nullptr;   // GtkFontDialogButton
    GtkCssProvider* ui_accent_css = nullptr; // this app's own accent color

    // Wallpaper sidebar page (top-level "wallpaper" object)
    AdwEntryRow* wp_directory = nullptr;
    GtkWidget* wp_grid_group = nullptr;  // AdwPreferencesGroup holding the grid
    GtkWidget* wp_grid = nullptr;        // GtkFlowBox of thumbnail tiles
    GtkWidget* wp_grid_scroller = nullptr; // its GtkScrolledWindow (capped height)
    GtkWidget* wp_grid_status = nullptr; // "no folder" / "no images" label
    GtkWidget* wp_look_group = nullptr;
    GtkWidget* wp_slideshow_group = nullptr;
    AdwComboRow* wp_fill = nullptr;
    AdwSwitchRow* wp_transitions = nullptr;
    GtkWidget* wp_transition_types = nullptr; // AdwExpanderRow
    AdwSwitchRow* wp_transition[kWpTransitionCount] = {};
    GtkWidget* wp_duration_row = nullptr;
    GtkAdjustment* wp_duration = nullptr; // ms
    AdwSwitchRow* wp_slideshow = nullptr;
    AdwComboRow* wp_order = nullptr;
    AdwSpinRow* wp_interval = nullptr; // minutes (stored in seconds)
    std::string wp_scanned_dir;        // folder the grid currently shows
    std::string wp_current;            // highlighted tile (shell state, else config)
    std::vector<std::string> wp_images;
    GFileMonitor* wp_dir_monitor = nullptr;
    GFileMonitor* wp_state_monitor = nullptr;
    guint wp_rescan_source = 0;

    AdwSwitchRow* bat_profiles = nullptr; // battery panel cards
    AdwSwitchRow* bat_brightness = nullptr;
    AdwSwitchRow* bat_refresh = nullptr;

    // Launcher sidebar page (top-level "launcher" object in config.json)
    AdwSwitchRow* lp_settings_search = nullptr;
    AdwSwitchRow* lp_session_search = nullptr;
    AdwSwitchRow* lp_web_search = nullptr;
    AdwSwitchRow* lp_result_count = nullptr;
    AdwSwitchRow* lp_show_all = nullptr;
    // Clipboard page (top-level "clipboard" object)
    AdwSwitchRow* cb_enabled = nullptr;
    AdwSwitchRow* cb_show_images = nullptr;
    AdwSwitchRow* cb_paste = nullptr;
    AdwComboRow* cb_position = nullptr;

    AdwSwitchRow* notif_badge = nullptr; // notifications module (bell widget)
    AdwSwitchRow* notif_hide_zero = nullptr;
    AdwSwitchRow* notif_hide_zero_unread = nullptr;

    // Notifications sidebar page — the daemon + popups (top-level
    // "notifications" object in config.json, Noctalia's notifications tab)
    GtkWidget* window = nullptr; // dialog/file-chooser parent
    AdwSwitchRow* nd_enabled = nullptr;
    AdwSwitchRow* nd_dnd = nullptr;
    AdwComboRow* nd_density = nullptr;
    AdwComboRow* nd_location = nullptr;
    AdwSwitchRow* nd_overlay = nullptr;
    GtkAdjustment* nd_opacity = nullptr;
    AdwSwitchRow* nd_respect_expire = nullptr;
    AdwSpinRow* nd_dur_low = nullptr;
    AdwSpinRow* nd_dur_normal = nullptr;
    AdwSpinRow* nd_dur_critical = nullptr;
    AdwSwitchRow* nd_clear_dismissed = nullptr;
    AdwSwitchRow* nd_save_low = nullptr;
    AdwSwitchRow* nd_save_normal = nullptr;
    AdwSwitchRow* nd_save_critical = nullptr;
    AdwSwitchRow* nd_snd_enabled = nullptr;
    GtkAdjustment* nd_snd_volume = nullptr;
    GtkWidget* nd_snd_volume_row = nullptr;
    AdwSwitchRow* nd_snd_separate = nullptr;
    AdwEntryRow* nd_snd_unified = nullptr;
    AdwEntryRow* nd_snd_low = nullptr;
    AdwEntryRow* nd_snd_normal = nullptr;
    AdwEntryRow* nd_snd_critical = nullptr;
    AdwEntryRow* nd_snd_excluded = nullptr;
    std::vector<GtkWidget*> nd_dependent_groups; // insensitive while disabled
    GtkWidget* rules_group = nullptr;
    std::vector<GtkWidget*> rule_rows;

    AdwComboRow* aw_hide = nullptr;      // Always visible / Hidden / Transparent
    AdwSwitchRow* aw_show_title = nullptr;
    AdwComboRow* aw_text = nullptr;      // Window title / Application name
    AdwComboRow* aw_empty = nullptr;     // "No active window" / "Desktop" / Nothing
    AdwSwitchRow* aw_icon = nullptr;

    std::array<std::vector<std::string>, 3> layout; // resolved section contents
    GtkWidget* layout_groups[3] = {};
    std::vector<GtkWidget*> layout_rows[3];
};

const ModuleInfo* module_info(const std::string& key) {
    for (const auto& m : kModules)
        if (key == m.key)
            return &m;
    return nullptr;
}

// index into Settings::modules — keeps cog attachments right as kModules grows
gsize module_index(const char* key) {
    for (gsize i = 0; i < kModuleCount; ++i)
        if (g_strcmp0(kModules[i].key, key) == 0)
            return i;
    g_warn_if_reached();
    return 0;
}

void load(Settings* s) {
    gchar* data = nullptr;
    gsize len = 0;
    if (!g_file_get_contents(s->path.c_str(), &data, &len, nullptr))
        return; // no config yet — defaults
    json parsed = json::parse(std::string_view(data, len), nullptr,
                              /*allow_exceptions=*/false);
    g_free(data);
    if (parsed.is_object())
        s->root = std::move(parsed);
    else
        g_warning("%s is not a JSON object — starting from defaults", s->path.c_str());
}

void save(Settings* s) {
    gchar* dir = g_path_get_dirname(s->path.c_str());
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    const std::string text = s->root.dump(2) + "\n";
    GError* error = nullptr;
    if (!g_file_set_contents(s->path.c_str(), text.c_str(),
                             static_cast<gssize>(text.size()), &error)) {
        g_warning("failed to write %s: %s", s->path.c_str(), error->message);
        g_error_free(error);
    }
}

// bar section of the config, created (and repaired to an object) on demand
json& bar_object(Settings* s) {
    if (!s->root.is_object())
        s->root = json::object();
    if (!s->root["bar"].is_object())
        s->root["bar"] = json::object();
    return s->root["bar"];
}

json& ui_object(Settings* s) {
    if (!s->root.is_object())
        s->root = json::object();
    if (!s->root["ui"].is_object())
        s->root["ui"] = json::object();
    return s->root["ui"];
}

void populate(Settings* s) {
    std::string position = "top";
    std::string visibility = "visible";
    bool ws_switch = true, ws_empty = false;
    double opacity = 0.88;
    bool enabled[kModuleCount];
    for (auto& e : enabled)
        e = true;

    try {
        const json bar = s->root.value("bar", json::object());
        position = bar.value("position", position);
        visibility = bar.value("visibility", visibility);
        ws_switch = bar.value("show_on_workspace_switch", ws_switch);
        ws_empty = bar.value("show_when_workspace_empty", ws_empty);
        opacity = std::clamp(bar.value("background_opacity", opacity), 0.0, 1.0);
        const json modules = bar.value("modules", json::object());
        for (gsize i = 0; i < kModuleCount; ++i)
            enabled[i] = modules.value(kModules[i].key, true);
    } catch (const json::exception& e) {
        g_warning("%s: %s — showing defaults", s->path.c_str(), e.what());
    }

    std::string ws_mode = "dynamic";
    int ws_count = 5;
    bool ws_wrap = true;
    try {
        const json ws = s->root.value("bar", json::object()).value("workspaces", json::object());
        ws_mode = ws.value("mode", ws_mode);
        ws_count = std::clamp(ws.value("fixed_count", ws_count), 1, 50);
        ws_wrap = ws.value("scroll_wrap", ws_wrap);
    } catch (const json::exception&) {
        // defaults
    }

    bool bt_auto = false;
    try {
        const json cc = s->root.value("bar", json::object()).value("control_center", json::object());
        adw_switch_row_set_active(s->cc_media, cc.value("show_media", true));
        adw_switch_row_set_active(s->cc_audio, cc.value("show_audio", true));
        adw_switch_row_set_active(s->cc_brightness, cc.value("show_brightness", false));
        adw_switch_row_set_active(s->cc_sysmon, cc.value("show_sysmon", true));
        const json bt = s->root.value("bar", json::object()).value("bluetooth", json::object());
        bt_auto = bt.value("auto_connect", false);
    } catch (const json::exception&) {
        // defaults
    }

    try {
        const json tb = s->root.value("bar", json::object()).value("taskbar", json::object());
        const std::string tb_hide = tb.value("hide_mode", "hidden");
        adw_combo_row_set_selected(s->tb_hide, 1);
        for (guint i = 0; i < 3; ++i)
            if (tb_hide == kTbHideKeys[i])
                adw_combo_row_set_selected(s->tb_hide, i);
        adw_switch_row_set_active(s->tb_same_monitor, tb.value("only_same_monitor", true));
        adw_switch_row_set_active(s->tb_active_workspaces,
                                  tb.value("only_active_workspaces", true));
        adw_switch_row_set_active(s->tb_pinned, tb.value("show_pinned_apps", true));
    } catch (const json::exception&) {
        // defaults
    }

    std::string am_display = "icon", am_icon = "rocket", am_custom, am_text = "Apps";
    int am_columns = 5;
    bool am_settings_btn = true, am_session_btn = true, am_multiline = false;
    bool am_show_search = true;
    try {
        const json am = s->root.value("bar", json::object()).value("app_menu", json::object());
        am_display = am.value("display", am_display);
        am_icon = am.value("icon", am_icon);
        am_custom = am.value("custom_icon", "");
        am_text = am.value("text", am_text);
        am_columns = std::clamp(am.value("columns", 5), 3, 8);
        am_settings_btn = am.value("show_settings_button", true);
        am_session_btn = am.value("show_session_button", true);
        am_multiline = am.value("multiline_labels", false);
        am_show_search = am.value("show_search", true);
    } catch (const json::exception&) {
        // defaults
    }

    bool bat_profiles = true, bat_brightness = true, bat_refresh = true;
    try {
        const json bat = s->root.value("bar", json::object()).value("battery", json::object());
        bat_profiles = bat.value("show_power_profiles", true);
        bat_brightness = bat.value("show_brightness", true);
        bat_refresh = bat.value("show_refresh_rate", true);
    } catch (const json::exception&) {
        // defaults
    }

    bool notif_badge = true, notif_hide_zero = false, notif_hide_zero_unread = false;
    try {
        const json notif =
            s->root.value("bar", json::object()).value("notifications", json::object());
        notif_badge = notif.value("show_unread_badge", true);
        notif_hide_zero = notif.value("hide_when_zero", false);
        notif_hide_zero_unread = notif.value("hide_when_zero_unread", false);
    } catch (const json::exception&) {
        // defaults
    }

    std::string aw_hide = "hidden", aw_text = "title", aw_empty = "default";
    bool aw_show_title = true, aw_icon = true;
    try {
        const json aw =
            s->root.value("bar", json::object()).value("active_window", json::object());
        aw_hide = aw.value("hide_mode", aw_hide);
        aw_show_title = aw.value("show_title", true);
        aw_text = aw.value("title_mode", aw_text);
        aw_empty = aw.value("no_window_text", aw_empty);
        aw_icon = aw.value("show_icon", true);
    } catch (const json::exception&) {
        // defaults
    }

    // Notifications page (top-level "notifications" object)
    bool nd_enabled = true, nd_dnd = false, nd_overlay = true, nd_respect = false;
    bool nd_clear = true, nd_s_low = true, nd_s_normal = true, nd_s_critical = true;
    std::string nd_density = "default", nd_location = "top_right";
    double nd_opacity = 1.0;
    int nd_d_low = 3, nd_d_normal = 8, nd_d_critical = 15;
    bool snd_enabled = false, snd_separate = false;
    double snd_volume = 0.5;
    std::string snd_low, snd_normal, snd_critical;
    std::string snd_excluded = "discord,firefox,chrome,chromium,edge";
    try {
        const json nd = s->root.value("notifications", json::object());
        nd_enabled = nd.value("enabled", true);
        nd_dnd = nd.value("do_not_disturb", false);
        nd_density = nd.value("density", nd_density);
        nd_location = nd.value("location", nd_location);
        nd_overlay = nd.value("overlay_layer", true);
        nd_opacity = std::clamp(nd.value("background_opacity", 1.0), 0.0, 1.0);
        nd_respect = nd.value("respect_expire_timeout", false);
        nd_d_low = std::clamp(nd.value("low_urgency_duration", 3), 1, 30);
        nd_d_normal = std::clamp(nd.value("normal_urgency_duration", 8), 1, 30);
        nd_d_critical = std::clamp(nd.value("critical_urgency_duration", 15), 1, 30);
        nd_clear = nd.value("clear_dismissed", true);
        const json hist = nd.value("save_to_history", json::object());
        nd_s_low = hist.value("low", true);
        nd_s_normal = hist.value("normal", true);
        nd_s_critical = hist.value("critical", true);
        const json sounds = nd.value("sounds", json::object());
        snd_enabled = sounds.value("enabled", false);
        snd_volume = std::clamp(sounds.value("volume", 0.5), 0.0, 1.0);
        snd_separate = sounds.value("separate_sounds", false);
        snd_low = sounds.value("low_sound_file", "");
        snd_normal = sounds.value("normal_sound_file", "");
        snd_critical = sounds.value("critical_sound_file", "");
        snd_excluded = sounds.value("excluded_apps", snd_excluded);
    } catch (const json::exception&) {
        // defaults
    }

    // Session menu page (top-level "session" object)
    std::string sm_mode = "dropdown", sm_layout = "single_row";
    bool sm_items[kSessionActionCount];
    for (gsize i = 0; i < kSessionActionCount; ++i)
        sm_items[i] = hyprshell::kSessionActions[i].default_on;
    try {
        const json sm = s->root.value("session", json::object());
        sm_mode = sm.value("mode", sm_mode);
        sm_layout = sm.value("fullscreen_layout", sm_layout);
        const json items = sm.value("items", json::object());
        for (gsize i = 0; i < kSessionActionCount; ++i)
            sm_items[i] = items.value(hyprshell::kSessionActions[i].key, sm_items[i]);
    } catch (const json::exception&) {
        // defaults
    }

    // Lock screen page (top-level "lock_screen" object)
    std::string lock_background;
    double lock_blur = 0.0;
    try {
        const json lock = s->root.value("lock_screen", json::object());
        lock_background = lock.value("background", "");
        lock_blur = std::clamp(lock.value("blur", 0.0), 0.0, 1.0);
    } catch (const json::exception&) {
        // defaults
    }

    // Night light page (top-level "night_light" object)
    bool nl_enabled = false, nl_forced = false;
    int nl_temp = 4000;
    std::string nl_sunrise = "06:30", nl_sunset = "18:30";
    {
        const json nl = s->root.value("night_light", json::object());
        nl_enabled = nl.value("enabled", false);
        nl_forced = nl.value("forced", false);
        nl_temp = std::clamp(nl.value("night_temp", 4000), kNlTempMin, kNlTempMax);
        nl_sunrise = nl.value("manual_sunrise", nl_sunrise);
        nl_sunset = nl.value("manual_sunset", nl_sunset);
    }

    // Wallpaper page (top-level "wallpaper" object)
    bool wp_transitions = true, wp_slideshow = false;
    std::string wp_directory, wp_fill = "crop", wp_order = "random";
    std::vector<std::string> wp_types = {"fade", "disc", "stripes", "wipe", "pixelate", "honeycomb"};
    int wp_duration = 1500, wp_interval = 300;
    {
        const json wp = s->root.value("wallpaper", json::object());
        wp_directory = wp.value("directory", "");
        wp_fill = wp.value("fill_mode", wp_fill);
        wp_transitions = wp.value("transitions_enabled", true);
        if (auto it = wp.find("transitions"); it != wp.end() && it->is_array()) {
            wp_types.clear();
            for (const auto& entry : *it)
                if (entry.is_string())
                    wp_types.push_back(entry.get<std::string>());
        }
        wp_duration = std::clamp(wp.value("transition_duration_ms", 1500), 500, 10000);
        wp_slideshow = wp.value("slideshow", false);
        wp_order = wp.value("slideshow_order", wp_order);
        wp_interval = std::clamp(wp.value("slideshow_interval_s", 300), 60, 86400);
    }

    // On-screen display page (top-level "osd" object)
    bool osd_enabled = true;
    std::string osd_location = "top_right";
    std::string osd_orientation = "auto";
    try {
        const json osd = s->root.value("osd", json::object());
        osd_enabled = osd.value("enabled", true);
        osd_location = osd.value("location", osd_location);
        osd_orientation = osd.value("orientation", osd_orientation);
    } catch (const json::exception&) {
        // defaults
    }

    // Idle page (top-level "idle" object)
    int idle_screen_off = 600, idle_lock = 660, idle_suspend = 1800;
    try {
        const json idle = s->root.value("idle", json::object());
        idle_screen_off = std::clamp(idle.value("screen_off_timeout", 600), 0, 86400);
        idle_lock = std::clamp(idle.value("lock_timeout", 660), 0, 86400);
        idle_suspend = std::clamp(idle.value("suspend_timeout", 1800), 0, 86400);
    } catch (const json::exception&) {
        // defaults
    }

    // Launcher page (top-level "launcher" object)
    bool lp_settings = true, lp_session = true, lp_web = false;
    bool lp_count = true, lp_all = true;
    try {
        const json lp = s->root.value("launcher", json::object());
        lp_settings = lp.value("enable_settings_search", true);
        lp_session = lp.value("enable_session_search", true);
        lp_web = lp.value("enable_web_search", false);
        lp_count = lp.value("show_result_count", true);
        lp_all = lp.value("show_all_apps", true);
    } catch (const json::exception&) {
        // defaults
    }

    int clock_fdow = 0;
    std::string fmt_h = "%H:%M %a, %b %d";
    std::string fmt_v = "%H %M";
    try {
        const json clock = s->root.value("bar", json::object()).value("clock", json::object());
        clock_fdow = std::clamp(clock.value("first_day_of_week", 0), 0, 1);
        fmt_h = clock.value("format_horizontal", fmt_h);
        fmt_v = clock.value("format_vertical", fmt_v);
    } catch (const json::exception&) {
        // defaults
    }

    s->loading = true;
    guint position_index = 0;
    for (guint i = 0; i < 4; ++i)
        if (position == kPositions[i])
            position_index = i;
    adw_combo_row_set_selected(s->position, position_index);
    for (guint i = 0; i < 3; ++i)
        if (visibility == kVisibilityKeys[i])
            adw_combo_row_set_selected(s->visibility, i);
    adw_switch_row_set_active(s->show_ws_switch, ws_switch);
    adw_switch_row_set_active(s->show_ws_empty, ws_empty);
    gtk_adjustment_set_value(s->opacity, opacity * 100.0);
    update_bar_visibility_rows(s);
    for (gsize i = 0; i < kModuleCount; ++i)
        adw_switch_row_set_active(s->modules[i], enabled[i]);
    adw_combo_row_set_selected(s->ws_mode, ws_mode == "fixed" ? 1 : 0);
    adw_spin_row_set_value(s->ws_count, ws_count);
    gtk_widget_set_sensitive(GTK_WIDGET(s->ws_count), ws_mode == "fixed");
    adw_switch_row_set_active(s->ws_wrap, ws_wrap);
    adw_switch_row_set_active(s->bt_auto, bt_auto);
    for (guint i = 0; i < 3; ++i)
        if (am_display == kAmDisplayKeys[i])
            adw_combo_row_set_selected(s->am_display, i);
    gtk_editable_set_text(GTK_EDITABLE(s->am_text), am_text.c_str());
    {
        guint icon_index = 0; // unknown keys fall back to the rocket
        for (guint i = 0; i < kAmPresetCount; ++i)
            if (am_icon == hyprshell::kAppMenuIconPresets[i].key)
                icon_index = i;
        if (am_icon == hyprshell::kAppMenuIconDistro)
            icon_index = kAmIconDistroIndex;
        else if (am_icon == hyprshell::kAppMenuIconCustom)
            icon_index = kAmIconCustomIndex;
        adw_combo_row_set_selected(s->am_icon, icon_index);
    }
    gtk_editable_set_text(GTK_EDITABLE(s->am_custom_icon), am_custom.c_str());
    adw_spin_row_set_value(s->am_columns, am_columns);
    adw_switch_row_set_active(s->am_settings_btn, am_settings_btn);
    adw_switch_row_set_active(s->am_session_btn, am_session_btn);
    adw_switch_row_set_active(s->am_multiline, am_multiline);
    adw_switch_row_set_active(s->am_show_search, am_show_search);
    update_am_rows(s);
    adw_switch_row_set_active(s->bat_profiles, bat_profiles);
    adw_switch_row_set_active(s->bat_brightness, bat_brightness);
    adw_switch_row_set_active(s->bat_refresh, bat_refresh);
    adw_switch_row_set_active(s->notif_badge, notif_badge);
    adw_switch_row_set_active(s->notif_hide_zero, notif_hide_zero);
    adw_switch_row_set_active(s->notif_hide_zero_unread, notif_hide_zero_unread);
    adw_combo_row_set_selected(s->clock_fdow, clock_fdow);
    gtk_editable_set_text(GTK_EDITABLE(s->clock_fmt_h), fmt_h.c_str());
    gtk_editable_set_text(GTK_EDITABLE(s->clock_fmt_v), fmt_v.c_str());
    for (guint i = 0; i < 3; ++i)
        if (aw_hide == kAwHideKeys[i])
            adw_combo_row_set_selected(s->aw_hide, i);
    adw_switch_row_set_active(s->aw_show_title, aw_show_title);
    adw_combo_row_set_selected(s->aw_text, aw_text == "appname" ? 1 : 0);
    for (guint i = 0; i < 3; ++i)
        if (aw_empty == kAwEmptyKeys[i])
            adw_combo_row_set_selected(s->aw_empty, i);
    adw_switch_row_set_active(s->aw_icon, aw_icon);
    update_aw_row_visibility(s);
    {
        bool cb_enabled = false, cb_images = true, cb_paste = false;
        std::string cb_position = "center";
        try {
            const json cb = s->root.value("clipboard", json::object());
            cb_enabled = cb.value("enabled", false);
            cb_images = cb.value("show_images", true);
            cb_paste = cb.value("paste_on_click", false);
            cb_position = cb.value("position", cb_position);
        } catch (const json::exception&) {
            // defaults
        }
        adw_switch_row_set_active(s->cb_enabled, cb_enabled);
        adw_switch_row_set_active(s->cb_show_images, cb_images);
        adw_switch_row_set_active(s->cb_paste, cb_paste);
        for (guint i = 0; i < kClipboardPositionCount; ++i)
            if (cb_position == kClipboardPositionKeys[i])
                adw_combo_row_set_selected(s->cb_position, i);
    }
    adw_switch_row_set_active(s->lp_settings_search, lp_settings);
    adw_switch_row_set_active(s->lp_session_search, lp_session);
    adw_switch_row_set_active(s->lp_web_search, lp_web);
    adw_switch_row_set_active(s->lp_result_count, lp_count);
    adw_switch_row_set_active(s->lp_show_all, lp_all);
    adw_combo_row_set_selected(s->sm_mode, sm_mode == "fullscreen" ? 1 : 0);
    adw_combo_row_set_selected(s->sm_layout, sm_layout == "grid" ? 1 : 0);
    for (gsize i = 0; i < kSessionActionCount; ++i)
        adw_switch_row_set_active(s->sm_items[i], sm_items[i]);
    update_sm_rows(s);
    gtk_editable_set_text(GTK_EDITABLE(s->lock_background), lock_background.c_str());
    gtk_adjustment_set_value(s->lock_blur, std::round(lock_blur * 100.0));
    gtk_editable_set_text(GTK_EDITABLE(s->wp_directory), wp_directory.c_str());
    for (guint i = 0; i < kWpFillCount; ++i)
        if (wp_fill == kWpFillKeys[i])
            adw_combo_row_set_selected(s->wp_fill, i);
    adw_switch_row_set_active(s->wp_transitions, wp_transitions);
    adw_switch_row_set_active(s->nl_enabled, nl_enabled && s->nl_available);

    // user interface (ui.*)
    update_ui_style_tiles(s);
    {
        std::string accent = ui_object(s).value("accent", std::string(hyprshell::kDefaultAccent));
        gchar* lower = g_ascii_strdown(accent.c_str(), -1);
        for (gsize i = 0; i < kAccentCount; ++i) // no match (custom hex) = none checked
            gtk_check_button_set_active(GTK_CHECK_BUTTON(s->ui_swatches[i]),
                                        g_strcmp0(lower, kAccents[i].hex) == 0);
        g_free(lower);
        const std::string font = ui_object(s).value("font", std::string(hyprshell::kDefaultFont));
        PangoFontDescription* desc = pango_font_description_from_string(font.c_str());
        gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(s->ui_font_button), desc);
        pango_font_description_free(desc);
    }
    gtk_adjustment_set_value(s->nl_temp, nl_temp);
    for (guint i = 0; i < kNlTimeOptions; ++i) {
        if (nl_time_option(i) == nl_sunrise)
            adw_combo_row_set_selected(s->nl_sunrise, i);
        if (nl_time_option(i) == nl_sunset)
            adw_combo_row_set_selected(s->nl_sunset, i);
    }
    adw_switch_row_set_active(s->nl_forced, nl_forced);
    update_nl_rows(s);
    for (gsize i = 0; i < kWpTransitionCount; ++i)
        adw_switch_row_set_active(s->wp_transition[i],
                                  std::find(wp_types.begin(), wp_types.end(), kWpTransitions[i].key) !=
                                      wp_types.end());
    gtk_adjustment_set_value(s->wp_duration, wp_duration);
    adw_switch_row_set_active(s->wp_slideshow, wp_slideshow);
    adw_combo_row_set_selected(s->wp_order, wp_order == "alphabetical" ? 1 : 0);
    adw_spin_row_set_value(s->wp_interval, std::round(wp_interval / 60.0));
    update_wp_rows(s);
    wp_set_directory(s, wp_directory);
    s->wp_current = wp_effective_current(s);
    wp_update_highlight(s);
    adw_switch_row_set_active(s->osd_enabled, osd_enabled);
    for (guint i = 0; i < kOsdLocationCount; ++i)
        if (osd_location == kOsdLocationKeys[i])
            adw_combo_row_set_selected(s->osd_location, i);
    for (guint i = 0; i < 3; ++i)
        if (osd_orientation == kOsdOrientationKeys[i])
            adw_combo_row_set_selected(s->osd_orientation, i);
    adw_spin_row_set_value(s->idle_screen_off, idle_screen_off);
    adw_spin_row_set_value(s->idle_lock, idle_lock);
    adw_spin_row_set_value(s->idle_suspend, idle_suspend);
    adw_switch_row_set_active(s->nd_enabled, nd_enabled);
    adw_switch_row_set_active(s->nd_dnd, nd_dnd);
    adw_combo_row_set_selected(s->nd_density, nd_density == "compact" ? 1 : 0);
    for (guint i = 0; i < 6; ++i)
        if (nd_location == kNdLocationKeys[i])
            adw_combo_row_set_selected(s->nd_location, i);
    adw_switch_row_set_active(s->nd_overlay, nd_overlay);
    gtk_adjustment_set_value(s->nd_opacity, nd_opacity * 100.0);
    adw_switch_row_set_active(s->nd_respect_expire, nd_respect);
    adw_spin_row_set_value(s->nd_dur_low, nd_d_low);
    adw_spin_row_set_value(s->nd_dur_normal, nd_d_normal);
    adw_spin_row_set_value(s->nd_dur_critical, nd_d_critical);
    adw_switch_row_set_active(s->nd_clear_dismissed, nd_clear);
    adw_switch_row_set_active(s->nd_save_low, nd_s_low);
    adw_switch_row_set_active(s->nd_save_normal, nd_s_normal);
    adw_switch_row_set_active(s->nd_save_critical, nd_s_critical);
    adw_switch_row_set_active(s->nd_snd_enabled, snd_enabled);
    gtk_adjustment_set_value(s->nd_snd_volume, snd_volume * 100.0);
    adw_switch_row_set_active(s->nd_snd_separate, snd_separate);
    gtk_editable_set_text(GTK_EDITABLE(s->nd_snd_unified), snd_normal.c_str());
    gtk_editable_set_text(GTK_EDITABLE(s->nd_snd_low), snd_low.c_str());
    gtk_editable_set_text(GTK_EDITABLE(s->nd_snd_normal), snd_normal.c_str());
    gtk_editable_set_text(GTK_EDITABLE(s->nd_snd_critical), snd_critical.c_str());
    gtk_editable_set_text(GTK_EDITABLE(s->nd_snd_excluded), snd_excluded.c_str());
    update_nd_rows(s);
    rebuild_rule_rows(s);
    s->loading = false;
}

// bar.workspaces section of the config, created on demand
json& workspaces_object(Settings* s) {
    json& bar = bar_object(s);
    if (!bar["workspaces"].is_object())
        bar["workspaces"] = json::object();
    return bar["workspaces"];
}

void on_ws_mode_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    const bool fixed = adw_combo_row_get_selected(s->ws_mode) == 1;
    gtk_widget_set_sensitive(GTK_WIDGET(s->ws_count), fixed);
    if (s->loading)
        return;
    workspaces_object(s)["mode"] = fixed ? "fixed" : "dynamic";
    save(s);
}

void on_ws_count_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    workspaces_object(s)["fixed_count"] =
        static_cast<int>(adw_spin_row_get_value(s->ws_count));
    save(s);
}

void on_ws_wrap_toggled(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    workspaces_object(s)["scroll_wrap"] = adw_switch_row_get_active(s->ws_wrap) != FALSE;
    save(s);
}

json& bluetooth_object(Settings* s) {
    json& bar = bar_object(s);
    if (!bar["bluetooth"].is_object())
        bar["bluetooth"] = json::object();
    return bar["bluetooth"];
}

void on_bt_auto_toggled(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    bluetooth_object(s)["auto_connect"] = adw_switch_row_get_active(s->bt_auto) != FALSE;
    save(s);
}

// -- App menu subpage: bar.app_menu -------------------------------------------

json& control_center_object(Settings* s) {
    json& bar = bar_object(s);
    if (!bar["control_center"].is_object())
        bar["control_center"] = json::object();
    return bar["control_center"];
}

void on_cc_toggled(GObject* row, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const char* key = static_cast<const char*>(g_object_get_data(row, "config-key"));
    if (key == nullptr)
        return;
    control_center_object(s)[key] = adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE;
    save(s);
}

// -- Taskbar subpage: bar.taskbar ---------------------------------------------

json& taskbar_object(Settings* s) {
    json& bar = bar_object(s);
    if (!bar["taskbar"].is_object())
        bar["taskbar"] = json::object();
    return bar["taskbar"];
}

void on_tb_hide_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto selected = adw_combo_row_get_selected(s->tb_hide);
    taskbar_object(s)["hide_mode"] = kTbHideKeys[selected < 3 ? selected : 1];
    save(s);
}

void on_tb_toggled(GObject* row, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const char* key = static_cast<const char*>(g_object_get_data(row, "config-key"));
    if (key == nullptr)
        return;
    taskbar_object(s)[key] = adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE;
    save(s);
}

json& app_menu_object(Settings* s) {
    json& bar = bar_object(s);
    if (!bar["app_menu"].is_object())
        bar["app_menu"] = json::object();
    return bar["app_menu"];
}

const char* am_icon_key(guint index) {
    if (index < kAmPresetCount)
        return hyprshell::kAppMenuIconPresets[index].key;
    if (index == kAmIconDistroIndex)
        return hyprshell::kAppMenuIconDistro;
    return hyprshell::kAppMenuIconCustom;
}

// label row only with a text display, icon rows only with an icon display,
// the custom-icon entry only for the Custom choice
void update_am_rows(Settings* s) {
    const guint display = adw_combo_row_get_selected(s->am_display);
    const bool show_text = display != 0;
    const bool show_icon = display != 2;
    gtk_widget_set_visible(GTK_WIDGET(s->am_text), show_text);
    gtk_widget_set_visible(GTK_WIDGET(s->am_icon), show_icon);
    gtk_widget_set_visible(GTK_WIDGET(s->am_custom_icon),
                           show_icon &&
                               adw_combo_row_get_selected(s->am_icon) == kAmIconCustomIndex);
}

void on_am_display_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    update_am_rows(s);
    if (s->loading)
        return;
    const auto selected = adw_combo_row_get_selected(s->am_display);
    app_menu_object(s)["display"] = kAmDisplayKeys[selected < 3 ? selected : 0];
    save(s);
}

void on_am_icon_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    update_am_rows(s);
    if (s->loading)
        return;
    app_menu_object(s)["icon"] = am_icon_key(adw_combo_row_get_selected(s->am_icon));
    save(s);
}

// entry rows with an "am-key" (label text, custom icon)
void on_am_entry_changed(GtkEditable* row, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto* key = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "am-key"));
    app_menu_object(s)[key] = gtk_editable_get_text(row);
    save(s);
}

void on_am_columns_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    app_menu_object(s)["columns"] = static_cast<int>(adw_spin_row_get_value(s->am_columns));
    save(s);
}

// switches with an "am-key" (settings button, session button)
void on_am_toggled(GObject* row, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto* key = static_cast<const char*>(g_object_get_data(row, "am-key"));
    app_menu_object(s)[key] = adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE;
    save(s);
}

json& battery_object(Settings* s) {
    json& bar = bar_object(s);
    if (!bar["battery"].is_object())
        bar["battery"] = json::object();
    return bar["battery"];
}

void on_battery_toggled(GObject* row, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto* key = static_cast<const char*>(g_object_get_data(row, "battery-key"));
    battery_object(s)[key] = adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE;
    save(s);
}

json& notifications_object(Settings* s) {
    json& bar = bar_object(s);
    if (!bar["notifications"].is_object())
        bar["notifications"] = json::object();
    return bar["notifications"];
}

void on_notifications_toggled(GObject* row, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto* key = static_cast<const char*>(g_object_get_data(row, "notif-key"));
    notifications_object(s)[key] = adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE;
    save(s);
}

// -- Launcher page: the top-level "launcher" config object -------------------

json& launcher_object(Settings* s) {
    if (!s->root.is_object())
        s->root = json::object();
    if (!s->root["launcher"].is_object())
        s->root["launcher"] = json::object();
    return s->root["launcher"];
}

void on_launcher_toggled(GObject* row, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto* key = static_cast<const char*>(g_object_get_data(row, "launcher-key"));
    launcher_object(s)[key] = adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE;
    save(s);
}

// -- Clipboard page: the top-level "clipboard" config object -------------------

json& clipboard_object(Settings* s) {
    if (!s->root.is_object())
        s->root = json::object();
    if (!s->root["clipboard"].is_object())
        s->root["clipboard"] = json::object();
    return s->root["clipboard"];
}

void on_clipboard_toggled(GObject* row, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto* key = static_cast<const char*>(g_object_get_data(row, "clipboard-key"));
    clipboard_object(s)[key] = adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE;
    save(s);
}

void on_clipboard_position_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto selected = adw_combo_row_get_selected(s->cb_position);
    clipboard_object(s)["position"] =
        kClipboardPositionKeys[selected < kClipboardPositionCount ? selected : 0];
    save(s);
}

// -- Session menu page: the top-level "session" config object -----------------

json& session_object(Settings* s) {
    if (!s->root.is_object())
        s->root = json::object();
    if (!s->root["session"].is_object())
        s->root["session"] = json::object();
    return s->root["session"];
}

json& session_items_object(Settings* s) {
    json& sm = session_object(s);
    if (!sm["items"].is_object())
        sm["items"] = json::object();
    return sm["items"];
}

// the layout row only matters for the fullscreen style
void update_sm_rows(Settings* s) {
    gtk_widget_set_visible(GTK_WIDGET(s->sm_layout),
                           adw_combo_row_get_selected(s->sm_mode) == 1);
}

void on_sm_mode_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    update_sm_rows(s);
    if (s->loading)
        return;
    const auto selected = adw_combo_row_get_selected(s->sm_mode);
    session_object(s)["mode"] = kSmModeKeys[selected < 2 ? selected : 0];
    save(s);
}

void on_sm_layout_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto selected = adw_combo_row_get_selected(s->sm_layout);
    session_object(s)["fullscreen_layout"] = kSmLayoutKeys[selected < 2 ? selected : 0];
    save(s);
}

// switches with an "sm-key" (one per session action)
void on_sm_item_toggled(GObject* row, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto* key = static_cast<const char*>(g_object_get_data(row, "sm-key"));
    session_items_object(s)[key] = adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE;
    save(s);
}

// -- Idle page: the top-level "idle" config object -----------------------------

json& idle_object(Settings* s) {
    if (!s->root.is_object())
        s->root = json::object();
    if (!s->root["idle"].is_object())
        s->root["idle"] = json::object();
    return s->root["idle"];
}

// spin rows with an "idle-key" (the three stage timeouts, seconds)
void on_idle_timeout_changed(GObject* row, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto* key = static_cast<const char*>(g_object_get_data(row, "idle-key"));
    idle_object(s)[key] = static_cast<int>(adw_spin_row_get_value(ADW_SPIN_ROW(row)));
    save(s);
}

// -- Lock screen page: the top-level "lock_screen" config object ---------------

json& lock_object(Settings* s) {
    if (!s->root.is_object())
        s->root = json::object();
    if (!s->root["lock_screen"].is_object())
        s->root["lock_screen"] = json::object();
    return s->root["lock_screen"];
}

void on_lock_background_changed(GtkEditable* row, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    lock_object(s)["background"] = gtk_editable_get_text(row);
    save(s);
}

void on_lock_blur_changed(GtkAdjustment* adjustment, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    lock_object(s)["blur"] = std::round(gtk_adjustment_get_value(adjustment)) / 100.0;
    save(s);
}

// -- Wallpaper page: the top-level "wallpaper" config object ------------------

json& wp_object(Settings* s) {
    if (!s->root.is_object())
        s->root = json::object();
    if (!s->root["wallpaper"].is_object())
        s->root["wallpaper"] = json::object();
    return s->root["wallpaper"];
}

// transition details hide behind the transitions switch, slideshow details
// behind its switch
void update_wp_rows(Settings* s) {
    const bool transitions = adw_switch_row_get_active(s->wp_transitions) != FALSE;
    gtk_widget_set_visible(s->wp_transition_types, transitions);
    gtk_widget_set_visible(s->wp_duration_row, transitions);
    const bool slideshow = adw_switch_row_get_active(s->wp_slideshow) != FALSE;
    gtk_widget_set_visible(GTK_WIDGET(s->wp_order), slideshow);
    gtk_widget_set_visible(GTK_WIDGET(s->wp_interval), slideshow);
}

void on_wp_directory_changed(GtkEditable* row, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const std::string directory = gtk_editable_get_text(row);
    wp_object(s)["directory"] = directory;
    save(s);
    wp_set_directory(s, directory);
}

// folder button on the directory row: pick a folder into the entry (its
// changed handler then writes the config and rescans)
void on_wp_browse_clicked(GtkButton* button, gpointer) {
    auto* entry = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(button), "target-entry"));
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select wallpaper folder");
    const char* current = gtk_editable_get_text(GTK_EDITABLE(entry));
    std::string initial = (current != nullptr && *current != '\0')
                              ? std::string(current)
                              : std::string(g_get_home_dir()) + "/Pictures";
    if (!initial.empty() && initial[0] == '~')
        initial = std::string(g_get_home_dir()) + initial.substr(1);
    if (g_file_test(initial.c_str(), G_FILE_TEST_IS_DIR)) {
        GFile* folder = g_file_new_for_path(initial.c_str());
        gtk_file_dialog_set_initial_folder(dialog, folder);
        g_object_unref(folder);
    }
    auto* root = gtk_widget_get_root(GTK_WIDGET(button));
    gtk_file_dialog_select_folder(
        dialog, GTK_WINDOW(root), nullptr,
        [](GObject* source, GAsyncResult* result, gpointer entry_ptr) {
            GFile* file =
                gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), result, nullptr);
            if (file != nullptr) {
                gchar* path = g_file_get_path(file);
                if (path != nullptr)
                    gtk_editable_set_text(GTK_EDITABLE(entry_ptr), path);
                g_free(path);
                g_object_unref(file);
            }
        },
        entry);
    g_object_unref(dialog);
}

void on_wp_fill_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto selected = adw_combo_row_get_selected(s->wp_fill);
    wp_object(s)["fill_mode"] = kWpFillKeys[selected < kWpFillCount ? selected : 1];
    save(s);
}

void on_wp_transitions_toggled(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    update_wp_rows(s);
    if (s->loading)
        return;
    wp_object(s)["transitions_enabled"] = adw_switch_row_get_active(s->wp_transitions) != FALSE;
    save(s);
}

// the four switches form Noctalia's transitionType array (a random one of the
// checked types is used per change); none checked = instant swap
void on_wp_transition_type_toggled(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    json list = json::array();
    for (gsize i = 0; i < kWpTransitionCount; ++i)
        if (adw_switch_row_get_active(s->wp_transition[i]))
            list.push_back(kWpTransitions[i].key);
    wp_object(s)["transitions"] = list;
    save(s);
}

void on_wp_duration_changed(GtkAdjustment* adjustment, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    wp_object(s)["transition_duration_ms"] = (int)std::round(gtk_adjustment_get_value(adjustment));
    save(s);
}

void on_wp_slideshow_toggled(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    update_wp_rows(s);
    if (s->loading)
        return;
    wp_object(s)["slideshow"] = adw_switch_row_get_active(s->wp_slideshow) != FALSE;
    save(s);
}

void on_wp_order_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto selected = adw_combo_row_get_selected(s->wp_order);
    wp_object(s)["slideshow_order"] = kWpOrderKeys[selected < 2 ? selected : 0];
    save(s);
}

void on_wp_interval_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    // Noctalia: minutes in the UI, seconds on disk
    wp_object(s)["slideshow_interval_s"] = (int)std::round(adw_spin_row_get_value(s->wp_interval)) * 60;
    save(s);
}

// -- Night light page: the top-level "night_light" config object ---------------

json& nl_object(Settings* s) {
    if (!s->root.is_object())
        s->root = json::object();
    if (!s->root["night_light"].is_object())
        s->root["night_light"] = json::object();
    return s->root["night_light"];
}

std::string nl_time_option(guint index) {
    return g_strdup_printf("%02u:%02u", index / 2, (index % 2) * 30);
}

// Noctalia: everything below the enable switch is greyed out while off; the
// sunrise/sunset times are greyed out while force activation is on
void update_nl_rows(Settings* s) {
    const bool enabled = adw_switch_row_get_active(s->nl_enabled) != FALSE;
    gtk_widget_set_sensitive(s->nl_temp_group, enabled && s->nl_available);
    gtk_widget_set_sensitive(s->nl_schedule_group, enabled && s->nl_available);
    const bool forced = adw_switch_row_get_active(s->nl_forced) != FALSE;
    gtk_widget_set_sensitive(GTK_WIDGET(s->nl_sunrise), !forced);
    gtk_widget_set_sensitive(GTK_WIDGET(s->nl_sunset), !forced);
}

// The settings window follows the shell's theme too: forced dark/light via
// AdwStyleManager and the accent as libadwaita's accent_bg_color (the
// light-mode primary — a mid tone that carries white text, which libadwaita
// expects of its accent; the dark-mode primary would be too pale).
void apply_settings_theme(Settings* s) {
    const bool dark = ui_object(s).value("dark_mode", true);
    adw_style_manager_set_color_scheme(adw_style_manager_get_default(),
                                       dark ? ADW_COLOR_SCHEME_FORCE_DARK
                                            : ADW_COLOR_SCHEME_FORCE_LIGHT);
    std::string accent = ui_object(s).value("accent", std::string(hyprshell::kDefaultAccent));
    hyprshell::Rgb probe;
    if (!hyprshell::parse_hex_color(accent, probe))
        accent = hyprshell::kDefaultAccent;
    const auto palette = hyprshell::derive_palette(accent, /*dark=*/false);
    // libadwaita >= 1.6 reads its accent from CSS variables on :root (the
    // legacy @define-color names are ignored there), so set both forms
    const std::string primary = hyprshell::palette_color(palette, "mPrimary");
    const std::string css = "@define-color accent_bg_color " + primary +
                            ";\n@define-color accent_fg_color #ffffff;\n"
                            ":root { --accent-bg-color: " + primary +
                            "; --accent-fg-color: #ffffff; --accent-color: " + primary + "; }\n";
    if (s->ui_accent_css == nullptr) {
        s->ui_accent_css = gtk_css_provider_new();
        gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                                   GTK_STYLE_PROVIDER(s->ui_accent_css),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    gtk_css_provider_load_from_string(s->ui_accent_css, css.c_str());
}

void update_ui_style_tiles(Settings* s) {
    const bool dark = ui_object(s).value("dark_mode", true);
    for (int i = 0; i < 2; ++i) {
        if (s->ui_style_preview[i] == nullptr)
            continue;
        if ((i == 1) == dark)
            gtk_widget_add_css_class(s->ui_style_preview[i], "selected");
        else
            gtk_widget_remove_css_class(s->ui_style_preview[i], "selected");
    }
}

void on_ui_style_clicked(GtkButton* button, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    const bool dark = g_object_get_data(G_OBJECT(button), "dark") != nullptr;
    ui_object(s)["dark_mode"] = dark;
    update_ui_style_tiles(s);
    save(s);
    apply_settings_theme(s);
}

void on_ui_swatch_toggled(GtkCheckButton* button, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading || !gtk_check_button_get_active(button))
        return;
    const char* hex = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "hex"));
    if (hex == nullptr)
        return;
    ui_object(s)["accent"] = hex;
    save(s);
    apply_settings_theme(s);
}

// GNOME Settings' style preview: a blue desktop with two windows behind and
// one in front, light or dark. Drawn in cairo so it needs no image assets.
void draw_style_preview(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer) {
    const bool dark = g_object_get_data(G_OBJECT(area), "dark") != nullptr;
    const auto rounded = [cr](double x, double y, double w, double h, double r) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
        cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
        cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
        cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
        cairo_close_path(cr);
    };
    // desktop
    rounded(0, 0, width, height, 12);
    cairo_set_source_rgb(cr, 0.02, 0.25, 0.56);
    cairo_fill(cr);
    // two windows behind (always dark, like GNOME's preview)
    const auto window = [&](double x, double y, double w, double h, double body_r,
                            double body_g, double body_b, double head_r, double head_g,
                            double head_b) {
        rounded(x, y, w, h, 5);
        cairo_set_source_rgb(cr, body_r, body_g, body_b);
        cairo_fill(cr);
        rounded(x, y, w, 12, 5);
        cairo_rectangle(cr, x, y + 6, w, 6);
        cairo_set_source_rgb(cr, head_r, head_g, head_b);
        cairo_fill(cr);
    };
    window(width * 0.36, height * 0.15, width * 0.48, height * 0.52, 0.17, 0.17, 0.17, 0.22,
           0.22, 0.22);
    window(width * 0.52, height * 0.15, width * 0.32, height * 0.52, 0.17, 0.17, 0.17, 0.22,
           0.22, 0.22);
    // the front window shows the style
    if (dark)
        window(width * 0.15, height * 0.38, width * 0.50, height * 0.46, 0.20, 0.20, 0.20, 0.27,
               0.27, 0.27);
    else
        window(width * 0.15, height * 0.38, width * 0.50, height * 0.46, 0.98, 0.98, 0.98, 0.92,
               0.92, 0.92);
}

GtkWidget* make_style_tile(Settings* s, const char* label, bool dark) {
    GtkWidget* button = gtk_button_new();
    gtk_widget_add_css_class(button, "style-tile");
    gtk_widget_add_css_class(button, "flat");
    if (dark)
        g_object_set_data(G_OBJECT(button), "dark", GINT_TO_POINTER(1));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget* preview = gtk_drawing_area_new();
    gtk_widget_add_css_class(preview, "style-preview");
    gtk_widget_set_size_request(preview, 172, 130);
    if (dark)
        g_object_set_data(G_OBJECT(preview), "dark", GINT_TO_POINTER(1));
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(preview), draw_style_preview, nullptr,
                                   nullptr);
    gtk_box_append(GTK_BOX(box), preview);
    gtk_box_append(GTK_BOX(box), gtk_label_new(label));
    gtk_button_set_child(GTK_BUTTON(button), box);
    g_signal_connect(button, "clicked", G_CALLBACK(on_ui_style_clicked), s);
    s->ui_style_preview[dark ? 1 : 0] = preview;
    return button;
}

void on_ui_font_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    PangoFontDescription* desc =
        gtk_font_dialog_button_get_font_desc(GTK_FONT_DIALOG_BUTTON(s->ui_font_button));
    const char* family = desc != nullptr ? pango_font_description_get_family(desc) : nullptr;
    if (family == nullptr || *family == '\0')
        return;
    ui_object(s)["font"] = family;
    save(s);
}


void on_nl_enabled_toggled(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    update_nl_rows(s);
    if (s->loading)
        return;
    const bool enabled = adw_switch_row_get_active(s->nl_enabled) != FALSE;
    nl_object(s)["enabled"] = enabled;
    if (!enabled) { // Noctalia also drops force when disabling
        nl_object(s)["forced"] = false;
        adw_switch_row_set_active(s->nl_forced, FALSE);
    }
    save(s);
}

void on_nl_temp_changed(GtkAdjustment* adjustment, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    // Noctalia writes on release; GtkScale has no release signal, so coalesce
    if (s->nl_temp_source != 0)
        g_source_remove(s->nl_temp_source);
    s->nl_temp_source = g_timeout_add(
        250,
        [](gpointer data) -> gboolean {
            auto* s = static_cast<Settings*>(data);
            s->nl_temp_source = 0;
            nl_object(s)["night_temp"] = (int)std::round(gtk_adjustment_get_value(s->nl_temp));
            save(s);
            return G_SOURCE_REMOVE;
        },
        s);
    (void)adjustment;
}

void on_nl_time_changed(GObject* row, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const guint selected = adw_combo_row_get_selected(ADW_COMBO_ROW(row));
    if (selected >= kNlTimeOptions)
        return;
    const char* key = ADW_COMBO_ROW(row) == s->nl_sunrise ? "manual_sunrise" : "manual_sunset";
    nl_object(s)[key] = nl_time_option(selected);
    save(s);
}

void on_nl_forced_toggled(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    update_nl_rows(s);
    if (s->loading)
        return;
    nl_object(s)["forced"] = adw_switch_row_get_active(s->nl_forced) != FALSE;
    save(s);
}

// -- wallpaper grid: thumbnails -----------------------------------------------

std::string wp_expand(std::string path) {
    if (!path.empty() && path[0] == '~')
        path = std::string(g_get_home_dir()) + path.substr(1);
    return path;
}

// ~/.cache/hypr-shell/wallpapers/thumbnails/<sha256(path@384x384@mtime)>.png —
// Noctalia's ImageCacheService key, so a re-saved image gets a fresh thumbnail
std::string wp_thumb_cache_path(const std::string& path) {
    static std::string dir;
    if (dir.empty()) {
        dir = std::string(g_get_user_cache_dir()) + "/hypr-shell/wallpapers/thumbnails";
        g_mkdir_with_parents(dir.c_str(), 0700);
    }
    struct stat st{};
    const std::string mtime = stat(path.c_str(), &st) == 0 ? std::to_string(st.st_mtime) : "unknown";
    const std::string key = path + "@" + std::to_string(kWpThumbCache) + "x" +
                            std::to_string(kWpThumbCache) + "@" + mtime;
    gchar* hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, key.c_str(), -1);
    const std::string result = dir + "/" + hash + ".png";
    g_free(hash);
    return result;
}

struct WpThumbJob {
    GtkPicture* picture; // ref held while decoding
    std::string path;
    std::string cache_path;
};

// decode → center-crop to a 384px square → cache as PNG → show
void wp_thumb_decoded(GObject*, GAsyncResult* result, gpointer data) {
    std::unique_ptr<WpThumbJob> job(static_cast<WpThumbJob*>(data));
    GError* error = nullptr;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_stream_finish(result, &error);
    if (pixbuf == nullptr) {
        g_message("wallpaper thumbnail: %s: %s", job->path.c_str(), error ? error->message : "?");
        g_clear_error(&error);
        g_object_unref(job->picture);
        return;
    }
    GdkPixbuf* oriented = gdk_pixbuf_apply_embedded_orientation(pixbuf);
    g_object_unref(pixbuf);
    const int w = gdk_pixbuf_get_width(oriented);
    const int h = gdk_pixbuf_get_height(oriented);
    const int side = std::min({w, h, kWpThumbCache});
    GdkPixbuf* sub = gdk_pixbuf_new_subpixbuf(oriented, (w - side) / 2, (h - side) / 2, side, side);
    GdkPixbuf* square = gdk_pixbuf_copy(sub);
    g_object_unref(sub);
    g_object_unref(oriented);
    if (!gdk_pixbuf_save(square, job->cache_path.c_str(), "png", &error, "compression", "1", nullptr)) {
        g_message("wallpaper thumbnail: cannot cache %s: %s", job->cache_path.c_str(),
                  error ? error->message : "?");
        g_clear_error(&error);
    }
    GdkTexture* texture = gdk_texture_new_for_pixbuf(square);
    gtk_picture_set_paintable(job->picture, GDK_PAINTABLE(texture));
    g_object_unref(texture);
    g_object_unref(square);
    g_object_unref(job->picture);
}

void wp_thumb_opened(GObject* source, GAsyncResult* result, gpointer data) {
    auto* job = static_cast<WpThumbJob*>(data);
    GError* error = nullptr;
    GFileInputStream* stream = g_file_read_finish(G_FILE(source), result, &error);
    if (stream == nullptr) {
        g_message("wallpaper thumbnail: cannot open %s: %s", job->path.c_str(),
                  error ? error->message : "?");
        g_clear_error(&error);
        g_object_unref(job->picture);
        delete job;
        return;
    }
    // cover-scale the shorter side to 384 (header read is cheap), crop after
    int image_w = 0, image_h = 0;
    int target_w = kWpThumbCache, target_h = kWpThumbCache;
    if (gdk_pixbuf_get_file_info(job->path.c_str(), &image_w, &image_h) != nullptr && image_w > 0 &&
        image_h > 0) {
        if (image_w > image_h)
            target_w = -1;
        else
            target_h = -1;
    }
    gdk_pixbuf_new_from_stream_at_scale_async(G_INPUT_STREAM(stream), target_w, target_h, TRUE,
                                              nullptr, wp_thumb_decoded, job);
    g_object_unref(stream);
}

void wp_load_thumbnail(GtkPicture* picture, const std::string& path) {
    const std::string cache = wp_thumb_cache_path(path);
    if (g_file_test(cache.c_str(), G_FILE_TEST_IS_REGULAR)) {
        gtk_picture_set_filename(picture, cache.c_str());
        return;
    }
    auto* job = new WpThumbJob{GTK_PICTURE(g_object_ref(picture)), path, cache};
    GFile* file = g_file_new_for_path(path.c_str());
    g_file_read_async(file, G_PRIORITY_LOW, nullptr, wp_thumb_opened, job);
    g_object_unref(file);
}

// The wallpaper on screen: the shell persists slideshow picks in
// ~/.cache/hypr-shell/wallpaper.json; before it ever wrote one, the config's
// own pick is the answer.
std::string wp_effective_current(Settings* s) {
    const std::string state_path = std::string(g_get_user_cache_dir()) + "/hypr-shell/wallpaper.json";
    gchar* contents = nullptr;
    gsize length = 0;
    if (g_file_get_contents(state_path.c_str(), &contents, &length, nullptr)) {
        std::string current;
        try {
            current = json::parse(contents, contents + length).value("current", "");
        } catch (const json::exception&) {
        }
        g_free(contents);
        if (!current.empty())
            return current;
    }
    return wp_expand(s->root.value("wallpaper", json::object()).value("current", ""));
}

void wp_update_highlight(Settings* s) {
    for (GtkWidget* child = gtk_widget_get_first_child(s->wp_grid); child != nullptr;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget* tile = gtk_flow_box_child_get_child(GTK_FLOW_BOX_CHILD(child));
        const char* path = static_cast<const char*>(g_object_get_data(G_OBJECT(tile), "wp-path"));
        const bool current = path != nullptr && s->wp_current == path;
        if (current)
            gtk_widget_add_css_class(tile, "current");
        else
            gtk_widget_remove_css_class(tile, "current");
    }
}

void on_wp_tile_clicked(GtkButton* button, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    const char* path = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "wp-path"));
    if (path == nullptr)
        return;
    wp_object(s)["current"] = path;
    save(s);
    s->wp_current = path;
    wp_update_highlight(s);
}

// Noctalia's grid tile: cover-cropped rounded thumbnail (accent border on the
// current one), filename underneath, dimmed until hovered
GtkWidget* wp_make_tile(Settings* s, const std::string& path) {
    GtkWidget* button = gtk_button_new();
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_add_css_class(button, "wp-tile");
    gtk_widget_set_tooltip_text(button, path.c_str());
    g_object_set_data_full(G_OBJECT(button), "wp-path", g_strdup(path.c_str()), g_free);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget* frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(frame, "wp-frame");
    gtk_widget_set_halign(frame, GTK_ALIGN_CENTER);
    GtkWidget* picture = gtk_picture_new();
    gtk_widget_add_css_class(picture, "wp-thumb");
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_COVER);
    gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
    gtk_widget_set_size_request(picture, kWpThumbWidth, kWpThumbHeight);
    gtk_widget_set_overflow(picture, GTK_OVERFLOW_HIDDEN);
    gtk_box_append(GTK_BOX(frame), picture);
    gtk_box_append(GTK_BOX(box), frame);

    gchar* basename = g_path_get_basename(path.c_str());
    GtkWidget* label = gtk_label_new(basename);
    g_free(basename);
    gtk_widget_add_css_class(label, "caption");
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 1); // never wider than the thumbnail
    gtk_label_set_xalign(GTK_LABEL(label), 0.5f);
    gtk_box_append(GTK_BOX(box), label);

    gtk_button_set_child(GTK_BUTTON(button), box);
    g_signal_connect(button, "clicked", G_CALLBACK(on_wp_tile_clicked), s);
    wp_load_thumbnail(GTK_PICTURE(picture), path);
    return button;
}

void wp_rebuild_grid(Settings* s) {
    while (GtkWidget* child = gtk_widget_get_first_child(s->wp_grid))
        gtk_flow_box_remove(GTK_FLOW_BOX(s->wp_grid), child);
    for (const auto& path : s->wp_images)
        gtk_flow_box_append(GTK_FLOW_BOX(s->wp_grid), wp_make_tile(s, path));
    const char* status = s->wp_scanned_dir.empty() ? "Choose a folder to see its images here."
                         : s->wp_images.empty()   ? "No images found in this folder."
                                                  : nullptr;
    gtk_label_set_text(GTK_LABEL(s->wp_grid_status), status != nullptr ? status : "");
    gtk_widget_set_visible(s->wp_grid_status, status != nullptr);
    gtk_widget_set_visible(s->wp_grid, !s->wp_images.empty());
    // The page's viewport allocates children at their MINIMUM height (GTK's
    // default scroll policy) and a scrolled window's minimum is tiny, so once
    // the page overflows the grid collapsed to a sliver. Pin the scroller's
    // minimum to the grid's natural height, capped — the flow box has fixed
    // columns, so measuring it before mapping is exact.
    int min_w = 0, nat_w = 0, min_h = 0, nat_h = 0, dummy = 0;
    gtk_widget_measure(s->wp_grid, GTK_ORIENTATION_HORIZONTAL, -1, &min_w, &nat_w, &dummy, &dummy);
    gtk_widget_measure(s->wp_grid, GTK_ORIENTATION_VERTICAL, nat_w, &min_h, &nat_h, &dummy, &dummy);
    const int height = s->wp_images.empty() ? 0 : std::min(nat_h, kWpGridMaxHeight);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(s->wp_grid_scroller), height);
    gtk_widget_set_visible(s->wp_grid_scroller, !s->wp_images.empty());
    wp_update_highlight(s);
}

// List the folder (local, small: synchronous like the shell's config read),
// sorted case-insensitively by name; hidden files skipped like Noctalia.
void wp_rescan(Settings* s) {
    s->wp_images.clear();
    if (!s->wp_scanned_dir.empty()) {
        GFile* dir = g_file_new_for_path(s->wp_scanned_dir.c_str());
        GFileEnumerator* enumerator = g_file_enumerate_children(
            dir, "standard::name,standard::type,standard::is-hidden", G_FILE_QUERY_INFO_NONE, nullptr,
            nullptr);
        if (enumerator != nullptr) {
            while (GFileInfo* info = g_file_enumerator_next_file(enumerator, nullptr, nullptr)) {
                const char* name = g_file_info_get_name(info);
                if (g_file_info_get_file_type(info) == G_FILE_TYPE_REGULAR &&
                    !g_file_info_get_is_hidden(info) && name != nullptr &&
                    hyprshell::is_wallpaper_image(name))
                    s->wp_images.push_back(s->wp_scanned_dir + "/" + name);
                g_object_unref(info);
            }
            g_object_unref(enumerator);
        }
        g_object_unref(dir);
        std::sort(s->wp_images.begin(), s->wp_images.end(),
                  [](const std::string& a, const std::string& b) {
                      gchar* ka = g_utf8_casefold(a.c_str() + a.rfind('/'), -1);
                      gchar* kb = g_utf8_casefold(b.c_str() + b.rfind('/'), -1);
                      const bool less = g_strcmp0(ka, kb) < 0;
                      g_free(ka);
                      g_free(kb);
                      return less;
                  });
    }
    wp_rebuild_grid(s);
}

void wp_schedule_rescan(Settings* s) {
    if (s->wp_rescan_source != 0)
        g_source_remove(s->wp_rescan_source);
    s->wp_rescan_source = g_timeout_add(
        400,
        [](gpointer data) -> gboolean {
            auto* s = static_cast<Settings*>(data);
            s->wp_rescan_source = 0;
            wp_rescan(s);
            return G_SOURCE_REMOVE;
        },
        s);
}

// point the grid at a folder: rescan now, follow changes on disk
void wp_set_directory(Settings* s, const std::string& directory) {
    const std::string expanded = wp_expand(directory);
    if (expanded == s->wp_scanned_dir && s->wp_dir_monitor != nullptr)
        return;
    s->wp_scanned_dir = g_file_test(expanded.c_str(), G_FILE_TEST_IS_DIR) ? expanded : "";
    if (s->wp_dir_monitor != nullptr) {
        g_file_monitor_cancel(s->wp_dir_monitor);
        g_clear_object(&s->wp_dir_monitor);
    }
    if (!s->wp_scanned_dir.empty()) {
        GFile* dir = g_file_new_for_path(s->wp_scanned_dir.c_str());
        s->wp_dir_monitor = g_file_monitor_directory(dir, G_FILE_MONITOR_NONE, nullptr, nullptr);
        g_object_unref(dir);
        if (s->wp_dir_monitor != nullptr)
            g_signal_connect(s->wp_dir_monitor, "changed",
                             G_CALLBACK(+[](GFileMonitor*, GFile*, GFile*, GFileMonitorEvent,
                                            gpointer data) { wp_schedule_rescan(static_cast<Settings*>(data)); }),
                             s);
    }
    wp_rescan(s);
}

// follow the shell's slideshow picks so the highlighted tile is what's on screen
void wp_watch_state(Settings* s) {
    const std::string state_path = std::string(g_get_user_cache_dir()) + "/hypr-shell/wallpaper.json";
    GFile* file = g_file_new_for_path(state_path.c_str());
    s->wp_state_monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, nullptr, nullptr);
    g_object_unref(file);
    if (s->wp_state_monitor != nullptr)
        g_signal_connect(s->wp_state_monitor, "changed",
                         G_CALLBACK(+[](GFileMonitor*, GFile*, GFile*, GFileMonitorEvent, gpointer data) {
                             auto* s = static_cast<Settings*>(data);
                             s->wp_current = wp_effective_current(s);
                             wp_update_highlight(s);
                         }),
                         s);
}

// -- On-screen display page: the top-level "osd" config object ---------------

json& osd_object(Settings* s) {
    if (!s->root.is_object())
        s->root = json::object();
    if (!s->root["osd"].is_object())
        s->root["osd"] = json::object();
    return s->root["osd"];
}

void on_osd_location_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto selected = adw_combo_row_get_selected(s->osd_location);
    osd_object(s)["location"] = kOsdLocationKeys[selected < kOsdLocationCount ? selected : 2];
    save(s);
}

void on_osd_orientation_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto selected = adw_combo_row_get_selected(s->osd_orientation);
    osd_object(s)["orientation"] = kOsdOrientationKeys[selected < 3 ? selected : 0];
    save(s);
}

void on_osd_toggled(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    osd_object(s)["enabled"] = adw_switch_row_get_active(s->osd_enabled) != FALSE;
    save(s);
}

// photo button on the background row: pick an image into the entry (its
// changed handler then writes the config)
void on_lock_browse_clicked(GtkButton* button, gpointer) {
    auto* entry = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(button), "target-entry"));
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select lock screen background");
    GtkFileFilter* images = gtk_file_filter_new();
    gtk_file_filter_set_name(images, "Images");
    gtk_file_filter_add_mime_type(images, "image/*");
    GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, images);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, images);
    g_object_unref(filters);
    g_object_unref(images);
    const char* current = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (current != nullptr && *current != '\0') {
        gchar* dir = g_path_get_dirname(current);
        GFile* folder = g_file_new_for_path(dir);
        gtk_file_dialog_set_initial_folder(dialog, folder);
        g_object_unref(folder);
        g_free(dir);
    }
    auto* root = gtk_widget_get_root(GTK_WIDGET(button));
    gtk_file_dialog_open(
        dialog, GTK_WINDOW(root), nullptr,
        [](GObject* source, GAsyncResult* result, gpointer entry_ptr) {
            GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, nullptr);
            if (file != nullptr) {
                gchar* path = g_file_get_path(file);
                if (path != nullptr)
                    gtk_editable_set_text(GTK_EDITABLE(entry_ptr), path);
                g_free(path);
                g_object_unref(file);
            }
        },
        entry);
    g_object_unref(dialog);
}

// -- Notifications page: the top-level "notifications" config object ---------

json& nd_object(Settings* s) {
    if (!s->root.is_object())
        s->root = json::object();
    if (!s->root["notifications"].is_object())
        s->root["notifications"] = json::object();
    return s->root["notifications"];
}

json& nd_sounds_object(Settings* s) {
    json& nd = nd_object(s);
    if (!nd["sounds"].is_object())
        nd["sounds"] = json::object();
    return nd["sounds"];
}

json& nd_history_object(Settings* s) {
    json& nd = nd_object(s);
    if (!nd["save_to_history"].is_object())
        nd["save_to_history"] = json::object();
    return nd["save_to_history"];
}

// switches with an "nd-key" writing straight into the notifications object
void on_nd_toggled(GObject* row, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    update_nd_rows(s);
    if (s->loading)
        return;
    const auto* key = static_cast<const char*>(g_object_get_data(row, "nd-key"));
    nd_object(s)[key] = adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE;
    save(s);
}

void on_nd_hist_toggled(GObject* row, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto* key = static_cast<const char*>(g_object_get_data(row, "nd-key"));
    nd_history_object(s)[key] = adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE;
    save(s);
}

void on_nd_sound_toggled(GObject* row, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    update_nd_rows(s);
    if (s->loading)
        return;
    const auto* key = static_cast<const char*>(g_object_get_data(row, "nd-key"));
    nd_sounds_object(s)[key] = adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE;
    save(s);
}

void on_nd_density_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto selected = adw_combo_row_get_selected(s->nd_density);
    nd_object(s)["density"] = kNdDensityKeys[selected < 2 ? selected : 0];
    save(s);
}

void on_nd_location_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto selected = adw_combo_row_get_selected(s->nd_location);
    nd_object(s)["location"] = kNdLocationKeys[selected < 6 ? selected : 2];
    save(s);
}

void on_nd_opacity_changed(GtkAdjustment* adjustment, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    nd_object(s)["background_opacity"] =
        std::round(gtk_adjustment_get_value(adjustment)) / 100.0;
    save(s);
}

void on_nd_duration_changed(GObject* row, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto* key = static_cast<const char*>(g_object_get_data(row, "nd-key"));
    nd_object(s)[key] = static_cast<int>(adw_spin_row_get_value(ADW_SPIN_ROW(row)));
    save(s);
}

void on_nd_sound_volume_changed(GtkAdjustment* adjustment, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    nd_sounds_object(s)["volume"] =
        std::round(gtk_adjustment_get_value(adjustment)) / 100.0;
    save(s);
}

void on_nd_sound_entry_changed(GtkEditable* row, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto* key =
        static_cast<const char*>(g_object_get_data(G_OBJECT(row), "nd-key"));
    nd_sounds_object(s)[key] = gtk_editable_get_text(row);
    save(s);
}

// folder button on a sound-file entry row: pick a file into the entry (the
// entry's changed handler then writes the config)
void on_nd_browse_clicked(GtkButton* button, gpointer) {
    auto* entry = static_cast<GtkWidget*>(
        g_object_get_data(G_OBJECT(button), "target-entry"));
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select sound file");
    auto* root = gtk_widget_get_root(GTK_WIDGET(button));
    gtk_file_dialog_open(
        dialog, GTK_WINDOW(root), nullptr,
        [](GObject* source, GAsyncResult* result, gpointer entry_ptr) {
            GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source),
                                                      result, nullptr);
            if (file != nullptr) {
                gchar* path = g_file_get_path(file);
                if (path != nullptr)
                    gtk_editable_set_text(GTK_EDITABLE(entry_ptr), path);
                g_free(path);
                g_object_unref(file);
            }
        },
        entry);
    g_object_unref(dialog);
}

// Noctalia's enabled-chains: everything follows "Enable notifications",
// sound rows follow "Enable notification sounds", and the unified sound file
// swaps for the per-urgency ones when "different sounds per priority" is on.
void update_nd_rows(Settings* s) {
    const bool enabled = adw_switch_row_get_active(s->nd_enabled) != FALSE;
    for (GtkWidget* group : s->nd_dependent_groups)
        gtk_widget_set_sensitive(group, enabled);
    gtk_widget_set_sensitive(GTK_WIDGET(s->nd_dnd), enabled);
    gtk_widget_set_sensitive(GTK_WIDGET(s->nd_density), enabled);
    gtk_widget_set_sensitive(GTK_WIDGET(s->nd_location), enabled);
    gtk_widget_set_sensitive(GTK_WIDGET(s->nd_overlay), enabled);

    const bool sounds = adw_switch_row_get_active(s->nd_snd_enabled) != FALSE;
    const bool separate = adw_switch_row_get_active(s->nd_snd_separate) != FALSE;
    gtk_widget_set_sensitive(s->nd_snd_volume_row, sounds);
    gtk_widget_set_sensitive(GTK_WIDGET(s->nd_snd_separate), sounds);
    gtk_widget_set_sensitive(GTK_WIDGET(s->nd_snd_excluded), sounds);
    for (AdwEntryRow* row : {s->nd_snd_unified, s->nd_snd_low, s->nd_snd_normal,
                             s->nd_snd_critical})
        gtk_widget_set_sensitive(GTK_WIDGET(row), sounds);
    gtk_widget_set_visible(GTK_WIDGET(s->nd_snd_unified), !separate);
    gtk_widget_set_visible(GTK_WIDGET(s->nd_snd_low), separate);
    gtk_widget_set_visible(GTK_WIDGET(s->nd_snd_normal), separate);
    gtk_widget_set_visible(GTK_WIDGET(s->nd_snd_critical), separate);
}

// -- rules: notifications.rules = [{pattern, action}] ------------------------

void save_rule(Settings* s, int index, const std::string& pattern,
               const std::string& action) {
    json& nd = nd_object(s);
    if (!nd["rules"].is_array())
        nd["rules"] = json::array();
    json rule = {{"pattern", pattern}, {"action", action}};
    if (index >= 0 && index < static_cast<int>(nd["rules"].size()))
        nd["rules"][index] = std::move(rule);
    else
        nd["rules"].push_back(std::move(rule));
    save(s);
    rebuild_rule_rows(s);
}

void on_rule_dialog_response(AdwAlertDialog* dialog, const char* response,
                             gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (g_strcmp0(response, "save") != 0)
        return;
    auto* entry = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(dialog), "entry"));
    auto* action_drop =
        static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(dialog), "action"));
    const int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dialog), "rule-index"));
    std::string pattern = gtk_editable_get_text(GTK_EDITABLE(entry));
    const auto from = pattern.find_first_not_of(" \t");
    if (from == std::string::npos)
        return; // empty pattern — ignore, like Noctalia
    pattern = pattern.substr(from, pattern.find_last_not_of(" \t") - from + 1);
    const auto selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(action_drop));
    save_rule(s, index, pattern, kRuleActionKeys[selected < 3 ? selected : 0]);
}

void open_rule_dialog(Settings* s, int index, const std::string& pattern,
                      const std::string& action) {
    AdwDialog* dialog =
        adw_alert_dialog_new(index < 0 ? "Add rule" : "Edit rule",
                             "Match app name or content. Rules are checked in "
                             "order, and the first match is applied.");
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "Cancel",
                                   "save", "Save", nullptr);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "save",
                                             ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "save");

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 9);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "firefox, discord, or /regex/");
    gtk_editable_set_text(GTK_EDITABLE(entry), pattern.c_str());
    gtk_box_append(GTK_BOX(box), entry);
    GtkWidget* action_drop = gtk_drop_down_new_from_strings(
        (const char*[]){kRuleActionLabels[0], kRuleActionLabels[1],
                        kRuleActionLabels[2], nullptr});
    guint selected = 0;
    for (guint i = 0; i < 3; ++i)
        if (action == kRuleActionKeys[i])
            selected = i;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(action_drop), selected);
    gtk_box_append(GTK_BOX(box), action_drop);
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), box);

    g_object_set_data(G_OBJECT(dialog), "entry", entry);
    g_object_set_data(G_OBJECT(dialog), "action", action_drop);
    g_object_set_data(G_OBJECT(dialog), "rule-index", GINT_TO_POINTER(index));
    g_signal_connect(dialog, "response", G_CALLBACK(on_rule_dialog_response), s);
    adw_dialog_present(dialog, s->window);
}

void on_rule_edit_clicked(GtkButton* button, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    const int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "rule-index"));
    try {
        const json rules = nd_object(s).value("rules", json::array());
        if (index >= 0 && index < static_cast<int>(rules.size()))
            open_rule_dialog(s, index, rules[index].value("pattern", ""),
                             rules[index].value("action", "block"));
    } catch (const json::exception&) {
    }
}

void on_rule_delete_clicked(GtkButton* button, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    const int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "rule-index"));
    json& nd = nd_object(s);
    if (nd["rules"].is_array() && index >= 0 &&
        index < static_cast<int>(nd["rules"].size())) {
        nd["rules"].erase(index);
        save(s);
        rebuild_rule_rows(s);
    }
}

void on_rule_add_clicked(GtkButton*, gpointer data) {
    open_rule_dialog(static_cast<Settings*>(data), -1, "", "block");
}

void rebuild_rule_rows(Settings* s) {
    for (auto* row : s->rule_rows)
        adw_preferences_group_remove(ADW_PREFERENCES_GROUP(s->rules_group), row);
    s->rule_rows.clear();

    json rules = json::array();
    try {
        rules = nd_object(s).value("rules", json::array());
    } catch (const json::exception&) {
    }
    int index = 0;
    for (const auto& rule : rules) {
        if (!rule.is_object())
            continue;
        const std::string pattern = rule.value("pattern", "");
        const std::string action = rule.value("action", "block");
        GtkWidget* row = adw_action_row_new();
        adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), pattern.c_str());
        adw_action_row_set_subtitle(
            ADW_ACTION_ROW(row), action == "hide"   ? "Hide — history only"
                                 : action == "mute" ? "Mute — no sound"
                                                    : "Block — skips completely");
        GtkWidget* edit = gtk_button_new_from_icon_name("document-edit-symbolic");
        GtkWidget* remove = gtk_button_new_from_icon_name("user-trash-symbolic");
        for (GtkWidget* button : {edit, remove}) {
            gtk_widget_add_css_class(button, "flat");
            gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
            g_object_set_data(G_OBJECT(button), "rule-index", GINT_TO_POINTER(index));
            adw_action_row_add_suffix(ADW_ACTION_ROW(row), button);
        }
        g_signal_connect(edit, "clicked", G_CALLBACK(on_rule_edit_clicked), s);
        g_signal_connect(remove, "clicked", G_CALLBACK(on_rule_delete_clicked), s);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(s->rules_group), row);
        s->rule_rows.push_back(row);
        ++index;
    }
}

json& active_window_object(Settings* s) {
    json& bar = bar_object(s);
    if (!bar["active_window"].is_object())
        bar["active_window"] = json::object();
    return bar["active_window"];
}

// rows 3 and 4 only make sense while the title is shown, like Noctalia
void update_aw_row_visibility(Settings* s) {
    const bool show = adw_switch_row_get_active(s->aw_show_title) != FALSE;
    gtk_widget_set_visible(GTK_WIDGET(s->aw_text), show);
    gtk_widget_set_visible(GTK_WIDGET(s->aw_empty), show);
}

void on_aw_hide_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto selected = adw_combo_row_get_selected(s->aw_hide);
    active_window_object(s)["hide_mode"] = kAwHideKeys[selected < 3 ? selected : 1];
    save(s);
}

void on_aw_show_title_toggled(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    update_aw_row_visibility(s);
    if (s->loading)
        return;
    active_window_object(s)["show_title"] =
        adw_switch_row_get_active(s->aw_show_title) != FALSE;
    save(s);
}

void on_aw_text_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto selected = adw_combo_row_get_selected(s->aw_text);
    active_window_object(s)["title_mode"] = kAwTextKeys[selected < 2 ? selected : 0];
    save(s);
}

void on_aw_empty_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto selected = adw_combo_row_get_selected(s->aw_empty);
    active_window_object(s)["no_window_text"] = kAwEmptyKeys[selected < 3 ? selected : 0];
    save(s);
}

void on_aw_icon_toggled(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    active_window_object(s)["show_icon"] = adw_switch_row_get_active(s->aw_icon) != FALSE;
    save(s);
}

json& clock_object(Settings* s) {
    json& bar = bar_object(s);
    if (!bar["clock"].is_object())
        bar["clock"] = json::object();
    return bar["clock"];
}

void on_clock_format_changed(GtkEditable* row, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto* key =
        static_cast<const char*>(g_object_get_data(G_OBJECT(row), "format-key"));
    clock_object(s)[key] = gtk_editable_get_text(row);
    save(s);
}

void on_clock_fdow_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    clock_object(s)["first_day_of_week"] =
        adw_combo_row_get_selected(s->clock_fdow) == 1 ? 1 : 0;
    save(s);
}

// -- bar.layout: section + order per module ---------------------------------
// Mirrors the shell's resolution: unknown names dropped, duplicates keep the
// first placement, unmentioned modules land at the end of their default section.
void resolve_layout(Settings* s) {
    for (auto& section : s->layout)
        section.clear();
    try {
        const json bar = s->root.value("bar", json::object());
        const json layout = bar.value("layout", json::object());
        for (int i = 0; i < 3; ++i) {
            const auto it = layout.find(kSectionKeys[i]);
            if (it == layout.end() || !it->is_array())
                continue;
            for (const auto& entry : *it) {
                if (!entry.is_string())
                    continue;
                const auto name = entry.get<std::string>();
                bool placed = false;
                for (const auto& section : s->layout)
                    placed = placed ||
                             std::find(section.begin(), section.end(), name) != section.end();
                if (module_info(name) && !placed)
                    s->layout[i].push_back(name);
            }
        }
    } catch (const json::exception& e) {
        g_warning("%s: %s — layout falls back to defaults", s->path.c_str(), e.what());
    }
    for (const auto& m : kModules) {
        bool placed = false;
        for (const auto& section : s->layout)
            placed = placed || std::find(section.begin(), section.end(), m.key) != section.end();
        if (!placed)
            s->layout[m.section].emplace_back(m.key);
    }
}

void write_layout(Settings* s) {
    json layout = json::object();
    for (int i = 0; i < 3; ++i)
        layout[kSectionKeys[i]] = s->layout[i];
    bar_object(s)["layout"] = std::move(layout);
    save(s);
}

void rebuild_layout_rows(Settings* s);

void schedule_layout_rebuild(Settings* s) {
    // Rebuilding destroys the widget whose signal is mid-flight — defer it.
    g_idle_add_once(
        [](gpointer data) { rebuild_layout_rows(static_cast<Settings*>(data)); }, s);
}

void on_layout_move(GtkButton* button, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    const auto* key =
        static_cast<const char*>(g_object_get_data(G_OBJECT(button), "module-key"));
    const int dir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "move-dir"));
    for (auto& section : s->layout) {
        const auto it = std::find(section.begin(), section.end(), key);
        if (it == section.end())
            continue;
        const auto idx = static_cast<int>(it - section.begin());
        const int target = idx + dir;
        if (target >= 0 && target < static_cast<int>(section.size())) {
            std::swap(section[idx], section[target]);
            write_layout(s);
            schedule_layout_rebuild(s);
        }
        return;
    }
}

void on_layout_section_changed(GObject* dropdown, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto* key =
        static_cast<const char*>(g_object_get_data(dropdown, "module-key"));
    const auto target = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    if (target > 2)
        return;
    for (auto& section : s->layout) {
        const auto it = std::find(section.begin(), section.end(), key);
        if (it == section.end())
            continue;
        if (&section == &s->layout[target])
            return; // already there
        section.erase(it);
        s->layout[target].emplace_back(key);
        write_layout(s);
        schedule_layout_rebuild(s);
        return;
    }
}

void rebuild_layout_rows(Settings* s) {
    s->loading = true;
    for (int i = 0; i < 3; ++i) {
        for (auto* row : s->layout_rows[i])
            adw_preferences_group_remove(ADW_PREFERENCES_GROUP(s->layout_groups[i]), row);
        s->layout_rows[i].clear();

        for (gsize pos = 0; pos < s->layout[i].size(); ++pos) {
            const auto* info = module_info(s->layout[i][pos]);
            GtkWidget* row = adw_action_row_new();
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), info->title);

            GtkWidget* up = gtk_button_new_from_icon_name("go-up-symbolic");
            GtkWidget* down = gtk_button_new_from_icon_name("go-down-symbolic");
            GtkWidget* section = gtk_drop_down_new_from_strings(
                (const char*[]){"Left", "Center", "Right", nullptr});
            for (GtkWidget* w : {up, down, section}) {
                gtk_widget_set_valign(w, GTK_ALIGN_CENTER);
                g_object_set_data(G_OBJECT(w), "module-key",
                                  const_cast<char*>(info->key));
            }
            gtk_widget_add_css_class(up, "flat");
            gtk_widget_add_css_class(down, "flat");
            gtk_widget_set_sensitive(up, pos > 0);
            gtk_widget_set_sensitive(down, pos + 1 < s->layout[i].size());
            gtk_drop_down_set_selected(GTK_DROP_DOWN(section), i);

            g_object_set_data(G_OBJECT(up), "move-dir", GINT_TO_POINTER(-1));
            g_object_set_data(G_OBJECT(down), "move-dir", GINT_TO_POINTER(+1));
            g_signal_connect(up, "clicked", G_CALLBACK(on_layout_move), s);
            g_signal_connect(down, "clicked", G_CALLBACK(on_layout_move), s);
            g_signal_connect(section, "notify::selected",
                             G_CALLBACK(on_layout_section_changed), s);

            adw_action_row_add_suffix(ADW_ACTION_ROW(row), up);
            adw_action_row_add_suffix(ADW_ACTION_ROW(row), down);
            adw_action_row_add_suffix(ADW_ACTION_ROW(row), section);
            adw_preferences_group_add(ADW_PREFERENCES_GROUP(s->layout_groups[i]), row);
            s->layout_rows[i].push_back(row);
        }
    }
    s->loading = false;
}

// the two auto-hide toggles only make sense in auto-hide mode, like Noctalia
void update_bar_visibility_rows(Settings* s) {
    const bool auto_hide = adw_combo_row_get_selected(s->visibility) == 2;
    gtk_widget_set_visible(GTK_WIDGET(s->show_ws_switch), auto_hide);
    gtk_widget_set_visible(GTK_WIDGET(s->show_ws_empty), auto_hide);
}

void on_visibility_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    update_bar_visibility_rows(s);
    if (s->loading)
        return;
    const auto selected = adw_combo_row_get_selected(s->visibility);
    bar_object(s)["visibility"] = kVisibilityKeys[selected < 3 ? selected : 0];
    save(s);
}

void on_show_ws_switch_toggled(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    bar_object(s)["show_on_workspace_switch"] =
        adw_switch_row_get_active(s->show_ws_switch) != FALSE;
    save(s);
}

void on_show_ws_empty_toggled(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    bar_object(s)["show_when_workspace_empty"] =
        adw_switch_row_get_active(s->show_ws_empty) != FALSE;
    save(s);
}

void on_opacity_changed(GtkAdjustment* adjustment, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    // store as 0..1 like Noctalia's backgroundOpacity, in whole percents
    bar_object(s)["background_opacity"] =
        std::round(gtk_adjustment_get_value(adjustment)) / 100.0;
    save(s);
}

void on_position_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto selected = adw_combo_row_get_selected(s->position);
    bar_object(s)["position"] = kPositions[selected < 4 ? selected : 0];
    save(s);
}

void on_module_toggled(GObject* row, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    const auto* key = static_cast<const char*>(g_object_get_data(row, "module-key"));
    json& bar = bar_object(s);
    if (!bar["modules"].is_object())
        bar["modules"] = json::object();
    bar["modules"][key] = adw_switch_row_get_active(ADW_SWITCH_ROW(row)) != FALSE;
    save(s);
}

void on_activate(GtkApplication* app, gpointer) {
    auto* s = new Settings();
    s->path = std::string(g_get_user_config_dir()) + "/hypr-shell/config.json";
    load(s);

    // wallpaper grid tiles (Noctalia's panel look: rounded cover thumbnails,
    // accent border on the current one, others veiled until hovered)
    GtkCssProvider* css = gtk_css_provider_new();
    std::string settings_css =
        ".wp-tile { padding: 4px; border-radius: 20px; }"
        ".wp-frame { border-radius: 16px; border: 3px solid transparent; }"
        ".wp-tile.current .wp-frame { border-color: @accent_bg_color; }"
        ".wp-thumb { border-radius: 13px; background-color: @card_bg_color; opacity: 0.7;"
        "  transition: opacity 150ms ease; }"
        ".wp-tile:hover .wp-thumb, .wp-tile.current .wp-thumb { opacity: 1; }"
        ".wp-tile label { color: @dim_label_color; }"
        ".wp-tile:hover label, .wp-tile.current label { color: @window_fg_color; }"
        // search result target flash (removed again by the search module)
        "row.search-hit { background-color: alpha(@accent_bg_color, 0.28); }"
        // tabler glyph standing in for an icon (sidebar + search results)
        ".page-glyph { font-family: \"noctalia-tabler-icons\"; font-size: 15px; }"
        // User interface page: GNOME's Appearance look
        "button.style-tile { background: none; box-shadow: none; border: none; padding: 6px; }"
        ".style-preview { border-radius: 12px; }"
        // (libadwaita >= 1.6 exposes the accent as a CSS variable, not a named colour)
        ".style-preview.selected { outline: 2px solid var(--accent-bg-color);"
        "  outline-offset: 3px; }"
        // grouped GtkCheckButtons render a `radio` node (a lone one a `check` node)
        "checkbutton.accent-swatch { padding: 0; margin: 0 8px; background: none; }"
        "checkbutton.accent-swatch > radio, checkbutton.accent-swatch > check {"
        "  min-width: 24px; min-height: 24px; margin: 0; border-radius: 999px; border: none;"
        "  box-shadow: none; background-image: none; -gtk-icon-source: none;"
        "  -gtk-icon-size: 0; }"
        "checkbutton.accent-swatch > radio:checked, checkbutton.accent-swatch > check:checked {"
        "  outline-width: 2px; outline-style: solid; outline-offset: 3px; }";
    for (gsize i = 0; i < kAccentCount; ++i) {
        gchar* rule = g_strdup_printf(
            "checkbutton.accent-swatch-%zu > radio, checkbutton.accent-swatch-%zu > check"
            "  { background-color: %s; }"
            "checkbutton.accent-swatch-%zu > radio:checked,"
            "checkbutton.accent-swatch-%zu > check:checked { outline-color: %s; }",
            i, i, kAccents[i].hex, i, i, kAccents[i].hex);
        settings_css += rule;
        g_free(rule);
    }
    gtk_css_provider_load_from_string(css, settings_css.c_str());
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    GtkWidget* win = adw_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), "Settings");
    gtk_window_set_default_size(GTK_WINDOW(win), 920, 660);
    g_object_set_data_full(G_OBJECT(win), "settings-state", s,
                           [](gpointer p) { delete static_cast<Settings*>(p); });
    s->window = win;

    GtkWidget* page = adw_preferences_page_new();

    // -- Bar geometry ------------------------------------------------------
    GtkWidget* bar_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(bar_group), "Bar");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(bar_group),
        "Changes apply live to the running shell");

    GtkWidget* pos_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(pos_row), "Position");
    const char* positions[] = {"Top", "Bottom", "Left", "Right", nullptr};
    GtkStringList* pos_model = gtk_string_list_new(positions);
    adw_combo_row_set_model(ADW_COMBO_ROW(pos_row), G_LIST_MODEL(pos_model));
    g_object_unref(pos_model);
    s->position = ADW_COMBO_ROW(pos_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(bar_group), pos_row);

    GtkWidget* vis_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(vis_row), "Visibility");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(vis_row),
                                "Auto hide slides the bar away; hover the screen "
                                "edge to bring it back.");
    const char* vis_options[] = {"Always show", "Always hide", "Auto hide", nullptr};
    GtkStringList* vis_model = gtk_string_list_new(vis_options);
    adw_combo_row_set_model(ADW_COMBO_ROW(vis_row), G_LIST_MODEL(vis_model));
    g_object_unref(vis_model);
    s->visibility = ADW_COMBO_ROW(vis_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(bar_group), vis_row);

    GtkWidget* ws_switch_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ws_switch_row),
                                  "Show bar on workspace switch");
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(ws_switch_row),
        "Automatically show the bar briefly when the workspace changes.");
    s->show_ws_switch = ADW_SWITCH_ROW(ws_switch_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(bar_group), ws_switch_row);

    GtkWidget* ws_empty_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ws_empty_row),
                                  "Show when workspace is empty");
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(ws_empty_row),
        "Keep the bar visible while the active workspace has no windows.");
    s->show_ws_empty = ADW_SWITCH_ROW(ws_empty_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(bar_group), ws_empty_row);

    GtkWidget* opacity_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(opacity_row),
                                  "Background opacity");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(opacity_row),
                                "Transparency of the bar background.");
    s->opacity = gtk_adjustment_new(88, 0, 100, 1, 10, 0);
    GtkWidget* opacity_scale =
        gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, s->opacity);
    gtk_scale_set_draw_value(GTK_SCALE(opacity_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(opacity_scale), GTK_POS_RIGHT);
    gtk_scale_set_format_value_func(
        GTK_SCALE(opacity_scale),
        [](GtkScale*, double value, gpointer) {
            return g_strdup_printf("%d%%", (int)std::round(value));
        },
        nullptr, nullptr);
    gtk_widget_set_size_request(opacity_scale, 200, -1);
    gtk_widget_set_valign(opacity_scale, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(opacity_row), opacity_scale);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(bar_group), opacity_row);

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                             ADW_PREFERENCES_GROUP(bar_group));

    // -- Module toggles ----------------------------------------------------
    GtkWidget* mod_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(mod_group), "Modules");

    for (gsize i = 0; i < kModuleCount; ++i) {
        GtkWidget* row = adw_switch_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), kModules[i].title);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), kModules[i].subtitle);
        g_object_set_data(G_OBJECT(row), "module-key",
                          const_cast<char*>(kModules[i].key));
        s->modules[i] = ADW_SWITCH_ROW(row);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(mod_group), row);
        g_signal_connect(row, "notify::active", G_CALLBACK(on_module_toggled), s);
    }

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page),
                             ADW_PREFERENCES_GROUP(mod_group));

    // -- Layout: section + order ---------------------------------------------
    for (int i = 0; i < 3; ++i) {
        GtkWidget* group = adw_preferences_group_new();
        adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(group), kSectionTitles[i]);
        if (i == 0)
            adw_preferences_group_set_description(
                ADW_PREFERENCES_GROUP(group),
                "Where each module sits in the bar, and its order within a section");
        s->layout_groups[i] = group;
        adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(group));
    }
    resolve_layout(s);
    rebuild_layout_rows(s);

    // -- Workspaces subpage --------------------------------------------------
    GtkWidget* ws_page = adw_preferences_page_new();
    GtkWidget* ws_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(ws_group), "Workspaces");
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(ws_group),
                                          "Changes apply live to the running shell");

    GtkWidget* ws_mode_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ws_mode_row), "Shown workspaces");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(ws_mode_row),
                                "Dynamic shows only existing workspaces");
    const char* ws_modes[] = {"Dynamic", "Fixed number", nullptr};
    GtkStringList* ws_mode_model = gtk_string_list_new(ws_modes);
    adw_combo_row_set_model(ADW_COMBO_ROW(ws_mode_row), G_LIST_MODEL(ws_mode_model));
    g_object_unref(ws_mode_model);
    s->ws_mode = ADW_COMBO_ROW(ws_mode_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(ws_group), ws_mode_row);

    GtkWidget* ws_count_row = adw_spin_row_new_with_range(1, 50, 1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ws_count_row), "Number of workspaces");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(ws_count_row),
                                "Workspaces 1…N are always shown; clicking an empty "
                                "one creates it");
    s->ws_count = ADW_SPIN_ROW(ws_count_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(ws_group), ws_count_row);

    GtkWidget* ws_wrap_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ws_wrap_row), "Wrap around on scroll");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(ws_wrap_row),
                                "Scrolling past the last workspace returns to the first");
    s->ws_wrap = ADW_SWITCH_ROW(ws_wrap_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(ws_group), ws_wrap_row);

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(ws_page), ADW_PREFERENCES_GROUP(ws_group));

    // -- Clock subpage --------------------------------------------------------
    GtkWidget* clock_page = adw_preferences_page_new();
    GtkWidget* clock_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(clock_group), "Calendar");
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(clock_group),
                                          "The calendar opens when clicking the clock");

    GtkWidget* fdow_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(fdow_row), "First day of week");
    const char* fdow_options[] = {"Sunday", "Monday", nullptr};
    GtkStringList* fdow_model = gtk_string_list_new(fdow_options);
    adw_combo_row_set_model(ADW_COMBO_ROW(fdow_row), G_LIST_MODEL(fdow_model));
    g_object_unref(fdow_model);
    s->clock_fdow = ADW_COMBO_ROW(fdow_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(clock_group), fdow_row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(clock_page),
                             ADW_PREFERENCES_GROUP(clock_group));

    GtkWidget* fmt_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(fmt_group), "Time format");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(fmt_group),
        "strftime format, e.g. %H:%M %a, %b %d. On vertical bars each "
        "space-separated part becomes its own line");

    GtkWidget* fmt_h_row = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(fmt_h_row), "Horizontal bar");
    g_object_set_data(G_OBJECT(fmt_h_row), "format-key",
                      const_cast<char*>("format_horizontal"));
    s->clock_fmt_h = ADW_ENTRY_ROW(fmt_h_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(fmt_group), fmt_h_row);

    GtkWidget* fmt_v_row = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(fmt_v_row), "Vertical bar");
    g_object_set_data(G_OBJECT(fmt_v_row), "format-key",
                      const_cast<char*>("format_vertical"));
    s->clock_fmt_v = ADW_ENTRY_ROW(fmt_v_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(fmt_group), fmt_v_row);

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(clock_page),
                             ADW_PREFERENCES_GROUP(fmt_group));

    // -- Active window subpage ------------------------------------------------
    GtkWidget* aw_page = adw_preferences_page_new();
    GtkWidget* aw_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(aw_group), "Active window");
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(aw_group),
                                          "Changes apply live to the running shell");

    auto make_combo = [](const char* title, const char* subtitle,
                         const char* const* options) {
        GtkWidget* row = adw_combo_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
        GtkStringList* model = gtk_string_list_new(options);
        adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(model));
        g_object_unref(model);
        return row;
    };

    const char* hide_options[] = {"Always visible", "Hidden", "Transparent", nullptr};
    GtkWidget* aw_hide_row =
        make_combo("Hiding mode",
                   "Controls how the widget behaves when no window is active.",
                   hide_options);
    s->aw_hide = ADW_COMBO_ROW(aw_hide_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(aw_group), aw_hide_row);

    GtkWidget* aw_title_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(aw_title_row), "Show window title");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(aw_title_row), "Display the window title.");
    s->aw_show_title = ADW_SWITCH_ROW(aw_title_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(aw_group), aw_title_row);

    const char* text_options[] = {"Window title", "Application name", nullptr};
    GtkWidget* aw_text_row =
        make_combo("Text to display",
                   "Show the focused window's title or its application name.",
                   text_options);
    s->aw_text = ADW_COMBO_ROW(aw_text_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(aw_group), aw_text_row);

    const char* empty_options[] = {"“No active window”", "“Desktop”",
                                   "Nothing", nullptr};
    GtkWidget* aw_empty_row =
        make_combo("When no window is active",
                   "What the text should show when nothing is focused.", empty_options);
    s->aw_empty = ADW_COMBO_ROW(aw_empty_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(aw_group), aw_empty_row);

    GtkWidget* aw_icon_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(aw_icon_row), "Show app icon");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(aw_icon_row),
                                "Display the application icon next to the window title.");
    s->aw_icon = ADW_SWITCH_ROW(aw_icon_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(aw_group), aw_icon_row);

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(aw_page), ADW_PREFERENCES_GROUP(aw_group));

    // -- Bluetooth subpage ------------------------------------------------------
    GtkWidget* bt_page = adw_preferences_page_new();
    GtkWidget* bt_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(bt_group), "Bluetooth panel");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(bt_group),
        "The panel opens when clicking the bluetooth icon.");

    GtkWidget* bt_auto_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(bt_auto_row), "Auto-connect");
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(bt_auto_row),
        "Reconnect paired devices when Bluetooth turns on.");
    s->bt_auto = ADW_SWITCH_ROW(bt_auto_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(bt_group), bt_auto_row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(bt_page),
                             ADW_PREFERENCES_GROUP(bt_group));

    // -- Control center subpage: card toggles ----------------------------------
    GtkWidget* cc_page = adw_preferences_page_new();
    GtkWidget* cc_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(cc_group), "Control center cards");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(cc_group),
        "Choose which controls appear in the control center panel.");
    struct CcRow {
        const char* key;
        const char* title;
        const char* subtitle;
        AdwSwitchRow** target;
    };
    const CcRow cc_rows[] = {
        {"show_media", "Media player", "Now playing with cover art, progress and controls.", &s->cc_media},
        {"show_audio", "Audio sliders", "Output and input volume with mute buttons.", &s->cc_audio},
        {"show_brightness", "Brightness slider", "Screen brightness (laptop backlight).", &s->cc_brightness},
        {"show_sysmon", "System monitor", "CPU usage, CPU temperature, memory and disk gauges.", &s->cc_sysmon},
    };
    for (const auto& row : cc_rows) {
        GtkWidget* sw = adw_switch_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(sw), row.title);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(sw), row.subtitle);
        g_object_set_data(G_OBJECT(sw), "config-key", const_cast<char*>(row.key));
        *row.target = ADW_SWITCH_ROW(sw);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(cc_group), sw);
        g_signal_connect(sw, "notify::active", G_CALLBACK(on_cc_toggled), s);
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(cc_page), ADW_PREFERENCES_GROUP(cc_group));

    // -- Taskbar subpage (Noctalia's TaskbarSettings, the four options kept) --
    GtkWidget* tb_page = adw_preferences_page_new();
    GtkWidget* tb_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(tb_group), "Taskbar");
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(tb_group),
                                          "Changes apply live to the running shell");
    GtkWidget* tb_hide_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(tb_hide_row), "Hiding mode");
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(tb_hide_row),
        "Controls how the widget behaves when there are no matching windows.");
    const char* tb_hide_options[] = {"Always visible", "Hide when empty",
                                     "Transparent when empty", nullptr};
    GtkStringList* tb_hide_model = gtk_string_list_new(tb_hide_options);
    adw_combo_row_set_model(ADW_COMBO_ROW(tb_hide_row), G_LIST_MODEL(tb_hide_model));
    g_object_unref(tb_hide_model);
    s->tb_hide = ADW_COMBO_ROW(tb_hide_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(tb_group), tb_hide_row);
    struct TbRow {
        const char* key;
        const char* title;
        const char* subtitle;
        AdwSwitchRow** target;
    };
    const TbRow tb_rows[] = {
        {"only_same_monitor", "Only from same monitor",
         "Show only apps from the monitor where the bar is located.", &s->tb_same_monitor},
        {"only_active_workspaces", "Only from active workspaces",
         "Show only apps from active workspaces.", &s->tb_active_workspaces},
        {"show_pinned_apps", "Show pinned apps",
         "Show pinned apps from the dock in the taskbar.", &s->tb_pinned},
    };
    for (const auto& row : tb_rows) {
        GtkWidget* sw = adw_switch_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(sw), row.title);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(sw), row.subtitle);
        g_object_set_data(G_OBJECT(sw), "config-key", const_cast<char*>(row.key));
        *row.target = ADW_SWITCH_ROW(sw);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(tb_group), sw);
        g_signal_connect(sw, "notify::active", G_CALLBACK(on_tb_toggled), s);
    }
    g_signal_connect(tb_hide_row, "notify::selected", G_CALLBACK(on_tb_hide_changed), s);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(tb_page), ADW_PREFERENCES_GROUP(tb_group));

    // -- App menu subpage -------------------------------------------------------
    GtkWidget* am_page = adw_preferences_page_new();
    GtkWidget* am_button_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(am_button_group), "Bar button");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(am_button_group),
        "How the app menu shows in the bar. Changes apply live.");

    GtkWidget* am_display_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(am_display_row), "Display");
    const char* am_display_options[] = {"Icon", "Icon and text", "Text", nullptr};
    GtkStringList* am_display_model = gtk_string_list_new(am_display_options);
    adw_combo_row_set_model(ADW_COMBO_ROW(am_display_row), G_LIST_MODEL(am_display_model));
    g_object_unref(am_display_model);
    s->am_display = ADW_COMBO_ROW(am_display_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(am_button_group), am_display_row);

    GtkWidget* am_text_row = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(am_text_row), "Label");
    g_object_set_data(G_OBJECT(am_text_row), "am-key", const_cast<char*>("text"));
    s->am_text = ADW_ENTRY_ROW(am_text_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(am_button_group), am_text_row);

    GtkWidget* am_icon_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(am_icon_row), "Icon");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(am_icon_row),
                                "Distro logo uses the LOGO icon from /etc/os-release.");
    {
        std::vector<const char*> am_icon_options;
        for (const auto& preset : hyprshell::kAppMenuIconPresets)
            am_icon_options.push_back(preset.label);
        am_icon_options.push_back("Distro logo");
        am_icon_options.push_back("Custom");
        am_icon_options.push_back(nullptr);
        GtkStringList* am_icon_model = gtk_string_list_new(am_icon_options.data());
        adw_combo_row_set_model(ADW_COMBO_ROW(am_icon_row), G_LIST_MODEL(am_icon_model));
        g_object_unref(am_icon_model);
    }
    s->am_icon = ADW_COMBO_ROW(am_icon_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(am_button_group), am_icon_row);

    GtkWidget* am_custom_row = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(am_custom_row),
                                  "Custom icon (icon theme name or image path)");
    g_object_set_data(G_OBJECT(am_custom_row), "am-key", const_cast<char*>("custom_icon"));
    s->am_custom_icon = ADW_ENTRY_ROW(am_custom_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(am_button_group), am_custom_row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(am_page),
                             ADW_PREFERENCES_GROUP(am_button_group));

    GtkWidget* am_panel_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(am_panel_group), "Menu panel");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(am_panel_group),
        "A search box over a grid of applications. Opens from the bar button "
        "or a Hyprland keybind, e.g. the Super key alone:\n"
        "bindr = SUPER, SUPER_L, exec, hypr-shell --app-menu");

    GtkWidget* am_columns_row = adw_spin_row_new_with_range(3, 8, 1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(am_columns_row), "Grid columns");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(am_columns_row),
                                "Applications per row; fewer columns mean bigger icons.");
    s->am_columns = ADW_SPIN_ROW(am_columns_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(am_panel_group), am_columns_row);

    struct AmRow {
        const char* key;
        const char* title;
        const char* subtitle;
        AdwSwitchRow** row;
    } am_rows[] = {
        {"show_search", "Search bar", "Show the search box at the top of the panel.",
         &s->am_show_search},
        {"multiline_labels", "Two-line app names",
         "Wrap long application names onto a second line instead of cutting "
         "them short.",
         &s->am_multiline},
        {"show_settings_button", "Settings button",
         "Button next to the search box that opens these settings.", &s->am_settings_btn},
        {"show_session_button", "Session button",
         "Power button opening the session menu (see the Session menu page).",
         &s->am_session_btn},
    };
    for (const auto& info : am_rows) {
        GtkWidget* row = adw_switch_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), info.title);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), info.subtitle);
        g_object_set_data(G_OBJECT(row), "am-key", const_cast<char*>(info.key));
        *info.row = ADW_SWITCH_ROW(row);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(am_panel_group), row);
        g_signal_connect(row, "notify::active", G_CALLBACK(on_am_toggled), s);
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(am_page),
                             ADW_PREFERENCES_GROUP(am_panel_group));

    // -- Battery subpage -------------------------------------------------------
    GtkWidget* bat_page = adw_preferences_page_new();
    GtkWidget* bat_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(bat_group), "Battery panel");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(bat_group),
        "Cards shown when clicking the battery icon. A card also needs its "
        "backend (power-profiles-daemon, a backlight, multiple display modes).");

    struct BatRow {
        const char* key;
        const char* title;
        const char* subtitle;
        AdwSwitchRow** row;
    } bat_rows[] = {
        {"show_power_profiles", "Power profile",
         "Slider for power-saver, balanced and performance.", &s->bat_profiles},
        {"show_brightness", "Brightness", "Screen brightness slider.",
         &s->bat_brightness},
        {"show_refresh_rate", "Refresh rate",
         "Switch the display's refresh rate.", &s->bat_refresh},
    };
    for (const auto& info : bat_rows) {
        GtkWidget* row = adw_switch_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), info.title);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), info.subtitle);
        g_object_set_data(G_OBJECT(row), "battery-key", const_cast<char*>(info.key));
        *info.row = ADW_SWITCH_ROW(row);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(bat_group), row);
        g_signal_connect(row, "notify::active", G_CALLBACK(on_battery_toggled), s);
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(bat_page),
                             ADW_PREFERENCES_GROUP(bat_group));

    // -- Notifications subpage ---------------------------------------------------
    GtkWidget* notif_page = adw_preferences_page_new();
    GtkWidget* notif_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(notif_group), "Notifications");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(notif_group),
        "The notification bell opens the history panel. hypr-shell must be the "
        "notification daemon (disable mako/dunst or Noctalia's).");

    struct NotifRow {
        const char* key;
        const char* title;
        const char* subtitle;
        AdwSwitchRow** row;
    } notif_rows[] = {
        {"show_unread_badge", "Unread badge",
         "Show a dot on the bell while there are unseen notifications.",
         &s->notif_badge},
        {"hide_when_zero", "Hide when empty",
         "Hide the bell while the history is empty.", &s->notif_hide_zero},
        {"hide_when_zero_unread", "Hide when nothing is unread",
         "Hide the bell while there are no unseen notifications.",
         &s->notif_hide_zero_unread},
    };
    for (const auto& info : notif_rows) {
        GtkWidget* row = adw_switch_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), info.title);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), info.subtitle);
        g_object_set_data(G_OBJECT(row), "notif-key", const_cast<char*>(info.key));
        *info.row = ADW_SWITCH_ROW(row);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(notif_group), row);
        g_signal_connect(row, "notify::active", G_CALLBACK(on_notifications_toggled), s);
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(notif_page),
                             ADW_PREFERENCES_GROUP(notif_group));

    // -- Launcher sidebar page (top-level "launcher" object) ------------------
    GtkWidget* lp_page = adw_preferences_page_new();

    GtkWidget* lp_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(lp_group), "Launcher");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(lp_group),
        "Application search and the calculator are always available. Toggle it "
        "from the bar's search button or bind a key in hyprland.conf:\n"
        "bind = SUPER, SPACE, exec, hypr-shell --launcher");

    struct LauncherRow {
        const char* key;
        const char* title;
        const char* subtitle;
        AdwSwitchRow** row;
    } launcher_rows[] = {
        {"enable_settings_search", "Settings search",
         "Include hypr-shell settings entries in the results.",
         &s->lp_settings_search},
        {"enable_session_search", "Session search",
         "Include lock, suspend, reboot, logout and shutdown commands.",
         &s->lp_session_search},
        {"enable_web_search", "Web search",
         "Offer searching the web in your default browser.", &s->lp_web_search},
        {"show_result_count", "Show result count",
         "Show the number of results under the list.", &s->lp_result_count},
        {"show_all_apps", "Show all applications by default",
         "List every application while the search field is empty.",
         &s->lp_show_all},
    };
    for (const auto& info : launcher_rows) {
        GtkWidget* row = adw_switch_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), info.title);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), info.subtitle);
        g_object_set_data(G_OBJECT(row), "launcher-key", const_cast<char*>(info.key));
        *info.row = ADW_SWITCH_ROW(row);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(lp_group), row);
        g_signal_connect(row, "notify::active", G_CALLBACK(on_launcher_toggled), s);
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(lp_page),
                             ADW_PREFERENCES_GROUP(lp_group));

    // -- Clipboard sidebar page (top-level "clipboard" object) ----------------
    GtkWidget* cb_page = adw_preferences_page_new();
    GtkWidget* cb_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(cb_group), "Clipboard history");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(cb_group),
        "Everything you copy is recorded by cliphist and listed in a searchable window. "
        "Open it from the bar's clipboard button or bind a key in hyprland.conf:\n"
        "bind = SUPER, V, exec, hypr-shell --clipboard");
    {
        char* cliphist = g_find_program_in_path("cliphist");
        char* wl_paste = g_find_program_in_path("wl-paste");
        char* wtype = g_find_program_in_path("wtype");
        const bool have_cliphist = cliphist != nullptr && wl_paste != nullptr;
        const bool have_wtype = wtype != nullptr;
        g_free(cliphist);
        g_free(wl_paste);
        g_free(wtype);

        GtkWidget* enabled_row = adw_switch_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(enabled_row), "Enable clipboard history");
        adw_action_row_set_subtitle(
            ADW_ACTION_ROW(enabled_row),
            have_cliphist ? "Record copied text and images and show the clipboard button in the bar."
                          : "Install the cliphist and wl-clipboard packages to record clipboard history.");
        gtk_widget_set_sensitive(enabled_row, have_cliphist);
        g_object_set_data(G_OBJECT(enabled_row), "clipboard-key", const_cast<char*>("enabled"));
        s->cb_enabled = ADW_SWITCH_ROW(enabled_row);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(cb_group), enabled_row);
        g_signal_connect(enabled_row, "notify::active", G_CALLBACK(on_clipboard_toggled), s);

        GtkWidget* images_row = adw_switch_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(images_row), "Show images");
        adw_action_row_set_subtitle(ADW_ACTION_ROW(images_row),
                                    "List copied images with a thumbnail. Off hides them from the list.");
        g_object_set_data(G_OBJECT(images_row), "clipboard-key", const_cast<char*>("show_images"));
        s->cb_show_images = ADW_SWITCH_ROW(images_row);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(cb_group), images_row);
        g_signal_connect(images_row, "notify::active", G_CALLBACK(on_clipboard_toggled), s);

        GtkWidget* paste_row = adw_switch_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(paste_row), "Paste on click");
        adw_action_row_set_subtitle(
            ADW_ACTION_ROW(paste_row),
            have_wtype ? "Paste the chosen entry into the focused window right away instead of "
                         "only copying it."
                       : "Install the wtype package to paste entries automatically.");
        gtk_widget_set_sensitive(paste_row, have_wtype);
        g_object_set_data(G_OBJECT(paste_row), "clipboard-key", const_cast<char*>("paste_on_click"));
        s->cb_paste = ADW_SWITCH_ROW(paste_row);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(cb_group), paste_row);
        g_signal_connect(paste_row, "notify::active", G_CALLBACK(on_clipboard_toggled), s);

        GtkWidget* position_row = adw_combo_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(position_row), "Position");
        adw_action_row_set_subtitle(ADW_ACTION_ROW(position_row),
                                    "Where the clipboard window appears on the screen.");
        const char* position_options[] = {"Center",      "Top left",     "Top center",
                                          "Top right",   "Bottom left",  "Bottom center",
                                          "Bottom right", nullptr};
        GtkStringList* position_model = gtk_string_list_new(position_options);
        adw_combo_row_set_model(ADW_COMBO_ROW(position_row), G_LIST_MODEL(position_model));
        g_object_unref(position_model);
        s->cb_position = ADW_COMBO_ROW(position_row);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(cb_group), position_row);
        g_signal_connect(position_row, "notify::selected",
                         G_CALLBACK(on_clipboard_position_changed), s);
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(cb_page), ADW_PREFERENCES_GROUP(cb_group));

    // -- Session menu sidebar page (top-level "session" object) ---------------
    GtkWidget* sm_page = adw_preferences_page_new();

    GtkWidget* sm_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(sm_group), "Session menu");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(sm_group),
        "Opened by the bar's Session button and the app menu's power button, or "
        "from a Hyprland keybind:\n"
        "bind = SUPER SHIFT, E, exec, hypr-shell --session");

    GtkWidget* sm_mode_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(sm_mode_row), "Style");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(sm_mode_row),
                                "Dropdown lists the actions under the button; Fullscreen "
                                "covers the screen with large buttons.");
    const char* sm_mode_options[] = {"Dropdown", "Fullscreen", nullptr};
    GtkStringList* sm_mode_model = gtk_string_list_new(sm_mode_options);
    adw_combo_row_set_model(ADW_COMBO_ROW(sm_mode_row), G_LIST_MODEL(sm_mode_model));
    g_object_unref(sm_mode_model);
    s->sm_mode = ADW_COMBO_ROW(sm_mode_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(sm_group), sm_mode_row);

    GtkWidget* sm_layout_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(sm_layout_row), "Fullscreen layout");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(sm_layout_row),
                                "Large buttons in one row, or wrapped into a grid.");
    const char* sm_layout_options[] = {"Single row", "Grid", nullptr};
    GtkStringList* sm_layout_model = gtk_string_list_new(sm_layout_options);
    adw_combo_row_set_model(ADW_COMBO_ROW(sm_layout_row), G_LIST_MODEL(sm_layout_model));
    g_object_unref(sm_layout_model);
    s->sm_layout = ADW_COMBO_ROW(sm_layout_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(sm_group), sm_layout_row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(sm_page), ADW_PREFERENCES_GROUP(sm_group));

    GtkWidget* sm_items_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(sm_items_group), "Actions");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(sm_items_group),
        "Which entries the session menus (and the launcher's session search) show.");
    for (gsize i = 0; i < kSessionActionCount; ++i) {
        const auto& action = hyprshell::kSessionActions[i];
        GtkWidget* row = adw_switch_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), action.label);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), action.description);
        g_object_set_data(G_OBJECT(row), "sm-key", const_cast<char*>(action.key));
        s->sm_items[i] = ADW_SWITCH_ROW(row);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(sm_items_group), row);
        g_signal_connect(row, "notify::active", G_CALLBACK(on_sm_item_toggled), s);
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(sm_page),
                             ADW_PREFERENCES_GROUP(sm_items_group));
    g_signal_connect(sm_mode_row, "notify::selected", G_CALLBACK(on_sm_mode_changed), s);
    g_signal_connect(sm_layout_row, "notify::selected", G_CALLBACK(on_sm_layout_changed), s);

    // -- Idle sidebar page (top-level "idle" object) --------------------------
    GtkWidget* idle_page = adw_preferences_page_new();
    GtkWidget* idle_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(idle_group), "Idle");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(idle_group),
        "Timeouts in seconds; 0 disables a step. The screen fades to black for a "
        "few seconds before each action and any mouse or keyboard activity cancels "
        "it.");

    struct IdleRow {
        const char* key;
        const char* title;
        const char* subtitle;
        AdwSpinRow** row;
    } idle_rows[] = {
        {"screen_off_timeout", "Turn off screen",
         "Seconds of inactivity before monitors are turned off.", &s->idle_screen_off},
        {"lock_timeout", "Lock screen",
         "Seconds of inactivity before the lock screen activates.", &s->idle_lock},
        {"suspend_timeout", "Suspend", "Seconds of inactivity before the system suspends.",
         &s->idle_suspend},
    };
    for (const auto& info : idle_rows) {
        GtkWidget* row = adw_spin_row_new_with_range(0, 86400, 10);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), info.title);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), info.subtitle);
        g_object_set_data(G_OBJECT(row), "idle-key", const_cast<char*>(info.key));
        *info.row = ADW_SPIN_ROW(row);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(idle_group), row);
        g_signal_connect(row, "notify::value", G_CALLBACK(on_idle_timeout_changed), s);
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(idle_page),
                             ADW_PREFERENCES_GROUP(idle_group));

    // -- Lock screen sidebar page (top-level "lock_screen" object) -------------
    GtkWidget* lock_page = adw_preferences_page_new();
    GtkWidget* lock_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(lock_group), "Lock screen");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(lock_group),
        "Lock from the session menu, with `loginctl lock-session`, or with a Hyprland "
        "keybind:  bind = SUPER, L, exec, hypr-shell --lock  — the idle daemon locks "
        "after its timeout. Leave the background empty for a plain dark background.");

    GtkWidget* lock_bg_row = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(lock_bg_row), "Background image");
    GtkWidget* lock_browse = gtk_button_new_from_icon_name("image-x-generic-symbolic");
    gtk_widget_add_css_class(lock_browse, "flat");
    gtk_widget_set_valign(lock_browse, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(lock_browse, "Select an image");
    g_object_set_data(G_OBJECT(lock_browse), "target-entry", lock_bg_row);
    g_signal_connect(lock_browse, "clicked", G_CALLBACK(on_lock_browse_clicked), nullptr);
    adw_entry_row_add_suffix(ADW_ENTRY_ROW(lock_bg_row), lock_browse);
    s->lock_background = ADW_ENTRY_ROW(lock_bg_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(lock_group), lock_bg_row);
    g_signal_connect(lock_bg_row, "changed", G_CALLBACK(on_lock_background_changed), s);

    GtkWidget* lock_blur_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(lock_blur_row), "Blur strength");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(lock_blur_row),
                                "Applies a blur effect to the lock screen wallpaper.");
    s->lock_blur = gtk_adjustment_new(0, 0, 100, 1, 10, 0);
    GtkWidget* lock_blur_scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, s->lock_blur);
    gtk_scale_set_draw_value(GTK_SCALE(lock_blur_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(lock_blur_scale), GTK_POS_RIGHT);
    gtk_scale_set_format_value_func(
        GTK_SCALE(lock_blur_scale),
        [](GtkScale*, double value, gpointer) {
            return g_strdup_printf("%d%%", (int)std::round(value));
        },
        nullptr, nullptr);
    gtk_widget_set_size_request(lock_blur_scale, 200, -1);
    gtk_widget_set_valign(lock_blur_scale, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(lock_blur_row), lock_blur_scale);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(lock_group), lock_blur_row);
    g_signal_connect(s->lock_blur, "value-changed", G_CALLBACK(on_lock_blur_changed), s);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(lock_page),
                             ADW_PREFERENCES_GROUP(lock_group));

    // -- User interface sidebar page (top-level "ui" object) -------------------
    // GNOME Settings' Appearance panel: Style tiles, an accent swatch row, the font
    GtkWidget* ui_page = adw_preferences_page_new();
    GtkWidget* ui_style_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(ui_style_group), "Style");
    GtkWidget* ui_style_list = gtk_list_box_new();
    gtk_widget_add_css_class(ui_style_list, "boxed-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui_style_list), GTK_SELECTION_NONE);
    GtkWidget* ui_style_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_widget_set_halign(ui_style_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(ui_style_box, 18);
    gtk_widget_set_margin_bottom(ui_style_box, 18);
    gtk_box_append(GTK_BOX(ui_style_box), make_style_tile(s, "Default", false));
    gtk_box_append(GTK_BOX(ui_style_box), make_style_tile(s, "Dark", true));
    GtkWidget* ui_style_row = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(ui_style_row), FALSE);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(ui_style_row), ui_style_box);
    gtk_list_box_append(GTK_LIST_BOX(ui_style_list), ui_style_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(ui_style_group), ui_style_list);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(ui_page), ADW_PREFERENCES_GROUP(ui_style_group));

    GtkWidget* ui_accent_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(ui_accent_group), "Accent Color");
    GtkWidget* ui_accent_list = gtk_list_box_new();
    gtk_widget_add_css_class(ui_accent_list, "boxed-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui_accent_list), GTK_SELECTION_NONE);
    GtkWidget* ui_accent_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(ui_accent_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(ui_accent_box, 14);
    gtk_widget_set_margin_bottom(ui_accent_box, 14);
    GtkCheckButton* swatch_group = nullptr;
    for (gsize i = 0; i < kAccentCount; ++i) {
        GtkWidget* swatch = gtk_check_button_new();
        gtk_widget_add_css_class(swatch, "accent-swatch");
        gchar* swatch_class = g_strdup_printf("accent-swatch-%zu", i);
        gtk_widget_add_css_class(swatch, swatch_class);
        g_free(swatch_class);
        gtk_widget_set_tooltip_text(swatch, kAccents[i].name);
        g_object_set_data(G_OBJECT(swatch), "hex", const_cast<char*>(kAccents[i].hex));
        if (swatch_group != nullptr)
            gtk_check_button_set_group(GTK_CHECK_BUTTON(swatch), swatch_group);
        else
            swatch_group = GTK_CHECK_BUTTON(swatch);
        g_signal_connect(swatch, "toggled", G_CALLBACK(on_ui_swatch_toggled), s);
        gtk_box_append(GTK_BOX(ui_accent_box), swatch);
        s->ui_swatches[i] = swatch;
    }
    GtkWidget* ui_accent_row = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(ui_accent_row), FALSE);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(ui_accent_row), ui_accent_box);
    gtk_list_box_append(GTK_LIST_BOX(ui_accent_list), ui_accent_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(ui_accent_group), ui_accent_list);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(ui_page),
                             ADW_PREFERENCES_GROUP(ui_accent_group));

    GtkWidget* ui_font_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(ui_font_group), "Font");
    GtkWidget* ui_font_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ui_font_row), "Font");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(ui_font_row),
                                "Text font of the bar, panels and lock screen");
    GtkFontDialog* font_dialog = gtk_font_dialog_new();
    gtk_font_dialog_set_title(font_dialog, "Shell font");
    s->ui_font_button = gtk_font_dialog_button_new(font_dialog);
    gtk_font_dialog_button_set_level(GTK_FONT_DIALOG_BUTTON(s->ui_font_button),
                                     GTK_FONT_LEVEL_FAMILY);
    gtk_font_dialog_button_set_use_font(GTK_FONT_DIALOG_BUTTON(s->ui_font_button), TRUE);
    gtk_widget_set_valign(s->ui_font_button, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(ui_font_row), s->ui_font_button);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(ui_font_group), ui_font_row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(ui_page), ADW_PREFERENCES_GROUP(ui_font_group));

    // -- Night light sidebar page (top-level "night_light" object) -------------
    GtkWidget* nl_page = adw_preferences_page_new();
    GtkWidget* nl_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(nl_group), "Night light");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(nl_group),
        "Reduce blue light emission to help you sleep better and reduce eye strain.");
    gchar* hyprsunset_path = g_find_program_in_path("hyprsunset");
    s->nl_available = hyprsunset_path != nullptr;
    g_free(hyprsunset_path);
    GtkWidget* nl_enabled_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(nl_enabled_row), "Enable Night Light");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(nl_enabled_row),
                                s->nl_available
                                    ? "Apply a warm color filter to reduce blue light emission."
                                    : "hyprsunset is not installed — Night Light is unavailable "
                                      "(pacman -S hyprsunset).");
    gtk_widget_set_sensitive(nl_enabled_row, s->nl_available);
    s->nl_enabled = ADW_SWITCH_ROW(nl_enabled_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(nl_group), nl_enabled_row);
    g_signal_connect(nl_enabled_row, "notify::active", G_CALLBACK(on_nl_enabled_toggled), s);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(nl_page), ADW_PREFERENCES_GROUP(nl_group));

    GtkWidget* nl_temp_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(nl_temp_group), "Color temperature");
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(nl_temp_group),
                                          "Set the color warmth for nighttime.");
    s->nl_temp_group = nl_temp_group;
    GtkWidget* nl_temp_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(nl_temp_row), "Night");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(nl_temp_row), "Controls the temperature during nighttime.");
    s->nl_temp = gtk_adjustment_new(4000, kNlTempMin, kNlTempMax, 1, 100, 0);
    GtkWidget* nl_temp_scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, s->nl_temp);
    gtk_scale_set_draw_value(GTK_SCALE(nl_temp_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(nl_temp_scale), GTK_POS_RIGHT);
    gtk_scale_set_format_value_func(
        GTK_SCALE(nl_temp_scale),
        [](GtkScale*, double value, gpointer) { return g_strdup_printf("%dK", (int)std::round(value)); },
        nullptr, nullptr);
    gtk_widget_set_size_request(nl_temp_scale, 260, -1);
    gtk_widget_set_valign(nl_temp_scale, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(nl_temp_row), nl_temp_scale);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(nl_temp_group), nl_temp_row);
    g_signal_connect(s->nl_temp, "value-changed", G_CALLBACK(on_nl_temp_changed), s);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(nl_page), ADW_PREFERENCES_GROUP(nl_temp_group));

    GtkWidget* nl_sched = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(nl_sched), "Schedule");
    s->nl_schedule_group = nl_sched;
    GtkWidget* nl_manual_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(nl_manual_row), "Scheduling");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(nl_manual_row),
                                "The filter is on from sunset to sunrise.");
    gtk_widget_set_sensitive(nl_manual_row, FALSE); // a heading row, like Noctalia's NLabel
    s->nl_manual_label = nl_manual_row;
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(nl_sched), nl_manual_row);

    GtkStringList* nl_times = gtk_string_list_new(nullptr);
    for (guint i = 0; i < kNlTimeOptions; ++i)
        gtk_string_list_append(nl_times, nl_time_option(i).c_str());
    GtkWidget* nl_sunrise_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(nl_sunrise_row), "Sunrise time");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(nl_sunrise_row), "Night light turns off.");
    adw_combo_row_set_model(ADW_COMBO_ROW(nl_sunrise_row), G_LIST_MODEL(nl_times));
    adw_combo_row_set_selected(ADW_COMBO_ROW(nl_sunrise_row), 13); // 06:30
    s->nl_sunrise = ADW_COMBO_ROW(nl_sunrise_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(nl_sched), nl_sunrise_row);
    g_signal_connect(nl_sunrise_row, "notify::selected", G_CALLBACK(on_nl_time_changed), s);
    GtkWidget* nl_sunset_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(nl_sunset_row), "Sunset time");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(nl_sunset_row), "Night light turns on.");
    adw_combo_row_set_model(ADW_COMBO_ROW(nl_sunset_row), G_LIST_MODEL(nl_times));
    adw_combo_row_set_selected(ADW_COMBO_ROW(nl_sunset_row), 37); // 18:30
    s->nl_sunset = ADW_COMBO_ROW(nl_sunset_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(nl_sched), nl_sunset_row);
    g_signal_connect(nl_sunset_row, "notify::selected", G_CALLBACK(on_nl_time_changed), s);

    GtkWidget* nl_forced_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(nl_forced_row), "Force activation");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(nl_forced_row),
                                "Ignores the schedule and applies the night filter immediately.");
    s->nl_forced = ADW_SWITCH_ROW(nl_forced_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(nl_sched), nl_forced_row);
    g_signal_connect(nl_forced_row, "notify::active", G_CALLBACK(on_nl_forced_toggled), s);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(nl_page), ADW_PREFERENCES_GROUP(nl_sched));

    // -- Wallpaper sidebar page (top-level "wallpaper" object) -----------------
    GtkWidget* wp_page = adw_preferences_page_new();
    GtkWidget* wp_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(wp_group), "Wallpaper");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(wp_group),
        "The shell draws the wallpaper itself on every monitor. Pick the folder holding "
        "your images; the grid at the bottom lists them.");
    GtkWidget* wp_dir_row = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(wp_dir_row), "Wallpaper folder");
    GtkWidget* wp_browse = gtk_button_new_from_icon_name("folder-open-symbolic");
    gtk_widget_add_css_class(wp_browse, "flat");
    gtk_widget_set_valign(wp_browse, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(wp_browse, "Select a folder");
    g_object_set_data(G_OBJECT(wp_browse), "target-entry", wp_dir_row);
    g_signal_connect(wp_browse, "clicked", G_CALLBACK(on_wp_browse_clicked), nullptr);
    adw_entry_row_add_suffix(ADW_ENTRY_ROW(wp_dir_row), wp_browse);
    s->wp_directory = ADW_ENTRY_ROW(wp_dir_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(wp_group), wp_dir_row);
    g_signal_connect(wp_dir_row, "changed", G_CALLBACK(on_wp_directory_changed), s);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(wp_page), ADW_PREFERENCES_GROUP(wp_group));

    GtkWidget* wp_look = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(wp_look), "Look");
    s->wp_look_group = wp_look;
    GtkWidget* wp_fill_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(wp_fill_row), "Fill mode");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(wp_fill_row),
                                "How the image scales to the monitor's resolution.");
    const char* wp_fill_labels[] = {"Center", "Crop (Fill)", "Fit (Contain)", "Stretch",
                                    "Repeat (Tile)", nullptr};
    adw_combo_row_set_model(ADW_COMBO_ROW(wp_fill_row), G_LIST_MODEL(gtk_string_list_new(wp_fill_labels)));
    s->wp_fill = ADW_COMBO_ROW(wp_fill_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(wp_look), wp_fill_row);
    g_signal_connect(wp_fill_row, "notify::selected", G_CALLBACK(on_wp_fill_changed), s);

    GtkWidget* wp_tr_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(wp_tr_row), "Transitions");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(wp_tr_row),
                                "Animate the change from one wallpaper to the next.");
    s->wp_transitions = ADW_SWITCH_ROW(wp_tr_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(wp_look), wp_tr_row);
    g_signal_connect(wp_tr_row, "notify::active", G_CALLBACK(on_wp_transitions_toggled), s);

    GtkWidget* wp_types_row = adw_expander_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(wp_types_row), "Transition type");
    adw_expander_row_set_subtitle(ADW_EXPANDER_ROW(wp_types_row),
                                  "One of the selected animations is picked at random for each change.");
    for (gsize i = 0; i < kWpTransitionCount; ++i) {
        GtkWidget* row = adw_switch_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), kWpTransitions[i].label);
        s->wp_transition[i] = ADW_SWITCH_ROW(row);
        adw_expander_row_add_row(ADW_EXPANDER_ROW(wp_types_row), row);
        g_signal_connect(row, "notify::active", G_CALLBACK(on_wp_transition_type_toggled), s);
    }
    s->wp_transition_types = wp_types_row;
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(wp_look), wp_types_row);

    GtkWidget* wp_dur_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(wp_dur_row), "Transition duration");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(wp_dur_row), "Length of the animation.");
    s->wp_duration = gtk_adjustment_new(1500, 500, 10000, 100, 500, 0);
    GtkWidget* wp_dur_scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, s->wp_duration);
    gtk_scale_set_draw_value(GTK_SCALE(wp_dur_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(wp_dur_scale), GTK_POS_RIGHT);
    gtk_scale_set_format_value_func(
        GTK_SCALE(wp_dur_scale),
        [](GtkScale*, double value, gpointer) { return g_strdup_printf("%.1fs", value / 1000.0); },
        nullptr, nullptr);
    gtk_widget_set_size_request(wp_dur_scale, 200, -1);
    gtk_widget_set_valign(wp_dur_scale, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(wp_dur_row), wp_dur_scale);
    s->wp_duration_row = wp_dur_row;
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(wp_look), wp_dur_row);
    g_signal_connect(s->wp_duration, "value-changed", G_CALLBACK(on_wp_duration_changed), s);

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(wp_page), ADW_PREFERENCES_GROUP(wp_look));

    GtkWidget* wp_auto = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(wp_auto), "Slideshow");
    s->wp_slideshow_group = wp_auto;
    GtkWidget* wp_ss_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(wp_ss_row), "Slideshow");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(wp_ss_row),
                                "Automatically change the wallpaper at regular intervals.");
    s->wp_slideshow = ADW_SWITCH_ROW(wp_ss_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(wp_auto), wp_ss_row);
    g_signal_connect(wp_ss_row, "notify::active", G_CALLBACK(on_wp_slideshow_toggled), s);

    GtkWidget* wp_order_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(wp_order_row), "Change order");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(wp_order_row),
                                "Random shows every image once before repeating.");
    const char* wp_order_labels[] = {"Random", "Alphabetical", nullptr};
    adw_combo_row_set_model(ADW_COMBO_ROW(wp_order_row),
                            G_LIST_MODEL(gtk_string_list_new(wp_order_labels)));
    s->wp_order = ADW_COMBO_ROW(wp_order_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(wp_auto), wp_order_row);
    g_signal_connect(wp_order_row, "notify::selected", G_CALLBACK(on_wp_order_changed), s);

    GtkWidget* wp_int_row = adw_spin_row_new_with_range(1, 1440, 1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(wp_int_row), "Time until next wallpaper");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(wp_int_row), "Minutes between changes.");
    s->wp_interval = ADW_SPIN_ROW(wp_int_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(wp_auto), wp_int_row);
    g_signal_connect(wp_int_row, "notify::value", G_CALLBACK(on_wp_interval_changed), s);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(wp_page), ADW_PREFERENCES_GROUP(wp_auto));

    // the grid: Noctalia's wallpaper panel as a preferences group
    GtkWidget* wp_grid_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(wp_grid_group), "Wallpapers");
    adw_preferences_group_set_description(ADW_PREFERENCES_GROUP(wp_grid_group),
                                          "Click an image to set it as the wallpaper.");
    s->wp_grid_group = wp_grid_group;
    GtkWidget* wp_grid_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    s->wp_grid_status = gtk_label_new("");
    gtk_widget_add_css_class(s->wp_grid_status, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(s->wp_grid_status), 0.0f);
    gtk_widget_set_margin_top(s->wp_grid_status, 6);
    gtk_box_append(GTK_BOX(wp_grid_box), s->wp_grid_status);
    s->wp_grid = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(s->wp_grid), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(s->wp_grid), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(s->wp_grid), 4); // Noctalia's 4 columns
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(s->wp_grid), 4);
    gtk_widget_set_halign(s->wp_grid, GTK_ALIGN_CENTER);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(s->wp_grid), 6);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(s->wp_grid), 6);
    gtk_widget_add_css_class(s->wp_grid, "wp-grid");
    gtk_widget_set_valign(s->wp_grid, GTK_ALIGN_START);
    // grows with its content up to a cap, then scrolls (user request)
    GtkWidget* wp_scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(wp_scroller), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(wp_scroller), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(wp_scroller), kWpGridMaxHeight);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(wp_scroller), s->wp_grid);
    s->wp_grid_scroller = wp_scroller;
    gtk_box_append(GTK_BOX(wp_grid_box), wp_scroller);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(wp_grid_group), wp_grid_box);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(wp_page), ADW_PREFERENCES_GROUP(wp_grid_group));

    wp_watch_state(s);

    // -- On-screen display sidebar page (top-level "osd" object) --------------
    GtkWidget* osd_page = adw_preferences_page_new();
    GtkWidget* osd_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(osd_group), "On-screen display");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(osd_group),
        "A small overlay shown for two seconds whenever the output volume, microphone "
        "volume, screen brightness or Caps/Num/Scroll Lock changes.");

    GtkWidget* osd_location_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(osd_location_row), "Position");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(osd_location_row),
                                "Where on-screen displays appear.");
    const char* osd_location_options[] = {"Top center",    "Top left",    "Top right",
                                          "Bottom center", "Bottom left", "Bottom right",
                                          "Center left",   "Center right", nullptr};
    GtkStringList* osd_location_model = gtk_string_list_new(osd_location_options);
    adw_combo_row_set_model(ADW_COMBO_ROW(osd_location_row),
                            G_LIST_MODEL(osd_location_model));
    g_object_unref(osd_location_model);
    s->osd_location = ADW_COMBO_ROW(osd_location_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(osd_group), osd_location_row);
    g_signal_connect(osd_location_row, "notify::selected",
                     G_CALLBACK(on_osd_location_changed), s);

    GtkWidget* osd_orientation_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(osd_orientation_row), "Orientation");
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(osd_orientation_row),
        "Landscape is a horizontal bar, portrait a vertical column. Automatic follows "
        "the position: portrait at the sides, landscape at the top or bottom.");
    const char* osd_orientation_options[] = {"Automatic", "Landscape", "Portrait", nullptr};
    GtkStringList* osd_orientation_model = gtk_string_list_new(osd_orientation_options);
    adw_combo_row_set_model(ADW_COMBO_ROW(osd_orientation_row),
                            G_LIST_MODEL(osd_orientation_model));
    g_object_unref(osd_orientation_model);
    s->osd_orientation = ADW_COMBO_ROW(osd_orientation_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(osd_group), osd_orientation_row);
    g_signal_connect(osd_orientation_row, "notify::selected",
                     G_CALLBACK(on_osd_orientation_changed), s);

    GtkWidget* osd_enabled_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(osd_enabled_row),
                                  "Enable on-screen display");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(osd_enabled_row),
                                "Show volume and brightness changes in real-time.");
    s->osd_enabled = ADW_SWITCH_ROW(osd_enabled_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(osd_group), osd_enabled_row);
    g_signal_connect(osd_enabled_row, "notify::active", G_CALLBACK(on_osd_toggled), s);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(osd_page),
                             ADW_PREFERENCES_GROUP(osd_group));

    // -- Notifications sidebar page (the daemon + popups, Noctalia's tab) -----
    GtkWidget* nd_page = adw_preferences_page_new();

    GtkWidget* nd_general = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(nd_general), "Notifications");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(nd_general),
        "Configure notifications appearance and behavior. hypr-shell must be "
        "the only notification daemon (disable mako/dunst or Noctalia's).");

    GtkWidget* nd_enabled_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(nd_enabled_row),
                                  "Enable notifications");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(nd_enabled_row),
                                "Enable or disable the notification daemon.");
    g_object_set_data(G_OBJECT(nd_enabled_row), "nd-key", const_cast<char*>("enabled"));
    s->nd_enabled = ADW_SWITCH_ROW(nd_enabled_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(nd_general), nd_enabled_row);
    g_signal_connect(nd_enabled_row, "notify::active", G_CALLBACK(on_nd_toggled), s);

    GtkWidget* nd_density_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(nd_density_row), "Density");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(nd_density_row),
                                "Choose the notification card density.");
    const char* density_options[] = {"Default", "Compact", nullptr};
    GtkStringList* density_model = gtk_string_list_new(density_options);
    adw_combo_row_set_model(ADW_COMBO_ROW(nd_density_row), G_LIST_MODEL(density_model));
    g_object_unref(density_model);
    s->nd_density = ADW_COMBO_ROW(nd_density_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(nd_general), nd_density_row);
    g_signal_connect(nd_density_row, "notify::selected",
                     G_CALLBACK(on_nd_density_changed), s);

    GtkWidget* nd_dnd_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(nd_dnd_row), "Do not disturb");
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(nd_dnd_row),
        "Disable all notification popups when enabled. Right-clicking the bell "
        "toggles this temporarily without changing the setting.");
    g_object_set_data(G_OBJECT(nd_dnd_row), "nd-key",
                      const_cast<char*>("do_not_disturb"));
    s->nd_dnd = ADW_SWITCH_ROW(nd_dnd_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(nd_general), nd_dnd_row);
    g_signal_connect(nd_dnd_row, "notify::active", G_CALLBACK(on_nd_toggled), s);

    GtkWidget* nd_location_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(nd_location_row), "Position");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(nd_location_row),
                                "Where notifications appear on screen.");
    const char* location_options[] = {"Top center",    "Top left",    "Top right",
                                      "Bottom center", "Bottom left", "Bottom right",
                                      nullptr};
    GtkStringList* location_model = gtk_string_list_new(location_options);
    adw_combo_row_set_model(ADW_COMBO_ROW(nd_location_row),
                            G_LIST_MODEL(location_model));
    g_object_unref(location_model);
    s->nd_location = ADW_COMBO_ROW(nd_location_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(nd_general), nd_location_row);
    g_signal_connect(nd_location_row, "notify::selected",
                     G_CALLBACK(on_nd_location_changed), s);

    GtkWidget* nd_overlay_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(nd_overlay_row), "Always on top");
    adw_action_row_set_subtitle(
        ADW_ACTION_ROW(nd_overlay_row),
        "Display notifications above fullscreen windows and other layers.");
    g_object_set_data(G_OBJECT(nd_overlay_row), "nd-key",
                      const_cast<char*>("overlay_layer"));
    s->nd_overlay = ADW_SWITCH_ROW(nd_overlay_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(nd_general), nd_overlay_row);
    g_signal_connect(nd_overlay_row, "notify::active", G_CALLBACK(on_nd_toggled), s);

    GtkWidget* nd_opacity_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(nd_opacity_row),
                                  "Background opacity");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(nd_opacity_row),
                                "Adjust the opacity of notification backgrounds.");
    s->nd_opacity = gtk_adjustment_new(100, 0, 100, 1, 10, 0);
    GtkWidget* nd_opacity_scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, s->nd_opacity);
    gtk_scale_set_draw_value(GTK_SCALE(nd_opacity_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(nd_opacity_scale), GTK_POS_RIGHT);
    gtk_scale_set_format_value_func(
        GTK_SCALE(nd_opacity_scale),
        [](GtkScale*, double value, gpointer) {
            return g_strdup_printf("%d%%", (int)std::round(value));
        },
        nullptr, nullptr);
    gtk_widget_set_size_request(nd_opacity_scale, 200, -1);
    gtk_widget_set_valign(nd_opacity_scale, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(nd_opacity_row), nd_opacity_scale);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(nd_general), nd_opacity_row);
    g_signal_connect(s->nd_opacity, "value-changed", G_CALLBACK(on_nd_opacity_changed),
                     s);

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(nd_page),
                             ADW_PREFERENCES_GROUP(nd_general));

    GtkWidget* nd_duration = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(nd_duration),
                                    "Notification duration");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(nd_duration),
        "Configure how long notifications stay visible based on their urgency level.");
    s->nd_dependent_groups.push_back(nd_duration);

    GtkWidget* nd_respect_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(nd_respect_row),
                                  "Respect expire timeout");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(nd_respect_row),
                                "Use the expire timeout set in the notification.");
    g_object_set_data(G_OBJECT(nd_respect_row), "nd-key",
                      const_cast<char*>("respect_expire_timeout"));
    s->nd_respect_expire = ADW_SWITCH_ROW(nd_respect_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(nd_duration), nd_respect_row);
    g_signal_connect(nd_respect_row, "notify::active", G_CALLBACK(on_nd_toggled), s);

    struct DurationRow {
        const char* key;
        const char* title;
        const char* subtitle;
        AdwSpinRow** row;
    } duration_rows[] = {
        {"low_urgency_duration", "Low urgency",
         "How long low priority notifications stay visible (seconds).", &s->nd_dur_low},
        {"normal_urgency_duration", "Normal urgency",
         "How long normal priority notifications stay visible (seconds).",
         &s->nd_dur_normal},
        {"critical_urgency_duration", "Critical urgency",
         "How long critical priority notifications stay visible (seconds).",
         &s->nd_dur_critical},
    };
    for (const auto& info : duration_rows) {
        GtkWidget* row = adw_spin_row_new_with_range(1, 30, 1);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), info.title);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), info.subtitle);
        g_object_set_data(G_OBJECT(row), "nd-key", const_cast<char*>(info.key));
        *info.row = ADW_SPIN_ROW(row);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(nd_duration), row);
        g_signal_connect(row, "notify::value", G_CALLBACK(on_nd_duration_changed), s);
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(nd_page),
                             ADW_PREFERENCES_GROUP(nd_duration));

    GtkWidget* nd_history = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(nd_history), "History");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(nd_history),
        "Control which notifications are saved to history based on their "
        "urgency level.");
    s->nd_dependent_groups.push_back(nd_history);

    struct HistRow {
        const char* key;
        const char* title;
        const char* subtitle;
        AdwSwitchRow** row;
        bool history_object;
    } hist_rows[] = {
        {"clear_dismissed", "Clear on dismissed",
         "Clear notification from history when dismissed.", &s->nd_clear_dismissed,
         false},
        {"low", "Save low urgency to history",
         "Save low priority notifications to history.", &s->nd_save_low, true},
        {"normal", "Save normal urgency to history",
         "Save normal priority notifications to history.", &s->nd_save_normal, true},
        {"critical", "Save critical urgency to history",
         "Save critical priority notifications to history.", &s->nd_save_critical,
         true},
    };
    for (const auto& info : hist_rows) {
        GtkWidget* row = adw_switch_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), info.title);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), info.subtitle);
        g_object_set_data(G_OBJECT(row), "nd-key", const_cast<char*>(info.key));
        *info.row = ADW_SWITCH_ROW(row);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(nd_history), row);
        g_signal_connect(row, "notify::active",
                         info.history_object ? G_CALLBACK(on_nd_hist_toggled)
                                             : G_CALLBACK(on_nd_toggled),
                         s);
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(nd_page),
                             ADW_PREFERENCES_GROUP(nd_history));

    GtkWidget* nd_sound = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(nd_sound), "Sound");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(nd_sound),
        "Configure notification sound effects and volume (played via paplay).");
    s->nd_dependent_groups.push_back(nd_sound);

    GtkWidget* snd_enabled_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(snd_enabled_row),
                                  "Enable notification sounds");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(snd_enabled_row),
                                "Enable sound effects for incoming notifications.");
    g_object_set_data(G_OBJECT(snd_enabled_row), "nd-key", const_cast<char*>("enabled"));
    s->nd_snd_enabled = ADW_SWITCH_ROW(snd_enabled_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(nd_sound), snd_enabled_row);
    g_signal_connect(snd_enabled_row, "notify::active", G_CALLBACK(on_nd_sound_toggled),
                     s);

    GtkWidget* snd_volume_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(snd_volume_row), "Sound volume");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(snd_volume_row),
                                "Adjust the volume level for notification sounds.");
    s->nd_snd_volume = gtk_adjustment_new(50, 0, 100, 1, 10, 0);
    GtkWidget* snd_volume_scale =
        gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, s->nd_snd_volume);
    gtk_scale_set_draw_value(GTK_SCALE(snd_volume_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(snd_volume_scale), GTK_POS_RIGHT);
    gtk_scale_set_format_value_func(
        GTK_SCALE(snd_volume_scale),
        [](GtkScale*, double value, gpointer) {
            return g_strdup_printf("%d%%", (int)std::round(value));
        },
        nullptr, nullptr);
    gtk_widget_set_size_request(snd_volume_scale, 200, -1);
    gtk_widget_set_valign(snd_volume_scale, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(snd_volume_row), snd_volume_scale);
    s->nd_snd_volume_row = snd_volume_row;
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(nd_sound), snd_volume_row);
    g_signal_connect(s->nd_snd_volume, "value-changed",
                     G_CALLBACK(on_nd_sound_volume_changed), s);

    GtkWidget* snd_separate_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(snd_separate_row),
                                  "Use different sounds per priority");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(snd_separate_row),
                                "Use different sound files for low, normal, and "
                                "critical priority notifications.");
    g_object_set_data(G_OBJECT(snd_separate_row), "nd-key",
                      const_cast<char*>("separate_sounds"));
    s->nd_snd_separate = ADW_SWITCH_ROW(snd_separate_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(nd_sound), snd_separate_row);
    g_signal_connect(snd_separate_row, "notify::active",
                     G_CALLBACK(on_nd_sound_toggled), s);

    auto make_sound_entry = [s, nd_sound](const char* title, const char* key,
                                          AdwEntryRow** target) {
        GtkWidget* row = adw_entry_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
        g_object_set_data(G_OBJECT(row), "nd-key", const_cast<char*>(key));
        GtkWidget* browse = gtk_button_new_from_icon_name("folder-open-symbolic");
        gtk_widget_add_css_class(browse, "flat");
        gtk_widget_set_valign(browse, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(browse, "Select sound file");
        g_object_set_data(G_OBJECT(browse), "target-entry", row);
        g_signal_connect(browse, "clicked", G_CALLBACK(on_nd_browse_clicked), nullptr);
        adw_entry_row_add_suffix(ADW_ENTRY_ROW(row), browse);
        *target = ADW_ENTRY_ROW(row);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(nd_sound), row);
        g_signal_connect(row, "changed", G_CALLBACK(on_nd_sound_entry_changed), s);
    };
    make_sound_entry("Notification sound (empty = default)", "normal_sound_file",
                     &s->nd_snd_unified);
    make_sound_entry("Low urgency sound", "low_sound_file", &s->nd_snd_low);
    make_sound_entry("Normal urgency sound", "normal_sound_file", &s->nd_snd_normal);
    make_sound_entry("Critical urgency sound", "critical_sound_file",
                     &s->nd_snd_critical);

    GtkWidget* snd_excluded_row = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(snd_excluded_row),
                                  "Excluded applications (comma separated)");
    g_object_set_data(G_OBJECT(snd_excluded_row), "nd-key",
                      const_cast<char*>("excluded_apps"));
    s->nd_snd_excluded = ADW_ENTRY_ROW(snd_excluded_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(nd_sound), snd_excluded_row);
    g_signal_connect(snd_excluded_row, "changed", G_CALLBACK(on_nd_sound_entry_changed),
                     s);

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(nd_page),
                             ADW_PREFERENCES_GROUP(nd_sound));

    GtkWidget* rules_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(rules_group), "Filter rules");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(rules_group),
        "Match app name or content — plain text, *globs* or /regex/. Rules are "
        "checked in order, and the first match is applied.");
    GtkWidget* add_rule = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_add_css_class(add_rule, "flat");
    gtk_widget_set_tooltip_text(add_rule, "Add rule");
    g_signal_connect(add_rule, "clicked", G_CALLBACK(on_rule_add_clicked), s);
    adw_preferences_group_set_header_suffix(ADW_PREFERENCES_GROUP(rules_group),
                                            add_rule);
    s->rules_group = rules_group;
    s->nd_dependent_groups.push_back(rules_group);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(nd_page),
                             ADW_PREFERENCES_GROUP(rules_group));

    populate(s);
    g_signal_connect(pos_row, "notify::selected", G_CALLBACK(on_position_changed), s);
    g_signal_connect(vis_row, "notify::selected", G_CALLBACK(on_visibility_changed), s);
    g_signal_connect(ws_switch_row, "notify::active",
                     G_CALLBACK(on_show_ws_switch_toggled), s);
    g_signal_connect(ws_empty_row, "notify::active",
                     G_CALLBACK(on_show_ws_empty_toggled), s);
    g_signal_connect(s->opacity, "value-changed", G_CALLBACK(on_opacity_changed), s);
    g_signal_connect(ws_mode_row, "notify::selected", G_CALLBACK(on_ws_mode_changed), s);
    g_signal_connect(ws_count_row, "notify::value", G_CALLBACK(on_ws_count_changed), s);
    g_signal_connect(ws_wrap_row, "notify::active", G_CALLBACK(on_ws_wrap_toggled), s);
    g_signal_connect(bt_auto_row, "notify::active", G_CALLBACK(on_bt_auto_toggled), s);
    g_signal_connect(fdow_row, "notify::selected", G_CALLBACK(on_clock_fdow_changed), s);
    g_signal_connect(fmt_h_row, "changed", G_CALLBACK(on_clock_format_changed), s);
    g_signal_connect(fmt_v_row, "changed", G_CALLBACK(on_clock_format_changed), s);
    g_signal_connect(aw_hide_row, "notify::selected", G_CALLBACK(on_aw_hide_changed), s);
    g_signal_connect(aw_title_row, "notify::active", G_CALLBACK(on_aw_show_title_toggled), s);
    g_signal_connect(aw_text_row, "notify::selected", G_CALLBACK(on_aw_text_changed), s);
    g_signal_connect(aw_empty_row, "notify::selected", G_CALLBACK(on_aw_empty_changed), s);
    g_signal_connect(aw_icon_row, "notify::active", G_CALLBACK(on_aw_icon_toggled), s);
    g_signal_connect(am_display_row, "notify::selected", G_CALLBACK(on_am_display_changed), s);
    g_signal_connect(am_text_row, "changed", G_CALLBACK(on_am_entry_changed), s);
    g_signal_connect(am_icon_row, "notify::selected", G_CALLBACK(on_am_icon_changed), s);
    g_signal_connect(am_custom_row, "changed", G_CALLBACK(on_am_entry_changed), s);
    g_signal_connect(am_columns_row, "notify::value", G_CALLBACK(on_am_columns_changed), s);

    // -- Navigation: main page + module subpages -----------------------------
    GtkWidget* nav = adw_navigation_view_new();

    GtkWidget* view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), page);
    adw_navigation_view_add(ADW_NAVIGATION_VIEW(nav),
                            adw_navigation_page_new(view, "Bar"));

    GtkWidget* ws_view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(ws_view), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(ws_view), ws_page);
    adw_navigation_view_add(ADW_NAVIGATION_VIEW(nav),
                            adw_navigation_page_new_with_tag(ws_view, "Workspaces",
                                                             "workspaces"));

    GtkWidget* clock_view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(clock_view), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(clock_view), clock_page);
    adw_navigation_view_add(ADW_NAVIGATION_VIEW(nav),
                            adw_navigation_page_new_with_tag(clock_view, "Clock", "clock"));

    GtkWidget* aw_view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(aw_view), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(aw_view), aw_page);
    adw_navigation_view_add(ADW_NAVIGATION_VIEW(nav),
                            adw_navigation_page_new_with_tag(aw_view, "Active window",
                                                             "active_window"));

    GtkWidget* bt_view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(bt_view), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(bt_view), bt_page);
    adw_navigation_view_add(ADW_NAVIGATION_VIEW(nav),
                            adw_navigation_page_new_with_tag(bt_view, "Bluetooth",
                                                             "bluetooth"));

    GtkWidget* cc_view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(cc_view), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(cc_view), cc_page);
    adw_navigation_view_add(ADW_NAVIGATION_VIEW(nav),
                            adw_navigation_page_new_with_tag(cc_view, "Control center", "control_center"));

    GtkWidget* tb_view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tb_view), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tb_view), tb_page);
    adw_navigation_view_add(ADW_NAVIGATION_VIEW(nav),
                            adw_navigation_page_new_with_tag(tb_view, "Taskbar", "taskbar"));

    GtkWidget* bat_view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(bat_view), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(bat_view), bat_page);
    adw_navigation_view_add(ADW_NAVIGATION_VIEW(nav),
                            adw_navigation_page_new_with_tag(bat_view, "Battery",
                                                             "battery"));

    GtkWidget* am_view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(am_view), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(am_view), am_page);
    adw_navigation_view_add(ADW_NAVIGATION_VIEW(nav),
                            adw_navigation_page_new_with_tag(am_view, "App menu",
                                                             "app_menu"));

    // cog on the App menu module row
    GtkWidget* am_cog = gtk_button_new_from_icon_name("emblem-system-symbolic");
    gtk_widget_add_css_class(am_cog, "flat");
    gtk_widget_set_valign(am_cog, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(am_cog, "App menu settings");
    g_signal_connect(am_cog, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer nav_ptr) {
                         adw_navigation_view_push_by_tag(ADW_NAVIGATION_VIEW(nav_ptr),
                                                         "app_menu");
                     }),
                     nav);
    adw_action_row_add_suffix(ADW_ACTION_ROW(s->modules[module_index("app_menu")]), am_cog);

    GtkWidget* notif_view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(notif_view), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(notif_view), notif_page);
    adw_navigation_view_add(ADW_NAVIGATION_VIEW(nav),
                            adw_navigation_page_new_with_tag(notif_view, "Notifications",
                                                             "notifications"));

    // cog on the Notifications module row
    GtkWidget* notif_cog = gtk_button_new_from_icon_name("emblem-system-symbolic");
    gtk_widget_add_css_class(notif_cog, "flat");
    gtk_widget_set_valign(notif_cog, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(notif_cog, "Notification settings");
    g_signal_connect(notif_cog, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer nav_ptr) {
                         adw_navigation_view_push_by_tag(ADW_NAVIGATION_VIEW(nav_ptr),
                                                         "notifications");
                     }),
                     nav);
    adw_action_row_add_suffix(ADW_ACTION_ROW(s->modules[module_index("notifications")]),
                              notif_cog);

    // cog on the Bluetooth module row
    GtkWidget* bt_cog = gtk_button_new_from_icon_name("emblem-system-symbolic");
    gtk_widget_add_css_class(bt_cog, "flat");
    gtk_widget_set_valign(bt_cog, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(bt_cog, "Bluetooth settings");
    g_signal_connect(bt_cog, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer nav_ptr) {
                         adw_navigation_view_push_by_tag(ADW_NAVIGATION_VIEW(nav_ptr),
                                                         "bluetooth");
                     }),
                     nav);
    adw_action_row_add_suffix(ADW_ACTION_ROW(s->modules[module_index("bluetooth")]),
                              bt_cog);

    // cog on the Control center module row
    GtkWidget* cc_cog = gtk_button_new_from_icon_name("emblem-system-symbolic");
    gtk_widget_add_css_class(cc_cog, "flat");
    gtk_widget_set_valign(cc_cog, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(cc_cog, "Control center settings");
    g_signal_connect(cc_cog, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer nav_ptr) {
                         adw_navigation_view_push_by_tag(ADW_NAVIGATION_VIEW(nav_ptr), "control_center");
                     }),
                     nav);
    adw_action_row_add_suffix(ADW_ACTION_ROW(s->modules[module_index("control_center")]), cc_cog);

    // cog on the Taskbar module row
    GtkWidget* tb_cog = gtk_button_new_from_icon_name("emblem-system-symbolic");
    gtk_widget_add_css_class(tb_cog, "flat");
    gtk_widget_set_valign(tb_cog, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(tb_cog, "Taskbar settings");
    g_signal_connect(tb_cog, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer nav_ptr) {
                         adw_navigation_view_push_by_tag(ADW_NAVIGATION_VIEW(nav_ptr), "taskbar");
                     }),
                     nav);
    adw_action_row_add_suffix(ADW_ACTION_ROW(s->modules[module_index("taskbar")]), tb_cog);

    // cog on the Battery module row
    GtkWidget* bat_cog = gtk_button_new_from_icon_name("emblem-system-symbolic");
    gtk_widget_add_css_class(bat_cog, "flat");
    gtk_widget_set_valign(bat_cog, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(bat_cog, "Battery settings");
    g_signal_connect(bat_cog, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer nav_ptr) {
                         adw_navigation_view_push_by_tag(ADW_NAVIGATION_VIEW(nav_ptr),
                                                         "battery");
                     }),
                     nav);
    adw_action_row_add_suffix(ADW_ACTION_ROW(s->modules[module_index("battery")]), bat_cog);

    // cog on the Active window module row
    GtkWidget* aw_cog = gtk_button_new_from_icon_name("emblem-system-symbolic");
    gtk_widget_add_css_class(aw_cog, "flat");
    gtk_widget_set_valign(aw_cog, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(aw_cog, "Active window settings");
    g_signal_connect(aw_cog, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer nav_ptr) {
                         adw_navigation_view_push_by_tag(ADW_NAVIGATION_VIEW(nav_ptr),
                                                         "active_window");
                     }),
                     nav);
    adw_action_row_add_suffix(ADW_ACTION_ROW(s->modules[module_index("active_window")]),
                              aw_cog);

    // cog on the Clock module row opens its subpage
    GtkWidget* clock_cog = gtk_button_new_from_icon_name("emblem-system-symbolic");
    gtk_widget_add_css_class(clock_cog, "flat");
    gtk_widget_set_valign(clock_cog, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(clock_cog, "Clock settings");
    g_signal_connect(clock_cog, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer nav_ptr) {
                         adw_navigation_view_push_by_tag(ADW_NAVIGATION_VIEW(nav_ptr),
                                                         "clock");
                     }),
                     nav);
    adw_action_row_add_suffix(ADW_ACTION_ROW(s->modules[module_index("clock")]), clock_cog);

    // cog on the Workspaces module row opens its subpage
    GtkWidget* ws_cog = gtk_button_new_from_icon_name("emblem-system-symbolic");
    gtk_widget_add_css_class(ws_cog, "flat");
    gtk_widget_set_valign(ws_cog, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(ws_cog, "Workspace settings");
    g_signal_connect(ws_cog, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer nav_ptr) {
                         adw_navigation_view_push_by_tag(ADW_NAVIGATION_VIEW(nav_ptr),
                                                         "workspaces");
                     }),
                     nav);
    adw_action_row_add_suffix(ADW_ACTION_ROW(s->modules[module_index("workspaces")]),
                              ws_cog);

    // -- GNOME-Settings-style sidebar: Bar, Launcher, Notifications ----------
    GtkWidget* wp_view = adw_toolbar_view_new();
    GtkWidget* wp_header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(wp_header),
                                    adw_window_title_new("Wallpaper", nullptr));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(wp_view), wp_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(wp_view), wp_page);
    GtkWidget* nl_view = adw_toolbar_view_new();
    GtkWidget* nl_header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(nl_header),
                                    adw_window_title_new("Night light", nullptr));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(nl_view), nl_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(nl_view), nl_page);
    GtkWidget* lp_view = adw_toolbar_view_new();
    GtkWidget* lp_header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(lp_header),
                                    adw_window_title_new("Launcher", nullptr));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(lp_view), lp_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(lp_view), lp_page);

    GtkWidget* cb_view = adw_toolbar_view_new();
    GtkWidget* cb_header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(cb_header),
                                    adw_window_title_new("Clipboard", nullptr));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(cb_view), cb_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(cb_view), cb_page);

    GtkWidget* sm_view = adw_toolbar_view_new();
    GtkWidget* sm_header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(sm_header),
                                    adw_window_title_new("Session menu", nullptr));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(sm_view), sm_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(sm_view), sm_page);

    GtkWidget* lock_view = adw_toolbar_view_new();
    GtkWidget* lock_header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(lock_header),
                                    adw_window_title_new("Lock screen", nullptr));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(lock_view), lock_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(lock_view), lock_page);

    GtkWidget* idle_view = adw_toolbar_view_new();
    GtkWidget* idle_header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(idle_header),
                                    adw_window_title_new("Idle", nullptr));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(idle_view), idle_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(idle_view), idle_page);

    GtkWidget* osd_view = adw_toolbar_view_new();
    GtkWidget* osd_header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(osd_header),
                                    adw_window_title_new("On-screen display", nullptr));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(osd_view), osd_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(osd_view), osd_page);

    GtkWidget* nd_view = adw_toolbar_view_new();
    GtkWidget* nd_header = adw_header_bar_new();
    // inside a plain GtkStack there is no per-page AdwNavigationPage title —
    // set this header's title directly
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(nd_header),
                                    adw_window_title_new("Notifications", nullptr));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(nd_view), nd_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(nd_view), nd_page);

    GtkWidget* stack = gtk_stack_new();
    gtk_stack_add_named(GTK_STACK(stack), nav, "bar");
    GtkWidget* ui_view = adw_toolbar_view_new();
    GtkWidget* ui_header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(ui_header),
                                    adw_window_title_new("User interface", nullptr));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(ui_view), ui_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(ui_view), ui_page);
    gtk_stack_add_named(GTK_STACK(stack), ui_view, "ui_page");
    g_signal_connect(s->ui_font_button, "notify::font-desc", G_CALLBACK(on_ui_font_changed), s);
    apply_settings_theme(s);
    gtk_stack_add_named(GTK_STACK(stack), wp_view, "wallpaper_page");
    gtk_stack_add_named(GTK_STACK(stack), nl_view, "night_light_page");
    gtk_stack_add_named(GTK_STACK(stack), lp_view, "launcher_page");
    gtk_stack_add_named(GTK_STACK(stack), cb_view, "clipboard_page");
    gtk_stack_add_named(GTK_STACK(stack), sm_view, "session_page");
    gtk_stack_add_named(GTK_STACK(stack), lock_view, "lock_page");
    gtk_stack_add_named(GTK_STACK(stack), idle_view, "idle_page");
    gtk_stack_add_named(GTK_STACK(stack), osd_view, "osd_page");
    gtk_stack_add_named(GTK_STACK(stack), nd_view, "notifications_page");

    // -- Hotspot: NetworkManager AP mode (state lives in NM, not config.json) --
    GtkWidget* hs_view = adw_toolbar_view_new();
    GtkWidget* hs_header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(hs_header),
                                    adw_window_title_new("Hotspot", nullptr));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(hs_view), hs_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(hs_view),
                                 hyprshell::settings::build_hotspot_page(GTK_WINDOW(win)));
    gtk_stack_add_named(GTK_STACK(stack), hs_view, "hotspot_page");

    // -- VPN: NetworkManager profiles (was the bar's vpn module + panel) ------
    GtkWidget* vpn_view = adw_toolbar_view_new();
    GtkWidget* vpn_header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(vpn_header),
                                    adw_window_title_new("VPN", nullptr));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(vpn_view), vpn_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(vpn_view),
                                 hyprshell::settings::build_vpn_page(GTK_WINDOW(win)));
    gtk_stack_add_named(GTK_STACK(stack), vpn_view, "vpn_page");

    // -- About: hardware + software facts (GNOME Settings' About panel) -----
    GtkWidget* about_view = adw_toolbar_view_new();
    GtkWidget* about_header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(about_header),
                                    adw_window_title_new("About", nullptr));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(about_view), about_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(about_view),
                                 hyprshell::settings::build_about_page());
    gtk_stack_add_named(GTK_STACK(stack), about_view, "about_page");

    GtkWidget* sidebar_list = gtk_list_box_new();
    gtk_widget_add_css_class(sidebar_list, "navigation-sidebar");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(sidebar_list), GTK_SELECTION_BROWSE);
    for (const auto& page : kSidebarPages) {
        // GNOME Settings row metrics: 12px icon/label gap, ~40px tall rows
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_set_margin_top(box, 6);
        gtk_widget_set_margin_bottom(box, 6);
        gtk_widget_set_margin_start(box, 4);
        gtk_widget_set_margin_end(box, 4);
        gtk_box_append(GTK_BOX(box), hyprshell::settings::make_page_icon(page));
        GtkWidget* label = gtk_label_new(page.title);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(box), label);
        GtkWidget* row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
        gtk_list_box_append(GTK_LIST_BOX(sidebar_list), row);
    }
    g_signal_connect(sidebar_list, "row-selected",
                     G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer stack_ptr) {
                         if (row == nullptr)
                             return;
                         const int index = gtk_list_box_row_get_index(row);
                         const char* page = (index >= 0 && index < kSidebarPageCount)
                                                ? kSidebarPages[index].name
                                                : "bar";
                         gtk_stack_set_visible_child_name(GTK_STACK(stack_ptr), page);
                     }),
                     stack);
    gtk_list_box_select_row(GTK_LIST_BOX(sidebar_list),
                            gtk_list_box_get_row_at_index(GTK_LIST_BOX(sidebar_list), 0));

    GtkWidget* sidebar_view = adw_toolbar_view_new();
    GtkWidget* sidebar_header = adw_header_bar_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(sidebar_view), sidebar_header);
    // the search module owns the sidebar content from here on: it wraps the
    // page list in a stack together with its results (settings/search.cpp)
    hyprshell::settings::install_search({
        .window = GTK_WINDOW(win),
        .sidebar_header = sidebar_header,
        .sidebar_view = sidebar_view,
        .sidebar_list = sidebar_list,
        .stack = stack,
        .nav = nav,
        .pages = kSidebarPages,
        .page_count = kSidebarPageCount,
    });

    GtkWidget* split = adw_navigation_split_view_new();
    adw_navigation_split_view_set_sidebar(
        ADW_NAVIGATION_SPLIT_VIEW(split),
        adw_navigation_page_new(sidebar_view, "Settings"));
    adw_navigation_split_view_set_content(
        ADW_NAVIGATION_SPLIT_VIEW(split), adw_navigation_page_new(stack, "Settings"));
    // GNOME Settings' sidebar proportions (icons + labels need the room)
    adw_navigation_split_view_set_min_sidebar_width(ADW_NAVIGATION_SPLIT_VIEW(split),
                                                    200);
    adw_navigation_split_view_set_max_sidebar_width(ADW_NAVIGATION_SPLIT_VIEW(split),
                                                    260);

    // dev hook (also the launcher's settings-search target):
    // HS_SETTINGS_PAGE=<tag> opens a Bar module subpage directly; a
    // kSidebarPages name (launcher_page, session_page, ...) opens that sidebar page
    if (const char* tag = g_getenv("HS_SETTINGS_PAGE")) {
        int sidebar_row = -1;
        for (int i = 1; i < kSidebarPageCount; ++i)
            if (g_strcmp0(tag, kSidebarPages[i].name) == 0)
                sidebar_row = i;
        if (sidebar_row >= 0)
            gtk_list_box_select_row(
                GTK_LIST_BOX(sidebar_list),
                gtk_list_box_get_row_at_index(GTK_LIST_BOX(sidebar_list), sidebar_row));
        else
            adw_navigation_view_push_by_tag(ADW_NAVIGATION_VIEW(nav), tag);
    }

    adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), split);

    gtk_window_present(GTK_WINDOW(win));
}

} // namespace

int main(int argc, char* argv[]) {
    AdwApplication* app =
        adw_application_new("dev.hyprshell.Settings", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
