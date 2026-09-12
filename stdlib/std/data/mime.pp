# Common MIME inference by file extension. Unknown files intentionally use the
# safe generic binary type instead of guessing text.
fn mime_type(path_value:str)->str {
    let ext=to_lower(path_extension(path_value));
    if ext=="html"||ext=="htm"{return "text/html; charset=utf-8";}if ext=="css"{return "text/css; charset=utf-8";}if ext=="js"||ext=="mjs"{return "text/javascript; charset=utf-8";}
    if ext=="json"{return "application/json";}if ext=="xml"{return "application/xml";}if ext=="txt"||ext=="log"{return "text/plain; charset=utf-8";}if ext=="csv"{return "text/csv; charset=utf-8";}if ext=="md"{return "text/markdown; charset=utf-8";}
    if ext=="png"{return "image/png";}if ext=="jpg"||ext=="jpeg"{return "image/jpeg";}if ext=="gif"{return "image/gif";}if ext=="webp"{return "image/webp";}if ext=="svg"{return "image/svg+xml";}if ext=="ico"{return "image/x-icon";}
    if ext=="pdf"{return "application/pdf";}if ext=="zip"{return "application/zip";}if ext=="gz"{return "application/gzip";}if ext=="wasm"{return "application/wasm";}if ext=="mp3"{return "audio/mpeg";}if ext=="mp4"{return "video/mp4";}
    return "application/octet-stream";
}
fn mime_is_text(value:str)->bool{return starts_with(value,"text/")||value=="application/json"||value=="application/xml"||value=="image/svg+xml";}
