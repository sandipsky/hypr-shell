#include "bar/modules/battery.hpp"

#include "services/power_profiles.hpp"
#include "services/upower.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace hyprshell {

namespace {

// Segoe Fluent Icons: Battery1..Battery10 decile fills, indexed 0..9.
constexpr const char* kLevels[] = {
    "", "", "", "", "",
    "", "", "", "", "",
};
constexpr const char* kChargingFrame = ""; // BatteryCharging0 U+E85A (bolt)
constexpr const char* kSaverFrame = "";    // BatterySaver0    U+E863 (leaf)

} // namespace

Battery::Battery() : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0) {
    add_css_class("module");
    add_css_class("battery");

    fill_.add_css_class("icon");
    fill_.add_css_class("icon-fill");
    frame_.add_css_class("icon");
    overlay_.set_child(fill_);
    overlay_.add_overlay(frame_);
    overlay_.set_valign(Gtk::Align::CENTER);
    append(overlay_);

    UPower::get().signal_changed().connect(sigc::mem_fun(*this, &Battery::update));
    PowerProfiles::get().signal_changed().connect(sigc::mem_fun(*this, &Battery::update));
    update();
}

void Battery::update() {
    auto& upower = UPower::get();
    set_visible(upower.available());
    if (!upower.available()) {
        return;
    }
    auto pct = upower.percentage();
    auto idx = std::clamp(static_cast<int>(std::ceil(pct / 10.0)) - 1, 0, 9);
    fill_.set_text(kLevels[idx]);

    // three looks, like the Noctalia fork: plugged-in wins (bolt frame, green
    // fill), then power-saver (leaf frame, amber fill), else plain fill only
    bool plugged = upower.plugged();
    bool saver = !plugged && PowerProfiles::get().saver();
    frame_.set_text(plugged ? kChargingFrame : kSaverFrame);
    frame_.set_visible(plugged || saver);
    if (plugged) {
        add_css_class("charging");
    } else {
        remove_css_class("charging");
    }
    if (saver) {
        add_css_class("saver");
    } else {
        remove_css_class("saver");
    }
    // icon-only in the bar; details live in the tooltip, worded like Noctalia's
    auto vague_duration = [](gint64 total) {
        std::string out;
        auto push = [&out](gint64 v, const char* unit) {
            if (!out.empty()) {
                out += ' ';
            }
            out += std::to_string(v) + unit;
        };
        gint64 days = total / 86400, hours = total % 86400 / 3600, minutes = total % 3600 / 60;
        if (days) {
            push(days, "d");
        }
        if (hours) {
            push(hours, "h");
        }
        if (minutes) {
            push(minutes, "m");
        }
        if (!hours && !minutes) {
            push(total % 60, "s");
        }
        return out;
    };

    std::string tooltip =
        "Battery level: " + std::to_string(static_cast<int>(std::lround(pct))) + "%";
    if (upower.plugged_idle()) {
        tooltip += "\nPlugged in";
    } else if (upower.time_to_full() > 0) {
        tooltip += "\nTime until full: " + vague_duration(upower.time_to_full());
    } else if (upower.time_to_empty() > 0) {
        tooltip += "\nTime left: " + vague_duration(upower.time_to_empty());
    } else {
        tooltip += "\nIdle";
    }
    if (!upower.plugged_idle() && (upower.time_to_full() > 0 || upower.time_to_empty() > 0)) {
        char rate[32];
        std::snprintf(rate, sizeof(rate), "%.2f", std::abs(upower.energy_rate()));
        tooltip += upower.time_to_full() > 0 ? "\nCharging rate: " : "\nDischarging rate: ";
        tooltip += std::string(rate) + " W";
    }
    if (upower.health() > 0) {
        tooltip +=
            "\nBattery health: " + std::to_string(static_cast<int>(std::lround(upower.health()))) + "%";
    }
    set_tooltip_text(tooltip);
}

} // namespace hyprshell
