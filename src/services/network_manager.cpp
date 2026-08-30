#include "services/network_manager.hpp"

namespace hyprshell {

namespace {

constexpr const char* kBusName = "org.freedesktop.NetworkManager";

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

} // namespace hyprshell
