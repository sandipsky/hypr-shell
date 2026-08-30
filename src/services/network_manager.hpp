#pragma once

#include <giomm.h>
#include <sigc++/sigc++.h>

#include <cstdint>

namespace hyprshell {

// Primary-connection state from NetworkManager (system DBus, no libnm).
// Resolves NM root -> active connection -> access point to get wifi strength,
// and re-resolves the whole chain whenever NM reports a change.
class NetworkManager {
public:
    enum class Kind { none, wifi, ethernet };

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
    bool wifi_enabled() const { return wifi_enabled_; }
    int strength() const { return strength_; } // 0..100, wifi only
    Connectivity connectivity() const { return connectivity_; }
    const std::string& ssid() const { return ssid_; }                   // wifi only
    unsigned max_bitrate() const { return max_bitrate_; }               // kb/s, wifi only
    const std::string& connection_id() const { return connection_id_; } // NM profile name

    sigc::signal<void()>& signal_changed() { return changed_; }

private:
    NetworkManager();

    void read_root_properties();
    void resolve_active_connection(const std::string& path);
    void resolve_access_point(const std::string& path, std::uint64_t serial);
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
    sigc::signal<void()> changed_;
};

} // namespace hyprshell
