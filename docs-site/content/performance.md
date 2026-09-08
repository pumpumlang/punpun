# Performance

Compiler timings are measured, not marketing constants. Use:

```sh
pp build --timings
pp build --stats
pp build --cache-info
```

The build fingerprint hashes source/dependency contents, compiler configuration, runtime artifact and selected native toolchain. Unchanged builds reuse the executable; edited source invalidates it even when file timestamps are deliberately preserved.
