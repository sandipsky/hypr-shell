// Icon theme of applications: GSettings `org.gnome.desktop.interface
// icon-theme` (what GTK reads on Wayland) and `gtk-icon-theme-name` in
// ~/.config/gtk-{3,4}.0/settings.ini. System state only — the shell has no
// config key for it (its own app icons follow GTK's icon theme like any app).
#pragma once

#include <string>
#include <vector>

namespace hyprshell::settings {

struct IconTheme {
    std::string id;   // directory name — the value GSettings / settings.ini take
    std::string name; // index.theme's Name
};

// Installed icon themes (index.theme with a Directories key under ~/.icons,
// ~/.local/share/icons and $XDG_DATA_DIRS/icons; hicolor and cursor-only
// themes skipped), sorted by name.
std::vector<IconTheme> icon_themes();

std::string system_icon_theme_read();               // GSettings, else settings.ini, else "Adwaita"
void system_icon_theme_write(const std::string& id); // GSettings + both settings.ini files

} // namespace hyprshell::settings
