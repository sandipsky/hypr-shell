// "Users" sidebar page of hypr-shell-settings — GNOME Settings' Users panel
// without automatic login and language: the current user's picture, name and
// password, the other local accounts (name, password, administrator, remove)
// and an Add User dialog that sets the password right away.
//
// Nothing here lives in config.json. Accounts are read and changed through
// AccountsService (org.freedesktop.Accounts on the system bus, polkit prompts
// for anything beyond one's own name and picture), the same backend GNOME
// uses. A changed picture is written to ~/.face (what the shell's lock screen
// and control center read), registered with AccountsService, and — when SDDM
// is installed — copied to SDDM's FacesDir as <user>.face.icon through pkexec,
// since the greeter runs as the sddm user and cannot read a 0700 home.
#pragma once

#include <adwaita.h>

namespace hyprshell::settings {

// Returns an AdwNavigationView: the current user's page at the root, other
// users' pages pushed on demand. `window` parents dialogs.
GtkWidget* build_users_page(GtkWindow* window);

} // namespace hyprshell::settings
