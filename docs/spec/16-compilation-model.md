# 16 Compilation Model

<!-- toc -->
- [16.1 Overview](#161-overview)
- [16.2 Phases](#162-phases)
- [16.3 Passes and Forward References](#163-passes-and-forward-references)
- [16.4 Include Resolution](#164-include-resolution)
- [16.5 Path Resolution](#165-path-resolution)
- [16.6 Layout of the Generated File](#166-layout-of-the-generated-file)
- [16.7 Emission Ordering](#167-emission-ordering)
- [16.8 `bglInit()`](#168-bglinit)
- [16.9 Pay-Only-If-Used Emission](#169-pay-only-if-used-emission)
- [16.10 The Frame Pool](#1610-the-frame-pool)
- [16.11 Dead Code and Economy](#1611-dead-code-and-economy)
<!-- /toc -->

## 16.1 Overview

Beguile is a transpiler: a program is translated to a single Inform 6 source file, which the Inform 6
compiler then turns into a story file. This chapter describes the pipeline as an author can observe
it: which passes run, what may be referenced before it is declared, how files are found, in what order
the generated file is laid out, and which parts of the output are present only when used.

The same pipeline serves both entry modes (§13.1). In precompiler mode the Beguile passes operate on
the embedded `#bgl` islands and the surrounding I6 passes through; an `.inf` file with no islands at
all is copied through unchanged (§13.1.2).

## 16.2 Phases

A build runs these stages in order. Stages 1 and 8 run only when blorb packaging is enabled
(§15.6); stages 6–8 run only when the Inform 6 hand-off is not disabled (§14.3, `-inform=none`).

1. **Asset scan.** The asset directory is scanned and `_blorbAssets.bgl` is (re)written next to the
   source file.
2. **Pre-scan (pass 1).** The source and everything it includes are read once to register the names
   of every global declaration (§16.3).
3. **Parse (pass 2).** The source is lexed and parsed in full; types are resolved and checked.
4. **Whole-program checks.** Checks that need the complete program, such as duplicate member
   detection across `extend` blocks and the hidden-member rules (§8.9.4), run here; unset settings take
   their defaults.
5. **Emission.** The transpiled file `<source>.transpiled.inf` is written to the output directory
   (§18.2); in a debug build the debug bundle is written beside it.
6. **Abbreviation pass** (only with `economy = true`, §16.11).
7. **Inform 6.** The Inform 6 compiler is run on the transpiled file; its diagnostics are rewritten
   to Beguile locations (§17.1.1).
8. **Blorb assembly.** The story file and the assets are packaged into a `.gblorb` or `.zblorb`.

The first compile-time error ends the build.

## 16.3 Passes and Forward References

Declarations at global scope may appear in any order. The pre-scan registers a stub for every global
name before the main parse begins, so a name may be used before the file position at which it is
declared, and an included file may refer to declarations in a file included after it. The pre-scan
registers:

- classes (including `extern` and generic ones) and their members;
- enums, bnums and unions, with their members;
- objects, verbs and global variables, with the members declared in an object body;
- global functions;
- members added by `extend` bodies, whether the target is declared before or after the `extend`;
- `#declare` symbols, which are consequently visible to every `#if` in the program regardless of
  position (§12.2.3).

Conditional directives (`#if`, `#elif`, `#else`, `#endif`) and `#define`/`#undef` are honored
during **both** passes, so a declaration excluded by `#if` is not registered either. `#define` is
positional: it affects only conditionals parsed after it (§12.2.1).

Some things are not order-independent, because their value is fixed at the point of parsing:

- `#beguilerSettings.prop` reads the value fixed so far (§15.7).
- The emission-ordering rule for `extern attribute` (§16.7, rule 3) depends on source order.
- A `#using` naming a class or object declared later is accepted but ignored, with a warning (§17.3).

## 16.4 Include Resolution

`#include`, `#includeI6` and `#beguilerSettings` are specified in §12.1 and §12.7.1; this section
states how the file they name is found.

**Library includes, `#include <name>`.** The `beguiLib` tree (§14.5) is
walked recursively, files in each directory before its subdirectories, subdirectories in alphabetical
order; the first file whose path ends with the requested name is taken.

**Relative includes, `#include "path"` and `#includeI6 "name"`.** The directory of the including file
is searched first, then each `includePaths` entry in order (command-line entries before block
entries). A `#includeI6` that resolves is emitted with its full absolute path, so Inform 6 never
searches for it; the raw form `#includeI6 @"…"` bypasses resolution and is emitted verbatim.

**Case-insensitive matching.** Every component of an include path, directory or file, matches
case-insensitively; an exact-case match is preferred, and if two entries in one directory differ only
in case, which one is chosen is unspecified.

**Nesting.** Includes may nest to a depth of 255; exceeding it is a compile-time error. A file
guarded by `#once` is processed at most once however often it is included.

## 16.5 Path Resolution

Every path written in Beguile source — include paths and the path-valued settings — is normalized
when parsed:

- **Separator rewriting.** Each `/` and `\` becomes the platform separator, so a path written with
  either separator works on any platform. `rewritePaths = false` disables this for the program.
- **Relative anchoring.** `includePaths` and `blorbAssetPath` are relative to the directory of the
  file that sets them; `outputPath` is relative to the entry source file's directory
  (§18.1).

## 16.6 Layout of the Generated File

The transpiled `.inf` file is laid out in this order, regardless of the order of the source. Items
marked *(when used)* are absent from programs that do not need them.

1. The ICL header: `!% -G` / `!% -v5` / `!% -v8`, `!% -E<n>`, `!% $OMIT_UNUSED_ROUTINES=1` and one
   `!% ++include_path=` line per `includePaths` entry. In precompiler mode the entry file's own `!%`
   lines appear here instead.
2. The `beguiler` version constant, then `Serial` and `Release` when set, the runtime size constants
   *(when used)*, and the `UUID_ARRAY` IFID string *(when set)*.
3. Compiler scratch globals *(when used)*.
4. The frame pool *(when used, §16.10)* and Z-machine excess-parameter globals *(when used)*.
5. Every `#emitfirst` block, in file-inclusion order, each file's block at most once; then the fired
   `#storedEmitFirst` blocks *(when triggered, §16.9)*.
6. The `bglInit` routine (§16.8).
7. The program's declarations in source order, subject to the ordering rules of §16.7: classes,
   objects, globals, arrays, functions, verbs and grammar, and the contents of `#i6` islands.
8. Static class routines, then any grammar evictions.
9. Every `#emitlast` block, in file-inclusion order; then the fired `#storedEmitLast` blocks
   *(when triggered)*.
10. Referenced `superposed` declarations, appended in the order they were observed (§16.9).
11. In precompiler mode, the entry file's trailing `end;` and anything after it.

## 16.7 Emission Ordering

Inform 6 requires a class to be defined before any instance of it or class derived from it, and an
attribute to be declared before any `has` clause that names it. The emitter keeps source order except
where these three rules require otherwise:

1. **Lazy class emission.** A class is normally emitted at its source position. If an earlier
   declaration is an instance of it (`ClassName var;`, or an object of that class), the class is
   emitted immediately before that first instance instead.
2. **Base before derived.** Before a class is emitted, at its own position or by rule 1, its base
   classes are emitted first, transitively.
3. **`extern attribute` as a gate.** A class whose attribute list names an `extern attribute` cannot
   be emitted before the `extern attribute` declaration has been passed in source order. That
   declaration stands in for the `#includeI6` that defines the attribute in I6, so a bindings file
   must be included before any class, or first instance of a class, that uses one of its attributes.
   Violating this is a compile-time error (§17.2).

Circular inheritance (`class A : B` where `B` derives, directly or transitively, from `A`) is a
compile-time error detected when the inheritance clause is parsed, independent of emission.

Member types, method bodies and value references impose no ordering constraint: a class may declare a
member of a class declared later, and a routine may call a routine or name an object declared later.

## 16.8 `bglInit()`

Every program contains a routine `bglInit`, emitted at position 6 of the generated file (§16.6),
before the program's own declarations. This section states only its place in the output; what it
does, in what order, and who calls it are specified in §19.2.

In precompiler mode the routine is declared but is never called unless the `.inf` file arranges it
(§13.1.2).

## 16.9 Pay-Only-If-Used Emission

Three mechanisms let the library declare code and data that cost nothing in a program that does not
use them. The `superposed` qualifier itself is specified in §3.2 and §3.13; this section states what
the emitter does with it.

**`superposed` declarations.** A `superposed` routine, global, array, object or class is withheld
from the transpiled file. After everything else has been emitted, the output is searched for the
declaration's name as a whole word, case-insensitively; if it occurs, the declaration is **appended at
the end** of the file, and the search repeats until nothing more is pulled in. A `superposed` class
that is used in a declaration position — a static instance or a subclass — is instead emitted in place
ahead of that use, with its `superposed` bases first, because an I6 `Class` must precede its
instances and subclasses. A `superposed` declaration nothing references does not appear in the
transpiled file at all, and is likewise absent from the debug bundle (§18.3).

Because the test is textual, a name that also occurs in a string literal or comment materializes its
declaration (a harmless dead entry); a genuinely referenced declaration is never dropped.

**Stored emit blocks.** `#storedEmitFirst name` and `#storedEmitLast name` (§12.4.4) register a raw-I6
block that is emitted, at the position of the corresponding unstored form, only when a built-in
template that needs it is applied. Each block is emitted at most once.

**Built-in templates.** A few constructs have no fixed I6 form until the compiler knows how the
program uses them: the frame pool is sized to the program, and the several `for … in` forms each expand
to a different loop. The compiler holds these as built-in I6 templates, which are part of the system
library but are not Beguile source and are never `#include`d; authors do not write or edit them. A
template names the stored blocks it needs, so that a helper such as the literal-list `for … in`
scratch buffer is emitted only in programs that use the construct.

## 16.10 The Frame Pool

The Z-machine gives a routine 15 local slots for parameters and locals together; one slot is
reserved for the frame pointer, so a routine needing more than 14 has its excess locals spilled into
a global **frame pool**. The mechanism is recursion-safe and invisible to the author. The pool is also the backing store for local arrays
(§10.6) on both targets. It is emitted only when some routine needs it, and its size in words is
`framePoolSize` (§15.4). A program that exhausts it halts with a runtime error (§17.4).

> **[Glulx]** Glulx has no practical local-variable limit; spilling never occurs, and the pool is
> emitted only for local arrays.

> **[Z-machine]** A routine call passes at most five arguments natively; parameters beyond the fifth
> travel through compiler-emitted globals, which appear in the header when any routine declares more
> than five.

## 16.11 Dead Code and Economy

Three independent controls reduce story-file size.

**`superposed`** (§16.9) withholds a declaration from the transpiled file unless it is referenced.

**`omitUnusedRoutines`** (default `true`) emits `!% $OMIT_UNUSED_ROUTINES=1`, asking Inform 6 to
drop every routine that is emitted but never referenced. Inform 6 sees the whole compilation,
including `#i6` islands and included I6 libraries, and keeps any routine whose address is taken, so
the setting is safe; set it `false` for a debug build in which every routine should be present.

**`economy`** (default `false`) enables automatic text abbreviation. After the transpiled file is
written, the compiler runs Inform 6 once with `-u` to compute the optimal `Abbreviate` set, inserts
those directives at the top of the transpiled file, and compiles with `-e`. If the program already
contains any `Abbreviate` directive of its own, the pass is skipped and a console note says so. The
pass is best-effort: if the pre-pass fails, the build proceeds without abbreviations.
