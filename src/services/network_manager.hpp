#pragma once

#include <giomm.h>
#include <sigc++/sigc++.h>

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace hyprshell {

// Primary-connection state from NetworkManager (system DBus, no libnm).
// Resolves NM root -> active connection -> access point to get wifi strength,
// and re-resolves the whole chain whenever NM reports a change.
// Wifi management (scan / connect / disconnect) shells out to nmcli
// asynchronously, like Noctalia — NM's raw DBus connect flow (settings
// profile matching, secrets agent) is not worth reimplementing.
class NetworkManager {
public:
    enum class Kind { none, wifi, ethernet };

    struct WifiNetwork {
        std::string ssid;
        std::string security; // "" = open, e.g. "WPA2"
        int signal = 0;       // 0..100
        bool in_use = false;
        bool saved = false; // an NM profile with this name exists
    };

    // org.freedesktop.NetworkManager connectivity states
    enum class Connectivity : std::uint32_t {
        unknown = 0,
        none = 1,
        portal = 2,
        limited = 3,
        full = 4,
    };

    static NetworkManager& get();

    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    bool available() const { return available_; }
    Kind kind() const { return kind_; }
    // Any active ethernet connection (Noctalia: ethernet wins the bar icon
    // even while wifi is up).
    bool ethernet_connected() const { return ethernet_connected_; }
    bool wifi_enabled() const { return wifi_enabled_; }
    int strength() const { return strength_; } // 0..100, wifi only
    Connectivity connectivity() const { return connectivity_; }
    const std::string& ssid() const { return ssid_; }                   // wifi only
    unsigned max_bitrate() const { return max_bitrate_; }               // kb/s, wifi only
    const std::string& connection_id() const { return connection_id_; } // NM profile name

    sigc::signal<void()>& signal_changed() { return changed_; }

    // -- wifi management (nmcli) --------------------------------------------
    const std::vector<WifiNetwork>& networks() const { return networks_; }
    bool scanning() const { return scanning_; }
    void scan(); // refresh networks(); emits signal_networks_changed when done
    // Saved profiles activate directly; new ones take an optional password.
    void wifi_connect(const std::string& ssid, const std::string& password = "");
    void wifi_disconnect(const std::string& ssid);
    void set_wifi_enabled(bool enabled); // WirelessEnabled via DBus

    sigc::signal<void()>& signal_networks_changed() { return networks_changed_; }
    // (ok, trimmed nmcli output) after a connect/disconnect finishes
    sigc::signal<void(bool, const std::string&)>& signal_action_done() {
        return action_done_;
    }

private:
    NetworkManager();

    void read_root_properties();
    void resolve_active_connection(const std::string& path);
    void resolve_access_point(const std::string& path, std::uint64_t serial);
    void check_ethernet(std::uint64_t serial);
    void run_nmcli(const std::vector<std::string>& argv,
                   std::function<void(bool, std::string)> on_done);
    void notify();

    Glib::RefPtr<Gio::DBus::Proxy> root_proxy_;
    Glib::RefPtr<Gio::DBus::Proxy> ap_proxy_;
    std::uint64_t resolve_serial_ = 0;

    bool available_ = false;
    Kind kind_ = Kind::none;
    bool wifi_enabled_ = true;
    int strength_ = 0;
    Connectivity connectivity_ = Connectivity::unknown;
    std::string ssid_;
    unsigned max_bitrate_ = 0;
    std::string connection_id_;
    bool ethernet_connected_ = false;
    std::vector<WifiNetwork> networks_;
    std::set<std::string> saved_names_;
    bool scanning_ = false;
    sigc::signal<void()> changed_;
    sigc::signal<void()> networks_changed_;
    sigc::signal<void(bool, const std::string&)> action_done_;
};

} // namespace hyprshell
