#pragma once

#include <giomm.h>
#include <glibmm.h>
#include <sigc++/sigc++.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hyprshell {

// Game controller activity. The compositor only counts keyboard / pointer /
// touch input towards idleness — Hyprland never opens joystick devices — so a
// controller-only gaming session looks idle to ext-idle-notify. This service
// reads the evdev nodes of gamepads / joysticks itself (/dev/input/event*;
// systemd's uaccess rule gives the seated user an ACL on ID_INPUT_JOYSTICK
// devices, other input nodes stay root:input and are skipped) and emits
// signal_activity() — rate-limited — on button presses and stick / trigger
// movements past a dead zone (idle sticks drift by a few percent). Hotplug via
// a file monitor on /dev/input; a fresh node is retried while udev has not
// applied the ACL yet.
class Gamepad {
public:
    static Gamepad& get();

    Gamepad(const Gamepad&) = delete;
    Gamepad& operator=(const Gamepad&) = delete;

    bool any_connected() const { return !devices_.empty(); }

    sigc::signal<void()>& signal_activity() { return activity_; }

private:
    struct Axis {
        int32_t last = 0;
        int32_t threshold = 0; // 0 = not present
    };
    struct Device {
        std::string path;
        std::string name;
        int fd = -1;
        sigc::connection io;
        std::array<Axis, 64> axes; // ABS_MAX + 1
        ~Device();
    };

    Gamepad();

    void scan();
    void try_open(const std::string& path, int attempt);
    void on_dir_changed(const Glib::RefPtr<Gio::File>& file, const Glib::RefPtr<Gio::File>& other,
                        Gio::FileMonitor::Event event);
    bool on_readable(Device* device, Glib::IOCondition condition);
    void remove(const std::string& path);
    void report_activity();

    std::vector<std::unique_ptr<Device>> devices_;
    Glib::RefPtr<Gio::FileMonitor> monitor_;
    gint64 last_report_us_ = 0;
    sigc::signal<void()> activity_;
};

} // namespace hyprshell
