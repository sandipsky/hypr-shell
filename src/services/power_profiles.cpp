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
