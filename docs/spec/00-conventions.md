# Conventions

<!-- toc -->
- [Typography](#typography)
- [Syntax Notation](#syntax-notation)
- [Examples](#examples)
- [Target Markers](#target-markers)
- [Shorthand](#shorthand)
- [Cross-References](#cross-references)
- [Terminology](#terminology)
- [Entry Shape](#entry-shape)
<!-- /toc -->


## Typography

Identifiers, keywords, operators, file names and any other literal source text appear in `code font`.
Keywords are written lowercase; Beguile is case-insensitive (§1.3), so `Print` and `print` are the
same word. A term is set in **bold** where it is defined. Inform 6 is abbreviated I6 throughout.

Within an entry, a paragraph that treats one alternative or one aspect of the rule opens with a bold
run-in head ending in a period (`**Placement.** …`, `**Capture.** …`); the run-in is a label, not a
heading, and the paragraphs under one entry may be read in any order.

## Syntax Notation

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

A placeholder may also stand for part of a name. `split⟨direction⟩⟨kind⟩` (§20.7.3) names one method
for every combination of the listed values, spelled as a single identifier (`splitUpGrid`); the
sentence under the block lists the values. In running text and in tables, a form is abbreviated with
capitalized words in place of angle quotes: `Type::operator op` stands for `⟨type⟩::operator ⟨op⟩`
(§4.15), and `obj.member` for `⟨expr⟩.⟨member⟩`.

## Examples

An example is the smallest fragment that shows the rule. A `// →` comment gives the resulting value or
the text printed. Program output appears in blocks tagged `text`. Inform 6 appears in blocks tagged
`i6`, and only in sections about emission (Part II and §13); emitter bodies, which are raw I6 by
definition, are exempt. Examples use real keywords and real library names, and `Main` as the entry
point, so that they compile in principle. An example that uses a name declared by the Inform 6
Standard Library (`location`, `player`, `light`, `selfobj`, …) assumes `#include <i6StandardLibrary>`
(§21.2) and leaves the name undeclared.

## Target Markers

Behavior that depends on the virtual machine is marked `[Z-machine]`, `[Glulx]` or
`[Z-machine/Glulx difference]`, either inline or as a callout:

> **[Glulx]** Applies only when the target is Glulx.

Unmarked text applies to both targets.

## Shorthand

Beguile aims for one consistent set of rules: declarations live in member bodies, statements end in
`;`, types precede names, and so on. The canonical form of every construct follows those rules.
Because strict adherence can require needless boilerplate, the language provides a small number of
shorthand forms, the **pretty lies**, that desugar to the canonical form at parse time. The author sees
a compact surface; the canonical form remains the underlying truth and is available whenever the
shorthand does not fit.

Every shorthand is marked where its construct is introduced, and the callout states the canonical
form:

> **Shorthand.** *Surface form* is equivalent to *canonical form*.

## Cross-References

`§N.M` and `§N.M.K` refer to a section; inside a chapter they refer to that chapter's own sections. A
bare chapter number, `§N`, is used only when the whole chapter is meant. `Appendix X` refers to an
appendix.

## Terminology

The terms of art used in this specification (program, source file, target, directive, emitter,
island, binding, pooled class, veneer class, owned member, …) are defined once, in Appendix H.

## Entry Shape

Every language construct (keyword, directive, operator, declaration form, library method) is
documented as an entry with up to five labeled parts, in this order: **Syntax** (the form, in the
notation above), **Description** (the normative rule), **Example**, **Notes** (target markers, the
Shorthand callout, limits) and **See also** (references to sections that are not neighbors of the
entry). A part that is genuinely empty is omitted. Chapters 14, 16, 17, 18 and 21 are narrative and
do not use the shape; the extension entries of §20 use their own shape, announced in §20.1.
