// The mouse cursor is desktop state shared by four consumers: GTK apps read
// GSettings `org.gnome.desktop.interface cursor-theme` / `cursor-size` (else
// `gtk-cursor-theme-*` in settings.ini), XWayland and Xcursor-based clients
// resolve the "default" theme through ~/.icons/default/index.theme, Hyprland
// takes `setcursor` (the shell applies `ui.cursor_theme` / `ui.cursor_size`
// from config.json), and SDDM's greeter reads [Theme] CursorTheme / CursorSize
// from its root-owned config (written through pkexec).
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace hyprshell::settings {

struct CursorTheme {
    std::string id;   // directory name — what XCURSOR_THEME / gsettings use
    std::string name; // index.theme's Name, else the id
};

// Installed cursor themes (directories with cursors/ or hyprcursors/ under
// ~/.icons, ~/.local/share/icons and $XDG_DATA_DIRS/icons), sorted by name.
std::vector<CursorTheme> cursor_themes();

struct SystemCursor {
    std::string theme = "Adwaita";
    int size = 24;
};

// Current cursor: GSettings, else settings.ini, else the defaults above.
SystemCursor system_cursor_read();

// GSettings (if the schema exists), both settings.ini files and
// ~/.icons/default/index.theme.
void system_cursor_write(const SystemCursor& cursor);

bool sddm_installed(); // sddm on PATH

// Writes /etc/sddm.conf.d/zz-hypr-shell-cursor.conf ([Theme] CursorTheme /
// CursorSize + the XCURSOR_* entries of GreeterEnvironment, other entries kept)
// through pkexec; `done(ok, message)` runs on the main loop. HS_CURSOR_SDDM_ROOT=
// <dir> writes under that directory without pkexec (dev hook).
void sddm_cursor_apply(const SystemCursor& cursor,
                       std::function<void(bool ok, std::string message)> done);

} // namespace hyprshell::settings
