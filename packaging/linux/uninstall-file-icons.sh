#!/usr/bin/env sh
set -eu
MODE=user
[ "${1:-}" != "--system" ] || MODE=system
if [ "$MODE" = system ]; then
    DATA_ROOT=${PUNPUN_DATA_ROOT:-/usr/share}
else
    DATA_ROOT=${XDG_DATA_HOME:-"$HOME/.local/share"}
fi
rm -f "$DATA_ROOT/mime/packages/punpun.xml"
for size in 16 32 64 128 256 512; do
    rm -f "$DATA_ROOT/icons/hicolor/${size}x${size}/mimetypes/application-x-punpun.png"
done
if command -v update-mime-database >/dev/null 2>&1; then update-mime-database "$DATA_ROOT/mime" >/dev/null 2>&1 || true; fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then gtk-update-icon-cache -f -t "$DATA_ROOT/icons/hicolor" >/dev/null 2>&1 || true; fi
printf 'PunPun file icon association removed from %s\n' "$DATA_ROOT"
