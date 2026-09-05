// "VPN" sidebar page of hypr-shell-settings: NetworkManager's vpn /
// wireguard profiles — connect/disconnect switches, import (.conf / .ovpn),
// delete with confirmation. Replaces the former bar module + panel (removed
// 2026-09-05 per user); the nmcli grammar and success checks are the ones
// ported from Noctalia's VPNService.
#pragma once

#include <adwaita.h>

namespace hyprshell::settings {

// Returns a ready AdwPreferencesPage; `window` parents the file/confirm dialogs.
GtkWidget* build_vpn_page(GtkWindow* window);

} // namespace hyprshell::settings
