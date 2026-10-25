#include "settings/system_icons.hpp"

#include <gio/gio.h>

#include <algorithm>
#include <set>

namespace hyprshell::settings {

namespace {

constexpr const char* kSchema = "org.gnome.desktop.interface";
constexpr const char* kKey = "icon-theme";

GSettings* interface_settings() {
    GSettingsSchemaSource* source = g_settings_schema_source_get_default();
    if (source == nullptr)
        return nullptr;
    GSettingsSchema* schema = g_settings_schema_source_lookup(source, kSchema, TRUE);
    if (schema == nullptr)
        return nullptr;
    const bool ok = g_settings_schema_has_key(schema, kKey);
    g_settings_schema_unref(schema);
    return ok ? g_settings_new(kSchema) : nullptr;
}

std::string ini_path(const char* dir) {
    gchar* path = g_build_filename(g_get_user_config_dir(), dir, "settings.ini", nullptr);
    std::string result = path;
    g_free(path);
    return result;
}

void write_ini(const char* dir, const std::string& id) {
    const std::string path = ini_path(dir);
    GKeyFile* key_file = g_key_file_new();
    g_key_file_load_from_file(key_file, path.c_str(), G_KEY_FILE_KEEP_COMMENTS, nullptr);
    g_key_file_set_string(key_file, "Settings", "gtk-icon-theme-name", id.c_str());
    gchar* parent = g_path_get_dirname(path.c_str());
    g_mkdir_with_parents(parent, 0755);
    g_free(parent);
    GError* error = nullptr;
    if (!g_key_file_save_to_file(key_file, path.c_str(), &error)) {
        g_warning("icon theme: cannot write %s: %s", path.c_str(), error->message);
        g_clear_error(&error);
    }
    g_key_file_free(key_file);
}

void scan_icon_dir(const std::string& base, std::set<std::string>& seen,
                   std::vector<IconTheme>& out) {
    GDir* dir = g_dir_open(base.c_str(), 0, nullptr);
    if (dir == nullptr)
        return;
    while (const char* entry = g_dir_read_name(dir)) {
        if (entry[0] == '.' || g_strcmp0(entry, "hicolor") == 0 ||
            g_strcmp0(entry, "default") == 0 || seen.count(entry) != 0)
            continue;
        gchar* index = g_build_filename(base.c_str(), entry, "index.theme", nullptr);
        GKeyFile* key_file = g_key_file_new();
        // Directories= marks an icon theme; cursor-only themes lack it
        if (g_key_file_load_from_file(key_file, index, G_KEY_FILE_NONE, nullptr) &&
            g_key_file_has_key(key_file, "Icon Theme", "Directories", nullptr)) {
            IconTheme theme{entry, entry};
            gchar* name = g_key_file_get_locale_string(key_file, "Icon Theme", "Name", nullptr,
                                                       nullptr);
            if (name != nullptr && *name != '\0')
                theme.name = name;
            g_free(name);
            seen.insert(entry);
            out.push_back(std::move(theme));
        }
        g_key_file_free(key_file);
        g_free(index);
    }
    g_dir_close(dir);
}

} // namespace

std::vector<IconTheme> icon_themes() {
    std::vector<IconTheme> themes;
    std::set<std::string> seen;
    gchar* home_icons = g_build_filename(g_get_home_dir(), ".icons", nullptr);
    scan_icon_dir(home_icons, seen, themes);
    g_free(home_icons);
    gchar* user_icons = g_build_filename(g_get_user_data_dir(), "icons", nullptr);
    scan_icon_dir(user_icons, seen, themes);
    g_free(user_icons);
    for (const gchar* const* d = g_get_system_data_dirs(); *d != nullptr; ++d) {
        gchar* icons = g_build_filename(*d, "icons", nullptr);
        scan_icon_dir(icons, seen, themes);
        g_free(icons);
    }
    std::sort(themes.begin(), themes.end(), [](const IconTheme& a, const IconTheme& b) {
        return g_utf8_collate(a.name.c_str(), b.name.c_str()) < 0;
    });
    return themes;
}

std::string system_icon_theme_read() {
    if (GSettings* settings = interface_settings()) {
        gchar* value = g_settings_get_string(settings, kKey);
        std::string id = (value != nullptr && *value != '\0') ? value : "Adwaita";
        g_free(value);
        g_object_unref(settings);
        return id;
    }
    for (const char* dir : {"gtk-4.0", "gtk-3.0"}) {
        GKeyFile* key_file = g_key_file_new();
        gchar* value = nullptr;
        if (g_key_file_load_from_file(key_file, ini_path(dir).c_str(), G_KEY_FILE_NONE, nullptr))
            value = g_key_file_get_string(key_file, "Settings", "gtk-icon-theme-name", nullptr);
        g_key_file_free(key_file);
        if (value != nullptr && *value != '\0') {
            std::string id = value;
            g_free(value);
            return id;
        }
        g_free(value);
    }
    return "Adwaita";
}

void system_icon_theme_write(const std::string& id) {
    if (GSettings* settings = interface_settings()) {
        g_settings_set_string(settings, kKey, id.c_str());
        g_settings_sync();
        g_object_unref(settings);
    }
    write_ini("gtk-3.0", id);
    write_ini("gtk-4.0", id);
}

} // namespace hyprshell::settings
