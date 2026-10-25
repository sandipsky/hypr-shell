#pragma once

#include <giomm.h>
#include <sigc++/sigc++.h>

namespace hyprshell {

// Battery state from UPower's DisplayDevice (system DBus), no libupower needed.
class UPower {
public:
    static UPower& get();

    UPower(const UPower&) = delete;
    UPower& operator=(const UPower&) = delete;

    // False until the proxy is up and a battery is present.
    bool available() const { return available_; }
    double percentage() const { return percentage_; }
    // True for any plugged-in state (charging / fully charged / pending charge),
    // win11 semantics: the charging look is shown whenever on AC.
    bool plugged() const { return plugged_; }
    // On AC but not actively charging (fully charged / pending charge) —
    // Noctalia's "isPluggedIn", which its tooltip words as "Plugged in".
    bool plugged_idle() const { return plugged_idle_; }
    gint64 time_to_empty() const { return time_to_empty_; } // seconds, 0 = n/a
    gint64 time_to_full() const { return time_to_full_; }   // seconds, 0 = n/a
    double energy_rate() const { return energy_rate_; }     // watts
    double health() const { return health_; }               // %, 0 = unsupported

    sigc::signal<void()>& signal_changed() { return changed_; }

private:
    UPower();

    void read_properties();
    void find_battery_device();

    Glib::RefPtr<Gio::DBus::Proxy> proxy_;
    Glib::RefPtr<Gio::DBus::Proxy> battery_proxy_; // real battery, for health
    bool available_ = false;
    double percentage_ = 0.0;
    bool plugged_ = false;
    bool plugged_idle_ = false;
    gint64 time_to_empty_ = 0;
    gint64 time_to_full_ = 0;
    double energy_rate_ = 0.0;
    double health_ = 0.0;
    sigc::signal<void()> changed_;
};

} // namespace hyprshell
