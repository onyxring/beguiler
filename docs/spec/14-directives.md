# 14 Directives

<!-- toc -->
- [14.1 Source Organization](#141-source-organization)
  - [14.1.1 `#include <name>`](#1411-include-name)
  - [14.1.2 `#include "path"`](#1412-include-path)
  - [14.1.3 `#include @"path"`](#1413-include-path)
  - [14.1.4 `#include ?"path"` and `#include ?<name>`](#1414-include-path-and-include-name)
  - [14.1.5 `#includeI6`](#1415-includei6)
  - [14.1.6 `#once`](#1416-once)
- [14.2 Symbols and Conditional Compilation](#142-symbols-and-conditional-compilation)
  - [14.2.1 `#define`](#1421-define)
  - [14.2.2 `#redef` and `#undef`](#1422-redef-and-undef)
  - [14.2.3 `#declare`](#1423-declare)
  - [14.2.4 Pre-defined Symbols](#1424-pre-defined-symbols)
  - [14.2.5 `#if`, `#elif`, `#else`, `#endif`](#1425-if-elif-else-endif)
- [14.3 Diagnostics and Control](#143-diagnostics-and-control)
  - [14.3.1 `#message`](#1431-message)
  - [14.3.2 `#warning`](#1432-warning)
  - [14.3.3 `#error`](#1433-error)
  - [14.3.4 `#exit`](#1434-exit)
- [14.4 Raw I6 Placement](#144-raw-i6-placement)
  - [14.4.1 `#startup`](#1441-startup)
  - [14.4.2 `#emitfirst`](#1442-emitfirst)
  - [14.4.3 `#emitlast`](#1443-emitlast)
  - [14.4.4 `#storedEmitFirst` and `#storedEmitLast`](#1444-storedemitfirst-and-storedemitlast)
  - [14.4.5 `##beguilerSettings.<key>` Substitution](#1445-beguilersettingskey-substitution)
- [14.5 Islands](#145-islands)
  - [14.5.1 `#i6`](#1451-i6)
  - [14.5.2 `#bgl`, `#bglDecl`, `#bglStmt`](#1452-bgl-bgldecl-bglstmt)
- [14.6 Namespace Import](#146-namespace-import)
  - [14.6.1 `#using`](#1461-using)
- [14.7 Settings](#147-settings)
  - [14.7.1 `#beguilerSettings { … }`](#1471-beguilersettings-)
  - [14.7.2 `#beguilerSettings.prop`](#1472-beguilersettingsprop)
<!-- /toc -->


A directive is a compile-time instruction introduced by `#`. Directive names are case-insensitive, like
every Beguile identifier (§1.3). Unless an entry says otherwise, a directive is written at global scope
and is consumed by the compiler: nothing of it reaches the generated I6 except the raw I6 bodies of the
placement directives in §14.4 and the island directives in §14.5. An unrecognized directive is a
compile-time error.

The entries are grouped by purpose; Appendix B lists every directive alphabetically.

## 14.1 Source Organization

### 14.1.1 `#include <name>`

**Syntax**

```syntax
#include <⟨name⟩>
#include <⟨sub⟩/⟨name⟩>
#include <⟨a⟩/⟨b⟩/⟨name⟩>
```

The angle brackets are literal.

**Description**

Includes a Beguile source file from the Beguile Language Runtime library tree (`beguiLib`, located by
the `beguiLibPath` setting, §17.2). The tree is searched recursively for `name.bgl`; the `.bgl`
extension is supplied by the compiler and must not be written.

- `<name>` matches `name.bgl` anywhere in the tree, root or any subfolder.
- `<sub/name>` matches `name.bgl` only when its immediate parent folder is `sub`, at any depth.
- `<a/b/name>` requires the resolved path to end with `a/b/name.bgl`.

The search is deterministic: the root folder's files are checked first, then each subfolder in
alphabetical order, depth first, and the first match wins. Matching is case-insensitive
(`<String>` and `<string>` are the same include) regardless of the file system. A file that is not
found is a compile-time error.

A folder prefix is optional: `#include <⟨name⟩>` is equivalent to the
canonical `#include <⟨folder⟩/…/⟨name⟩>` naming the folder in which the search finds `name.bgl`,
so `<i6StandardLibrary>` and `<bindings/i6StandardLibrary>` are the same include.

**Example**

```bgl
#include <string>                  // beguiLib/string.bgl
#include <i6StandardLibrary>       // same as <bindings/i6StandardLibrary>
#include <bindings/punyInform>     // beguiLib/bindings/punyInform.bgl
```

**Notes**

The extensions that ship with the BLR and what each provides are listed in §22.

**See also** §18.5, §22.

### 14.1.2 `#include "path"`

**Syntax**

```syntax
#include "⟨path⟩"
```

**Description**

Includes a Beguile source file by path. The compiler searches the directory of the current source
file, then each `includePaths` directory (§17.3), trying *path*`.bgl` first and then *path* exactly as
written. Subdirectory paths are permitted and resolve relative to each directory searched. Either `/`
or `\` may be used as a separator. Matching is case-insensitive. A file that is not found is a
compile-time error.

A file may be included more than once; a file that must be processed only once guards itself with
`#once` (§14.1.6).

**Example**

```bgl
#include "myLibrary"
#include "utils/helpers"
```

**Notes**

Beguile has no equivalent of Inform 6's `>filename` prefix; the current file's directory is always
searched first.

**See also** §18.5.

### 14.1.3 `#include @"path"`

**Syntax**

```syntax
#include @"⟨path⟩"
#include ?@"⟨path⟩"
```

**Description**

As `#include "path"`, except that the path is a raw string (§1.6.4): no escape processing is applied,
so backslashes and escape-like sequences pass through verbatim. Path search and the file-not-found
error are those of §14.1.2. The optional and raw modifiers combine as `?@"path"`. An interpolated
string (`$"…"`) is not accepted as an include path.

**Example**

```bgl
#include @"vendor\legacy\helpers"
```

**See also** §1.6.4.

### 14.1.4 `#include ?"path"` and `#include ?<name>`

**Syntax**

```syntax
#include ?"⟨path⟩"
#include ?<⟨name⟩>
```

The angle brackets are literal.

**Description**

As the corresponding unmodified form, except that a file that is not found is silently skipped
instead of being reported as an error.

**Example**

```bgl
#include ?"optionalExtension"
#include ?<glulxImage>
```

### 14.1.5 `#includeI6`

**Syntax**

```syntax
#includeI6 "⟨name⟩"
#includeI6 ?"⟨name⟩"
#includeI6 @"⟨name⟩"
```

**Description**

Includes an Inform 6 source file in the generated program. The file is resolved like
`#include "path"` (§14.1.2): the current source file's directory, then each `includePaths` directory,
trying *name* as written and then *name*`.h`. Subdirectory paths are permitted. The I6 compiler
receives the resolved file. A file that is not found is a compile-time error.

`?"name"` silently skips a file that is not found. `@"name"` hands the string to the I6 compiler
verbatim: no search, no existence check and no separator rewriting are performed.

Every `includePaths` directory is also made available to the I6 compiler, so an included I6 file can
resolve its own internal includes.

**Example**

```bgl
#includeI6 "parser"
```

**Notes**

The I6 include is placed in the generated output at the directive's source position; a Beguile
declaration that depends on something the I6 file defines must follow it (§15.4, §18.7).

An `extern` declaration for a name the included file defines may precede or follow the include; the
exception is a class that uses an `extern attribute`, which must follow that attribute's declaration
(§18.7).

**See also** §15.5, §18.5.

### 14.1.6 `#once`

**Syntax**

```syntax
#once
```

**Description**

Placed at the top of a source file, marks the file so that any later `#include` of the same file (by
any path that resolves to the same location) is silently ignored. Without `#once` a file is
processed every time it is included.

Include nesting is limited to 255 levels; exceeding the limit, for example through circular includes
of files without `#once`, is a compile-time error.

**Example**

```bgl
#once
// rest of myLibrary.bgl …
```

**Notes**

Every file intended to be included as a library should begin with `#once`; the BLR extension files
all do.

**See also** §14.4 (per-file deduplication that does not depend on `#once`).

## 14.2 Symbols and Conditional Compilation

### 14.2.1 `#define`

**Syntax**

```syntax
#define ⟨name⟩
#define ⟨name⟩ ⟨value⟩
```

**Description**

Defines a compilation symbol. Without a value the symbol has the boolean value `true`. A symbol with
a value is also usable in a Beguile expression, where the name is replaced by its value at compile
time: a numeric value is an integer literal, any other value a string literal. Symbols are tested with
`#if` (§14.2.5).

`#define` is linear: it affects only the source that follows it, in the order files are processed. An
`#if` sees a symbol as defined only if a `#define` for it appears earlier and no intervening `#undef`
removed it. Defining a symbol that is already defined is a compile-time error; use `#redef`
(§14.2.2). Defining a symbol that was `#declare`d is a compile-time error (§14.2.3).

**Example**

```bgl
#define DEBUG          // DEBUG is true
#define MAX_SCORE 100

if(score >= MAX_SCORE) print("You win!");   // compiles as: if(score >= 100)
const int maxScore = MAX_SCORE;
```

### 14.2.2 `#redef` and `#undef`

**Syntax**

```syntax
#redef ⟨name⟩
#redef ⟨name⟩ ⟨value⟩
#undef ⟨name⟩
```

**Description**

`#redef` behaves exactly like `#define` except that an already-defined symbol is overwritten rather
than reported. `#undef` removes a symbol so that a later `#if` treats it as undefined. Both are
linear, like `#define`. Applying either to a `#declare`d symbol is a compile-time error.

**Example**

```bgl
#define LEVEL 1
#redef  LEVEL 2      // now 2
#undef  LEVEL        // now undefined
```

### 14.2.3 `#declare`

**Syntax**

```syntax
#declare ⟨name⟩
#declare ⟨name⟩ ⟨value⟩
```

**Description**

Defines a symbol like `#define`, with two differences:

- **Order-independent.** A declared symbol is visible to every `#if` in the program, including
  `#if`s in files processed before the file containing the `#declare`.
- **Immutable.** A declared symbol cannot be `#undef`d, `#redef`d or shadowed by a `#define`; each is
  a compile-time error, as is a second `#declare` of the same name with a different value.

In every other respect it matches `#define`: a bare `#declare NAME` is a boolean flag, a value is
usable inline as a literal, and the symbol is tested with the same `#if` expressions.

| | `#define` / `#redef` | `#declare` |
|---|---|---|
| Visibility | Linear: source after the directive | Global: every `#if`, before or after |
| Across `#include` | Only from an earlier-processed file | Any file, in any include order |
| Mutability | `#redef` overwrites, `#undef` removes | Immutable |
| Intended for | Configuration, constants, toggles | Capability flags a library advertises |

**Example**

```bgl
// A core file, processed first:
#if I6_STANDARD_LIBRARY
    // use something the standard-library binding provides
#else
    // self-contained fallback
#endif

// The binding, included later:
#declare I6_STANDARD_LIBRARY
```

**See also** §23.3.3.

### 14.2.4 Pre-defined Symbols

**Description**

The compiler defines these symbols before any source file is processed: `beguiler`,
`beguilerMajor`, `beguilerMinor`, `beguilerPatch` (the compiler version), and exactly one of
`TARGET_GLULX` or `TARGET_ZCODE` (the compilation target, from the `target` setting). They behave as
`#define`d symbols in `#if` expressions and as inline literals in Beguile expressions. Their values
and the resolution rule are given in Appendix F.

**See also** Appendix F, §17.3.

### 14.2.5 `#if`, `#elif`, `#else`, `#endif`

**Syntax**

```syntax
#if ⟨expression⟩
    …
[ #elif ⟨expression⟩
    … ] …
[ #else
    … ]
#endif
```

**Description**

Includes or excludes the enclosed source text according to a compile-time expression over the
currently defined symbols. Source in an excluded branch is skipped without being parsed. Conditional
blocks nest. They may enclose declarations as well as statements; an excluded declaration does not
exist.

The expression may contain: symbol names; integer literals; `true` and `false` (the values `1` and
`0`); the comparisons `==`, `!=`, `<`, `>`, `<=`, `>=`; `&&`, `||`, `!`; and parentheses.

A bare symbol name is true when the symbol is *defined*, whatever its value, so `#if V` is true even
when `V` was defined as `0` or `false`. In a comparison the name resolves to the symbol's *value*; an
undefined symbol compares as `0`. Thus after `#define V false`, `#if V` and `#if V == false` are both
true and `#if V == true` is false.

There is no `#ifdef` or `#ifndef`: `#if SYMBOL` and `#if !SYMBOL` test definedness.

**Example**

```bgl
#define DEBUG
#define FEATURE_LEVEL 3

#if DEBUG && beguiler >= 1010
    print("debug mode on Beguile 1.1+");
#endif

#if FEATURE_LEVEL > 2
    // FEATURE_LEVEL is 3 or higher
#elif FEATURE_LEVEL == 1
    // exactly 1
#else
    // fallback
#endif

#if TARGET_ZCODE <= 5
    // Z5 only (`TARGET_ZCODE` carries the version number, Appendix F)
#endif
```

**Notes**

The `##if` / `##else` / `##endif` forms provide the same conditional logic inside emitter bodies and
are not valid in ordinary Beguile source; see §7.4.

**See also** §7.4.

## 14.3 Diagnostics and Control

### 14.3.1 `#message`

**Syntax**

```syntax
#message "⟨text⟩"
```

**Description**

Writes the string to the compiler's standard output during compilation. It does not affect the
generated output or the result of compilation.

**Example**

```bgl
#message "Loading custom library..."
```

**See also** §20.5.

### 14.3.2 `#warning`

**Syntax**

```syntax
#warning "⟨text⟩"
```

**Description**

Reports a warning with the given text, in the compiler's standard warning format with file name and
line number, and continues compilation.

**Example**

```bgl
#if !FEATURE_XYZ
    #warning "FEATURE_XYZ is not defined - some functionality will be disabled."
#endif
```

**See also** §19.3.

### 14.3.3 `#error`

**Syntax**

```syntax
#error "⟨text⟩"
```

**Description**

Halts compilation with an error carrying the given text, reported in the compiler's standard error
format with file name and line number.

**Example**

```bgl
#if !PLATFORM_DEFINED
    #error "You must define PLATFORM_DEFINED before including this file."
#endif
```

**See also** §19.2.

### 14.3.4 `#exit`

**Syntax**

```syntax
#exit
```

**Description**

Stops processing the current file immediately, as though its end had been reached. Any conditional
nesting (§14.2.5) still open in that file is discarded. Open code blocks are not closed, so `#exit`
is used only at the top level of a file.

**Example**

```bgl
#once
#if !FEATURE_ENABLED
    #exit
#endif
// … feature implementation follows …
```

**See also** §14.2.5.

## 14.4 Raw I6 Placement

The directives in this section carry a body of raw I6 text between braces. The body is not parsed as
Beguile; only the substitution in §14.4.5 is applied. All of them share two rules:

- **Deduplication.** Each source file contributes its blocks at most once, however many times the
  file is included. `#once` is not required for this.
- **Ordering.** Blocks from different files are placed in file-inclusion order, the order in which
  the compiler first encounters each file.

### 14.4.1 `#startup`

**Syntax**

```syntax
#startup {
    ⟨raw I6 statements⟩
}
```

**Description**

Registers I6 statements to run at program startup. The statements run inside the runtime's
`bglInit()` routine, before any global variable initializers, and before startup blocks from files
included later. The IF library bindings (§23.3.1) call `bglInit()`; a program built without a
binding must call it from its own starting routine.

**Example**

```bgl
#startup {
    _initializeStringBuffers();
}
```

**Notes**

Intended for library code that must initialize runtime infrastructure before any object is
constructed. Program code normally uses object initializers or the game's starting routine instead.

**See also** §18.8, §21.2, §23.3.1.

### 14.4.2 `#emitfirst`

**Syntax**

```syntax
#emitfirst {
    ⟨raw I6⟩
}
```

**Description**

Places the body at the beginning of the generated I6 program, before the runtime and before every
declaration, so that it precedes any I6 library included later. This is where an I6 directive that
must appear early belongs: a `Replace`, a banner constant, or conditional-compilation setup that an
I6 library reads while it is being included. The body may use `##beguilerSettings.<key>` (§14.4.5).

**Example**

```bgl
#emitfirst {
    Replace DrawStatusLine;
    Constant story    = ##beguilerSettings.title;
    Constant headline = ##beguilerSettings.headline;
}
```

**Notes**

The IF library bindings use this directive to declare the constants their I6 library requires
(§23.3.2).

**See also** §15.6, §18.6, §23.3.2.

### 14.4.3 `#emitlast`

**Syntax**

```syntax
#emitlast {
    ⟨raw I6⟩
}
```

**Description**

Places the body at the very end of the generated I6 program, after every declaration, grammar
directive and object definition. The body may use `##beguilerSettings.<key>` (§14.4.5).

**Example**

```bgl
#emitlast {
    [ DrawStatusLine;
        ! replacement routine
    ];
}
```

**See also** §18.6.

### 14.4.4 `#storedEmitFirst` and `#storedEmitLast`

**Syntax**

```syntax
#storedEmitFirst ⟨name⟩ {
    ⟨raw I6⟩
}
#storedEmitLast ⟨name⟩ {
    ⟨raw I6⟩
}
```

**Description**

Registers a named, deferred raw-I6 block. Unlike `#emitfirst` and `#emitlast`, a stored block is not
placed in the output by default: it is emitted, once, only when a built-in I6 template that needs it
is applied (§18.9). When emitted, the block takes the position of the corresponding non-stored form:
the top of the program for `#storedEmitFirst`, the end for `#storedEmitLast`. A program in which no
template needs the block pays nothing for it.

Several stored blocks may share one name, and one template may need several names. Registering a
name that is already registered replaces the earlier block (latest wins); `#once` on the declaring
file is the normal guard. The body may use `##beguilerSettings.<key>` (§14.4.5).

**Example**

```bgl
#storedEmitFirst scratchSupport {
    Array  scratchStack --> 33;
    Global scratchTop = 0;
    [ scratchPush v; … ];
}
```

**Notes**

Intended for BLR authors: helper routines that some runtime feature needs but most programs do not.

**See also** §18.9, Appendix G.

### 14.4.5 `##beguilerSettings.<key>` Substitution

**Syntax**

```syntax
##beguilerSettings.⟨key⟩
```

**Description**

Inside the raw I6 body of `#emitfirst`, `#emitlast`, `#storedEmitFirst` or `#storedEmitLast`, this
marker is replaced by the compile-time value of the named `#beguilerSettings` property. A string
property becomes an I6 quoted string (`"…"`); an integer property becomes a decimal literal. The
property names are those accepted by the `#beguilerSettings.prop` expression form (§14.7.2, §17.7).
The substitution is applied only in the bodies of `#emitfirst`, `#emitlast`, `#storedEmitFirst` and
`#storedEmitLast`; it is not applied in `#startup`, `#i6` or emitter bodies.

**Example**

```bgl
#emitfirst {
    Constant story = ##beguilerSettings.title;
}
```

**See also** §14.7.2, §17.7, Appendix G.

## 14.5 Islands

An island is a region of one language embedded in a stream of the other. This section gives the
syntax; the two compilation modes are in §15.1, the semantics of I6 islands in §15.2, of Beguile
islands in §15.3, and loose identifier resolution in §15.3.3.

### 14.5.1 `#i6`

Syntax: `#i6 ⟨raw I6 to end of line⟩` or `#i6 { ⟨raw I6⟩ }`. See §15.2.

### 14.5.2 `#bgl`, `#bglDecl`, `#bglStmt`

Syntax: `#bgl ⟨statement⟩ ;`, `#bgl { … }`, `#bglDecl { ⟨declarations⟩ }`, `#bglStmt { ⟨statements⟩ }`. See §15.3.

## 14.6 Namespace Import

### 14.6.1 `#using`

**Syntax**

```syntax
#using ⟨name⟩ [ .⟨name⟩ ] … [ ; ]
```

**Description**

Imports the members of a class or object into the current file's scope so they may be used
unqualified. Every rule of the directive — its file scope, the priority of imported names, what each
kind of target contributes and alias imports — is specified in §10.4.

**See also** §10.4.

## 14.7 Settings

### 14.7.1 `#beguilerSettings { … }`

Syntax: `#beguilerSettings { ⟨property⟩ = ⟨value⟩ ; … }`. See §17.1 and Appendix E.

### 14.7.2 `#beguilerSettings.prop`

Syntax: `#beguilerSettings.⟨property⟩`. See §17.7.
