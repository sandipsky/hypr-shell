#pragma once

#include <sigc++/sigc++.h>

namespace hyprshell {

// Low / critical battery notifications — Noctalia's BatteryService toasts:
// one "Low battery" notification when discharging at or below 20 %, one
// "Critical battery" at or below 5 %; each fires once per discharge cycle
// (re-armed when plugged in or when the level climbs back above the
// threshold). Gated by notifications.battery_alerts.
class BatteryAlerts {
public:
    static BatteryAlerts& get();

    BatteryAlerts(const BatteryAlerts&) = delete;
    BatteryAlerts& operator=(const BatteryAlerts&) = delete;

private:
    BatteryAlerts();
    void check();

    bool notified_low_ = false;
    bool notified_critical_ = false;
};

} // namespace hyprshell
