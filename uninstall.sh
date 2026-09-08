#!/usr/bin/env sh
set -eu

if [ -z "${HOME:-}" ]; then
    echo "Punpun uninstaller: HOME is not set" >&2
    exit 1
fi

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" 2>/dev/null && pwd)
PREFIX=${PUNPUN_PREFIX:-"$HOME/.local"}
BIN_DIR="$PREFIX/bin"
VERSION="0.5.0-beta"

remove_path_block() {
    file=$1
    [ -f "$file" ] || return 0
    tmp="${file}.punpun-remove.$$"
    awk '
        $0 == "# >>> Punpun PATH >>>" { skip=1; next }
        $0 == "# <<< Punpun PATH <<<" { skip=0; next }
        !skip { print }
    ' "$file" > "$tmp"
    mv "$tmp" "$file"
}

for name in pp punpun ppc ppx punpun-uninstall; do
    file="$BIN_DIR/$name"
    [ -f "$file" ] || continue
    if grep -Fq "$ROOT" "$file" 2>/dev/null; then
        rm -f "$file"
    fi
done

for cli in code code-insiders codium code-oss; do
    if command -v "$cli" >/dev/null 2>&1; then
        "$cli" --uninstall-extension punpun.punpun >/dev/null 2>&1 || true
    fi
done

for destination in \
    "$HOME/.vscode/extensions/punpun.punpun-$VERSION" \
    "$HOME/.vscode-oss/extensions/punpun.punpun-$VERSION" \
    "$HOME/.vscode-server/extensions/punpun.punpun-$VERSION" \
    "$HOME/.var/app/com.visualstudio.code/data/vscode/extensions/punpun.punpun-$VERSION" \
    "$HOME/.var/app/com.vscodium.codium/data/codium/extensions/punpun.punpun-$VERSION"
do
    if [ -f "$destination/package.json" ] && grep -q '"name"[[:space:]]*:[[:space:]]*"punpun"' "$destination/package.json"; then
        rm -rf "$destination"
    fi
done

legacy="$HOME/.vscode/extensions/punpun-local"
if [ -L "$legacy" ]; then
    target=$(readlink "$legacy" 2>/dev/null || true)
    case "$target" in "$ROOT"/*) rm -f "$legacy" ;; esac
fi

remove_path_block "$HOME/.profile"
remove_path_block "$HOME/.bashrc"
remove_path_block "$HOME/.zshrc"
remove_path_block "$HOME/.config/fish/config.fish"

rm -rf "$ROOT"
printf '%s\n' "Punpun $VERSION was removed. Reload VS Code and open a new terminal to refresh the environment."
