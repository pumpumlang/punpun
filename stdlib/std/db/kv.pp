import std.data.json
import std.filesystem

# Small durable document/key-value database implemented in PunPun.
# It intentionally favors correctness and inspectability over pretending to be
# a relational engine: one JSON object is atomically replaced on commit.

object KeyValueDb {
    public let path: str;
    public let values: Map<JsonValue>;
    public let dirty: bool;
    public let last_error: str;

    public init(path: str) {
        self.path = path;
        self.values = map<JsonValue>();
        self.dirty = false;
        self.last_error = "";
        self.reload();
    }

    public fn reload() -> bool {
        map_clear(self.values);
        self.last_error = "";
        if !file_exists(self.path) { self.dirty = false; return true; }
        let parsed = json_parse(read_text(self.path));
        if !parsed.ok() { self.last_error = parsed.error; return false; }
        if !parsed.value.is_object() { self.last_error = "database root must be a JSON object"; return false; }
        for key in map_keys(parsed.value.object_value) {
            map_put(self.values, key, map_get(parsed.value.object_value, key));
        }
        self.dirty = false;
        return true;
    }

    public fn has(key: str) -> bool { return map_has(self.values, key); }
    public fn get(key: str) -> JsonValue {
        if !map_has(self.values, key) { return json_null(); }
        return map_get(self.values, key);
    }
    public fn get_string(key: str, fallback: str) -> str { return self.get(key).string_or(fallback); }
    public fn get_int(key: str, fallback: int) -> int { return self.get(key).int_or(fallback); }
    public fn get_bool(key: str, fallback: bool) -> bool { return self.get(key).bool_or(fallback); }

    public fn set(key: str, value: JsonValue) -> void { map_put(self.values, key, value); self.dirty = true; }
    public fn set_string(key: str, value: str) -> void { self.set(key, json_string(value)); }
    public fn set_int(key: str, value: int) -> void { self.set(key, json_int(value)); }
    public fn set_bool(key: str, value: bool) -> void { self.set(key, json_bool(value)); }
    public fn remove(key: str) -> bool {
        let removed = map_remove(self.values, key);
        if removed { self.dirty = true; }
        return removed;
    }
    public fn size() -> int { return map_size(self.values); }
    public fn keys() -> List<str> { return map_keys(self.values); }

    public fn commit() -> bool {
        let root = json_object();
        for key in map_keys(self.values) { map_put(root.object_value, key, map_get(self.values, key)); }
        fs_write_text_atomic(self.path, json_pretty(root, 2) + "\n");
        self.dirty = false;
        return true;
    }
}

fn open_kv(path: str) -> KeyValueDb { return KeyValueDb(path); }
