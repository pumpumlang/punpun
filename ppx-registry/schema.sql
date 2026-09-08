-- Informational copy of the schema created by server.py.
CREATE TABLE users(id INTEGER PRIMARY KEY, username TEXT UNIQUE NOT NULL, password_salt BLOB NOT NULL, password_hash BLOB NOT NULL, created_at INTEGER NOT NULL);
CREATE TABLE tokens(token_hash TEXT PRIMARY KEY, user_id INTEGER NOT NULL REFERENCES users(id), created_at INTEGER NOT NULL);
CREATE TABLE packages(id INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL, description TEXT NOT NULL, owner_user_id INTEGER NOT NULL REFERENCES users(id), created_at INTEGER NOT NULL);
CREATE TABLE versions(id INTEGER PRIMARY KEY, package_id INTEGER NOT NULL REFERENCES packages(id), version TEXT NOT NULL, checksum TEXT NOT NULL, archive_path TEXT NOT NULL, dependencies_json TEXT NOT NULL, yanked INTEGER NOT NULL DEFAULT 0, downloads INTEGER NOT NULL DEFAULT 0, created_at INTEGER NOT NULL, UNIQUE(package_id, version));
