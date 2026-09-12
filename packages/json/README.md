# json

First-party compatibility package backed by the full PunPun-written
`std.data.json` implementation.

```sh
ppx add json
```

The package preserves the small 1.x helpers (`json_valid`, `json_get_string`,
`json_get_i64`, `json_quote`) and also exposes `json_decode`, `json_encode` and
`json_encode_pretty`. New code may import `std.data.json` directly for the full
`JsonValue`/`JsonParseResult` surface.

The parser/serializer is PunPun code. There is no injected C JSON parser.
