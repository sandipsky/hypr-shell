#include "services/vpn.hpp"

#include "services/network_manager.hpp"

#include <algorithm>
#include <cctype>

namespace hyprshell {

namespace {

std::string first_line(const std::string& text) {
    std::string s = text;
    while (!s.empty() && (s.back() == '\n' || s.back() == ' '))
        s.pop_back();
    const auto nl = s.find('\n');
    return nl == std::string::npos ? s : s.substr(0, nl);
}

std::string lowercase(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

VpnService& VpnService::get() {
    static VpnService instance;
    return instance;
}

VpnService::VpnService() {
    // slow fallback poll; real-time updates come from NetworkManager's
    // property changes (Noctalia listens to `nmcli monitor` for the same)
    poll_timer_ = Glib::signal_timeout().connect_seconds(
        [this] {
            refresh();
            return true;
        },
        60);
    NetworkManager::get().signal_changed().connect([this] { schedule_refresh(1000); });
    refresh();
}

std::vector<VpnService::Connection> VpnService::active_connections() const {
    std::vector<Connection> out;
    for (const auto& [uuid, conn] : connections_)
        if (conn.active)
            out.push_back(conn);
    return out;
}

std::vector<VpnService::Connection> VpnService::inactive_connections() const {
    std::vector<Connection> out;
    for (const auto& [uuid, conn] : connections_)
        if (!conn.active)
            out.push_back(conn);
    return out;
}

bool VpnService::has_active_connection() const {
    return std::any_of(connections_.begin(), connections_.end(),
                       [](const auto& kv) { return kv.second.active; });
}

void VpnService::schedule_refresh(unsigned ms) {
    delayed_refresh_.disconnect();
    delayed_refresh_ = Glib::signal_timeout().connect(
        [this] {
            refresh();
            return false;
        },
        ms);
}

void VpnService::run_nmcli(const std::vector<std::string>& argv,
                    std::function<void(bool, std::string, std::string)> on_done) {
    try {
        auto proc = Gio::Subprocess::create(
            argv, Gio::Subprocess::Flags::STDOUT_PIPE | Gio::Subprocess::Flags::STDERR_PIPE);
        proc->communicate_utf8_async(
            "",
            [proc, on_done, alive = alive_](Glib::RefPtr<Gio::AsyncResult>& result) {
                if (!*alive)
                    return;
                Glib::ustring out, err;
                try {
                    auto pair = proc->communicate_utf8_finish(result);
                    out = pair.first;
                    err = pair.second;
                } catch (const Glib::Error& e) {
                    on_done(false, "", e.what());
                    return;
                }
                on_done(proc->get_successful(), out.raw(), err.raw());
            },
            {});
    } catch (const Glib::Error& e) {
        on_done(false, "", e.what());
    }
}

// `nmcli -t -f NAME,UUID,TYPE,DEVICE connection show`, parsed from the right
// like Noctalia so names containing ':' survive
void VpnService::parse_connections(const std::string& out) {
    std::map<std::string, Connection> map;
    std::size_t pos = 0;
    while (pos < out.size()) {
        auto end = out.find('\n', pos);
        if (end == std::string::npos)
            end = out.size();
        std::string line = out.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.empty())
            continue;
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
        Connection conn;
        conn.uuid = rest2.substr(c3 + 1);
        conn.name = rest2.substr(0, c3);
        if (conn.uuid.empty() || conn.name.empty())
            continue;
        conn.device = (device == "--") ? "" : device;
        conn.active = !conn.device.empty();
        map[conn.uuid] = conn;
    }
    connections_ = std::move(map);
}

void VpnService::refresh() {
    if (refreshing_) {
        refresh_pending_ = true;
        return;
    }
    refreshing_ = true;
    last_error_.clear();
    changed_.emit();
    run_nmcli({"nmcli", "-t", "-f", "NAME,UUID,TYPE,DEVICE", "connection", "show"},
              [this](bool ok, std::string out, std::string err) {
                  if (ok || !out.empty())
                      parse_connections(out);
                  if (!ok && !first_line(err).empty()) {
                      last_error_ = first_line(err);
                      g_warning("vpn: refresh error: %s", last_error_.c_str());
                  }
                  const bool pending = refresh_pending_;
                  refreshing_ = false;
                  refresh_pending_ = false;
                  changed_.emit();
                  if (pending)
                      schedule_refresh(ok ? 200 : 2000);
              });
}

void VpnService::connect(const std::string& uuid) {
    if (connecting_ || uuid.empty())
        return;
    const auto it = connections_.find(uuid);
    if (it == connections_.end())
        return;
    const std::string name = it->second.name;
    connecting_ = true;
    connecting_uuid_ = uuid;
    last_error_.clear();
    changed_.emit();
    run_nmcli({"nmcli", "connection", "up", "uuid", uuid},
              [this, uuid, name](bool, std::string out, std::string err) {
                  // Noctalia trusts the success text, not the exit code
                  const bool success = out.find("successfully activated") != std::string::npos ||
                                       out.find("Connection successfully") != std::string::npos;
                  if (success) {
                      if (auto it = connections_.find(uuid); it != connections_.end())
                          it->second.active = true;
                      g_message("vpn: connected to %s", name.c_str());
                  } else if (!first_line(err).empty()) {
                      last_error_ = first_line(err);
                      g_warning("vpn: connect error: %s", last_error_.c_str());
                  }
                  connecting_ = false;
                  connecting_uuid_.clear();
                  changed_.emit();
                  if (success)
                      schedule_refresh(1000);
              });
}

void VpnService::disconnect(const std::string& uuid) {
    if (disconnecting_ || uuid.empty())
        return;
    const auto it = connections_.find(uuid);
    if (it == connections_.end())
        return;
    const std::string name = it->second.name;
    disconnecting_ = true;
    disconnecting_uuid_ = uuid;
    last_error_.clear();
    changed_.emit();
    run_nmcli({"nmcli", "connection", "down", "uuid", uuid},
              [this, uuid, name](bool ok, std::string, std::string err) {
                  if (ok) {
                      if (auto it = connections_.find(uuid); it != connections_.end()) {
                          it->second.active = false;
                          it->second.device.clear();
                      }
                      g_message("vpn: disconnected from %s", name.c_str());
                  } else if (!first_line(err).empty()) {
                      last_error_ = first_line(err);
                      g_warning("vpn: disconnect error: %s", last_error_.c_str());
                  }
                  disconnecting_ = false;
                  disconnecting_uuid_.clear();
                  changed_.emit();
                  if (ok)
                      schedule_refresh(1000);
              });
}

void VpnService::toggle(const std::string& uuid) {
    const auto it = connections_.find(uuid);
    if (it == connections_.end())
        return;
    if (it->second.active)
        disconnect(uuid);
    else
        connect(uuid);
}

void VpnService::remove(const std::string& uuid) {
    if (removing_ || uuid.empty())
        return;
    const auto it = connections_.find(uuid);
    if (it == connections_.end())
        return;
    const std::string name = it->second.name;
    removing_ = true;
    removing_uuid_ = uuid;
    last_error_.clear();
    changed_.emit();
    run_nmcli({"nmcli", "connection", "delete", "uuid", uuid},
              [this, uuid, name](bool, std::string out, std::string err) {
                  const bool success = out.find("successfully deleted") != std::string::npos;
                  if (success) {
                      connections_.erase(uuid);
                      g_message("vpn: removed %s", name.c_str());
                  } else if (!first_line(err).empty()) {
                      last_error_ = first_line(err);
                      g_warning("vpn: remove error: %s", last_error_.c_str());
                  }
                  removing_ = false;
                  removing_uuid_.clear();
                  changed_.emit();
                  if (success)
                      schedule_refresh(500);
              });
}

void VpnService::import_config(const std::string& path) {
    if (importing_ || path.empty())
        return;
    // NetworkManager needs the VPN type up-front; infer it from the extension
    const std::string lower = lowercase(path);
    const std::string type =
        lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".ovpn") == 0 ? "openvpn" : "wireguard";
    importing_ = true;
    last_error_.clear();
    changed_.emit();
    run_nmcli({"nmcli", "connection", "import", "type", type, "file", path},
              [this, path](bool, std::string out, std::string err) {
                  const bool success = out.find("successfully added") != std::string::npos;
                  if (success)
                      g_message("vpn: imported %s", path.c_str());
                  else if (!first_line(err).empty()) {
                      last_error_ = first_line(err);
                      g_warning("vpn: import error: %s", last_error_.c_str());
                  }
                  importing_ = false;
                  changed_.emit();
                  if (success)
                      schedule_refresh(500);
              });
}

} // namespace hyprshell
