# LZSS compression implemented entirely in PunPun.
# Stream format: "PPLZ" + 32-bit original size + groups of one flag byte and up
# to eight tokens. Flag 1 = literal byte; flag 0 = two-byte (offset,length)
# backreference. Window 4095 bytes, match length 3..18.

fn _lz_push_u32(out: bytes, value: int) -> void {
    bytes_push(out,(value>>24)&255); bytes_push(out,(value>>16)&255);
    bytes_push(out,(value>>8)&255); bytes_push(out,value&255);
}
fn _lz_read_u32(data: bytes, at: int) -> int {
    return (bytes_at(data,at)<<24)|(bytes_at(data,at+1)<<16)|(bytes_at(data,at+2)<<8)|bytes_at(data,at+3);
}

struct _LzMatch { offset: int, length: int }

fn _lz_best(data: bytes, pos: int) -> _LzMatch {
    let mut best_length = 0; let mut best_offset = 0;
    let mut start = pos - 4095; if start < 0 { start = 0; }
    let mut candidate = pos - 1;
    while candidate >= start {
        let mut length = 0;
        while length < 18 && pos + length < bytes_len(data) && bytes_at(data,candidate + (length % (pos-candidate))) == bytes_at(data,pos+length) {
            length = length + 1;
        }
        if length >= 3 && length > best_length {
            best_length = length; best_offset = pos - candidate;
            if length == 18 { return _LzMatch(best_offset,best_length); }
        }
        candidate = candidate - 1;
    }
    return _LzMatch(best_offset,best_length);
}

fn lzss_compress(data: bytes) -> bytes {
    let out = bytes_from_text("PPLZ");
    _lz_push_u32(out, bytes_len(data));
    let mut pos = 0;
    while pos < bytes_len(data) {
        let flag_at = bytes_len(out); bytes_push(out,0); let mut flags = 0;
        for bit in 0..8 {
            if pos >= bytes_len(data) { break; }
            let match_value = _lz_best(data,pos);
            if match_value.length >= 3 {
                let offset = match_value.offset;
                bytes_push(out,(offset>>4)&255);
                bytes_push(out,((offset&15)<<4)|(match_value.length-3));
                pos = pos + match_value.length;
            } else {
                flags = flags | (1 << bit);
                bytes_push(out,bytes_at(data,pos)); pos = pos + 1;
            }
        }
        bytes_put(out,flag_at,flags);
    }
    return out;
}

fn lzss_decompress(data: bytes) -> bytes {
    if bytes_len(data) < 8 || bytes_to_text(bytes_slice(data,0,4)) != "PPLZ" { panic("invalid PPLZ stream"); }
    let expected = _lz_read_u32(data,4);
    let out = bytes(); let mut pos = 8;
    while bytes_len(out) < expected {
        if pos >= bytes_len(data) { panic("truncated PPLZ flag group"); }
        let flags = bytes_at(data,pos); pos = pos + 1;
        for bit in 0..8 {
            if bytes_len(out) >= expected { break; }
            if ((flags >> bit) & 1) == 1 {
                if pos >= bytes_len(data) { panic("truncated PPLZ literal"); }
                bytes_push(out,bytes_at(data,pos)); pos = pos + 1;
            } else {
                if pos + 1 >= bytes_len(data) { panic("truncated PPLZ match"); }
                let first = bytes_at(data,pos); let second = bytes_at(data,pos+1); pos = pos + 2;
                let offset = (first<<4)|((second>>4)&15); let length = (second&15)+3;
                if offset <= 0 || offset > bytes_len(out) { panic("invalid PPLZ backreference"); }
                for i in 0..length {
                    if bytes_len(out) >= expected { break; }
                    bytes_push(out,bytes_at(out,bytes_len(out)-offset));
                }
            }
        }
    }
    return out;
}

fn lzss_compress_text(value: str) -> bytes { return lzss_compress(bytes_from_text(value)); }
fn lzss_decompress_text(data: bytes) -> str { return bytes_to_text(lzss_decompress(data)); }
