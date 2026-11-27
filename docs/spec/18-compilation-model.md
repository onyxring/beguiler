# 18 Compilation Model

<!-- toc -->
- [18.1 Overview](#181-overview)
- [18.2 Phases](#182-phases)
- [18.3 Passes and Forward References](#183-passes-and-forward-references)
- [18.4 Include Resolution](#184-include-resolution)
- [18.5 Path Resolution](#185-path-resolution)
- [18.6 Layout of the Generated File](#186-layout-of-the-generated-file)
- [18.7 Emission Ordering](#187-emission-ordering)
- [18.8 `bglInit()`](#188-bglinit)
- [18.9 Pay-Only-If-Used Emission](#189-pay-only-if-used-emission)
- [18.10 The Frame Pool](#1810-the-frame-pool)
- [18.11 Dead Code and Economy](#1811-dead-code-and-economy)
- [18.12 Z-machine Capacity](#1812-z-machine-capacity)
<!-- /toc -->

## 18.1 Overview

Beguile is a transpiler:  a program is translated to a single Inform 6 source file, which the Inform 6 compiler then turns into a story file.  In this chapter we go over the pipeline as you can observe it.

The same pipeline serves both entry modes (§16.1).  In precompiler mode the Beguile passes operate on the embedded `#bgl` islands and the surrounding I6 passes through (§16.1.2).

## 18.2 Phases

A build runs the following stages in order.  Stages 1 and 8 run only when blorb packaging is enabled (§15.6); stages 6 to 8 run only when the Inform 6 hand-off is not disabled (§17.3, `-inform=none`).

1. **Asset scan.**  The asset directory is scanned and `_blorbAssets.bgl` is (re)written next to the source file.
2. **Pre-scan (pass 1).**  The source and everything it includes are read once to register the names of every global declaration (§18.3).
3. **Parse (pass 2).**  The source is lexed and parsed in full; types are resolved and checked.
4. **Whole-program checks.**  Checks that need the complete program, such as duplicate member detection across `extend` blocks and the hidden-member rules (§8.7.4), run here; unset settings take their defaults.
5. **Emission.**  The transpiled file `SOURCE.transpiled.inf` is written to the output directory (§20.2); in a debug build the debug bundle is written beside it.
6. **Abbreviation pass** (only with `economy = true`, §18.11).
7. **Inform 6.**  The Inform 6 compiler is run on the transpiled file; its diagnostics are rewritten to Beguile locations (§19.1.1).
8. **Blorb assembly.**  The story file and the assets are packaged into a `.gblorb` or `.zblorb`.

The first compile-time error ends the build.

## 18.3 Passes and Forward References

Declarations at global scope can appear in any order.  The pre-scan registers a stub for every global name before the main parse begins, so you can use a name before it is declared, and an included file can refer to declarations in a file included after it.  It registers:

- classes (including `extern`, `alias`, and generic ones) and structs, with their members;
- enums, bnums, and unions, with their members;
- objects, verbs, global variables, and constants, with the members declared in an object body;
- global functions;
- members added by `extend` bodies of objects, whether the object is declared before or after the `extend` (a class or an array must be declared before it is extended);
- `#declare` symbols (§14.2.3).

Conditional directives (`#if`, `#elif`, `#else`, `#endif`) and `#define`/`#undef` are honored during **both** passes, so a declaration excluded by `#if` is not registered either.

Some things are not order-independent, since their value is fixed at the point of parsing:

- `#beguilerSettings.prop` reads the value fixed so far (§15.7).
- The emission-ordering rule for `extern attribute` (§18.7, rule 3) depends on source order.
- A `#using` naming a class or object declared later is accepted but ignored, with a warning (§19.3).

## 18.4 Include Resolution

**Library includes, `#include <name>`.**  The `beguiLib` tree (§17.5) is searched as §14.1.1 describes.

**Relative includes, `#include "path"` and `#includeI6 "name"`.**  The search is described in §14.1.2; its `includePaths` entries are tried in order, command-line entries before block entries.  A `#includeI6` that resolves is emitted with its full absolute path, so Inform 6 never searches for it; the raw form `#includeI6 @"…"` bypasses resolution and is emitted verbatim.  A file that a patch changes is read from its patched copy (§14.1.6).

**Current-file-relative I6 includes.**  An `Include ">name"` in the transpiled file still resolves against the source directory, so your `#i6 { Include ">lib.h"; }` will behave just as it would in a hand-written `.inf` beside the source.

**Case-insensitive matching.**  Every component of an include path, directory or file, matches case-insensitively.  An exact-case match is preferred; if two entries in one directory differ only in case, which one is chosen is unspecified.

**Nesting.**  Include nesting is limited to 255 levels.  Exceeding the limit (e.g., through circular includes of files without `#once`, §14.1.7) is a compile-time error.

## 18.5 Path Resolution

Every path written in Beguile source (include paths and the path-valued settings) is normalized when parsed:

- **Separator rewriting.**  Each `/` and `\` becomes the platform separator, so a path written with either separator will work on any platform.  `rewritePaths = false` disables this for the program.
- **Relative anchoring.**  `includePaths` and `blorbAssetPath` are relative to the directory of the file which sets them; `outputPath` is relative to the entry source file's directory (§20.1).

## 18.6 Layout of the Generated File

The transpiled `.inf` file is laid out in the following order, regardless of the order of the source.  Items marked *(when used)* are absent from programs that don't need them.

1. The ICL header:  `!% -G` / `!% -v5` / `!% -v8`, `!% -EN`, `!% $OMIT_UNUSED_ROUTINES=1`, and one `!% ++include_path=` line per `includePaths` entry.  In precompiler mode the entry file's own `!%` lines appear here instead.
2. The `beguiler` version constant, then `Serial` and `Release` when set, the runtime size constants *(when used)*, and the `UUID_ARRAY` IFID string *(when set)*.
3. Compiler scratch globals *(when used)*.
4. The frame pool *(when used, §18.10)* and Z-machine excess-parameter globals *(when used, §6.3)*.
5. Every `#emitfirst` block, in file-inclusion order, each file's block at most once; then the fired `#storedEmitFirst` blocks *(when triggered, §18.9)*.
6. The `bglInit` routine (§18.8).
7. The program's declarations in source order, subject to the ordering rules of §18.7:  classes, objects, globals, arrays, functions, verbs and grammar, and the contents of `#i6` islands.
8. Static class routines, then any grammar evictions.
9. Every `#emitlast` block, in file-inclusion order; then the fired `#storedEmitLast` blocks *(when triggered)*.
10. Referenced `superposed` declarations, appended in the order they were observed (§18.9).
11. In precompiler mode, the entry file's trailing `end;` and anything after it.  It comes after the `superposed` declarations since Inform 6 stops reading at `end;`.

## 18.7 Emission Ordering

Inform 6 requires a class to be defined before any instance of it or class derived from it, and an attribute to be declared before any `has` clause that names it.  The emitter keeps source order, except where the following three rules require otherwise:

1. **Lazy class emission.**  A class is normally emitted at its source position.  If an earlier declaration is an instance of it (`ClassName var;`, or an object of that class), the class is emitted immediately before that first instance instead.
2. **Base before derived.**  Before a class is emitted, at its own position or by rule 1, its base classes are emitted first, transitively.
3. **`extern attribute` as a gate.**  A class whose attribute list names an `extern attribute` can't be emitted before the `extern attribute` declaration has been passed in source order.  That declaration stands in for the `#includeI6` which defines the attribute in I6, so a bindings file must be included before any class, or first instance of a class, that uses one of its attributes.  Violating this is a compile-time error (§19.2).

Member types, method bodies, and value references impose no ordering constraint:  a class can declare a member of a class declared later, and a routine can call a routine or name an object declared later.

## 18.8 `bglInit()`

Every program contains a routine `bglInit`, emitted at position 6 of the generated file (§18.6), before the program's own declarations.  What it does, in what order, and who calls it are covered in §21.2.

## 18.9 Pay-Only-If-Used Emission

Three mechanisms let the library declare code and data which cost nothing in a program which doesn't use them.  The `superposed` qualifier itself is covered in §3.12.

**`superposed` declarations.**  A `superposed` routine, global, array, object, or class is withheld from the transpiled file.  After everything else has been emitted, the output is searched for the declaration's name as a whole word, case-insensitively; if it occurs, the declaration is **appended at the end** of the file, and the search repeats until nothing more is pulled in.  A `superposed` class which is used in a declaration position (a static instance or a subclass) is instead emitted in place ahead of that use, with its `superposed` bases first, since an I6 `Class` must precede its instances and subclasses.  A `superposed` declaration that nothing references appears neither in the transpiled file nor in the debug bundle (§20.3).

Since the test is textual, a name which also occurs in a string literal or comment will materialize its declaration (a harmless dead entry); a genuinely referenced declaration is never dropped.

**Stored emit blocks.**  `#storedEmitFirst name` and `#storedEmitLast name` (§14.4.4) register a raw-I6 block which is emitted, at the position of the corresponding unstored form, only when a built-in template that needs it is applied.  Each block is emitted at most once.

**Built-in templates.**  A few constructs have no fixed I6 form until the compiler knows how the program uses them:  the frame pool is sized to the program, and the several `for … in` forms each expand to a different loop.  The compiler holds these as built-in I6 templates, which are part of the system library but are not Beguile source and are never `#include`d; you don't write or edit them.  A template names the stored blocks it needs, so that a helper is emitted only in programs that use the construct.

## 18.10 The Frame Pool

The Z-machine gives a routine 15 local slots for parameters and locals together; one slot is reserved for the frame pointer, so a routine needing more than 14 has its excess locals spilled into a global **frame pool**.  The mechanism is recursion-safe and invisible to you.

The pool is also the backing store for local `array<char>` buffers on both targets, and it records each routine's heap arrays (§12.6).  It holds the saved fields of a routine's struct locals and parameters while that routine is called again before it returns, so each call of a recursive routine takes words from it.

The pool is emitted only when some routine needs it.  Its size in words is `framePoolSize` (§15.4) when the program sets it; otherwise, the compiler sizes it from the program, as the largest routine frame times a nominal depth of 4, and never below 64.  When `framePoolSize` is set, a routine which calls itself and holds a local array will draw a warning if four of its calls wouldn't fit.  A program that exhausts the pool halts with a runtime error (§19.4).

> ***[Glulx]:** Glulx has no practical local-variable limit; spilling never occurs, and the pool is emitted only for local arrays.*

## 18.11 Dead Code and Economy

Three independent controls reduce the size of the story file.

**`superposed`** (§18.9) withholds a declaration from the transpiled file unless it is referenced.

**`omitUnusedRoutines`** (default `true`) emits `!% $OMIT_UNUSED_ROUTINES=1`, asking Inform 6 to drop every routine which is emitted but never referenced; a routine whose address is taken is kept.  Set it to `false` for a debug build in which every routine should be present.

**`economy`** (default `false`) enables automatic text abbreviation.  After the transpiled file is written, the compiler runs Inform 6 once with `-u` to compute the optimal `Abbreviate` set, inserts those directives at the top of the transpiled file, and compiles with `-e`.  If the program already contains any `Abbreviate` directive of its own, the pass is skipped and a console note says so.  The pass is best-effort:  if the pre-pass fails, the build proceeds without abbreviations.

## 18.12 Z-machine Capacity

A Z-machine story shares fixed tables between the program, the runtime library, and the IF library:

| Resource | Z-machine capacity | What uses it |
| --- | --- | --- |
| Global variables | 233 | Library globals, the runtime's, a program's globals, and compiler temporaries such as lambda captures |
| Attributes | 48 | Library attributes and a program's `attribute` declarations |
| Common properties | 61 | Library properties and a program's `property` declarations |
| Readable memory | 64K | Objects (every slot of a pooled class copies all its properties), arrays, the `<string>` pool's buffers |
| Entries in one array | 32,767 | One `array<T>`, its capacity (the length and marker are a separate two-word array before it) |

The compiler itself reports an array declared larger than the limit.  The others depend on what the library declares, so Inform 6 finds them; the compiler then reports which capacity ran out, in its own words instead of Inform 6's.  Glulx has none of these limits.

