#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, os, shutil, stat, subprocess, tarfile, tempfile, time, zipfile
from pathlib import Path
from versioning import PKGVER, VERSION

ROOT=Path(__file__).resolve().parents[1]
TARGET='linux-x86_64'
RELEASE=ROOT/'dist'/f'release-{VERSION}'
SOURCE_DATE_EPOCH=int(os.environ.get('SOURCE_DATE_EPOCH','1788753600'))


def run(cmd,cwd=ROOT,**kw):
    print('+',' '.join(map(str,cmd)))
    return subprocess.run(list(map(str,cmd)),cwd=cwd,check=True,**kw)

def sha(path:Path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''): h.update(b)
    return h.hexdigest()

def executable(path:Path):
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

def zip_tree(source:Path,dest:Path,arcroot:str|None=None):
    with zipfile.ZipFile(dest,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for p in sorted(source.rglob('*')):
            if p.is_file():
                rel=p.relative_to(source).as_posix()
                name=f'{arcroot}/{rel}' if arcroot else rel
                info=zipfile.ZipInfo(name,time.gmtime(SOURCE_DATE_EPOCH)[:6]); info.external_attr=(p.stat().st_mode & 0xffff)<<16
                z.writestr(info,p.read_bytes(),compress_type=zipfile.ZIP_DEFLATED,compresslevel=9)

def copy_clean_source(dst:Path):
    # Source/publication copies are rebuilt from an allow-source tree rather than
    # inheriting whatever a developer happened to generate locally.
    ignored_dirs={
        '.git','.punpun','build','dist','__pycache__','.pytest_cache','.mypy_cache',
        '.ruff_cache','.idea','node_modules','__MACOSX','.ppx-registry','htmlcov'
    }
    ignored_names={'.DS_Store','Thumbs.db','desktop.ini','.coverage'}
    ignored_suffixes={'.pyc','.tmp','.swp','.swo','.o','.a','.so','.dll','.exe'}
    def ignore(path,names):
        out=[]
        for n in names:
            candidate=Path(n)
            if n in ignored_dirs or n in ignored_names or n.endswith('~') or candidate.suffix.lower() in ignored_suffixes:
                out.append(n)
        return out
    shutil.copytree(ROOT,dst,ignore=ignore)

def copy_part(src:Path,dst:Path):
    if not src.exists(): return
    if src.is_dir(): shutil.copytree(src,dst,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','*.pyc','.punpun'))
    else:
        dst.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(src,dst)

def make_sdk(stage:Path):
    sdk=stage/f'PunPun-{VERSION}-{TARGET}'
    sdk.mkdir(parents=True)
    for f in ('VERSION','punpun','pp','README.md','LICENSE','CHANGELOG.md','ROADMAP.md','PROJECT_STATUS.txt','COMPLETION_REPORT.md','PUBLISHING.md','RELEASE_NOTES.md','publish-punpun.sh'):
        copy_part(ROOT/f,sdk/f)
    for d in ('runtime','stdlib','packages','ppx','tooling','editors','docs','spec','assets','packaging','gui-maker','selfhost'):
        copy_part(ROOT/d,sdk/d)
    # Built docs are consumer-facing; source stays in source/full bundle.
    copy_part(ROOT/'docs-site'/'dist',sdk/'docs-site'/'dist')
    copy_part(ROOT/'ppx-site'/'dist',sdk/'ppx-site'/'dist')
    (sdk/'dist').mkdir(exist_ok=True)
    copy_part(ROOT/'dist'/f'punpun-vscode-{VERSION}.vsix',sdk/'dist'/f'punpun-vscode-{VERSION}.vsix')
    (sdk/'bin').mkdir()
    shutil.copy2(ROOT/'build'/'ppc',sdk/'bin'/'ppc'); executable(sdk/'bin'/'ppc')
    selfhost=ROOT/'build'/'selfhost'/'ppc-self'
    if selfhost.is_file():
        shutil.copy2(selfhost,sdk/'bin'/'ppc-self'); executable(sdk/'bin'/'ppc-self')
    wrappers={
        'pp':'ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)\nexport PATH="$ROOT/bin:$PATH"\nexec "$ROOT/punpun" "$@"',
        'punpun':'ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)\nexport PATH="$ROOT/bin:$PATH"\nexec "$ROOT/punpun" "$@"',
        'ppx':'ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)\nexec "$ROOT/ppx/ppx" "$@"',
        'punpun-lsp':'ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)\nexport PUNPUN_PPC="$ROOT/bin/ppc"\nexec node "$ROOT/tooling/lsp/server.js" "$@"',
    }
    for name,body in wrappers.items():
        p=sdk/'bin'/name; p.write_text('#!/usr/bin/env sh\nset -eu\n'+body+'\n'); executable(p)
    (sdk/'VERSION').write_text(VERSION+'\n')
    return sdk

def make_self_extractor(sdk:Path,out:Path):
    with tempfile.NamedTemporaryFile(suffix='.tar.gz',delete=False) as t: payload=Path(t.name)
    try:
        with tarfile.open(payload,'w:gz',compresslevel=9) as tf: tf.add(sdk,arcname=sdk.name)
        header=rf'''#!/usr/bin/env sh
set -eu
VERSION="{VERSION}"
[ "$(uname -s 2>/dev/null || true)" = Linux ] || {{ echo "PunPun installer: Linux required" >&2; exit 1; }}
case "$(uname -m 2>/dev/null || true)" in x86_64|amd64) ;; *) echo "PunPun installer: x86-64 required" >&2; exit 1;; esac
[ -n "${{HOME:-}}" ] || {{ echo "PunPun installer: HOME is not set" >&2; exit 1; }}
PREFIX=${{PUNPUN_PREFIX:-"$HOME/.local"}}
DEST=${{PUNPUN_HOME:-"$PREFIX/share/punpun"}}
BIN="$PREFIX/bin"
tmp=$(mktemp -d "${{TMPDIR:-/tmp}}/punpun-installer.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
line=$(awk '/^__PUNPUN_PAYLOAD_BELOW__$/ {{print NR+1; exit}}' "$0")
tail -n +"$line" "$0" | tar -xzf - -C "$tmp"
src="$tmp/{sdk.name}"
[ -x "$src/bin/ppc" ] || {{ echo "PunPun installer: corrupt SDK payload" >&2; exit 1; }}
backup="$DEST.previous.$$"; rm -rf "$backup"; mkdir -p "$(dirname "$DEST")" "$BIN"
[ ! -e "$DEST" ] || mv "$DEST" "$backup"
if ! mv "$src" "$DEST"; then [ ! -e "$backup" ] || mv "$backup" "$DEST"; exit 1; fi
rm -rf "$backup"
for n in pp ppc ppx punpun punpun-lsp; do
  cat > "$BIN/$n" <<EOF
#!/usr/bin/env sh
exec "$DEST/bin/$n" "\$@"
EOF
  chmod 755 "$BIN/$n"
done
cat > "$BIN/punpun-uninstall" <<EOF
#!/usr/bin/env sh
set -eu
[ ! -x "$DEST/packaging/linux/uninstall-file-icons.sh" ] || "$DEST/packaging/linux/uninstall-file-icons.sh" >/dev/null 2>&1 || true
rm -rf "$DEST"
for n in pp ppc ppx punpun punpun-lsp punpun-uninstall; do rm -f "$BIN/\$n"; done
echo "PunPun $VERSION removed; user projects and PPX cache were kept."
EOF
chmod 755 "$BIN/punpun-uninstall"
if ! printf '%s' ":$PATH:" | grep -Fq ":$BIN:"; then
  touch "$HOME/.profile"
  grep -Fq '# >>> PunPun PATH >>>' "$HOME/.profile" || printf '\n# >>> PunPun PATH >>>\nexport PATH="%s:$PATH"\n# <<< PunPun PATH <<<\n' "$BIN" >> "$HOME/.profile"
fi
if [ -x "$DEST/packaging/linux/install-file-icons.sh" ]; then
  "$DEST/packaging/linux/install-file-icons.sh" >/dev/null 2>&1 || true
fi
vsix="$DEST/dist/punpun-vscode-{VERSION}.vsix"
for cli in code code-insiders codium code-oss; do command -v "$cli" >/dev/null 2>&1 || continue; "$cli" --install-extension "$vsix" --force >/dev/null 2>&1 || true; break; done
"$DEST/bin/ppc" --version
if command -v cc >/dev/null 2>&1; then
  smoke="$tmp/smoke"; mkdir -p "$smoke"; cat > "$smoke/main.pp" <<'EOF'
launch {{ say("PunPun installed"); }}
EOF
  (cd "$smoke" && "$DEST/bin/pp" build main.pp -o hello >/dev/null && ./hello >/dev/null) || {{ echo "PunPun installer: native smoke test failed" >&2; exit 1; }}
fi
echo "Installed PunPun $VERSION -> $DEST"
echo "Open a new terminal or: export PATH=\"$BIN:\$PATH\""
exit 0
__PUNPUN_PAYLOAD_BELOW__
'''
        out.write_bytes(header.encode()+payload.read_bytes()); executable(out)
    finally: payload.unlink(missing_ok=True)

def make_pkgbuild(release:Path,sdk_tar_name:str,sdk_sha:str):
    d=ROOT/'packaging'/'arch'; d.mkdir(parents=True,exist_ok=True)
    text=f'''pkgname=punpun
pkgver={PKGVER}
pkgrel=1
pkgdesc="PunPun native programming language SDK"
arch=('x86_64')
url="https://example.invalid/punpun"
license=('MIT')
depends=('glibc' 'gcc-libs' 'python' 'nodejs' 'shared-mime-info' 'hicolor-icon-theme')
optdepends=('base-devel: rebuild compiler and use C injection' 'libcurl: requests package' 'libx11: PunUI Linux backend')
source=('{sdk_tar_name}')
sha256sums=('{sdk_sha}')

package() {{
  mkdir -p "$pkgdir/usr/lib/punpun" "$pkgdir/usr/bin" "$pkgdir/usr/share/licenses/punpun" "$pkgdir/usr/share/doc/punpun"
  cp -a "$srcdir/PunPun-{VERSION}-{TARGET}/." "$pkgdir/usr/lib/punpun/"
  for name in pp ppc ppx punpun punpun-lsp; do
    printf '#!/bin/sh\nexec /usr/lib/punpun/bin/%s "$@"\n' "$name" > "$pkgdir/usr/bin/$name"
    chmod 755 "$pkgdir/usr/bin/$name"
  done
  cp "$pkgdir/usr/lib/punpun/LICENSE" "$pkgdir/usr/share/licenses/punpun/LICENSE"
  cp "$pkgdir/usr/lib/punpun/README.md" "$pkgdir/usr/share/doc/punpun/README.md"
  install -Dm644 "$pkgdir/usr/lib/punpun/packaging/linux/application-x-punpun.xml" "$pkgdir/usr/share/mime/packages/punpun.xml"
  for size in 16 32 64 128 256 512; do
    install -Dm644 "$pkgdir/usr/lib/punpun/assets/punpun-icon-$size.png" "$pkgdir/usr/share/icons/hicolor/${{size}}x${{size}}/mimetypes/application-x-punpun.png"
  done
}}
'''
    (d/'PKGBUILD').write_text(text)
    shutil.copy2(d/'PKGBUILD',release/'PKGBUILD')

def make_arch_package(sdk:Path,out:Path):
    with tempfile.TemporaryDirectory() as td:
        root=Path(td)
        lib=root/'usr/lib/punpun'; lib.mkdir(parents=True); shutil.copytree(sdk,lib,dirs_exist_ok=True)
        (root/'usr/bin').mkdir(parents=True)
        for name in ('pp','ppc','ppx','punpun','punpun-lsp'):
            p=root/'usr/bin'/name; p.write_text(f'#!/bin/sh\nexec /usr/lib/punpun/bin/{name} "$@"\n'); executable(p)
        lic=root/'usr/share/licenses/punpun'; lic.mkdir(parents=True); shutil.copy2(sdk/'LICENSE',lic/'LICENSE')
        doc=root/'usr/share/doc/punpun'; doc.mkdir(parents=True); shutil.copy2(sdk/'README.md',doc/'README.md')
        mime=root/'usr/share/mime/packages'; mime.mkdir(parents=True); shutil.copy2(sdk/'packaging/linux/application-x-punpun.xml',mime/'punpun.xml')
        for icon_size in (16,32,64,128,256,512):
            icon_dir=root/f'usr/share/icons/hicolor/{icon_size}x{icon_size}/mimetypes'; icon_dir.mkdir(parents=True)
            shutil.copy2(sdk/f'assets/punpun-icon-{icon_size}.png',icon_dir/'application-x-punpun.png')
        size=sum(p.stat().st_size for p in root.rglob('*') if p.is_file())
        (root/'.PKGINFO').write_text(f'pkgname = punpun\npkgbase = punpun\npkgver = {PKGVER}-1\npkgdesc = PunPun native programming language SDK\nurl = https://example.invalid/punpun\nbuilddate = {SOURCE_DATE_EPOCH}\npackager = PunPun Project\nsize = {size}\narch = x86_64\nlicense = MIT\ndepend = glibc\ndepend = gcc-libs\ndepend = python\ndepend = nodejs\ndepend = shared-mime-info\ndepend = hicolor-icon-theme\n')
        # GNU tar + zstd produces the package payload format; .MTREE is omitted on this host because libarchive/makepkg are unavailable.
        subprocess.run(['tar','--zstd','-cf',str(out),'-C',str(root),'.'],check=True)

def make_publisher_bundle():
    destination=RELEASE/f'PunPun-{VERSION}-publisher.zip'
    with tempfile.TemporaryDirectory() as td:
        bundle=Path(td)/f'PunPun-{VERSION}-publisher'
        groups={name:bundle/name for name in ('source','linux','arch','editor','websites','windows','reports')}
        for directory in groups.values(): directory.mkdir(parents=True)
        copies={
            ROOT/'VERSION':bundle/'VERSION',
            ROOT/'PUBLISHING.md':bundle/'PUBLISHING.md',
            ROOT/'RELEASE_NOTES.md':bundle/'RELEASE_NOTES.md',
            ROOT/'publish-punpun.sh':bundle/'publish-punpun.sh',
            RELEASE/f'PunPun-{VERSION}-source.zip':groups['source']/f'PunPun-{VERSION}-source.zip',
            RELEASE/f'PunPun-{VERSION}-{TARGET}-SDK.zip':groups['linux']/f'PunPun-{VERSION}-{TARGET}-SDK.zip',
            RELEASE/f'PunPun-{VERSION}-{TARGET}.tar.zst':groups['linux']/f'PunPun-{VERSION}-{TARGET}.tar.zst',
            RELEASE/f'PunPun-{VERSION}-Linux-x86_64-Installer.run':groups['linux']/f'PunPun-{VERSION}-Linux-x86_64-Installer.run',
            RELEASE/f'punpun-{PKGVER}-1-x86_64.pkg.tar.zst':groups['arch']/f'punpun-{PKGVER}-1-x86_64.pkg.tar.zst',
            RELEASE/'PKGBUILD':groups['arch']/'PKGBUILD',
            RELEASE/f'punpun-vscode-{VERSION}.vsix':groups['editor']/f'punpun-vscode-{VERSION}.vsix',
            RELEASE/f'PunPun-{VERSION}-docs-site.zip':groups['websites']/f'PunPun-{VERSION}-docs-site.zip',
            RELEASE/f'PunPun-{VERSION}-ppx-site.zip':groups['websites']/f'PunPun-{VERSION}-ppx-site.zip',
            RELEASE/f'PunPun-{VERSION}-windows-installer-source.zip':groups['windows']/f'PunPun-{VERSION}-windows-installer-source.zip',
            RELEASE/'RELEASE_VALIDATION.md':groups['reports']/'RELEASE_VALIDATION.md',
            RELEASE/'release-manifest.json':groups['reports']/'release-manifest.json',
        }
        for source,target in copies.items():
            if not source.is_file(): raise RuntimeError(f'publisher input missing: {source}')
            shutil.copy2(source,target)
        (bundle/'PRIVACY_AUDIT.md').write_text(
            '# Release privacy audit\n\n'
            'The source audit passed before packaging. No account name, email address, '
            'user home/workspace path, access token, private key, environment file, cache, '
            'or editor backup is included. Publication commands obtain the GitHub account '
            'from the authenticated `gh` session instead of hardcoding a username.\n',encoding='utf-8')
        checksum_lines=[]
        for path in sorted(bundle.rglob('*')):
            if path.is_file() and path.name!='SHA256SUMS':
                checksum_lines.append(f'{sha(path)}  {path.relative_to(bundle).as_posix()}')
        (bundle/'SHA256SUMS').write_text('\n'.join(checksum_lines)+'\n',encoding='utf-8')
        zip_tree(bundle,destination,bundle.name)
    return destination

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--skip-tests',action='store_true'); args=ap.parse_args()
    if RELEASE.exists(): shutil.rmtree(RELEASE)
    RELEASE.mkdir(parents=True)
    run(['python3','scripts/sync_version.py'])
    run(['make','-s','-B','compiler'])
    run(['python3','scripts/check_version.py'])
    run(['python3','scripts/privacy_audit.py',str(ROOT)])
    run(['./selfhost/bootstrap.sh'])
    run(['python3','scripts/package_vsix.py'])
    run(['python3','docs-site/build.py']); run(['python3','ppx-site/build.py'])
    if not args.skip_tests: run(['./tests/run.sh'])
    with tempfile.TemporaryDirectory() as td:
        stage=Path(td)
        sdk=make_sdk(stage)
        sdk_zip=RELEASE/f'PunPun-{VERSION}-{TARGET}-SDK.zip'; zip_tree(sdk,sdk_zip,sdk.name)
        sdk_tarz=RELEASE/f'PunPun-{VERSION}-{TARGET}.tar.zst'
        subprocess.run(['tar','--zstd','-cf',str(sdk_tarz),'-C',str(stage),sdk.name],check=True)
        installer=RELEASE/f'PunPun-{VERSION}-Linux-x86_64-Installer.run'; make_self_extractor(sdk,installer)
        make_pkgbuild(RELEASE,sdk_tarz.name,sha(sdk_tarz))
        archpkg=RELEASE/f'punpun-{PKGVER}-1-x86_64.pkg.tar.zst'; make_arch_package(sdk,archpkg)
    # Source archive from a clean tree, and a separate full engineering bundle.
    with tempfile.TemporaryDirectory() as td:
        src=Path(td)/f'PunPun-{VERSION}-source'; copy_clean_source(src)
        source_zip=RELEASE/f'PunPun-{VERSION}-source.zip'; zip_tree(src,source_zip,src.name)
    # Deploy-ready static sites plus the Windows installer project. All other
    # source already lives in the single authoritative source archive.
    zip_tree(ROOT/'docs-site'/'dist',RELEASE/f'PunPun-{VERSION}-docs-site.zip','')
    zip_tree(ROOT/'ppx-site'/'dist',RELEASE/f'PunPun-{VERSION}-ppx-site.zip','')
    zip_tree(ROOT/'installers'/'windows',RELEASE/f'PunPun-{VERSION}-windows-installer-source.zip',f'PunPun-{VERSION}-windows-installer')
    shutil.copy2(ROOT/'dist'/f'punpun-vscode-{VERSION}.vsix',RELEASE/f'punpun-vscode-{VERSION}.vsix')

    # Validate the artifacts that can actually execute on this host before
    # declaring the release assembled. This includes installing the exact .run
    # payload into an isolated HOME and reproducing the stale-build test there.
    run(['python3','scripts/validate_release.py',str(RELEASE)])

    # Full engineering bundle: clean source plus the generated consumer/package
    # artifacts. It intentionally cannot contain itself, SHA256SUMS, or the
    # final manifest because those are generated after the bundle checksum is
    # known.
    with tempfile.TemporaryDirectory() as td:
        full=Path(td)/f'PunPun-{VERSION}-full'
        source_tree=full/f'PunPun-{VERSION}-source'
        copy_clean_source(source_tree)
        artifacts=full/'release-artifacts'; artifacts.mkdir(parents=True)
        for item in sorted(RELEASE.iterdir()):
            if item.is_file() and item.name not in {f'PunPun-{VERSION}-full.zip','SHA256SUMS','release-manifest.json'}:
                shutil.copy2(item,artifacts/item.name)
        zip_tree(full,RELEASE/f'PunPun-{VERSION}-full.zip',full.name)

    # Validation source files and Windows build project are included, but no fake Windows binary is emitted.
    statuses=[]
    for p in sorted(RELEASE.iterdir()):
        if p.is_file() and p.name not in {'SHA256SUMS','release-manifest.json','RELEASE_VALIDATION.md'}:
            statuses.append({'filename':p.name,'sha256':sha(p),'size':p.stat().st_size,'type':p.suffix.lstrip('.') or 'file'})
    manifest={'product':'PunPun','version':VERSION,'host_target':'x86_64-unknown-linux-gnu','artifacts':statuses,
              'validation_report':'RELEASE_VALIDATION.md',
              'unbuilt':{'windows_msi':'WiX source complete; not built/tested on Linux host','windows_setup_exe':'WiX Burn source complete; not built/tested on Linux host','windows_portable_sdk':'Windows compiler payload unavailable on host'}}
    (RELEASE/'release-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    checks=[]
    for p in sorted(RELEASE.iterdir()):
        if p.is_file() and p.name!='SHA256SUMS': checks.append(f'{sha(p)}  {p.name}')
    (RELEASE/'SHA256SUMS').write_text('\n'.join(checks)+'\n')
    publisher=make_publisher_bundle()
    print('publisher bundle:',publisher)
    print('release assembled:',RELEASE)

if __name__=='__main__': main()
