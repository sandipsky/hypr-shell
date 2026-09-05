#!/usr/bin/env bash
# hypr-shell — build & install (Arch Linux).
#
#   ./install.sh            install deps (pacman), build, install to ~/.local
#   ./install.sh --restart  same, then restart a running hypr-shell
#   PREFIX=/some/path ./install.sh   custom prefix (needs write access)

set -euo pipefail
cd "$(dirname "$(realpath "$0")")"

PREFIX="${PREFIX:-$HOME/.local}"
DEPS=(gcc pkgconf meson ninja glib2 glib2-devel gtk4 gtk4-layer-shell gtkmm-4.0 nlohmann-json libpulse libadwaita wayland wayland-protocols pam)

info() { printf '\033[1;34m::\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32mok\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

command -v pacman >/dev/null || die "only Arch Linux (pacman) is supported for now"

missing=()
for pkg in "${DEPS[@]}"; do
    pacman -Qq "$pkg" &>/dev/null || missing+=("$pkg")
done
if ((${#missing[@]})); then
    info "Installing dependencies: ${missing[*]}"
    sudo pacman -S --needed "${missing[@]}"
else
    ok "dependencies already installed"
fi

if [[ ! -d build ]]; then
    info "Configuring (prefix: $PREFIX)"
    meson setup build --prefix="$PREFIX" --buildtype=release
fi

info "Building"
meson compile -C build

info "Installing"
meson install -C build >/dev/null
# icon font (wifi/volume glyphs) lands in $PREFIX/share/fonts — refresh the cache
fc-cache -f "$PREFIX/share/fonts/hypr-shell" >/dev/null 2>&1 || true
# A stale icon-theme.cache in ~/.local/share/icons/hicolor hides newly
# installed icons (GTK trusts the cache over the directory) — refresh it.
if [ -f "$PREFIX/share/icons/hicolor/icon-theme.cache" ]; then
    gtk-update-icon-cache -q -f -t "$PREFIX/share/icons/hicolor" 2>/dev/null || true
fi

ok "installed: $PREFIX/bin/hypr-shell"

case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *) info "note: $PREFIX/bin is not in your PATH — use the full path below" ;;
esac

if pgrep -x hypr-shell >/dev/null; then
    if [[ "${1:-}" == "--restart" ]]; then
        info "Restarting running instance"
        pkill -x hypr-shell
        sleep 0.3
        setsid -f "$PREFIX/bin/hypr-shell" >/dev/null 2>&1
        ok "restarted"
    else
        info "hypr-shell is running — apply the new build with: ./install.sh --restart"
    fi
fi

cat <<EOF

Try it now:   $PREFIX/bin/hypr-shell &
Autostart:    add to hyprland.conf:  exec-once = $PREFIX/bin/hypr-shell
Settings:     $PREFIX/bin/hypr-shell-settings (applies live via config.json)
Custom CSS:   ~/.config/hypr-shell/style.css (hot-reloaded while running)
Uninstall:    ./uninstall.sh [--purge]
EOF
