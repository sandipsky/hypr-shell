#!/usr/bin/env bash
# hypr-shell — uninstall.
#
#   ./uninstall.sh           stop + remove the installed binary
#   ./uninstall.sh --purge   also delete ~/.config/hypr-shell

set -euo pipefail
cd "$(dirname "$(realpath "$0")")"

PREFIX="${PREFIX:-$HOME/.local}"

if pgrep -x hypr-shell >/dev/null; then
    echo ":: Stopping running hypr-shell"
    pkill -x hypr-shell || true
fi

if [[ -f build/build.ninja ]]; then
    echo ":: Removing installed files (ninja uninstall)"
    ninja -C build uninstall >/dev/null
else
    echo ":: Removing $PREFIX/bin/hypr-shell"
    rm -f "$PREFIX/bin/hypr-shell"
fi
rm -rf "$PREFIX/share/fonts/hypr-shell"

if [[ "${1:-}" == "--purge" ]]; then
    echo ":: Removing ~/.config/hypr-shell"
    rm -rf "$HOME/.config/hypr-shell"
fi

echo "Done. If you added an exec-once line to hyprland.conf, remove it manually."
