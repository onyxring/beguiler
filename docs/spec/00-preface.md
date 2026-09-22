# Preface

<!-- toc -->
- [What Beguile is](#what-beguile-is)
- [Design goals](#design-goals)
- [Relationship to Inform 6](#relationship-to-inform-6)
- [Audiences](#audiences)
- [What this specification is not](#what-this-specification-is-not)
- [Scope](#scope)
- [How to read this specification](#how-to-read-this-specification)
<!-- /toc -->

*Draft, September 2026. Describes the language as implemented by the current Beguiler compiler and beguiLib.*


## What Beguile is

Beguile is a statically typed, compiled language for authoring interactive fiction. It provides a
structured, C-like syntax that transpiles to Inform 6 and, from there, to a Z-machine or Glulx story
file.

Although it borrows features from general-purpose languages, Beguile is not one. Its features are shaped
by the needs of interactive fiction and by the constraints of the virtual machines its programs
ultimately run on.

## Design goals

- **Familiar syntax.** Beguile reads like C or a C-adjacent language. Developers comfortable with C,
  C++, C#, TypeScript or similar languages should find the syntax intuitive.
- **Strong typing.** Every variable, parameter and return value carries a declared type. Type mismatches
  are caught at compile time, not at run time.
- **Transparency.** The generated Inform 6 is readable and maps closely to the Beguile source.
  Developers who know Inform 6 can inspect or supplement the output.
- **Extensibility through emitters.** Performance-sensitive or platform-specific operations can be
  expressed as *emitters*: inline Inform 6 fragments substituted at the call site. Library authors get
  full control over the generated code without giving up type safety at the Beguile level.
- **One consistent core.** Declarations live in member bodies, statements end in `;`, types precede
  names. Where strict adherence would mean boilerplate, a small number of shorthand forms desugar to
  that core (see *Shorthand* in Conventions).

## Relationship to Inform 6

Beguile is built on top of Inform 6; it is not a replacement for it. It generates human-readable Inform
6 source, and several features exist specifically to bridge the two languages:

- `extern` declarations let Beguile use types, functions, attributes and constants defined in Inform 6
  without re-implementing them.
- `#i6` islands place raw Inform 6 inline with Beguile code.
- `#includeI6` passes an Inform 6 include directive through to the generated file.
- Emitter bodies contain literal Inform 6, giving library authors precise control over the output.

Authors who need capabilities beyond what Beguile exposes can always drop down to Inform 6 through these
mechanisms (§15).

## Audiences

- **Game authors** writing interactive fiction in Beguile against one of the supplied library bindings
  (Part I; §23).
- **Library and binding authors** exposing an Inform 6 library to Beguile or writing reusable Beguile
  code (§7, §15, Part III).
- **Inform 6 developers** adopting Beguile incrementally inside an existing `.inf` project (§15).
- **Tool developers** building editors, debuggers and build integrations around the compiler's inputs
  and outputs (Part II).

## What this specification is not

It is not an Inform 6 manual. The Inform 6 language, the libraries commonly used with it, and the
Z-machine and Glulx virtual machines are documented elsewhere and are assumed.

It is not a tutorial. Readers new to Beguile should start with [quickStart.md](../quickStart.md).
Readers coming from Inform 6 should start with
[Beguile for the I6 Developer](../Beguile%20for%20the%20I6%20Developer.md), which teaches the language
through a complete, runnable port and maps each construct back to the Inform 6 it replaces. This
specification is the reference to graduate to once the language is familiar.

## Scope

This specification describes the language as implemented by the current Beguiler compiler and
beguiLib. Part I defines the language. Part II describes the compiler, including the compilation
pipeline from source to story file (§18). Part III describes the Beguile Language Runtime that every
program compiles against. Terms of art are collected in Appendix H.

## How to read this specification

Part I is written in dependency order: each chapter assumes only the vocabulary of the chapters
before it, and where a rule needs a later construct the entry gives a one-sentence version and a
forward reference, so it can be read front to back. Parts II and III and the appendices are reference material for lookup. Every
construct's entry has the same shape (syntax, description, example, notes), described in the
Conventions chapter. Terms of art are defined in the Glossary (Appendix H). For a runtime or library name, start at
Appendix I; for a keyword, Appendix A; for an operator, Appendix C.
