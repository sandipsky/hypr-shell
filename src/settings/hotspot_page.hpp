// "Hotspot" sidebar page of hypr-shell-settings: share the internet
// connection over Wi-Fi through NetworkManager's AP mode.
//
// Everything lives in NetworkManager (the single "Hotspot" profile, owned by
// the current user) rather than config.json: the hotspot is live network
// state, like the Wi-Fi list in the bar's network panel, and nmcli already
// persists the name, password, security, band and interface. The page reads
// the profile back with `nmcli --show-secrets` and writes changes with
// `nmcli connection modify`, all asynchronously through GSubprocess.
#pragma once

#include <adwaita.h>

namespace hyprshell::settings {

// Returns a ready AdwPreferencesPage. `window` parents nothing today; kept
// for future dialogs.
GtkWidget* build_hotspot_page(GtkWindow* window);

} // namespace hyprshell::settings
