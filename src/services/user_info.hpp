#pragma once

#include <glibmm.h>

#include <string>

namespace hyprshell {

// Noctalia's HostService.displayName: the GECOS real name, else the login
// name (GLib reports "Unknown" when GECOS is empty).
inline std::string user_display_name() {
    std::string name = Glib::get_real_name();
    while (!name.empty() && (name.back() == ' ' || name.back() == ','))
        name.pop_back();
    if (name.empty() || name == "Unknown")
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
