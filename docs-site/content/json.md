# JSON

The bundled dependency-free `json` beta package provides validation, simple object field extraction, and JSON string quoting.

```punpun
bring json;
launch {
    let data = "{\"name\":\"PunPun\",\"n\":42}";
    assert(json_valid(data), "valid JSON");
    say(json_get_string(data, "name", "missing"));
    say(json_get_i64(data, "n", 0));
}
```

Typed reflection-driven serialization is planned but not represented here as finished functionality.
