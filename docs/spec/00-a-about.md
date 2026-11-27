# About This Specification

<!-- toc -->
- [Purpose and Scope](#purpose-and-scope)
- [What This Specification Is, And Is Not](#what-this-specification-is-and-is-not)
- [Conventions](#conventions)
  - [Typography](#typography)
  - [Syntax Notation](#syntax-notation)
  - [Examples](#examples)
  - [Notes](#notes)
  - [Target Markers](#target-markers)
  - [Cross-References](#cross-references)
  - [Entry Shape](#entry-shape)
<!-- /toc -->
## Purpose and Scope

Welcome to the Beguile Language Specification.  This document is organized into three parts, plus a few appendices:

- **Part I: The Beguile Language** (§1 to §16) covers the language itself.
- **Part II: The Beguiler Compiler** (§17 to §20) describes the compiler which processes the Beguile language.
- **Part III: The Beguile Language Runtime** (§21 to §23) goes over the runtime library every program uses, including the core and the opt-in language extensions.
- **Appendices A to K** collect the lookup tables, including reserved words, directives, operators, character escapes, etc...

## What This Specification Is, And Is Not

It ***is not*** an Inform 6 manual.  The Inform 6 language, the libraries commonly used with it, and the Z-machine and Glulx virtual machines are documented elsewhere and are assumed.

It is also ***not*** a tutorial.  Readers new to Beguile should start with the [Quick Start](../quickStart.md) document which walks you through the installation and helps you get your development environment ready.  Readers coming from Inform 6 can start with [Beguile for the I6 Developer](../Beguile%20for%20the%20I6%20Developer.md) guide, which teaches the language through a complete, runnable port and maps the syntax back to the Inform 6 it replaces.

This specification ***is*** the reference to graduate to once the language is familiar.

## Conventions

### Typography

Identifiers, keywords, operators, file names, and any other literal source text appear in `code font`.  Keywords are written lowercase, although Beguile is case-insensitive (§1.3).  A term is **bold** where it is defined.  Inform 6 is abbreviated I6.

### Syntax Notation

Syntax forms are given in fenced blocks, like so:

```syntax
[ QUALIFIER … ] TYPE NAME [ = INITIALIZER ] ;
```

Here's how to read one:

- `NAME` an uppercase word is a placeholder for text you supply.
- `[ text ]` brackets represent an optional part.
- `…` the ellipsis repeats the part before it.
- `|` the vertical bar separates alternatives.
- Every other character is literal.

The form above reads:  zero or more qualifiers, a type, a name, an optional initializer introduced by `=`, and a semicolon.  Where a form uses `[`, `]` or `|` as actual Beguile tokens (e.g., array sizes, union types), the sentence under the block says so.

A placeholder of more than one word is joined with hyphens, as in `RETURN-TYPE`.

A placeholder which is only part of a name is written in mixed case, with the placeholder parts capitalized:  `splitDirectionKind` (§22.7.3) names one method for every combination of the listed values (e.g., `splitUpGrid`); the sentence under the block lists the values.

In running text and tables, we write a form the way you would say it:  `Type::operator op` stands for `TYPE::operator OP` (§4.15), and `obj.member` for `EXPR.MEMBER`.

### Examples

An example is the smallest fragment which shows the rule.  A `// ->` comment gives the resulting value or the text printed.  Program output appears in its own block.

A fragment of statements belongs inside a routine.  When an example also needs lines at file scope (an `#include`, a global, a class), a `// … in a routine:` line separates the two:  everything above it goes at file scope, and everything below it goes in a function body.

Inform 6 code appears only in the sections about what Beguile generates (Part II and §16), and in emitter bodies.

Examples use real keywords and real library names, with `Main` as the entry point.  An example which uses a name declared by the Inform 6 Standard Library (`location`, `player`, `light`, `selfobj`, …) assumes `#include <bindings/i6StandardLibrary>` (§23.2) and leaves the name undeclared.

### Notes

Material which can be skipped on a first reading (a consequence of the rule, a caution, a limit) is set apart as a note, right where it applies:

> ***Note:** The note's text.*

The notes are also where I speak in the first person; the rest of the text sticks to "we."

### Target Markers

Behavior which depends on the virtual machine is marked `[Z-machine]`, `[Glulx]` or `[Z-machine/Glulx difference]`, either inline or in the form of a note:

> ***[Glulx]:** Applies only when the target is Glulx.*

Unmarked text applies to both targets.

### Cross-References

`§N.M` and `§N.M.K` refer to a section; inside a chapter they refer to that chapter's own sections.  A bare `§N` means the whole chapter.  `Appendix X` refers to an appendix.

### Entry Shape

Every language construct (keyword, directive, operator, declaration form, library method) is documented as an entry, with its parts in this order:

- **Syntax** gives the form, in the notation above.
- The rule itself follows, unlabeled, including any shorter form which is equivalent to it.
- **Example** shows it in use.
- **See also** points to sections which are not neighbors of the entry.

Within an entry, a paragraph which covers one alternative or aspect of the rule opens with a bold label ending in a period (e.g., **Placement.** or **Capture.**).  It is a label, not a heading.

Notes and target markers sit inline, where they apply.  A part with nothing to say is left out.  Chapters 17, 18, 19, 20, and 23 are narrative and don't use this shape; the extension entries of §22 use their own, which §22.1 lays out.
