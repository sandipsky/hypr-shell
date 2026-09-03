#pragma once

namespace hyprshell {

// Session actions — Noctalia's SessionMenu set, in its menu order. Shared
// (header-only) by the shell and hypr-shell-settings so the settings page
// lists exactly what the menus can show. Glyphs are noctalia-tabler-icons
// (\u escapes — never literal PUA).
struct SessionAction {
    const char* key;         // config key under session.items
    const char* label;
    const char* description; // settings row subtitle
    const char* glyph;
    const char* keywords;    // launcher search keywords (Noctalia's)
    const char* command;     // run via `sh -c`; empty = Hyprland's exit dispatcher
    bool destructive;        // Noctalia's isShutdown: error-tinted in the menus
    bool default_on;         // shown unless session.items.<key> says otherwise
};

constexpr SessionAction kSessionActions[] = {
    {"lock", "Lock", "Lock the screen (loginctl lock-session).", "\uEAE2",
     "lock screen secure", "loginctl lock-session", false, true},
    {"suspend", "Suspend", "Sleep, keeping the session in memory.", "\uED45",
     "suspend sleep standby", "systemctl suspend || loginctl suspend", false, true},
    {"hibernate", "Hibernate", "Save the session to disk and power off (needs swap space).",
     "\uF228", "hibernate sleep disk", "systemctl hibernate || loginctl hibernate", false,
     false},
    {"reboot", "Reboot", "Restart the computer.", "\uEB13", "reboot restart reload",
     "systemctl reboot || loginctl reboot", false, true},
    {"logout", "Logout", "End the Hyprland session.", "\uEBA8", "logout sign out exit leave",
     "", false, true},
    {"shutdown", "Shutdown", "Power off the computer.", "\uEB0D",
     "shutdown power off turn off poweroff", "systemctl poweroff || loginctl poweroff", true,
     true},
    {"reboot_uefi", "Reboot to UEFI", "Restart into the firmware (UEFI) setup.", "\uED57",
     "reboot uefi bios firmware setup",
     "systemctl reboot --firmware-setup || loginctl reboot --firmware-setup", false, false},
    {"soft_reboot", "Userspace reboot", "Restart userspace only (systemctl soft-reboot).",
     "\uEB16", "soft reboot userspace restart", "systemctl soft-reboot", false, false},
};

} // namespace hyprshell
