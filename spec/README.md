# PunPun language specification

This directory records normative language decisions separately from implementation notes.

- [`0.6/syntax.md`](0.6/syntax.md) defines the frozen surface grammar.
- [`0.6/types-and-generics.md`](0.6/types-and-generics.md) defines generic identity, constraints, specialization and overload selection.
- [`0.6/enums-and-matching.md`](0.6/enums-and-matching.md) defines algebraic enums, `Option`, `Result`, destructuring and exhaustiveness.
- [`0.6/ownership.md`](0.6/ownership.md) defines ownership, moves, borrows and destruction.
- [`0.6/compatibility.md`](0.6/compatibility.md) defines the 0.5-to-0.6 source-compatibility gate.

Keywords or grammar may be recognized before their semantics are enabled. Such syntax must fail with a stable diagnostic and must never silently compile with placeholder behavior.

## Stable 1.x contract

The normative stable compatibility contract is in [`1.0/`](1.0/).
