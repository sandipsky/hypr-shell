#pragma once

#include "services/item_order.hpp"

#include <string>
#include <vector>

namespace hyprshell {

// Entries of the desktop context menu (right click on the wallpaper), in
// default order. Shared (header-only) by the shell and hypr-shell-settings so
// the settings page lists exactly what the menu can show. Glyphs are
// noctalia-tabler-icons (\u escapes — never literal PUA).
struct DesktopMenuItem {
    const char* key;         // config key under desktop_menu.items / .order
    const char* label;
    const char* description; // settings row subtitle
    const char* glyph;
    bool default_on;
};

constexpr DesktopMenuItem kDesktopMenuItems[] = {
    {"apps", "Apps", "A submenu listing every installed application.", "", true},
    {"settings", "Settings",
     "All Settings, then one entry per page of the settings app.", "", true},
    {"next_wallpaper", "Next desktop background",
     "Steps the wallpaper slideshow; shown only while the slideshow is on.", "", true},
    {"session", "Session", "The session menu's actions: lock, suspend, reboot …",
     "", true},
    {"keybindings", "Keybindings",
     "Overlay listing Hyprland's key bindings (also `hypr-shell --keybindings`).",
     "", true},
};

// The table in the user's order (desktop_menu.order in config); see
// services/item_order.hpp for the rules.
inline std::vector<const DesktopMenuItem*> desktop_menu_items_in_order(
    const std::vector<std::string>& order) {
    return items_in_config_order(kDesktopMenuItems, order);
}

} // namespace hyprshell
