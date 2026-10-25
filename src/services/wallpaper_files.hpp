#pragma once

// Wallpaper file types, shared by the shell's Wallpaper service and
// hypr-shell-settings' thumbnail grid so both list exactly the same files.
// Header-only, standard library only (the settings app has no gtkmm/giomm).

#include <cctype>
#include <string>
#include <string_view>

namespace hyprshell {

// Formats GdkPixbuf decodes with the loaders Arch ships by default (plus the
// common extra loaders); a missing loader shows up as a decode warning, not a
// crash.
inline constexpr std::string_view kWallpaperExtensions[] = {
    ".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp", ".tiff", ".tif", ".avif", ".jxl", ".svg",
};

inline bool is_wallpaper_image(std::string_view name) {
    std::string lower(name);
    for (auto& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto ext : kWallpaperExtensions)
        if (lower.size() > ext.size() && lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0)
            return true;
    return false;
}

} // namespace hyprshell
