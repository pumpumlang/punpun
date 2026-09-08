# PunPun 0.6 types and generics

## Type identity

- Types are nominal unless documented as built-in structural types.
- `Container<A>` and `Container<B>` are distinct constructed types when `A` and `B` differ.
- Generic arguments are invariant in 0.6.
- Aliases do not create a new nominal type; structs, objects and enums do.
- No implicit numeric, text, nullable or reference conversion participates in generic inference.

## Inference

Generic arguments are inferred from call arguments first and the expected result type second. Every type parameter must resolve to exactly one concrete type. Conflicting or unconstrained parameters are compile errors; PunPun does not silently choose `any`, `object` or a default type.

## Constraints

A constraint names a contract. `T: Copy + Comparable<T>` requires all listed contracts. Constraint satisfaction is checked before specialization. Generic bodies may use only operations provided by the constraints or universally available operations.

`Copy` is compiler-known: copying duplicates a value with no ownership transfer. User code cannot falsely implement `Copy` for a type whose fields require destruction.

## Monomorphization

PunPun specializes generic functions and types for the canonical concrete type tuple used by the program. The specialization key includes:

- the generic declaration's interface fingerprint;
- canonical type arguments;
- target and ABI;
- optimization mode;
- compiler version.

Equivalent requests must produce one deterministic specialization. Deduplicating machine code for layout-identical specializations is permitted but must not alter type identity or observable behavior.

Recursive specialization must make structural progress. The compiler applies a deterministic depth/work limit and reports a diagnostic instead of exhausting host memory.

## Overload resolution

Candidate selection follows this order:

1. arity, named arguments and visibility;
2. exact non-generic parameter matches;
3. viable generic inference and satisfied constraints;
4. specificity comparison between remaining candidates.

There are no user-defined or implicit numeric conversions during selection. Two equally specific candidates are ambiguous and produce a diagnostic containing both signatures. Return type alone cannot select an overload.

Default arguments are substituted after overload selection. Named arguments identify parameters before generic inference and cannot be duplicated.
