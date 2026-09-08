#!/usr/bin/env python3
"""Local PPX registry backend using only the Python standard library.

This is intentionally suitable for local development and integration tests,
not presented as an internet-hardened production service. Published archives
are immutable, checksummed, path-validated ZIPs. The server never executes
uploaded package code.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import secrets
import sqlite3
import stat
import time
import urllib.parse
import zipfile
import io
import threading
import tomllib
from contextlib import contextmanager
from collections import defaultdict, deque

NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_-]{0,63}$")
SEMVER_RE = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z.-]+))?$")
MAX_ARCHIVE = 32 * 1024 * 1024
TOKEN_LIFETIME = 30 * 24 * 60 * 60


def semver_key(value: str):
    match = SEMVER_RE.fullmatch(value)
    if not match:
        return (-1, -1, -1, -1, "")
    major, minor, patch = map(int, match.group(1, 2, 3))
    prerelease = match.group(4)
    return major, minor, patch, 1 if prerelease is None else 0, prerelease or ""


class Registry:
    def __init__(self, root: Path):
        self.root = root.resolve()
        self.storage = self.root / "storage"
        self.storage.mkdir(parents=True, exist_ok=True)
        self.db_path = self.root / "registry.sqlite3"
        self.initialize()

    @contextmanager
    def connect(self):
        con = sqlite3.connect(self.db_path, timeout=10)
        con.row_factory = sqlite3.Row
        con.execute("PRAGMA foreign_keys=ON")
        try:
            yield con
            con.commit()
        except Exception:
            con.rollback()
            raise
        finally:
            con.close()

    def initialize(self):
        with self.connect() as db:
            db.executescript("""
            CREATE TABLE IF NOT EXISTS users(
              id INTEGER PRIMARY KEY,
              username TEXT NOT NULL UNIQUE,
              password_salt BLOB NOT NULL,
              password_hash BLOB NOT NULL,
              created_at INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS tokens(
              token_hash TEXT PRIMARY KEY,
              user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
              created_at INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS packages(
              id INTEGER PRIMARY KEY,
              name TEXT NOT NULL UNIQUE,
              description TEXT NOT NULL DEFAULT '',
              metadata_json TEXT NOT NULL DEFAULT '{}',
              owner_user_id INTEGER NOT NULL REFERENCES users(id),
              created_at INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS versions(
              id INTEGER PRIMARY KEY,
              package_id INTEGER NOT NULL REFERENCES packages(id) ON DELETE CASCADE,
              version TEXT NOT NULL,
              checksum TEXT NOT NULL,
              archive_path TEXT NOT NULL,
              dependencies_json TEXT NOT NULL DEFAULT '{}',
              yanked INTEGER NOT NULL DEFAULT 0,
              downloads INTEGER NOT NULL DEFAULT 0,
              created_at INTEGER NOT NULL,
              UNIQUE(package_id, version)
            );
            """)
            token_columns = {row[1] for row in db.execute("PRAGMA table_info(tokens)")}
            if "expires_at" not in token_columns:
                db.execute("ALTER TABLE tokens ADD COLUMN expires_at INTEGER NOT NULL DEFAULT 0")
            package_columns = {row[1] for row in db.execute("PRAGMA table_info(packages)")}
            if "metadata_json" not in package_columns:
                db.execute("ALTER TABLE packages ADD COLUMN metadata_json TEXT NOT NULL DEFAULT '{}'")

    @staticmethod
    def password_hash(password: str, salt: bytes) -> bytes:
        return hashlib.pbkdf2_hmac("sha256", password.encode(), salt, 200_000)

    def register(self, username: str, password: str):
        if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_-]{2,31}", username):
            raise ValueError("username must be 3-32 letters/digits/_/- and start with a letter")
        if len(password) < 8:
            raise ValueError("password must be at least 8 characters")
        salt = secrets.token_bytes(16)
        digest = self.password_hash(password, salt)
        try:
            with self.connect() as db:
                db.execute("INSERT INTO users(username,password_salt,password_hash,created_at) VALUES(?,?,?,?)",
                           (username, salt, digest, int(time.time())))
        except sqlite3.IntegrityError:
            raise ValueError("username already exists")

    def login(self, username: str, password: str) -> str:
        with self.connect() as db:
            user = db.execute("SELECT * FROM users WHERE username=?", (username,)).fetchone()
            if not user or not secrets.compare_digest(self.password_hash(password, user["password_salt"]), user["password_hash"]):
                raise PermissionError("invalid username or password")
            token = secrets.token_urlsafe(32)
            digest = hashlib.sha256(token.encode()).hexdigest()
            now = int(time.time())
            db.execute("DELETE FROM tokens WHERE expires_at>0 AND expires_at<=?", (now,))
            db.execute("INSERT INTO tokens(token_hash,user_id,created_at,expires_at) VALUES(?,?,?,?)",
                       (digest, user["id"], now, now + TOKEN_LIFETIME))
            return token

    def authenticate(self, token: str | None):
        if not token: return None
        digest = hashlib.sha256(token.encode()).hexdigest()
        with self.connect() as db:
            return db.execute("""SELECT users.* FROM tokens JOIN users ON users.id=tokens.user_id
                              WHERE token_hash=? AND (tokens.expires_at=0 OR tokens.expires_at>?)""",
                              (digest, int(time.time()))).fetchone()

    def logout(self, token: str | None):
        if not token: return
        digest = hashlib.sha256(token.encode()).hexdigest()
        with self.connect() as db:
            db.execute("DELETE FROM tokens WHERE token_hash=?", (digest,))

    @staticmethod
    def valid_requirement(requirement: str) -> bool:
        req = requirement.strip()
        if req in ("", "*", "latest") or SEMVER_RE.fullmatch(req):
            return True
        if req.startswith(("^", "~")):
            return bool(SEMVER_RE.fullmatch(req[1:]))
        pieces = [piece.strip() for piece in req.split(",") if piece.strip()]
        if not pieces:
            return False
        for piece in pieces:
            op = next((candidate for candidate in (">=", "<=", ">", "<", "=") if piece.startswith(candidate)), None)
            if not op or not SEMVER_RE.fullmatch(piece[len(op):].strip()):
                return False
        return True

    @classmethod
    def normalize_manifest(cls, document: dict) -> dict:
        package = document.get("package")
        if not isinstance(package, dict):
            raise ValueError("Punpun.toml requires a [package] table")
        name = str(package.get("name", "")).strip()
        version = str(package.get("version", "")).strip()
        description = str(package.get("description", "")).strip()
        if not NAME_RE.fullmatch(name): raise ValueError("manifest contains an invalid package name")
        if not SEMVER_RE.fullmatch(version): raise ValueError("manifest contains an invalid semantic version")
        if len(description) > 1000: raise ValueError("package description exceeds 1000 characters")
        metadata = {"description": description}
        license_name = str(package.get("license", "")).strip()
        if license_name:
            if len(license_name) > 128: raise ValueError("package license metadata is too long")
            metadata["license"] = license_name
        for field in ("repository", "homepage"):
            value = str(package.get(field, "")).strip()
            if value:
                parsed = urllib.parse.urlparse(value)
                if parsed.scheme not in {"http", "https"} or not parsed.netloc:
                    raise ValueError(f"package {field} must be an http(s) URL")
                metadata[field] = value
        readme = str(package.get("readme", "")).strip()
        if readme:
            rel = Path(readme)
            if rel.is_absolute() or ".." in rel.parts: raise ValueError("package readme path must stay inside the archive")
            metadata["readme"] = rel.as_posix()
        keywords = package.get("keywords", [])
        if keywords:
            if not isinstance(keywords, list) or len(keywords) > 16:
                raise ValueError("package keywords must be a list with at most 16 entries")
            normalized=[]
            for keyword in keywords:
                text=str(keyword).strip()
                if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_+.-]{0,31}", text):
                    raise ValueError(f"invalid package keyword: {text!r}")
                normalized.append(text)
            metadata["keywords"] = normalized
        dependencies = {}
        raw_dependencies = document.get("dependencies") or {}
        if not isinstance(raw_dependencies, dict) or len(raw_dependencies) > 128:
            raise ValueError("invalid dependency metadata")
        for dep_name, spec in raw_dependencies.items():
            dep_name = str(dep_name)
            if not NAME_RE.fullmatch(dep_name): raise ValueError(f"invalid dependency name: {dep_name!r}")
            if isinstance(spec, str):
                requirement = spec.strip()
            elif isinstance(spec, dict):
                if "path" in spec: raise ValueError(f"published dependency {dep_name!r} may not use a local path")
                requirement = str(spec.get("version", "")).strip()
            else:
                raise ValueError(f"dependency {dep_name!r} has invalid metadata")
            if not cls.valid_requirement(requirement): raise ValueError(f"invalid version requirement for {dep_name}: {requirement!r}")
            dependencies[dep_name] = requirement or "*"
        return {"name": name, "version": version, "description": description, "metadata": metadata, "dependencies": dependencies}

    @classmethod
    def validate_archive(cls, content: bytes):
        if len(content) > MAX_ARCHIVE: raise ValueError("archive exceeds 32 MiB limit")
        try:
            zf = zipfile.ZipFile(io.BytesIO(content))
        except zipfile.BadZipFile:
            raise ValueError("archive is not a valid ZIP")
        total = 0
        file_count = 0
        manifest_bytes = None
        names = set()
        for info in zf.infolist():
            name = info.filename.replace("\\", "/")
            parts = Path(name).parts
            if name.startswith("/") or ".." in parts or "\x00" in name:
                raise ValueError(f"unsafe archive path: {name!r}")
            if name in names: raise ValueError(f"duplicate archive path: {name!r}")
            names.add(name)
            mode = info.external_attr >> 16
            if mode and stat.S_ISLNK(mode): raise ValueError(f"symbolic links are not allowed in package archives: {name!r}")
            if info.is_dir():
                continue
            file_count += 1
            if file_count > 4096: raise ValueError("package archive contains too many files")
            total += info.file_size
            if total > 128 * 1024 * 1024:
                raise ValueError("expanded archive exceeds 128 MiB limit")
            if name == "Punpun.toml":
                manifest_bytes = zf.read(info)
        if manifest_bytes is None: raise ValueError("package archive must contain Punpun.toml at its root")
        try:
            document = tomllib.loads(manifest_bytes.decode("utf-8"))
        except (UnicodeDecodeError, tomllib.TOMLDecodeError) as exc:
            raise ValueError(f"package Punpun.toml is invalid: {exc}")
        normalized = cls.normalize_manifest(document)
        readme = normalized["metadata"].get("readme")
        if readme and readme not in names: raise ValueError(f"package readme is missing from archive: {readme}")
        return normalized

    def publish(self, user, payload: dict):
        try:
            content = base64.b64decode(payload.get("archive_b64", ""), validate=True)
        except Exception:
            raise ValueError("archive_b64 is invalid")
        manifest = self.validate_archive(content)
        name = manifest["name"]
        version = manifest["version"]
        description = manifest["description"]
        dependencies = manifest["dependencies"]
        metadata = manifest["metadata"]
        if payload.get("name") not in (None, name): raise ValueError("uploaded package name does not match Punpun.toml")
        if payload.get("version") not in (None, version): raise ValueError("uploaded package version does not match Punpun.toml")
        if "dependencies" in payload and payload.get("dependencies") != dependencies:
            raise ValueError("uploaded dependency metadata does not match Punpun.toml")
        checksum = hashlib.sha256(content).hexdigest()
        supplied = str(payload.get("checksum", ""))
        if supplied and supplied != checksum: raise ValueError("archive checksum mismatch")
        with self.connect() as db:
            package = db.execute("SELECT * FROM packages WHERE name=?", (name,)).fetchone()
            if package and package["owner_user_id"] != user["id"]:
                raise PermissionError("package is owned by another user")
            metadata_json = json.dumps(metadata, sort_keys=True, separators=(",", ":"))
            if not package:
                cur = db.execute("INSERT INTO packages(name,description,metadata_json,owner_user_id,created_at) VALUES(?,?,?,?,?)",
                                 (name, description, metadata_json, user["id"], int(time.time())))
                package_id = cur.lastrowid
            else:
                package_id = package["id"]
                db.execute("UPDATE packages SET description=?,metadata_json=? WHERE id=?", (description, metadata_json, package_id))
            if db.execute("SELECT 1 FROM versions WHERE package_id=? AND version=?", (package_id, version)).fetchone():
                raise FileExistsError("published package versions are immutable")
            package_dir = self.storage / name / version
            package_dir.mkdir(parents=True, exist_ok=True)
            archive = package_dir / f"{name}-{version}.zip"
            archive.write_bytes(content)
            rel = archive.relative_to(self.root).as_posix()
            db.execute("INSERT INTO versions(package_id,version,checksum,archive_path,dependencies_json,created_at) VALUES(?,?,?,?,?,?)",
                       (package_id, version, checksum, rel, json.dumps(dependencies, sort_keys=True), int(time.time())))
        return checksum

    def package_metadata(self, name: str):
        with self.connect() as db:
            package = db.execute("SELECT packages.*, users.username owner FROM packages JOIN users ON users.id=packages.owner_user_id WHERE name=?", (name,)).fetchone()
            if not package: return None
            versions = db.execute("SELECT version,checksum,dependencies_json,yanked,downloads,created_at FROM versions WHERE package_id=?", (package["id"],)).fetchall()
            return {
                "name": package["name"], "description": package["description"], "owner": package["owner"],
                "metadata": json.loads(package["metadata_json"] or "{}"),
                "versions": sorted([{"version": r["version"], "checksum": r["checksum"], "dependencies": json.loads(r["dependencies_json"]),
                              "yanked": bool(r["yanked"]), "downloads": r["downloads"], "created_at": r["created_at"]} for r in versions],
                              key=lambda item: semver_key(item["version"]), reverse=True)
            }

    def search(self, query: str):
        with self.connect() as db:
            rows = db.execute("""SELECT p.id,p.name,p.description,COALESCE(SUM(v.downloads),0) downloads
                               FROM packages p LEFT JOIN versions v ON v.package_id=p.id
                               WHERE p.name LIKE ? OR p.description LIKE ? GROUP BY p.id ORDER BY downloads DESC,p.name LIMIT 50""",
                              (f"%{query}%", f"%{query}%")).fetchall()
            result=[]
            for row in rows:
                versions=[item[0] for item in db.execute("SELECT version FROM versions WHERE package_id=? AND yanked=0",(row["id"],))]
                result.append({"name":row["name"],"description":row["description"],
                               "latest":max(versions,key=semver_key) if versions else "","downloads":row["downloads"]})
            return result

    def download(self, name: str, version: str):
        with self.connect() as db:
            row = db.execute("""SELECT v.id,v.archive_path FROM versions v JOIN packages p ON p.id=v.package_id
                              WHERE p.name=? AND v.version=? AND v.yanked=0""", (name, version)).fetchone()
            if not row: return None
            db.execute("UPDATE versions SET downloads=downloads+1 WHERE id=?", (row["id"],))
            path = (self.root / row["archive_path"]).resolve()
            if self.root != path and self.root not in path.parents: raise RuntimeError("stored archive escaped registry root")
            return path.read_bytes()

    def yank(self, user, name: str, version: str):
        with self.connect() as db:
            package = db.execute("SELECT * FROM packages WHERE name=?", (name,)).fetchone()
            if not package: raise LookupError("package not found")
            if package["owner_user_id"] != user["id"]: raise PermissionError("package is owned by another user")
            cur = db.execute("UPDATE versions SET yanked=1 WHERE package_id=? AND version=?", (package["id"], version))
            if cur.rowcount != 1: raise LookupError("package version not found")


class Handler(BaseHTTPRequestHandler):
    registry: Registry
    server_version = "PPXRegistry/" + (Path(__file__).resolve().parents[1] / "VERSION").read_text(encoding="utf-8").strip()
    rate_lock = threading.Lock()
    rate_windows = defaultdict(deque)
    rate_limit = int(os.environ.get("PPX_RATE_LIMIT", "120"))

    def log_message(self, fmt, *args):
        print("ppx-registry:", fmt % args)

    def json_body(self):
        length = int(self.headers.get("Content-Length", "0"))
        if length > MAX_ARCHIVE * 2:
            raise ValueError("request body too large")
        try: return json.loads(self.rfile.read(length).decode("utf-8"))
        except Exception: raise ValueError("invalid JSON body")

    def reply(self, status, payload, content_type="application/json"):
        if content_type == "application/json": data = json.dumps(payload, separators=(",", ":")).encode()
        else: data = payload
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("X-Frame-Options", "DENY")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Cache-Control", "no-store" if status >= 400 else "private, max-age=30")
        self.end_headers(); self.wfile.write(data)

    def rate_allowed(self):
        now=time.monotonic(); key=self.client_address[0]
        with self.rate_lock:
            window=self.rate_windows[key]
            while window and window[0] <= now-60: window.popleft()
            if len(window) >= self.rate_limit: return False
            window.append(now); return True

    def bearer(self):
        value = self.headers.get("Authorization", "")
        return value[7:] if value.startswith("Bearer ") else None

    def do_GET(self):
        if not self.rate_allowed(): return self.reply(429,{"error":"rate limit exceeded"})
        parsed = urllib.parse.urlparse(self.path); parts = [p for p in parsed.path.split("/") if p]
        if parsed.path == "/api/v1/health":
            version = (Path(__file__).resolve().parents[1] / "VERSION").read_text(encoding="utf-8").strip()
            return self.reply(200, {"status":"ok","version":version})
        if parsed.path == "/api/v1/search":
            q = urllib.parse.parse_qs(parsed.query).get("q", [""])[0][:128]
            return self.reply(200, {"packages": self.registry.search(q)})
        if len(parts) == 4 and parts[:3] == ["api","v1","packages"]:
            meta = self.registry.package_metadata(parts[3])
            return self.reply(200, meta) if meta else self.reply(404, {"error":"package not found"})
        if len(parts) == 6 and parts[:3] == ["api","v1","packages"] and parts[5] == "download":
            data = self.registry.download(parts[3], parts[4])
            return self.reply(200, data, "application/zip") if data is not None else self.reply(404, {"error":"package version not found"})
        return self.reply(404, {"error":"not found"})

    def do_POST(self):
        if not self.rate_allowed(): return self.reply(429,{"error":"rate limit exceeded"})
        parsed = urllib.parse.urlparse(self.path); parts = [p for p in parsed.path.split("/") if p]
        try:
            if parsed.path == "/api/v1/register":
                body=self.json_body(); self.registry.register(str(body.get("username","")),str(body.get("password","")))
                return self.reply(201,{"status":"created"})
            if parsed.path == "/api/v1/login":
                body=self.json_body(); token=self.registry.login(str(body.get("username","")),str(body.get("password","")))
                return self.reply(200,{"token":token})
            if parsed.path == "/api/v1/logout":
                self.registry.logout(self.bearer()); return self.reply(200,{"status":"logged out"})
            user=self.registry.authenticate(self.bearer())
            if not user: return self.reply(401,{"error":"authentication required"})
            if parsed.path == "/api/v1/packages":
                checksum=self.registry.publish(user,self.json_body()); return self.reply(201,{"status":"published","checksum":checksum})
            if len(parts)==6 and parts[:3]==["api","v1","packages"] and parts[5]=="yank":
                self.registry.yank(user,parts[3],parts[4]); return self.reply(200,{"status":"yanked"})
            return self.reply(404,{"error":"not found"})
        except ValueError as exc: return self.reply(400,{"error":str(exc)})
        except PermissionError as exc: return self.reply(403,{"error":str(exc)})
        except FileExistsError as exc: return self.reply(409,{"error":str(exc)})
        except LookupError as exc: return self.reply(404,{"error":str(exc)})
        except Exception as exc:
            self.log_error("internal error: %s", exc); return self.reply(500,{"error":"internal registry error"})


def main():
    ap=argparse.ArgumentParser(description="Local PunPunXPac registry")
    ap.add_argument("--host",default="127.0.0.1"); ap.add_argument("--port",type=int,default=8765)
    ap.add_argument("--data",type=Path,default=Path(".ppx-registry"))
    args=ap.parse_args(); Handler.registry=Registry(args.data)
    server=ThreadingHTTPServer((args.host,args.port),Handler)
    print(f"PPX registry listening on http://{args.host}:{args.port} data={args.data.resolve()}")
    try: server.serve_forever()
    except KeyboardInterrupt: pass

if __name__=="__main__": main()
