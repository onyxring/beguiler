# Beguile Language Specification

---
# Chapter 1 — Introduction

## 1.1 What Is Beguile?

Beguile is a statically-typed, compiled language designed for authoring interactive fiction (IF). It provides a structured, C-like syntax that transpiles to Inform 6 (I6), a low-level language traditionally used to target the Z-Machine and Glulx virtual machines — the runtime platforms used by most modern IF interpreters.

Beguile is not a general-purpose language. Its type system and object model are shaped by the needs of interactive fiction: rooms, objects, attributes, verbs, and the grammar that connects player input to game logic.

## 1.2 Design Goals

- **Familiar syntax.** Beguile reads like C or a C-adjacent language. Developers comfortable with C, C++, C#, TypeScript, or similar languages should find the syntax intuitive.
- **Strong typing.** All variables, parameters, and return values carry a declared type. Type mismatches are caught at compile time, not at runtime.
- **Transparency.** The generated I6 is readable and maps closely to the Beguile source. Developers who know I6 can inspect or supplement the output.
- **Extensibility through emitters.** Performance-sensitive or platform-specific operations can be expressed as *emitters* — inline I6 fragments that are substituted at the call site. This gives library authors full control over the generated code without sacrificing type safety at the Beguile level.
- **IF-native constructs.** Verbs, grammar, attributes, and world objects are first-class concepts in the language, not simulated through generic data structures.

## 1.3 Compilation Model

A Beguile source file (`.bgl`) is processed in two passes by the Beguile compiler. The compiler:

1. **Pre-scans** the source (and all included files) to register type, object, function, and variable stubs. This pass resolves forward references so that declarations may appear in any order.
2. **Lexes and parses** the source in full, resolving types and checking compatibility against the stubs established in pass one.
3. **Emits** an Inform 6 source file (`.inf`) that is semantically equivalent to the Beguile source.
4. **Invokes the Inform 6 compiler** (`inform`) on the generated `.inf` file to produce the final story file (`.ulx` for Glulx or `.z8`, `.z5`, or `.z3` for Z-Machine).

The intermediate `.inf` file is retained alongside the story file. When compiled with `--debug`, a debug bundle (`<filename>.bgl.transpiled.inf.bgldbg`) and an Inform 6 debug database (`<filename>.bgl.transpiled.inf.dbg`) are also produced, supporting source-level debugging in the VS Code extension.

## 1.4 Relationship to Inform 6

Beguile is built on top of Inform 6, not a replacement for it. The generated I6 is valid, human-readable Inform 6 source. Several Beguile features exist specifically to bridge the two languages cleanly:

- `extern` declarations allow Beguile to use types, functions, attributes, and constants that are defined in I6 without re-implementing them.
- `#includeI6` passes raw I6 include directives through to the generated file.
- `#i6` directives output raw I6
- Emitter bodies contain literal I6 code, giving library authors precise control over the output.

Authors who need capabilities beyond what Beguile exposes can always drop down to I6 through these mechanisms.

## 1.5 A Note on Scope

This specification describes the Beguile language as recognized by the compiler. It does not describe the Inform 6 language itself or the behavior of the Z-Machine or Glulx virtual machines. Readers wanting background on those topics should consult the Inform 6 documentation and the relevant virtual machine specifications.

---

# Chapter 2 — Lexical Conventions

## 2.1 Character Set and Encoding

Beguile source files are plain text. The compiler processes them one character at a time; no particular encoding is mandated beyond what the host file system provides.

## 2.2 Comments

Beguile supports two comment forms, both identical in syntax to C:

```bgl
// This is a single-line comment. It extends to the end of the line.

/* This is a multi-line comment.
   It extends until the closing delimiter. */
```

Comments are discarded by the lexer and have no effect on compilation.

## 2.3 Case Sensitivity

Beguile is **case-insensitive** for all tokens except string literal contents. Keywords, type names, identifiers, and operator names are normalized to lowercase at lex time. The following are all equivalent:

```bgl
if(X == 1) print("yes");
IF(x == 1) Print("yes");
If(x == 1) PRINT("yes");
```

String literal contents are preserved exactly as written.

## 2.4 Identifiers

An identifier is a sequence of alphanumeric characters and underscores. The first character must be a letter or an underscore; it may not be a digit.

```
identifier ::= ( letter | '_' ) ( letter | digit | '_' )*
```

Examples of valid identifiers: `score`, `myVar`, `_internal`, `room1`, `velvetCloak`.

Identifiers that match a reserved keyword (§2.8) may not be used as variable or function names.

### 2.4.1 Reserved Prefix `_bgl`

Identifiers beginning with `_bgl` are reserved for compiler-generated symbols (loop counters, frame pool variables, lambda functions, scratch temporaries, etc.). User code should not declare names that begin with `_bgl` as collisions may produce undefined or erroneous I6 output.

## 2.5 Literals

### 2.5.1 Integer Literals

A sequence of decimal digits. Negative values are formed by applying the unary `-` operator to a positive literal.

```bgl
42
0
1000
```

### 2.5.2 String Literals

A string literal is enclosed in double quotes. As a design decision, Beguile preserves most of Inform's conventions for embedding extended characters in strings.  As such, `^` and `~` pass through to I6 as written, as do the patterns for specifying ZSCII characters. 

The following escape sequences are recognized:

| Escape | I6 output | Displayed character |
|--------|-----------|---------------------|
| `\n`   | `^`       | Newline |
| `\"`   | `~`       | Double-quote (`"`) |
| `\\`   | `@@92`    | Backslash (`\`) |
| `\^`   | `@@94`    | Caret (`^`) |
| `\~`   | `@@126`   | Tilde (`~`) |
| `\@`   | `@@64`    | At-sign (`@`) |


```bgl
"Hello, world!"
"She said, \"well done.\""
"Line one\nLine two"
"Price: 5\~ off!"
"Press \^ to continue."
```

### 2.5.2a Raw String Literals

A **raw string literal** is prefixed with `@` and disables all Beguile escape processing. Every character between the delimiters is passed through to the generated I6 string as-is, except that `~` and `^` are escaped to their I6 ZSCII equivalents (`@@126` and `@@94`) so they remain literal rather than being interpreted by I6 as quote/newline.

Raw strings are useful for Windows-style file paths or any string that contains many backslashes.

```bgl
string path = @"C:\Users\jim\documents\game.bgl";
string regex = @"\d+\.\d+";
```

The closing `"` terminates the raw string; there is no way to embed a literal `"` inside a raw string (use an escaped string `"\""` for that).

Raw strings may be used anywhere a regular string literal is valid — in assignments, as function arguments, and in `#beguilerSettings` property values.

### 2.5.2b Interpolated String Literals

An **interpolated string** is prefixed with `$` and may contain embedded Beguile expressions inside `{` `}` spans. It is only valid as the argument to `print()` or `log()`.

```bgl
print($"The {obj.name} weighs {obj.weight} stone.");
print($"Score: {score}  Turns: {turns}");
log($"Entering handler for {actor.name}");
```

Each segment is emitted as a separate I6 `print` statement. String segments are emitted as string literals; expression segments are emitted with an appropriate I6 type cast derived from the expression's resolved type.

**Escape sequences** inside interpolated strings follow the same rules as plain string literals. To include a literal `{` character, write `\{`:

```bgl
print($"Press \{enter} to continue.");   // prints: Press {enter} to continue.
```

**Constraints:**

- Only valid as the sole argument to `print()` or `log()` — cannot be stored in a variable or passed to other functions (Z-Machine strings are static; no runtime concatenation).
- `log($"...")` is compiled out entirely unless the `DEBUG` symbol is defined, matching the behavior of `log(string)`.
- Nested `{...}` inside an expression span (e.g. initializer lists) is not supported.

### 2.5.3 Character Literals

A single character enclosed in single quotes. Escape sequences follow the same rules as string literals.

```bgl
'a'
'\n'
'\\'
```

Character literals are typed as `charliteral` and participate in operator overload resolution independently of integer literals. 

### 2.5.4 Dictionary Word Literals

Dictionary word literals represent I6 dictionary entries — the tokens the parser uses to match player input.

- A **singular** dictionary word is written with a leading `.`: `.cloak`, `.hook`, `.velvet`
- A **plural** dictionary word is written with a leading `..`: `..cloaks`

```bgl
array<dictionaryWord> name = { .small, .brass, .hook };
```

Both forms resolve to type `dictionaryWord`. 

## 2.6 Operators and Symbols

The following two-character sequences are recognized as single operator tokens:

`-=`  `+=`  `?=`  `==`  `!=`  `<=`  `>=`  `&&`  `||`  `++`  `--`  `<<`  `>>`  `*=`  `/=`  `%=`  `&=`  `|=`  `^=`

Single-character operator and punctuation symbols include:

`=`  `+`  `-`  `*`  `/`  `%`  `<`  `>`  `!`  `&`  `|`  `^`  `(`  `)`  `{`  `}`  `[`  `]`  `;`  `,`  `.`  `#`  `?`  `:`

Two-character tokens take precedence: when the lexer encounters a character that could begin a two-character token, it peeks at the next character before deciding.

## 2.7 Preprocessor Directives

Tokens beginning with `#` immediately followed (with no whitespace) by an identifier are preprocessor directives:

| Directive | Purpose |
|-----------|---------|
| `#once` | Mark the current file so it is processed only once, even if included multiple times |
| `#include <name>` | Include a Beguile language extension from `beguilib/` |
| `#include "path"` | Include a Beguile source file by relative path |
| `#includeI6 "name"` | Emit an I6 `#include` directly into the generated output |
| `#define NAME` | Define a boolean compilation flag |
| `#define NAME value` | Define a symbol with a literal value |
| `#if expr` | Conditionally compile the following block |
| `#elif expr` | Alternative branch in a conditional block |
| `#else` | Final alternative branch |
| `#endif` | Close a conditional block |
| `#message "text"` | Print a message to the terminal during compilation |
| `#error "text"` | Emit a compile-time error with file and line information |
| `#exit` | Stop processing the current file as though end-of-file was reached |

Directives are described in full in Chapter 3.

## 2.8 Reserved Keywords

The following identifiers are reserved and may not be used as variable, function, type, or object names:

`if` `else` `for` `while` `switch` `case` `default` `break` `continue` `return`
`class` `extend` `extern` `alias` `object` `enum` `verb` `grammar` `attribute` `emitter`
`replace` `const` `array` `null` `self` `true` `false`
`int` `bool` `string` `void` `var`

---

# Chapter 3 — Program Structure

## 3.1 Source Files

A Beguile program consists of one or more `.bgl` source files. There is no designated entry point function; the Inform 6 runtime drives execution through lifecycle routines (`Initialise`, `begin_action`, etc.) that the author defines as ordinary Beguile functions.

Declarations at the outermost level of a file — types, classes, enums, variables, functions, objects, verbs, and grammar — constitute the *global scope*. Declarations may appear in any order within a file, but a name must be declared before it is referenced.

## 3.2 Include Directives

### 3.2.1 `#include <name>`

Includes a file from the Beguile language extensions (`beguilib/` directory). The `.bgl` extension is appended automatically. The compiler performs a case-insensitive search of the library directory, so `#include <String>` and `#include <string>` are equivalent regardless of the file system. If two files in the library directory differ only by case, the compiler will select one arbitrarily.

```bgl
#include <string>
```

### 3.2.2 `#include "path"`

Includes a Beguile source file by path relative to the current file. A file may be included more than once unless it contains a `#once` directive (see §3.2.4).

```bgl
#include "myLibrary.bgl"
```

### 3.2.3 `#includeI6 "name"`

Passes an include directive through directly to the generated I6 output. Use this to pull in I6 files that have no Beguile wrapper.

```bgl
#includeI6 "parser"
// emits: Include "parser";
```

### 3.2.4 `#once`

When placed at the top of a Beguile source file, `#once` marks the file so that any subsequent `#include` of the same file (by any path that resolves to the same absolute location) is silently ignored. Without `#once`, a file may be processed multiple times if included from different places.

```bgl
#once
// rest of myLibrary.bgl ...
```

`#once` is the recommended guard for any file intended to be included as a library. Standard library files in `beguilib/` all use it.

The compiler also enforces a maximum include nesting depth of 255. Exceeding this limit — for example through circular includes in files without `#once` — is a compile-time error.

## 3.3 Preprocessor Symbols

### 3.3.1 `#define`

Defines a named compilation symbol. The symbol may be a boolean flag or carry a literal value.

```bgl
#define DEBUG
#define MAX_SCORE 100
```

Symbols defined with `#define` can be tested with `#if`. A value-bearing symbol can be used as a literal wherever the language accepts a constant.

### 3.3.2 Pre-Defined Symbols

The compiler pre-defines the following symbols before any source file is parsed:

| Symbol | Value | Description |
|--------|-------|-------------|
| `beguiler` | `1000` | Full version encoded as `major*1000 + minor*10 + patch` (e.g. 1.0.0 = `1000`, 1.1.0 = `1010`). Safe on Z-machine (16-bit signed max 32,767 → major up to 32). |
| `beguilerMajor` | `1` | Major version component only. |
| `beguilerMinor` | `0` | Minor version component only. |
| `beguilerPatch` | `0` | Patch version component only. |

These are read-only — they cannot be overridden with `#define` — and are calculated automatically from the compiler's internal version constant. They behave identically to user-defined `#define` symbols in `#if` expressions:

```bgl
#if beguiler >= 1010
    // requires Beguile 1.1.0 or later
#endif

#if beguilerMajor >= 2
    // major version 2+
#endif
```

All four are also emitted into the generated I6 output as `Constant` declarations, making them available to runtime Beguile code:

```bgl
if(beguiler >= 1010) { ... }
```

### 3.3.3 Conditional Compilation

`#if`, `#elif`, `#else`, and `#endif` conditionally include or exclude blocks of source text. The expression following `#if` or `#elif` is evaluated at compile time against the currently defined symbols.

```bgl
#define DEBUG

#if DEBUG
    print("debug mode");
#endif
```

Nesting is supported. The compiler skips tokens in excluded branches without parsing them.

## 3.4 `#beguilerSettings`

The `#beguilerSettings` block configures the transpiler and the downstream Inform 6 invocation. Multiple `#beguilerSettings` blocks are allowed; properties follow **first-writer-wins** semantics (the first block to set a property wins; later blocks are ignored for that property), except for `i6IncludePath` and `bglIncludePath`, which are **additive** — every occurrence adds a directory to the respective search path.

The schema for this directive is declared as `extern class beguilerSettingsType` in `_beguileCore.bgl`. The parser validates property names and value types against this class, and the VS Code extension uses it to offer IntelliSense inside `#beguilerSettings` blocks.

Enum-typed properties accept either the bare value name or the optionally qualified `EnumType.Value` form — both are equivalent:

```bgl
#beguilerSettings {
    target         = Z5;           // bare form
    target         = eTarget.Z5;   // qualified form — equivalent
    informName     = "inform6";
    outputPath     = "output";
    i6IncludePath  = "/inform6/lib";
    bglIncludePath = "/myproject/bgl";
    errorFormat    = E2;
    release        = 3;
    rewritePaths   = true;
}
```

### Toolchain paths

These settings tell the transpiler where to find external tools. They are not written to the generated output.

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `informPath` | string | — | Full path to the Inform 6 compiler binary. Takes precedence over `informName`. |
| `informName` | string | `"inform"` | Filename of the Inform 6 binary (looked up adjacent to the `beguiler` binary). Use `"none"` to skip the I6 handoff entirely. CLI `-inform=` overrides this. |
| `beguiLibPath` | string | `"beguiLib"` | Path to the Beguile language extensions directory. Overrides the default binary-adjacent search. |
| `bglIncludePath` | string | — | Adds a directory to the search path for `#include "file"` resolution. May be specified multiple times (additive). |

### Compilation settings

These settings control the compilation target and output characteristics.

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `target` | `eTarget` | `Glulx` | Compilation target: `Glulx`, `Z3`, `Z5`, or `Z8`. |
| `outputPath` | string | `"output"` | Directory for the compiled story file. Relative paths are resolved from the source file's directory. CLI `-o` overrides this. |
| `i6IncludePath` | string | — | Adds a directory to the I6 compiler's library search path. May be specified multiple times (additive). |
| `release` | int | `0` | Sets the story release number. `0` means unset. |
| `errorFormat` | `eErrorFormat` | `E1` | Error reporting style passed to the I6 compiler. `E1` = Microsoft-style; `E2` = Macintosh-style. |

### Runtime settings

These settings affect the generated code.

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `framePoolSize` | int | `64` | Number of slots in the Z-machine local-variable overflow pool. Active only on Z3/Z5/Z8 targets. See §10.2.1. |
| `rewritePaths` | bool | `true` | When `true` (the default), path separators (`/` and `\`) in all file path settings and `#include`/`#includeI6` paths are rewritten to the OS path separator at parse time. Set to `false` to disable this normalization. |

### Game metadata settings

These settings carry game identity information. They feed blorb packaging directly (see §3.4.1) and can be surfaced as Beguile constants — see §3.4.2.

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `story` | string | `""` | Game title. |
| `author` | string | `""` | Game author. |
| `headline` | string | `""` | Game subtitle or tagline. |
| `release` | int | `0` | Story release number. `0` means unset. |

None of these are auto-emitted to I6 output. To expose them as I6 constants (e.g. for library integration) declare them explicitly in Beguile source — see §3.4.2.

### 3.4.1 Blorb packaging

When `generateBlorb = true`, the transpiler runs two additional steps:

1. **Before parsing** — scans `blorbAssetPath` for image and audio files, writes `_blorbAssets.bgl` containing a single `eAssets` enum. Add `#include "_blorbAssets.bgl"` to your source once after the first run generates it.
2. **After I6 succeeds** — assembles a `.zblorb` (Z-Machine) or `.gblorb` (Glulx) file containing the story file and all discovered assets.

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `generateBlorb` | bool | `false` | Master switch. Both steps are skipped unless this is `true`. |
| `blorbAssetPath` | string | `"assets"` | Directory to scan for assets, relative to the source file. |

Asset files are discovered non-recursively. PNG, JPG, and JPEG files become picture resources; AIFF/AIF files become sound resources. Member names in `eAssets` are derived from the filename: stem in camelCase with a capitalised extension suffix (`priest.png` → `priestPng`, `theme.aiff` → `themeAiff`). IDs are globally sequential starting at 1, pictures first then sounds, sorted alphabetically within each group.

```bgl
#beguilerSettings {
    generateBlorb  = true;
    blorbAssetPath = "assets";   // default — may be omitted
    story          = "Unwelcomed";
    author         = "Jim Fisher";
    release        = 1;
}
```

### 3.4.2 Referencing settings values in Beguile source

Any `#beguilerSettings` property can be read as a compile-time expression using the syntax `#beguilerSettings.propertyName`. The result is a string or integer literal substituted inline — string properties become `stringliteral` typed, integer properties become `intliteral` typed.

```bgl
const string story    = #beguilerSettings.story;
const string author   = #beguilerSettings.author;
const string headline = #beguilerSettings.headline;
const int    ver      = #beguilerSettings.release;
```

**Why use this instead of `const string story = "My Game";`?**

Both produce identical I6 output. The difference is where the value lives:

- `const string story = "My Game";` — the value is defined only in Beguile source. To use it in a `#beguilerSettings` blorb context (where the compiler reads it for metadata packaging), you would have to duplicate it.
- `const string story = #beguilerSettings.story;` — the value is defined once in `#beguilerSettings`, where it serves both the blorb packager and the compiled output. The Beguile constant is a *reference* to that single definition, not a second copy.

In short: if a value only needs to exist as an I6 constant and never participates in blorb packaging, declare it directly. If the value needs to be consumed by beguiler tooling (blorb, future toolchain features) *and* referenced in compiled code, define it in `#beguilerSettings` and surface it with `#beguilerSettings.propertyName`.

Supported properties: `story`, `author`, `headline`, `target`, `outputPath`, `blorbAssetPath`, `informName` (string); `release`, `framePoolSize` (int).

## 3.5 Diagnostic and Control Directives

### 3.5.1 `#message`

Prints a string literal to the terminal during compilation. The message is written to standard output and does not affect the generated output or compilation result.

```bgl
#message "Loading custom library..."
```

Useful for progress notes or tracing include chains during development.

### 3.5.2 `#error`

Halts compilation with a user-defined error message. The message is reported in the standard compiler error format, including file name and line number, and is indistinguishable in appearance from a parser error.

```bgl
#if !PLATFORM_DEFINED
    #error "You must define PLATFORM_DEFINED before including this file."
#endif
```

### 3.5.3 `#exit`

Stops processing the current file immediately, as though end-of-file had been reached. Any open directive nesting (`#if`/`##ifdef` blocks) accumulated in that file is discarded cleanly. Code nesting — open `{` blocks in parsed Beguile source — is not affected, so `#exit` should only be used at the top level of a file.

```bgl
#once
#if !FEATURE_ENABLED
    #exit
#endif
// ... feature implementation follows ...
```

`#exit` is primarily useful in library files that want to skip their body entirely when a required symbol is not defined.

---

# Chapter 4 — Types

## 4.1 Overview

Beguile is statically typed. Every variable, parameter, and return value has a declared type that is known at compile time. Types fall into four categories: primitive types, pseudo-types, user-defined types (classes and enums), and the `var` escape type.

## 4.2 Primitive Types

| Type | Description |
|------|-------------|
| `int` | Integer value. Maps to the native I6 integer. |
| `bool` | Boolean value. Backed by the `eBool` enum (`true`/`false`). |
| `char` | A single ZSCII character value. |
| `string` | A string object, backed by an I6 string utility class. |
| `object` | The base class for all world objects in the IF model. |
| `verb` | An I6 action constant. Used for comparing `action` against verb names. |
| `void` | No value. Valid only as a function return type. |

## 4.3 Literal Pseudo-Types

Literal pseudo-types are not declared by user code. They are inferred automatically by the compiler when a literal value appears in an expression, and they participate in operator overload resolution independently of their corresponding runtime types.

| Pseudo-type | Example | Notes |
|-------------|---------|-------|
| `intliteral` | `42` | Compatible with `int` via `operator =` on the `int` class |
| `stringliteral` | `"hello"`, `@"raw"` | Compatible with `string` via `operator =` on the `string` class; both regular and raw string literals share this pseudo-type |
| `charliteral` | `'a'` | Compatible with `char` via `operator =` on the `char` class |
| `dictionaryWord` | `.cloak`, `..cloaks` | Dictionary word; plural form (`..`) sets an internal flag that emits `'word/p'` in I6 |

All three literal pseudo-types (`intliteral`, `stringliteral`, `charliteral`) are compatible with their corresponding concrete types (`int`, `string`, `char`) exclusively through declared operators — specifically `operator =` on the target class. No built-in compatibility rule exists for any of them.

Literal pseudo-types are first-class types: they are declared as `extern class` in the core library and can have operators and methods defined against them via `extend class`. This means method calls are valid directly on literal values:

```bgl
"hello".print();       // calls print() on stringLiteral
42.someMethod();       // calls someMethod() on intLiteral
'x'.someMethod();      // calls someMethod() on charLiteral
```

The literal value is substituted for `$self` in the emitter body.

## 4.4 The `null` Keyword

`null` represents a null object reference. It maps to the I6 value `nothing` and has the resolved type `object`.

```bgl
object o = null;
if(o == null) print("nothing here");
```

`null` may be used anywhere an `object`-typed value is expected. It is not valid as an integer or string.

## 4.5 Enumerations

### `enum`

An `enum` declares a named set of integer constants. Values start at 1 and increment by 1.

```bgl
enum direction {
    north,      // 1
    south,      // 2
    east,       // 3
    west        // 4
}
```

Individual members may be assigned explicit integer values. Auto-numbering resumes by incrementing from the last assigned value.

```bgl
enum myPhase {
    setup  = 0,
    play   = 10,
    ending          // auto: 11
}
```

### `bnum`

A `bnum` is a bit-flag enumeration. Values start at 1 and double with each member (left-shift by 1), producing non-overlapping powers of two suitable for use as combinable bit flags.

```bgl
bnum itemFlag {
    portable,   // 1
    fragile,    // 2
    lit,        // 4
    locked      // 8
}
```

Individual members may be assigned an explicit starting value; auto-assignment resumes by doubling from the last assigned value.

### `extern enum` and `extern bnum`

`extern enum` (or `extern bnum`) declares an enumeration whose values are defined in I6, not by Beguile. The declaration registers the names for type-checking purposes only and produces no I6 output.

```bgl
extern enum eBool { true, false }
extern enum eErrorFormat { E1, E2 }
```

Enum and bnum values are referenced by name directly (not qualified by the enumeration type name): `true`, `false`, `north`, `portable`, etc. The one exception is `#beguilerSettings` blocks, where the optional `EnumType.Value` qualified form is also accepted — see §3.4.

## 4.6 The `var` Type

`var` is a universal escape type that bypasses static type checking. A `var` variable or parameter accepts any value.

```bgl
var x = 5;
```

In overload resolution, overloads with `var` parameters are treated as a fallback: they are only selected if no typed overload matches. This prevents a catch-all `var` overload from shadowing more specific ones.

```bgl
emitter void print(var val)        { print val; }          // fallback
emitter void print(string str)     { print (string)str; }  // preferred for string
emitter void print(stringLiteral s){ print (string)s; }    // preferred for string literals
```

## 4.7 Arrays

`array<T>` declares a typed word array. The element type `T` is used for type-checking operations on individual elements.

```bgl
array<int> scores[5];                    // sized, zero-initialized
array<int> primes = {2, 3, 5, 7, 11};   // initialized with values
```

At global scope, arrays emit I6 `Array` directives. As class members, they emit inline property value lists. Array elements are accessed using subscript syntax:

```bgl
int x = scores[2];       // read element 2
scores[0] = 99;          // write element 0
int n = scores.length(); // number of elements
```

The `.length()` method is also available as a direct call. Subscript read and write are backed by `operator[]` and `operator[]=` emitters on the `array` class — see §5.5.4.

---

# Chapter 5 — Classes

## 5.1 Defining a Class

A class groups related data and behavior. Members may be variable properties, methods, or emitters.

```bgl
class Point {
    int x = 0;
    int y = 0;

    void describe() {
        print(x);
        print(y);
    }
}
```

Member variable declarations follow the same type-name syntax as global variables. An optional default value may be provided with `=`.

A class declaration requires a name that is not yet registered as a type. After parsing, the class name becomes a valid type that can be used in subsequent declarations.

## 5.2 The Four Class Forms

Beguile has four class declaration forms. Each is introduced by a different keyword combination and carries distinct rules about what members are allowed and what I6 is generated.

| Form | Syntax | I6 class emitted | Members allowed |
|---|---|---|---|
| Normal class | `class Foo` | Yes | Variables (with or without values), emitter methods, regular methods |
| Extern class | `extern class Foo` | No | Typed variable declarations (initializers allowed but ignored), emitter methods |
| Emitter class | `emitter class Foo` | No | Emitter methods only (`emitter` keyword optional) |
| Alias class | `alias class Foo for Parent` | No | Typed variable declarations (no values), emitter methods |

Normal, extern, and emitter classes support optional inheritance via `: Parent` (see §5.6). Alias classes use `for Parent` instead (see §5.2.3). All forms support extension via `extend class` (see §5.7). Alias class requires exactly one parent; extern/alias classes do not allow non-emitter methods.

## 5.2.1 `extern class`

`extern class` declares a class that is implemented in I6. Beguile uses the declaration for type-checking and emitter dispatch only — no I6 class definition is generated.

```bgl
extern class object {
    parentProp parent;
    attributeList attributes;
    emitter void give(attribute attr){ give $self attr }
    emitter eBool has(attribute attr){ $self has attr }
}
```

Rules for `extern class` members:
- **Emitter methods** are allowed and require the `emitter` keyword.
- **Non-emitter methods** are not allowed — a compile-time error.
- **Variable declarations** (type and name only, no `=` initializer) are allowed and contribute to type inference on object instances.
- **Variable definitions** (with `=` initializer) are syntactically valid but the initializer value is ignored at runtime — no code is generated for it. The value exists solely as metadata and is invisible to normal Beguile programs. Certain compiler-internal classes (such as `beguilerSettingsType`) treat these initializers as default values for directive properties.

## 5.2.2 `emitter class`

`emitter class` declares a class that has no I6 backing at all — it exists purely in Beguile's type system to drive emitter dispatch. No I6 class definition is generated, and no I6 object or variable underlies instances of the type.

```bgl
emitter class celsius {
    fahrenheit operator(){ $self * 9 / 5 + 32 }
    celsius operator = (celsius v){ $self = v; }
}
```

Rules for `emitter class` members:
- All methods are implicitly emitters. The `emitter` keyword may be included for clarity but is not required.
- Variable properties (declared or defined) are not allowed — a compile-time error.

## 5.2.3 `alias class`

`alias class` declares a Beguile type that dissolves to an existing type for I6 emission. No I6 class declaration is generated. Instances of an alias class emit using the I6 name resolved by walking the alias chain to its root.

```bgl
alias class worldObject for object {
    string description;
}

worldObject foyer {
    description = "A grand hall.";   // type inferred: string (from worldObject)
    attributes = {light};            // type inferred: attributeList (from object)
}
```

The `for` keyword rather than `:` signals that the RHS is the I6 type being dissolved to — not a superclass being extended. No I6 class declaration is generated for the alias; instances emit using the I6 name of the root non-alias type.

The primary use of alias classes is **type inference**: typed member declarations on the alias class let instances omit the type specifier when setting those properties. The instance body still resolves members against both the alias class and the base `object` class.

**I6 name resolution** — the emitted class prefix for instances is determined by walking the parent chain until a non-alias type is found:

```bgl
alias class worldObject for object { ... }    // chain: worldObject → object → "Object"
alias class heavyObject for worldObject { }   // chain: heavyObject → worldObject → object → "Object"
```

Both `worldObject` and `heavyObject` instances emit using I6's `Object` keyword.

Rules for `alias class` members:
- **Emitter methods** are allowed and require the `emitter` keyword.
- **Non-emitter methods** are not allowed — a compile-time error.
- **Variable declarations** (type and name only, no `=` initializer) are allowed for type inference.
- **Variable definitions** (with `=`) are not allowed — a compile-time error.
- Exactly one parent class is required after `for`. Multiple parents are not allowed.

`alias` and `extern` are mutually exclusive. `alias` and `emitter` are mutually exclusive.

## 5.3 Member Variables

Member variables declare properties on the class.

```bgl
class Room {
    string short_name = "A dark room";
    int visits = 0;
    object parent;
}
```

Within a method body, bare member names are automatically resolved as `self.memberName`. Explicit `self.memberName` is also accepted and has the same effect.

## 5.4 Member Methods

Non-emitter methods are declared and defined inside the class body with a full statement block:

```bgl
class Counter {
    int count = 0;

    void incrementBy(int amount) {       // parameter
        int newCount = count + amount;   // local variable; count resolves to self.count
        count = newCount;
    }

    bool isAbove(int threshold) {        // parameter
        int c = count;                   // local variable
        return c > threshold;
    }

    int value() {
        return count;
    }
}
```


## 5.5 Emitters

An `emitter` member inlines raw I6 code at the call site instead of generating a function call. See Chapter 7 for a full description of emitter mechanics. The key forms relevant to classes are summarized here.

### 5.5.1 Operator Emitters

An operator emitter defines how a built-in operator is compiled when applied to the class. The operator name follows the `emitter` keyword and return type. Operator emitters may appear on any class — extern or otherwise.

```bgl
class Counter {
    int value = 0;
    emitter Counter operator ++ (){ $self.value++ }
    emitter bool operator == (Counter v){ $self.value == v.value }
}
```

The full list of operators that may have emitters:
`=` `+` `-` `*` `/` `%` `==` `!=` `<` `>` `<=` `>=` `?=` `&` `|` `^` `<<` `>>`
`+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `++` `--` `prefix++` `prefix--` `[]` `[]=`

### 5.5.2 `init` and `deinit` Emitters

`init` and `deinit` are zero-parameter lifecycle emitters called implicitly by the compiler:

- `init` fires immediately after a local variable of this type is declared.
- `deinit` fires before every `return` in the enclosing routine, and at the end of the routine if it falls through.

`init` and `deinit` may be declared on any class — extern or otherwise:

```bgl
class Token {
    emitter void init()   { $self = AllocToken(); }
    emitter void deinit() { FreeToken($self); }
}
```

`init` and `deinit` must not declare parameters; doing so is a compile-time error.

### 5.5.3 Conversion Operator

A zero-parameter emitter named `operator()` declares that this type is I6-compatible with the return type, enabling implicit type conversion (see §12). This may be declared on any class:

```bgl
class celsius {
    emitter fahrenheit operator(){ $self * 9 / 5 + 32 }
}
```

When no body is provided (empty braces), the value passes through unchanged. When a body is provided, it is substituted with `$self` replaced by the source expression.

### 5.5.4 Subscript Operators

`operator[]` and `operator[]=` allow a class to support subscript read and write syntax (`obj[n]` and `obj[n] = v`). Any class may define them — not just `array<T>`.

```bgl
extern class myBuf {
    emitter var  operator[]  (int i)        { $self-->i }
    emitter void operator[]= (int i, var v) { $self-->i = v }
}
```

With these defined:

```bgl
myBuf buf;
var x  = buf[3];    // calls operator[]  — emits: buf-->3
buf[3] = x + 1;    // calls operator[]= — emits: buf-->3 = x + 1
```

`operator[]` takes one parameter (the index) and returns the element type. `operator[]=` takes two parameters — the index first, then the value — and returns `void`. Both are looked up via the class hierarchy, so a subclass inherits subscript support from a parent.

The built-in `array<T>` class defines both operators, delegating to the `_orArray` helper object.

## 5.6 Class Inheritance

A class may inherit from one or more parent classes by listing them after a colon in the class declaration:

```bgl
class Animal {
    string name;
    void speak() { print("..."); }
}

class Dog : Animal {
    void speak() { print("Woof!"); }
}
```

Multiple inheritance is supported. Parent classes are listed comma-separated:

```bgl
class FlyingFish : Fish, Bird { }
```

Member lookup walks the inheritance hierarchy depth-first, left-to-right. The first matching member found wins. If two parent classes define the same member name, the first-listed parent's member is used.

To explicitly dispatch to a specific parent's version of a member, use the type-cast syntax (see §11.5):

```bgl
(Animal)myDog.speak();   // forces dispatch through Animal's speak, not Dog's
```

## 5.7 `extend class` and `replace`

`extend class` adds new members to any already-declared class. If augmenting an `extern class` type, only emitters may be added.  Classes defined in Beguile code may be extended will actual members as well.

```bgl
extend class Counter {
    emitter bool isZero(){ $self.value == 0 }
}

extend extern class string {
    emitter string operator + (stringLiteral v){ $self.append(v) }
    emitter string operator + (string v)       { $self.append(v) }
}
```

`extend class` requires the class name to already be registered as a type.

### Replacing Existing Members

The `replace` qualifier inside an `extend class` block replaces an existing member rather than adding a new one. It is required when the new member would duplicate an existing one; adding a duplicate without `replace` is a compile-time error.

```bgl
extend extern class string {
    replace emitter string operator = (stringLiteral v){ $self.set(v); }
}
```

Matching rules for `replace`:
- **Emitters**: matched by name and full parameter-type signature, because emitters support overloading.
- **Regular functions**: matched by name alone. Since I6 does not support routine overloading, only one regular function per name can exist; signature matching is unnecessary.
- **Variable properties**: matched by name alone.
- `replace` on a non-existent member issues a compiler warning and adds the member as new.
- `replace` outside an `extend class` block is a compile-time error.

`replace` also applies to global functions; see §8.4.

---

# Chapter 6 — Objects

## 6.1 Overview

An *object* is a named, globally visible instance that maps directly to an I6 object and exists as a concrete entity in the compiled story file. Unlike a class, which is a type definition, an object is a single named value.

Objects are commonly used for IF world-model entities — rooms, things, and actors — but their use is not restricted to the world model. Any class that inherits from `object` can be instantiated as a named object, including utility classes, data tables, and other non-interactive constructs that benefit from I6's property and method dispatch.

## 6.2 Declaring an Object

An object is declared at global scope with the `object` keyword followed by a name and a body enclosed in braces:

```bgl
object foyer {
    string short_name = "Foyer of the Opera House";
    string description = "A grand hall decorated in red and gold.";
    attributes = {light};
}
```

The object name becomes a globally visible identifier that can be used wherever an `object`-typed value is expected. An object declaration may optionally carry an `as i6name` clause to specify a different name in the emitted I6 — see §8.7.

## 6.3 Object Types

An object may be associated with a class in two equivalent ways:

**Using the class name as the type keyword** (preferred when the class is the primary type):

```bgl
class worldObject : object {
    string description;
}

worldObject foyer {
    description = "A grand hall.";
}
```

**Using `: ClassName1, ClassName2` after the object name** (preferred when the object/classes inherits from more than one base type):

```bgl
class Robot {
    int power;
}
class Animal {
    string short_name;
    void describe() { print(short_name); }
}

object dog : Animal, Robot{
    string short_name = "shaggy dog";
    int power=10;
}
```
TODO: double check that this multiple inheritance still works with type instances

If no class is specified (`object name { }`), the object is a plain I6 object with no class membership beyond the base `object` type.

## 6.4 Properties

Properties within an object body set initial values for the object's I6 properties. They are declared using a type-name-value form:

```bgl
object hook {
    string short_name = "small brass hook";
    int number = 0;
    object parent = cloakroom;
}
```

### 6.4.1 Type Inference from Class and Base Object

When an object is associated with a class (via either syntax in §6.3), typed member declarations on that class and on `extern class object` may be set without repeating the type. The compiler searches the object's declared class first, then walks the parent chain, and finally checks the base `object` class.

```bgl
alias class worldObject for object {
    string description;     // declared on worldObject
}

worldObject foyer {
    description = "A grand hall.";  // type inferred: string (from worldObject)
    attributes = {light};           // type inferred: attributeList (from object)
}
```

If a property name without a type specifier is not found on any class in the hierarchy, the compiler reports a compile-time error and suggests adding a type specifier.

### 6.4.2 The `parent` Property

Setting the `parent` property places the object inside another object at the start of the game. This is the standard I6 mechanism for establishing the initial world state.

```bgl
object cloak {
    object parent = selfobj;    // player is carrying it at game start
}

object hook {
    object parent = cloakroom;  // hook starts in the cloakroom
}
```

### 6.4.3 The `attributes` Property

The `attributes` property declares the object's initial I6 attributes using an initializer list of attribute names:

```bgl
object foyer {
    attributes = {light};
}

object cloak {
    attributes = {clothing, general, worn};
}
```

Attribute names used in `attributes` initializer lists must be declared before use. Two forms are available:

```bgl
attribute myNewAttr;           // declares a new attribute; emits 'Attribute myNewAttr;' to I6
extern attribute light;        // references an attribute already declared in the I6 library; no I6 declaration emitted
```

## 6.5 Array Properties

A property may be an array, declared with `array<T>`:

```bgl
object cloak {
    array<dictionaryWord> name = {.handsome, .dark, .velvet, .cloak};
}
```

Array properties emit inline property value lists in the I6 output. Element type-checking applies using the same rules as global arrays (§4.7).

## 6.6 Method Properties

An object may define method properties — functions that are properties of the object rather than standalone routines. These are used by the I6 library for lifecycle callbacks such as `before`, `after`, and `description`.

```bgl
object bar {
    bool before() {
        switch(action) {
            case Go:
                print("You can't go that way.");
                rtrue;
        }
    }
}
```

Method properties are emitted in I6 method-property format (`name[; locals; body]`) and are called by the library when the relevant event fires.

## 6.7 Complete Example

The following is a representative object from the Cloak of Darkness demonstration:

```bgl
object cloak {
    object parent = selfobj;
    string short_name = "velvet cloak";
    string description = "A handsome cloak, of velvet trimmed with satin.";
    array<dictionaryWord> name = {.handsome, .dark, .black, .velvet, .cloak};
    attributes = {clothing, general, worn};

    bool before() {
        switch(action) {
            case Drop, PutOn:
                if(location == cloakroom) {
                    bar.give(light);
                } else {
                    print("This isn't the best place to leave a cloak.");
                    rtrue;
                }
        }
    }

    bool after() {
        switch(action) {
            case Take: bar.ungive(light);
        }
        rfalse;
    }
}
```

---

# Chapter 7 — Emitters

## 7.1 What Is an Emitter?

An emitter is a function whose body contains raw I6 code that is *inlined* at every call site. Rather than generating an I6 function call, the compiler substitutes the emitter's body text directly into the output, replacing placeholder tokens with the actual argument expressions.

Emitters are the primary mechanism for giving library authors precise control over generated I6 while keeping the Beguile call site type-safe and readable. They may appear on any class — not just `extern class`.

```bgl
class Counter {
    int value = 0;
    emitter void increment(){ $self.value++ }
}

Counter c;
c.increment();
// emits: c.value++;   (body inlined, $self=c)
```

## 7.2 Emitter Syntax

An emitter declaration begins with the `emitter` keyword, followed by a return type, a name, a parameter list, and a body enclosed in braces:

```bgl
emitter returnType name(paramType paramName, ...) {
    raw i6 code here
}
```

Emitters may appear:
- As members of a class or extern class body
- At global scope, as top-level declarations

The body may contain any I6 text. It is not parsed for Beguile syntax — only the placeholder tokens `$self` and parameter names are recognized for substitution.

## 7.3 Substitution

When an emitter is called, the compiler performs textual substitution on the body before inlining it:

| Placeholder | Replaced with |
|-------------|---------------|
| `$self` | The receiver — the variable or expression on the left-hand side of a method call or operator |
| Each parameter name | The corresponding argument expression at the call site |

Substitution is performed as a simple text replacement across the entire body string. If a parameter name appears as a substring of another word, it will not be incorrectly substituted — the replacement targets exact token matches.

```bgl
extern class bool {
    emitter bool operator = (bool v){ $self = v; }
}

isBad = true;
// body: "$self = v;"  →  "isBad = true;"
```

## 7.4 Global Emitters

A global emitter is declared at file scope rather than inside a class. It behaves like a regular global function from the caller's perspective, but inlines its body at every call site.

```bgl
emitter void print(stringLiteral str){ print (string)str; }
emitter void print(string str)       { print (string)str; }
emitter void print(var val)          { print val; }
```

Global emitters participate in overload resolution by the same rules as regular global functions (see §8.3). `$self` is not meaningful for global emitters — the receiver concept does not apply.

## 7.5 Emitter Namespaces

An **emitter namespace** groups related emitter methods under a single name without creating a class, instances, or any I6 backing. It is declared with `emitter` followed directly by the namespace name and a body block — no `class` keyword:

```bgl
emitter style {
    void italics() { style underline; }
    void roman()   { style roman; }
}
```

Methods are called using dot syntax on the namespace name:

```bgl
style.italics();
print($"{style.italics()}Italic text{style.roman()}");
```

Rules:
- All members are implicitly emitters. The `emitter` keyword on individual members is optional.
- No I6 class definition or object is generated.
- The namespace name cannot be used as a variable type — it exists solely to group emitter methods.
- Inheritance and `extend` are not supported.

Emitter namespaces are distinguished from `emitter class` by the absence of the `class` keyword. An `emitter class` requires instances of a named type; an emitter namespace is a singleton with no associated type.

`style` is a built-in emitter namespace providing I6 style directives (`style.italics()`, `style.roman()`) without requiring an instance variable.

## 7.6 `print()` and `log()`

`print()` and `log()` are the two core output routines.

`print()` is overloaded to accept any value type and outputs it immediately:

```bgl
print("hello");       // string literal
print(score);         // int or var
print(c);             // char
```

`log()` has an identical signature to `print()` but is a debug-only output routine. Any call to `log()` is entirely absent from a non-debug build — it is not simply suppressed at runtime, it is not present in the compiled output at all. To enable `log()` output, define the `DEBUG` compiler value:

```bgl
#define DEBUG;
```

```bgl
log("entering handler");   // present only in debug builds
log(score);
```

When `DEBUG` is not defined, `log(...)` calls produce zero output — no code is emitted at the call site at all, not even a conditional check.

## 7.7 Operator Emitters

An operator emitter defines how a built-in operator is compiled when the left-hand operand is of the declaring class. The operator symbol replaces the function name. Operator emitters may be declared on any class:

```bgl
class Counter {
    int value = 0;
    emitter bool operator == (int v){ $self.value == v }
    emitter Counter operator = (Counter v){ $self.value = v.value }
}
```

When the compiler encounters `a == b` it looks for an `operator ==` emitter on type `a` whose parameter type matches the type of `b`. If found, the emitter body is inlined with `$self` replaced by `a` and the first parameter replaced by `b`.  With the above `==` operator defined...

```bgl
if(c==4) ... //assuming c is of type Counter
```
...will emit as...
```
if(c.value == 4) ...
```

### Supported Operators

The full set of operators that may be overloaded via emitters:

| Category | Operators |
|----------|-----------|
| Assignment | `=` |
| Arithmetic | `+` `-` `*` `/` `%` |
| Comparison | `==` `!=` `<` `>` `<=` `>=` `?=` |
| Bitwise / logical | `&` `\|` `^` `<<` `>>` `&&` `\|\|` |
| Compound assignment | `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` |
| Increment / decrement | `++` `--` `prefix++` `prefix--` |
| Subscript | `[]` `[]=` |

The `prefix++` and `prefix--` names distinguish prefix forms (`++n`) from postfix forms (`n++`). This allows a class to provide different behavior for each:

```bgl
extern class counter {
    emitter counter operator ++      (){ $self + 1 }   // postfix: n++
    emitter counter operator prefix++(){ $self + 1 }   // prefix:  ++n
}
```

### Compound Assignment Fallback

If no emitter is defined for a compound assignment operator (e.g., `+=`), the compiler expands it to its equivalent simple assignment:

```
n += 2    →    n = n + 2;
```

This is necessary because I6 does not natively support compound assignment operators.

### Increment / Decrement Fallback

If no emitter is defined for `++` or `--`, the statement is emitted directly as I6:

```
n++;    →    n++;
++n;    →    ++n;
```

## 7.8 Conversion Operator

A zero-parameter emitter named `operator()` declares that the class is I6-compatible with the return type. It enables implicit type conversion and allows the compiler to use an existing operator overload when the exact type match is absent.

```bgl
extern class int {
    emitter intLiteral operator(){}   // int passes through as intLiteral in I6
}
```

When the body is empty, the source value passes through unchanged. When a body is provided, it is substituted with `$self` replaced by the source expression:

```bgl
extern class celsius {
    emitter fahrenheit operator(){ $self * 9 / 5 + 32 }
}
```

See §12 for the full rules governing when conversion operators are applied.

## 7.9 Lifecycle Emitters: `init` and `deinit`

A class may declare `init` and `deinit` emitters to run code automatically when a local variable of that type comes into and out of scope.

```bgl
extend extern class string {
    emitter void init()   { $self = GetNewString(); }
    emitter void deinit() { FreeString($self); }
}
```

**`init`** fires immediately after the variable is declared, before any initializer assignment. This ensures the object exists before any operations are performed on it.

**`deinit`** fires:
- Before every explicit `return` statement in the enclosing routine
- At the implicit end of the routine if it falls through without returning

`init` and `deinit` must declare zero parameters. Declaring a parameter on either is a compile-time error.

```bgl
void doSomething() {
    string s;           // init fires: s = GetNewString();
    s = "hello";
    return;             // deinit fires first: FreeString(s);
}                       // deinit also fires here on fall-through
```

## 7.10 Emitters vs. Regular Functions

| | Regular function | Emitter |
|---|---|---|
| I6 output | Generates an I6 routine | Inlines body text at call site |
| Body language | Beguile statements | Raw I6 code |
| `$self` | Not applicable | Replaced with receiver expression |
| Recursion | Supported | Not meaningful (no routine exists) |
| In an `extern class` body | Must have no body (stub only) | Must have a body |
| Allowed at global scope | Yes | Yes |

---

# Chapter 8 — Global Declarations

## 8.1 Overview

Declarations at the outermost level of a source file — outside any function, class, or object body — are *global declarations*. They are visible throughout the entire compilation.

## 8.2 Global Variables

A global variable is declared with a type, a name, and an optional initializer:

```bgl
bool isGood = true;
bool isBad;
int score = 0;
string playerName;
```

An initializer may be a literal value or a simple expression; complex expressions that require runtime evaluation are not supported at global scope.

Every global variable name must be unique across the entire compilation. Declaring a second global with the same name as an existing global, class, object, or enum is a compile-time error that reports the original definition's location (see §13.6).

A local variable declared inside a routine may not share a name with any global variable. Doing so is a compile-time error (see §13.4).

## 8.3 Global Constants

The `const` modifier marks a global variable as read-only. The compiler prevents assignment to `const` variables.

```bgl
const string story    = "Cloak of Darkness";
const string headline = "A basic IF demonstration.";
const int MAX_SCORE   = 2;
```

Attempting to assign to a `const` variable is a compile-time error.

`extern const` declares a constant that is defined in I6, not in Beguile. It registers the name in the type system for type-checking purposes and produces no I6 output:

```bgl
extern const int STUCK_PE;
extern const int CANTSEE_PE;
```

## 8.4 Global Functions

A global function is declared with a return type, a name, a parameter list, and a body:

```bgl
void DeathMessage() {
    print("You have lost");
}

bool Initialise() {
    location = foyer;
    rtrue;
}
```

Each global function name must be unique; the compiler will produce duplicate-name errors if two regular functions share an identifier. See Chapter 9 for full details on function syntax, parameters, and return rules.

Overloading by parameter type is supported only for emitters (see §7.3), because emitters are inlined at the call site and never emitted as named I6 routines.

### `replace` for Global Functions

The `replace` qualifier replaces the body of an already-registered global function or emitter. The replaced entry keeps its position in the global list; emission order is unchanged.

For **emitters**, matching is by name and full parameter-type signature (since emitters can be overloaded). For **regular functions**, matching is by name alone (since only one routine per name can exist in I6).

```bgl
// initial definition
emitter void print(string str){ print (string)str; }

// string.bgl — better implementation, loaded later
replace emitter void print(string str){ str.print() }
```

A compile-time error is reported if no matching function exists to replace.

## 8.5 Extern Variables

`extern` variables are declared in I6 and used by Beguile for type-checking only. They produce no I6 output.

```bgl
extern int  score;      // mutable — game code may assign to this
extern object location; // the current room
```

Mutable `extern` variables may be read and assigned. `extern const` variables are read-only (§8.3).

## 8.6 Attributes

Two forms of attribute declaration are available:

```bgl
attribute myAttr;              // declares a new attribute; emits 'Attribute myAttr;' to I6
extern attribute light;        // references an existing I6 attribute; no declaration emitted
```

Use `extern attribute` for attributes already declared externally (e.g. via an IF library binding). Use plain `attribute` for new attributes defined in Beguile code.

Once declared, attributes are available as identifiers of type `attribute` and can be passed to the `give`, `ungive`, and `has` methods defined on `object` and `attributeList`.

## 8.7 I6 Name Aliasing — the `as` Clause

Any global instance declaration may carry an optional `as i6name` clause that specifies the name to emit in the generated I6. The Beguile name is used throughout `.bgl` source for type-checking and identifier resolution; the alias is substituted transparently at every emission site.

```bgl
extern grammarToken OBJ as noun;   // Beguile name: OBJ  →  I6 name: noun
object myHook as hook { ... }      // Beguile name: myHook  →  I6 name: hook
```

The `as` clause is valid on:
- `T Name as i6name;` — any typed instance declaration
- `object Name as i6name { ... }` — a named object definition (including instances of subclasses such as `room Name as place { }`)

It is **not** valid on type declarations (`extern class`, `alias class`, etc.) or function declarations.

This is available any time a chosen Beguile name must differ from the underlying I6 name — for example, when the I6 name conflicts with a Beguile keyword, or when a more descriptive Beguile name is preferred in source while the I6 name must remain unchanged for compatibility.

---

# Chapter 9 — Functions and Routines

## 9.1 Syntax

A function declaration specifies a return type, a name, a parenthesized parameter list, and a braced body:

```bgl
returnType name(paramType paramName, ...) {
    // body
}
```

The body is a sequence of statements (see Chapter 10). Functions declared at global scope emit I6 routines. Functions declared as class or object members emit I6 method properties.

## 9.2 Return Types

The return type precedes the function name. Every non-`void` function must have at least one `return` statement somewhere in its body; the absence of a return in a non-`void` function is a compile-time error.

| Return type | Meaning |
|-------------|---------|
| `void` | The function produces no value. A bare `return;` is permitted; `return expr;` is an error. |
| Any other type | The function must return a value of that type via `return expr;`. |

The I6 `rtrue` and `rfalse` keywords may also appear in non-`void` functions as return shortcuts (they are passed through to I6 unchanged). Using `rtrue` or `rfalse` in a `void` function is a compile-time error.

## 9.3 Parameters

Parameters are declared as a comma-separated list of type-name pairs inside the parentheses. An empty list is expressed as `()`.

```bgl
void foo(int x, string label) { ... }
bool check(object o, verb action) { ... }
void doNothing() { ... }
```

Parameter names are local to the function body. They are not visible outside the function.

### Default Parameter Values

A parameter may be given a default value with `=`. If the caller omits the argument, the default value is used.

```bgl
void greet(string name, int times = 1) {
    // 'times' defaults to 1 if not provided
}

greet("player");        // times = 1
greet("player", 3);     // times = 3
```

Required parameters (no default) must appear before optional parameters (with defaults). The compiler validates call-site arity against the required-to-total range. Too few or too many arguments is a compile-time error.

### Parameter Types in `extern class`

For non-emitter function declarations inside an `extern class`, parameter names are optional — only the type is required:

```bgl
extern class string {
    bool contains(string);   // parameter name omitted
}
```

## 9.4 Overload Resolution

Multiple functions with the same name but different parameter-type signatures may coexist. Resolution proceeds as described in §8.4: exact match, then conversion match, then `var` fallback.

Arity is checked before type compatibility. A call with the wrong number of arguments will not match any overload regardless of types.

## 9.5 The `self` Keyword

Inside a class method or object method property, `self` refers to the receiver — the object on which the method was called. Bare member names are automatically prefixed with `self.` in the emitted I6; explicit `self.memberName` is also valid.

```bgl
class Counter {
    int count = 0;
    void increment() {
        count = count + 1;       // emits: self.count = self.count + 1
        self.count = self.count + 1;  // identical
    }
}
```

`self` is not valid outside a method body.

---

# Chapter 10 — Statements

## 10.1 Overview

The body of a function, method, or emitter is a sequence of statements. Statements are executed in order. Each statement is terminated by a semicolon unless it ends with a closing brace.

## 10.2 Variable Declaration

A local variable is declared with a type, a name, and an optional initializer:

```bgl
int count = 0;
string label;
bool found = false;
```

The variable is visible from the point of declaration to the end of the enclosing block. A local variable name may not shadow a global variable of the same name — doing so is a compile-time error (§13.4).

If the variable's type defines an `init` emitter, it fires immediately after the declaration, before any initializer assignment (§7.7).

### 10.2.1 Z-Machine Local Variable Limit

The Z-machine architecture limits a routine to **15 local variable slots** total (parameters + declared locals combined). This is a hard constraint of the virtual machine.

Beguile handles overflow automatically. At emit time, if a routine's total local count exceeds 14 (one slot is reserved for the frame pointer `_bglFrm`), the compiler spills the excess locals into a pre-allocated global frame pool:

```i6
Array _bglFramePool --> 64;   ! 64 overflow slots (128 bytes)
Global _bglFrameTop = 0;
```

Spilled variables are rewritten to `_bglFrm-->N` references. The last-declared body locals spill first; since compiler-generated variables (such as for-in counters `_bglfiN`) are declared last, they spill before user-declared variables.

The frame pool uses a simple stack with `_bglFrameAlloc(N)` / `_bglFrameFree(N)` routines, making it reentrant and recursion-safe.

- The pool is emitted only when at least one function actually needs it.
- The pool holds up to 64 overflow slots. Exceeding this in concurrent active calls causes silent overflow; the pool size can be raised in a future compiler version.
- Glulx is unaffected — it has no meaningful local variable limit.

## 10.3 Assignment

```bgl
x = expr;
```

The left-hand side must be a previously declared variable or a dotted member path. Type compatibility is checked:

1. If the LHS type defines `emitter T operator = (RHS_type v)`, that emitter is inlined.
2. Else if the RHS type defines `emitter LHS_type operator()`, the conversion emitter is applied.
3. Else if the types are identical or I6-compatible via the conversion operator, a plain assignment is emitted.
4. Otherwise a compile-time type mismatch error is reported.

## 10.4 Compound Assignment

The compound assignment operators modify a variable in-place:

```bgl
n += 2;
score -= penalty;
x *= factor;
```

Supported operators: `+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=`

If the LHS type defines an emitter for the operator, it is used. Otherwise the expression is expanded to its equivalent simple assignment, since I6 does not natively support compound operators:

```
n += 2    →    n = n + 2;
```

## 10.5 Increment and Decrement

The `++` and `--` operators are valid as standalone statements in both postfix and prefix forms:

```bgl
n++;    // postfix increment
n--;    // postfix decrement
++n;    // prefix increment
--n;    // prefix decrement
```

If the variable's type defines the corresponding emitter (`operator ++`, `operator --`, `operator prefix++`, `operator prefix--`), it is used. Otherwise the statement is passed through to I6 directly.

## 10.6 Function and Method Calls

A standalone function or method call is a statement when followed by a semicolon:

```bgl
print("hello");
bar.give(light);
cloak.ungive(general);
```

Argument types and arity are validated at compile time. Method calls are resolved against the declared type of the receiver.

### Method Chaining

Method calls may be chained. Each call in the chain is resolved against the return type of the previous call:

```bgl
str.trim().print();
c.toLower().print();
```

## 10.7 `if` / `else`

```bgl
if(condition) statement;

if(condition) {
    statements;
} else {
    statements;
}
```

Both single-statement and braced-block bodies are supported. The `else` branch is optional. `else if` chains are expressed by nesting `if` inside `else`:

```bgl
if(x == 1) {
    print("one");
} else if(x == 2) {
    print("two");
} else {
    print("other");
}
```

## 10.8 `for` Loop

C-style `for` loop with an initializer, condition, and increment expression:

```bgl
for(int i = 0; i < 10; i++) {
    print(i);
}
```

All three parts are required. The initializer may declare a new variable (scoped to the loop) or assign to an existing one.  The initializer may not declare a type to an existing variable, even if it was declared in a the initializer of a previous `for` loop.

### 10.8.1 `for-in` Loop

A `for-in` loop iterates over every element of an array without managing an index variable manually:

```bgl
// Form 1: pre-declared variable
array<int> primes = {2, 3, 5, 7, 11};
int p;
for(p in primes) {
    print(p);
}

// Form 2: inline type declaration
for(int p in primes) {
    print(p);
}
```

Both forms require the iteration variable's type to match the array's element type. The only exception is `var`, which is accepted for any element type.

```bgl
bool p;
for(p in primes) { }  // compile-time error: bool ≠ int
```

### Call-expression form

The right-hand side of `in` may also be a call expression that returns an `array`. In this case the compiler assigns the result to a synthesized temporary before the loop runs:

```bgl
for(object o in bglWorld.getAll()) {
    print(o);
}
```

When the RHS is a call expression, the element type defaults to `var` (the return type of the called method governs which array is used, but individual elements are untyped at the Beguile level). A type-annotated iteration variable (`object o`) is accepted without an error as long as it is compatible with `var`.

The array name must be either a declared array variable or a call expression that returns an array. Any other expression form is a compile-time error.

**Nesting** is supported; each loop gets a unique internal index variable (`_bglfi0`, `_bglfi1`, …):

```bgl
for(int row in rows) {
    for(int col in cols) {
        // _bglfi0 and _bglfi1 are distinct
    }
}
```

## 10.9 `while` Loop

```bgl
while(condition) {
    statements;
}
```

The condition is evaluated before each iteration. If false on first entry, the body is not executed.

## 10.10 `switch` / `case`

```bgl
switch(expr) {
    case value1:
        statements;
    case value2, value3:
        statements;
    default:
        statements;
}
```

Multiple values may share a case by listing them comma-separated. I6 cases do not fall through by default; the `break` keyword is accepted inside `switch` but is not required and has no effect (I6 handles the non-fall-through behavior natively).

Case values are type-checked against the switch condition type using the full compatibility rules of §12.2. In particular, `intliteral` case values are compatible with an `int` condition via the `operator =` declared on the `int` class, and enum case values are matched by exact type. When the switch expression is of type `verb`, case values are emitted as `##VerbName` action constants automatically.

## 10.11 `break` and `continue`

`break` exits the innermost enclosing loop or `switch` immediately.

`continue` skips the remainder of the current loop iteration and re-evaluates the loop condition.

Both require a terminating semicolon and are valid inside `for`, `while`, and `switch` bodies.

```bgl
while(n < 10) {
    if(n == 5) break;
    if(n == 3) continue;
    n++;
}
```

## 10.12 `return`

Exits the current function and optionally returns a value:

```bgl
return;                    // valid in void functions
return someValue;          // identifier
return obj.method(arg);    // call expression
return a + b * c;          // arithmetic expression
```

The return value may be any expression — identifier, method call, arithmetic, or ternary. The expression is type-checked against the function's declared return type.

Returning a value from a `void` function is a compile-time error. A non-`void` function must have at least one reachable `return` statement; its absence is a compile-time error.

If the function's local variables have `deinit` emitters, they fire before the `return` is emitted.

---

# Chapter 11 — Expressions

## 11.1 Overview

Expressions appear as conditions in `if` and loop statements, as initializers in variable declarations, as right-hand sides of assignments, and as arguments to function calls. The compiler assigns each expression a *resolved type*, which drives operator overload resolution, type checking, and emitter dispatch.

## 11.2 Operands

An expression is built from one or more operands joined by operators. Operands are:

- **Integer literals** — resolved type `intLiteral`; may have methods called directly: `42.someMethod()`
- **String literals** — resolved type `stringLiteral`; may have methods called directly: `"hello".print()`
- **Character literals** — resolved type `charLiteral`; may have methods called directly: `'x'.someMethod()`
- **Dictionary word literals** — resolved type `dictionaryWord` (both singular `.word` and plural `..word` forms)
- **Identifiers** — resolved by scope lookup (see Chapter 13); type is the declared type of the variable, parameter, or enum value
- **`null`** — resolved type `object`
- **`self`** — resolved type of the enclosing class
- **Method call results** — resolved type is the method's return type
- **Subscript expressions** — `arr[i]` resolves as the element type of the array
- **Parenthesized sub-expressions** — `(expr)` has the same type as `expr`

## 11.3 Binary Operators

Binary operators join two operands. Resolution proceeds as follows:

1. The compiler looks for an emitter on the LHS type matching the operator and the RHS type.
2. If not found, it looks for an `operator()` conversion on the RHS type that maps to a type for which the LHS does have an operator emitter.
3. If still not found, the operator is emitted verbatim as I6.

Operators are resolved left-to-right for chained expressions:

```bgl
str + " followed" + apnd
// resolves as: (str + " followed") + apnd
// each + resolves in turn, using the return type of the previous call as the new LHS type
```

## 11.4 Ternary Operator

The `?:` operator selects between two values based on a condition:

```bgl
condition ? trueExpr : falseExpr
```

The ternary operator may appear as an argument to a function call or on the right-hand side of an assignment. The resolved type is taken from the true branch.

```bgl
print(x > 0 ? "positive" : "non-positive");
```

**Constraints:**
- Both branches should be the same type for predictable results.
- Only one ternary may appear per statement.
- Ternaries may not be nested inside another ternary's condition.

## 11.5 Type Cast

A type cast overrides the resolved type of an expression at compile time, redirecting operator and method dispatch through the specified type's members:

```bgl
(TypeName)expr
```

No I6 cast is emitted — this is a compile-time-only annotation. It is useful for:

**Explicit parent dispatch** — when a subclass overrides a method and you want to call the parent's version:

```bgl
(Animal)myDog.speak();   // dispatches speak() through Animal, not Dog
```

**Disambiguation in expressions** — forcing operator resolution through a specific type when the inferred type would resolve differently.

The cast applies to the immediately following identifier or method call. It does not propagate through a chain.

## 11.6 Anonymous Functions

An anonymous function (also called a *lambda*) is a function literal that can be assigned to a variable, passed as an argument, or stored as a property. It has no name of its own; the compiler lifts it to a named global routine automatically.

### 11.6.1 The `func<>` Type

A variable that holds a function reference is declared with the `func<>` generic type:

```bgl
func<ReturnType, ParamType1, ParamType2, ...> varName;
```

The first type argument is the return type; the remaining arguments (if any) are the parameter types, in order. A function with no parameters uses `func<ReturnType>`. A `void`-returning function uses `func<void, ...>`.

```bgl
func<void, int>    printer;   // takes one int, returns nothing
func<int, int>     doubler;   // takes one int, returns int
func<void>         callback;  // takes nothing, returns nothing
```

`func<>` variables are I6 globals (or function-local slots). Calling one uses the same syntax as a normal call; the compiler emits an indirect I6 call through the stored address.

### 11.6.2 Lambda Literal Syntax

A lambda literal creates an anonymous function inline:

```bgl
(ParamType paramName, ...) => { body }
```

For a zero-parameter lambda:

```bgl
() => { body }
```

The return type is inferred: if the body contains a `return expr;` statement the return type is taken from the expression's type; if there is no `return`, the return type is `void`.

```bgl
func<void, int> printer = (int n) => { print(n); };
func<int, int>  doubler  = (int n) => { return n * 2; };
func<void>      greet    = () => { print("hello"); };
```

### 11.6.3 Passing Lambdas as Arguments

Lambdas are most useful as callbacks passed to other functions. A function that accepts a callback declares its parameter as `func<...>`:

```bgl
void applyToAll(array<int> arr, func<void, int> fn) {
    for(int item in arr) {
        fn(item);
    }
}

array<int> scores = {10, 20, 30};

void main() {
    applyToAll(scores, (int n) => { print(n); });
}
```

An inline lambda in an argument position is lifted and passed by address, just like a named lambda variable.

### 11.6.4 Constraints

- **No captures.** A lambda body may not reference local variables from the enclosing scope. Globals and parameters of the lambda itself are accessible; locals of the enclosing function are not.
- **No immediate invocation.** The syntax `((int n) => { print(n); })(42)` is not supported. Assign to a variable or pass as an argument first.

---

# Chapter 12 — Type Compatibility and Conversion

## 12.1 Overview

Beguile checks type compatibility at every assignment, variable declaration initializer, and function call argument. The compiler applies implicit conversion automatically when an exact type match is not present, using a defined priority order.

## 12.2 Compatibility Rules

A value of type `A` is compatible with a target of type `B` if any of the following hold, checked in order:

1. **Exact match** — `A == B`.
2. **Object subtyping** — any class instance (normal class, extern class, or alias class) is compatible with the base `object` type. This reflects that all I6 objects are ultimately `Object`s.
3. **Class hierarchy** — if `A` is a class that inherits from (or is an alias of) `B`, `A` is compatible with `B`. The check walks the full parent chain, including alias chains.
4. **Assignment operator** — type `B` defines `emitter B operator = (A v)` (the target type accepts the source type).
5. **Conversion operator** — type `A` defines `emitter B operator()` (the source type converts itself to the target type).
6. **I6 compatibility** — type `A` defines `emitter C operator(){}` where `C` is I6-compatible with `B` (an empty-body conversion, meaning both types share the same underlying I6 representation).

If none apply, the assignment or call is a compile-time type mismatch error.

## 12.3 Priority at Assignment

When assigning `lhs = rhs`:

1. If LHS type has `emitter LHS_type operator = (RHS_type v)`, inline that emitter.
2. Else if RHS type has `emitter LHS_type operator()` with a body, apply the conversion and emit a plain assignment.
3. Else if the types are I6-compatible (both map to the same underlying I6 type via empty `operator()`), emit a plain assignment.
4. Otherwise — compile-time error.

This priority is consistent across assignment statements (`x = expr;`), variable declaration initializers (`T x = expr;`), and function call arguments.

## 12.4 Priority at Function Call Arguments

For each argument, the same priority applies: exact match first, then `operator =`, then `operator()` conversion, then I6 compatibility, then `var` fallback. If multiple overloads exist, exact matches win over conversion matches, which win over `var` fallbacks (see §8.4).

## 12.5 Example

```bgl
extern class celsius {
    emitter fahrenheit operator(){ $self * 9 / 5 + 32 }
}

fahrenheit f;
celsius temp;

f = temp;          // RHS celsius has operator() → fahrenheit; applied automatically
print(temp);       // if print(fahrenheit) exists, celsius converts; else var fallback
```

---

# Chapter 13 — Scope and Identifier Resolution

## 13.1 Overview

When the compiler encounters an identifier in an expression or statement, it resolves it by searching through a sequence of scopes in priority order. The first match found wins; later scopes are not searched.

## 13.2 Resolution Tiers

Identifiers are resolved through four tiers, searched in order:

**Tier 1 — Local scope**
1. Function parameters of the enclosing function
2. Local variables declared in the current block
3. Local variables declared in enclosing blocks of the same function

**Tier 2 — Class/object scope**
4. Members of the current class or object (when inside a method body). Resolved as `self.memberName` in the emitted I6.

**Tier 3 — Global scope**
5. Enum values (all declared enum members are in a flat global namespace)
6. Global variables, constants, and extern declarations

**Tier 4 — Verb names**
7. Declared verb names. Resolved as the plain I6 verb name. The `##` prefix is applied only by emitter bodies (e.g., `##v` in `operator ==`) and by `switch`/`case` emission for `verb`-typed case values.

If no tier matches, the identifier is undeclared — a compile-time error.

## 13.3 The `self` Keyword

`self` explicitly refers to the receiver object within a method body. It may be used anywhere a member name appears:

```bgl
class Counter {
    int count = 0;
    void reset() {
        self.count = 0;   // explicit self
        count = 0;        // identical — bare member names resolve via self
    }
}
```

`self` is only valid inside a class or object method body.

## 13.4 Global Variable Shadowing — Prohibited

A local variable declaration may not use the same name as any global variable. If a local would shadow a global, the compiler reports a compile-time error and halts:

```bgl
int score = 0;     // global

void foo() {
    int score = 5; // ERROR: local 'score' shadows global variable of the same name
}
```

**Rationale:** I6 provides no mechanism to access a global that has been shadowed by a local. Rather than silently producing unreachable globals, Beguile disallows the condition entirely. Rename the local to resolve the conflict.

## 13.5 Verb Names vs. Variables

When an identifier matches both a declared variable and a declared verb name, the variable always wins (Tier 1–3 before Tier 4). The `##VerbName` form is never emitted simply from naming an identifier — it requires the `verb` type's `operator ==` emitter or a `switch` case value of type `verb`.

## 13.6 Global Name Collision Detection

The compiler detects collisions between global declarations and reports them as compile-time errors. The error message always includes the file and line of the *original* declaration so both sites of the conflict are visible:

```
myGame.bgl:42:5: error: 'score' is already defined (originally declared at myLibrary.bgl:17)
```

Collisions detected at global scope:

| Conflict | Error |
|----------|-------|
| Global variable vs. global variable | `'name' is already defined (originally declared at file:line)` |
| Global variable vs. class/object/enum type | `'name' is already defined as a type (originally declared at file:line)` |
| Class vs. class | detected; error cites original class declaration |
| Object vs. object or class | detected; error cites original declaration |
| Enum vs. enum or other type | detected; error cites original declaration |

**Beguile vs. raw I6 collisions** (symbols declared via `#includeI6` or `#i6` blocks) are invisible to the Beguile parser and will surface as I6 compiler errors instead. Using `extern` declarations in a bridging file is the recommended way to make I6 symbols known to the Beguile type system and prevent silent conflicts.

**Naming conventions to avoid collisions:**
- Compiler-generated symbols use the `_bgl` prefix (reserved — see §2.4.1)
- Standard library internal symbols use the `or` prefix (e.g., `orString`, `orBufferWrapper`)
- User code should use application-specific prefixes for any global variables or types intended to coexist with included library files

---

# Chapter 14 — Verbs and Grammar

## 14.1 Overview

Verbs and grammar declarations are the mechanism for adding new player commands to an IF game. A `verb` declaration defines an action and its handler. A `grammar` declaration — either inline inside the verb block or as a separate top-level `grammar` block — adds input patterns that the Inform parser recognizes as triggering that action.

## 14.2 Verb Declarations

A `verb` declaration creates a named Beguile object that is an instance of the built-in `verb` class. The object's action body is provided in a `perform()` block:

```bgl
verb Examine {
    perform() {
        print("You examine it closely.");
    }
}
```

Grammar patterns may be declared inline inside the verb block using a `grammar { }` sub-block:

```bgl
verb Examine {
    grammar {
        {.examine, NOUN},
        {.x, NOUN},
        {.look, .at, NOUN},
    }
    perform() {
        print("You examine it closely.");
    }
}
```

Verb action bodies have their own variable scope — locals declared inside `perform()` are confined to that action and do not conflict with other verbs or global routines.

### External Verbs

A verb with no Beguile handler (one that reuses an I6 library verb's behavior) is declared as `extern verb`:

```bgl
extern verb Take;
extern verb Drop;
extern verb Go;
```

`extern verb` declarations register the name in Beguile's type system for use in `switch(action)` comparisons and grammar lines without generating any I6 output.

## 14.3 Action Comparisons

The `action` library variable has type `verb`. Comparing it against a verb name uses the `verb` class's `operator ==` emitter, which emits the `##VerbName` prefix:

```bgl
if(action == Take) { ... }
// emits: if (action == ##Take) { ... }
```

In a `switch` statement on `action`, case values of type `verb` are automatically prefixed with `##`:

```bgl
switch(action) {
    case Take: print("Taken.");
    case Drop: print("Dropped.");
}
// emits:
//   switch(action) { ##Take: ... ##Drop: ... }
```

## 14.4 Grammar Declarations

Grammar patterns may be defined inline in the verb block (see §14.2) or in a separate top-level `grammar` block. The standalone form is useful when a verb is declared elsewhere (e.g., as `extern verb`) or when grammar lines are added incrementally:

```bgl
grammar PutOn {
    {.hang, HELD, .on, NOUN},
    {.put, HELD, .on, NOUN},
}
```

Each line inside the braces is a comma-separated grammar pattern enclosed in `{}`.

### Grammar Pattern Tokens

Each element of a pattern is one of:

| Token form | Meaning |
|------------|---------|
| `.word` | Dictionary word literal — matches the player typing that exact word |
| `..words` | Plural dictionary word |
| `.word1 \| .word2` | Alternative dictionary words — matches any one of them; may be wrapped in optional parentheses |
| `OBJ` | Matches any in-scope object (emits as the I6 `noun` grammar token via `as noun`) |
| `HELD` | Matches a held object |
| `CREATURE` | Matches a creature or actor |
| `TOPIC` | Matches a topic phrase |
| `MULTI` | Matches one or more in-scope objects |
| `MULTIHELD` | Matches one or more held objects |
| `NUMBER` | Matches a number typed by the player (range-checked) |
| `ANYNUMBER` | Matches any number (no range check) |
| `SPECIAL` | Matches a number or dictionary word |
| `attributeName` | Matches objects that have that attribute |
| `variableName` | Any declared global variable — emits its I6 name (respecting `as` aliases) |
| `RoutineName` | Calls routine as a general token filter (must be a declared global function) |
| `OBJ(Routine)` | Filters noun matches through Routine |
| `SCOPE(Routine)` | Sets scope via Routine |

`OBJ` is used instead of the I6 name `noun` because `noun` is also declared as a runtime variable in Beguile (`extern object noun`). Using `OBJ` avoids this collision — `OBJ` is declared as `extern grammarToken OBJ as noun`, so it carries the Beguile-facing name `OBJ` and emits as the I6 grammar token `noun`. Grammar tokens (`OBJ`, `HELD`, `CREATURE`, etc.) are written in ALL_CAPS by convention to distinguish them visually from dictionary word literals (`.word`). Since Beguile identifiers are case-insensitive, this is a matter of style, not enforced by the compiler.

The compiler validates that bare identifiers in grammar patterns are declared as `grammarToken`, `attribute`, or a global function. Plain object or variable declarations (such as the runtime variable `noun`) are not valid in this position — use the corresponding `grammarToken` alias instead (e.g. `OBJ`). The identifier emits as its I6 name, respecting any `as` alias on the declaration. Unrecognized or wrong-typed names are a compile error.

## 14.5 The `_bglGlobalDeclaration` Mechanism

The wrapper routine emitted for each verb (`examinesub` above) is generated automatically by a `_bglGlobalDeclaration` emitter on the `verb` type. This mechanism allows a class definition to inject additional top-level declarations for every object instance of that class.

Within a `_bglGlobalDeclaration` emitter body, two substitution variables are available:

| Variable | Expands to |
|----------|-----------|
| `$self` | The object's I6 name (e.g., `examine`) |
| `$selfsub` | The object's name with `sub` appended (e.g., `examinesub`) |

The built-in `verb` class uses this to generate the wrapper routine:

```bgl
extern class verb {
    emitter void _bglGlobalDeclaration() {
        [$selfsub;
            $self.perform();
        ];
    }
}
```

This pattern is available to any user-defined class — not just `verb` — when additional top-level I6 declarations must accompany each instance.

---

# Chapter 15 — Interoperability with Inform 6

## 15.1 Overview

Beguile is built on top of I6, and several mechanisms allow Beguile code to use I6 constructs directly, declare I6-defined entities for type-checking, and pass I6 directives through to the generated output unchanged.

## 15.2 `extern` Declarations

`extern` declares that a type, variable, function, attribute, or enum member is defined in I6. Beguile registers the name and type for compile-time checking but emits no I6 definition.

| Form | Purpose |
|------|---------|
| `extern class Name { ... }` | I6 class with Beguile-typed emitters and stubs |
| `extern enum Name { ... }` | I6 enum; registers member names globally |
| `extern object Name;` | Forward-declares an I6 object |
| `extern verb Name;` | Registers a library verb name |
| `extern attribute Name;` | Registers an existing I6 attribute (no declaration emitted) |
| `extern int Name;` | Mutable I6 global variable |
| `extern const int Name;` | Read-only I6 constant |
| `extern var Name;` | Untyped I6 variable (when type is unknowable) |

None of these produce any I6 output. They exist solely to make I6-defined names available in Beguile source with proper typing.

To declare a new attribute in Beguile (emitting an I6 `Attribute` declaration), use the non-extern form: `attribute Name;` (see §8.6).

Extern variable declarations may also carry an `as i6name` alias clause — see §8.7.

## 15.3 `#includeI6`

Passes an I6 `Include` directive through to the generated `.inf` file verbatim:

```bgl
#includeI6 "parser"
#includeI6 "verblib"
#includeI6 "grammar"
```

These are used to pull in standard Inform library files at the correct positions in the output. The Beguile compiler does not parse or validate the included I6 files.

## 15.4 Emitter Bodies as Raw I6

Every emitter body is raw I6 text. Authors may use any I6 construct inside an emitter body — I6 operators, library calls, conditionals, `objectloop`, `move`, `give`, etc. — without restriction. Only `$self` and parameter names are treated specially (substituted before emission).

```bgl
extern class object {
    emitter void move(object dest){ move $self to dest }
    emitter eBool has(attribute a) { $self has a }
}
```

This is the primary path for I6 capabilities that have no Beguile syntax equivalent.

## 15.5 Debug Bundle

When compiled with `--debug`, beguiler produces two debug files alongside the story file:

**`<filename>.bgl.transpiled.inf.bgldbg`** — a plain-text bundle containing three sections:

```
[map]
<i6LineNumber>\t<bglSourceFile>\t<bglLineNumber>
...
[sym]
<bglName>\t<i6Name>\t<kind>
...
[types]
type <TypeName>
  prop <bglPropName> <i6PropName> <valueType>
routine <i6FunctionName>
  local <varName> <valueType>
global <varName> <valueType>
```

- **`[map]`** — correlates each I6 source line number back to its originating Beguile source file and line. Used by the debugger to navigate between Beguile source and generated I6.
- **`[sym]`** — symbol table mapping Beguile names to their I6 identifiers and declaration kind (`global`, `object`, `function`, `property`).
- **`[types]`** — Beguile type information for all declared variables, enabling rich display of typed values (including object properties) in the debugger's Variables pane.

**`<filename>.bgl.transpiled.inf.dbg`** — the Inform 6 XML debug database, produced by `inform6 -k`. Contains VM bytecode addresses mapped to I6 source line numbers, routine definitions with local variable frame offsets, global variable addresses, and property numbers. Its format is fixed by the Inform 6 compiler.

Together these two files give the VS Code extension everything it needs for source-level debugging: the chain `VM address → I6 line → .bgl file + line`, full call stack reconstruction, and typed variable display.

---

# Chapter 16 — Beguile Language Extensions

## 16.1 Overview

The Beguile language extensions (`beguiLib/`) provide opt-in language features that are independent of any particular IF engine library. None are built into the compiler; each is included by the author as needed.

A separate category of files, **IF Library Bindings** (`beguiLib/bindings/`), expose a specific external IF library's symbols — attributes, globals, actions, and so on — to Beguile's type system using `extern` declarations. These are inherently target-dependent and entirely optional; projects that manage their own I6 bindings or use a different library do not need them.

## 16.2 Language Extension Files

Core files in `beguiLib/`. These are library-agnostic and work with any IF target.

### File Overview

**`string.bgl`** — The full string runtime. Provides a pool-based string type with methods covering assignment, concatenation, comparison, mutation, transformation, substring extraction, search, and formatted output. Requires `bglInit()` (see §16.2.1).

**`char.bgl`** — Extends the built-in `char` type with character-testing, case transformation, and support for extended characters including diacritics and other Z-machine character set entries.

## 16.2.1 `bglInit()` — Runtime Initialization

Some language extensions allocate runtime resources (memory pools, buffers, etc.) that must be set up before use. These extensions register an initialization hook automatically when included; all registered hooks are called by `bglInit()`.

`bglInit()` is always available — it is a no-op if no extensions that need it have been included. Call it once, early in your game's startup routine:

```bgl
void Initialise() {
    bglInit();
    // ... rest of setup
}
```

**Which extensions require it:** `string.bgl` requires `bglInit()` to initialize the string pool. Extensions that do not allocate runtime resources (such as `char.bgl`) do not require it but are unaffected by the call.

If you use `string` and forget to call `bglInit()`, strings will not function correctly. The call is harmless to include unconditionally whenever any language extension is in use.

## 16.3 IF Library Bindings

Files in `beguiLib/bindings/`. Each binding file is a Beguile declaration layer over one particular external IF library, giving Beguile code typed, name-checked access to that library's attributes, globals, and actions. Binding files do not define behavior — they map existing I6 names into the Beguile type system using `extern` declarations.

Different IF libraries require different binding files and would not be used together. An example binding file is provided for the Inform 6 standard library.

### File Overview

**`i6StandardLibrary.bgl`** — Example bindings for the Inform 6 standard library. Declares standard world-model attributes, mutable library globals, parser variables, and the full set of standard IF actions as typed verbs.

---

## 16.4 `bglWorld` — Object Tree Iteration

`bglWorld` is a built-in object that provides structured iteration over the Inform 6 object tree. It is available without any `#include`.

All methods fill a shared internal scratch buffer and return it as an `array`. The buffer is reused on every call — do not store the result across turns or nest two `bglWorld` calls.

### Methods

| Method | Description |
|--------|-------------|
| `bglWorld.getAll()` | Returns all objects in the game world |
| `bglWorld.getFiltered(pred)` | Returns all objects for which `pred(o)` returns true |
| `bglWorld.inParent(parent)` | Returns all direct children of `parent` |
| `bglWorld.inParent(parent, pred)` | Returns direct children of `parent` for which `pred(o)` returns true |
| `bglWorld.ofClass(cls)` | Returns all objects of class `cls` (I6 `ofclass` loop) |
| `bglWorld.ofClass(cls, pred)` | Returns objects of class `cls` for which `pred(o)` returns true |

The predicate argument is a function that takes one `object` parameter and returns `bool`.

### Usage

```bgl
// Iterate all objects
for(object o in bglWorld.getAll()) {
    print(o);
}

// Filter by predicate (named function)
bool isLit(object v) {
    return v.has(light);
}
for(object o in bglWorld.getFiltered(isLit)) {
    print(o);
}

// Filter by predicate (anonymous function)
for(object o in bglWorld.getFiltered((object v) => v.has(light))) {
    print(o);
}

// Children of a room
for(object o in bglWorld.inParent(location)) {
    print(o);
}

// All objects of a given class
for(object o in bglWorld.ofClass(Treasure)) {
    print(o);
}
```

### Type guard for `ofClass`

The compiler validates the class argument to `ofClass`. The following are compile-time errors:

- Passing an `emitter class` (no I6 backing)
- Passing an `alias class` such as `verb` (dissolves to a parent type)
- Passing an `enum` type
- Passing a primitive type such as `int` or `bool`

Only user-defined `object` declarations and non-emitter, non-alias `extern class` types are accepted.

### Buffer capacity

The scratch buffer holds up to 256 objects. Games with more than 256 objects in a single category will be silently truncated. For typical IF games this limit is not a concern.

---
