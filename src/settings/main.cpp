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
    {"volume",        "Volume",        "Output volume status icon",    2},
    {"battery",       "Battery",       "Battery status icon",          2},
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

struct Settings;
void update_aw_row_visibility(Settings* s);
void update_bar_visibility_rows(Settings* s);

struct Settings {
    std::string path;           // ~/.config/hypr-shell/config.json
    json root = json::object(); // the whole file, unknown keys included
    bool loading = false;       // widgets being populated — suppress writes

    AdwComboRow* position = nullptr;
    AdwComboRow* visibility = nullptr; // Always show / Always hide / Auto hide
    AdwSwitchRow* show_ws_switch = nullptr; // auto-hide: peek on workspace switch
    AdwSwitchRow* show_ws_empty = nullptr;  // auto-hide: stay while workspace empty
    AdwSwitchRow* modules[kModuleCount] = {};

    AdwComboRow* ws_mode = nullptr; // Dynamic / Fixed number
    AdwSpinRow* ws_count = nullptr;
    AdwSwitchRow* ws_wrap = nullptr;

    AdwComboRow* clock_fdow = nullptr; // Sunday / Monday
    AdwEntryRow* clock_fmt_h = nullptr;
    AdwEntryRow* clock_fmt_v = nullptr;

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
    bool enabled[kModuleCount];
    for (auto& e : enabled)
        e = true;

    try {
        const json bar = s->root.value("bar", json::object());
        position = bar.value("position", position);
        visibility = bar.value("visibility", visibility);
        ws_switch = bar.value("show_on_workspace_switch", ws_switch);
        ws_empty = bar.value("show_when_workspace_empty", ws_empty);
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
    update_bar_visibility_rows(s);
    for (gsize i = 0; i < kModuleCount; ++i)
        adw_switch_row_set_active(s->modules[i], enabled[i]);
    adw_combo_row_set_selected(s->ws_mode, ws_mode == "fixed" ? 1 : 0);
    adw_spin_row_set_value(s->ws_count, ws_count);
    gtk_widget_set_sensitive(GTK_WIDGET(s->ws_count), ws_mode == "fixed");
    adw_switch_row_set_active(s->ws_wrap, ws_wrap);
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
    gtk_window_set_default_size(GTK_WINDOW(win), 520, 620);
    g_object_set_data_full(G_OBJECT(win), "settings-state", s,
                           [](gpointer p) { delete static_cast<Settings*>(p); });

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

    populate(s);
    g_signal_connect(pos_row, "notify::selected", G_CALLBACK(on_position_changed), s);
    g_signal_connect(vis_row, "notify::selected", G_CALLBACK(on_visibility_changed), s);
    g_signal_connect(ws_switch_row, "notify::active",
                     G_CALLBACK(on_show_ws_switch_toggled), s);
    g_signal_connect(ws_empty_row, "notify::active",
                     G_CALLBACK(on_show_ws_empty_toggled), s);
    g_signal_connect(ws_mode_row, "notify::selected", G_CALLBACK(on_ws_mode_changed), s);
    g_signal_connect(ws_count_row, "notify::value", G_CALLBACK(on_ws_count_changed), s);
    g_signal_connect(ws_wrap_row, "notify::active", G_CALLBACK(on_ws_wrap_toggled), s);
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

    // cog on the Active window module row (kModules[1])
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
    adw_action_row_add_suffix(ADW_ACTION_ROW(s->modules[1]), aw_cog);

    // cog on the Clock module row opens its subpage (clock is the last module)
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
    adw_action_row_add_suffix(ADW_ACTION_ROW(s->modules[kModuleCount - 1]), clock_cog);

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
    adw_action_row_add_suffix(ADW_ACTION_ROW(s->modules[0]), ws_cog);

    // dev hook: HS_SETTINGS_PAGE=<tag> opens a subpage directly
    if (const char* tag = g_getenv("HS_SETTINGS_PAGE"))
        adw_navigation_view_push_by_tag(ADW_NAVIGATION_VIEW(nav), tag);

    adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), nav);

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
