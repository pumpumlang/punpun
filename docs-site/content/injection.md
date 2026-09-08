# Foreign Injection

`@inject->c` is explicit interoperability, not PunPun's normal compiler pipeline.

```punpun
@inject->c("""
#include <stdint.h>
int64_t native_add(int64_t a, int64_t b) { return a + b; }
""");
extern native fn native_add(a: i64, b: i64) -> i64;
```

Injected C is content-addressed and compiled only during builds, never during hover or `pp check`. Unchanged foreign objects are reused from the build cache.
