#include "services/lock_keys.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <string>

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
            const std::string path = std::string(kLedsDir) + "/" + name + "/brightness";
            const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd >= 0)
                leds_.push_back({key, fd});
        }
    } catch (const Glib::Error&) {
    }
}

LockKeys::~LockKeys() {
    for (const auto& led : leds_)
        close(led.fd);
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
        char buf[16];
        const ssize_t n = pread(led.fd, buf, sizeof buf - 1, 0);
        if (n <= 0 || buf[0] < '0' || buf[0] > '9')
            continue; // transient sysfs failure (e.g. resume) — skip this LED
        bool on = false;
        for (ssize_t i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; ++i)
            on = on || buf[i] != '0';
        const int index = static_cast<int>(led.key);
        seen[index] = true;
        next[index] = next[index] || on;
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
