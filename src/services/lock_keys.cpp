#include "services/lock_keys.hpp"

namespace hyprshell {

namespace {

constexpr const char* kLedsDir = "/sys/class/leds";
constexpr unsigned kPollMs = 200; // Noctalia's refreshTimer

} // namespace

LockKeys& LockKeys::get() {
    static LockKeys instance;
    return instance;
}

LockKeys::LockKeys() {
    // enumerate once (sync, tiny directory — like the backlight service)
    try {
        Glib::Dir dir(kLedsDir);
        for (const auto& name : dir) {
            if (name.rfind("input", 0) != 0)
                continue;
            const auto sep = name.find("::");
            if (sep == std::string::npos)
                continue;
            const std::string kind = name.substr(sep + 2);
            Key key;
            if (kind == "capslock")
                key = Key::Caps;
            else if (kind == "numlock")
                key = Key::Num;
            else if (kind == "scrolllock")
                key = Key::Scroll;
            else
                continue;
            leds_.push_back({key, std::string(kLedsDir) + "/" + name + "/brightness"});
        }
    } catch (const Glib::Error&) {
        // no sysfs leds — available() stays false
    }
}

const char* LockKeys::key_name(Key key) {
    switch (key) {
    case Key::Caps:
        return "CAPS";
    case Key::Num:
        return "NUM";
    case Key::Scroll:
        return "SCROLL";
    }
    return "";
}

void LockKeys::set_enabled(bool enabled) {
    if (enabled == timer_.connected())
        return;
    if (!enabled) {
        timer_.disconnect();
        return;
    }
    if (leds_.empty())
        return;
    synced_ = false; // first poll after (re)enabling must not emit
    poll();
    timer_ = Glib::signal_timeout().connect(sigc::mem_fun(*this, &LockKeys::poll), kPollMs);
}

bool LockKeys::poll() {
    bool next[3] = {false, false, false};
    bool seen[3] = {false, false, false};
    for (const auto& led : leds_) {
        std::string raw;
        try {
            raw = Glib::file_get_contents(led.path);
        } catch (const Glib::Error&) {
            continue; // transient sysfs failure (e.g. resume) — skip this LED
        }
        // digits only; anything else is not a valid reading
        const auto end = raw.find_first_not_of("0123456789");
        const std::string digits = raw.substr(0, end);
        if (digits.empty())
            continue;
        const int index = static_cast<int>(led.key);
        seen[index] = true;
        next[index] = next[index] || digits != "0";
    }
    for (int i = 0; i < 3; ++i) {
        if (!seen[i])
            continue;
        if (state_[i] == next[i])
            continue;
        state_[i] = next[i];
        if (synced_)
            changed_.emit(static_cast<Key>(i), next[i]);
    }
    synced_ = true;
    return true;
}

} // namespace hyprshell
