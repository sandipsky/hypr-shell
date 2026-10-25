#include "services/brightness.hpp"

#include <glibmm.h>

#include <algorithm>
#include <cmath>

namespace hyprshell {

namespace {

constexpr const char* kBacklightDir = "/sys/class/backlight";

int read_sysfs_int(const std::string& path) {
    try {
        return std::stoi(Glib::file_get_contents(path));
    } catch (...) {
        return 0;
    }
}

} // namespace

Brightness& Brightness::get() {
    static Brightness instance;
    return instance;
}

Brightness::Brightness() {
    // sysfs enumeration + two tiny reads: sync like the config's initial load
    try {
        Glib::Dir dir(kBacklightDir);
        for (const auto& name : dir) {
            const std::string base = std::string(kBacklightDir) + "/" + name;
            const int max = read_sysfs_int(base + "/max_brightness");
            if (max > 0) {
                device_ = name;
                max_ = max;
                current_ = read_sysfs_int(base + "/brightness");
                break;
            }
        }
    } catch (const Glib::Error&) {
        // no backlight — desktop machine; available() stays false
    }
    if (max_ == 0)
        return;

    // external writes (brightnessctl, another client via logind) → refresh
    try {
        auto file = Gio::File::create_for_path(std::string(kBacklightDir) + "/" + device_ +
                                               "/brightness");
        monitor_ = file->monitor_file();
        monitor_->signal_changed().connect(
            [this](const Glib::RefPtr<Gio::File>&, const Glib::RefPtr<Gio::File>&,
                   Gio::FileMonitor::Event event) {
                if (event == Gio::FileMonitor::Event::CHANGED ||
                    event == Gio::FileMonitor::Event::CHANGES_DONE_HINT)
                    refresh();
            });
    } catch (const Glib::Error& e) {
        g_warning("brightness: cannot watch sysfs: %s", e.what());
    }

    Gio::DBus::Connection::get(
        Gio::DBus::BusType::SYSTEM, [this](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                bus_ = Gio::DBus::Connection::get_finish(result);
            } catch (const Glib::Error& e) {
                g_warning("brightness: system bus unavailable: %s", e.what());
            }
        });
}

void Brightness::refresh() {
    if (max_ == 0)
        return;
    const int value =
        read_sysfs_int(std::string(kBacklightDir) + "/" + device_ + "/brightness");
    if (value != current_) {
        current_ = value;
        changed_.emit();
    }
}

void Brightness::set_fraction(double fraction) {
    if (max_ == 0)
        return;
    fraction = std::clamp(fraction, 0.0, 1.0);
    current_ = static_cast<int>(std::lround(fraction * max_));
    changed_.emit();
    pending_ = current_;
    if (debounce_.connected())
        return; // a write is already queued; it picks up the newest value
    debounce_ = Glib::signal_timeout().connect(
        [this] {
            write_pending();
            return false;
        },
        100);
}

void Brightness::write_pending() {
    if (!bus_ || pending_ < 0)
        return;
    const guint32 value = static_cast<guint32>(pending_);
    pending_ = -1;
    // logind resolves "auto" to the caller's session; the session owner may
    // set brightness on its own seat without polkit interaction.
    bus_->call(
        "/org/freedesktop/login1/session/auto", "org.freedesktop.login1.Session",
        "SetBrightness",
        Glib::Variant<std::tuple<Glib::ustring, Glib::ustring, guint32>>::create(
            {"backlight", device_, value}),
        [this](Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                bus_->call_finish(result);
            } catch (const Glib::Error& e) {
                g_warning("brightness: SetBrightness failed: %s", e.what());
            }
        },
        "org.freedesktop.login1");
}

} // namespace hyprshell
