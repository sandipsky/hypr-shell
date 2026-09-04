#pragma once

#include <giomm.h>
#include <sigc++/sigc++.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace hyprshell {

// VPN profiles known to NetworkManager (Noctalia's VPNService, 1:1): the
// `vpn` and `wireguard` connections from `nmcli connection show`, active
// when bound to a device. Everything goes through nmcli like Noctalia —
// connect / disconnect / delete / import — with the same output checks.
// Refreshes on NetworkManager changes (1 s debounce) plus a 60 s fallback poll.
class VpnService {
public:
    struct Connection {
        std::string uuid;
        std::string name;
        std::string device; // "" when inactive
        bool active = false;
    };

    static VpnService& get();

    VpnService(const VpnService&) = delete;
    VpnService& operator=(const VpnService&) = delete;

    const std::map<std::string, Connection>& connections() const { return connections_; }
    std::vector<Connection> active_connections() const;
    std::vector<Connection> inactive_connections() const;
    bool has_active_connection() const;

    bool refreshing() const { return refreshing_; }
    bool connecting() const { return connecting_; }
    bool disconnecting() const { return disconnecting_; }
    bool removing() const { return removing_; }
    bool importing() const { return importing_; }
    bool busy() const { return connecting_ || disconnecting_ || removing_; }
    const std::string& connecting_uuid() const { return connecting_uuid_; }
    const std::string& disconnecting_uuid() const { return disconnecting_uuid_; }
    const std::string& removing_uuid() const { return removing_uuid_; }
    const std::string& last_error() const { return last_error_; }

    void refresh();
    void connect(const std::string& uuid);
    void disconnect(const std::string& uuid);
    void toggle(const std::string& uuid);
    void remove(const std::string& uuid);
    // WireGuard .conf (default) or OpenVPN .ovpn, by extension
    void import_config(const std::string& path);

    sigc::signal<void()>& signal_changed() { return changed_; }

private:
    VpnService();

    void schedule_refresh(unsigned ms);
    void run_nmcli(const std::vector<std::string>& argv,
                   std::function<void(bool ok, std::string out, std::string err)> on_done);
    void parse_connections(const std::string& out);

    std::map<std::string, Connection> connections_;
    bool refreshing_ = false, refresh_pending_ = false;
    bool connecting_ = false, disconnecting_ = false, removing_ = false, importing_ = false;
    std::string connecting_uuid_, disconnecting_uuid_, removing_uuid_;
    std::string last_error_;
    sigc::connection poll_timer_;
    sigc::connection delayed_refresh_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    sigc::signal<void()> changed_;
};

} // namespace hyprshell
