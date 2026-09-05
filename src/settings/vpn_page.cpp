#include "settings/vpn_page.hpp"

#include "settings/command.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hyprshell::settings {
namespace {

constexpr guint kPollMs = 3000;

struct Profile {
    std::string name;
    std::string uuid;
    std::string type;   // "vpn" (OpenVPN & co.) or "wireguard"
    std::string device; // bound device while active
    bool active = false;
    bool operator==(const Profile&) const = default;
};

struct Vpn {
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    GtkWindow* window = nullptr;
    GtkWidget* page = nullptr;
    GtkWidget* group = nullptr;       // "Profiles" group holding the rows
    GtkWidget* status_row = nullptr;  // .property: summary / last error
    GtkWidget* empty_row = nullptr;   // shown when there are no profiles
    std::vector<GtkWidget*> rows;

    std::vector<Profile> profiles;
    std::string busy_uuid;            // connect/disconnect/delete in flight
    bool importing = false;
    bool refreshing = false;
    std::string error;
    guint poll_source = 0;
    bool loading = false;             // setting switch states programmatically
};

CommandDone guarded(Vpn* v, std::function<void(bool, int, const std::string&, const std::string&)> f) {
    std::weak_ptr<bool> alive = v->alive;
    return [alive, f = std::move(f)](bool ok, int status, const std::string& out,
                                     const std::string& err) {
        if (alive.lock())
            f(ok, status, out, err);
    };
}

// nmcli -t escapes ':' and '\' inside values as "\:" and "\\".
std::string unescape_terse(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            out += text[++i];
            continue;
        }
        out += text[i];
    }
    return out;
}

// `nmcli -t -f NAME,UUID,TYPE,DEVICE connection show`, parsed from the right
// so names containing ':' survive (Noctalia's parser), then unescaped.
std::vector<Profile> parse_profiles(const std::string& out) {
    std::vector<Profile> list;
    for (std::string line : split_lines(out)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        const auto c1 = line.rfind(':');
        if (c1 == std::string::npos)
            continue;
        const std::string device = line.substr(c1 + 1);
        const std::string rest = line.substr(0, c1);
        const auto c2 = rest.rfind(':');
        if (c2 == std::string::npos)
            continue;
        const std::string type = rest.substr(c2 + 1);
        if (type != "vpn" && type != "wireguard")
            continue;
        const std::string rest2 = rest.substr(0, c2);
        const auto c3 = rest2.rfind(':');
        if (c3 == std::string::npos)
            continue;
        Profile p;
        p.uuid = rest2.substr(c3 + 1);
        p.name = unescape_terse(rest2.substr(0, c3));
        p.type = type;
        p.device = device == "--" ? "" : device;
        p.active = !p.device.empty();
        if (!p.uuid.empty() && !p.name.empty())
            list.push_back(std::move(p));
    }
    std::sort(list.begin(), list.end(), [](const Profile& a, const Profile& b) {
        return g_utf8_collate(a.name.c_str(), b.name.c_str()) < 0;
    });
    return list;
}

void render_status(Vpn* v) {
    std::string text;
    if (!v->error.empty()) {
        text = v->error;
        gtk_widget_add_css_class(v->status_row, "error");
    } else {
        gtk_widget_remove_css_class(v->status_row, "error");
        std::vector<std::string> active;
        for (const auto& p : v->profiles)
            if (p.active)
                active.push_back(p.name);
        if (v->importing)
            text = "Importing…";
        else if (active.empty())
            text = "Not connected";
        else {
            text = "Connected to " + active.front();
            if (active.size() > 1)
                text += " and " + std::to_string(active.size() - 1) + " more";
        }
    }
    adw_action_row_set_subtitle(ADW_ACTION_ROW(v->status_row), text.c_str());
}

void refresh(Vpn* v);

void on_toggle(GObject* row, GParamSpec*, gpointer data) {
    Vpn* v = static_cast<Vpn*>(data);
    if (v->loading)
        return;
    const char* uuid = static_cast<const char*>(g_object_get_data(row, "uuid"));
    if (uuid == nullptr || !v->busy_uuid.empty())
        return;
    const bool want = adw_switch_row_get_active(ADW_SWITCH_ROW(row));
    const std::string id = uuid;
    v->busy_uuid = id;
    v->error.clear();
    gtk_widget_set_sensitive(GTK_WIDGET(row), FALSE);
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), want ? "Connecting…" : "Disconnecting…");
    run_command({"nmcli", "connection", want ? "up" : "down", "uuid", id},
                guarded(v, [v, id, want](bool ok, int, const std::string& out,
                                          const std::string& err) {
                    // Noctalia trusts nmcli's success text for `up` — it can
                    // exit 0 while the connection failed
                    const bool success =
                        want ? (out.find("successfully activated") != std::string::npos ||
                                out.find("Connection successfully") != std::string::npos)
                             : ok;
                    if (!success) {
                        const std::string line = first_line(err.empty() ? out : err);
                        v->error = (want ? "Could not connect: " : "Could not disconnect: ") +
                                   (line.empty() ? std::string("nmcli failed") : line);
                    }
                    v->busy_uuid.clear();
                    refresh(v);
                }));
}

void delete_profile(Vpn* v, const std::string& uuid) {
    if (!v->busy_uuid.empty())
        return;
    v->busy_uuid = uuid;
    v->error.clear();
    run_command({"nmcli", "connection", "delete", "uuid", uuid},
                guarded(v, [v](bool, int, const std::string& out, const std::string& err) {
                    if (out.find("successfully deleted") == std::string::npos) {
                        const std::string line = first_line(err.empty() ? out : err);
                        v->error = "Could not delete: " +
                                   (line.empty() ? std::string("nmcli failed") : line);
                    }
                    v->busy_uuid.clear();
                    refresh(v);
                }));
}

void on_delete_clicked(GtkButton* button, gpointer data) {
    Vpn* v = static_cast<Vpn*>(data);
    const char* uuid = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "uuid"));
    const char* name = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "name"));
    if (uuid == nullptr)
        return;
    const std::string body =
        std::string("The VPN profile “") + (name != nullptr ? name : "") +
        "” will be removed from NetworkManager. This cannot be undone.";
    AdwDialog* dialog = adw_alert_dialog_new("Delete VPN profile?", body.c_str());
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "_Cancel", "delete",
                                   "_Delete", nullptr);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "delete",
                                             ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "cancel");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
    g_object_set_data_full(G_OBJECT(dialog), "uuid", g_strdup(uuid), g_free);
    g_signal_connect(dialog, "response",
                     G_CALLBACK(+[](AdwAlertDialog* dialog, const char* response, gpointer data) {
                         if (g_strcmp0(response, "delete") != 0)
                             return;
                         const char* uuid =
                             static_cast<const char*>(g_object_get_data(G_OBJECT(dialog), "uuid"));
                         delete_profile(static_cast<Vpn*>(data), uuid);
                     }),
                     v);
    adw_dialog_present(dialog, GTK_WIDGET(v->window));
}

void import_file(Vpn* v, const std::string& path) {
    if (v->importing)
        return;
    // NetworkManager needs the plugin type up-front; infer it from the extension
    gchar* lower = g_ascii_strdown(path.c_str(), -1);
    const bool ovpn = g_str_has_suffix(lower, ".ovpn");
    g_free(lower);
    v->importing = true;
    v->error.clear();
    render_status(v);
    run_command({"nmcli", "connection", "import", "type", ovpn ? "openvpn" : "wireguard", "file",
                 path},
                guarded(v, [v](bool, int, const std::string& out, const std::string& err) {
                    v->importing = false;
                    if (out.find("successfully added") == std::string::npos) {
                        const std::string line = first_line(err.empty() ? out : err);
                        v->error = "Import failed: " +
                                   (line.empty() ? std::string("nmcli failed") : line);
                    }
                    refresh(v);
                }));
}

void on_import_clicked(GtkButton*, gpointer data) {
    Vpn* v = static_cast<Vpn*>(data);
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Import VPN configuration");
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "VPN configurations (WireGuard .conf, OpenVPN .ovpn)");
    gtk_file_filter_add_suffix(filter, "conf");
    gtk_file_filter_add_suffix(filter, "ovpn");
    GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, filter);
    g_object_unref(filters);
    g_object_unref(filter);
    const char* downloads = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (downloads != nullptr) {
        GFile* folder = g_file_new_for_path(downloads);
        gtk_file_dialog_set_initial_folder(dialog, folder);
        g_object_unref(folder);
    }
    gtk_file_dialog_open(
        dialog, v->window, nullptr,
        +[](GObject* source, GAsyncResult* result, gpointer data) {
            GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, nullptr);
            if (file != nullptr) {
                gchar* path = g_file_get_path(file);
                if (path != nullptr)
                    import_file(static_cast<Vpn*>(data), path);
                g_free(path);
                g_object_unref(file);
            }
            g_object_unref(source);
        },
        v);
}

void rebuild_rows(Vpn* v) {
    for (GtkWidget* row : v->rows)
        adw_preferences_group_remove(ADW_PREFERENCES_GROUP(v->group), row);
    v->rows.clear();

    gtk_widget_set_visible(v->empty_row, v->profiles.empty());
    for (const auto& p : v->profiles) {
        GtkWidget* row = adw_switch_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), p.name.c_str());
        std::string subtitle = p.type == "wireguard" ? "WireGuard" : "VPN plugin";
        if (p.active)
            subtitle += " · connected on " + p.device;
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle.c_str());
        g_object_set_data_full(G_OBJECT(row), "uuid", g_strdup(p.uuid.c_str()), g_free);
        {
            v->loading = true;
            adw_switch_row_set_active(ADW_SWITCH_ROW(row), p.active);
            v->loading = false;
        }
        gtk_widget_set_sensitive(row, v->busy_uuid.empty());
        g_signal_connect(row, "notify::active", G_CALLBACK(on_toggle), v);

        GtkWidget* del = gtk_button_new_from_icon_name("user-trash-symbolic");
        gtk_widget_add_css_class(del, "flat");
        gtk_widget_set_valign(del, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(del, "Delete profile");
        gtk_widget_set_sensitive(del, !p.active && v->busy_uuid.empty());
        g_object_set_data_full(G_OBJECT(del), "uuid", g_strdup(p.uuid.c_str()), g_free);
        g_object_set_data_full(G_OBJECT(del), "name", g_strdup(p.name.c_str()), g_free);
        g_signal_connect(del, "clicked", G_CALLBACK(on_delete_clicked), v);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), del);

        adw_preferences_group_add(ADW_PREFERENCES_GROUP(v->group), row);
        v->rows.push_back(row);
    }
    render_status(v);
}

void refresh(Vpn* v) {
    if (v->refreshing)
        return;
    v->refreshing = true;
    run_command({"nmcli", "-t", "-f", "NAME,UUID,TYPE,DEVICE", "connection", "show"},
                guarded(v, [v](bool ok, int, const std::string& out, const std::string& err) {
                    v->refreshing = false;
                    if (!ok && out.empty()) {
                        v->error = "nmcli: " + first_line(err);
                        render_status(v);
                        return;
                    }
                    auto profiles = parse_profiles(out);
                    if (profiles == v->profiles && !v->rows.empty() == !profiles.empty()) {
                        render_status(v); // the 3 s poll: nothing changed, keep the rows
                        return;
                    }
                    v->profiles = std::move(profiles);
                    rebuild_rows(v);
                }));
}

gboolean on_poll(gpointer data) {
    Vpn* v = static_cast<Vpn*>(data);
    if (v->busy_uuid.empty() && !v->importing)
        refresh(v);
    return G_SOURCE_CONTINUE;
}

void on_map(GtkWidget*, gpointer data) {
    Vpn* v = static_cast<Vpn*>(data);
    refresh(v);
    if (v->poll_source == 0)
        v->poll_source = g_timeout_add(kPollMs, on_poll, v);
}

void on_unmap(GtkWidget*, gpointer data) {
    Vpn* v = static_cast<Vpn*>(data);
    if (v->poll_source != 0) {
        g_source_remove(v->poll_source);
        v->poll_source = 0;
    }
}

} // namespace

GtkWidget* build_vpn_page(GtkWindow* window) {
    auto* v = new Vpn;
    v->window = window;
    v->page = adw_preferences_page_new();
    g_object_set_data_full(G_OBJECT(v->page), "vpn-state", v, [](gpointer p) {
        auto* v = static_cast<Vpn*>(p);
        *v->alive = false;
        if (v->poll_source != 0)
            g_source_remove(v->poll_source);
        delete v;
    });

    GtkWidget* status_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(status_group), "VPN");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(status_group),
        "NetworkManager VPN profiles. Import a WireGuard .conf or OpenVPN .ovpn file, then "
        "switch a profile on to connect.");
    v->status_row = adw_action_row_new();
    gtk_widget_add_css_class(v->status_row, "property");
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(v->status_row), "Status");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(v->status_row), "Not connected");
    adw_action_row_set_subtitle_selectable(ADW_ACTION_ROW(v->status_row), TRUE);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(status_group), v->status_row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(v->page), ADW_PREFERENCES_GROUP(status_group));

    v->group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(v->group), "Profiles");
    GtkWidget* import = gtk_button_new();
    GtkWidget* import_content = adw_button_content_new();
    adw_button_content_set_icon_name(ADW_BUTTON_CONTENT(import_content), "list-add-symbolic");
    adw_button_content_set_label(ADW_BUTTON_CONTENT(import_content), "Import");
    gtk_button_set_child(GTK_BUTTON(import), import_content);
    gtk_widget_add_css_class(import, "flat");
    gtk_widget_set_tooltip_text(import, "Import a .conf or .ovpn file");
    g_signal_connect(import, "clicked", G_CALLBACK(on_import_clicked), v);
    adw_preferences_group_set_header_suffix(ADW_PREFERENCES_GROUP(v->group), import);
    v->empty_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(v->empty_row), "No VPN profiles");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(v->empty_row),
                                "Use Import to add a WireGuard or OpenVPN configuration");
    gtk_widget_set_sensitive(v->empty_row, FALSE);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(v->group), v->empty_row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(v->page), ADW_PREFERENCES_GROUP(v->group));

    g_signal_connect(v->page, "map", G_CALLBACK(on_map), v);
    g_signal_connect(v->page, "unmap", G_CALLBACK(on_unmap), v);
    return v->page;
}

} // namespace hyprshell::settings
