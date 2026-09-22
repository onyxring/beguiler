# About This Specification

<!-- toc -->
- [Purpose and Scope](#purpose-and-scope)
- [Audiences](#audiences)
- [What This Specification Is Not](#what-this-specification-is-not)
- [How to Read It](#how-to-read-it)
- [Conventions](#conventions)
  - [Typography](#typography)
  - [Syntax Notation](#syntax-notation)
  - [Examples](#examples)
  - [Asides](#asides)
  - [Target Markers](#target-markers)
  - [Cross-References](#cross-references)
  - [Entry Shape](#entry-shape)
<!-- /toc -->


*Draft, September 2026. Describes the language as implemented by the current Beguiler compiler and
beguiLib.*

## Purpose and Scope

This specification is the reference for Beguile. It is organized in three parts and a set of
appendices:

- **Part I — The Beguile Language** (§1–§15) defines the language.
- **Part II — The Beguiler Compiler** (§16–§20) describes the compiler: how it is invoked, its
  settings, the compilation pipeline from source to story file, its diagnostics and its outputs.
- **Part III — The Beguile Language Runtime** (§21–§23) describes the library every program compiles
  against: the runtime core, the opt-in language extensions and the IF library bindings.
- **Appendices A–J** collect the lookup tables: reserved words, directives, operators, character
  escapes, settings, pre-defined symbols, substitution tokens, the glossary, a symbol index and a table
  of limits.

## Audiences

- **Game authors** writing interactive fiction in Beguile against one of the supplied library bindings
  (Part I; §23).
- **Library and binding authors** exposing an Inform 6 library to Beguile or writing reusable Beguile
  code (§7, §15, Part III).
- **Inform 6 developers** adopting Beguile incrementally inside an existing `.inf` project (§15).
- **Tool developers** building editors, debuggers and build integrations around the compiler's inputs
  and outputs (Part II).

## What This Specification Is Not

It is not an Inform 6 manual. The Inform 6 language, the libraries commonly used with it, and the
Z-machine and Glulx virtual machines are documented elsewhere and are assumed.

It is not a tutorial. Readers new to Beguile should start with the
[Quick Start](../quickStart.md). Readers coming from Inform 6 should start with
[Beguile for the I6 Developer](../Beguile%20for%20the%20I6%20Developer.md), which teaches the language
through a complete, runnable port and maps each construct back to the Inform 6 it replaces. This
specification is the reference to graduate to once the language is familiar.

## How to Read It

Part I is written in dependency order: each chapter assumes only the vocabulary of the chapters before
it, and where a rule needs a later construct the entry gives a one-sentence version and a forward
reference. Part I can therefore be read front to back. Parts II and III and the appendices are
reference material for lookup.

For a runtime or library name, start at Appendix I; for a reserved word, Appendix A; for an operator,
Appendix C; for a numeric limit, Appendix J. The terms of art used throughout — program, target,
directive, emitter, island, binding and the rest — are defined once, in the Glossary (Appendix H).

The conventions below apply to every chapter. The language itself is introduced in the next chapter,
*Introduction to Beguile*.

## Conventions

### Typography

Identifiers, keywords, operators, file names and any other literal source text appear in `code font`.
Keywords are written lowercase; Beguile is case-insensitive (§1.3), so `Print` and `print` are the
same word. A term is set in **bold** where it is defined. Inform 6 is abbreviated I6.

Within an entry, a paragraph that treats one alternative or one aspect of the rule opens with a bold
run-in head ending in a period, such as **Placement.** or **Capture.** The run-in is a label, not a
heading, and the paragraphs under one entry may be read in any order.

### Syntax Notation

Syntax forms are given in fenced blocks tagged `syntax`:

```syntax
[ ⟨qualifier⟩ … ] ⟨type⟩ ⟨name⟩ [ = ⟨initializer⟩ ] ;
```

- `⟨name⟩` — a placeholder, in angle quotes and lowercase, for author-supplied text.
- `[ … ]` — an optional part.
- `…` — repetition of the preceding part.
- `|` — alternation between parts.
- Every other character is literal.

The form above reads: zero or more qualifiers, then a type, then a name, then an optional initializer
introduced by `=`, then a semicolon. Where a form uses `[`, `]`, `|` or `<` as Beguile tokens (array
sizes, union types, type parameters), the sentence under the block says so.

A placeholder may also stand for part of a name. `split⟨direction⟩⟨kind⟩` (§22.7.3) names one method
for every combination of the listed values, spelled as a single identifier (`splitUpGrid`); the
sentence under the block lists the values. In running text and in tables, a form is abbreviated with
capitalized words in place of angle quotes: `Type::operator op` stands for `⟨type⟩::operator ⟨op⟩`
(§4.15), and `obj.member` for `⟨expr⟩.⟨member⟩`.

### Examples

An example is the smallest fragment that shows the rule. A `// →` comment gives the resulting value or
the text printed. Program output appears in blocks tagged `text`. Inform 6 appears in blocks tagged
`i6`, and only in sections about emission (Part II and §15); emitter bodies, which are raw I6 by
definition, are exempt. Examples use real keywords and real library names, and `Main` as the entry
point, so that they compile in principle. An example that uses a name declared by the Inform 6
Standard Library (`location`, `player`, `light`, `selfobj`, …) assumes `#include <i6StandardLibrary>`
(§23.2) and leaves the name undeclared.

### Asides

Material a reader may skip on a first reading — a consequence of the rule, a caution, a limit — is set
apart from the rule as an aside: a block quote that opens with a bold label.

> **Label.** The aside's text.

Target markers are asides of this kind.

### Target Markers

Behavior that depends on the virtual machine is marked `[Z-machine]`, `[Glulx]` or
`[Z-machine/Glulx difference]`, either inline or as an aside:

> **[Glulx]** Applies only when the target is Glulx.

Unmarked text applies to both targets.

### Cross-References

`§N.M` and `§N.M.K` refer to a section; inside a chapter they refer to that chapter's own sections. A
bare chapter number, `§N`, is used only when the whole chapter is meant. `Appendix X` refers to an
appendix.

### Entry Shape

Every language construct (keyword, directive, operator, declaration form, library method) is
documented as an entry with up to five labeled parts, in this order: **Syntax** (the form, in the
notation above), **Description** (the normative rule, including any shorter form that is equivalent to
it), **Example**, **Notes** (target markers, asides, limits) and **See also** (references to sections
that are not neighbors of the entry). A part that is genuinely empty is omitted. Chapters 16, 18, 19,
20 and 23 are narrative and do not use the shape; the extension entries of §22 use their own shape,
announced in §22.1.
