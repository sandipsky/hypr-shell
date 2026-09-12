// The system font is state of the desktop, not of hypr-shell: GTK apps on
// Wayland read `org.gnome.desktop.interface font-name` (GSettings) and, without
// those schemas, `gtk-font-name` from ~/.config/gtk-{3,4}.0/settings.ini. The
// "User interface" page writes both so the family follows the shell font and the
// size can be changed for applications only (the shell's CSS owns its sizes).
//
// Qt apps read neither. Without a Qt platform theme (none is configured on the
// dotfiles desktop — the gtk3 one imports the GTK size too, which was far too
// big on top of QT_SCALE_FACTOR) Qt asks fontconfig for the generic "Sans
// Serif" family at its own 9pt, so the family is mirrored a third way: a
// fontconfig snippet, ~/.config/fontconfig/conf.d/50-hypr-shell-sans-serif.conf,
// that makes sans-serif prefer the chosen family. Only the family can follow
// this way — the size stays Qt's. Fontconfig is read at process start, so
// running Qt apps pick it up at their next launch.
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

// Writes GSettings (if the schema exists), the two settings.ini files and the
// fontconfig sans-serif snippet (family only).
void system_font_write(const SystemFont& font);

// Path of the fontconfig snippet system_font_write() maintains.
std::string system_font_fontconfig_path();

// GSettings object for org.gnome.desktop.interface, or null when the schema is
// missing (connect "changed::font-name" to it). Owned by the caller.
GSettings* system_font_settings();

} // namespace hyprshell::settings
