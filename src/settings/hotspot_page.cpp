#include "settings/hotspot_page.hpp"

#include "settings/command.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hyprshell::settings {
namespace {

constexpr const char* kProfile = "Hotspot"; // NetworkManager connection id
constexpr guint kSaveDebounceMs = 600;
constexpr guint kPollMs = 3000;
constexpr int kVirtualWaitTries = 25; // x 200 ms for NM to see a new interface
constexpr const char* kSecurityKeys[] = {"none", "wpa-psk", "sae"};
constexpr const char* kBandKeys[] = {"", "bg", "a"}; // Automatic / 2.4 GHz / 5 GHz

enum class IwSupport { Unknown, Missing, No, Yes };

struct Hotspot {
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    bool loading = false; // populating widgets — suppress writes
    bool busy = false;    // an up/down/apply command is running

    // widgets
    GtkWidget* page = nullptr;
    AdwSwitchRow* enabled = nullptr;
    GtkWidget* status_row = nullptr; // .property row: Off / Active on …
    AdwEntryRow* ssid = nullptr;
    AdwComboRow* security = nullptr;
    AdwPasswordEntryRow* password = nullptr;
    AdwComboRow* band = nullptr;
    AdwSwitchRow* hidden = nullptr;
    GtkWidget* adapter_row = nullptr; // shown with >1 Wi-Fi adapter
    AdwComboRow* adapter = nullptr;
    AdwSwitchRow* concurrent = nullptr;
    AdwSwitchRow* autostart = nullptr;

    // NetworkManager state
    bool exists = false;             // the "Hotspot" profile is present
    bool active = false;
    std::string active_device;       // interface the hotspot runs on
    std::string address;             // gateway address while active
    int stations = -1;               // connected devices while active (-1 unknown)
    std::vector<std::string> wifi_devices; // physical Wi-Fi adapters
    std::string parent;              // chosen physical adapter
    bool use_virtual = false;        // hotspot on "<parent>-ap" next to the client
    std::string error;               // last failure, shown in the status row

    IwSupport iw = IwSupport::Unknown;
    int iw_channels = 0; // "#channels <= N" of the supporting combination (0 = n/a)

    guint save_source = 0;
    guint poll_source = 0;
    bool page_mapped = false;
    bool defaults_done = false; // first read found no profile → defaults shown once
};

// Sets `loading` for a scope, restoring the previous value (populate() may
// already hold it — resetting to false there would leak saves).
struct LoadingScope {
    Hotspot* h;
    bool was;
    explicit LoadingScope(Hotspot* h) : h(h), was(h->loading) { h->loading = true; }
    ~LoadingScope() { h->loading = was; }
};

// True while the user is typing in the row (its inner text has focus).
bool row_editing(GtkWidget* row) {
    GtkRoot* root = gtk_widget_get_root(row);
    if (root == nullptr)
        return false;
    GtkWidget* focus = gtk_root_get_focus(root);
    return focus != nullptr && (focus == row || gtk_widget_is_ancestor(focus, row));
}

// Wraps `done` so it is skipped once the page is gone.
CommandDone guarded(Hotspot* h, std::function<void(bool, int, const std::string&, const std::string&)> f) {
    std::weak_ptr<bool> alive = h->alive;
    return [alive, f = std::move(f)](bool ok, int status, const std::string& out,
                                     const std::string& err) {
        if (alive.lock())
            f(ok, status, out, err);
    };
}

// nmcli -t "field:value" — the field never contains ':', the value may.
std::pair<std::string, std::string> split_field(const std::string& line) {
    const auto colon = line.find(':');
    if (colon == std::string::npos)
        return {line, ""};
    return {line.substr(0, colon), line.substr(colon + 1)};
}

bool is_physical_netdev(const std::string& name) {
    const std::string path = "/sys/class/net/" + name + "/device";
    return g_file_test(path.c_str(), G_FILE_TEST_EXISTS);
}

std::string virtual_name(const std::string& parent) {
    std::string name = parent + "-ap";
    if (name.size() > 15) // IFNAMSIZ - 1
        name = "ap0";
    return name;
}

std::string generate_password() {
    static const char kAlphabet[] = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::string out;
    for (int i = 0; i < 12; ++i)
        out += kAlphabet[g_random_int_range(0, static_cast<gint32>(sizeof(kAlphabet) - 1))];
    return out;
}

std::string entry_text(GtkWidget* row) {
    const char* text = gtk_editable_get_text(GTK_EDITABLE(row));
    return text != nullptr ? text : "";
}

bool security_open(Hotspot* h) {
    return adw_combo_row_get_selected(h->security) == 0;
}

bool ssid_valid(const std::string& ssid) {
    return !ssid.empty() && ssid.size() <= 32;
}

bool password_valid(const std::string& password) {
    return password.size() >= 8 && password.size() <= 63;
}

// ---- status -------------------------------------------------------------

void render_status(Hotspot* h) {
    std::string text;
    if (!h->error.empty()) {
        text = h->error;
        gtk_widget_add_css_class(h->status_row, "error");
    } else {
        gtk_widget_remove_css_class(h->status_row, "error");
        if (h->busy)
            text = h->active ? "Restarting…" : "Starting…";
        else if (!h->active)
            text = "Off";
        else {
            text = "Active on " + h->active_device;
            if (!h->address.empty())
                text += " · " + h->address;
            if (h->stations >= 0)
                text += " · " + std::to_string(h->stations) +
                        (h->stations == 1 ? " device connected" : " devices connected");
        }
    }
    adw_action_row_set_subtitle(ADW_ACTION_ROW(h->status_row), text.c_str());
}

// Enable switch: mirrors NM, blocked while the network settings are invalid.
void update_enable_row(Hotspot* h) {
    const std::string ssid = entry_text(GTK_WIDGET(h->ssid));
    const std::string password = entry_text(GTK_WIDGET(h->password));
    const bool valid = ssid_valid(ssid) && (security_open(h) || password_valid(password));
    gtk_widget_set_sensitive(GTK_WIDGET(h->enabled), valid && !h->busy);
    const char* subtitle;
    if (!valid)
        subtitle = "Enter a network name and, for a protected network, a password of 8–63 characters";
    else if (h->use_virtual)
        subtitle = "Shares this computer's internet connection; Wi-Fi stays connected";
    else
        subtitle = "Shares this computer's internet connection. Wi-Fi disconnects while the hotspot is on";
    adw_action_row_set_subtitle(ADW_ACTION_ROW(h->enabled), subtitle);

    if (ssid_valid(ssid))
        gtk_widget_remove_css_class(GTK_WIDGET(h->ssid), "error");
    else
        gtk_widget_add_css_class(GTK_WIDGET(h->ssid), "error");
    if (security_open(h) || password_valid(password))
        gtk_widget_remove_css_class(GTK_WIDGET(h->password), "error");
    else
        gtk_widget_add_css_class(GTK_WIDGET(h->password), "error");
}

void fetch_active_details(Hotspot* h) {
    const std::string dev = h->active_device;
    if (dev.empty())
        return;
    run_command({"ip", "-4", "-o", "addr", "show", "dev", dev},
            guarded(h, [h, dev](bool ok, int, const std::string& out, const std::string&) {
                if (!ok || dev != h->active_device)
                    return;
                // "3: wlo1    inet 10.42.0.1/24 brd ..."
                const auto inet = out.find("inet ");
                std::string address;
                if (inet != std::string::npos) {
                    const auto start = inet + 5;
                    const auto end = out.find_first_of("/ ", start);
                    address = out.substr(start, end == std::string::npos ? std::string::npos
                                                                         : end - start);
                }
                h->address = address;
                render_status(h);
            }));
    if (h->iw != IwSupport::Missing && h->iw != IwSupport::Unknown) {
        run_command({"iw", "dev", dev, "station", "dump"},
                guarded(h, [h, dev](bool ok, int, const std::string& out, const std::string&) {
                    if (!ok || dev != h->active_device)
                        return;
                    int count = 0;
                    for (const auto& line : split_lines(out))
                        if (line.rfind("Station ", 0) == 0)
                            ++count;
                    h->stations = count;
                    render_status(h);
                }));
    } else {
        // no iw: count reachable neighbours on the shared subnet instead
        run_command({"ip", "-4", "neigh", "show", "dev", dev},
                guarded(h, [h, dev](bool ok, int, const std::string& out, const std::string&) {
                    if (!ok || dev != h->active_device)
                        return;
                    int count = 0;
                    for (const auto& line : split_lines(out))
                        if (line.find("lladdr") != std::string::npos &&
                            line.find("FAILED") == std::string::npos)
                            ++count;
                    h->stations = count;
                    render_status(h);
                }));
    }
}

// ---- reading the profile -------------------------------------------------

void populate_from_profile(Hotspot* h, const std::string& out) {
    std::string iface, ssid, band, key_mgmt, psk, autoconnect, hidden, state, device;
    for (const auto& line : split_lines(out)) {
        auto [key, value] = split_field(line);
        if (key == "connection.interface-name")
            iface = value;
        else if (key == "connection.autoconnect")
            autoconnect = value;
        else if (key == "802-11-wireless.ssid")
            ssid = value;
        else if (key == "802-11-wireless.band")
            band = value;
        else if (key == "802-11-wireless.hidden")
            hidden = value;
        else if (key == "802-11-wireless-security.key-mgmt")
            key_mgmt = value;
        else if (key == "802-11-wireless-security.psk")
            psk = value;
        else if (key == "GENERAL.STATE")
            state = value;
        else if (key == "GENERAL.DEVICES")
            device = value;
    }

    h->exists = true;
    const bool was_active = h->active;
    h->active = state == "activated" || state == "activating";
    h->active_device = h->active ? device : "";
    if (!h->active) {
        h->address.clear();
        h->stations = -1;
    }

    // "<parent>-ap" (or ap0) means the virtual-interface mode
    h->use_virtual = false;
    if (!iface.empty()) {
        if (std::find(h->wifi_devices.begin(), h->wifi_devices.end(), iface) !=
            h->wifi_devices.end()) {
            h->parent = iface;
        } else {
            h->use_virtual = true;
            if (iface.size() > 3 && iface.compare(iface.size() - 3, 3, "-ap") == 0) {
                const std::string parent = iface.substr(0, iface.size() - 3);
                if (std::find(h->wifi_devices.begin(), h->wifi_devices.end(), parent) !=
                    h->wifi_devices.end())
                    h->parent = parent;
            }
        }
    }

    {
        LoadingScope scope(h);
        // never rewrite the settings while an edit is pending or being typed —
        // the poll would move the cursor / revert the keystrokes
        if (h->save_source == 0) {
            if (!row_editing(GTK_WIDGET(h->ssid)) &&
                ssid != entry_text(GTK_WIDGET(h->ssid)))
                gtk_editable_set_text(GTK_EDITABLE(h->ssid), ssid.c_str());
            guint security = 0;
            if (key_mgmt == "wpa-psk")
                security = 1;
            else if (key_mgmt == "sae")
                security = 2;
            adw_combo_row_set_selected(h->security, security);
            if (!row_editing(GTK_WIDGET(h->password)) &&
                psk != entry_text(GTK_WIDGET(h->password)))
                gtk_editable_set_text(GTK_EDITABLE(h->password), psk.c_str());
            guint band_index = 0;
            for (guint i = 0; i < G_N_ELEMENTS(kBandKeys); ++i)
                if (band == kBandKeys[i])
                    band_index = i;
            adw_combo_row_set_selected(h->band, band_index);
            adw_switch_row_set_active(h->hidden, hidden == "yes");
            adw_switch_row_set_active(h->autostart, autoconnect == "yes");
            adw_switch_row_set_active(h->concurrent, h->use_virtual);
            for (guint i = 0; i < h->wifi_devices.size(); ++i)
                if (h->wifi_devices[i] == h->parent)
                    adw_combo_row_set_selected(h->adapter, i);
            gtk_widget_set_visible(GTK_WIDGET(h->password), !security_open(h));
        }
        adw_switch_row_set_active(h->enabled, h->active);
    }
    (void)was_active;

    if (h->active)
        fetch_active_details(h); // address once, station count on every poll
    update_enable_row(h);
    render_status(h);
}

void populate_defaults(Hotspot* h) {
    h->exists = false;
    h->active = false;
    h->active_device.clear();
    h->use_virtual = false;
    h->defaults_done = true;
    LoadingScope scope(h);
    if (entry_text(GTK_WIDGET(h->ssid)).empty())
        gtk_editable_set_text(GTK_EDITABLE(h->ssid), g_get_host_name());
    if (entry_text(GTK_WIDGET(h->password)).empty())
        gtk_editable_set_text(GTK_EDITABLE(h->password), generate_password().c_str());
    adw_combo_row_set_selected(h->security, 1); // WPA2
    adw_combo_row_set_selected(h->band, 0);
    adw_switch_row_set_active(h->hidden, FALSE);
    adw_switch_row_set_active(h->autostart, FALSE);
    adw_switch_row_set_active(h->concurrent, FALSE);
    adw_switch_row_set_active(h->enabled, FALSE);
    gtk_widget_set_visible(GTK_WIDGET(h->password), TRUE);
    update_enable_row(h);
    render_status(h);
}

void read_profile(Hotspot* h) {
    run_command({"nmcli", "-t", "--show-secrets", "-f",
             "connection.interface-name,connection.autoconnect,802-11-wireless.ssid,"
             "802-11-wireless.band,802-11-wireless.hidden,802-11-wireless-security.key-mgmt,"
             "802-11-wireless-security.psk,GENERAL.STATE,GENERAL.DEVICES",
             "connection", "show", kProfile},
            guarded(h, [h](bool ok, int status, const std::string& out, const std::string& err) {
                if (h->busy)
                    return; // a command is changing the state right now
                if (ok) {
                    populate_from_profile(h, out);
                } else if (status == 10 || err.find("not found") != std::string::npos ||
                           err.find("Unknown connection") != std::string::npos) {
                    // profile missing (never created, or deleted elsewhere):
                    // show defaults once, then leave the user's edits alone
                    if (!h->defaults_done) {
                        populate_defaults(h);
                    } else if (h->exists || h->active) {
                        h->exists = false;
                        h->active = false;
                        h->active_device.clear();
                        LoadingScope scope(h);
                        adw_switch_row_set_active(h->enabled, FALSE);
                        update_enable_row(h);
                        render_status(h);
                    }
                } else {
                    h->error = "nmcli: " + first_line(err);
                    render_status(h);
                }
            }));
}

void refresh_devices(Hotspot* h, std::function<void()> then) {
    run_command({"nmcli", "-t", "-f", "DEVICE,TYPE", "device"},
            guarded(h, [h, then = std::move(then)](bool ok, int, const std::string& out,
                                                   const std::string&) {
                std::vector<std::string> devices;
                if (ok)
                    for (const auto& line : split_lines(out)) {
                        auto [name, type] = split_field(line);
                        if (type == "wifi" && is_physical_netdev(name))
                            devices.push_back(name);
                    }
                std::sort(devices.begin(), devices.end());
                if (devices != h->wifi_devices) {
                    h->wifi_devices = devices;
                    GtkStringList* model = gtk_string_list_new(nullptr);
                    for (const auto& d : devices)
                        gtk_string_list_append(model, d.c_str());
                    {
                        LoadingScope scope(h);
                        adw_combo_row_set_model(h->adapter, G_LIST_MODEL(model));
                    }
                    g_object_unref(model);
                    gtk_widget_set_visible(h->adapter_row, devices.size() > 1);
                }
                if (h->parent.empty() ||
                    std::find(devices.begin(), devices.end(), h->parent) == devices.end())
                    h->parent = devices.empty() ? "" : devices.front();
                gtk_widget_set_sensitive(h->page, !devices.empty());
                if (devices.empty()) {
                    h->error = "No Wi-Fi adapter found";
                    render_status(h);
                }
                then();
            }));
}

// ---- AP + client concurrency (iw) --------------------------------------

void update_concurrent_row(Hotspot* h) {
    const char* subtitle = "";
    bool sensitive = false;
    switch (h->iw) {
    case IwSupport::Unknown:
        subtitle = "Checking whether the adapter supports it…";
        break;
    case IwSupport::Missing:
        subtitle = "Install the iw package so the adapter's support can be checked";
        break;
    case IwSupport::No:
        subtitle = "This Wi-Fi adapter cannot run a hotspot and stay connected at the same time";
        break;
    case IwSupport::Yes:
        sensitive = true;
        subtitle = h->iw_channels == 1
                       ? "Runs the hotspot on a virtual interface next to the Wi-Fi connection. "
                         "This adapter needs both on the same band, so set Band to Automatic"
                       : "Runs the hotspot on a virtual interface next to the Wi-Fi connection";
        break;
    }
    adw_action_row_set_subtitle(ADW_ACTION_ROW(h->concurrent), subtitle);
    gtk_widget_set_sensitive(GTK_WIDGET(h->concurrent), sensitive);
}

// Parses `iw phy <phy> info` "valid interface combinations" for a
// combination allowing managed + AP with total >= 2.
void parse_iw_combinations(Hotspot* h, const std::string& out) {
    bool in_section = false;
    bool supported = false;
    int channels = 0;
    for (const auto& raw : split_lines(out)) {
        const std::string line = trim(raw);
        if (line.rfind("valid interface combinations", 0) == 0) {
            in_section = true;
            continue;
        }
        if (!in_section)
            continue;
        if (line.rfind("*", 0) != 0) {
            if (!line.empty() && line.find("#{") == std::string::npos &&
                line.find("<=") == std::string::npos)
                in_section = false; // next top-level section
            continue;
        }
        // "* #{ managed } <= 1, #{ AP, P2P-client, P2P-GO } <= 1, total <= 3, #channels <= 2"
        bool has_managed = false, has_ap = false;
        size_t pos = 0;
        while ((pos = line.find("#{", pos)) != std::string::npos) {
            const auto close = line.find('}', pos);
            if (close == std::string::npos)
                break;
            const std::string set = line.substr(pos + 2, close - pos - 2);
            int limit = 1;
            const auto le = line.find("<=", close);
            if (le != std::string::npos)
                limit = static_cast<int>(g_ascii_strtoll(line.c_str() + le + 2, nullptr, 10));
            const bool m = set.find("managed") != std::string::npos;
            const bool a = (" " + set + ",").find(" AP,") != std::string::npos ||
                           (" " + set + " ").find(" AP ") != std::string::npos;
            if (m && a) {
                if (limit >= 2) {
                    has_managed = true;
                    has_ap = true;
                }
            } else {
                has_managed |= m;
                has_ap |= a;
            }
            pos = close + 1;
        }
        int total = 1;
        if (const auto t = line.find("total <="); t != std::string::npos)
            total = static_cast<int>(g_ascii_strtoll(line.c_str() + t + 8, nullptr, 10));
        int ch = 0;
        if (const auto c = line.find("#channels <="); c != std::string::npos)
            ch = static_cast<int>(g_ascii_strtoll(line.c_str() + c + 12, nullptr, 10));
        if (has_managed && has_ap && total >= 2) {
            supported = true;
            channels = std::max(channels, ch);
        }
    }
    h->iw = supported ? IwSupport::Yes : IwSupport::No;
    h->iw_channels = channels;
    update_concurrent_row(h);
}

void detect_concurrency(Hotspot* h) {
    gchar* iw = g_find_program_in_path("iw");
    if (iw == nullptr) {
        h->iw = IwSupport::Missing;
        update_concurrent_row(h);
        return;
    }
    g_free(iw);
    if (h->parent.empty()) {
        h->iw = IwSupport::No;
        update_concurrent_row(h);
        return;
    }
    gchar* phy = nullptr;
    const std::string phy_path = "/sys/class/net/" + h->parent + "/phy80211/name";
    if (!g_file_get_contents(phy_path.c_str(), &phy, nullptr, nullptr)) {
        h->iw = IwSupport::No;
        update_concurrent_row(h);
        return;
    }
    const std::string phy_name = trim(phy);
    g_free(phy);
    run_command({"iw", "phy", phy_name, "info"},
            guarded(h, [h](bool ok, int, const std::string& out, const std::string&) {
                if (ok)
                    parse_iw_combinations(h, out);
                else
                    h->iw = IwSupport::No;
                update_concurrent_row(h);
            }));
}

// ---- writing the profile -------------------------------------------------

std::vector<std::string> profile_properties(Hotspot* h) {
    const std::string iface = h->use_virtual ? virtual_name(h->parent) : h->parent;
    std::vector<std::string> props = {
        "connection.interface-name", iface,
        "connection.autoconnect", adw_switch_row_get_active(h->autostart) ? "yes" : "no",
        "802-11-wireless.ssid", entry_text(GTK_WIDGET(h->ssid)),
        "802-11-wireless.band", kBandKeys[std::min<guint>(adw_combo_row_get_selected(h->band), 2)],
        "802-11-wireless.hidden", adw_switch_row_get_active(h->hidden) ? "yes" : "no",
        // a virtual AP interface must not share the parent's MAC
        "802-11-wireless.cloned-mac-address", h->use_virtual ? "stable" : "",
    };
    return props;
}

void apply_security(Hotspot* h, std::function<void(bool, const std::string&)> done) {
    const guint security = adw_combo_row_get_selected(h->security);
    std::vector<std::string> argv = {"nmcli", "connection", "modify", kProfile};
    if (security == 0) {
        argv.push_back("remove");
        argv.push_back("802-11-wireless-security");
    } else {
        const std::string psk = entry_text(GTK_WIDGET(h->password));
        argv.insert(argv.end(), {"802-11-wireless-security.key-mgmt", kSecurityKeys[security],
                                 "802-11-wireless-security.proto", "rsn",
                                 "802-11-wireless-security.pairwise", "ccmp",
                                 "802-11-wireless-security.group", "ccmp",
                                 "802-11-wireless-security.psk", psk,
                                 // WPA3 mandates protected management frames
                                 "802-11-wireless-security.pmf", security == 2 ? "3" : "0"});
    }
    run_command(argv, guarded(h, [done](bool ok, int, const std::string&, const std::string& err) {
                done(ok, first_line(err));
            }));
}

// Creates or updates the profile from the widgets, then `done(ok, error)`.
void apply_profile(Hotspot* h, std::function<void(bool, const std::string&)> done) {
    const std::string ssid = entry_text(GTK_WIDGET(h->ssid));
    const std::string password = entry_text(GTK_WIDGET(h->password));
    if (!ssid_valid(ssid) || (!security_open(h) && !password_valid(password))) {
        done(false, "");
        return;
    }
    std::vector<std::string> argv;
    if (h->exists) {
        argv = {"nmcli", "connection", "modify", kProfile};
    } else {
        argv = {"nmcli", "connection", "add", "type", "wifi", "con-name", kProfile,
                "ssid", ssid, "802-11-wireless.mode", "ap", "ipv4.method", "shared",
                "ipv6.method", "ignore",
                // owned by this user: secrets read back without a polkit prompt
                "connection.permissions", std::string("user:") + g_get_user_name()};
    }
    const auto props = profile_properties(h);
    argv.insert(argv.end(), props.begin(), props.end());
    run_command(argv, guarded(h, [h, done](bool ok, int, const std::string&, const std::string& err) {
                if (!ok) {
                    done(false, first_line(err));
                    return;
                }
                h->exists = true;
                apply_security(h, done);
            }));
}

void restart_if_active(Hotspot* h) {
    if (!h->active || h->busy)
        return;
    h->busy = true;
    update_enable_row(h);
    render_status(h);
    run_command({"nmcli", "connection", "up", kProfile},
            guarded(h, [h](bool ok, int, const std::string&, const std::string& err) {
                h->busy = false;
                h->error = ok ? "" : "Could not restart the hotspot: " + first_line(err);
                update_enable_row(h);
                read_profile(h);
            }));
}

gboolean on_save_timeout(gpointer data) {
    Hotspot* h = static_cast<Hotspot*>(data);
    h->save_source = 0;
    apply_profile(h, [h](bool ok, const std::string& err) {
        if (!ok && !err.empty()) {
            h->error = "Could not save: " + err;
            render_status(h);
            return;
        }
        if (ok) {
            h->error.clear();
            render_status(h);
            restart_if_active(h); // NM applies AP changes only on (re)activation
        }
    });
    return G_SOURCE_REMOVE;
}

void schedule_save(Hotspot* h) {
    if (h->loading)
        return;
    update_enable_row(h);
    if (h->save_source != 0)
        g_source_remove(h->save_source);
    h->save_source = g_timeout_add(kSaveDebounceMs, on_save_timeout, h);
}

// ---- enable / disable ----------------------------------------------------

void bring_up(Hotspot* h) {
    run_command({"nmcli", "connection", "up", kProfile},
            guarded(h, [h](bool ok, int, const std::string&, const std::string& err) {
                h->busy = false;
                if (!ok) {
                    h->error = "Could not start the hotspot: " + first_line(err);
                    LoadingScope scope(h);
                    adw_switch_row_set_active(h->enabled, FALSE);
                } else {
                    h->error.clear();
                }
                update_enable_row(h);
                read_profile(h);
            }));
}

struct VirtualWait {
    Hotspot* h;
    std::string iface;
    int tries = 0;
};

gboolean wait_for_virtual(gpointer data) {
    auto* w = static_cast<VirtualWait*>(data);
    Hotspot* h = w->h;
    if (!*h->alive) {
        delete w;
        return G_SOURCE_REMOVE;
    }
    const std::string path = "/sys/class/net/" + w->iface;
    if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS) || ++w->tries > kVirtualWaitTries) {
        // NetworkManager needs a moment to notice and adopt the new device
        g_timeout_add(
            1200,
            +[](gpointer data) {
                Hotspot* h = static_cast<Hotspot*>(data);
                if (*h->alive)
                    run_command({"nmcli", "device", "set", virtual_name(h->parent), "managed", "yes"},
                            guarded(h, [h](bool, int, const std::string&, const std::string&) {
                                bring_up(h);
                            }));
                return G_SOURCE_REMOVE;
            },
            h);
        delete w;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

// Concurrent mode: the virtual AP interface has to exist before NM can
// activate on it. Creating one needs CAP_NET_ADMIN, hence pkexec (the
// polkit agent shows the prompt).
void ensure_virtual_then_up(Hotspot* h) {
    const std::string iface = virtual_name(h->parent);
    const std::string path = "/sys/class/net/" + iface;
    if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) {
        bring_up(h);
        return;
    }
    run_command({"pkexec", "iw", "dev", h->parent, "interface", "add", iface, "type", "__ap"},
            guarded(h, [h, iface](bool ok, int status, const std::string&,
                                   const std::string& err) {
                if (!ok) {
                    h->busy = false;
                    h->error = status == 126 || status == 127
                                   ? "Creating the virtual interface was not authorised"
                                   : "Could not create the virtual interface: " + first_line(err);
                    {
                        LoadingScope scope(h);
                        adw_switch_row_set_active(h->enabled, FALSE);
                    }
                    update_enable_row(h);
                    render_status(h);
                    return;
                }
                auto* wait = new VirtualWait{h, iface};
                g_timeout_add(200, wait_for_virtual, wait);
            }));
}

void on_enabled_toggled(GObject*, GParamSpec*, gpointer data) {
    Hotspot* h = static_cast<Hotspot*>(data);
    if (h->loading || h->busy)
        return;
    const bool want = adw_switch_row_get_active(h->enabled);
    h->busy = true;
    h->error.clear();
    update_enable_row(h);
    render_status(h);
    if (h->save_source != 0) { // flush pending edits first
        g_source_remove(h->save_source);
        h->save_source = 0;
    }
    if (!want) {
        run_command({"nmcli", "connection", "down", kProfile},
                guarded(h, [h](bool ok, int, const std::string&, const std::string& err) {
                    h->busy = false;
                    if (!ok)
                        h->error = "Could not stop the hotspot: " + first_line(err);
                    h->active = false;
                    update_enable_row(h);
                    read_profile(h);
                }));
        return;
    }
    apply_profile(h, [h](bool ok, const std::string& err) {
        if (!ok) {
            h->busy = false;
            h->error = err.empty() ? "Check the network name and password" : "Could not save: " + err;
            {
                LoadingScope scope(h);
                adw_switch_row_set_active(h->enabled, FALSE);
            }
            update_enable_row(h);
            render_status(h);
            return;
        }
        if (h->use_virtual)
            ensure_virtual_then_up(h);
        else
            bring_up(h);
    });
}

// ---- widget handlers -----------------------------------------------------

void on_text_changed(GtkEditable*, gpointer data) {
    schedule_save(static_cast<Hotspot*>(data));
}

void on_security_changed(GObject*, GParamSpec*, gpointer data) {
    Hotspot* h = static_cast<Hotspot*>(data);
    gtk_widget_set_visible(GTK_WIDGET(h->password), !security_open(h));
    if (!security_open(h) && entry_text(GTK_WIDGET(h->password)).empty()) {
        LoadingScope scope(h);
        gtk_editable_set_text(GTK_EDITABLE(h->password), generate_password().c_str());
    }
    schedule_save(h);
}

void on_simple_changed(GObject*, GParamSpec*, gpointer data) {
    schedule_save(static_cast<Hotspot*>(data));
}

void on_adapter_changed(GObject*, GParamSpec*, gpointer data) {
    Hotspot* h = static_cast<Hotspot*>(data);
    if (h->loading)
        return;
    const guint index = adw_combo_row_get_selected(h->adapter);
    if (index < h->wifi_devices.size())
        h->parent = h->wifi_devices[index];
    h->iw = IwSupport::Unknown;
    update_concurrent_row(h);
    detect_concurrency(h);
    schedule_save(h);
}

void on_concurrent_toggled(GObject*, GParamSpec*, gpointer data) {
    Hotspot* h = static_cast<Hotspot*>(data);
    if (h->loading)
        return;
    h->use_virtual = adw_switch_row_get_active(h->concurrent);
    schedule_save(h);
}

void on_generate_clicked(GtkButton*, gpointer data) {
    Hotspot* h = static_cast<Hotspot*>(data);
    gtk_editable_set_text(GTK_EDITABLE(h->password), generate_password().c_str());
}

gboolean on_poll(gpointer data) {
    Hotspot* h = static_cast<Hotspot*>(data);
    if (!h->busy)
        read_profile(h);
    return G_SOURCE_CONTINUE;
}

void on_map(GtkWidget*, gpointer data) {
    Hotspot* h = static_cast<Hotspot*>(data);
    h->page_mapped = true;
    refresh_devices(h, [h] {
        if (h->iw == IwSupport::Unknown)
            detect_concurrency(h);
        read_profile(h);
    });
    if (h->poll_source == 0)
        h->poll_source = g_timeout_add(kPollMs, on_poll, h);
}

void on_unmap(GtkWidget*, gpointer data) {
    Hotspot* h = static_cast<Hotspot*>(data);
    h->page_mapped = false;
    if (h->poll_source != 0) {
        g_source_remove(h->poll_source);
        h->poll_source = 0;
    }
}

AdwComboRow* combo_row(GtkWidget* group, const char* title, const char* subtitle,
                       const std::vector<const char*>& items) {
    GtkWidget* row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle != nullptr)
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    GtkStringList* model = gtk_string_list_new(nullptr);
    for (const char* item : items)
        gtk_string_list_append(model, item);
    adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(model));
    g_object_unref(model);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
    return ADW_COMBO_ROW(row);
}

AdwSwitchRow* switch_row(GtkWidget* group, const char* title, const char* subtitle) {
    GtkWidget* row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle != nullptr)
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
    return ADW_SWITCH_ROW(row);
}

} // namespace

GtkWidget* build_hotspot_page(GtkWindow*) {
    auto* h = new Hotspot;
    h->page = adw_preferences_page_new();
    g_object_set_data_full(G_OBJECT(h->page), "hotspot-state", h, [](gpointer p) {
        auto* h = static_cast<Hotspot*>(p);
        *h->alive = false;
        if (h->save_source != 0)
            g_source_remove(h->save_source);
        if (h->poll_source != 0)
            g_source_remove(h->poll_source);
        delete h;
    });

    // -- Hotspot: the switch + live status --------------------------------
    GtkWidget* main_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(main_group), "Wi-Fi hotspot");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(main_group),
        "Turn this computer into a Wi-Fi access point that shares its internet connection. "
        "Managed through NetworkManager; the hotspot keeps running after this window closes.");
    h->enabled = switch_row(main_group, "Wi-Fi hotspot", "");
    h->status_row = adw_action_row_new();
    gtk_widget_add_css_class(h->status_row, "property");
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(h->status_row), "Status");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(h->status_row), "Off");
    adw_action_row_set_subtitle_selectable(ADW_ACTION_ROW(h->status_row), TRUE);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(main_group), h->status_row);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(h->page), ADW_PREFERENCES_GROUP(main_group));

    // -- Network ----------------------------------------------------------
    GtkWidget* net_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(net_group), "Network");
    adw_preferences_group_set_description(
        ADW_PREFERENCES_GROUP(net_group),
        "Changes are saved as you type and applied the next time the hotspot starts; "
        "a running hotspot restarts briefly.");
    h->ssid = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(h->ssid), "Network name");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(net_group), GTK_WIDGET(h->ssid));
    h->security = combo_row(net_group, "Security",
                            "WPA3 is the most secure; use WPA2 if older devices cannot join",
                            {"None (open network)", "WPA2 Personal", "WPA3 Personal"});
    h->password = ADW_PASSWORD_ENTRY_ROW(adw_password_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(h->password), "Password");
    GtkWidget* generate = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_add_css_class(generate, "flat");
    gtk_widget_set_valign(generate, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(generate, "Generate a random password");
    g_signal_connect(generate, "clicked", G_CALLBACK(on_generate_clicked), h);
    adw_entry_row_add_suffix(ADW_ENTRY_ROW(h->password), generate);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(net_group), GTK_WIDGET(h->password));
    h->band = combo_row(net_group, "Band",
                        "5 GHz is faster nearby, 2.4 GHz reaches further",
                        {"Automatic", "2.4 GHz", "5 GHz"});
    h->hidden = switch_row(net_group, "Hidden network",
                           "Do not broadcast the network name; devices must enter it manually");
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(h->page), ADW_PREFERENCES_GROUP(net_group));

    // -- Adapter ----------------------------------------------------------
    GtkWidget* adapter_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(adapter_group), "Adapter");
    h->adapter = combo_row(adapter_group, "Wi-Fi adapter", nullptr, {});
    h->adapter_row = GTK_WIDGET(h->adapter);
    gtk_widget_set_visible(h->adapter_row, FALSE);
    h->concurrent = switch_row(adapter_group, "Keep Wi-Fi connected", "");
    h->autostart = switch_row(adapter_group, "Start automatically",
                              "Bring the hotspot up when you log in");
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(h->page),
                             ADW_PREFERENCES_GROUP(adapter_group));

    update_concurrent_row(h);
    populate_defaults(h);

    g_signal_connect(h->enabled, "notify::active", G_CALLBACK(on_enabled_toggled), h);
    g_signal_connect(h->ssid, "changed", G_CALLBACK(on_text_changed), h);
    g_signal_connect(h->password, "changed", G_CALLBACK(on_text_changed), h);
    g_signal_connect(h->security, "notify::selected", G_CALLBACK(on_security_changed), h);
    g_signal_connect(h->band, "notify::selected", G_CALLBACK(on_simple_changed), h);
    g_signal_connect(h->hidden, "notify::active", G_CALLBACK(on_simple_changed), h);
    g_signal_connect(h->autostart, "notify::active", G_CALLBACK(on_simple_changed), h);
    g_signal_connect(h->adapter, "notify::selected", G_CALLBACK(on_adapter_changed), h);
    g_signal_connect(h->concurrent, "notify::active", G_CALLBACK(on_concurrent_toggled), h);
    g_signal_connect(h->page, "map", G_CALLBACK(on_map), h);
    g_signal_connect(h->page, "unmap", G_CALLBACK(on_unmap), h);

    // dev hook: HS_HOTSPOT_SAVE=1 writes the shown settings to the profile
    // 2 s after startup (creates "Hotspot" from the defaults; never activates)
    if (g_getenv("HS_HOTSPOT_SAVE") != nullptr)
        g_timeout_add(
            2000,
            +[](gpointer data) {
                Hotspot* h = static_cast<Hotspot*>(data);
                if (*h->alive)
                    schedule_save(h);
                return G_SOURCE_REMOVE;
            },
            h);

    return h->page;
}

} // namespace hyprshell::settings
