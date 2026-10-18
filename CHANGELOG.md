# Changelog

## [0.1.0-preview.5] - 2026-10-18

Language:
- Type kinds: primitive, reference and value types, with two roots (`_bglObject`
  for object-backed classes, `_bglPrimitive` for word values); `object` is no
  longer special to the compiler.
- `struct`: value types with an automatic copy operator. A plain class-typed
  declaration owns an instance; `ref` refers to one, bound with `:=`; `:==` is
  identity comparison; copy operators on reference classes.
- Arrays own their storage: `=` copies, `ref`/`:=` shares. Local arrays and
  `<linq>` results live on a heap that is collected at the prompt (`bgl.gc`).
- `superposed` methods in object bodies are kept only when called; `<array>`
  ships only the methods a program uses.
- `property x;` declares an individual property, with no Z-machine limit.
- A class-typed object member takes its instance from a body:
  `Pet dog { "rex", 3; }`.
- `extend class` on an extern class types the properties Inform 6 provides;
  `extern` on an extend is an error.
- Also: named arguments in any order, `<=>` derives the ordering operators,
  Z-machine `try`/`catch`, `asI6` / `asBgl` name clauses, emitter chaining with
  `$i6Name` / `$i6Expr`, enum and bnum emitter methods, Glulx hyperlinks,
  `literal` values, bare `=>` lambdas, a ternary over unrelated types resolves to
  their union, and order-independent forward references throughout.

Library (beguiLib):
- New layout: core, language extensions at the root (`#include <array>`),
  `bindings/`, `extensions/{general,stdlib,puny}/` and `libraries/`. Core no
  longer refers to any library.
- `<glulxWindow>` and `<glulxImage>` for Glulx windows and images.
- The first official extension, `<extensions/stdlib/remoteLook>`: look in a
  direction and see into the next room.
- Doc comments across the author-facing surface; article print rules moved to
  `bgl.printRules`.

Editor and debugger (with beguilex):
- The debugger reads arrays in their current layout, shows signed numbers,
  chars, floats, bit flags and null objects, and evaluates `obj.member`,
  `arr[i]` and `arr.length` in Watch and hover; conditional breakpoints, hit
  counts and logpoints; values can be edited.
- The language server reports warnings, renames and finds references
  case-insensitively, recovers from errors in class bodies, reads include paths
  from `#beguilerSettings`, and coalesces fast typing into one reparse.

Documentation:
- The language specification is restructured under `docs/spec/`, with
  Appendix K for gotchas; `languageSpec.md` is generated from it.

Quality:
- Hardening rounds (field trials, spec doc tests, a feature-interaction matrix,
  LSP probes, heap and measurement rounds) found and fixed several hundred
  defects; the regression suite is 998 tests.

## [0.1.0-preview.4] - 2026-09-09

Language:
- Typed properties: every property declaration now carries a type, not only
  `extern` ones. Additive and raw properties use `rawArray<T>`, and raw member
  array layout is keyed on additivity, with a correct extent.
- Member arrays: `array<T>` and `rawArray<T>` work as object and class members,
  each with a real length slot and addressed as (owner, property); an oversized
  member array is promoted to a synthesized global; `ref array<T>` members and a
  member's `as <i6name>` alias are honoured at every access site.
- References: `:=` is the reference binding (rebinding) operator; `ref` leaves a
  member empty until bound; `=` assigns through a reference, dispatching
  `operator=` into the referent. `=` on a `ref` slot is now a hard error.
- Strings: `string` is now a lightweight text pointer; owned string storage is
  `stringObj`, and owned-slot writes are enforced.
- Operators: `Type::operator <op>` is a user-facing operator reference;
  `operator <=>` may be an instance operator; overloaded operator names are
  discriminated on the instance side; and a literal argument matches an operator
  declared for its base type.
- Computed access: `obj.p` where `p` holds a property, including the statement
  form of a computed message send.
- Owning arrays: automatic allocation and destruction of element storage for
  arrays whose elements own storage; `indexOf` scoped to `length()`.
- `superposed class`: the I6 `Class` directive is withheld and emitted only when
  the name is referenced (a static instance, subclass, `new`, or `ofclass`),
  base classes first; a class nothing references costs zero bytes.
- `bgl.ui.statusBar.height` get/set property, auto-included but zero-cost when
  unused; the standard-library binding defers it to the library's status window.

Build / portability:
- The `<format>` dependency is gone: the toolchain floor is now C++17 (was C++20),
  and `std::format` is used automatically when the compiler provides it, so the
  build works at any standard from C++17 up. One build command line per platform,
  with no per-OS defines, forced includes, or library installs.
- The build tracks header dependencies for incremental `make`.

Fixes:
- `stringObj.trimRight` no longer trims from the left.
- Subscript writes work again after the operator-reference unification.

Editor / LSP:
- Starting a Run now stops any already-running Beguile debug session instead of
  stacking a second one on top.

## [0.1.0-preview.3] - 2026-08-23

Language:
- Inline object declarations — `Type{ … }` anonymous objects in expression and
  statement position: an `inline` member modifier for positional slots; comma
  separates positional values, the first `;` transitions to named `field = value;`
  members; type inference from the target (array elements, variable initializers,
  and `inject`); nested aggregate field values; and a standalone no-name form.
- Declarative array editing — `extend <array> { inject / remove / move … }`,
  applied at compile time with no runtime cost.
- `func<>` usable as a type argument (array element / member type).
- Array `+=` / `-=` operators.

Fixes:
- Subscript-then-member access on object arrays (`arr[i].member`) no longer reads
  the wrong slot (Inform 6 binds `.` tighter than `-->`; now parenthesized).
- Unicode / smart-quote handling in string literals.
- Current-file-relative (`>`) includes when the transpiled `.inf` moves to `output/`.

Editor / LSP:
- Colors the new `inline` keyword; context-aware completion for `extend` bodies
  (verb grammar, array `inject`/`remove`/`move`) and class/object member modifiers.
- Semantic tokens: multi-line interpolated strings, `#using`-imported members, and
  UTF-16 column alignment on lines with non-ASCII (smart-quote) text.

## [0.1.0-preview] - 2026-08-16

Initial public preview release.

Beguile is a modern language for interactive fiction that transpiles to
Inform 6 and targets the Z-Machine (v3/v5/v8) and Glulx. This first preview
includes the Beguiler compiler, both library bindings (PunyInform and the
Inform 6 Standard Library), and the Beguilex VS Code extension with syntax
highlighting, language support, and step debugging.

This is an experimental preview. The language and compiler are evolving
actively; expect breaking changes between preview versions.
