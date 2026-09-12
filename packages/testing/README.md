# testing

First-party testing helpers backed by `std.testing`.

```sh
ppx add testing
```

Includes scalar/approximate assertions, `TestSuite` failure aggregation and
closure-based benchmark helpers. The compatibility package keeps the original
assertion names while new code can import `std.testing` directly.
