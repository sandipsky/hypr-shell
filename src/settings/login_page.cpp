// "Login screen" page — see login_page.hpp. Keys written to theme.conf.user
// ([General], the theme's Main.qml documents them):
//   background       #rrggbb   solid colour (default GDM's #222226)
//   backgroundMode   color | image
//   backgroundImage  file in the theme dir the greeter loads (our copy)
//   backgroundSource the user's original path (this page only, for display)
//   backgroundBlur   0..1 like lock_screen.blur (× 48 px)
//   showSessionMenu  true | false
//   defaultSession   /usr/share/{wayland-,x}sessions/*.desktop, "" = last used
// Other keys in the file (scripts/sddm.sh's `accent`) are preserved.
#include "settings/login_page.hpp"

#include "settings/command.hpp"

#include <glib/gstdio.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hyprshell::settings {
namespace {

constexpr const char* kThemeDir = "/usr/share/sddm/themes/Elegant";
// gnome-shell's #lockDialogGroup background ($_gdm_bg → $_base_color_dark)
constexpr const char* kDefaultBackground = "#222226";
constexpr const char* kModeKeys[] = {"color", "image"};
constexpr const char* kSessionDirs[] = {"/usr/share/wayland-sessions", "/usr/share/xsessions"};

using Values = std::map<std::string, std::string>;

struct Session {
    std::string path;
    std::string name;
};

struct LoginPage {
    AdwComboRow* mode = nullptr;
    GtkWidget* color_row = nullptr;
    GtkWidget* color_button = nullptr;
    GtkWidget* color_reset = nullptr;
    GtkWidget* image_row = nullptr;
    GtkWidget* blur_row = nullptr;
    GtkAdjustment* blur = nullptr;
    AdwSwitchRow* session_menu = nullptr;
    AdwComboRow* default_session = nullptr;
    GtkWidget* apply_row = nullptr;
    GtkWidget* apply_button = nullptr;

    std::vector<Session> sessions;
    Values saved;      // theme.conf overlaid with theme.conf.user, as last read
    Values user_saved; // theme.conf.user alone — keys we don't own are kept
    bool loading = false;
    bool busy = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

// -- files ------------------------------------------------------------------

// key=value lines of a single-section INI (theme.conf has only [General]).
void read_ini(const std::string& path, Values& out) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '[' || line[0] == '#' || line[0] == ';')
            continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        out[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
}

std::string value_or(const Values& v, const std::string& key, const char* fallback) {
    auto it = v.find(key);
    return it == v.end() ? fallback : it->second;
}

bool is_hex_color(const std::string& s) {
    if (s.size() != 7 || s[0] != '#')
        return false;
    for (size_t i = 1; i < s.size(); ++i)
        if (!g_ascii_isxdigit(s[i]))
            return false;
    return true;
}

// Current=<theme> from every SDDM config file, the last one wins like SDDM.
std::string current_sddm_theme() {
    std::string theme;
    auto scan = [&theme](const std::string& path) {
        std::ifstream in(path);
        std::string line;
        bool in_theme = false;
        while (std::getline(in, line)) {
            line = trim(line);
            if (!line.empty() && line[0] == '[') {
                in_theme = line == "[Theme]";
                continue;
            }
            if (in_theme && line.rfind("Current=", 0) == 0)
                theme = trim(line.substr(8));
        }
    };
    auto scan_dir = [&scan](const char* dir) {
        GDir* d = g_dir_open(dir, 0, nullptr);
        if (d == nullptr)
            return;
        std::vector<std::string> files;
        while (const char* name = g_dir_read_name(d))
            if (g_str_has_suffix(name, ".conf"))
                files.push_back(std::string(dir) + "/" + name);
        g_dir_close(d);
        std::sort(files.begin(), files.end());
        for (const auto& f : files)
            scan(f);
    };
    scan_dir("/usr/lib/sddm/sddm.conf.d");
    scan("/etc/sddm.conf");
    scan_dir("/etc/sddm.conf.d");
    return theme;
}

std::vector<Session> list_sessions() {
    std::vector<Session> out;
    for (const char* dir : kSessionDirs) {
        GDir* d = g_dir_open(dir, 0, nullptr);
        if (d == nullptr)
            continue;
        while (const char* name = g_dir_read_name(d)) {
            if (!g_str_has_suffix(name, ".desktop"))
                continue;
            const std::string path = std::string(dir) + "/" + name;
            GKeyFile* kf = g_key_file_new();
            if (g_key_file_load_from_file(kf, path.c_str(), G_KEY_FILE_NONE, nullptr)
                && !g_key_file_get_boolean(kf, "Desktop Entry", "Hidden", nullptr)
                && !g_key_file_get_boolean(kf, "Desktop Entry", "NoDisplay", nullptr)) {
                gchar* title = g_key_file_get_locale_string(kf, "Desktop Entry", "Name", nullptr, nullptr);
                out.push_back({path, title != nullptr ? title : name});
                g_free(title);
            }
            g_key_file_free(kf);
        }
        g_dir_close(d);
    }
    std::sort(out.begin(), out.end(),
              [](const Session& a, const Session& b) { return a.name < b.name; });
    return out;
}

// -- widget <-> values ------------------------------------------------------

std::string rgba_hex(const GdkRGBA& c) {
    gchar* s = g_strdup_printf("#%02x%02x%02x", (int)std::round(c.red * 255),
                               (int)std::round(c.green * 255), (int)std::round(c.blue * 255));
    std::string out = s;
    g_free(s);
    return out;
}

Values current_values(LoginPage* p) {
    Values v;
    const guint mode = adw_combo_row_get_selected(p->mode);
    v["backgroundMode"] = kModeKeys[mode < G_N_ELEMENTS(kModeKeys) ? mode : 0];
    v["background"] = rgba_hex(*gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(p->color_button)));
    v["backgroundSource"] = gtk_editable_get_text(GTK_EDITABLE(p->image_row));
    gchar* blur = g_strdup_printf("%.2f", std::round(gtk_adjustment_get_value(p->blur)) / 100.0);
    v["backgroundBlur"] = blur;
    g_free(blur);
    v["showSessionMenu"] = adw_switch_row_get_active(p->session_menu) ? "true" : "false";
    const guint sel = adw_combo_row_get_selected(p->default_session);
    v["defaultSession"] = (sel >= 1 && sel - 1 < p->sessions.size()) ? p->sessions[sel - 1].path : "";
    return v;
}

void update_visibility(LoginPage* p) {
    const bool image = adw_combo_row_get_selected(p->mode) == 1;
    gtk_widget_set_visible(p->color_row, !image);
    gtk_widget_set_visible(p->image_row, image);
    gtk_widget_set_visible(p->blur_row, image);
    const std::string color = rgba_hex(*gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(p->color_button)));
    gtk_widget_set_visible(p->color_reset, color != kDefaultBackground);
}

// Apply is sensitive only while the widgets differ from the file.
void update_dirty(LoginPage* p) {
    if (p->loading)
        return;
    update_visibility(p);
    const Values now = current_values(p);
    bool dirty = false;
    for (const auto& [key, value] : now)
        if (value_or(p->saved, key, "") != value) {
            dirty = true;
            break;
        }
    gtk_widget_set_sensitive(p->apply_button, dirty && !p->busy);
    if (dirty)
        adw_action_row_set_subtitle(ADW_ACTION_ROW(p->apply_row),
                                    "Writes the theme's settings as administrator; they show at the "
                                    "next login screen.");
}

void set_color(LoginPage* p, const std::string& hex) {
    GdkRGBA rgba;
    if (!gdk_rgba_parse(&rgba, is_hex_color(hex) ? hex.c_str() : kDefaultBackground))
        gdk_rgba_parse(&rgba, kDefaultBackground);
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(p->color_button), &rgba);
}

void load(LoginPage* p) {
    p->saved.clear();
    p->user_saved.clear();
    read_ini(std::string(kThemeDir) + "/theme.conf", p->saved);
    read_ini(std::string(kThemeDir) + "/theme.conf.user", p->user_saved);
    for (const auto& [k, v] : p->user_saved)
        p->saved[k] = v;
    // what the page shows for the image: the original path, else the copy
    if (p->saved["backgroundSource"].empty() && !p->saved["backgroundImage"].empty()) {
        const std::string img = p->saved["backgroundImage"];
        p->saved["backgroundSource"] = img[0] == '/' ? img : std::string(kThemeDir) + "/" + img;
    }
    // normalise so the dirty check compares like with like
    p->saved["background"] = is_hex_color(p->saved["background"]) ? p->saved["background"] : kDefaultBackground;
    p->saved["backgroundMode"] = p->saved["backgroundMode"] == "image" ? "image" : "color";
    {
        double blur = g_ascii_strtod(p->saved["backgroundBlur"].c_str(), nullptr);
        blur = std::isfinite(blur) ? std::clamp(blur, 0.0, 1.0) : 0.0;
        gchar* s = g_strdup_printf("%.2f", std::round(blur * 100.0) / 100.0);
        p->saved["backgroundBlur"] = s;
        g_free(s);
    }
    p->saved["showSessionMenu"] = p->saved["showSessionMenu"] == "false" ? "false" : "true";
    guint session_index = 0;
    for (size_t i = 0; i < p->sessions.size(); ++i)
        if (p->sessions[i].path == p->saved["defaultSession"])
            session_index = static_cast<guint>(i + 1);
    if (session_index == 0)
        p->saved["defaultSession"] = "";

    p->loading = true;
    adw_combo_row_set_selected(p->mode, p->saved["backgroundMode"] == "image" ? 1 : 0);
    set_color(p, p->saved["background"]);
    gtk_editable_set_text(GTK_EDITABLE(p->image_row), p->saved["backgroundSource"].c_str());
    gtk_adjustment_set_value(p->blur, std::round(g_ascii_strtod(p->saved["backgroundBlur"].c_str(), nullptr) * 100.0));
    adw_switch_row_set_active(p->session_menu, p->saved["showSessionMenu"] == "true");
    adw_combo_row_set_selected(p->default_session, session_index);
    p->loading = false;
    update_visibility(p);
    update_dirty(p);
}

// -- apply (pkexec) ---------------------------------------------------------

void on_apply_clicked(GtkButton*, gpointer data) {
    auto* p = static_cast<LoginPage*>(data);
    if (p->busy)
        return;
    Values v = p->user_saved; // keeps accent and anything else we don't own
    for (const auto& [k, val] : current_values(p))
        v[k] = val;

    // Image mode: the greeter can't read the user's home, so the file is
    // copied into the theme dir as background.<ext> and that name is what the
    // theme loads. An image already inside the theme dir is used as is.
    std::string src_image;
    std::string dst_image;
    if (v["backgroundMode"] == "image") {
        const std::string source = v["backgroundSource"];
        if (source.empty() || !g_file_test(source.c_str(), G_FILE_TEST_IS_REGULAR)) {
            adw_action_row_set_subtitle(ADW_ACTION_ROW(p->apply_row),
                                        "Choose an image file first.");
            return;
        }
        if (source.rfind(std::string(kThemeDir) + "/", 0) == 0) {
            v["backgroundImage"] = source.substr(std::string(kThemeDir).size() + 1);
        } else {
            const char* dot = strrchr(source.c_str(), '.');
            std::string ext = (dot != nullptr && strchr(dot, '/') == nullptr) ? dot : "";
            for (auto& c : ext)
                c = static_cast<char>(g_ascii_tolower(c));
            src_image = source;
            v["backgroundImage"] = "background" + ext;
            dst_image = std::string(kThemeDir) + "/" + v["backgroundImage"];
        }
    }

    std::string text = "[General]\n";
    for (const auto& [k, val] : v)
        text += k + "=" + val + "\n";
    gchar* dir = g_build_filename(g_get_user_cache_dir(), "hypr-shell", nullptr);
    g_mkdir_with_parents(dir, 0755);
    gchar* tmp = g_build_filename(dir, "sddm-theme.conf.user", nullptr);
    g_free(dir);
    GError* error = nullptr;
    if (!g_file_set_contents(tmp, text.c_str(), static_cast<gssize>(text.size()), &error)) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(p->apply_row), error->message);
        g_error_free(error);
        g_free(tmp);
        return;
    }
    const std::string tmp_path = tmp;
    g_free(tmp);

    p->busy = true;
    gtk_widget_set_sensitive(p->apply_button, FALSE);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->apply_row), "Waiting for authorisation…");
    // One privileged step for both files; positional parameters keep paths
    // out of the shell text.
    const std::string script =
        "install -m 644 -o root -g root \"$1\" \"$2\" && "
        "{ [ -z \"$3\" ] || install -m 644 -o root -g root \"$3\" \"$4\"; }";
    std::weak_ptr<bool> alive = p->alive;
    run_command({"pkexec", "/bin/sh", "-c", script, "sh", tmp_path,
                 std::string(kThemeDir) + "/theme.conf.user", src_image, dst_image},
                [p, alive, tmp_path](bool ok, int status, const std::string&, const std::string& err) {
                    g_unlink(tmp_path.c_str());
                    if (alive.expired())
                        return;
                    p->busy = false;
                    if (ok) {
                        load(p);
                        adw_action_row_set_subtitle(ADW_ACTION_ROW(p->apply_row),
                                                    "Applied — the change shows at the next login screen.");
                        return;
                    }
                    // pkexec: 126 = dialog dismissed, 127 = not authorised
                    const std::string why = (status == 126 || status == 127)
                                                ? "Authorisation was not granted."
                                                : "Failed: " + first_line(err);
                    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->apply_row), why.c_str());
                    update_dirty(p);
                });
}

// -- handlers ---------------------------------------------------------------

void on_changed(GObject*, GParamSpec*, gpointer data) { update_dirty(static_cast<LoginPage*>(data)); }
void on_blur_changed(GtkAdjustment*, gpointer data) { update_dirty(static_cast<LoginPage*>(data)); }
void on_image_changed(GtkEditable*, gpointer data) { update_dirty(static_cast<LoginPage*>(data)); }

void on_color_reset_clicked(GtkButton*, gpointer data) {
    auto* p = static_cast<LoginPage*>(data);
    set_color(p, kDefaultBackground); // notify::rgba → update_dirty
}

void on_browse_clicked(GtkButton* button, gpointer data) {
    auto* p = static_cast<LoginPage*>(data);
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select login screen background");
    GtkFileFilter* images = gtk_file_filter_new();
    gtk_file_filter_set_name(images, "Images");
    gtk_file_filter_add_mime_type(images, "image/*");
    GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, images);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, images);
    g_object_unref(filters);
    g_object_unref(images);
    const char* current = gtk_editable_get_text(GTK_EDITABLE(p->image_row));
    gchar* start = (current != nullptr && *current != '\0')
                       ? g_path_get_dirname(current)
                       : g_build_filename(g_get_home_dir(), "Pictures", nullptr);
    GFile* folder = g_file_new_for_path(start);
    gtk_file_dialog_set_initial_folder(dialog, folder);
    g_object_unref(folder);
    g_free(start);
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
        p->image_row);
    g_object_unref(dialog);
}

} // namespace

bool login_page_available() {
    gchar* sddm = g_find_program_in_path("sddm");
    const bool have_sddm = sddm != nullptr;
    g_free(sddm);
    if (!have_sddm)
        return false;
    if (!g_file_test((std::string(kThemeDir) + "/Main.qml").c_str(), G_FILE_TEST_IS_REGULAR))
        return false;
    return current_sddm_theme() == "Elegant";
}

GtkWidget* build_login_page(GtkWindow*) {
    auto* p = new LoginPage();
    GtkWidget* page = adw_preferences_page_new();
    g_object_set_data_full(G_OBJECT(page), "login-page-state", p, [](gpointer data) {
        auto* p = static_cast<LoginPage*>(data);
        *p->alive = false;
        delete p;
    });

    // -- Background ---------------------------------------------------------
    GtkWidget* bg_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(bg_group), "Background");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(bg_group),
        "The login screen uses the Elegant SDDM theme, styled like the lock screen.");
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(bg_group));

    GtkWidget* mode_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(mode_row), "Background");
    const char* modes[] = {"Solid color", "Image", nullptr};
    GtkStringList* mode_model = gtk_string_list_new(modes);
    adw_combo_row_set_model(ADW_COMBO_ROW(mode_row), G_LIST_MODEL(mode_model));
    g_object_unref(mode_model);
    p->mode = ADW_COMBO_ROW(mode_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(bg_group), mode_row);

    p->color_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->color_row), "Color");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->color_row),
                                "Default is the solid background GDM's login screen uses.");
    GtkColorDialog* color_dialog = gtk_color_dialog_new();
    gtk_color_dialog_set_title(color_dialog, "Login screen background");
    gtk_color_dialog_set_with_alpha(color_dialog, FALSE);
    p->color_button = gtk_color_dialog_button_new(color_dialog);
    gtk_widget_set_valign(p->color_button, GTK_ALIGN_CENTER);
    p->color_reset = gtk_button_new_from_icon_name("edit-undo-symbolic");
    gtk_widget_add_css_class(p->color_reset, "flat");
    gtk_widget_set_valign(p->color_reset, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(p->color_reset, "Reset to GDM's default (#222226)");
    adw_action_row_add_suffix(ADW_ACTION_ROW(p->color_row), p->color_reset);
    adw_action_row_add_suffix(ADW_ACTION_ROW(p->color_row), p->color_button);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(bg_group), p->color_row);

    p->image_row = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->image_row), "Image");
    GtkWidget* browse = gtk_button_new_from_icon_name("image-x-generic-symbolic");
    gtk_widget_add_css_class(browse, "flat");
    gtk_widget_set_valign(browse, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(browse, "Select an image");
    g_signal_connect(browse, "clicked", G_CALLBACK(on_browse_clicked), p);
    adw_entry_row_add_suffix(ADW_ENTRY_ROW(p->image_row), browse);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(bg_group), p->image_row);

    p->blur_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->blur_row), "Blur strength");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->blur_row),
                                "Applies a blur effect to the login screen image, like the lock screen.");
    p->blur = gtk_adjustment_new(0, 0, 100, 1, 10, 0);
    GtkWidget* blur_scale = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, p->blur);
    gtk_scale_set_draw_value(GTK_SCALE(blur_scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(blur_scale), GTK_POS_RIGHT);
    gtk_scale_set_format_value_func(
        GTK_SCALE(blur_scale),
        [](GtkScale*, double value, gpointer) { return g_strdup_printf("%d%%", (int)std::round(value)); },
        nullptr, nullptr);
    gtk_widget_set_size_request(blur_scale, 200, -1);
    gtk_widget_set_valign(blur_scale, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(p->blur_row), blur_scale);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(bg_group), p->blur_row);

    // -- Sessions -------------------------------------------------------------
    GtkWidget* session_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(session_group), "Sessions");
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(session_group));

    GtkWidget* menu_row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(menu_row), "Show session menu");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(menu_row),
                                "A button in the corner to pick another desktop environment. "
                                "Only shown when more than one session is installed.");
    p->session_menu = ADW_SWITCH_ROW(menu_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(session_group), menu_row);

    p->sessions = list_sessions();
    GtkWidget* default_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(default_row), "Default session");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(default_row),
                                "Preselected at the login screen. \"Last used\" keeps SDDM's memory.");
    GtkStringList* session_model = gtk_string_list_new(nullptr);
    gtk_string_list_append(session_model, "Last used");
    for (const auto& session : p->sessions)
        gtk_string_list_append(session_model, session.name.c_str());
    adw_combo_row_set_model(ADW_COMBO_ROW(default_row), G_LIST_MODEL(session_model));
    g_object_unref(session_model);
    p->default_session = ADW_COMBO_ROW(default_row);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(session_group), default_row);

    // -- Apply ----------------------------------------------------------------
    // Every other page saves instantly, but this file is root-owned: one
    // pkexec prompt per change would be unbearable, so changes are applied
    // together on request.
    GtkWidget* apply_group = adw_preferences_group_new();
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(apply_group));
    p->apply_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(p->apply_row), "Apply changes");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(p->apply_row),
                                "Writes the theme's settings as administrator; they show at the "
                                "next login screen.");
    p->apply_button = gtk_button_new_with_label("Apply");
    gtk_widget_add_css_class(p->apply_button, "suggested-action");
    gtk_widget_set_valign(p->apply_button, GTK_ALIGN_CENTER);
    g_signal_connect(p->apply_button, "clicked", G_CALLBACK(on_apply_clicked), p);
    adw_action_row_add_suffix(ADW_ACTION_ROW(p->apply_row), p->apply_button);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(apply_group), p->apply_row);

    load(p);

    // connected after load(): widget writes above must not count as edits
    g_signal_connect(mode_row, "notify::selected", G_CALLBACK(on_changed), p);
    g_signal_connect(p->color_button, "notify::rgba", G_CALLBACK(on_changed), p);
    g_signal_connect(p->color_reset, "clicked", G_CALLBACK(on_color_reset_clicked), p);
    g_signal_connect(p->image_row, "changed", G_CALLBACK(on_image_changed), p);
    g_signal_connect(p->blur, "value-changed", G_CALLBACK(on_blur_changed), p);
    g_signal_connect(menu_row, "notify::active", G_CALLBACK(on_changed), p);
    g_signal_connect(default_row, "notify::selected", G_CALLBACK(on_changed), p);
    return page;
}

} // namespace hyprshell::settings
