# Byte run-length encoding. Excellent for repetitive masks/images, intentionally
# simple and deterministic. Pairs are (count,value), count 1..255.
fn rle_compress(data: bytes) -> bytes {
    let out=bytes(); if bytes_len(data)==0{return out;}
    let mut value=bytes_at(data,0); let mut count=1;
    for i in 1..bytes_len(data) {
        let current=bytes_at(data,i);
        if current==value && count<255 { count=count+1; }
        else { bytes_push(out,count); bytes_push(out,value); value=current; count=1; }
    }
    bytes_push(out,count); bytes_push(out,value); return out;
}
fn rle_decompress(data: bytes) -> bytes {
    if bytes_len(data)%2!=0 { panic("invalid RLE stream"); }
    let out=bytes(); let mut i=0;
    while i<bytes_len(data) { let count=bytes_at(data,i); let value=bytes_at(data,i+1); if count==0{panic("invalid zero RLE run");} for n in 0..count {bytes_push(out,value);} i=i+2; }
    return out;
}
