#pragma once

#include <glibmm.h>
#include <sigc++/sigc++.h>

#include <vector>

namespace hyprshell {

// Caps / Num / Scroll Lock state — Noctalia's LockKeysService: the kernel's
// keyboard LEDs under /sys/class/leds/input*::{capslock,numlock,scrolllock}
// are polled every 200ms while enabled (LED state is driven by the kernel, so
// inotify never fires for these files). The LED files stay open and are
// re-read with pread(), so a poll is three tiny syscalls and no allocation.
// The first read after enabling only syncs the state; later changes emit
// signal_changed(key, on). Several keyboards are OR-ed together.
class LockKeys {
public:
    enum class Key { Caps, Num, Scroll };

    static LockKeys& get();

    LockKeys(const LockKeys&) = delete;
    LockKeys& operator=(const LockKeys&) = delete;

    bool available() const { return !leds_.empty(); }
    bool caps_lock() const { return state_[0]; }
    bool num_lock() const { return state_[1]; }
    bool scroll_lock() const { return state_[2]; }
    bool is_on(Key key) const { return state_[static_cast<int>(key)]; }

    // start/stop polling (the OSD registers while osd.enabled)
    void set_enabled(bool enabled);

    static const char* key_name(Key key); // "CAPS" / "NUM" / "SCROLL"

    sigc::signal<void(Key, bool)>& signal_changed() { return changed_; }

private:
    LockKeys();
    ~LockKeys();

    bool poll();

    struct Led {
        Key key;
        int fd; // .../brightness, kept open
    };
    std::vector<Led> leds_;
    bool state_[3] = {false, false, false};
    bool synced_ = false;
    sigc::connection timer_;
    sigc::signal<void(Key, bool)> changed_;
};

} // namespace hyprshell
