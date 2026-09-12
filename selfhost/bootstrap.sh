#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out="$root/build/selfhost"
mkdir -p "$out"

"$root/build/ppc" build "$root/selfhost/ppc_self.pp" -o "$out/ppc-self-stage1" --no-cache
"$out/ppc-self-stage1" "$root/selfhost/ppc_self.pp" "$out/ppc-self-stage1.c"

${CC:-cc} -std=c17 -O2 -Wall -Wextra -Werror -I"$root/runtime" \
    "$out/ppc-self-stage1.c" "$root/runtime/ppcrt.c" \
    "$root/runtime/ppc_https.c" "$root/runtime/ppc_gui.c" \
    "$root/runtime/ppc_net.c" \
    "$root/runtime/ppc_platform_posix.c" "$root/runtime/ppc_platform_windows.c" \
    -pthread -lm -ldl \
    -o "$out/ppc-self-stage2"

"$out/ppc-self-stage2" "$root/selfhost/ppc_self.pp" "$out/ppc-self-stage2.c"
cmp "$out/ppc-self-stage1.c" "$out/ppc-self-stage2.c"

"$out/ppc-self-stage2" "$root/selfhost/examples/hello.pp" "$out/hello.c"
${CC:-cc} -std=c17 -O2 -Wall -Wextra -Werror -I"$root/runtime" \
    "$out/hello.c" "$root/runtime/ppcrt.c" \
    "$root/runtime/ppc_https.c" "$root/runtime/ppc_gui.c" \
    "$root/runtime/ppc_net.c" \
    "$root/runtime/ppc_platform_posix.c" "$root/runtime/ppc_platform_windows.c" \
    -pthread -lm -ldl -o "$out/hello"
test "$("$out/hello")" = "hello from self-hosted PunPun, world"

cp "$out/ppc-self-stage2" "$out/ppc-self"
echo "self-host bootstrap fixed point verified: $out/ppc-self"
