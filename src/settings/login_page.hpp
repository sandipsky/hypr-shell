// "Login screen" sidebar page of hypr-shell-settings: options for the Elegant
// SDDM theme (the dotfiles' login screen in the style of the lock screen).
//
// Nothing here lives in config.json — the greeter runs as the sddm user and
// reads /usr/share/sddm/themes/Elegant/theme.conf.user, so the page reads
// that file (over the theme's theme.conf defaults) and writes it back through
// pkexec on an explicit Apply, together with a copy of the chosen background
// image (the sddm user can't read a 0700 home). The sidebar row is hidden
// unless SDDM with this theme is installed and selected.
#pragma once

#include <adwaita.h>

#include <string>

namespace hyprshell::settings {

// sddm on PATH, the Elegant theme present, and selected as SDDM's current
// theme (Current=Elegant in /etc/sddm.conf{,.d} or the packaged defaults).
bool login_page_available();

// Returns a ready AdwPreferencesPage. `window` parents the file dialog.
GtkWidget* build_login_page(GtkWindow* window);

// `key` from `[section]` across SDDM's config files — the packaged defaults
// under /usr/lib/sddm/sddm.conf.d, /etc/sddm.conf, /etc/sddm.conf.d — with
// the last file that sets it winning, like SDDM. "" when unset. Shared with
// the Users page (FacesDir).
std::string sddm_config_value(const char* section, const char* key);

} // namespace hyprshell::settings
