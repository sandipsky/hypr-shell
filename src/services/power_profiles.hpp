#pragma once

#include <giomm.h>
#include <sigc++/sigc++.h>

#include <string>

namespace hyprshell {

// Active power profile from power-profiles-daemon (net.hadess.PowerProfiles).
class PowerProfiles {
public:
    static PowerProfiles& get();

    PowerProfiles(const PowerProfiles&) = delete;
    PowerProfiles& operator=(const PowerProfiles&) = delete;

    bool available() const { return available_; }
    // "power-saver", "balanced" or "performance"
    const std::string& profile() const { return profile_; }
    bool saver() const { return profile_ == "power-saver"; }
    // Write ActiveProfile; the local cache updates optimistically and the
    // daemon's PropertiesChanged confirms (or corrects) it.
    void set_profile(const std::string& profile);

    sigc::signal<void()>& signal_changed() { return changed_; }

private:
    PowerProfiles();

    void read_properties();

    Glib::RefPtr<Gio::DBus::Proxy> proxy_;
    bool available_ = false;
    std::string profile_;
    sigc::signal<void()> changed_;
};

} // namespace hyprshell
