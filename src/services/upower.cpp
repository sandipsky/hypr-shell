#include "services/upower.hpp"

namespace hyprshell {

namespace {

// org.freedesktop.UPower.Device.State
enum UPowerState : guint32 {
    kCharging = 1,
    kFullyCharged = 4,
    kPendingCharge = 5,
};

} // namespace

UPower& UPower::get() {
    static UPower instance;
    return instance;
}

UPower::UPower() {
    Gio::DBus::Proxy::create_for_bus(
        Gio::DBus::BusType::SYSTEM, "org.freedesktop.UPower",
        "/org/freedesktop/UPower/devices/DisplayDevice", "org.freedesktop.UPower.Device",
        [this](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                proxy_ = Gio::DBus::Proxy::create_for_bus_finish(result);
            } catch (const Glib::Error& e) {
                g_debug("UPower unavailable: %s", e.what());
                return;
            }
            proxy_->signal_properties_changed().connect(
                [this](const Gio::DBus::Proxy::MapChangedProperties&,
                       const std::vector<Glib::ustring>&) { read_properties(); });
            read_properties();
            find_battery_device();
        });
}

// The DisplayDevice is a composite and reports no Capacity (health); find the
// first real battery device and read it from there, like Noctalia does.
void UPower::find_battery_device() {
    proxy_->get_connection()->call(
        "/org/freedesktop/UPower", "org.freedesktop.UPower", "EnumerateDevices", {},
        [this](Glib::RefPtr<Gio::AsyncResult>& result) {
            std::string battery_path;
            try {
                auto reply = proxy_->get_connection()->call_finish(result);
                Glib::Variant<std::vector<Glib::DBusObjectPathString>> paths;
                reply.get_child(paths, 0);
                for (const auto& path : paths.get()) {
                    if (path.raw().find("/devices/battery_") != std::string::npos) {
                        battery_path = path.raw();
                        break;
                    }
                }
            } catch (const Glib::Error& e) {
                g_debug("UPower EnumerateDevices failed: %s", e.what());
            }
            if (battery_path.empty()) {
                return;
            }
            Gio::DBus::Proxy::create_for_bus(
                Gio::DBus::BusType::SYSTEM, "org.freedesktop.UPower", battery_path,
                "org.freedesktop.UPower.Device", [this](Glib::RefPtr<Gio::AsyncResult>& res) {
                    try {
                        battery_proxy_ = Gio::DBus::Proxy::create_for_bus_finish(res);
                    } catch (const Glib::Error& e) {
                        g_debug("UPower battery proxy failed: %s", e.what());
                        return;
                    }
                    battery_proxy_->signal_properties_changed().connect(
                        [this](const Gio::DBus::Proxy::MapChangedProperties&,
                               const std::vector<Glib::ustring>&) { read_properties(); });
                    read_properties();
                });
        },
        "org.freedesktop.UPower");
}

void UPower::read_properties() {
    auto get_prop = [this](const char* name) {
        Glib::VariantBase value;
        proxy_->get_cached_property(value, name);
        return value;
    };

    bool present = false;
    if (auto v = get_prop("IsPresent"); v.gobj() != nullptr) {
        present = Glib::VariantBase::cast_dynamic<Glib::Variant<bool>>(v).get();
    }
    if (auto v = get_prop("Percentage"); v.gobj() != nullptr) {
        percentage_ = Glib::VariantBase::cast_dynamic<Glib::Variant<double>>(v).get();
    }
    if (auto v = get_prop("State"); v.gobj() != nullptr) {
        auto state = Glib::VariantBase::cast_dynamic<Glib::Variant<guint32>>(v).get();
        plugged_ = state == kCharging || state == kFullyCharged || state == kPendingCharge;
        plugged_idle_ = state == kFullyCharged || state == kPendingCharge;
    }
    if (auto v = get_prop("TimeToEmpty"); v.gobj() != nullptr) {
        time_to_empty_ = Glib::VariantBase::cast_dynamic<Glib::Variant<gint64>>(v).get();
    }
    if (auto v = get_prop("TimeToFull"); v.gobj() != nullptr) {
        time_to_full_ = Glib::VariantBase::cast_dynamic<Glib::Variant<gint64>>(v).get();
    }
    if (auto v = get_prop("EnergyRate"); v.gobj() != nullptr) {
        energy_rate_ = Glib::VariantBase::cast_dynamic<Glib::Variant<double>>(v).get();
    }
    if (battery_proxy_) {
        Glib::VariantBase v;
        battery_proxy_->get_cached_property(v, "Capacity");
        if (v.gobj() != nullptr) {
            health_ = Glib::VariantBase::cast_dynamic<Glib::Variant<double>>(v).get();
        }
    }

    available_ = present;
    changed_.emit();
}

} // namespace hyprshell
