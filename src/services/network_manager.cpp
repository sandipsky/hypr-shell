#include "services/network_manager.hpp"

#include <algorithm>
#include <map>
#include <sstream>

namespace hyprshell {

namespace {

constexpr const char* kBusName = "org.freedesktop.NetworkManager";

// nmcli -t fields are ':'-separated with literal ':' escaped as "\:"
std::vector<std::string> split_terse(const std::string& line) {
    std::vector<std::string> out(1);
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '\\' && i + 1 < line.size())
            out.back() += line[++i];
        else if (c == ':')
            out.emplace_back();
        else
            out.back() += c;
    }
    return out;
}

template <typename T>
bool get_cached(const Glib::RefPtr<Gio::DBus::Proxy>& proxy, const char* name, T& out) {
    Glib::VariantBase value;
    proxy->get_cached_property(value, name);
    if (value.gobj() == nullptr) {
        return false;
    }
    out = Glib::VariantBase::cast_dynamic<Glib::Variant<T>>(value).get();
    return true;
}

} // namespace

NetworkManager& NetworkManager::get() {
    static NetworkManager instance;
    return instance;
}

NetworkManager::NetworkManager() {
    Gio::DBus::Proxy::create_for_bus(
        Gio::DBus::BusType::SYSTEM, kBusName, "/org/freedesktop/NetworkManager", kBusName,
        [this](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                root_proxy_ = Gio::DBus::Proxy::create_for_bus_finish(result);
            } catch (const Glib::Error& e) {
                g_debug("NetworkManager unavailable: %s", e.what());
                return;
            }
            available_ = true;
            root_proxy_->signal_properties_changed().connect(
                [this](const Gio::DBus::Proxy::MapChangedProperties&,
                       const std::vector<Glib::ustring>&) { read_root_properties(); });
            read_root_properties();
        });
}

void NetworkManager::read_root_properties() {
    bool wireless = true;
    if (get_cached(root_proxy_, "WirelessEnabled", wireless)) {
        wifi_enabled_ = wireless;
    }
    // hold the optimistic radio target until NM confirms it (see header)
    if (pending_wireless_ >= 0) {
        if (wifi_enabled_ == (pending_wireless_ == 1)) {
            pending_wireless_ = -1;
            pending_wireless_timer_.disconnect();
        } else {
            wifi_enabled_ = pending_wireless_ == 1;
        }
    }
    std::uint32_t connectivity = 0;
    if (get_cached(root_proxy_, "Connectivity", connectivity)) {
        connectivity_ = static_cast<Connectivity>(connectivity);
    }

    Glib::ustring type;
    get_cached(root_proxy_, "PrimaryConnectionType", type);
    Glib::VariantBase primary_value;
    root_proxy_->get_cached_property(primary_value, "PrimaryConnection");
    std::string primary;
    if (primary_value.gobj() != nullptr) {
        primary = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(primary_value).get();
    }

    ++resolve_serial_; // invalidates in-flight lookups
    ap_proxy_.reset();
    strength_ = 0;
    ssid_.clear();
    max_bitrate_ = 0;
    connection_id_.clear();

    check_ethernet(resolve_serial_);

    if (primary.empty() || primary == "/") {
        kind_ = Kind::none;
        notify();
        return;
    }
    // treat every non-wifi primary connection as wired
    kind_ = type == "802-11-wireless" ? Kind::wifi : Kind::ethernet;
    resolve_active_connection(primary);
    notify();
}

// Is ANY active connection ethernet? (the primary may still be wifi)
void NetworkManager::check_ethernet(std::uint64_t serial) {
    std::vector<Glib::DBusObjectPathString> paths;
    if (!get_cached(root_proxy_, "ActiveConnections", paths))
        return;
    if (paths.empty()) {
        if (ethernet_connected_) {
            ethernet_connected_ = false;
            notify();
        }
        return;
    }
    auto pending = std::make_shared<std::size_t>(paths.size());
    auto found = std::make_shared<bool>(false);
    for (const auto& path : paths) {
        Gio::DBus::Proxy::create_for_bus(
            Gio::DBus::BusType::SYSTEM, kBusName, path,
            "org.freedesktop.NetworkManager.Connection.Active",
            [this, serial, pending, found](Glib::RefPtr<Gio::AsyncResult>& result) {
                try {
                    auto active = Gio::DBus::Proxy::create_for_bus_finish(result);
                    Glib::ustring type;
                    std::uint32_t state = 0;
                    get_cached(active, "Type", type);
                    get_cached(active, "State", state);
                    if (type == "802-3-ethernet" && state == 2) // ACTIVATED
                        *found = true;
                } catch (const Glib::Error&) {
                    // connection went away mid-lookup
                }
                if (--*pending == 0 && serial == resolve_serial_ &&
                    ethernet_connected_ != *found) {
                    ethernet_connected_ = *found;
                    notify();
                }
            });
    }
}

void NetworkManager::resolve_active_connection(const std::string& path) {
    auto serial = resolve_serial_;
    Gio::DBus::Proxy::create_for_bus(
        Gio::DBus::BusType::SYSTEM, kBusName, path,
        "org.freedesktop.NetworkManager.Connection.Active",
        [this, serial](Glib::RefPtr<Gio::AsyncResult>& result) {
            if (serial != resolve_serial_) {
                return;
            }
            try {
                auto active = Gio::DBus::Proxy::create_for_bus_finish(result);
                Glib::ustring id;
                if (get_cached(active, "Id", id)) {
                    connection_id_ = id;
                    notify();
                }
                Glib::VariantBase value;
                active->get_cached_property(value, "SpecificObject");
                if (value.gobj() == nullptr) {
                    return;
                }
                auto ap = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(value).get();
                if (kind_ == Kind::wifi && !ap.empty() && ap != "/") {
                    resolve_access_point(ap, serial);
                }
            } catch (const Glib::Error& e) {
                g_debug("NM active connection lookup failed: %s", e.what());
            }
        });
}

void NetworkManager::resolve_access_point(const std::string& path, std::uint64_t serial) {
    Gio::DBus::Proxy::create_for_bus(
        Gio::DBus::BusType::SYSTEM, kBusName, path, "org.freedesktop.NetworkManager.AccessPoint",
        [this, serial](Glib::RefPtr<Gio::AsyncResult>& result) {
            if (serial != resolve_serial_) {
                return;
            }
            try {
                ap_proxy_ = Gio::DBus::Proxy::create_for_bus_finish(result);
            } catch (const Glib::Error& e) {
                g_debug("NM access point lookup failed: %s", e.what());
                return;
            }
            auto read_details = [this] {
                guchar strength = 0;
                if (get_cached(ap_proxy_, "Strength", strength)) {
                    strength_ = strength;
                }
                std::vector<guint8> ssid;
                if (get_cached(ap_proxy_, "Ssid", ssid)) {
                    ssid_.assign(ssid.begin(), ssid.end());
                }
                guint32 bitrate = 0;
                if (get_cached(ap_proxy_, "MaxBitrate", bitrate)) { // kb/s
                    max_bitrate_ = bitrate;
                }
                notify();
            };
            ap_proxy_->signal_properties_changed().connect(
                [this, serial, read_details](const Gio::DBus::Proxy::MapChangedProperties&,
                                             const std::vector<Glib::ustring>&) {
                    if (serial == resolve_serial_) {
                        read_details();
                    }
                });
            read_details();
        });
}

void NetworkManager::notify() {
    changed_.emit();
}

// -- wifi management (nmcli) --------------------------------------------------

void NetworkManager::run_nmcli(const std::vector<std::string>& argv,
                               std::function<void(bool, std::string)> on_done) {
    try {
        auto proc = Gio::Subprocess::create(
            argv, Gio::Subprocess::Flags::STDOUT_PIPE | Gio::Subprocess::Flags::STDERR_MERGE);
        proc->communicate_utf8_async(
            "",
            [proc, on_done](Glib::RefPtr<Gio::AsyncResult>& result) {
                Glib::ustring out;
                try {
                    out = proc->communicate_utf8_finish(result).first;
                } catch (const Glib::Error& e) {
                    on_done(false, e.what());
                    return;
                }
                std::string trimmed = out.raw();
                while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == ' '))
                    trimmed.pop_back();
                on_done(proc->get_successful(), trimmed);
            },
            {});
    } catch (const Glib::Error& e) {
        on_done(false, e.what());
    }
}

void NetworkManager::scan() {
    if (scanning_)
        return;
    scanning_ = true;
    networks_changed_.emit();
    // saved profile names first, then the (slow) rescan list
    run_nmcli({"nmcli", "-t", "-f", "NAME", "connection", "show"},
              [this](bool ok, std::string out) {
                  if (ok) {
                      saved_names_.clear();
                      std::istringstream lines(out);
                      for (std::string line; std::getline(lines, line);)
                          if (!line.empty())
                              saved_names_.insert(split_terse(line)[0]);
                  }
                  run_nmcli({"nmcli", "-t", "-f", "SSID,SECURITY,SIGNAL,IN-USE", "device",
                             "wifi", "list", "--rescan", "yes"},
                            [this](bool list_ok, std::string list_out) {
                                scanning_ = false;
                                if (!list_ok) {
                                    networks_changed_.emit();
                                    return;
                                }
                                // strongest entry per SSID (nmcli lists every BSS)
                                std::map<std::string, WifiNetwork> best;
                                std::istringstream lines(list_out);
                                for (std::string line; std::getline(lines, line);) {
                                    const auto f = split_terse(line);
                                    if (f.size() < 4 || f[0].empty())
                                        continue;
                                    WifiNetwork net;
                                    net.ssid = f[0];
                                    net.security = f[1];
                                    net.signal = std::atoi(f[2].c_str());
                                    net.in_use = f[3] == "*";
                                    net.saved = saved_names_.count(net.ssid) > 0;
                                    auto it = best.find(net.ssid);
                                    if (it == best.end() || net.in_use ||
                                        (!it->second.in_use && net.signal > it->second.signal))
                                        best[net.ssid] = net;
                                }
                                networks_.clear();
                                for (auto& [ssid, net] : best)
                                    networks_.push_back(std::move(net));
                                std::sort(networks_.begin(), networks_.end(),
                                          [](const WifiNetwork& a, const WifiNetwork& b) {
                                              if (a.in_use != b.in_use)
                                                  return a.in_use;
                                              return a.signal > b.signal;
                                          });
                                networks_changed_.emit();
                            });
              });
}

void NetworkManager::wifi_connect(const std::string& ssid, const std::string& password) {
    std::vector<std::string> argv;
    const bool saved = saved_names_.count(ssid) > 0;
    if (saved && password.empty())
        argv = {"nmcli", "connection", "up", "id", ssid};
    else {
        argv = {"nmcli", "device", "wifi", "connect", ssid};
        if (!password.empty()) {
            argv.emplace_back("password");
            argv.emplace_back(password);
        }
    }
    run_nmcli(argv, [this](bool ok, std::string out) {
        // nmcli exits 0 with an "Error:" body on some failures — check the text
        if (ok && out.find("Error") != std::string::npos)
            ok = false;
        action_done_.emit(ok, out);
        scan();
    });
}

void NetworkManager::wifi_disconnect(const std::string& ssid) {
    run_nmcli({"nmcli", "connection", "down", "id", ssid},
              [this](bool ok, std::string out) {
                  action_done_.emit(ok, out);
                  scan();
              });
}

void NetworkManager::set_wifi_enabled(bool enabled) {
    if (!available_ || !root_proxy_)
        return;
    wifi_enabled_ = enabled; // optimistic — PropertiesChanged confirms
    pending_wireless_ = enabled ? 1 : 0;
    pending_wireless_timer_.disconnect();
    pending_wireless_timer_ = Glib::signal_timeout().connect(
        [this] {
            pending_wireless_ = -1; // never confirmed — re-sync with reality
            read_root_properties();
            return false;
        },
        4000);
    notify();
    auto conn = root_proxy_->get_connection();
    conn->call(
        "/org/freedesktop/NetworkManager", "org.freedesktop.DBus.Properties", "Set",
        Glib::Variant<std::tuple<Glib::ustring, Glib::ustring, Glib::VariantBase>>::create(
            {kBusName, "WirelessEnabled", Glib::Variant<bool>::create(enabled)}),
        [this, conn](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                conn->call_finish(result);
            } catch (const Glib::Error& e) {
                g_warning("failed to toggle wifi: %s", e.what());
                pending_wireless_ = -1; // revert the optimistic state
                pending_wireless_timer_.disconnect();
                read_root_properties();
            }
        },
        kBusName);
}

} // namespace hyprshell
