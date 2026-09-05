// "About" sidebar page of hypr-shell-settings: hardware and software facts
// laid out like GNOME Settings' About panel (property rows, no distro logo —
// per user). Everything is read locally (sysfs, /proc, os-release); only the
// Hyprland version comes from an async `hyprctl -j version`.
#pragma once

#include <adwaita.h>

namespace hyprshell::settings {

// Returns an AdwPreferencesPage whose rows are filled from a low-priority
// idle callback (after the first frame), so gathering the facts never delays
// the window.
GtkWidget* build_about_page();

} // namespace hyprshell::settings
