// The system font is state of the desktop, not of hypr-shell: GTK apps on
// Wayland read `org.gnome.desktop.interface font-name` (GSettings) and, without
// those schemas, `gtk-font-name` from ~/.config/gtk-{3,4}.0/settings.ini. The
// "User interface" page writes both so the family follows the shell font and the
// size can be changed for applications only (the shell's CSS owns its sizes).
#pragma once

#include <gio/gio.h>

#include <string>

namespace hyprshell::settings {

struct SystemFont {
    std::string family = "Adwaita Sans"; // Pango family
    std::string style;                   // e.g. "Book" / "Semi-Bold" — kept when only the size changes
    double size = 11;                    // points
    std::string to_string() const;       // Pango description: "Family Style 12"
};

// Current system font: GSettings, else settings.ini, else the defaults above.
SystemFont system_font_read();

// Writes GSettings (if the schema exists) and the two settings.ini files.
void system_font_write(const SystemFont& font);

// GSettings object for org.gnome.desktop.interface, or null when the schema is
// missing (connect "changed::font-name" to it). Owned by the caller.
GSettings* system_font_settings();

} // namespace hyprshell::settings
