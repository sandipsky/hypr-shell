// hypr-shell-settings — configures the shell by editing
// ~/.config/hypr-shell/config.json. The running shell hot-reloads that file,
// so every change here applies live; no IPC between the two binaries.
//
// libadwaita has no official C++ bindings — the C API is called directly
// (see CLAUDE.md). Keys the settings app doesn't manage are preserved on save.

#include <adwaita.h>
#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

using json = nlohmann::json;

namespace {

struct ModuleInfo {
    const char* key; // key under bar.modules in config.json
    const char* title;
    const char* subtitle;
};

constexpr ModuleInfo kModules[] = {
    {"workspaces",    "Workspaces",    "Hyprland workspace switcher"},
    {"active_window", "Active window", "Focused window title"},
    {"network",       "Network",       "Wi-Fi / ethernet status icon"},
    {"volume",        "Volume",        "Output volume status icon"},
    {"battery",       "Battery",       "Battery status icon"},
    {"clock",         "Clock",         "Date and time"},
};

constexpr gsize kModuleCount = G_N_ELEMENTS(kModules);

struct Settings {
    std::string path;           // ~/.config/hypr-shell/config.json
    json root = json::object(); // the whole file, unknown keys included
    bool loading = false;       // widgets being populated — suppress writes

    AdwComboRow* position = nullptr;
    AdwSpinRow* height = nullptr;
    AdwSwitchRow* modules[kModuleCount] = {};
};

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
    int height = 0;
    bool enabled[kModuleCount];
    for (auto& e : enabled)
        e = true;

    try {
        const json bar = s->root.value("bar", json::object());
        position = bar.value("position", position);
        height = bar.value("height", 0);
        const json modules = bar.value("modules", json::object());
        for (gsize i = 0; i < kModuleCount; ++i)
            enabled[i] = modules.value(kModules[i].key, true);
    } catch (const json::exception& e) {
        g_warning("%s: %s — showing defaults", s->path.c_str(), e.what());
    }

    s->loading = true;
    adw_combo_row_set_selected(s->position, position == "bottom" ? 1 : 0);
    adw_spin_row_set_value(s->height, height);
    for (gsize i = 0; i < kModuleCount; ++i)
        adw_switch_row_set_active(s->modules[i], enabled[i]);
    s->loading = false;
}

void on_position_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    bar_object(s)["position"] =
        adw_combo_row_get_selected(s->position) == 1 ? "bottom" : "top";
    save(s);
}

void on_height_changed(GObject*, GParamSpec*, gpointer data) {
    auto* s = static_cast<Settings*>(data);
    if (s->loading)
        return;
    bar_object(s)["height"] = static_cast<int>(adw_spin_row_get_value(s->height));
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
    const char* positions[] = {"Top", "Bottom", nullptr};
    GtkStringList* pos_model = gtk_string_list_new(positions);
    adw_combo_row_set_model(ADW_COMBO_ROW(pos_row), G_LIST_MODEL(pos_model));
    g_object_unref(pos_model);
    s->position = ADW_COMBO_ROW(pos_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(bar_group), pos_row);

    GtkWidget* height_row = adw_spin_row_new_with_range(0, 128, 1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(height_row), "Height");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(height_row),
                                "Pixels — 0 uses the automatic CSS height");
    s->height = ADW_SPIN_ROW(height_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(bar_group), height_row);

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

    populate(s);
    g_signal_connect(pos_row, "notify::selected", G_CALLBACK(on_position_changed), s);
    g_signal_connect(height_row, "notify::value", G_CALLBACK(on_height_changed), s);

    GtkWidget* view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view), page);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), view);

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
