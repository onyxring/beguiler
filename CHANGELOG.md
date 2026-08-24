# Changelog

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
