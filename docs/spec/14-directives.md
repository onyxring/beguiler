# 14 Directives

<!-- toc -->
- [14.1 Source Organization](#141-source-organization)
  - [14.1.1 `#include <NAME>`](#1411-include-name)
  - [14.1.2 `#include "path"`](#1412-include-path)
  - [14.1.3 `#include @"path"`](#1413-include-path)
  - [14.1.4 `#include ?"path"` and `#include ?<NAME>`](#1414-include-path-and-include-name)
  - [14.1.5 `#includeI6`](#1415-includei6)
  - [14.1.6 `#mergeI6`](#1416-mergei6)
  - [14.1.7 `#once`](#1417-once)
  - [14.1.8 `#emitAfterI6Include`](#1418-emitafteri6include)
- [14.2 Symbols and Conditional Compilation](#142-symbols-and-conditional-compilation)
  - [14.2.1 `#define`, `#defineI6`](#1421-define-definei6)
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
  - [14.4.5 `##beguilerSettings.KEY` Substitution](#1445-beguilersettingskey-substitution)
- [14.5 Islands](#145-islands)
  - [14.5.1 `#i6`](#1451-i6)
  - [14.5.2 `#bgl`, `#bglDecl`, `#bglStmt`](#1452-bgl-bgldecl-bglstmt)
- [14.6 Namespace Import](#146-namespace-import)
  - [14.6.1 `#using`](#1461-using)
- [14.7 Settings](#147-settings)
  - [14.7.1 `#beguilerSettings { … }`](#1471-beguilersettings-)
  - [14.7.2 `#beguilerSettings.prop`](#1472-beguilersettingsprop)
<!-- /toc -->

A directive is a compile-time instruction introduced by `#`.  Directive names are case-insensitive, like every Beguile identifier (§1.3).  Unless an entry says otherwise, a directive is written at global scope.  An unrecognized directive is a compile-time error.

Inside a function body, you can write only the conditionals (§14.2.5), `#message`, `#warning`, `#error`, and the island directives (§14.5).  Any other directive there is a compile-time error.

Appendix B lists every directive alphabetically.

## 14.1 Source Organization

### 14.1.1 `#include <NAME>`

**Syntax**

```syntax
#include <NAME>
#include <SUB/NAME>
#include <A/B/NAME>
```

The angle brackets are literal.

This includes a Beguile source file from the Beguile Language Runtime library tree (`beguiLib`, located by the `beguiLibPath` setting, §15.2).  The tree is searched recursively for `NAME.bgl`; the compiler supplies the `.bgl` extension, which you must not write.

- `<NAME>` matches `NAME.bgl` anywhere in the tree, root or any subfolder.
- `<SUB/NAME>` matches `NAME.bgl` only when its immediate parent folder is `SUB`, at any depth.
- `<A/B/NAME>` requires the resolved path to end with `A/B/NAME.bgl`.

The search is deterministic:  the root folder's files are checked first, then each subfolder in alphabetical order, depth first, and the first match wins.  Matching is case-insensitive regardless of the file system.  A file which is not found is a compile-time error.  Only the library is searched; the source folder and `includePaths` belong to the quoted form (§14.1.2).

**Example**

<!-- doctest: skip -->
```bgl
#include <string>                  // beguiLib/string.bgl
#include <i6StandardLibrary>       // same as <bindings/i6StandardLibrary>
#include <bindings/punyInform>     // beguiLib/bindings/punyInform.bgl
```

**See also** §18.5, §22.

### 14.1.2 `#include "path"`

**Syntax**

```syntax
#include "PATH"
```

This includes a Beguile source file by path.  The compiler searches the directory of the current source file, then each `includePaths` directory (§15.3), trying *path*`.bgl` first and then *path* exactly as written.  Subdirectory paths are allowed and resolve relative to each directory searched, with either `/` or `\` as the separator.  Matching is case-insensitive.  A file which is not found is a compile-time error.

A quoted include never finds a file in the Beguile library, even when a directory it searches lies inside it; a quoted include of a library file is a compile-time error naming the angle-bracket form (§14.1.1).

**Example**

<!-- doctest: skip -->
```bgl
#include "myLibrary"
#include "utils/helpers"
```

> ***Note:** Beguile has no equivalent of Inform 6's `>filename` prefix.*

**See also** §14.1.7, §18.5.

### 14.1.3 `#include @"path"`

**Syntax**

```syntax
#include @"PATH"
#include ?@"PATH"
```

This works as `#include "path"` does, except that the path is a raw string (§1.6.4) with no escape processing.  An interpolated string (`$"…"`) is not accepted as an include path.

**Example**

<!-- doctest: skip -->
```bgl
#include @"vendor\legacy\helpers"
```

**See also** §1.6.4.

### 14.1.4 `#include ?"path"` and `#include ?<NAME>`

**Syntax**

```syntax
#include ?"PATH"
#include ?<NAME>
```

The angle brackets are literal.

These work as the corresponding unmodified forms do, except that a file that is not found is silently skipped.

**Example**

```bgl
#include ?"optionalExtension"
#include ?<glulxImage>
```

### 14.1.5 `#includeI6`

**Syntax**

```syntax
#includeI6 "NAME"
#includeI6 ?"NAME"
#includeI6 @"NAME"
```

`#includeI6` includes an Inform 6 source file in the generated program.  The file is resolved like `#include "path"` (§14.1.2):  the current source file's directory, then each `includePaths` directory, trying *name* as written and then *name*`.h`.  Subdirectory paths are allowed.  A file which is not found is a compile-time error.

`?"name"` silently skips a file that is not found.  `@"name"` hands the string to the I6 compiler verbatim, with no search, existence check, or separator rewriting.

An included I6 file's own includes are also searched for in every `includePaths` directory.

A `.patch` or `.diff` file is a compile-time error here; patches are applied with `#mergeI6` (§14.1.6).

**Example**

```bgl
#includeI6 "parser"
```

> ***Note:** A Beguile declaration which depends on something the I6 file defines must follow the include (§16.4, §18.7).*

> ***Note:** An `extern` declaration for a name the included file defines can come before or after the include, except that a class using an `extern attribute` must follow that attribute's declaration (§18.7).*

**See also** §16.5, §18.5.

### 14.1.6 `#mergeI6`

**Syntax**

```syntax
#mergeI6 "NAME"
#mergeI6 ?"NAME"
```

`#mergeI6` applies a patch to the Inform 6 files of the build.  The patch is a unified diff, as written by `diff -u` or `git diff`.  It is found like `#includeI6` (§14.1.5), trying *name* as written, then *name*`.patch`, then *name*`.diff`.  A patch which is not found is a compile-time error; however, `?"name"` silently skips it, and skips any file the patch names which is not found.

Each file the patch changes is found through the same search, by its `+++` path without a leading `a/` or `b/`, then by its file name alone.  Its **merge chunks**, the sections which each start with `@@`, are applied in order.  A merge chunk must match its lines exactly, ignoring trailing whitespace, at the line it states or the nearest place they match.  A merge chunk which matches nowhere, a file that is not found, or a malformed patch is a compile-time error at the directive.  A patch can't create or delete a file.

A **routine chunk** replaces a whole routine without quoting its old text.  It starts with `@@ routine NAME @@`, and the lines after it, up to the next chunk or file header, are the routine which takes the old one's place:

```
--- a/verblib.h
+++ b/verblib.h
@@ routine Banner @@
[ Banner;
    print "My banner.^";
];
```

The routine is found by name, wherever it is in the file; where the file doesn't hold it, the files it includes are searched.  A routine which is not found, or which the file defines more than once, is a compile-time error.

A merge applies to the whole build, not at its position:  a patch can come before or after the `#includeI6` of the file it changes.  Patches apply in source order, each to the file as the earlier ones left it.  Two patches which change the same lines conflict.  A patch merged more than once applies once.

The patched files are written to the `i6patched` folder of the output directory, and every include of an original will read the patched copy, including one made inside another I6 file.  When the source file is a `.inf`, merges are not applied; its Inform 6 files are read as they are.

**Example**

Here's a patch which makes the library's `LookSub` call a Beguile function first:

```diff
--- a/verblib.h
+++ b/verblib.h
@@ -2446,4 +2446,5 @@
 [ LookSub allow_abbrev  visibility_levels i j k nl_flag;
+    LookHook();
     if (parent(player) == 0) return RunTimeError(10);
 
   .MovedByInitial;
```

An extension merges it and defines the function, so a program need only include the extension:

<!-- doctest: skip -->
```bgl
#once
#mergeI6 "lookHook"
void LookHook() { printLine("You look around."); }
```

> ***Note:** Patches can't be used in precompiler mode (§17.8).*

> ***Note:** To write a patch, edit a copy of the file and write the differences with `diff -u original.h edited.h > name.patch`, `git diff`, or `beguiler --make-patch` (§17.10).  Any unified diff will do, including one pasted into a `.patch` file.*
>
> ***Note:** Lines before the first `---` line are not part of any change, so a patch can start with comments (by convention, lines starting with `#`) which never reach the patched file.  `beguiler --make-patch` keeps them when it rewrites the patch.*

> ***Note:** Replacing a library routine (§16.6) takes the whole routine, and only one replacement can win; however, a patch changes only the lines it names, so extensions which each change a routine can be used together.*

**See also** §16.6, §20.1.

### 14.1.7 `#once`

**Syntax**

```syntax
#once
```

Placed at the top of a source file, `#once` marks the file so that any later `#include` of the same file (by any path resolving to the same location) is silently ignored.  Without `#once`, a file is processed every time it is included, so circular includes of such files exceed the include nesting limit (§18.4).

**Example**

```bgl
#once
// rest of myLibrary.bgl …
```

> ***Note:** Every file intended to be included as a library should begin with `#once`.*

**See also** §14.4 (per-file deduplication that does not depend on `#once`).

### 14.1.8 `#emitAfterI6Include`

**Syntax**

```syntax
#emitAfterI6Include "NAME"
```

Some code simply has to come after a piece of the library; an object which lives in `LibraryExtensions` can't appear before the parser defines it.  Put this directive at the top of a file and everything below it is written out right after the program's `#includeI6 "NAME"` (§14.1.5), no matter where the file itself is included.  The name is the Inform 6 file's, with or without the `.h` and in any letter case.

A file which depends on an Inform 6 file says so once, and a program can then include it anywhere:

```bgl
// lookCount.bgl
#once
#emitAfterI6Include "verblib"

int looks = 0;
replace void LookSub() { looks++; print("You look around.^"); }
```

<!-- doctest: skip -->
```bgl
#include <bindings/i6StandardLibrary>
#include "lookCount"          // emitted after verblib

#includeI6 "parser"
#includeI6 "verblib"
```

The directive covers this file's own declarations, from the directive to the end of the file.  A file it includes is emitted where that file's own directive says, or at the include when it has none.  Files placed after the same Inform 6 file keep the order in which they were included.  When the `#includeI6` comes before this file's `#include`, the file is emitted at its include.

A file can have only one `#emitAfterI6Include`.  A program which never includes the named Inform 6 file is a compile-time error at the directive.

**See also** §14.1.5, §16.5.

## 14.2 Symbols and Conditional Compilation

### 14.2.1 `#define`, `#defineI6`

**Syntax**

```syntax
#define NAME
#define NAME VALUE
#defineI6 NAME
#defineI6 NAME VALUE
```

`#define` defines a compilation symbol.  Without a value, the symbol has the boolean value `true`, which is what `#if` tests (§14.2.5); such a symbol is not a name in a Beguile expression, where using it is a compile-time error.  A symbol with a value can also be used in a Beguile expression, where the name is replaced by its value at compile time:  an integer value is an integer literal, a decimal number (`0.5`) is a float literal, and a quoted string is that string.  Any other word is a string literal of that word.

`#define` is linear:  it affects only the source which follows it, in the order files are processed.  Defining a symbol which is already defined is a compile-time error (use `#redef`, §14.2.2), as is defining one that was `#declare`d (§14.2.3).

A symbol shares its name with declarations.  A parameter, local, loop variable, or lambda parameter of that name hides the symbol where it is in scope, and so does a member of the class or object whose code is being compiled; each draws a warning.  A member's initializer is outside that code and reads the symbol.  `::NAME` skips the hiding declaration and reaches the symbol, unless a variable is declared at file scope under the name; in that case `::NAME` is that variable, and a bare `NAME` there is ambiguous and an error.  A property of the name doesn't compete with the symbol, except where a property is expected (`obj.provides(capacity)`).

`#defineI6` is a `#define` which also passes the symbol to Inform 6 as a `Constant` of the same name and value, placed ahead of any I6 include.  In a Beguile expression, a `#defineI6` symbol with a value is emitted by name, as the I6 constant, rather than replaced by its value.  A later `#undef` or `#redef` affects only the Beguile symbol; the I6 constant stays.

Use `#defineI6` to set an I6 library's option constants (`NO_PLACES`, `OPTIONAL_EXTENDED_METAVERBS`, …), since the library bindings declare a library's optional parts only under the matching symbol (§23.3.10).  Note the following:

- The `#defineI6` must come before the `#include` of the binding, or the binding's `#if` won't see it.
- A constant written in raw I6 (`#i6 { Constant NO_PLACES; }`) is invisible to `#if`, so the binding will still declare the parts it removes.

**Example**

```bgl
#define CHEATS         // CHEATS is true
#define MAX_SCORE 100
#defineI6 OPTIONAL_EXTENDED_METAVERBS   // also Constant OPTIONAL_EXTENDED_METAVERBS; in I6
const int maxScore = MAX_SCORE;

// … in a routine:
if(score >= MAX_SCORE) print("You win!");   // compiles as: if(score >= 100)
```

### 14.2.2 `#redef` and `#undef`

**Syntax**

```syntax
#redef NAME
#redef NAME VALUE
#undef NAME
```

`#redef` behaves exactly like `#define`, except that an already-defined symbol is overwritten rather than reported.  `#undef` removes a symbol, so that a later `#if` will treat it as undefined.  Both are linear, like `#define`.  Applying either to a `#declare`d symbol is a compile-time error.

**Example**

```bgl
#define LEVEL 1
#redef  LEVEL 2      // now 2
#undef  LEVEL        // now undefined
```

### 14.2.3 `#declare`

**Syntax**

```syntax
#declare NAME
#declare NAME VALUE
```

`#declare` defines a symbol just as `#define` does, with two differences:

- **Order-independent.**  A declared symbol is visible to every `#if` in the program, including those in files processed before the file containing the `#declare`.
- **Immutable.**  A declared symbol can't be `#undef`d, `#redef`d, or shadowed by a `#define`; each is a compile-time error, as is a second `#declare` of the same name with a different value.

In every other respect it matches `#define`.

| | `#define` / `#redef` | `#declare` |
|---|---|---|
| Visibility | Linear:  source after the directive | Global:  every `#if`, before or after |
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

The compiler defines the following symbols before any source file is processed:  `beguiler`, `beguilerMajor`, `beguilerMinor`, `beguilerPatch` (the compiler version); exactly one of `TARGET_GLULX` or `TARGET_ZCODE` (the compilation target, from the `target` setting); `DEBUG` in a debug build (§20.3); and `bglAutoInitialize` when the `autoInitialize` setting is `true` (§15.4).  They behave as `#define`d symbols in `#if` expressions, and those with a value as inline literals in Beguile expressions.  Their values and the resolution rule are given in Appendix F.

`generateBlorb` is also visible to `#if` as the symbol `generateBlorb`, carrying the value `true` or `false` (§15.6).  Test it by value, `#if (generateBlorb == true)`, since the symbol is always declared (Appendix F).

The two target symbols are flags with no value.  To tell `Z5` from `Z8`, read the setting instead:  `#if #beguilerSettings.target == "z8"` (§14.2.5, §15.7).

**See also** Appendix F, §15.3.

### 14.2.5 `#if`, `#elif`, `#else`, `#endif`

**Syntax**

```syntax
#if EXPRESSION
    …
[ #elif EXPRESSION
    … ] …
[ #else
    … ]
#endif
```

These directives include or exclude the enclosed source text according to a compile-time expression over the currently defined symbols.  Source in an excluded branch is skipped without being parsed.  Conditional blocks nest, and an excluded declaration doesn't exist.

`#if`, `#elif`, `#else`, and `#endif` can appear anywhere a declaration, member, statement, or `extend` edit can:  at file scope, in a function body, in an object or class body, in an `extend` body, and among the `inject`, `remove`, and `move` lines of an array's `extend` body (§12.11).

The expression can contain:  symbol names; integer literals; string literals; `#beguilerSettings.PROPERTY` references (§15.7); `true` and `false` (the values `1` and `0`); the comparisons `==`, `!=`, `<`, `>`, `<=`, `>=`; `&&`, `||`, `!`; and parentheses.

A `#beguilerSettings.PROPERTY` reference resolves to the property's value, as it does in a Beguile expression (§14.7.2).  String comparison ignores case.  The property must be written with no space around the `.`; an undeclared property is a compile-time error, not a false condition.

The condition runs to the end of its line.  Anything after it on that line, other than a comment, is a compile-time error.

A bare symbol name is true when the symbol is *defined*, regardless of its value, so `#if V` is true even when `V` was defined as `0` or `false`.  In a comparison, the name resolves to the symbol's *value*; an undefined symbol compares as `0`.  A symbol whose value is a quoted string compares as its text.

There is no `#ifdef` or `#ifndef`, in either the single- or double-hash form; `#if SYMBOL` and `#if !SYMBOL` test definedness.  `#if` tests *Beguile* symbols (a symbol set with `#defineI6` is both, §14.2.1).  To emit a conditional for the Inform 6 compiler to evaluate (over one of its own constants, say), write it in an `#i6` island, where a single-hash directive is raw I6 and passes through (§7.4):

```bgl
#i6 { #Ifndef NO_SCORE; print "^"; #Endif; }
```

**Example**

```bgl
#define CHEATS
#define FEATURE_LEVEL 3

#if CHEATS && beguiler >= 1010
    print("cheats on, Beguile 1.1+");
#endif

#if FEATURE_LEVEL > 2
    // FEATURE_LEVEL is 3 or higher
#elif FEATURE_LEVEL == 1
    // exactly 1
#else
    // fallback
#endif

#if TARGET_ZCODE
    // any Z-machine target
#endif

#if #beguilerSettings.target == "z8"
    // Z8 only: the target symbols say which machine, the setting says which version
#endif
```

> ***Note:** The `##if` / `##else` / `##endif` forms provide the same conditional logic inside emitter bodies and are not valid in ordinary Beguile source; see §7.4.*

**See also** §7.4.

## 14.3 Diagnostics and Control

### 14.3.1 `#message`

**Syntax**

```syntax
#message "TEXT"
```

`#message` writes the string to the compiler's standard output during compilation.  It has no effect on the generated output or on the result of compilation.

**Example**

```bgl
#message "Loading custom library..."
```

**See also** §20.5.

### 14.3.2 `#warning`

**Syntax**

```syntax
#warning "TEXT"
```

`#warning` reports a warning with the given text, in the compiler's standard warning format with file name and line number, and compilation continues.

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
#error "TEXT"
```

`#error` halts compilation with an error carrying the given text, reported in the compiler's standard error format with file name and line number.

As the whole body of an emitter, `#error` makes using that emitter the error, reported at the line that uses it.  An `operator = (nullLiteral)` written this way is how a class refuses `null`:

```bgl
emitter class Tag {
    emitter Tag operator = (nullLiteral v) { #error "A Tag can't be null." }
}
```

**Example**

<!-- doctest: error -->
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

`#exit` stops processing the current file immediately, as though its end had been reached.  Any conditional nesting (§14.2.5) still open in that file is discarded.  Open code blocks are not closed, so `#exit` is used only at the top level of a file.

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

The directives in this section carry a body of raw I6 text between braces.  The body is not parsed as Beguile; only the substitution in §14.4.5 is applied.  All of them share two rules:

- **Deduplication.**  Each source file contributes its blocks at most once, however many times the file is included.  `#once` is not required for this.
- **Ordering.**  Blocks from different files are placed in file-inclusion order (i.e., the order in which the compiler first encounters each file).

### 14.4.1 `#startup`

**Syntax**

```syntax
#startup {
    RAW-I6-STATEMENTS
}
```

`#startup` registers I6 statements to run at program startup.  They will run inside the runtime's `bglInit()` routine, before any global variable initializers; who calls `bglInit()` is covered in §21.2.

**Example**

```bgl
#startup {
    _initializeStringBuffers();
}
```

> ***Note:** This is intended for library code; a program normally uses object initializers or the game's starting routine instead.*

**See also** §18.8, §21.2, §23.3.1.

### 14.4.2 `#emitfirst`

**Syntax**

```syntax
#emitfirst {
    RAW-I6
}
```

`#emitfirst` places the body at the beginning of the generated I6 program, before the runtime and before every declaration, so that it precedes any I6 library included later.  The body can use `##beguilerSettings.KEY` (§14.4.5).

**Example**

```bgl
#emitfirst {
    Replace DrawStatusLine;
    Constant story    = ##beguilerSettings.title;
    Constant headline = ##beguilerSettings.headline;
}
```

**See also** §16.6, §18.6, §23.3.2.

### 14.4.3 `#emitlast`

**Syntax**

```syntax
#emitlast {
    RAW-I6
}
```

`#emitlast` places the body at the very end of the generated I6 program, after every declaration, grammar directive, and object definition.  The body can use `##beguilerSettings.KEY` (§14.4.5).

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
#storedEmitFirst NAME {
    RAW-I6
}
#storedEmitLast NAME {
    RAW-I6
}
```

These directives register a named, deferred raw-I6 block.  Unlike `#emitfirst` and `#emitlast`, a stored block is not placed in the output by default:  it is emitted, once, only when a built-in I6 template which needs it is applied (§18.9).  When emitted, the block takes the position of the corresponding non-stored form:  the top of the program for `#storedEmitFirst`, the end for `#storedEmitLast`.

A `#storedEmitFirst` block and a `#storedEmitLast` block can share one name, and one template can need several names.  Registering a second block of the same kind under a name replaces the earlier one; `#once` on the declaring file is the normal guard.  The body can use `##beguilerSettings.KEY` (§14.4.5).

**Example**

```bgl
#storedEmitFirst scratchSupport {
    Array  scratchStack --> 33;
    Global scratchTop = 0;
    [ scratchPush v; … ];
}
```

> ***Note:** These are intended for BLR authors.*

**See also** §18.9, Appendix G.

### 14.4.5 `##beguilerSettings.KEY` Substitution

**Syntax**

```syntax
##beguilerSettings.KEY
```

Inside the raw I6 body of `#emitfirst`, `#emitlast`, `#storedEmitFirst`, or `#storedEmitLast`, this marker is replaced by the compile-time value of the named `#beguilerSettings` property.  A string property becomes an I6 quoted string (`"…"`); an integer property becomes a decimal literal.  The property names are those accepted by the `#beguilerSettings.prop` expression form (§14.7.2, §15.7).  The substitution is not applied in `#startup`, `#i6`, or emitter bodies.

**Example**

```bgl
#emitfirst {
    Constant story = ##beguilerSettings.title;
}
```

**See also** §14.7.2, §15.7, Appendix G.

## 14.5 Islands

Here we give only the syntax of the island directives; islands are defined in §16.1.3, the two compilation modes are covered in §16.1, the semantics of I6 islands in §16.2, those of Beguile islands in §16.3, and loose identifier resolution in §16.3.3.

### 14.5.1 `#i6`

Syntax:  `#i6 RAW-I6-TO-END-OF-LINE` or `#i6 { RAW-I6 }`.  See §16.2.

### 14.5.2 `#bgl`, `#bglDecl`, `#bglStmt`

Syntax:  `#bgl STATEMENT ;`, `#bgl { … }`, `#bglDecl { DECLARATIONS }`, `#bglStmt { STATEMENTS }`.  See §16.3.

## 14.6 Namespace Import

### 14.6.1 `#using`

**Syntax**

```syntax
#using NAME [ .NAME ] … [ ; ]
```

`#using` imports the members of a class or object into the current file's scope so they can be used unqualified.

**See also** §10.4.

## 14.7 Settings

### 14.7.1 `#beguilerSettings { … }`

Syntax:  `#beguilerSettings { PROPERTY = VALUE ; … }`.  See §15.1 and Appendix E.

### 14.7.2 `#beguilerSettings.prop`

Syntax:  `#beguilerSettings.PROPERTY`.  See §15.7.
