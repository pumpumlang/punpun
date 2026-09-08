#!/usr/bin/env python3
"""PunPunXPac (PPX) beta package client.

PPX deliberately uses the same Punpun.toml/Punpun.lock model as `pp`. Local and
registry packages are materialized as ordinary path dependencies before the
compiler sees them, so there is one resolver/build graph rather than a second
compiler path.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import zipfile

VERSION = (Path(__file__).resolve().parents[1] / "VERSION").read_text(encoding="utf-8").strip()
DEFAULT_REGISTRY = "http://127.0.0.1:8765"
NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_-]{0,63}$")
SEMVER_RE = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z.-]+))?$")


def cache_root() -> Path:
    if os.name == "nt":
        base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
        return base / "PunPun" / "PPX"
    return Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "ppx"


def state_root() -> Path:
    if os.name == "nt":
        base = Path(os.environ.get("APPDATA", Path.home() / "AppData" / "Roaming"))
        return base / "PunPun" / "PPX"
    return Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "ppx"


def bundled_packages() -> Path:
    override = os.environ.get("PUNPUN_PACKAGES")
    if override:
        return Path(override)
    return Path(__file__).resolve().parents[1] / "packages"


def registry_url() -> str:
    return os.environ.get("PPX_REGISTRY", DEFAULT_REGISTRY).rstrip("/")


def pp_command() -> str:
    return os.environ.get("PUNPUN_PP", shutil.which("pp") or str(Path(__file__).resolve().parents[1] / "pp"))


def run_pp(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run([pp_command(), *args], text=True, check=check)


def api(path: str, *, method: str = "GET", payload=None, token: str | None = None, timeout: float = 10.0) -> bytes:
    data = None
    headers = {"Accept": "application/json", "User-Agent": f"PPX/{VERSION}"}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    if token:
        headers["Authorization"] = "Bearer " + token
    req = urllib.request.Request(registry_url() + path, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            return response.read()
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", "replace")
        try:
            detail = json.loads(detail).get("error", detail)
        except Exception:
            pass
        raise SystemExit(f"ppx: registry error {exc.code}: {detail}")
    except urllib.error.URLError as exc:
        raise SystemExit(f"ppx: cannot reach registry {registry_url()}: {exc.reason}")


def api_json(path: str, **kwargs):
    return json.loads(api(path, **kwargs).decode("utf-8"))


def parse_semver(value: str):
    match = SEMVER_RE.fullmatch(value)
    if not match:
        raise ValueError(f"invalid semantic version: {value}")
    major, minor, patch = map(int, match.group(1, 2, 3))
    prerelease = match.group(4)
    return major, minor, patch, prerelease


def version_key(value: str):
    major, minor, patch, prerelease = parse_semver(value)
    # Stable versions sort after prereleases of the same numeric tuple.
    return major, minor, patch, 1 if prerelease is None else 0, prerelease or ""


def satisfies(version: str, requirement: str | None) -> bool:
    if not requirement or requirement in ("*", "latest"):
        return True
    actual = parse_semver(version)[:3]
    req = requirement.strip()
    # Stable-by-default resolution: prereleases are selected only when the
    # requirement explicitly names a prerelease.
    if parse_semver(version)[3] is not None and "-" not in req:
        return False
    if SEMVER_RE.fullmatch(req):
        return version == req
    if req.startswith("^"):
        base = parse_semver(req[1:])[:3]
        upper = (base[0] + 1, 0, 0) if base[0] else ((0, base[1] + 1, 0) if base[1] else (0, 0, base[2] + 1))
        return base <= actual < upper
    if req.startswith("~"):
        base = parse_semver(req[1:])[:3]
        return base <= actual < (base[0], base[1] + 1, 0)
    for piece in [p.strip() for p in req.split(",") if p.strip()]:
        op = next((x for x in (">=", "<=", ">", "<", "=") if piece.startswith(x)), None)
        if not op:
            return False
        rhs = parse_semver(piece[len(op):].strip())[:3]
        if op == ">=" and not actual >= rhs: return False
        if op == "<=" and not actual <= rhs: return False
        if op == ">" and not actual > rhs: return False
        if op == "<" and not actual < rhs: return False
        if op == "=" and not actual == rhs: return False
    return True


def choose_version(metadata: dict, requirement: str | None) -> dict:
    candidates = [v for v in metadata.get("versions", []) if not v.get("yanked") and satisfies(v["version"], requirement)]
    if not candidates:
        raise SystemExit(f"ppx: no version of {metadata.get('name', 'package')} satisfies {requirement or 'latest'}")
    return max(candidates, key=lambda v: version_key(v["version"]))


def safe_extract_zip(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    root = destination.resolve()
    with zipfile.ZipFile(archive) as zf:
        for info in zf.infolist():
            name = info.filename.replace("\\", "/")
            if name.startswith("/") or ".." in Path(name).parts or "\x00" in name:
                raise SystemExit(f"ppx: unsafe archive path: {name!r}")
            target = (destination / name).resolve()
            if root != target and root not in target.parents:
                raise SystemExit(f"ppx: archive path escapes destination: {name!r}")
            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with zf.open(info) as src, target.open("wb") as dst:
                shutil.copyfileobj(src, dst)


def token_file() -> Path:
    return state_root() / "token"


def load_token(required: bool = False) -> str | None:
    path = token_file()
    if path.is_file():
        return path.read_text(encoding="utf-8").strip() or None
    if required:
        raise SystemExit("ppx: not logged in; run `ppx login`")
    return None


def manifest_name_version(root: Path) -> tuple[str, str, str]:
    path = root / "Punpun.toml"
    if not path.is_file():
        raise SystemExit("ppx: current directory has no Punpun.toml")
    name = version = description = ""
    section = ""
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip()
        elif section == "package" and "=" in line:
            key, value = map(str.strip, line.split("=", 1))
            value = value.strip('"')
            if key == "name": name = value
            elif key == "version": version = value
            elif key == "description": description = value
    if not NAME_RE.fullmatch(name):
        raise SystemExit("ppx: manifest has no valid package name")
    try: parse_semver(version)
    except ValueError as exc: raise SystemExit(f"ppx: {exc}")
    return name, version, description


def create_package_archive(root: Path) -> tuple[bytes, str]:
    with tempfile.NamedTemporaryFile(suffix=".zip", delete=False) as temp:
        temp_path = Path(temp.name)
    try:
        with zipfile.ZipFile(temp_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            for path in sorted(root.rglob("*")):
                if not path.is_file(): continue
                rel = path.relative_to(root)
                if any(part in {".git", ".punpun", "node_modules", "__pycache__"} for part in rel.parts): continue
                if path.stat().st_size > 16 * 1024 * 1024:
                    raise SystemExit(f"ppx: refusing unusually large package file: {rel}")
                zf.write(path, rel.as_posix())
        content = temp_path.read_bytes()
        if len(content) > 32 * 1024 * 1024:
            raise SystemExit("ppx: package archive exceeds 32 MiB beta registry limit")
        return content, hashlib.sha256(content).hexdigest()
    finally:
        temp_path.unlink(missing_ok=True)


def bundled_matches(query: str):
    root = bundled_packages()
    if not root.is_dir(): return []
    result = []
    for child in sorted(root.iterdir()):
        if not child.is_dir() or not (child / "Punpun.toml").is_file(): continue
        if query.lower() not in child.name.lower(): continue
        try:
            name, version, description = manifest_name_version(child)
            result.append({"name": name, "latest": version, "description": description, "source": "bundled"})
        except SystemExit:
            continue
    return result


def resolve_remote(name: str, requirement: str | None, resolving=None) -> Path:
    resolving = set() if resolving is None else resolving
    key = (name, requirement or "latest")
    if key in resolving:
        raise SystemExit(f"ppx: dependency cycle while resolving {name}")
    resolving.add(key)
    meta = api_json("/api/v1/packages/" + urllib.parse.quote(name))
    selected = choose_version(meta, requirement)
    version = selected["version"]
    package_dir = cache_root() / "packages" / name / version
    marker = package_dir / ".ppx-checksum"
    checksum = selected["checksum"]
    if package_dir.is_dir() and marker.is_file() and marker.read_text().strip() == checksum:
        resolving.remove(key)
        return package_dir
    archive_bytes = api("/api/v1/packages/%s/%s/download" % (urllib.parse.quote(name), urllib.parse.quote(version)))
    actual = hashlib.sha256(archive_bytes).hexdigest()
    if actual != checksum:
        raise SystemExit(f"ppx: checksum mismatch for {name} {version}: expected {checksum}, got {actual}")
    package_dir.parent.mkdir(parents=True, exist_ok=True)
    staging = package_dir.with_name(package_dir.name + ".tmp")
    shutil.rmtree(staging, ignore_errors=True)
    staging.mkdir(parents=True)
    archive = staging / "package.zip"
    archive.write_bytes(archive_bytes)
    safe_extract_zip(archive, staging / "src")
    archive.unlink()
    source_root = staging / "src"
    manifest = source_root / "Punpun.toml"
    if not manifest.is_file():
        raise SystemExit(f"ppx: {name} {version} archive has no Punpun.toml")
    # Resolve declared registry dependencies and materialize them as the path
    # dependencies understood by the single compiler package graph.
    deps = selected.get("dependencies") or {}
    if deps:
        text = manifest.read_text(encoding="utf-8")
        lines = text.splitlines()
        out, section = [], ""
        for raw in lines:
            stripped = raw.strip()
            if stripped.startswith("[") and stripped.endswith("]"):
                section = stripped[1:-1].strip()
            if section == "dependencies" and "=" in raw:
                dep_name = raw.split("=", 1)[0].strip()
                if dep_name in deps:
                    dep_dir = resolve_remote(dep_name, deps[dep_name], resolving)
                    raw = f'{dep_name} = {{ path = "{dep_dir.as_posix()}" }}'
            out.append(raw)
        manifest.write_text("\n".join(out) + "\n", encoding="utf-8")
    (staging / ".ppx-checksum").write_text(checksum + "\n", encoding="utf-8")
    shutil.rmtree(package_dir, ignore_errors=True)
    os.replace(staging, package_dir)
    # package sources are under src/ because staging also stores PPX metadata.
    final_root = package_dir / "src"
    resolving.remove(key)
    return final_root


def cmd_search(args):
    rows = bundled_matches(args.query)
    try:
        remote = api_json("/api/v1/search?q=" + urllib.parse.quote(args.query)).get("packages", [])
        for row in remote:
            row = dict(row); row["source"] = "registry"; rows.append(row)
    except SystemExit:
        if not rows: raise
    if not rows:
        print("No packages found.")
        return
    seen = set()
    for row in rows:
        key = (row.get("name"), row.get("source"))
        if key in seen: continue
        seen.add(key)
        print(f"{row.get('name','?'):<24} {row.get('latest','?'):<14} {row.get('source',''):<9} {row.get('description','')}")


def cmd_info(args):
    local = bundled_packages() / args.name
    if local.is_dir():
        name, version, description = manifest_name_version(local)
        print(f"name: {name}\nversion: {version}\nsource: bundled\npath: {local}\ndescription: {description}")
        return
    meta = api_json("/api/v1/packages/" + urllib.parse.quote(args.name))
    print(json.dumps(meta, indent=2))


def cmd_add(args):
    if not NAME_RE.fullmatch(args.name): raise SystemExit("ppx: invalid package name")
    if args.path:
        root = Path(args.path).expanduser().resolve()
    else:
        bundled = bundled_packages() / args.name
        if bundled.is_dir():
            root = bundled.resolve()
        else:
            root = resolve_remote(args.name, args.version)
    run_pp("add", args.name, str(root))
    print(f"PPX added {args.name} from {root}")


def cmd_publish(args):
    root = Path.cwd()
    name, version, description = manifest_name_version(root)
    content, checksum = create_package_archive(root)
    payload = {
        "name": name, "version": version, "description": description,
        "checksum": checksum, "archive_b64": base64.b64encode(content).decode("ascii"),
        "dependencies": {},
    }
    response = api_json("/api/v1/packages", method="POST", payload=payload, token=load_token(True), timeout=30)
    print(f"published {name} {version} ({response.get('checksum', checksum)})")


def cmd_login(args):
    payload = {"username": args.username, "password": args.password}
    response = api_json("/api/v1/login", method="POST", payload=payload)
    state_root().mkdir(parents=True, exist_ok=True)
    token_file().write_text(response["token"] + "\n", encoding="utf-8")
    try: os.chmod(token_file(), 0o600)
    except OSError: pass
    print(f"logged in as {args.username}")


def cmd_logout(_args):
    token = load_token(False)
    if token:
        try: api_json("/api/v1/logout", method="POST", payload={}, token=token)
        except SystemExit: pass
    token_file().unlink(missing_ok=True)
    print("logged out")


def cmd_yank(args):
    api_json(f"/api/v1/packages/{urllib.parse.quote(args.name)}/{urllib.parse.quote(args.version)}/yank",
             method="POST", payload={}, token=load_token(True))
    print(f"yanked {args.name} {args.version}")


def cmd_doctor(_args):
    print(f"PPX {VERSION}")
    print(f"pp:       {pp_command()} {'OK' if Path(pp_command()).exists() or shutil.which(pp_command()) else 'MISSING'}")
    print(f"cache:    {cache_root()}")
    print(f"bundled:  {bundled_packages()} {'OK' if bundled_packages().is_dir() else 'missing'}")
    try:
        api_json("/api/v1/health", timeout=2)
        print(f"registry: {registry_url()} OK")
    except SystemExit:
        print(f"registry: {registry_url()} unavailable (optional for bundled/path packages)")


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="ppx", description="PunPunXPac package manager")
    p.add_argument("--version", action="version", version=f"ppx {VERSION}")
    sub = p.add_subparsers(dest="command", required=True)
    sub.add_parser("init").set_defaults(func=lambda a: run_pp("init"))
    add = sub.add_parser("add"); add.add_argument("name"); add.add_argument("version", nargs="?"); add.add_argument("--path"); add.set_defaults(func=cmd_add)
    rem = sub.add_parser("remove"); rem.add_argument("name"); rem.set_defaults(func=lambda a: run_pp("remove", a.name))
    sub.add_parser("install").set_defaults(func=lambda a: run_pp("update"))
    sub.add_parser("update").set_defaults(func=lambda a: run_pp("update"))
    search = sub.add_parser("search"); search.add_argument("query", nargs="?", default=""); search.set_defaults(func=cmd_search)
    info = sub.add_parser("info"); info.add_argument("name"); info.set_defaults(func=cmd_info)
    sub.add_parser("tree").set_defaults(func=lambda a: run_pp("tree"))
    sub.add_parser("outdated").set_defaults(func=lambda a: print("ppx: registry-installed dependency version tracking is beta; use `ppx info <name>`"))
    pub = sub.add_parser("publish"); pub.set_defaults(func=cmd_publish)
    yank = sub.add_parser("yank"); yank.add_argument("name"); yank.add_argument("version"); yank.set_defaults(func=cmd_yank)
    login = sub.add_parser("login"); login.add_argument("username"); login.add_argument("password"); login.set_defaults(func=cmd_login)
    sub.add_parser("logout").set_defaults(func=cmd_logout)
    cache = sub.add_parser("cache"); cache.add_argument("action", choices=["path", "clean"], nargs="?", default="path"); cache.set_defaults(func=lambda a: (shutil.rmtree(cache_root(), ignore_errors=True), print("PPX cache cleared")) if a.action == "clean" else print(cache_root()))
    sub.add_parser("doctor").set_defaults(func=cmd_doctor)
    return p


def main() -> int:
    args = parser().parse_args()
    result = args.func(args)
    return result.returncode if isinstance(result, subprocess.CompletedProcess) else 0


if __name__ == "__main__":
    raise SystemExit(main())
