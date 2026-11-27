# 19 Diagnostics

<!-- toc -->
- [19.1 Message Format](#191-message-format)
  - [19.1.1 Inform 6 Diagnostics](#1911-inform-6-diagnostics)
- [19.2 Compile-Time Errors](#192-compile-time-errors)
- [19.3 Warnings](#193-warnings)
- [19.4 Runtime Failures](#194-runtime-failures)
<!-- /toc -->

## 19.1 Message Format

Every diagnostic which the compiler produces about the source is a single line, in one of the following forms:

```text
FILE:LINE:COLUMN: ERROR: MESSAGE
FILE:LINE:COLUMN: warning: MESSAGE
```

`FILE` is the path of the Beguile source file containing the construct, as the compiler resolved it.  When a diagnostic is anchored to a statement, the column is `1`; otherwise, it is the position at which the lexer stopped.  Diagnostics are written to standard error.

**Errors stop the build.**  The first error ends compilation with exit status 1 (§17.7), so you will never see more than one per build.

**Warnings continue.**  A warning is advisory, and the build proceeds.  Warnings whose location lies inside the system library (the `beguiLib` tree) are not shown.

**Location-free messages.**  A few checks run after parsing, when no source position is current; their messages carry no `file:line:column` prefix.

### 19.1.1 Inform 6 Diagnostics

Inform 6's own output is passed through the console, with each diagnostic rewritten to Beguile conventions:

```text
FILE.transpiled.inf:LINE:1: ERROR: MESSAGE
  ↳ FILE.bgl:LINE:1
```

The first line names the transpiled file.  The indented second line names the Beguile source line from which that I6 line was generated, and is omitted when no Beguile line corresponds (e.g., in a compiler-generated header line).  Inform 6's "Fatal error" and "Error" both appear as `ERROR`, and "Warning" appears as `warning`.  Inform 6 warnings which map into the system library, or which name a `_bgl`-prefixed symbol, are hidden, and Inform 6's closing "Compiled with N warnings" count is adjusted to the number actually shown.  Any Inform 6 error fails the build, even when Inform 6 itself exits successfully.

## 19.2 Compile-Time Errors

The categories below are the ones you are most likely to meet.  The messages are quoted as representatives; `'x'` stands for the offending name.

**Global name collisions.**  A global name which is not unique (§3.1) is an error, and the message always cites the original declaration:

```text
'score' is already defined (originally declared at myLibrary.bgl:17)
'score' is already defined as a type (originally declared at myLibrary.bgl:17)
```

Collisions between Beguile names and symbols defined only in raw I6 (`#i6`, `#includeI6`) are not visible to Beguile, and will surface as Inform 6 errors instead; declare such symbols `extern` (§16.4) to bring them into the Beguile namespace.

**Member collisions.**  A member declared twice in one class or object, or added by `extend` when a member of that name exists, is an error unless `replace` is used (§8.7):

```text
class 'Room': member 'light' is already defined
extend class 'Room': member 'describe' is already defined; use 'replace' to override
```

**Ambiguous names.**  A bare identifier which could refer to more than one declaration (for instance, members of two `#using` imports), or an operator reference with more than one candidate:

```text
'val' is ambiguous: matches #using-imported member 'libA.val' and #using-imported member 'libB.val'. Qualify the use explicitly to disambiguate.
```

**Inheritance and emission order** (§8.6, §18.7):

```text
class 'A': circular inheritance; 'B' transitively inherits from 'A'
class 'Room' lists attribute 'light' (`attributes = {light}`), but its declaration `extern attribute light;` comes later in source (needed by: …). Declare the attribute, or include the bindings file that declares it, before the class.
```

**Includes and directives** (§14):

```text
#include: file 'x' not found
Maximum include nesting depth (255) exceeded while including 'x'
'#define x' redefines a symbol that is already defined; use '#redef x' to intentionally redefine it
```

**Settings** (§15.1):

```text
Unknown beguilerSettings property 'x'
beguilerSettings property 'x' expects an int, got 'y'
beguilerSettings property 'serial' must be exactly 6 digits (e.g., "250328")
beguilerSettings property 'framePoolSize' must be at least 1
beguilerSettings includePaths entry 'x' does not resolve to an existing directory ('…'). Use @"..." to emit a literal path without filesystem validation.
#beguilerSettings.x: unknown or unsupported property
```

**Qualifier misuse.**  Combining qualifiers which exclude one another, or applying one where it has no effect, is an error; for example, `superposed` on an `extern`, `emitter`, or `alias` class, or on `extend class` (§3.2, §3.12).

**Post-emission check.**  After the transpiled file is written, the compiler verifies that no property-class member (§9.9) was emitted as a raw property access.  A failure names the transpiled file and line, and suggests reading the value into a local first.

## 19.3 Warnings

The compiler's warnings fall into the following categories.

**`#using`** (§10.4).  The directive is ignored, with a warning, when its target is not a declared class or object, when a named member doesn't exist, or when the member is not of an importable type:

```text
#using 'myLib': not a declared class or object; directive ignored
#using 'myLib.x': member not found; directive ignored
```

**Shadowing.**  A local variable, parameter, or loop variable which hides a global or a capturable name from an enclosing function, or a local or loop variable which hides a member of the enclosing class or object (a parameter can share a member's name):

```text
Local variable 'score' shadows the global variable 'score'; '::score' reaches it.
Local variable 'name' shadows a member of class 'Thing'.
```

**Object-member ambiguity.**  Inside an object body, a bare identifier which resolves at file scope but is also a property of the enclosing object (§3.8):

```text
Bare 'light' resolves as global variable 'light', but 'light' is also a property of the enclosing object. Beguile cannot tell which you mean. Write 'self.light' for the object's property, or '::light' to force the global and silence this warning.
```

**Method overriding without `replace`** (§8.7.3):

```text
class 'Door': method 'describe' shadows definition in base class 'Thing'; use 'replace' to suppress this warning
replace: no existing global function 'x' found; treating as new definition
```

**String comparison** (§2.2).  `==` or `!=` between two `string`s, which compares their addresses:

```text
'==' between two strings compares their addresses, not their text.
```

**Verbs and grammar** (§13.2.3, §13.5.2).  Pattern tokens after the trigger word in an `extern verb`'s grammar are ignored; a `grammar -=` which matches nothing removes nothing:

```text
extern verb 'PutOn': pattern tokens after the trigger word(s) in `grammar = {...}` are ignored; …
grammar -= on 'Take': no grammar for word `'grab'` exists, so nothing was removed. …
```

**Precompiler mode.**  A routine, global, constant, object, or other symbol declared in the surrounding I6 whose name is also a Beguile global:

```text
FILE: warning: I6 routine 'Score' collides with Beguile-declared global of the same name
```

**Unreachable `bglInit()`** (§21.2).  The program has runtime initialization to do (a sized byte array to stamp, a `#startup` block, or a deferred global initializer) and nothing in the transpiled file calls `bglInit()`.

```text
WARNING: nothing calls bglInit(), so this program starts with the BLR uninitialized.
```

**Other.**  `hide` naming a member which is not inherited; re-typing a `typesealed` member (the retype is ignored); `superposed` on a non-`static` method (no effect).

## 19.4 Runtime Failures

The generated code and the BLR detect a small number of conditions at run time.  Each prints a bracketed message and halts the story with `quit`, except where stated.

| Condition | Message | Recovery |
| --- | --- | --- |
| Frame pool full (§18.10) | `[Beguile runtime error: frame pool exhausted]` | Raise `framePoolSize`. |
| The heap is full (§12.6); the program stops | `[Beguile run-time error: the heap is full; raise heapSize in #beguilerSettings (it is N words)]` | Raise `heapSize`, or free arrays sooner with `bgl.gc.free` (§21.15). |
| The string heap is full (§22.3); the text is cut short; debug builds only | `[Beguile run-time warning: the string heap is full, so text was cut short; raise the stringHeapSize setting]` | Raise `stringHeapSize`; the `heap` debug verb (§20.3) reports the most a run needed. |
| The string heap can't hold the string pool at startup, or has no room for a Z-machine capture (§22.3); the program stops | `[Beguile run-time error: the string heap is full; raise the stringHeapSize setting]` | Raise `stringHeapSize`. |
| Text printed into a string runs past its room (§22.3); the text is cut short; debug builds only | `[Beguile run-time warning: N characters were printed into room for M, so the text was cut short; raise …]` | Raise the setting it names:  `stringCaptureSize` with `<string>`, `bglStringDefaultSize` for a capture into an untracked buffer. |
| The same on the Z-machine, where the text has overwritten memory in use (§22.3); the program stops | `[Beguile run-time error: N characters were printed into room for M; raise …]` | Raise the setting it names. |
| `bgl.gc.free` given an array already freed (§21.15); the program continues | `[Beguile run-time error: bgl.gc.free(): the array was already freed]` | - |
| `bgl.gc.free` given a file-scope or member array (§21.15); the program continues | `[Beguile run-time error: bgl.gc.free(): the array isn't on the heap (…)]` | - |
| Copying into a `ref array` bound to nothing (§12.5); the program continues | `[Beguile run-time error: copying into an array that is null; bind it with := first]` | Bind it with `:=`. |
| Copying into an array that was freed (§21.15); the program continues | `[Beguile run-time error: copying into an array that was freed]` | - |
| An array index out of range (§12.3); the program continues | `[Beguile run-time error: array index I is outside the N elements it holds; the read is skipped]` | - |
| Every instance of a struct in use (§12.10.1); the program stops | `[Beguile run-time error: all N instances of S are in use: …]` | Declare more slots, or let arrays go sooner. |
| Adding a struct to an array that shares its elements (§12.10.1); the program continues | `[Beguile run-time error: adding a struct to an array that shares its elements; …]` | Declare the array with a capacity. |
| `first()` or `last()` of an empty array of structs (§22.5); the program stops | `[Beguile run-time error: first() of an empty array of values; check count() first]` | Check `count()` first. |
| An array `length` assigned beyond the word range (§12.3) | `[Beguile runtime error: array length exceeds signed range (max 32767 on Z, 2^31-1 on Glulx)]` | - |
| `<string>` pool full (§22.3); the program stops | `[Beguile runtime error: the <string> pool is out of strings; raise bglStringPoolReserve]` | Raise `bglStringPoolReserve`. |
| `<buf>` registry full (§22.2) | `[Beguile runtime: tracked buffer registry full; raise bglTrackedBufferReserve]` | Raise `bglTrackedBufferReserve`. |
| Pooled class full (§8.2.6) | none:  `new` returns `nothing` | Test the result of `new`, or size the pool larger. |
| `bgl.world` result buffer full (§21.9) | none:  the walk stops at `worldBufSize` objects | Narrow the query, or raise `worldBufSize` (§15.4). |
| `throw` with no enclosing `try` (§5.16); the program stops | `[Unhandled exception]` | Throw only where a `try` is on the call stack. |
