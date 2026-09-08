#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
MODE=user
[ "${1:-}" != "--system" ] || MODE=system

if [ "$MODE" = system ]; then
    DATA_ROOT=${PUNPUN_DATA_ROOT:-/usr/share}
else
    DATA_ROOT=${XDG_DATA_HOME:-"$HOME/.local/share"}
fi

MIME_PACKAGES="$DATA_ROOT/mime/packages"
ICON_ROOT="$DATA_ROOT/icons/hicolor"
mkdir -p "$MIME_PACKAGES"
cp "$SCRIPT_DIR/application-x-punpun.xml" "$MIME_PACKAGES/punpun.xml"

for size in 16 32 64 128 256 512; do
    target="$ICON_ROOT/${size}x${size}/mimetypes"
    mkdir -p "$target"
    cp "$ROOT/assets/punpun-icon-${size}.png" "$target/application-x-punpun.png"
done

if command -v update-mime-database >/dev/null 2>&1; then
    update-mime-database "$DATA_ROOT/mime" >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$ICON_ROOT" >/dev/null 2>&1 || true
fi

printf 'PunPun file icon association installed in %s\n' "$DATA_ROOT"
printf 'MIME: application/x-punpun (*.pp)\n'
