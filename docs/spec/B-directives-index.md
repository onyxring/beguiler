# Appendix B Directive Index

Directive names are case-insensitive. Each entry points to its specification in §14; the semantics
of islands and `extern`-related behavior are in §15.

| Directive | Purpose | Entry |
|---|---|---|
| `#beguilerSettings { … }` | Configure the compiler and the I6 invocation | §14.7.1 (§17.1) |
| `#beguilerSettings.prop` | Read a setting as a compile-time literal | §14.7.2 (§17.7) |
| `#bgl` | Beguile island in raw I6 (in-routine or file-scope) | §14.5.2 |
| `#bglDecl` | File-scope Beguile island, declarations only | §14.5.2 |
| `#bglStmt` | File-scope Beguile island, statements only | §14.5.2 |
| `#declare` | Define an order-independent, immutable symbol | §14.2.3 |
| `#define` | Define a compilation symbol (linear) | §14.2.1 |
| `#elif` | Alternative condition in an `#if` block | §14.2.5 |
| `#else` | Fallback branch of an `#if` block | §14.2.5 |
| `#emitfirst` | Raw I6 at the beginning of the generated program | §14.4.2 |
| `#emitlast` | Raw I6 at the end of the generated program | §14.4.3 |
| `#endif` | Close an `#if` block | §14.2.5 |
| `#error` | Halt compilation with a message | §14.3.3 |
| `#exit` | Stop processing the current file | §14.3.4 |
| `#i6` | I6 island in Beguile source (single-line or block) | §14.5.1 (§15.2) |
| `#if` | Conditional compilation | §14.2.5 |
| `#include <name>` | Include a BLR library file | §14.1.1 |
| `#include "path"` | Include a Beguile file by path | §14.1.2 |
| `#include @"path"` | Include by raw-string path | §14.1.3 |
| `#include ?"path"`, `#include ?<name>` | Include, silently skipping a missing file | §14.1.4 |
| `#includeI6` | Include an I6 source file (`?` optional, `@` raw variants) | §14.1.5 |
| `#message` | Print a message during compilation | §14.3.1 |
| `#once` | Process this file at most once | §14.1.6 |
| `#redef` | Redefine a symbol without error | §14.2.2 |
| `#startup` | Raw I6 run at program startup, inside `bglInit()` | §14.4.1 |
| `#storedEmitFirst` | Named raw-I6 block, emitted at the top only when triggered | §14.4.4 |
| `#storedEmitLast` | Named raw-I6 block, emitted at the end only when triggered | §14.4.4 |
| `#undef` | Remove a symbol | §14.2.2 |
| `#using` | Import a class's or object's members into file scope | §10.4 |
| `#warning` | Report a warning and continue | §14.3.2 |

The `##` prefix marks Beguile-level processing inside otherwise raw I6 text. These markers are not
directives and are not valid in ordinary Beguile source:

| Marker | Where | Purpose | Specified in |
|---|---|---|---|
| `##if expr` | emitter bodies | Include the following body text only if the expression is true; same expression syntax as `#if` | §7.4 |
| `##else` | emitter bodies | Alternate branch of `##if` | §7.4 |
| `##endif` | emitter bodies | Close a `##if` block | §7.4 |
| `##beguilerSettings.key` | `#emitfirst`, `#emitlast`, `#storedEmitFirst`, `#storedEmitLast` bodies | Substitute a setting's value as an I6 literal | §14.4.5 |
| `##Action` | raw I6 only | Not a Beguile marker: Inform 6's own action-constant syntax (`##Take`), valid wherever raw I6 is written. In Beguile source, compare `action` with the verb name instead | §13.3 |

A single-hash directive inside an emitter body (`#ifdef`, `#iftrue`, …) is not a Beguile directive:
it is passed through verbatim and becomes an I6 compile-time conditional (§7.4).
