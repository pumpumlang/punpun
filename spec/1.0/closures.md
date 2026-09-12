# Capturing closures

This document defines the stable 1.x semantics of a function literal used as a
value.

## Function values

`fn(T...) -> R` is the source-level type of both named functions and function
literals. The type describes only the callable signature; captured values are
not part of type identity.

A named function used as a value is a zero-capture closure. A function literal
such as `fn(x: int) -> int { ... }` may capture free local bindings that are in
scope at the point where the literal is evaluated.

## Capture mode

Captures are implicit and by value.

- A `Copy` binding is copied into the closure environment.
- A move-only binding is moved into the environment. Using the original binding
  afterwards is a use-after-move error.
- Binding mutability is preserved inside the environment. A mutable capture may
  be assigned by the closure and its updated value persists across later calls.
- Updating a captured `Copy` value does not update the creating scope's copy.
- Copying a function value copies the closure handle, not its environment. Both
  handles therefore observe the same persistent mutable capture state.

Capture happens when the function literal is evaluated, not when it is later
called.

## Name resolution

Parameters and locals declared inside the literal shadow outer bindings. Module
functions, builtins, types, and other module-level declarations are resolved
normally and are not captured.

Nested literals propagate free names through intermediate closure environments,
so an inner closure may safely capture a local from a grandparent function.

## Borrowed values

A closure may escape the stack frame that created it. Until PunPun has
lifetime-aware closure escape analysis, the following borrowed values may not
be captured:

- `&T`
- `&mut T`
- `Slice<T>`

Such a capture is rejected with `E0803` rather than risking a dangling borrow.
An explicitly unsafe raw pointer remains a raw pointer and is governed by the
existing unsafe-pointer rules.

## Representation contract

The concrete environment layout is an implementation detail and is not part of
the source ABI. Implementations must preserve these observable properties:

1. an escaping closure keeps its owned environment alive;
2. mutable capture state survives calls;
3. copied closure handles share one environment;
4. value-semantic aggregates captured by value do not alias the creating
   scope's aggregate;
5. all supported backends produce equivalent behavior.
