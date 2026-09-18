#pragma once

namespace hyprshell {

// The sidebar pages of hypr-shell-settings: GtkStack child name (also the
// HS_SETTINGS_PAGE tag `open_settings()` passes), label, icon. `icon` is a
// symbolic icon-theme name, or "glyph:<codepoint>" for a tabler glyph from the
// bundled noctalia-tabler-icons font where Adwaita has nothing fitting.
// Shared (header-only) by the settings app (sidebar rows, search) and the
// shell (the desktop menu's Settings submenu lists these pages).
struct SettingsPage {
    const char* name;
    const char* title;
    const char* icon;
};

// GNOME Settings look; About last, like GNOME.
constexpr SettingsPage kSettingsPages[] = {
    {"bar", "Bar", "focus-top-bar-symbolic"},
    {"presets_page", "Presets", "document-save-symbolic"},
    {"ui_page", "User interface", "preferences-desktop-appearance-symbolic"},
    {"wallpaper_page", "Wallpaper", "preferences-desktop-wallpaper-symbolic"},
    {"desktop_menu_page", "Desktop menu", "open-menu-symbolic"},
    {"night_light_page", "Night light", "night-light-symbolic"},
    {"hotspot_page", "Hotspot", "glyph:"}, // tabler access-point
    {"vpn_page", "VPN", "glyph:"},         // tabler shield-lock
    {"launcher_page", "Launcher", "glyph:"}, // tabler rocket, the app menu's default
    {"clipboard_page", "Clipboard", "edit-paste-symbolic"},
    {"session_page", "Session menu", "system-shutdown-symbolic"},
    {"lock_page", "Lock screen", "system-lock-screen-symbolic"},
    {"users_page", "Users", "system-users-symbolic"},
    {"login_page", "Login screen", "glyph:"}, // tabler login; row hidden without SDDM+Elegant
    {"idle_page", "Idle", "alarm-symbolic"},
    {"osd_page", "On-screen display", "display-brightness-symbolic"},
    {"notifications_page", "Notifications", "preferences-system-notifications-symbolic"},
    {"about_page", "About", "help-about-symbolic"},
};
constexpr int kSettingsPageCount = sizeof(kSettingsPages) / sizeof(kSettingsPages[0]);

} // namespace hyprshell
