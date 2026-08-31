#include "services/power_profiles.hpp"

namespace hyprshell {

PowerProfiles& PowerProfiles::get() {
    static PowerProfiles instance;
    return instance;
}

PowerProfiles::PowerProfiles() {
    Gio::DBus::Proxy::create_for_bus(
        Gio::DBus::BusType::SYSTEM, "net.hadess.PowerProfiles", "/net/hadess/PowerProfiles",
        "net.hadess.PowerProfiles", [this](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                proxy_ = Gio::DBus::Proxy::create_for_bus_finish(result);
            } catch (const Glib::Error& e) {
                g_debug("power-profiles-daemon unavailable: %s", e.what());
                return;
            }
            proxy_->signal_properties_changed().connect(
                [this](const Gio::DBus::Proxy::MapChangedProperties&,
                       const std::vector<Glib::ustring>&) { read_properties(); });
            read_properties();
        });
}

void PowerProfiles::set_profile(const std::string& profile) {
    if (!available_ || !proxy_)
        return;
    profile_ = profile; // optimistic — keeps the UI from bouncing
    changed_.emit();
    auto conn = proxy_->get_connection();
    conn->call(
        "/net/hadess/PowerProfiles", "org.freedesktop.DBus.Properties", "Set",
        Glib::Variant<std::tuple<Glib::ustring, Glib::ustring, Glib::VariantBase>>::
            create({"net.hadess.PowerProfiles", "ActiveProfile",
                    Glib::Variant<Glib::ustring>::create(profile)}),
        [conn](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                conn->call_finish(result);
            } catch (const Glib::Error& e) {
                g_warning("failed to set power profile: %s", e.what());
            }
        },
        "net.hadess.PowerProfiles");
}

void PowerProfiles::read_properties() {
    Glib::VariantBase value;
    proxy_->get_cached_property(value, "ActiveProfile");
    if (value.gobj() != nullptr) {
        profile_ = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(value).get();
        available_ = true;
    }
    changed_.emit();
}

} // namespace hyprshell
