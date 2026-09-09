# Changelog

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
