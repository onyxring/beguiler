# 19 Diagnostics

<!-- toc -->
- [19.1 Message Format](#191-message-format)
  - [19.1.1 Inform 6 Diagnostics](#1911-inform-6-diagnostics)
- [19.2 Compile-Time Errors](#192-compile-time-errors)
- [19.3 Warnings](#193-warnings)
- [19.4 Runtime Failures](#194-runtime-failures)
<!-- /toc -->

## 19.1 Message Format

Every diagnostic the compiler produces about the source is one line of the form

```text
<file>:<line>:<column>: ERROR: <message>
<file>:<line>:<column>: warning: <message>
```

`<file>` is the path of the Beguile source file containing the construct, as the compiler resolved
it. When a diagnostic is anchored to a statement the column is `1`; otherwise it is the position at
which the lexer stopped. Diagnostics are written to standard error.

**Errors stop the build.** The first error ends compilation with exit status 1 (§16.7).
A program therefore never receives more than one error per build.

**Warnings continue.** A warning is advisory; the build proceeds. Warnings whose location lies inside
the system library (the `beguiLib` tree) are not shown: they concern the library's own internals and
an author cannot act on them.

**Location-free messages.** A few checks run after parsing, when no source position is current; their
messages carry no `file:line:column` prefix.

### 19.1.1 Inform 6 Diagnostics

Inform 6's own output is passed through the console with each diagnostic rewritten to Beguile
conventions:

```text
<file>.transpiled.inf:<line>:1: ERROR: <message>
  ↳ <file>.bgl:<line>:1
```

The first line names the transpiled file; the indented second line names the Beguile source line from
which that I6 line was generated, and is omitted when no Beguile line corresponds (for example, in a
compiler-generated header line). Inform 6 "Fatal error" and "Error" both appear as `ERROR`;
"Warning" appears as `warning`. Inform 6 warnings that map into the system library, or that name a
`_bgl`-prefixed symbol, are hidden, and Inform 6's closing "Compiled with N warnings" count is
adjusted to the number actually shown. Any Inform 6 error fails the build even when Inform 6 itself
exits successfully.

## 19.2 Compile-Time Errors

The categories below are those an author is most likely to meet. Messages are quoted as
representatives; `'x'` stands for the offending name.

**Global name collisions.** Every global declaration — variable, function, class, object, enum, verb —
must be unique. The message always cites the original declaration:

```text
'score' is already defined (originally declared at myLibrary.bgl:17)
'score' is already defined as a type (originally declared at myLibrary.bgl:17)
```

Collisions between Beguile names and symbols defined only in raw I6 (`#i6`, `#includeI6`) are not
visible to Beguile and surface as Inform 6 errors instead; declare such symbols `extern` (§15.4) to
bring them into the Beguile namespace. In precompiler mode the compiler does scan the surrounding I6
for declarations and reports overlaps as warnings (§19.3).

**Member collisions.** A member declared twice in one class or object, or added by `extend` when a
member of that name exists, is an error unless `replace` is used (§8.7):

```text
class 'Room': member 'light' is already defined
extend class 'Room': member 'describe' is already defined; use 'replace' to override
```

**Ambiguous names.** A bare identifier that could refer to more than one declaration, for instance
members of two `#using` imports, or an operator reference with more than one candidate:

```text
'val' is ambiguous: matches #using-imported member 'libA.val' and #using-imported member 'libB.val'. Qualify the use explicitly to disambiguate.
```

**Inheritance and emission order** (§18.7):

```text
class 'A': circular inheritance — 'B' transitively inherits from 'A'
class 'Room' uses `has light` but its bindings-file declaration `extern attribute light;` comes later in source (triggered by: …). Move the bindings file before the class or its first instance.
```

The second is a location-free message.

**Includes and directives** (§14):

```text
#include: file 'x' not found
Maximum include nesting depth (255) exceeded while including 'x'
'#define x' redefines a symbol that is already defined; use '#redef x' to intentionally redefine it
```

`#error "text"` raises an error whose message is the given text.

**Settings** (§17.1):

```text
Unknown beguilerSettings property 'x'
beguilerSettings property 'x' expects an int, got 'y'
beguilerSettings property 'serial' must be exactly 6 digits (e.g. "250328")
beguilerSettings property 'framePoolSize' must be at least 1
beguilerSettings includePaths entry 'x' does not resolve to an existing directory ('…'). Use @"..." to emit a literal path without filesystem validation.
#beguilerSettings.x: unknown or unsupported property
```

**Qualifier misuse.** Combining qualifiers that exclude one another, or applying one where it has no
effect, is an error; for example `superposed` on an `extern`, `emitter` or `alias` class or on
`extend class` (§3.2, §3.12).

**Post-emission check.** After the transpiled file is written, the compiler verifies that no
property-class member (§9.9) was emitted as a raw property access. A failure names the transpiled file
and line and suggests reading the value into a local first.

## 19.3 Warnings

**`#using`** (§10.4). The directive is ignored, with a warning, when its target is not a declared class
or object, when a named member does not exist, or when the member is not of an importable type:

```text
#using 'myLib': not a declared class or object; directive ignored
#using 'myLib.x': member not found; directive ignored
```

**Shadowing.** A local variable, parameter or loop variable that hides a global, a member of the
enclosing class or object, or a capturable name from an enclosing function:

```text
Local variable 'score' shadows global of the same name; the global is unreachable from this scope.
Parameter 'name' shadows a member of class 'Thing'.
```

**Object-member ambiguity.** Inside an object body, a bare identifier that resolves at file scope but
is also a property of the enclosing object (§3.8):

```text
Bare 'light' resolves as global variable 'light', but 'light' is also a property of the enclosing object. Beguile cannot tell which you mean. Write 'self.light' for the object's property, or '::light' to force the global and silence this warning.
```

**Method overriding without `replace`** (§8.7.3):

```text
class 'Door': method 'describe' shadows definition in base class 'Thing'; use 'replace' to suppress this warning
replace: no existing global function 'x' found; treating as new definition
```

**Verbs and grammar** (§13.2.3, §13.5.2). Pattern tokens after the trigger word in an `extern verb`'s grammar are
ignored; a `grammar -=` that matches nothing removes nothing:

```text
extern verb 'PutOn': pattern tokens after the trigger word(s) in `grammar = {...}` are ignored — …
grammar -= on 'Take': no grammar for word `'grab'` exists, so nothing was removed. …
```

**Precompiler mode.** A routine, global, constant, object or other symbol declared in the surrounding
I6 whose name is also a Beguile global:

```text
<file>: warning: I6 routine 'Score' collides with Beguile-declared global of the same name
```

**Other.** `hide` naming a member that is not inherited; re-typing a `typesealed` member (the retype
is ignored); `superposed` on a non-`static` method (no effect); `#warning "text"` reports the given
text.

## 19.4 Runtime Failures

The generated code and the BLR detect a small number of conditions at run time. Each prints a bracketed
message and halts the story with `quit`, except where stated.

| Condition | Message | Recovery |
| --- | --- | --- |
| Frame pool full (§18.10) | `[Beguile runtime error: frame pool exhausted]` | Raise `framePoolSize`. |
| Literal-list `for … in` scratch full (§5.9.1) | `[Beguile runtime error: for-in literal-list scratch exhausted]` | Raise `forInScratchSize`. |
| `<linq>` query step exceeds its buffer (§22.5) | `[Beguile runtime error: filter() output exceeds linqScratchSize. Increase via #beguilerSettings.linqScratchSize.]` (the method name varies) | Raise `linqScratchSize`. |
| `<linq>` query chains nested more than two deep (§22.5) | `[Beguile runtime error: LINQ chain nesting exceeds _BGL_LINQ_MAXDEPTH. …]` | Capture the inner result in a local first. |
| `setLength` beyond the word range (§12.3) | `[Beguile runtime error: setLength value exceeds signed range (max 32767 on Z, 2^31-1 on Glulx)]` | — |
| `<string>` pool full (§22.3) | `[ERROR: Unable to allocate a new instance.]` | Raise `bglStringPoolReserve`. |
| Pooled class full (§8.2.6) | none: `new` returns `nothing` | Test the result of `new`, or size the pool larger. |
| `bgl.world` result buffer full (§21.9) | none: the walk stops at 128 objects | Narrow the query. |
| `throw` with no enclosing `try` (§5.16) | the interpreter's own error; the story halts | — |
