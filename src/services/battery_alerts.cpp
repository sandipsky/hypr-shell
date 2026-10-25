#include "services/battery_alerts.hpp"

#include "services/config.hpp"
#include "services/notifications.hpp"
#include "services/upower.hpp"

#include <cmath>
#include <string>

namespace hyprshell {

namespace {
constexpr double kWarningPercent = 20;  // Noctalia's batteryWarningThreshold
constexpr double kCriticalPercent = 5;  // batteryCriticalThreshold
} // namespace

BatteryAlerts& BatteryAlerts::get() {
    static BatteryAlerts instance;
    return instance;
}

BatteryAlerts::BatteryAlerts() {
    UPower::get().signal_changed().connect(sigc::mem_fun(*this, &BatteryAlerts::check));
}

void BatteryAlerts::check() {
    auto& upower = UPower::get();
    if (!upower.available())
        return;
    const double percent = upower.percentage();
    if (upower.plugged() || percent > kWarningPercent) {
        notified_low_ = notified_critical_ = false;
        return;
    }
    if (percent > kCriticalPercent)
        notified_critical_ = false;
    if (!Config::get().notifications().battery_alerts)
        return;

    const bool critical = percent <= kCriticalPercent;
    bool& notified = critical ? notified_critical_ : notified_low_;
    if (notified)
        return;
    notified = true;
    const std::string level = std::to_string(static_cast<int>(std::lround(percent)));
    NotificationService::get().notify_local(
        "Battery", critical ? "Critical battery" : "Low battery",
        "Battery is at " + level + "% — please connect the charger" +
            (critical ? " immediately" : ""),
        critical ? 2 : 1, critical ? "battery-caution-symbolic" : "battery-low-symbolic");
}

} // namespace hyprshell
