#include "services/gamepad.hpp"

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace hyprshell {

namespace {

constexpr const char* kInputDir = "/dev/input";
constexpr unsigned kRetryMs = 400;       // udev applies the uaccess ACL shortly after the node appears
constexpr int kOpenAttempts = 8;         // ~3 s of retries for a fresh node
constexpr gint64 kReportIntervalUs = 250'000; // a moving stick sends hundreds of events per second

constexpr size_t kBitsPerLong = sizeof(unsigned long) * 8;

bool test_bit(const unsigned long* bits, unsigned bit) {
    return (bits[bit / kBitsPerLong] >> (bit % kBitsPerLong)) & 1ul;
}

bool any_bit_in(const unsigned long* bits, unsigned from, unsigned to) {
    for (unsigned bit = from; bit <= to; ++bit)
        if (test_bit(bits, bit))
            return true;
    return false;
}

// A gamepad / joystick advertises at least one button from the joystick,
// gamepad, d-pad or trigger-happy ranges. Motion-sensor and touchpad nodes
// of the same controller have none and are skipped (their sensors stream
// continuously).
bool has_gamepad_buttons(int fd) {
    unsigned long bits[(KEY_MAX + 1) / kBitsPerLong + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) < 0)
        return false;
    return any_bit_in(bits, BTN_JOYSTICK, BTN_JOYSTICK + 15) ||
           any_bit_in(bits, BTN_GAMEPAD, BTN_THUMBR) ||
           any_bit_in(bits, BTN_DPAD_UP, BTN_DPAD_RIGHT) ||
           any_bit_in(bits, BTN_TRIGGER_HAPPY, BTN_TRIGGER_HAPPY40);
}

} // namespace

Gamepad::Device::~Device() {
    io.disconnect();
    if (fd >= 0)
        close(fd);
}

Gamepad& Gamepad::get() {
    static Gamepad instance;
    return instance;
}

Gamepad::Gamepad() {
    try {
        auto dir = Gio::File::create_for_path(kInputDir);
        monitor_ = dir->monitor_directory(Gio::FileMonitorFlags::NONE);
        monitor_->signal_changed().connect(sigc::mem_fun(*this, &Gamepad::on_dir_changed));
    } catch (const Glib::Error& e) {
        g_message("gamepad: cannot watch %s (%s) — controller hotplug disabled", kInputDir,
                  e.what());
    }
    scan();
}

void Gamepad::scan() {
    try {
        Glib::Dir dir(kInputDir);
        for (const auto& name : dir) {
            if (name.rfind("event", 0) == 0)
                try_open(std::string(kInputDir) + "/" + name, 1); // no retries at startup
        }
    } catch (const Glib::Error&) {
    }
}

void Gamepad::on_dir_changed(const Glib::RefPtr<Gio::File>& file, const Glib::RefPtr<Gio::File>&,
                             Gio::FileMonitor::Event event) {
    if (!file)
        return;
    const std::string name = file->get_basename();
    if (name.rfind("event", 0) != 0)
        return;
    const std::string path = file->get_path();
    switch (event) {
    case Gio::FileMonitor::Event::CREATED:
        try_open(path, kOpenAttempts);
        break;
    case Gio::FileMonitor::Event::ATTRIBUTE_CHANGED: // the uaccess ACL landed
        try_open(path, 1);
        break;
    case Gio::FileMonitor::Event::DELETED:
        remove(path);
        break;
    default:
        break;
    }
}

void Gamepad::try_open(const std::string& path, int attempts_left) {
    if (std::any_of(devices_.begin(), devices_.end(),
                    [&](const auto& d) { return d->path == path; }))
        return;
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        // keyboards / mice stay root:input — only joystick nodes get the
        // seat user's ACL, and that ACL may trail the node by a moment
        if (errno == EACCES && attempts_left > 1)
            Glib::signal_timeout().connect_once(
                [this, path, attempts_left] { try_open(path, attempts_left - 1); }, kRetryMs);
        return;
    }
    if (!has_gamepad_buttons(fd)) {
        close(fd);
        return;
    }

    auto device = std::make_unique<Device>();
    device->path = path;
    device->fd = fd;
    char name[256] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof name - 1), name) >= 0)
        device->name = name;

    unsigned long abs_bits[(ABS_MAX + 1) / kBitsPerLong + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs_bits), abs_bits) >= 0) {
        for (unsigned code = 0; code <= ABS_MAX; ++code) {
            if (!test_bit(abs_bits, code))
                continue;
            input_absinfo info = {};
            if (ioctl(fd, EVIOCGABS(code), &info) < 0)
                continue;
            // dead zone: the driver's flat value, at least an eighth of the
            // range — resting sticks drift by a few percent
            const int32_t range = info.maximum - info.minimum;
            auto& axis = device->axes[code];
            axis.threshold = std::max<int32_t>({1, info.flat, range / 8});
            axis.last = info.value;
        }
    }

    Device* raw = device.get();
    device->io = Glib::signal_io().connect(
        [this, raw](Glib::IOCondition cond) { return on_readable(raw, cond); }, fd,
        Glib::IOCondition::IO_IN | Glib::IOCondition::IO_HUP | Glib::IOCondition::IO_ERR);
    g_message("gamepad: watching %s (%s)", path.c_str(), device->name.c_str());
    devices_.push_back(std::move(device));
}

bool Gamepad::on_readable(Device* device, Glib::IOCondition condition) {
    bool gone = (condition & (Glib::IOCondition::IO_HUP | Glib::IOCondition::IO_ERR)) !=
                Glib::IOCondition(0);
    bool activity = false;
    if (!gone) {
        input_event events[32];
        for (;;) {
            const ssize_t n = read(device->fd, events, sizeof events);
            if (n < 0) {
                if (errno == EAGAIN || errno == EINTR)
                    break;
                gone = true; // ENODEV: unplugged
                break;
            }
            if (n == 0)
                break;
            const size_t count = static_cast<size_t>(n) / sizeof(input_event);
            for (size_t i = 0; i < count; ++i) {
                const auto& ev = events[i];
                if (ev.type == EV_KEY) {
                    if (ev.value == 1)
                        activity = true;
                } else if (ev.type == EV_ABS && ev.code <= ABS_MAX) {
                    auto& axis = device->axes[ev.code];
                    if (axis.threshold > 0 && std::abs(ev.value - axis.last) >= axis.threshold) {
                        axis.last = ev.value;
                        activity = true;
                    }
                }
            }
        }
    }
    if (activity)
        report_activity();
    if (gone) {
        // the source is removed by returning false; drop the device from an
        // idle callback since this handler runs inside its own dispatch
        const std::string path = device->path;
        Glib::signal_idle().connect_once([this, path] { remove(path); });
        return false;
    }
    return true;
}

void Gamepad::remove(const std::string& path) {
    auto it = std::find_if(devices_.begin(), devices_.end(),
                           [&](const auto& d) { return d->path == path; });
    if (it == devices_.end())
        return;
    g_message("gamepad: %s (%s) removed", path.c_str(), (*it)->name.c_str());
    devices_.erase(it);
}

void Gamepad::report_activity() {
    const gint64 now = g_get_monotonic_time();
    if (now - last_report_us_ < kReportIntervalUs)
        return;
    last_report_us_ = now;
    activity_.emit();
}

} // namespace hyprshell
