#include "settings/system_font.hpp"

#include <pango/pango.h>

#include <cmath>

namespace hyprshell::settings {

namespace {

constexpr const char* kSchema = "org.gnome.desktop.interface";
constexpr const char* kKey = "font-name";

std::string ini_path(const char* dir) {
    gchar* path = g_build_filename(g_get_user_config_dir(), dir, "settings.ini", nullptr);
    std::string result = path;
    g_free(path);
    return result;
}

std::string format_size(double size) {
    // Pango parses "12" and "10.5"; keep whole points plain.
    gchar buf[G_ASCII_DTOSTR_BUF_SIZE];
    if (std::fabs(size - std::round(size)) < 0.01)
        return std::to_string(static_cast<int>(std::lround(size)));
    return g_ascii_formatd(buf, sizeof buf, "%.1f", size);
}

SystemFont parse(const std::string& text) {
    SystemFont font;
    PangoFontDescription* desc = pango_font_description_from_string(text.c_str());
    if (desc == nullptr)
        return font;
    const char* family = pango_font_description_get_family(desc);
    if (family != nullptr && *family != '\0')
        font.family = family;
    if (pango_font_description_get_size(desc) > 0)
        font.size = pango_font_description_get_size(desc) / static_cast<double>(PANGO_SCALE);
    // The style is whatever sits between the family and the size in the
    // original string — Pango can't give it back verbatim, so cut it out.
    std::string style = text;
    if (style.rfind(font.family, 0) == 0)
        style.erase(0, font.family.size());
    const auto space = style.find_last_of(' ');
    if (space != std::string::npos && space + 1 < style.size() &&
        g_ascii_isdigit(static_cast<guchar>(style[space + 1])))
        style.erase(space);
    while (!style.empty() && style.front() == ' ')
        style.erase(0, 1);
    while (!style.empty() && style.back() == ' ')
        style.pop_back();
    if (style.find(',') == std::string::npos) // a family list would confuse the writer
        font.style = style;
    pango_font_description_free(desc);
    return font;
}

void write_ini(const char* dir, const std::string& value) {
    const std::string path = ini_path(dir);
    GKeyFile* key_file = g_key_file_new();
    g_key_file_load_from_file(key_file, path.c_str(), G_KEY_FILE_KEEP_COMMENTS, nullptr);
    g_key_file_set_string(key_file, "Settings", "gtk-font-name", value.c_str());
    gchar* parent = g_path_get_dirname(path.c_str());
    g_mkdir_with_parents(parent, 0755);
    g_free(parent);
    GError* error = nullptr;
    if (!g_key_file_save_to_file(key_file, path.c_str(), &error)) {
        g_warning("system font: cannot write %s: %s", path.c_str(), error->message);
        g_clear_error(&error);
    }
    g_key_file_free(key_file);
}

// ~/.config/fontconfig/conf.d/ is included by fontconfig's 50-user.conf before
// ~/.config/fontconfig/fonts.conf, so this alias wins over one written there.
// A <prefer> alias only reorders the fallback list — if the family is not
// installed, fontconfig moves on to the next sans (Noto, DejaVu, ...).
void write_fontconfig(const std::string& family) {
    const std::string path = system_font_fontconfig_path();
    gchar* escaped = g_markup_escape_text(family.c_str(), -1);
    const std::string xml = std::string(
        "<?xml version=\"1.0\"?>\n"
        "<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
        "<!-- Written by hypr-shell-settings (User interface > Font) — the system\n"
        "     font's family as the preferred generic sans-serif, so Qt apps, which\n"
        "     read neither GSettings nor settings.ini, follow it too. Edits here\n"
        "     are overwritten by the next font change in the settings app. -->\n"
        "<fontconfig>\n"
        "  <alias binding=\"strong\">\n"
        "    <family>sans-serif</family>\n"
        "    <prefer><family>") + escaped + std::string("</family></prefer>\n"
        "  </alias>\n"
        "</fontconfig>\n");
    g_free(escaped);
    gchar* parent = g_path_get_dirname(path.c_str());
    g_mkdir_with_parents(parent, 0755);
    g_free(parent);
    GError* error = nullptr;
    if (!g_file_set_contents(path.c_str(), xml.c_str(), static_cast<gssize>(xml.size()), &error)) {
        g_warning("system font: cannot write %s: %s", path.c_str(), error->message);
        g_clear_error(&error);
    }
}

} // namespace

std::string system_font_fontconfig_path() {
    gchar* path = g_build_filename(g_get_user_config_dir(), "fontconfig", "conf.d",
                                   "50-hypr-shell-sans-serif.conf", nullptr);
    std::string result = path;
    g_free(path);
    return result;
}

std::string SystemFont::to_string() const {
    std::string text = family;
    if (!style.empty())
        text += " " + style;
    return text + " " + format_size(size);
}

GSettings* system_font_settings() {
    GSettingsSchemaSource* source = g_settings_schema_source_get_default();
    if (source == nullptr)
        return nullptr;
    GSettingsSchema* schema = g_settings_schema_source_lookup(source, kSchema, TRUE);
    if (schema == nullptr)
        return nullptr;
    const bool has_key = g_settings_schema_has_key(schema, kKey);
    g_settings_schema_unref(schema);
    return has_key ? g_settings_new(kSchema) : nullptr;
}

SystemFont system_font_read() {
    if (GSettings* settings = system_font_settings()) {
        gchar* value = g_settings_get_string(settings, kKey);
        SystemFont font = parse(value != nullptr ? value : "");
        g_free(value);
        g_object_unref(settings);
        return font;
    }
    for (const char* dir : {"gtk-4.0", "gtk-3.0"}) {
        GKeyFile* key_file = g_key_file_new();
        gchar* value = nullptr;
        if (g_key_file_load_from_file(key_file, ini_path(dir).c_str(), G_KEY_FILE_NONE, nullptr))
            value = g_key_file_get_string(key_file, "Settings", "gtk-font-name", nullptr);
        g_key_file_free(key_file);
        if (value != nullptr) {
            SystemFont font = parse(value);
            g_free(value);
            return font;
        }
    }
    return {};
}

void system_font_write(const SystemFont& font) {
    const std::string value = font.to_string();
    if (GSettings* settings = system_font_settings()) {
        g_settings_set_string(settings, kKey, value.c_str());
        g_settings_sync();
        g_object_unref(settings);
    }
    write_ini("gtk-3.0", value);
    write_ini("gtk-4.0", value);
    write_fontconfig(font.family);
}

} // namespace hyprshell::settings
