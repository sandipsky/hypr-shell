// hypr-shell-settings — configures the shell by editing
// ~/.config/hypr-shell/config.json. The running shell hot-reloads that file,
// so every change here applies live; no IPC between the two binaries.
//
// libadwaita has no official C++ bindings — the C API is called directly
// (see CLAUDE.md). Keys the settings app doesn't manage are preserved on save.

#include <adwaita.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
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
    {"workspaces",    "Workspaces",    "Hyprland workspace switcher", 0},
    {"active_window", "Active window", "Focused window title",        1},
    {"network",       "Network",       "Wi-Fi / ethernet status icon", 2},
    {"bluetooth",     "Bluetooth",     "Bluetooth status icon",        2},
    {"volume",        "Volume",        "Output volume status icon",    2},
    {"battery",       "Battery",       "Battery status icon",          2},
    {"notifications", "Notifications", "Notification bell and history", 2},
    {"clock",         "Clock",         "Date and time",                2},
};

constexpr gsize kModuleCount = G_N_ELEMENTS(kModules);
constexpr const char* kSectionKeys[] = {"left", "center", "right"};
constexpr const char* kSectionTitles[] = {"Left section", "Center section", "Right section"};
constexpr const char* kPositions[] = {"top", "bottom", "left", "right"};
constexpr const char* kVisibilityKeys[] = {"visible", "hidden", "auto_hide"};
constexpr const char* kAwHideKeys[] = {"visible", "hidden", "transparent"};
constexpr const char* kAwTextKeys[] = {"title", "appname"};
constexpr const char* kAwEmptyKeys[] = {"default", "desktop", "none"};
constexpr const char* kNdDensityKeys[] = {"default", "compact"};
constexpr const char* kNdLocationKeys[] = {"top",    "top_left",    "top_right",
                                           "bottom", "bottom_left", "bottom_right"};
constexpr const char* kRuleActionKeys[] = {"block", "hide", "mute"};
constexpr const char* kRuleActionLabels[] = {
    "Block — skips completely",
    "Hide — no popup, no sound, adds to history",
    "Mute — no sound, still shows popup and in history",
};

struct Settings;
void update_aw_row_visibility(Settings* s);
void update_bar_visibility_rows(Settings* s);
void update_nd_rows(Settings* s);
void rebuild_rule_rows(Settings* s);

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

    AdwSwitchRow* bat_profiles = nullptr; // battery panel cards
    AdwSwitchRow* bat_brightness = nullptr;
    AdwSwitchRow* bat_refresh = nullptr;

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
        const json bt = s->root.value("bar", json::object()).value("bluetooth", json::object());
        bt_auto = bt.value("auto_connect", false);
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

    GtkWidget* win = adw_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), "hypr-shell Settings");
    gtk_window_set_default_size(GTK_WINDOW(win), 860, 640);
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

    // -- Navigation: main page + module subpages -----------------------------
    GtkWidget* nav = adw_navigation_view_new();

    GtkWidget* view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), page);
    adw_navigation_view_add(ADW_NAVIGATION_VIEW(nav),
                            adw_navigation_page_new(view, "hypr-shell Settings"));

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

    GtkWidget* bat_view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(bat_view), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(bat_view), bat_page);
    adw_navigation_view_add(ADW_NAVIGATION_VIEW(nav),
                            adw_navigation_page_new_with_tag(bat_view, "Battery",
                                                             "battery"));

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

    // -- GNOME-Settings-style sidebar: Bar, then Notifications ---------------
    GtkWidget* nd_view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(nd_view), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(nd_view), nd_page);

    GtkWidget* stack = gtk_stack_new();
    gtk_stack_add_named(GTK_STACK(stack), nav, "bar");
    gtk_stack_add_named(GTK_STACK(stack), nd_view, "notifications_page");

    GtkWidget* sidebar_list = gtk_list_box_new();
    gtk_widget_add_css_class(sidebar_list, "navigation-sidebar");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(sidebar_list), GTK_SELECTION_BROWSE);
    for (const char* title : {"Bar", "Notifications"}) {
        GtkWidget* label = gtk_label_new(title);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_widget_set_margin_top(label, 9);
        gtk_widget_set_margin_bottom(label, 9);
        gtk_widget_set_margin_start(label, 6);
        gtk_list_box_append(GTK_LIST_BOX(sidebar_list), label);
    }
    g_signal_connect(sidebar_list, "row-selected",
                     G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer stack_ptr) {
                         if (row == nullptr)
                             return;
                         gtk_stack_set_visible_child_name(
                             GTK_STACK(stack_ptr),
                             gtk_list_box_row_get_index(row) == 1 ? "notifications_page"
                                                                  : "bar");
                     }),
                     stack);
    gtk_list_box_select_row(GTK_LIST_BOX(sidebar_list),
                            gtk_list_box_get_row_at_index(GTK_LIST_BOX(sidebar_list), 0));

    GtkWidget* sidebar_view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(sidebar_view), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(sidebar_view), sidebar_list);

    GtkWidget* split = adw_navigation_split_view_new();
    adw_navigation_split_view_set_sidebar(
        ADW_NAVIGATION_SPLIT_VIEW(split),
        adw_navigation_page_new(sidebar_view, "hypr-shell"));
    adw_navigation_split_view_set_content(
        ADW_NAVIGATION_SPLIT_VIEW(split), adw_navigation_page_new(stack, "Settings"));
    adw_navigation_split_view_set_min_sidebar_width(ADW_NAVIGATION_SPLIT_VIEW(split),
                                                    160);
    adw_navigation_split_view_set_max_sidebar_width(ADW_NAVIGATION_SPLIT_VIEW(split),
                                                    200);

    // dev hook: HS_SETTINGS_PAGE=<tag> opens a Bar module subpage directly;
    // HS_SETTINGS_PAGE=notifications_page opens the Notifications sidebar page
    if (const char* tag = g_getenv("HS_SETTINGS_PAGE")) {
        if (g_strcmp0(tag, "notifications_page") == 0)
            gtk_list_box_select_row(
                GTK_LIST_BOX(sidebar_list),
                gtk_list_box_get_row_at_index(GTK_LIST_BOX(sidebar_list), 1));
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
