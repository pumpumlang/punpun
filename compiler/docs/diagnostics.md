# ppc diagnostics

Every diagnostic ppc can emit, with what it means and how to resolve it.
`ppc explain E0800` prints one of these entries at the terminal.

Codes are grouped by the phase that produces them, so the number alone tells
you where in the pipeline a problem was found.

---

**Lexical — E0001 to E0099**

## E0001 — Unexpected character in source

A byte appeared that cannot begin any token.

**Fix.** Remove the stray character. If it is inside text, put it in a string literal.

## E0002 — Unterminated string literal

A string was opened but never closed before the end of the line or file.

**Fix.** Close it with `"`. Use `"""` for text that must span lines.

## E0003 — Unknown string escape

A backslash was followed by a character that is not a recognized escape.

**Fix.** Supported escapes are `\n`, `\t`, `\r`, `\0`, `\"`, and `\\`. Write `\\` for a literal backslash.

## E0004 — Unmatched delimiter

A closing bracket appeared with no matching opener.

**Fix.** Remove the extra bracket, or add the opener it was meant to close.

## E0005 — Invalid numeric literal

The literal does not fit the type it would have.

**Fix.** `int` is a signed 64-bit value, so it holds -9223372036854775808 through 9223372036854775807.


---

**Syntax — E0100 to E0199**

## E0100 — Unexpected token

The parser found a token that cannot appear in this position.

**Fix.** Check the surrounding syntax. The message names what was expected.

## E0101 — Expected end of statement

Two statements ran together with nothing separating them.

**Fix.** End the statement with a newline or `;`.

## E0102 — Reserved word used as a name

A keyword or primitive type name was used to name something.

**Fix.** Pick a different name. Reserved words cannot be redefined, in either dialect.

## E0103 — Mixed dialect construct

Two dialect forms were combined in a way that has no meaning.

**Fix.** Both dialects may be used in one project, and even in one file, but a single construct has to pick one.

## E0104 — Expected a type

A type annotation was required here and was missing or malformed.

**Fix.** Write `name: Type` in the modern dialect, or `name as Type` in the migration dialect.

## E0105 — Duplicate parameter name

Two parameters of the same function share a name.

**Fix.** Rename one. Otherwise a named argument could not say which it meant.

## E0106 — Required parameter after a defaulted one

A parameter with no default follows one that has a default.

**Fix.** Move the required parameters first, or give this one a default too.

## E0107 — Unclosed block

A block was opened but never closed.

**Fix.** Close it with `}`, or `done` if it opened with `:`.


---

**Modules — E0200 to E0299**

## E0200 — Module not found

An import named a module that is not on any search path.

**Fix.** Check the spelling. Pass `--stdlib DIR` if the standard library is not beside the compiler.

## E0201 — Circular import

A module imports itself, directly or through a chain.

**Fix.** Move the shared declarations into a third module that both can import.

## E0202 — Duplicate definition

Two declarations in the program share a name.

**Fix.** Rename one of them.

## E0203 — No entry point

The program has nothing to run.

**Fix.** Add a `launch { ... }` block or a `fn main()`.

## E0204 — More than one entry point

Several `launch` blocks or `main` functions were found.

**Fix.** A program has exactly one. Keep one and make the others ordinary functions.


---

**Name resolution — E0300 to E0399**

## E0300 — Name not found

An identifier does not resolve to anything in scope.

**Fix.** Check the spelling, declare it with `let`, or import the module that provides it.

## E0301 — No such field

The field does not exist on this type.

**Fix.** The message lists the fields the type does have.

## E0302 — No such method

The method does not exist on this type.

**Fix.** Check the spelling, or add the method to the type.

## E0303 — Type not found

A type name does not resolve.

**Fix.** Check the spelling, or import the module that defines it.

## E0304 — No such variant

The enum has no variant by that name.

**Fix.** Variants live in their enum's namespace and are written `Enum::Variant`.

## E0305 — Not callable

The expression being called is not a function.

**Fix.** A function must be called with `()`. PunPun has no first-class functions yet, so a bare name is not a value.

## E0306 — Private member

A `private` field or method was used from outside its own type.

**Fix.** Access it through a `public` method, or change the declaration.


---

**Types — E0400 to E0499**

## E0400 — Type mismatch

A value of one type was used where another was required.

**Fix.** PunPun has no implicit conversions, deliberately: convert explicitly, for example with `text(...)` or `decimal(...)`.

## E0401 — Cannot infer a type

There is not enough context to determine a type.

**Fix.** Add an annotation, as in `let x: Option<int> = ...`.

## E0402 — Assignment to an immutable binding

A binding declared without `mut` was assigned to.

**Fix.** Declare it `let mut`, or `keep` in the migration dialect. Writing through an object handle or a list is allowed regardless, because that mutates the referent rather than the binding.

## E0403 — Missing return

A function that returns a value can reach its end without returning one.

**Fix.** Add a `return` on every path, or handle the remaining branch.

## E0404 — Invalid operand

An operator was applied to a type it is not defined for.

**Fix.** Both operands of an arithmetic or comparison operator must have the same type.

## E0405 — Condition is not a bool

A condition had some type other than `bool`.

**Fix.** Compare explicitly, for example `count != 0`. There is no truthiness in PunPun.

## E0406 — Return outside a function

`return` appeared where there is no function to return from.

## E0407 — Index is not an int

An index expression had a non-integer type.

**Fix.** Convert it, for example with `whole(...)`.

## E0408 — Type cannot be indexed

Indexing was applied to a type that does not support it.

**Fix.** Indexing works on `nums` and `Slice<int>`.

## E0409 — Dereference of a non-pointer

`*` was applied to something that is not a reference or pointer.

## E0410 — Unsafe operation outside an unsafe block

Raw pointers and native calls carry guarantees the compiler cannot check.

**Fix.** Wrap the operation in `unsafe { ... }` to state that you have checked it yourself.

## E0411 — Await outside an async context

`await` was used where there is nothing to suspend.

**Fix.** Use it inside an `async fn` or a `launch` block.

## E0412 — Value is not awaitable

`await` was applied to something that is not a task.

**Fix.** Only a call to an `async fn` produces a task.


---

**Calls and arguments — E0500 to E0599**

## E0500 — Wrong number of arguments

A call passed too many or too few arguments.

**Fix.** The message names the parameter that was missing or unexpected.

## E0501 — No parameter by that name

A named argument did not match any parameter.

**Fix.** Check the spelling against the declaration.

## E0502 — Argument given twice

The same parameter was supplied more than once.

## E0503 — Positional argument after a named one

Argument order became ambiguous.

**Fix.** Put all positional arguments before the named ones.

## E0504 — Ambiguous call

More than one overload fits equally well.

**Fix.** Give the type arguments explicitly to disambiguate.

## E0505 — No matching overload

No declaration of this name accepts these arguments.


---

**Patterns and matching — E0600 to E0699**

## E0600 — Match is not exhaustive

Some value the scrutinee could take is matched by no arm.

**Fix.** The message names an uncovered value. Add an arm for it, or a `_ =>` wildcard. Guarded arms do not count toward coverage, because a guard can fail.

## E0601 — Unreachable match arm

Every value this arm could match is already matched above it.

**Fix.** Remove it, or move it earlier.

## E0602 — Pattern cannot match this type

A pattern's shape does not fit the value being matched.

## E0603 — Wrong payload count in a pattern

A variant pattern bound a different number of values than the variant carries.

## E0604 — Invalid use of `?`

`?` requires the enclosing function to return the same outer type.

**Fix.** `?` never converts between `Option` and `Result`. Match on the value and construct the outer type yourself if you need to change it.


---

**Generics — E0700 to E0799**

## E0700 — Constraint not satisfied

A type argument does not meet a generic parameter's bound.

**Fix.** For `Copy`, the type must be freely duplicable: pass it with `move(...)`, or borrow it with `&`. For a user contract, declare the type with `meets ContractName`.

## E0701 — Wrong number of type arguments

A generic type or function was given the wrong number of type arguments.

## E0702 — Generic specialization too deep

A generic instantiates itself without ever reaching a concrete type.

**Fix.** Recursive generics must make structural progress toward a non-generic type.

## E0703 — Cannot infer a type parameter

Nothing in the call determines what a type parameter should be.

**Fix.** Write it explicitly, as in `identity<int>(x)`.


---

**Ownership and borrows — E0800 to E0899**

## E0800 — Use after move

A binding was used after its value was moved out of it.

**Fix.** Assign a new value to the binding first; whole-value assignment revives it.

## E0801 — Value may have been moved

A move happened on some paths through the program but not others.

**Fix.** Move on all paths or none, or reinitialize the binding before this use.

## E0802 — Conflicting borrow

A value was mutated while a borrow of it was still live, or two incompatible borrows overlapped.

**Fix.** Any number of shared borrows or one mutable borrow may be live, never both. A borrow lives until its binding leaves scope, so putting it in a smaller block releases it earlier.

## E0803 — Borrow outlives its owner

A borrow would still be reachable after the value it points into is gone.

**Fix.** Return the owning value itself, or copy out the parts you need.

## E0804 — Partial move

A move was attempted out of a field or element rather than a whole binding.

**Fix.** 0.6 moves whole bindings only. Move the container, or copy the part you need.

## E0805 — Move of a borrowed value

A value was moved while a borrow of it was still live.

**Fix.** Finish using the borrow first.


---

**Unsupported — E0900 to E0999**

## E0900 — Reserved syntax

The syntax is reserved for a future version of the language.

## E0901 — Unsupported construct

The construct is valid syntax but not implemented.


---

**Code generation — E1000 to E1099**

## E1000 — Backend cannot compile this

The selected backend does not support something the program uses.

**Fix.** `--backend=c` supports the whole language; the others trade coverage for compile speed.

## E1001 — Linking failed

The host toolchain rejected the generated artifact.

**Fix.** Rerun with `--keep` to inspect the generated file, and `-v` to see the exact command.

## E1002 — Host toolchain not found

ppc could not find a C compiler or the runtime source.

**Fix.** Pass `--cc` and `--runtime`, or set `PPC_RUNTIME`.

## E1003 — Internal compiler error

ppc reached a state it believes impossible.

**Fix.** This is a bug in ppc, not in your program. A reproducer would be welcome.

