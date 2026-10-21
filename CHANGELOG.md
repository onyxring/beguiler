# Changelog

## [Unreleased]

## [0.1.0-preview.5] - 2026-10-20

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
- `alias name = obj.name;` at file scope makes an object's emitters callable as
  globals. `print`, `printLine` and `printName` are global this way; `log()` is
  now `bgl.output.log()`, so authors can name objects `log`.
- `replace` keeps a member's kind: an accessor can be replaced only by an
  accessor, and a value only by a value.
- The library compiles under the same rules as any program; the compiler no
  longer asks where code comes from.
- One rule decides Inform 6 name clashes: a routine, global, array or object
  named like a member property is emitted as `_g_<name>`; a member yields
  (`_m_<name>`) only to externs, `asI6` names and other fixed names. Raw Inform 6
  names a renamed global with `$i6Name(x)`; its bare name is an error.
- A later `extern` of a routine from another file replaces the earlier
  signature, whoever declared it.
- An extension can declare int settings of its own (`extend class
  beguilerSettingsType { int x; }`) and read them as `#beguilerSettings.x`.
- A member initializer can read an extern only if it is `const`: declare an
  Inform 6 object typed by its class as `extern const Tray trayA;`.

Library (beguiLib):
- New layout: core, language extensions at the root (`#include <array>`),
  `bindings/`, `extensions/{general,stdlib,puny}/` and `libraries/`. Core no
  longer refers to any library.
- `<glulxWindow>` and `<glulxImage>` for Glulx windows and images.
- The first official extension, `<extensions/stdlib/remoteLook>`: look in a
  direction and see into the next room.
- Doc comments across the author-facing surface; article print rules moved to
  `bgl.printRules`.
- `<extensions/general/center>`: `bgl.ui.center()` and `bgl.ui.inset()` print
  text centered or between margins, word-wrapped, in any window.
- `bgl.output` holds every output routine: `print()`, `printLine()`,
  `printName()`, `spaces()` and `log()`. Types add their `print()` overloads
  there with `extend _bglOutput`.
- `bgl.ui.mainWin.width`, `bgl.ui.statusBar.width` and
  `bgl.ui.statusBar.moveCursor()` work on both targets without `<glulxWindow>`.
- `<buf>` ships only the methods a program uses.
- `<stringObj>`, an alternative to `<string>` (include one or the other): each
  `stringObj` buffer is a block in a string heap and grows as text is written
  into it, so a string is no longer limited to 494 characters. Its sizes are the
  `stringHeapSize` and `stringCaptureSize` settings, and in a debug build the
  `heap` verb reports the most the run needed, for sizing it for release.

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

Fixes:
- On the Z-machine, a capture or a literal longer than its string's buffer
  was written past the buffer (with `<string>`, over the next string) and then
  silently cut off. The program now stops with an error naming the setting to
  raise, on both targets. With `<stringObj>`, a long literal is kept whole when
  the heap has room.
- On the Z-machine, `bgl.ui.statusBar.id` is now an accessor that returns `0`;
  it was a plain `null` value, and code that read it as an accessor failed at
  run time.
- A method declared below its caller and returning `array<T>` keeps its
  element type in the call.
- A routine no longer sweeps fresh arrays after every statement when its calls
  return file-scope arrays, or when a local takes a call's whole result.
- An emitter body naming an Inform 6 compiler constant such as `WORDSIZE` is no
  longer rejected when a Beguile emitter shares its name.

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
