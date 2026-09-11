#pragma once

#include <glibmm.h>

#include <pwd.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace hyprshell {

// Noctalia's HostService.displayName: the GECOS real name (its first
// comma-separated field), else the login name. Read from passwd on every
// call — GLib caches g_get_real_name() for the process lifetime, and the
// settings app's Users page can rename the account while the shell runs.
inline std::string user_display_name() {
    std::string name;
    std::vector<char> buffer(16384);
    passwd pw{};
    passwd* result = nullptr;
    if (getpwuid_r(getuid(), &pw, buffer.data(), buffer.size(), &result) == 0 && result != nullptr
        && pw.pw_gecos != nullptr) {
        name = pw.pw_gecos;
        if (const auto comma = name.find(','); comma != std::string::npos)
            name.erase(comma);
    }
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    while (!name.empty() && name.front() == ' ')
        name.erase(0, 1);
    if (name.empty())
        name = Glib::get_user_name();
    return name;
}

// The profile picture: freedesktop's ~/.face when present (HS_LOCK_AVATAR
// overrides it for testing); empty when there is none — callers then show
// the bundled fallback image (see bar/avatar.hpp).
inline std::string user_avatar_path() {
    if (const char* override_path = g_getenv("HS_LOCK_AVATAR"))
        return override_path;
    const std::string face = Glib::build_filename(Glib::get_home_dir(), ".face");
    return Glib::file_test(face, Glib::FileTest::IS_REGULAR) ? face : std::string();
}

} // namespace hyprshell
