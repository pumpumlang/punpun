import std.data.hex

# SHA-256 implemented in PunPun. Arithmetic is explicitly reduced to 32 bits so
# checked signed-int overflow never becomes part of the algorithm.

fn _u32(value: int) -> int { return value & 4294967295; }
fn _rotr32(value: int, amount: int) -> int {
    let x = _u32(value);
    if amount == 0 { return x; }
    return _u32((x >> amount) | _u32(x << (32 - amount)));
}
fn _shr32(value: int, amount: int) -> int { return _u32(value) >> amount; }
fn _sha_ch(x: int, y: int, z: int) -> int { return _u32((x & y) ^ ((~x) & z)); }
fn _sha_maj(x: int, y: int, z: int) -> int { return _u32((x & y) ^ (x & z) ^ (y & z)); }
fn _sha_big0(x: int) -> int { return _u32(_rotr32(x, 2) ^ _rotr32(x, 13) ^ _rotr32(x, 22)); }
fn _sha_big1(x: int) -> int { return _u32(_rotr32(x, 6) ^ _rotr32(x, 11) ^ _rotr32(x, 25)); }
fn _sha_small0(x: int) -> int { return _u32(_rotr32(x, 7) ^ _rotr32(x, 18) ^ _shr32(x, 3)); }
fn _sha_small1(x: int) -> int { return _u32(_rotr32(x, 17) ^ _rotr32(x, 19) ^ _shr32(x, 10)); }

fn _sha_constants() -> List<int> {
    let k = list<int>();
    list_push(k,1116352408); list_push(k,1899447441); list_push(k,3049323471); list_push(k,3921009573);
    list_push(k,961987163); list_push(k,1508970993); list_push(k,2453635748); list_push(k,2870763221);
    list_push(k,3624381080); list_push(k,310598401); list_push(k,607225278); list_push(k,1426881987);
    list_push(k,1925078388); list_push(k,2162078206); list_push(k,2614888103); list_push(k,3248222580);
    list_push(k,3835390401); list_push(k,4022224774); list_push(k,264347078); list_push(k,604807628);
    list_push(k,770255983); list_push(k,1249150122); list_push(k,1555081692); list_push(k,1996064986);
    list_push(k,2554220882); list_push(k,2821834349); list_push(k,2952996808); list_push(k,3210313671);
    list_push(k,3336571891); list_push(k,3584528711); list_push(k,113926993); list_push(k,338241895);
    list_push(k,666307205); list_push(k,773529912); list_push(k,1294757372); list_push(k,1396182291);
    list_push(k,1695183700); list_push(k,1986661051); list_push(k,2177026350); list_push(k,2456956037);
    list_push(k,2730485921); list_push(k,2820302411); list_push(k,3259730800); list_push(k,3345764771);
    list_push(k,3516065817); list_push(k,3600352804); list_push(k,4094571909); list_push(k,275423344);
    list_push(k,430227734); list_push(k,506948616); list_push(k,659060556); list_push(k,883997877);
    list_push(k,958139571); list_push(k,1322822218); list_push(k,1537002063); list_push(k,1747873779);
    list_push(k,1955562222); list_push(k,2024104815); list_push(k,2227730452); list_push(k,2361852424);
    list_push(k,2428436474); list_push(k,2756734187); list_push(k,3204031479); list_push(k,3329325298);
    return k;
}

fn _sha_push_u32(out: bytes, value: int) -> void {
    bytes_push(out, (value >> 24) & 255); bytes_push(out, (value >> 16) & 255);
    bytes_push(out, (value >> 8) & 255); bytes_push(out, value & 255);
}

fn sha256(data: bytes) -> bytes {
    let original = bytes_len(data);
    let message = bytes_slice(data, 0, original);
    bytes_push(message, 128);
    while (bytes_len(message) % 64) != 56 { bytes_push(message, 0); }
    let bit_length = original * 8;
    for shift in [56,48,40,32,24,16,8,0] { bytes_push(message, (bit_length >> shift) & 255); }

    let mut h0 = 1779033703; let mut h1 = 3144134277; let mut h2 = 1013904242; let mut h3 = 2773480762;
    let mut h4 = 1359893119; let mut h5 = 2600822924; let mut h6 = 528734635; let mut h7 = 1541459225;
    let k = _sha_constants();
    let mut block = 0;
    while block < bytes_len(message) {
        let w = list<int>();
        for i in 0..16 {
            let at = block + i * 4;
            list_push(w, _u32((bytes_at(message, at) << 24) | (bytes_at(message, at + 1) << 16) | (bytes_at(message, at + 2) << 8) | bytes_at(message, at + 3)));
        }
        for i in 16..64 {
            list_push(w, _u32(_sha_small1(list_at(w, i - 2)) + list_at(w, i - 7) + _sha_small0(list_at(w, i - 15)) + list_at(w, i - 16)));
        }
        let mut a=h0; let mut b=h1; let mut c=h2; let mut d=h3; let mut e=h4; let mut f=h5; let mut g=h6; let mut h=h7;
        for i in 0..64 {
            let t1 = _u32(h + _sha_big1(e) + _sha_ch(e,f,g) + list_at(k,i) + list_at(w,i));
            let t2 = _u32(_sha_big0(a) + _sha_maj(a,b,c));
            h=g; g=f; f=e; e=_u32(d+t1); d=c; c=b; b=a; a=_u32(t1+t2);
        }
        h0=_u32(h0+a); h1=_u32(h1+b); h2=_u32(h2+c); h3=_u32(h3+d);
        h4=_u32(h4+e); h5=_u32(h5+f); h6=_u32(h6+g); h7=_u32(h7+h);
        block = block + 64;
    }
    let out = bytes();
    _sha_push_u32(out,h0); _sha_push_u32(out,h1); _sha_push_u32(out,h2); _sha_push_u32(out,h3);
    _sha_push_u32(out,h4); _sha_push_u32(out,h5); _sha_push_u32(out,h6); _sha_push_u32(out,h7);
    return out;
}

fn sha256_text(value: str) -> bytes { return sha256(bytes_from_text(value)); }
fn sha256_hex(data: bytes) -> str { return hex_encode(sha256(data)); }
fn sha256_text_hex(value: str) -> str { return sha256_hex(bytes_from_text(value)); }

fn hmac_sha256(key: bytes, message: bytes) -> bytes {
    let mut normalized = key;
    if bytes_len(normalized) > 64 { normalized = sha256(normalized); }
    let inner_key = bytes(); let outer_key = bytes();
    for i in 0..64 {
        let mut value = 0; if i < bytes_len(normalized) { value = bytes_at(normalized, i); }
        bytes_push(inner_key, value ^ 54); bytes_push(outer_key, value ^ 92);
    }
    return sha256(bytes_concat(outer_key, sha256(bytes_concat(inner_key, message))));
}
fn hmac_sha256_hex(key: bytes, message: bytes) -> str { return hex_encode(hmac_sha256(key, message)); }
fn hmac_sha256_text_hex(key: str, message: str) -> str { return hmac_sha256_hex(bytes_from_text(key), bytes_from_text(message)); }

fn constant_time_equal(left: bytes, right: bytes) -> bool {
    if bytes_len(left) != bytes_len(right) { return false; }
    let mut difference = 0;
    for i in 0..bytes_len(left) { difference = difference | (bytes_at(left,i) ^ bytes_at(right,i)); }
    return difference == 0;
}
