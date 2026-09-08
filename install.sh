#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" 2>/dev/null && pwd)
VERSION=$(tr -d '\r\n' < "$ROOT/VERSION")

if [ -z "${HOME:-}" ]; then
    echo "Punpun installer: HOME is not set" >&2
    exit 1
fi

case "$(uname -s 2>/dev/null || true)" in
    Linux) ;;
    *)
        echo "Punpun installer: this installer currently supports Linux only" >&2
        exit 1
        ;;
esac

case "$(uname -m 2>/dev/null || true)" in
    x86_64|amd64) ;;
    *)
        echo "Punpun installer: this bundle currently contains an x86-64 compiler" >&2
        exit 1
        ;;
esac

PREFIX=${PUNPUN_PREFIX:-"$HOME/.local"}
INSTALL_DIR=${PUNPUN_HOME:-"$PREFIX/share/punpun"}
BIN_DIR="$PREFIX/bin"

say() { printf '%s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }

quote_single() {
    # Print a shell-safe single-quoted string.
    printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"
}

write_wrapper() {
    name=$1
    target=$2
    tmp="$BIN_DIR/.${name}.tmp.$$"
    quoted=$(quote_single "$target")
    cat > "$tmp" <<EOF_WRAPPER
#!/usr/bin/env sh
exec $quoted "\$@"
EOF_WRAPPER
    chmod 755 "$tmp"
    mv -f "$tmp" "$BIN_DIR/$name"
}

append_path_block() {
    file=$1
    line=$2
    [ -n "$file" ] || return 0
    [ -f "$file" ] || : > "$file"
    if grep -Fq '# >>> Punpun PATH >>>' "$file" 2>/dev/null; then
        return 0
    fi
    {
        printf '\n# >>> Punpun PATH >>>\n'
        printf '%s\n' "$line"
        printf '# <<< Punpun PATH <<<\n'
    } >> "$file"
}

install_extension_dir() {
    extensions_root=$1
    [ -n "$extensions_root" ] || return 0
    mkdir -p "$extensions_root"
    destination="$extensions_root/punpun.punpun-$VERSION"
    rm -rf "$destination"
    cp -a "$INSTALL_DIR/editors/vscode" "$destination"
    printf '%s\n' "$destination"
}

install_extension_via_cli() {
    vsix="$INSTALL_DIR/dist/punpun-vscode-$VERSION.vsix"
    [ -f "$vsix" ] || return 1
    for cli in code code-insiders codium code-oss; do
        command -v "$cli" >/dev/null 2>&1 || continue
        if "$cli" --install-extension "$vsix" --force >/dev/null 2>&1; then
            printf '%s\n' "$cli --install-extension $vsix"
            return 0
        fi
    done
    return 1
}

say "Punpun $VERSION installer"
say "  project: $INSTALL_DIR"
say "  commands: $BIN_DIR"

mkdir -p "$(dirname "$INSTALL_DIR")" "$BIN_DIR"

# Copy into a sibling staging directory first so an interrupted install does not
# leave half a compiler in the final location.
stage="${INSTALL_DIR}.install.$$"
rm -rf "$stage"
mkdir -p "$stage"
cp -a "$ROOT/." "$stage/"
rm -rf "$stage/.punpun" "$stage/tests/tmp"

# Prefer rebuilding the bootstrap compiler locally when the normal development
# toolchain exists. This avoids depending on the distribution used to build the
# bundled binary. The prebuilt compiler remains a fallback for minimal systems.
if command -v make >/dev/null 2>&1 && command -v c++ >/dev/null 2>&1 && command -v cc >/dev/null 2>&1 && command -v ar >/dev/null 2>&1; then
    say "Rebuilding compiler/runtime for this machine..."
    if ! make -s -B -C "$stage" compiler; then
        rm -rf "$stage"
        echo "Punpun installer: local compiler rebuild failed" >&2
        exit 1
    fi
else
    warn "C/C++ build tools were not all found; using the bundled compiler binary"
    warn "install base-devel later if you want to rebuild the compiler itself"
fi

if [ ! -x "$stage/build/ppc" ]; then
    rm -rf "$stage"
    echo "Punpun installer: compiler binary is missing" >&2
    exit 1
fi

"$stage/build/ppc" --version >/dev/null

backup="${INSTALL_DIR}.previous.$$"
rm -rf "$backup"
if [ -e "$INSTALL_DIR" ]; then
    mv "$INSTALL_DIR" "$backup"
fi
if ! mv "$stage" "$INSTALL_DIR"; then
    [ ! -e "$backup" ] || mv "$backup" "$INSTALL_DIR"
    echo "Punpun installer: could not activate installation" >&2
    exit 1
fi
rm -rf "$backup"

# Do not symlink the shell launchers directly into ~/.local/bin. Their relative
# paths intentionally resolve from the real project root, so wrappers preserve
# access to the stdlib, editor files, and LSP server.
write_wrapper pp "$INSTALL_DIR/pp"
write_wrapper punpun "$INSTALL_DIR/punpun"
write_wrapper ppc "$INSTALL_DIR/build/ppc"
write_wrapper ppx "$INSTALL_DIR/ppx/ppx"
write_wrapper punpun-uninstall "$INSTALL_DIR/uninstall.sh"

# Make ~/.local/bin persistent for terminals. The VS Code extension additionally
# probes ~/.local/bin/pp directly, so GUI-launched Code works even before the next
# login session refreshes PATH.
path_line="export PATH=\"$BIN_DIR:\$PATH\""
append_path_block "$HOME/.profile" "$path_line"
case "${SHELL:-}" in
    */bash) append_path_block "$HOME/.bashrc" "$path_line" ;;
    */zsh)  append_path_block "$HOME/.zshrc" "$path_line" ;;
esac

fish_config="$HOME/.config/fish/config.fish"
fish_bin=$(command -v fish 2>/dev/null || true)
if [ -d "$HOME/.config/fish" ] || { [ -n "$fish_bin" ] && [ "${SHELL:-}" = "$fish_bin" ]; }; then
    append_path_block "$fish_config" "fish_add_path $(quote_single "$BIN_DIR")"
fi

say "Installing VS Code language support..."
installed_extensions=""
if installed_extensions=$(install_extension_via_cli); then
    say "VS Code extension installed through editor CLI."
else
    installed_extensions="$(install_extension_dir "$HOME/.vscode/extensions")"
    for candidate in \
        "$HOME/.vscode-oss/extensions" \
        "$HOME/.vscode-server/extensions" \
        "$HOME/.var/app/com.visualstudio.code/data/vscode/extensions" \
        "$HOME/.var/app/com.vscodium.codium/data/codium/extensions"
    do
        parent=$(dirname "$candidate")
        if [ -d "$candidate" ] || [ -d "$parent" ]; then
            install_extension_dir "$candidate" >/dev/null
        fi
    done
fi

# Remove the old development symlink only when it points into this installed
# tree. Never delete an unrelated extension just because its name is similar.
legacy="$HOME/.vscode/extensions/punpun-local"
if [ -L "$legacy" ]; then
    target=$(readlink "$legacy" 2>/dev/null || true)
    case "$target" in
        "$INSTALL_DIR"/*) rm -f "$legacy" ;;
    esac
fi

# End-to-end smoke test when the native linker/compiler is available.
if command -v cc >/dev/null 2>&1; then
    smoke=$(mktemp -d "${TMPDIR:-/tmp}/punpun-install.XXXXXX")
    trap 'rm -rf "$smoke"' EXIT HUP INT TERM
    cat > "$smoke/main.pp" <<'EOF_PP'
fn main() {
    println("Punpun installed");
}
EOF_PP
    if ! (cd "$smoke" && "$INSTALL_DIR/pp" build main.pp -o hello >/dev/null && ./hello >/dev/null); then
        echo "Punpun installer: end-to-end native compilation smoke test failed" >&2
        exit 1
    fi
    rm -rf "$smoke"
    trap - EXIT HUP INT TERM
else
    warn "cc is not installed, so native program linking could not be smoke-tested"
fi

say ""
say "Installed Punpun $VERSION successfully."
say ""
say "Commands:"
say "  pp --version"
say "  pp new hello"
say "  pp build"
say "  pp run"
say "  pp lsp"
say "  ppx --version"
say ""
say "VS Code extension: $installed_extensions"
say "Reload VS Code if it is currently open."
say ""
say "This terminal may need either:"
say "  export PATH=\"$BIN_DIR:\$PATH\""
say "or simply a new terminal window."
say ""
say "Uninstall with: punpun-uninstall"
