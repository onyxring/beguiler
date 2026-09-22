# Introduction to Beguile

<!-- toc -->
- [What Beguile Is](#what-beguile-is)
- [Design Goals](#design-goals)
- [Relationship to Inform 6](#relationship-to-inform-6)
<!-- /toc -->


This chapter introduces the language itself: what Beguile is, the goals that shaped it, and how it
relates to Inform 6. The chapters of Part I then define it construct by construct.

## What Beguile Is

Beguile is a statically typed, compiled language for authoring interactive fiction. Its structured
syntax transpiles to Inform 6 and, from there, to a Z-machine or Glulx story file.

Although it borrows features from general-purpose languages, Beguile is not one. Its features are
shaped by the needs of interactive fiction and by the constraints of the virtual machines its programs
ultimately run on.

## Design Goals

- **Familiar syntax.** Beguile's syntax draws on C++, C# and TypeScript. Developers comfortable with
  those languages should find it intuitive.
- **A consistent syntax.** Beguile is designed upon a foundation of core rules — declarations live in
  member bodies, statements end in `;`, types precede names — and every construct follows them. The
  language seeks to minimize targeted micro-grammars for special features, in favor of general-purpose
  constructs that work the same way everywhere. Inform 6's verb grammar shows the alternative:
  `Verb 'take' * noun -> Take;` is a sub-language with its own tokens and punctuation, usable only in
  that directive. In Beguile a verb is an object, its grammar is a member initialized with an ordinary
  list, and changing it later uses the same `extend`, `+=` and `replace` that apply to any object
  (§13).
- **Strong typing.** Every variable, parameter and return value carries a declared type. Type mismatches
  are caught at compile time, not at run time.
- **Transparency.** The generated Inform 6 is readable and maps closely to the Beguile source.
  Developers who know Inform 6 can inspect or supplement the output.
- **Extensibility through emitters.** Performance-sensitive or platform-specific operations can be
  expressed as **emitters**: inline Inform 6 fragments substituted at the call site (§7). Library
  authors get full control over the generated code without giving up type safety at the Beguile level.

## Relationship to Inform 6

Beguile is built on top of Inform 6; it is not a replacement for it. It generates human-readable
Inform 6 source, and several features exist specifically to bridge the two languages:

- `extern` declarations let Beguile use types, functions, attributes and constants defined in Inform 6
  without re-implementing them.
- `#i6` islands place raw Inform 6 inline with Beguile code.
- The emitter subsystem gives authors precise control over the Inform 6 that is generated.

Authors who need capabilities beyond what Beguile exposes can always drop down to Inform 6 through these
mechanisms (§15).
