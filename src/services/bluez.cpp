#include "services/bluez.hpp"

#include "services/config.hpp"

#include <algorithm>

namespace hyprshell {

namespace {

constexpr const char* kBusName = "org.bluez";
constexpr const char* kAdapterIface = "org.bluez.Adapter1";
constexpr const char* kDeviceIface = "org.bluez.Device1";
constexpr const char* kBatteryIface = "org.bluez.Battery1";
constexpr const char* kPropertiesIface = "org.freedesktop.DBus.Properties";

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

Glib::RefPtr<Gio::DBus::Proxy> iface_proxy(const Glib::RefPtr<Gio::DBus::Object>& object,
                                           const char* name) {
    return std::dynamic_pointer_cast<Gio::DBus::Proxy>(object->get_interface(name));
}

// Noctalia's sortDevices heuristic: a "real" name contains a space and is
// longer than 3 chars — MAC-ish placeholder names sink below those.
bool has_real_name(const std::string& name) {
    return name.find(' ') != std::string::npos && name.size() > 3;
}

// Noctalia's unnamed-device filter: BlueZ's Alias falls back to the MAC, so a
// name that normalizes to the address (alnum only) is no name at all.
std::string normalize(const std::string& text) {
    std::string out;
    for (const char c : text)
        if (g_ascii_isalnum(c))
            out += static_cast<char>(g_ascii_tolower(c));
    return out;
}

} // namespace

Bluez& Bluez::get() {
    static Bluez instance;
    return instance;
}

Bluez::Bluez() {
    Gio::DBus::ObjectManagerClient::create_for_bus(
        Gio::DBus::BusType::SYSTEM, kBusName, "/",
        [this](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                manager_ = Gio::DBus::ObjectManagerClient::create_for_bus_finish(result);
            } catch (const Glib::Error& e) {
                g_debug("BlueZ unavailable: %s", e.what());
                return;
            }
            manager_->signal_object_added().connect(
                [this](const Glib::RefPtr<Gio::DBus::Object>&) { rebuild(); });
            manager_->signal_object_removed().connect(
                [this](const Glib::RefPtr<Gio::DBus::Object>&) { rebuild(); });
            manager_->signal_interface_proxy_properties_changed().connect(
                [this](const Glib::RefPtr<Gio::DBus::ObjectProxy>&,
                       const Glib::RefPtr<Gio::DBus::Proxy>&,
                       const Gio::DBus::ObjectManagerClient::MapChangedProperties&,
                       const std::vector<Glib::ustring>&) { rebuild(); });
            // bluetoothd restarts synthesize object-removed/added pairs, but the
            // owner change itself also flips available_ when no adapter is left
            manager_->property_name_owner().signal_changed().connect(
                [this] { rebuild(); });
            rebuild();
        });

    // switching auto-connect ON acts immediately, like Noctalia's
    // onBluetoothAutoConnectChanged
    last_auto_connect_ = Config::get().bluetooth_auto_connect();
    Config::get().signal_changed().connect([this] {
        const bool on = Config::get().bluetooth_auto_connect();
        if (on && !last_auto_connect_ && enabled_)
            schedule_auto_connect();
        last_auto_connect_ = on;
    });
}

void Bluez::rebuild() {
    available_ = false;
    enabled_ = false;
    scanning_ = false;
    adapter_path_.clear();
    devices_.clear();
    if (manager_) {
        std::set<std::string> seen; // dedupe by address, first wins (Noctalia)
        for (const auto& object : manager_->get_objects()) {
            if (adapter_path_.empty()) {
                if (auto adapter = iface_proxy(object, kAdapterIface)) {
                    adapter_path_ = object->get_object_path();
                    available_ = true;
                    bool powered = false;
                    if (get_cached(adapter, "Powered", powered)) {
                        enabled_ = powered;
                    }
                    bool discovering = false;
                    if (get_cached(adapter, "Discovering", discovering)) {
                        scanning_ = discovering;
                    }
                }
            }
            auto proxy = iface_proxy(object, kDeviceIface);
            if (!proxy) {
                continue;
            }
            Device dev;
            dev.path = object->get_object_path();
            Glib::ustring text;
            if (get_cached(proxy, "Name", text)) {
                dev.name = text;
            } else if (get_cached(proxy, "Alias", text)) {
                dev.name = text;
            }
            if (get_cached(proxy, "Address", text)) {
                dev.address = text;
            }
            if (get_cached(proxy, "Icon", text)) {
                dev.icon = text;
            }
            get_cached(proxy, "Connected", dev.connected);
            get_cached(proxy, "Paired", dev.paired);
            get_cached(proxy, "Trusted", dev.trusted);
            get_cached(proxy, "Blocked", dev.blocked);
            if (auto battery = iface_proxy(object, kBatteryIface)) {
                guchar percent = 0;
                if (get_cached(battery, "Percentage", percent)) {
                    dev.battery = percent;
                }
            }
            dev.busy = busy_.count(dev.path) > 0;
            dev.named = !dev.name.empty() && normalize(dev.name) != normalize(dev.address);
            if (dev.name.empty()) {
                dev.name = dev.address;
            }
            if (!dev.address.empty() && !seen.insert(dev.address).second) {
                continue;
            }
            devices_.push_back(std::move(dev));
        }
    }
    // hold the optimistic power target until BlueZ confirms it (see header)
    if (pending_power_ >= 0) {
        if (enabled_ == (pending_power_ == 1)) {
            pending_power_ = -1;
            pending_power_timer_.disconnect();
        } else {
            enabled_ = pending_power_ == 1;
        }
    }
    std::sort(devices_.begin(), devices_.end(), [](const Device& a, const Device& b) {
        if (a.connected != b.connected)
            return a.connected;
        const bool ra = has_real_name(a.name);
        const bool rb = has_real_name(b.name);
        if (ra != rb)
            return ra;
        return a.name < b.name;
    });
    if (enabled_ && !prev_enabled_ && last_auto_connect_) {
        schedule_auto_connect(); // covers both power-on and shell startup
    }
    prev_enabled_ = enabled_;
    changed_.emit();
}

bool Bluez::any_connected() const {
    return std::any_of(devices_.begin(), devices_.end(),
                       [](const Device& d) { return d.connected && !d.blocked; });
}

Glib::RefPtr<Gio::DBus::Proxy> Bluez::device_proxy(const std::string& path) {
    if (!manager_) {
        return {};
    }
    auto object = manager_->get_object(path);
    return object ? iface_proxy(object, kDeviceIface) : Glib::RefPtr<Gio::DBus::Proxy>();
}

void Bluez::set_enabled(bool enabled) {
    if (!available_ || !manager_) {
        return;
    }
    enabled_ = enabled; // optimistic — Powered's PropertiesChanged confirms
    pending_power_ = enabled ? 1 : 0;
    pending_power_timer_.disconnect();
    pending_power_timer_ = Glib::signal_timeout().connect(
        [this] {
            // never confirmed (e.g. a hard rfkill block) — re-sync with reality
            pending_power_ = -1;
            rebuild();
            return false;
        },
        4000);
    changed_.emit();
    auto set_powered = [this, enabled, path = adapter_path_] {
        auto conn = manager_->get_connection();
        conn->call(
            path, kPropertiesIface, "Set",
            Glib::Variant<std::tuple<Glib::ustring, Glib::ustring, Glib::VariantBase>>::create(
                {kAdapterIface, "Powered", Glib::Variant<bool>::create(enabled)}),
            [this, conn](Glib::RefPtr<Gio::AsyncResult>& result) {
                try {
                    conn->call_finish(result);
                } catch (const Glib::Error& e) {
                    g_warning("failed to toggle bluetooth: %s", e.what());
                    pending_power_ = -1; // revert the optimistic state
                    pending_power_timer_.disconnect();
                    rebuild();
                }
            },
            kBusName);
    };
    if (!enabled) {
        set_powered();
        return;
    }
    // BlueZ can't power an rfkill-blocked adapter (e.g. a block persisted by
    // systemd-rfkill) — clear the block first, then power on (like Noctalia).
    try {
        auto proc = Gio::Subprocess::create({"rfkill", "unblock", "bluetooth"},
                                            Gio::Subprocess::Flags::STDERR_SILENCE);
        proc->wait_async([proc, set_powered](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                proc->wait_finish(result);
            } catch (const Glib::Error&) {
            }
            set_powered();
        });
    } catch (const Glib::Error&) {
        set_powered(); // no rfkill binary — try anyway
    }
}

void Bluez::set_scanning(bool active) {
    want_scanning_ = active;
    if (!manager_ || adapter_path_.empty() || active == scanning_) {
        return;
    }
    auto object = manager_->get_object(adapter_path_);
    auto adapter = object ? iface_proxy(object, kAdapterIface) : nullptr;
    if (!adapter) {
        return;
    }
    scanning_ = active; // optimistic — Discovering confirms, and this keeps a
                        // burst of rebuilds from re-issuing the call
    adapter->call(active ? "StartDiscovery" : "StopDiscovery",
                  [adapter, active](Glib::RefPtr<Gio::AsyncResult>& result) {
                      try {
                          adapter->call_finish(result);
                      } catch (const Glib::Error& e) {
                          // "InProgress"/"No discovery started" races are harmless
                          g_debug("bluetooth %s discovery: %s",
                                  active ? "start" : "stop", e.what());
                      }
                  });
}

void Bluez::refresh_devices() {
    if (!manager_ || adapter_path_.empty() || !enabled_) {
        return;
    }
    auto object = manager_->get_object(adapter_path_);
    auto adapter = object ? iface_proxy(object, kAdapterIface) : nullptr;
    if (!adapter) {
        return;
    }
    // Unpaired devices are only BlueZ's in-memory discovery cache; dropping
    // them makes the Available list repopulate from what is really in range.
    for (const auto& dev : devices_) {
        if (dev.paired || dev.trusted || dev.connected || busy_.count(dev.path)) {
            continue;
        }
        adapter->call("RemoveDevice",
                      [adapter](Glib::RefPtr<Gio::AsyncResult>& result) {
                          try {
                              adapter->call_finish(result);
                          } catch (const Glib::Error& e) {
                              g_debug("bluetooth RemoveDevice: %s", e.what());
                          }
                      },
                      Glib::Variant<std::tuple<Glib::DBusObjectPathString>>::create(
                          {Glib::DBusObjectPathString(dev.path)}));
    }
    // Restart discovery so the adapter runs a fresh inquiry; StartDiscovery
    // only goes out once BlueZ confirmed the stop (and only if still wanted —
    // the popover may have closed meanwhile). A rebuild seeing Discovering
    // false may already have re-armed it through the panel; then skip.
    adapter->call("StopDiscovery", [this, adapter](Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            adapter->call_finish(result);
        } catch (const Glib::Error& e) {
            g_debug("bluetooth refresh stop discovery: %s", e.what());
        }
        if (want_scanning_ && !scanning_) {
            set_scanning(true);
        }
    });
}

void Bluez::finish_action(const std::string& path, bool ok, const std::string& message) {
    busy_.erase(path);
    if (!ok) {
        g_warning("bluetooth: %s", message.c_str());
    }
    action_done_.emit(ok, message);
    rebuild();
}

// Pair via bluetoothctl: it registers its own BlueZ agent internally, so
// just-works devices pair without us implementing org.bluez.Agent1 (the same
// reason Noctalia's pair script drives bluetoothctl). PIN-entry pairing
// (keyboards) is not supported here. On success: trust + connect.
void Bluez::pair_device(const std::string& path) {
    std::string address;
    for (const auto& dev : devices_)
        if (dev.path == path)
            address = dev.address;
    if (address.empty()) {
        return;
    }
    busy_.insert(path);
    changed_.emit();
    // pause discovery during pairing to reduce HCI churn (Noctalia does too)
    resume_scan_ = scanning_;
    if (scanning_) {
        set_scanning(false);
    }
    try {
        auto proc = Gio::Subprocess::create(
            {"bluetoothctl", "--timeout", "30", "pair", address},
            Gio::Subprocess::Flags::STDOUT_PIPE | Gio::Subprocess::Flags::STDERR_MERGE);
        proc->communicate_utf8_async(
            "",
            [this, proc, path](Glib::RefPtr<Gio::AsyncResult>& result) {
                std::string out;
                bool ok = false;
                try {
                    out = proc->communicate_utf8_finish(result).first;
                    ok = proc->get_successful();
                } catch (const Glib::Error& e) {
                    out = e.what();
                }
                if (resume_scan_) {
                    resume_scan_ = false;
                    set_scanning(true);
                }
                while (!out.empty() && (out.back() == '\n' || out.back() == ' '))
                    out.pop_back();
                if (ok && out.find("Failed") != std::string::npos)
                    ok = false; // bluetoothctl can exit 0 on failure
                if (!ok) {
                    finish_action(path, false, out.empty() ? "Pairing failed" : out);
                    return;
                }
                busy_.erase(path);
                connect_device(path); // trust + connect the fresh pairing
            },
            {});
    } catch (const Glib::Error& e) {
        resume_scan_ = false;
        finish_action(path, false, e.what());
    }
}

void Bluez::connect_device(const std::string& path) {
    auto device = device_proxy(path);
    if (!device) {
        return;
    }
    busy_.insert(path);
    changed_.emit();
    // Trust first so reconnects work unattended (Noctalia's connectDeviceWithTrust)
    auto conn = manager_->get_connection();
    conn->call(
        path, kPropertiesIface, "Set",
        Glib::Variant<std::tuple<Glib::ustring, Glib::ustring, Glib::VariantBase>>::create(
            {kDeviceIface, "Trusted", Glib::Variant<bool>::create(true)}),
        [this, path, device, conn](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                conn->call_finish(result);
            } catch (const Glib::Error& e) {
                g_debug("bluetooth: setting Trusted failed: %s", e.what());
            }
            device->call(
                "Connect",
                [this, path, device](Glib::RefPtr<Gio::AsyncResult>& result) {
                    try {
                        device->call_finish(result);
                        finish_action(path, true, "");
                    } catch (const Glib::Error& e) {
                        finish_action(path, false, e.what());
                    }
                },
                Glib::VariantContainerBase(), 60000); // profile connects are slow
        },
        kBusName);
}

void Bluez::disconnect_device(const std::string& path) {
    auto device = device_proxy(path);
    if (!device) {
        return;
    }
    busy_.insert(path);
    changed_.emit();
    device->call(
        "Disconnect",
        [this, path, device](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                device->call_finish(result);
                finish_action(path, true, "");
            } catch (const Glib::Error& e) {
                finish_action(path, false, e.what());
            }
        },
        Glib::VariantContainerBase(), 30000);
}

// -- auto-connect (bar.bluetooth.auto_connect) --------------------------------

void Bluez::schedule_auto_connect() {
    auto_connect_timer_.disconnect();
    auto_connect_timer_ = Glib::signal_timeout().connect(
        [this] {
            attempt_auto_connect();
            return false;
        },
        1500); // give the adapter a moment after power-on, like Noctalia
}

void Bluez::attempt_auto_connect() {
    if (!enabled_ || !Config::get().bluetooth_auto_connect()) {
        return;
    }
    // connect sequentially, 500ms apart (Noctalia's autoConnectStepTimer)
    unsigned delay = 0;
    for (const auto& dev : devices_) {
        if (!dev.paired || dev.connected || dev.blocked || dev.busy) {
            continue;
        }
        Glib::signal_timeout().connect_once(
            [this, path = dev.path] {
                if (enabled_)
                    connect_device(path);
            },
            delay);
        delay += 500;
    }
}

} // namespace hyprshell
