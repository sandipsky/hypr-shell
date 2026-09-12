#include "settings/system_cursor.hpp"

#include "settings/command.hpp"
#include "settings/login_page.hpp" // sddm_config_value

#include <gio/gio.h>
#include <glib/gstdio.h>

#include <algorithm>
#include <set>

namespace hyprshell::settings {

namespace {

constexpr const char* kSchema = "org.gnome.desktop.interface";
constexpr const char* kSddmDropIn = "/etc/sddm.conf.d/zz-hypr-shell-cursor.conf";

GSettings* interface_settings() {
    GSettingsSchemaSource* source = g_settings_schema_source_get_default();
    if (source == nullptr)
        return nullptr;
    GSettingsSchema* schema = g_settings_schema_source_lookup(source, kSchema, TRUE);
    if (schema == nullptr)
        return nullptr;
    const bool ok = g_settings_schema_has_key(schema, "cursor-theme") &&
                    g_settings_schema_has_key(schema, "cursor-size");
    g_settings_schema_unref(schema);
    return ok ? g_settings_new(kSchema) : nullptr;
}

std::string ini_path(const char* dir) {
    gchar* path = g_build_filename(g_get_user_config_dir(), dir, "settings.ini", nullptr);
    std::string result = path;
    g_free(path);
    return result;
}

void write_ini(const char* dir, const SystemCursor& cursor) {
    const std::string path = ini_path(dir);
    GKeyFile* key_file = g_key_file_new();
    g_key_file_load_from_file(key_file, path.c_str(), G_KEY_FILE_KEEP_COMMENTS, nullptr);
    g_key_file_set_string(key_file, "Settings", "gtk-cursor-theme-name", cursor.theme.c_str());
    g_key_file_set_integer(key_file, "Settings", "gtk-cursor-theme-size", cursor.size);
    gchar* parent = g_path_get_dirname(path.c_str());
    g_mkdir_with_parents(parent, 0755);
    g_free(parent);
    GError* error = nullptr;
    if (!g_key_file_save_to_file(key_file, path.c_str(), &error)) {
        g_warning("cursor: cannot write %s: %s", path.c_str(), error->message);
        g_clear_error(&error);
    }
    g_key_file_free(key_file);
}

// ~/.icons/default/index.theme: the Xcursor "default" theme inherits the
// chosen one, so XWayland clients and anything without XCURSOR_THEME follow.
void write_default_index(const SystemCursor& cursor) {
    gchar* dir = g_build_filename(g_get_home_dir(), ".icons", "default", nullptr);
    g_mkdir_with_parents(dir, 0755);
    gchar* path = g_build_filename(dir, "index.theme", nullptr);
    const std::string text = "[Icon Theme]\nName=Default\nComment=Default cursor theme "
                             "(written by hypr-shell-settings)\nInherits=" +
                             cursor.theme + "\n";
    GError* error = nullptr;
    if (!g_file_set_contents(path, text.c_str(), static_cast<gssize>(text.size()), &error)) {
        g_warning("cursor: cannot write %s: %s", path, error->message);
        g_clear_error(&error);
    }
    g_free(path);
    g_free(dir);
}

void scan_icon_dir(const std::string& base, std::set<std::string>& seen,
                   std::vector<CursorTheme>& out) {
    GDir* dir = g_dir_open(base.c_str(), 0, nullptr);
    if (dir == nullptr)
        return;
    while (const char* entry = g_dir_read_name(dir)) {
        if (entry[0] == '.' || g_strcmp0(entry, "default") == 0 || !seen.insert(entry).second)
            continue;
        gchar* x = g_build_filename(base.c_str(), entry, "cursors", nullptr);
        gchar* h = g_build_filename(base.c_str(), entry, "hyprcursors", nullptr);
        const bool has = g_file_test(x, G_FILE_TEST_IS_DIR) || g_file_test(h, G_FILE_TEST_IS_DIR);
        g_free(x);
        g_free(h);
        if (!has) {
            seen.erase(entry); // another data dir may hold the real theme
            continue;
        }
        CursorTheme theme{entry, entry};
        gchar* index = g_build_filename(base.c_str(), entry, "index.theme", nullptr);
        GKeyFile* key_file = g_key_file_new();
        if (g_key_file_load_from_file(key_file, index, G_KEY_FILE_NONE, nullptr)) {
            gchar* name = g_key_file_get_locale_string(key_file, "Icon Theme", "Name", nullptr,
                                                       nullptr);
            if (name != nullptr && *name != '\0')
                theme.name = name;
            g_free(name);
        }
        g_key_file_free(key_file);
        g_free(index);
        out.push_back(std::move(theme));
    }
    g_dir_close(dir);
}

std::vector<std::string> split_commas(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == ',') {
            parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    parts.push_back(current);
    return parts;
}

} // namespace

std::vector<CursorTheme> cursor_themes() {
    std::vector<CursorTheme> themes;
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
    std::sort(themes.begin(), themes.end(), [](const CursorTheme& a, const CursorTheme& b) {
        return g_utf8_collate(a.name.c_str(), b.name.c_str()) < 0;
    });
    return themes;
}

SystemCursor system_cursor_read() {
    SystemCursor cursor;
    if (GSettings* settings = interface_settings()) {
        gchar* theme = g_settings_get_string(settings, "cursor-theme");
        if (theme != nullptr && *theme != '\0')
            cursor.theme = theme;
        g_free(theme);
        const int size = g_settings_get_int(settings, "cursor-size");
        if (size > 0)
            cursor.size = size;
        g_object_unref(settings);
        return cursor;
    }
    for (const char* dir : {"gtk-4.0", "gtk-3.0"}) {
        GKeyFile* key_file = g_key_file_new();
        if (g_key_file_load_from_file(key_file, ini_path(dir).c_str(), G_KEY_FILE_NONE, nullptr)) {
            gchar* theme =
                g_key_file_get_string(key_file, "Settings", "gtk-cursor-theme-name", nullptr);
            if (theme != nullptr && *theme != '\0') {
                cursor.theme = theme;
                const int size =
                    g_key_file_get_integer(key_file, "Settings", "gtk-cursor-theme-size", nullptr);
                if (size > 0)
                    cursor.size = size;
                g_free(theme);
                g_key_file_free(key_file);
                return cursor;
            }
            g_free(theme);
        }
        g_key_file_free(key_file);
    }
    return cursor;
}

void system_cursor_write(const SystemCursor& cursor) {
    if (GSettings* settings = interface_settings()) {
        g_settings_set_string(settings, "cursor-theme", cursor.theme.c_str());
        g_settings_set_int(settings, "cursor-size", cursor.size);
        g_settings_sync();
        g_object_unref(settings);
    }
    write_ini("gtk-3.0", cursor);
    write_ini("gtk-4.0", cursor);
    write_default_index(cursor);
}

bool sddm_installed() {
    gchar* path = g_find_program_in_path("sddm");
    const bool found = path != nullptr;
    g_free(path);
    return found;
}

void sddm_cursor_apply(const SystemCursor& cursor,
                       std::function<void(bool ok, std::string message)> done) {
    // GreeterEnvironment (a [General] key) is applied after [Theme]'s cursor
    // keys, so its XCURSOR_* entries are replaced too; other entries survive.
    std::string env;
    for (const auto& entry : split_commas(sddm_config_value("General", "GreeterEnvironment"))) {
        const std::string e = trim(entry);
        if (e.empty() || e.rfind("XCURSOR_THEME=", 0) == 0 || e.rfind("XCURSOR_SIZE=", 0) == 0 ||
            e.rfind("HYPRCURSOR_THEME=", 0) == 0 || e.rfind("HYPRCURSOR_SIZE=", 0) == 0)
            continue;
        env += e + ",";
    }
    env += "XCURSOR_THEME=" + cursor.theme + ",XCURSOR_SIZE=" + std::to_string(cursor.size);
    const std::string text = "# Written by hypr-shell-settings (User interface > Cursor).\n"
                             "[General]\nGreeterEnvironment=" + env + "\n\n[Theme]\nCursorTheme=" +
                             cursor.theme + "\nCursorSize=" + std::to_string(cursor.size) + "\n";

    if (const char* root = g_getenv("HS_CURSOR_SDDM_ROOT")) { // dev hook: no pkexec
        const std::string path = std::string(root) + kSddmDropIn;
        gchar* parent = g_path_get_dirname(path.c_str());
        g_mkdir_with_parents(parent, 0755);
        g_free(parent);
        GError* error = nullptr;
        const bool ok =
            g_file_set_contents(path.c_str(), text.c_str(), static_cast<gssize>(text.size()), &error);
        std::string message = ok ? "Applied to " + path : error->message;
        g_clear_error(&error);
        done(ok, message);
        return;
    }

    gchar* dir = g_build_filename(g_get_user_cache_dir(), "hypr-shell", nullptr);
    g_mkdir_with_parents(dir, 0755);
    gchar* tmp = g_build_filename(dir, "sddm-cursor.conf", nullptr);
    g_free(dir);
    GError* error = nullptr;
    if (!g_file_set_contents(tmp, text.c_str(), static_cast<gssize>(text.size()), &error)) {
        std::string message = error->message;
        g_error_free(error);
        g_free(tmp);
        done(false, message);
        return;
    }
    const std::string tmp_path = tmp;
    g_free(tmp);
    run_command({"pkexec", "/bin/sh", "-c", "install -m 644 -o root -g root \"$1\" \"$2\"", "sh",
                 tmp_path, kSddmDropIn},
                [done, tmp_path](bool ok, int status, const std::string&, const std::string& err) {
                    g_unlink(tmp_path.c_str());
                    if (ok) {
                        done(true, "Applied — the login screen uses it from its next start.");
                        return;
                    }
                    // pkexec: 126 = dialog dismissed, 127 = not authorised
                    done(false, (status == 126 || status == 127) ? "Authorisation was not granted."
                                                                 : "Failed: " + first_line(err));
                });
}

} // namespace hyprshell::settings
