import std.data.binary
import std.data.checksum

# Interoperable ZIP archives using method 0 (stored/no compression).
# The format machinery is deliberately implemented in PunPun.  Pair this with
# std.compress when an application wants to compress payloads before archiving.

struct ZipEntry {
    name: str,
    data: bytes,
}

struct ZipReadResult {
    entries: List<ZipEntry>,
    error: str,
    public fn ok() -> bool { return len(self.error) == 0; }
    public fn get(name: str) -> bytes {
        for entry in self.entries { if entry.name == name { return entry.data; } }
        return bytes();
    }
    public fn has(name: str) -> bool {
        for entry in self.entries { if entry.name == name { return true; } }
        return false;
    }
}

fn _zip_append(out: bytes, data: bytes) -> void {
    for i in 0..bytes_len(data) { bytes_push(out, bytes_at(data, i)); }
}

fn _zip_need(data: bytes, offset: int, amount: int) -> bool {
    return offset >= 0 && amount >= 0 && offset + amount <= bytes_len(data);
}

fn zip_write(entries: List<ZipEntry>) -> bytes {
    let out = bytes();
    let offsets = list<int>();
    let crcs = list<int>();
    let sizes = list<int>();

    # Local file records and payloads.
    for entry in entries {
        let name_bytes = bytes_from_text(entry.name);
        let size = bytes_len(entry.data);
        let checksum = crc32(entry.data);
        if bytes_len(name_bytes) > 65535 { panic("ZIP entry name is too long"); }
        if size > 4294967295 { panic("ZIP64 is not supported"); }
        list_push(offsets, bytes_len(out));
        list_push(crcs, checksum);
        list_push(sizes, size);

        write_u32_le(out, 67324752);       # PK\x03\x04
        write_u16_le(out, 20);             # version needed
        write_u16_le(out, 2048);           # UTF-8 names
        write_u16_le(out, 0);              # method: stored
        write_u16_le(out, 0); write_u16_le(out, 0); # DOS time/date
        write_u32_le(out, checksum);
        write_u32_le(out, size); write_u32_le(out, size);
        write_u16_le(out, bytes_len(name_bytes));
        write_u16_le(out, 0);              # extra length
        _zip_append(out, name_bytes);
        _zip_append(out, entry.data);
    }

    let central_start = bytes_len(out);
    for i in 0..list_size(entries) {
        let entry = list_at(entries, i);
        let name_bytes = bytes_from_text(entry.name);
        let size = list_at(sizes, i);
        write_u32_le(out, 33639248);        # PK\x01\x02
        write_u16_le(out, 20); write_u16_le(out, 20);
        write_u16_le(out, 2048);            # UTF-8
        write_u16_le(out, 0);               # stored
        write_u16_le(out, 0); write_u16_le(out, 0);
        write_u32_le(out, list_at(crcs, i));
        write_u32_le(out, size); write_u32_le(out, size);
        write_u16_le(out, bytes_len(name_bytes));
        write_u16_le(out, 0); write_u16_le(out, 0); # extra/comment
        write_u16_le(out, 0); write_u16_le(out, 0); # disk/internal attrs
        write_u32_le(out, 0);               # external attrs
        write_u32_le(out, list_at(offsets, i));
        _zip_append(out, name_bytes);
    }

    let central_size = bytes_len(out) - central_start;
    if list_size(entries) > 65535 { panic("ZIP64 is not supported"); }
    write_u32_le(out, 101010256);           # PK\x05\x06
    write_u16_le(out, 0); write_u16_le(out, 0);
    write_u16_le(out, list_size(entries)); write_u16_le(out, list_size(entries));
    write_u32_le(out, central_size); write_u32_le(out, central_start);
    write_u16_le(out, 0);
    return out;
}

fn zip_read(data: bytes) -> ZipReadResult {
    let entries = list<ZipEntry>();
    let mut pos = 0;
    while pos + 4 <= bytes_len(data) {
        let signature = read_u32_le(data, pos);
        if signature == 33639248 || signature == 101010256 { break; }
        if signature != 67324752 { return ZipReadResult(entries, "invalid ZIP local header"); }
        if !_zip_need(data, pos, 30) { return ZipReadResult(entries, "truncated ZIP local header"); }
        let flags = read_u16_le(data, pos + 6);
        let method = read_u16_le(data, pos + 8);
        let checksum = read_u32_le(data, pos + 14);
        let compressed = read_u32_le(data, pos + 18);
        let uncompressed = read_u32_le(data, pos + 22);
        let name_len = read_u16_le(data, pos + 26);
        let extra_len = read_u16_le(data, pos + 28);
        if (flags & 8) != 0 { return ZipReadResult(entries, "ZIP data descriptors are not supported"); }
        if method != 0 { return ZipReadResult(entries, "ZIP compression method is not supported"); }
        if compressed != uncompressed { return ZipReadResult(entries, "stored ZIP entry has mismatched sizes"); }
        let name_start = pos + 30;
        let payload_start = name_start + name_len + extra_len;
        if !_zip_need(data, name_start, name_len) || !_zip_need(data, payload_start, compressed) {
            return ZipReadResult(entries, "truncated ZIP entry");
        }
        let name = bytes_to_text(bytes_slice(data, name_start, name_start + name_len));
        let payload = bytes_slice(data, payload_start, payload_start + compressed);
        if crc32(payload) != checksum { return ZipReadResult(entries, "ZIP CRC-32 mismatch for " + name); }
        list_push(entries, ZipEntry(name, payload));
        pos = payload_start + compressed;
    }
    return ZipReadResult(entries, "");
}

fn zip_write_file(path: str, entries: List<ZipEntry>) -> void { write_bytes(path, zip_write(entries)); }
fn zip_read_file(path: str) -> ZipReadResult { return zip_read(read_bytes(path)); }
