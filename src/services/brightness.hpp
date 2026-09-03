#pragma once

#include <giomm.h>
#include <sigc++/sigc++.h>

#include <string>

namespace hyprshell {

// Internal backlight (first /sys/class/backlight device). Reads are tiny sysfs
// files; writes go through logind's Session.SetBrightness, which the session
// owner may call without privileges — no brightnessctl, no polkit prompt.
// A Gio::FileMonitor on the sysfs brightness file picks up writes made by
// other programs (brightnessctl, logind on behalf of another client) — the
// kernel does emit inotify events for write(2) on sysfs attributes — so
// signal_changed() also fires for external changes (the OSD relies on it).
class Brightness {
public:
    static Brightness& get();

    Brightness(const Brightness&) = delete;
    Brightness& operator=(const Brightness&) = delete;

    bool available() const { return max_ > 0; }
    double fraction() const { return max_ > 0 ? double(current_) / max_ : 0.0; }

    // Re-read the sysfs value; the file monitor calls this too, panels call
    // it when they open to be safe (kernel-driven changes are not notified).
    void refresh();

    // 0..1; the cache updates immediately for the UI, the logind call is
    // debounced so slider drags don't flood the bus.
    void set_fraction(double fraction);

    sigc::signal<void()>& signal_changed() { return changed_; }

private:
    Brightness();

    void write_pending();

    std::string device_; // e.g. "intel_backlight"
    int max_ = 0;
    int current_ = 0;
    int pending_ = -1;
    Glib::RefPtr<Gio::DBus::Connection> bus_;
    Glib::RefPtr<Gio::FileMonitor> monitor_;
    sigc::connection debounce_;
    sigc::signal<void()> changed_;
};

} // namespace hyprshell
