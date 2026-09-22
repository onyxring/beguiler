# Beguile Language Specification — authoring conventions

> Authoring notes for contributors. Not part of the specification; readers should start at [README.md](README.md).

This folder is the Beguile Language Specification. Every chapter and appendix follows the rules below;
`tools/build-toc.py --check` enforces the mechanical ones.

## Layout

```
docs/spec/
  README.md            front matter + generated table of contents for the whole spec
  00-preface.md        Preface
  00-conventions.md    Conventions used in this specification
  01-lexical.md … 15-interop.md          Part I   The Beguile Language
  16-invoking.md … 20-outputs-debugging.md   Part II  The Beguiler Compiler
  21-runtime-core.md … 23-bindings.md        Part III The Beguile Language Runtime
  A-keywords.md … J-limits.md            Appendices
  tools/build-toc.py   regenerates every TOC and validates cross-references
```

## Reading order is a contract

A chapter may rely only on chapters **before** it. If a construct genuinely needs a later concept, give
the reader the one-sentence version here and a forward cross-reference; never explain the later concept
in place.

## Headings

- Every heading is numbered: `# N Title`, `## N.M Title`, `### N.M.K Title`. Appendices use letters.
- Maximum depth is three levels. Nothing below `###`. If an entry needs sub-parts, use a bold run-in
  (`**Placement.** …`) for enumerated alternatives, or a table.
- No unnumbered headings anywhere in a chapter body. `build-toc.py` rejects them.
- One `#` per file. The first line of a chapter file is its `#` heading; `build-toc.py` inserts the
  chapter TOC directly under it between `<!-- toc -->` / `<!-- /toc -->` markers.

## Entry template

Every language construct (keyword, directive, operator, declaration form, library method) is documented
with these labelled parts, each label bold on its own line, in this order. Omit a part only when it is
genuinely empty. Chapter 14 is the reference example of the shape.

1. **Syntax** — a fenced block tagged ```syntax with the form(s), in the notation below.
2. **Description** — normative prose: what it means, what is permitted, what is an error.
3. **Example** — the smallest fragment (```bgl) that shows the rule. `// →` comments may show the
   resulting value or output; never the emitted I6 unless the section is *about* emission (Part II,
   Chapter 15). Emitter *bodies* are raw I6 by definition and are exempt.
4. **Notes** — `[Z-machine]`, `[Glulx]`, `[Z-machine/Glulx difference]` markers; the **Shorthand** callout
   (below); limits.
5. **See also** — `§` references to non-adjacent sections only (the chapter TOC already shows neighbours).

Narrative chapters — 16, 18, 19, 20 and 23 — are exempt from the template. Chapter 22's extension entries
use their own announced shape (Purpose · Include · Description · Methods · Example · Settings · Notes ·
See also).

## Syntax notation

Syntax blocks are fenced ```syntax. Placeholders are written in angle quotes `⟨name⟩`, lowercase
(`⟨type⟩ ⟨name⟩ = ⟨expr⟩ ;`). `[ … ]` marks an optional part, `…` repetition, `|` alternation. When a
literal `[`, `]`, `|` or `<` appears in a form, the sentence under the block says so.

## Callouts

- `> **Shorthand.** …` marks a "pretty lie": a surface form that desugars to a canonical form. State the
  canonical form. This is the only place the term *pretty lie* is used outside the Conventions chapter.
- `> **[Z-machine]** …` / `> **[Glulx]** …` for target-specific behavior.
- No other blockquote callouts. No emoji.

## What does not belong in this specification

Not admitted:

- **Implementation narration**: how the compiler/lexer/pre-scan does something, what I6 it emits, mangled
  names, opcode names, "the compiler hoists…", "lowers to…", complexity notes. Part II and Chapter 15 are
  the only homes for emission facts, and only where the fact is observable by the author.
- **Rationale and history**: "why we chose…", "earlier versions…", "**Changed.**", "this matches
  orLibrary…", "the alternative would be…". Beguile is pre-release: the spec describes what *is*.
- **Verbatim diagnostics** as prose. Say "is a compile-time error". Chapter 19 owns diagnostic text.
- **Tutorials and worked walkthroughs**: "Typical use", narrative examples, complete game listings,
  porting advice. Those belong in *Beguile for the I6 Developer* or *Verbs-Grammar.md*.
- **Duplicates**: each rule is stated exactly once, in the chapter that owns it; everywhere else is a
  one-line pointer.
- **Roadmap**: "may be extended in the future", "not yet supported" → state the current rule as a rule
  ("Owned members are not permitted on identifier-sized pools.").

## Cross-references

- Always to a section: `§N.M` or `§N.M.K`. A bare chapter number `§N` is allowed only when the whole
  chapter is meant ("emitters, §7"). While drafting, `§N *Heading Title*` may be written and
  `build-toc.py --resolve` turns it into the number.
- To an appendix: `Appendix C`.
- Never reference by line number.

## Code

- Fenced blocks are tagged ```syntax for syntax forms, ```bgl for Beguile, ```i6 for Inform 6, ```text for terminal output.
- Spelling is American (`initialize`, `color`, `behavior`). Headings are Title Case.
- Examples compile in principle: real keywords, real library names. Prefer `Main` for the entry point.
- Identifiers in prose use backticks; keywords are always lowercase.

## Terminology (use these words, not synonyms)

program · source file · story file · target (Z-machine / Glulx) · directive · statement · expression ·
declaration · routine (I6) vs function (Beguile) · member · property (object) vs field (class) is **not**
a distinction Beguile makes: use *member* · emitter · emitter body · substitution token · island ·
default mode / precompiler mode · binding · BLR (Beguile Language Runtime) · extension (an opt-in
`#include <…>` file) · pretty lie / shorthand · pooled class · veneer class · owned member.
