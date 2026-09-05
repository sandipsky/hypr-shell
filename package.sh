#!/usr/bin/env bash
# hypr-shell — pack the committed tree into a source tarball for the dotfiles repo.
#
#   ./package.sh                 writes dist/hypr-shell.tar.gz
#   ./package.sh out.tar.gz      writes there instead
#
# Uses `git archive` of HEAD, so only committed files are packed (never build/);
# commit first. Copy the result to the dotfiles repo
# (applications/hypr-shell/hypr-shell.tar.gz), whose install.sh extracts it
# and runs its install.sh.

set -euo pipefail
cd "$(dirname "$(realpath "$0")")"

OUT="${1:-dist/hypr-shell.tar.gz}"

if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
    echo "warning: uncommitted changes are NOT included (git archive packs HEAD)" >&2
fi

mkdir -p "$(dirname "$OUT")"
git archive --format=tar.gz --prefix=hypr-shell/ -o "$OUT" HEAD
echo "$OUT <- $(git log --oneline -1)"
