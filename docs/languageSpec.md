# Beguile Language Specification

> **How to read this document.** This is the complete language reference. If you are new to Beguile and coming from Inform 6, start with the companion document [**"Beguile for the I6 Developer"**](https://github.com/onyxring/beguiler/blob/main/docs/Beguile%20for%20the%20I6%20Developer.md) instead: it teaches the language through a complete, runnable Cloak of Darkness port and maps each construct back to the Inform 6 you already know. This specification is the reference you graduate to, for looking things up once you know the language. A glossary of the terms of art used here is in Appendix A.

## Table of Contents

### Chapter 1 - Introduction
- 1.1 What Is Beguile?
- 1.2 Design Goals
- 1.3 Compilation Model
- 1.4 Relationship to Inform 6
- 1.5 A Note on Scope

### Chapter 2 - Lexical Conventions
- 2.1 Character Set and Encoding
- 2.2 Comments
- 2.3 Case Sensitivity
- 2.4 Identifiers
  - 2.4.1 Special Prefixes `_bgl` and `bgl`
- 2.5 Literals
  - 2.5.1 Integer Literals
  - 2.5.2 String Literals
  - 2.5.3 Raw String Literals
  - 2.5.4 Interpolated String Literals
  - 2.5.5 Character Literals
  - 2.5.6 Dictionary Word Literals
- 2.6 Operators and Symbols
- 2.7 Preprocessor Directives
- 2.8 Reserved Keywords

### Chapter 3 - Program Structure
- 3.1 Source Files
- 3.2 Include Directives
  - 3.2.1 `#include <name>`
  - 3.2.2 `#include "path"`
  - 3.2.3 `#include ?"path"` (optional)
  - 3.2.4 `#includeI6 "name"`
  - 3.2.5 Path resolution
  - 3.2.6 Differences from I6 path conventions
  - 3.2.7 `#once`
- 3.3 Preprocessor Symbols
  - 3.3.1 `#define`
  - 3.3.2 `#redef` and `#undef`
  - 3.3.3 `#declare` (order-independent symbols)
  - 3.3.4 Pre-Defined Symbols
  - 3.3.5 Conditional Compilation
- 3.4 `#beguilerSettings`
  - 3.4.1 Blorb packaging
  - 3.4.2 Referencing settings values in Beguile source
- 3.5 Diagnostic and Control Directives
  - 3.5.1 `#message`
  - 3.5.2 `#warning`
  - 3.5.3 `#error`
  - 3.5.4 `#exit`
  - 3.5.5 `#startup`
  - 3.5.6 `#emitfirst`
  - 3.5.7 `#storedEmitFirst <name>` and `#storedEmitLast <name>`
  - 3.5.8 `#emitlast`
  - 3.5.9 `#using`

### Chapter 4 - Types
- 4.1 Overview
- 4.2 Primitive Types
- 4.3 The `uint` Type
- 4.4 The `float` Type
- 4.5 Literal Pseudo-Types
- 4.6 `nothing` and `null`
- 4.7 Enumerations
- 4.8 The `var` Type
- 4.9 Arrays
  - 4.9.1 `rawArray<T>` - raw I6 word arrays
  - 4.9.2 Local array lifetime - returned local arrays are *ephemeral*

### Chapter 5 - Classes
- 5.1 Defining a Class
- 5.1.1 Type Parameters
- 5.2 The Four Class Forms
- 5.2.1 Normal Classes
- 5.2.2 `extern class`
- 5.2.3 `emitter class`
- 5.2.4 `alias class`
- 5.2.5 Pooled Classes
- 5.2.6 `byVal class` - Value-Semantic Class Parameters
  - 5.2.7 `typesealed` - Type-Locked Members
- 5.3 Member Variables
- 5.4 Member Methods
- 5.5 Lifecycle Emitters
- 5.6 Operator Overloads
  - 5.6.1 Emitter Operators
  - 5.6.2 Non-Emitter Operators
  - 5.6.3 Subscript Operators
  - 5.6.4 Conversion Operator (`operator()`)
  - 5.6.5 Special Operators
  - 5.6.6 Overloadable Operator List
- 5.7 Class Inheritance
- 5.8 `extend class` and `replace` for Members
- 5.9 `extend` for Objects
- 5.10 `_bglGlobalDeclaration`
- 5.11 Namespace-Scoped Types
  - 5.11.1 Value aliases: `alias name = Target`
  - 5.11.2 Alias Members on Emitter Classes

### Chapter 6 - Objects
- 6.1 Overview
- 6.2 Declaring an Object
- 6.3 Object Types
- 6.4 Properties
  - 6.4.1 Type Inference from Class and Base Object
  - 6.4.2 Special Properties
- 6.5 Array Properties
- 6.6 Method Properties
  - 6.6.1 Method dispatch on object receivers
  - 6.6.2 Method overloads on objects and classes
- 6.7 Complete Example

### Chapter 7 - Global Declarations
- 7.1 Overview
- 7.2 Global Variables
- 7.3 Global Constants
- 7.4 Global Functions
- 7.5 Extern Variables
- 7.6 Attributes
- 7.7 Properties
- 7.8 Class Tests
- 7.9 I6 Name Aliasing - the `as` Clause

### Chapter 8 - Functions and Routines
- 8.1 Syntax
- 8.2 Return Types
- 8.3 Parameters
- 8.4 Overload Resolution
- 8.5 The `self` Keyword

### Chapter 9 - Statements
- 9.1 Overview
- 9.2 Variable Declaration
  - 9.2.1 Class-Typed Locals and Reference Semantics
  - 9.2.2 Bypassing the Z-Machine's Local Variable Limit
- 9.3 Assignment
- 9.4 Compound Assignment
- 9.5 Increment and Decrement
- 9.6 Function and Method Calls
- 9.7 `if` / `else`
- 9.8 `for` Loop
  - 9.8.1 `for-in` Loop
- 9.9 `while` Loop
- 9.10 `do` / `while` and `do` / `until`
- 9.11 `switch` / `case`
- 9.12 `break` and `continue`
- 9.13 `return`
- 9.14 `print()` and `log()`
- 9.15 `try` / `catch` / `throw`

### Chapter 10 - Expressions
- 10.1 Overview
- 10.2 Operands
- 10.3 Binary Operators
- 10.4 Ternary Operator
- 10.5 Optional Chaining, Null Coalescing, and Postfix Query
- 10.6 Type Cast
  - 10.6.1 Casting to a class vs. an instance
- 10.7 `new` and `delete` for Pooled Classes
- 10.8 Lambda Functions
  - 10.8.1 The `func<>` Type
  - 10.8.2 Lambda Literal Syntax
  - 10.8.3 Passing Lambdas as Arguments
  - 10.8.4 Closures and Capture
  - 10.8.5 Constraints

### Chapter 11 - Type Compatibility and Conversion
- 11.1 Overview
- 11.2 Compatibility Rules
- 11.3 Priority at Assignment
- 11.4 Priority at Function Call Arguments
- 11.5 Example

### Chapter 12 - Scope and Identifier Resolution
- 12.1 Overview
- 12.2 Resolution Tiers
- 12.3 Global-Scope Qualifier `::`
- 12.4 The `self` Keyword
- 12.5 Variable Shadowing
- 12.6 Verb Names vs. Variables
- 12.7 Global Name Collision Detection

### Chapter 13 - Verbs and Grammar
- 13.1 Overview
- 13.2 Verb Declarations
  - 13.2.1 Defining behavior: `handler()`
  - 13.2.2 Launching an action: `perform()`
  - 13.2.3 External Verbs
  - 13.2.4 Meta Verbs
  - 13.2.5 Verb Priority
- 13.3 Action Comparisons
- 13.4 Grammar
  - 13.4.1 Grammar Types
  - 13.4.2 Grammar Pattern Tokens
  - 13.4.3 Advanced Pattern Syntax
  - 13.4.4 Trailing line modifiers (`reverse`, `withI6Synonyms`)
  - 13.4.5 Grammar on Verbs
  - 13.4.6 Grammar Objects
  - 13.4.7 Per-Rule Priority on Grammar Objects
  - 13.4.8 Extending Verb Grammar
  - 13.4.9 Replacing Verb Grammar
  - 13.4.10 Verb Synonyms

### Chapter 14 - Interoperability with Inform 6
- 14.1 Overview
  - 14.1.1 Two compilation modes
  - 14.1.2 Islands
- 14.2 `extern` Declarations
  - 14.2.1 Extern functions and `default` stubs
  - 14.2.2 Extern objects with member signatures
- 14.3 `#includeI6`
- 14.4 Emitters
  - 14.4.1 What Is an Emitter?
  - 14.4.2 Emitter Syntax
  - 14.4.3 Substitution
  - 14.4.4 Global Emitters
  - 14.4.5 Emitter Values
  - 14.4.6 Emitter Namespaces
  - 14.4.7 `print()` and `log()`
  - 14.4.8 Operator Emitters
  - 14.4.9 Conversion Operator
  - 14.4.10 `operator auto()` - Auto-Inference Type
  - 14.4.11 Lifecycle Emitters: `init` and `deinit`
  - 14.4.12 Emitters vs. Regular Functions
  - 14.4.13 Emitter Bodies as Raw I6
- 14.5 `#i6` - I6 Islands (Inline Raw I6)
  - 14.5.1 `#i6replace` - hoisted I6 `Replace` directive
- 14.6 `#bgl` - In-Routine Beguile Islands
- 14.7 Precompiler Mode - File-Scope Beguile Islands
  - 14.7.1 The three Beguile island directives
  - 14.7.2 Loose identifier mode
  - 14.7.3 Auto-loaded ICL header
  - 14.7.4 Runtime initialization
  - 14.7.5 Required explicit `#using bgl`
  - 14.7.6 No-island fast path
- 14.8 I6 Emission Ordering
- 14.9 Debug Bundle
- 14.10 `superposed` Routines, Globals, Objects, and Classes

### Chapter 15 - Runtime Library
- 15.1 Overview
  - 15.1.1 `bglInit()` - Runtime Initialization
- 15.2 The `bgl` Namespace
- 15.3 IF-Domain Built-in Types
  - 15.3.1 `attribute` and `attributeList`
  - 15.3.2 `property`
  - 15.3.3 `dictionaryWord`
  - 15.3.4 `verb`
  - 15.3.5 Grammar types
  - 15.3.6 `bglClass`
  - 15.3.7 `parentProp`
- 15.4 `bgl.world` - Object Tree Iteration

### Chapter 16 - Opt-In Language Extensions
- 16.1 Overview
- 16.2 Core Language Extension Files
  - 16.2.1 `<bglAllocated>` - Allocator-Managed Object Mixin
  - 16.2.2 `<char>` - Character Utilities
  - 16.2.3 `<buf>` - Tracked Character Buffers
  - 16.2.4 `<string>` - Mutable String Runtime
  - 16.2.5 `<uint>` - Unsigned Integer Type
  - 16.2.6 `<math>` - Mathematical Functions
  - 16.2.7 `<array>` - Extended Array Utilities
  - 16.2.8 `<linq>` - LINQ-style Fluent Operations
- 16.3 IF Library Bindings

### Appendix A - Glossary

# Chapter 1 - Introduction

## 1.1 What Is Beguile?

Beguile is a statically-typed, compiled language designed for authoring Interactive Fiction (IF). It provides a structured, C-like syntax which transpiles to Inform 6.

Although it borrows features from popular compilers, Beguile itself is not general-purpose language. Its features are shaped by the needs of Interactive Fiction and the constraints of the virtual machines its code ultimately compiles down to.

## 1.2 Design Goals

- **Familiar syntax.** Beguile reads like C or a C-adjacent language. Developers comfortable with C, C++, C#, TypeScript, or similar languages should find the syntax intuitive.
- **Strong typing.** All variables, parameters, and return values carry a declared type. Type mismatches are caught at compile time, not at runtime.
- **Transparency.** The generated I6 is readable and maps closely to the Beguile source. Developers who know I6 can inspect or supplement the output.
- **Extensibility through emitters.** Performance-sensitive or platform-specific operations can be expressed as *emitters*, inline I6 fragments that are substituted at the call site. This gives library authors full control over the generated code without sacrificing type safety at the Beguile level.

### Pretty Lies - Shorthand over a Consistent Core

Beguile aims for **one consistent set of rules**: declarations live in member bodies, assignments end in `;`, types precede variable names, and so on. The canonical form of every construct follows those rules.

Strict adherence can require needless boilerplate, so Beguile provides a small number of shorthand forms, the *pretty lies*, that desugar to the canonical form at parse time. The author sees a compact, readable surface; the canonical form remains the underlying truth, available whenever the shorthand doesn't fit. Each shorthand is flagged where its construct is introduced (for example, inferred member types in §6.4.1 and the grammar shorthands in §13.2).

## 1.3 Compilation Model

A Beguile source file (`.bgl`) is processed by the Beguile compiler through the following stages. Steps 1 and 6 are conditional and only run when blorb packaging is enabled (`generateBlorb = true` in `#beguilerSettings`); see §3.4.1.

1. *(optional)* **Pre-scans for assets** when blorb packaging is enabled.
2. **Pre-scans** the source to register type, object, function, and variable names. This pass resolves forward references so that declarations may appear in any order.
3. **Lexes and parses** the source in full, resolving types and checking type compatibility.
4. **Emits** an Inform 6 source file (`.inf`) that is semantically equivalent to the Beguile source.  If enabled, a debug file is also generated for use by run-time debuggers (see below). 
5. **Invokes the Inform 6 compiler** on the generated `.inf` file to produce the story file (`.ulx` for Glulx or `.z5` or `.z8` for Z-Machine).
6. *(optional)* **Blorb assembly** when blorb packaging is enabled.  

The intermediate `.inf` file is retained alongside the story file. When compiled with `--debug`, Beguile generates a debug file and triggers Inform 6 to do the same, supporting source-level debugging in the VS Code extension.

Beguile compiles in one of two modes. In the **default mode**, the source is a Beguile (`.bgl`) file that may embed raw Inform 6 through `#i6` islands. In **precompiler mode**, the source is an Inform 6 (`.inf`) file with Beguile embedded through `#bgl` islands; it follows the same pipeline, except the Beguile passes (steps 2-4) run only over the embedded islands while the surrounding I6 passes through to step 5 unchanged. Both modes are covered in §14.1.

## 1.4 Relationship to Inform 6

Beguile is built on top of Inform 6; it is not a replacement for it. It generates human-readable I6 source and several Beguile features exist specifically to bridge the two languages cleanly:

- `extern` declarations allow Beguile to use types, functions, attributes, and constants that are defined in I6 without re-implementing them.
- `#i6` output blocks of raw I6 code, declared inline with Beguile code, to the generated file.
- `#includeI6` passes raw I6 include directives through to the generated file.
- Emitter bodies contain literal I6 code, giving library authors precise control over the output.

Authors who need capabilities beyond what Beguile exposes can always drop down to I6 through these mechanisms.

## 1.5 A Note on Scope

This specification describes the Beguile language as recognized by the compiler. It does not describe the Inform 6 language itself, the common libraries used with I6, or the behavior of the Z-Machine or Glulx virtual machines. Readers wanting background on those topics should consult the Inform 6 documentation and the relevant virtual machine specifications.

---

# Chapter 2 - Lexical Conventions

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

Beguile is **case-insensitive** for all tokens except string literal values. Keywords, type names, identifiers, and operator names are normalized to lowercase at lex time. The following are all equivalent:

```bgl
if(X == 1) print("yes");
IF(x == 1) Print("yes");
If(x == 1) PRINT("yes");
```

String literal contents are preserved exactly as written.

## 2.4 Identifiers

An identifier is a sequence of alphanumeric characters and underscores. The first character must be a letter or an underscore; it may not be a digit.

Examples of valid identifiers: `score`, `myVar`, `_internal`, `room1`, `velvetCloak`.

Identifiers that match a reserved keyword (§2.8) may not be used as variable or function names.

A **member or method name may share a name with a type**. Because Beguile is case-insensitive, a field named `color` and a class named `Color` are the same identifier; a member may even be named after a built-in type keyword such as `object`. Such members are still fully reachable through dot-access (`thing.color`, `self.color`, `win.draw()`), because a name following `.` is unambiguously a member. Type names stay reserved for *top-level* identifiers (§2.8); this relaxation applies only in member positions.

### 2.4.1 Special Prefixes `_bgl` and `bgl`

Identifiers beginning with `_bgl` or `bgl` are earmarked for language-generated symbols and runtime infrastructure (loop counters, frame pool variables, lambda functions, scratch temporaries, `bglInit`, `bglWorld`, etc.). This is a convention, rather than a hard requirement; however, it is best practice for user code to avoid names that begin with either prefix as collisions may produce undefined or erroneous I6 output.

## 2.5 Literals

### 2.5.1 Integer Literals

Integers may be expressed in decimal, hexadecimal, or binary notation. All three forms resemble established I6 notation but are converted to decimal at compile time. The generated I6 output is always rendered as plain decimal.

#### Decimal

A sequence of digits `0` to `9`. Negative values are formed by prefixing `-`.

```bgl
42
0
-1234
```

#### Hexadecimal

Prefixed with `$`, followed by one or more hex digits (`0` to `9`, `A` to `F`, `a` to `f`). 

```bgl
$FF         // 255
$10         // 16
$0A         // 10
$FFFF       // 65535
```

C-style hex notation (`0xFF`) is not supported. The compiler rejects it with an error directing the user to use `$XX` syntax instead.

#### Binary

Prefixed with `$$`, followed by one or more binary digits (`0` or `1`). 

```bgl
$$11111111  // 255
$$11010     // 26
$$00000001  // 1
$$10000000  // 128
```

### 2.5.2 String Literals

A string literal is enclosed in double quotes. The following escape sequences are recognized:

#### Basic escapes

| Escape | Displayed character |
|--------|---------------------|
| `\n`   | Newline |
| `\"`   | Double-quote (`"`) |
| `\\`   | Backslash (`\`) |
| `\^`   | Caret (`^`), see note below |
| `\~`   | Tilde (`~`), see note below |
| `\@`   | At-sign (`@`) |

```bgl
"Hello, world!"
"She said, \"well done.\""
"Line one\nLine two"
"Price: 5\~ off!"
"Press \^ to continue."
```
As a design decision, Beguile preserves most of Inform's conventions for embedding extended characters in strings.  As such, `^` and `~` pass through to I6 as written and should be escaped to print.

#### Numeric character escapes

Characters escapes can be specified by their numeric code, using either decimal or hexadecimal (prefixed with `$`):

| Escape | Meaning |
|--------|---------|
| `\NNN` | Decimal character code (e.g. `\228` → ä) |
| `\$XX` | Hex character code (e.g. `\$E4` → ä) |

Both forms consume all consecutive digits (or hex digits) after the prefix. They emit the I6 unicode escape `@{XXXX}`, which is supported on both Glulx and Z-machine.

```bgl
"na\$EFve"       // naïve (using hex)
"na\239ve"       // naïve (using decimal)
```

#### Diacritical accent shorthands

Common accented characters can be written using shorthand escape sequences that mirror the I6 `@` accent notation, using `\` as the escape prefix:

| Escape | Applies to | Example | Result |
|--------|-----------|---------|--------|
| `\'X`  | a e i o u y A E I O U Y | `"caf\'e"` | café |
| `` \`X `` | a e i o u y A E I O U Y | `` "cr\`eme" `` | crème |
| `\^X`  | a e i o u y A E I O U Y | `"g\^ateau"` | gâteau |
| `\:X`  | a e i o u y A E I O U Y | `"na\:ive"` | naïve |
| `\~X`  | a n o A N O | `"se\~nor"` | señor |

| Named escape | Result | | Named escape | Result |
|-------------|--------|--|-------------|--------|
| `\/o` `\/O` | ø Ø (slashed o) | | `\th` `\TH` | þ Þ (thorn) |
| `\cc` `\cC` | ç Ç (cedilla) | | `\et` `\ET` | ð Ð (eth) |
| `\oa` `\oA` | å Å (angstrom) | | `\ae` `\AE` | æ Æ (ae ligature) |
| `\oe` `\OE` | œ Œ (oe ligature) | | `\ss` | ß (eszett) |
| `\LL` | £ (pounds sterling) | | `\!!` | ¡ (inverted !) |
| `\??` | ¿ (inverted ?) | | `\<<` `\>>` | « » (guillemets) |

**Context-sensitive `\^` and `\~`:** When `\^` or `\~` is followed by a character in its accent set, it produces the accented character. Otherwise it produces the literal caret or tilde. To force a literal caret or tilde before a character that would otherwise trigger an accent, double the character: `\^^` or `\~~`.

```bgl
"\^a"       // â  (circumflex a - auto-detected)
"\^^a"      // ^a (forced literal caret, then a)
"\~n"       // ñ  (tilde n - auto-detected)
"\~~n"      // ~n (forced literal tilde, then n)
"\^z"       // ^z (literal - z is not in the accent set)
```

**Inline Unicode diacriticals:** If your keyboard supports typing accented characters directly, Beguile accepts them in string literals, character literals, and dictionary words. The compiler automatically translates them to the appropriate I6 encoding. Both UTF-8 and Latin-1 source files are supported.

```bgl
"café"          // emits as "caf@'e"
'ñ'             // emits as 206 (ZSCII code)
.café           // emits as 'caf@'e'
```

The full ZSCII extended character set is supported (codes 155 to 224): diaeresis, acute, grave, circumflex, angstrom, slashed o, tilde, æ/Æ, ç/Ç, þ/Þ, ð/Ð, œ/Œ, ß, £, ¡, ¿. Unrecognized Unicode characters produce a compile-time error.

**Smart quotes and the reverse tick:** Typographic quotation marks — the curly double quotes `“` (U+201C) and `”` (U+201D), the curly single quotes `‘` (U+2018) and `’` (U+2019) — and the backtick `` ` `` are accepted in string literals and translated according to the target:

- **Glulx** renders the real glyphs, so each is emitted with the I6 Unicode escape (`@{201C}`, `@{201D}`, `@{2018}`, `@{2019}`); the backtick is kept as a literal backtick.
- **Z-code** has no typographic glyphs, so the curly double quotes fold to `~` (I6's double quote) and the curly single quotes and the backtick fold to a straight apostrophe `'`.

The target is taken from the resolved build target (`#beguilerSettings target`, a command-line target flag, or the ICL target); Glulx is the default. Only the directly typed characters are affected — an explicit `\$201C` numeric escape is emitted verbatim.

### 2.5.3 Raw String Literals

A **raw string literal** is prefixed with `@` and disables all Beguile escape processing except `\"`. Every character between the delimiters is passed through to the generated I6 string as-is, except that `~` and `^` are emitted as their I6 ZSCII equivalents (`@@126` and `@@94`) so they remain literal rather than being interpreted by I6 as quote/newline.

Raw strings are useful for Windows-style file paths or any string that contains many backslashes.

```bgl
string path = @"C:\Users\jim\IF-Games\medusa.bgl";
```

The closing `"` terminates the raw string.

Raw strings may be used anywhere a regular string literal is valid: in assignments, as function arguments, and in `#beguilerSettings` property values.

### 2.5.4 Interpolated String Literals

An **interpolated string** is prefixed with `$` and may contain embedded Beguile expressions inside `{` `}` spans. An interpolated string literal has the pseudo-type `interpolatedStringLiteral` (see §4.5). It can be passed to any emitter that declares an `interpolatedStringLiteral` parameter. Passing an interpolated string to a non-emitter function is a compile-time error; the expansion is purely textual and requires an emitter body to splice into.

```bgl
print($"The {obj.name} weighs {obj.weight} stone.");
print($"Score: {score}  Turns: {turns}");
log($"Entering handler for {actor.name}");
```

By default, interpolated strings work with `print()` and `log()`; however,  interpolated strings can also appear in assignment and variable initialization when used with Beguile's `string` language extension (`#include <string>`) (see §16.2.4):

```bgl
string greeting = $"Hello, {name}!";
greeting = $"Score: {score}  Turns: {turns}";
```

**Escape sequences** inside interpolated strings follow the same rules as plain string literals. To include a literal `{` character, write `\{`:

```bgl
print($"Press \{enter} to continue.");   // prints: Press {enter} to continue.
```

**Constraints:**

- Nested `{...}` inside an expression span (e.g. initializer lists) is not supported.

### 2.5.5 Character Literals

A single character enclosed in single quotes.

```bgl
'a'
'\n'
'\\'
```

Character literals accept the same standard, numeric, and diacritical escapes as string literals (§2.5.2), as well as raw Unicode diacriticals typed directly (UTF-8 or Latin-1 source).

Because I6's `@`-accent notation (e.g. `@:a`) is only valid inside string literals, diacritical and numeric escapes in character literals are converted to their numeric ZSCII codes for use in I6 expressions. For example, `'\:a'` and `'ä'` both emit as `155`, and `c >= 'ä'` compiles to `c >= 155`.

**Acute accent ambiguity**: `\'` followed by a vowel is treated as an acute accent (`'\'e'` = é). A bare `\'` not followed by a vowel is a literal escaped quote.
### 2.5.6 Dictionary Word Literals

Dictionary word literals represent I6 dictionary entries, the tokens the parser uses to match player input.

- A **singular** dictionary word is written with a leading `.`: `.cloak`, `.hook`, `.velvet`
- A **plural** dictionary word is written with a leading `..`: `..cloaks`

```bgl
array<dictionaryWord> name = { .small, .brass, ..bells };
```

Both forms resolve to type `dictionaryWord`.

A dictionary word may contain an **internal hyphen** (I6 dictionary words such as `'medium-sized'`): a `-` between two word characters is read as part of the word, not as the subtraction operator.

```bgl
adjective = { .medium-sized, .well-worn };   // → I6  'medium-sized' 'well-worn'
```

Apostrophes are likewise acceptable (`.monkey's` → `'monkey^s'`).

## 2.6 Operators and Symbols

The following two-character sequences are recognized as single operator tokens:

`-=`  `+=`  `?=`  `==`  `!=`  `<=`  `>=`  `=~`  `&&`  `||`  `++`  `--`  `<<`  `>>`  `*=`  `/=`  `%=`  `&=`  `|=`  `^=`

Single-character operator and punctuation symbols include:

`=`  `+`  `-`  `*`  `/`  `%`  `<`  `>`  `!`  `&`  `|`  `^`  `(`  `)`  `{`  `}`  `[`  `]`  `;`  `,`  `.`  `#`  `?`  `:`

The meaning of each operator is detailed in later chapters. The following table is a quick index from operator to role and the section with the full treatment.

| Operator(s) | Role | See |
|---|---|---|
| `+` `-` `*` `/` `%` | Arithmetic | §10.3 |
| `+=` `-=` `*=` `/=` `%=` | Compound arithmetic assignment | §10.3 |
| `==` `!=` `<` `>` `<=` `>=` `?=` | Comparison (result `eBool`) | §10.3 |
| `=~` | Content / case-insensitive equality (`<char>`, `<string>`) | §16.2.2 |
| `&&` `\|\|` `!` | Logical (result `eBool`) | §10.3 |
| `&` `\|` `^` `<<` `>>` | Bitwise and shift (shift via `<int>`) | §5.6.6 |
| `&=` `\|=` `^=` | Compound bitwise assignment | §10.3 |
| `++` `--` | Increment / decrement | §9.5 |
| `=` | Assignment | §10.3 |
| `? :` | Ternary conditional | §10.4 |
| `?` (postfix) | Query / null test (result `eBool`) | §10.5 |
| `?.` | Optional chaining | §10.5 |
| `??` | Null coalescing | §10.5 |
| `(Type)x` | Type cast and conversion | §10.6, §5.6.4 |
| `[]` `[]=` | Subscript read / write | §5.6.3 |
| `::name` | Global-scope qualifier | §12.3 |

The set of operators a class may overload is listed in §5.6.6; operator precedence is tabulated in §10.3.

Two-character tokens take precedence: when the lexer encounters a character that could begin a two-character token, it peeks at the next character before deciding.

A leading `::` immediately followed by an identifier is the **global-scope qualifier** (`::name`): the lexer glues it onto the following identifier as a single token. It forces file-scope resolution and is described in §12.3. (No other Beguile construct uses `::`.)

The bracket pair `[` `]` has a secondary role as part of the subscript operator names `[]` (read) and `[]=` (write) when used in `operator` declarations inside a class body. In that context `[]` and `[]=` are the operator names, not individual punctuation tokens; see §5.6.3.

## 2.7 Preprocessor Directives

Tokens beginning with `#` immediately followed (with no whitespace) by an identifier are preprocessor directives, as in the follow examples:

`#include "path"`
`#define NAME value`
`#if expr`
`#message "text"`
`#warning "text"`

The complete list of directives is described in Chapter 3.

## 2.8 Reserved Keywords

The following identifiers are reserved and may not be used as variable, function, type, or object names. They are grouped by origin.
### Beguile keywords

Structural and type keywords unique to Beguile. They have no corresponding I6 keyword and are fully consumed by the transpiler; nothing from this group appears in the generated output.

`const`  `default`  `extern`  `extend`  `alias` `emitter`  `explicit`  `replaced` `static`  `inline`  `typesealed` `enum` `int`  `bool`  `void`  `auto` `var` `null`

### Control flow keywords

Reserved by Beguile and transpile to I6 statements of the same or equivalent name:

`if`  `else`  `for`  `while`  `do`  `switch`  `case`  `default`  `to`  `break`  `continue`  `return`  `rtrue`  `rfalse`  `try`  `catch`  `throw`

### I6-significant keywords

Beguile uses these as language constructs, but they also appear verbatim in the generated I6 output as keywords or well-known global identifiers. Reusing them as names would produce invalid or ambiguous I6.

`class` `object` `array` `attribute` `property` `grammar` `verb` `replace` `string` `self` `true` `false` 

### I6 keywords and reserved words

The groups above cover the I6 constructs that Beguile surfaces as its own keywords. Inform 6 reserves many more words that Beguile does not, and they matter in two situations.

**Directives with no Beguile form.** Directives such as `Abbreviate`, `Zcharacter`, `Dictionary`, `Fake_action`, `Lowstring`, `Stub`, `Trace`, and `System_file` have no dedicated Beguile keyword. Reach for them through raw I6: `#i6` (§14.5) and `#includeI6` (§3.2.4) pass their contents to the I6 compiler untouched, so any directive is available this way.

```bgl
#i6 {
    Abbreviate "the ";
}
```

**Reserved words that collide in generated names.** A Beguile name that reaches the generated output verbatim (an object, routine, global, or `extern` name) must not be an Inform 6 reserved word, or the Inform 6 stage rejects it even though Beguile accepted it. Many of these words are already Beguile keywords (§2.8 above) and so cannot be used as names anyway; the rest are legal Beguile identifiers that nonetheless fail at I6 if emitted verbatim. When you must keep such a name, give the declaration an explicit I6 name with an `as` clause (§7.9). The Inform 6 reserved words are:

- **Directives:** `Abbreviate`, `Array`, `Attribute`, `Class`, `Constant`, `Default`, `Dictionary`, `End`, `Endif`, `Extend`, `Fake_action`, `Global`, `Ifdef`, `Iffalse`, `Ifndef`, `Ifnot`, `Iftrue`, `Ifv3`, `Ifv5`, `Import`, `Include`, `Link`, `Lowstring`, `Message`, `Nearby`, `Object`, `Property`, `Release`, `Replace`, `Serial`, `Statusline`, `Stub`, `Switches`, `System_file`, `Trace`, `Undef`, `Verb`, `Version`, `Zcharacter`
- **Statements:** `box`, `break`, `continue`, `do`, `else`, `font`, `for`, `give`, `if`, `inversion`, `jump`, `move`, `new_line`, `objectloop`, `print`, `print_ret`, `quit`, `read`, `remove`, `restore`, `return`, `rfalse`, `rtrue`, `save`, `spaces`, `string`, `style`, `switch`, `until`, `while`
- **Condition and relation keywords:** `has`, `hasnt`, `in`, `notin`, `ofclass`, `or`, `provides`, `with`, `private`, `to`, `near`, `from`
- **Built-in identifiers:** `nothing`, `self`, `sender`, `true`, `false`, `parent`, `child`, `sibling`, `children`, `metaclass`, `indirect`, `random`, `glk`

Consult the Inform 6 documentation (the Designer's Manual's reserved-word list) for the authoritative, version-current list.

---

# Chapter 3 - Program Structure

## 3.1 Source Files

A Beguile program consists of one or more `.bgl` source files. Like Inform 6, a program must define a `Main` global function as its entry point. The smallest complete program is only a few lines:

```bgl
void Main() {
    print("Hello, world!^");
}
```

General-purpose libraries, such as the Inform Standard Library or Puny Inform, usually define `Main` for you and expect a library-specific entry point instead, such as `Initialise`; consult the library's own documentation.

### Global Scope

Declarations at the outermost level of a file (types, classes, enums, variables, functions, objects, verbs, and grammar) constitute the *global scope*. They may appear in any order within a file; the two-pass compilation model (pre-scan, then full parse) resolves forward references, so a name may be used before it is declared as long as both appear in the same compilation unit.

Declarations may be preceded by qualifier keywords such as `const`, `static`, `extern`, `emitter`, `extend`, `alias`, `replace`, `default`, `superposed`, and `typesealed`. These qualifiers and their valid combinations are covered under *Declaration Qualifiers* at the start of Chapter 5.

## 3.2 Include Directives

### 3.2.1 `#include <name>`

This directive includes a file from the language extensions provided by the Beguile Language Runtime (BLR). The compiler searches the `beguiLib` tree **recursively** and matches the include name, regardless of the folder it exists in:

- `#include <name>` finds `name.bgl` **anywhere** in `beguiLib`, root or any subfolder, so a subfolder prefix is optional. `<i6StandardLibrary>` finds `bindings/i6StandardLibrary.bgl` just as `<bindings/i6StandardLibrary>` does.
- `#include <sub/name>` matches `name.bgl` only when its **immediate parent folder** is `sub` (at any depth): `<orLibrary/orDialogue>` matches `.../orLibrary/orDialogue.bgl`.
- `#include <a/b/name>` requires the resolved path to end with `a/b/name.bgl`.

The search is deterministic: it checks the root folder's files first, then descends into subfolders in **alphabetical order**, depth-first, and the **first match wins**. Matching is case-insensitive (`<String>` ≡ `<string>`) *even if your filesystem is case insensitive*. The `.bgl` extension is supplied by the compiler, so write the base name only.

```bgl
#include <string>                  // core extension at beguiLib/string.bgl
#include <i6StandardLibrary>       // = <bindings/i6StandardLibrary> - prefix optional
#include <orLibrary/orDialogue>    // matches beguiLib/bindings/orLibrary/orDialogue.bgl
```

### 3.2.2 `#include "path"`

This directive includes a Beguile source file by path. The compiler searches the current source file's directory, then each `includePaths` directory, trying with `.bgl` extension first, then without. Subdirectory paths are supported; `#include "utils/helpers"` resolves relative to each folder searched.

A compile-time error is reported if the file is not found. It is legal to include files more than once; protect against this with `#once` (see §3.2.7).

```bgl
#include "myLibrary"
#include "utils/helpers"
```

### 3.2.3 `#include ?"path"` (optional)

Same as `#include "path"`, but silently skips if the file is not found instead of reporting an error. Also supported with angle brackets: `#include ?<name>`.

```bgl
#include ?"optionalExtension"
```

### 3.2.4 `#includeI6 "name"`

Includes an I6 source file. The compiler resolves the file in the same manner as `#include`, by searching the current source file's directory, then each `includePaths` directory, trying the name as-is and with `.h` extension. The resolved absolute path is emitted into the I6 output.

A compile-time error is reported if the file is not found. Subdirectory paths are supported.

```bgl
#includeI6 "parser"
```

An optional variant `#includeI6 ?"name"` silently skips if the file is not found. A raw variant `#includeI6 @"name"` emits the given string verbatim as the include path, with no path search, no existence check, and no separator rewriting; use it when you need to hand I6 an exact include string yourself.

All `includePaths` directories are also emitted as `!% ++include_path=` directives in the I6 output, so that I6 can resolve its own internal includes (e.g., `parser.h` including `linklpa.h`).

### 3.2.5 Path resolution

All file paths in Beguile source (`#include` paths, `#includeI6` paths, and `#beguilerSettings` path properties such as `informPath`, `outputPath`, `blorbAssetPath`) receive two normalization passes at parse time.

**Separator rewriting.** Every `/` and `\` character in a path string is replaced with the OS path separator (`\` on Windows, `/` on all other platforms). This means paths may be written with either separator and will work correctly on any platform. Separator rewriting can be disabled project-wide with `rewritePaths = false` in `#beguilerSettings`.

**Case-insensitive resolution.** Include matching is case-insensitive throughout (lowercased comparison). For `#include <name>` (library search) the compiler walks the `beguiLib` tree recursively, files in each folder first, then subfolders alphabetically, depth-first, and takes the first file whose path ends with the requested `name` suffix (see §3.2.1). For `#include "path"` (relative search) it matches against the current source directory and each `includePaths` root. If nothing matches, a normal file-not-found error results.

> Note: case-insensitive matching applies to intermediate directory components as well as the filename, so an include resolves on a case-sensitive file system even when directories or the filename differ in case. Each component prefers an exact match and falls back to the first case-insensitive match; if two entries in the same folder differ only in case, which one wins is unspecified.

**Resolved paths emitted to I6.** For `#includeI6`, the *fully resolved absolute path* is what gets written into the generated I6 stream, not the literal path the user typed. This isolates I6's own include lookup from the user's working-directory and search-path setup; I6 sees a single canonical filename and never has to re-search. For example, if `parser.h` resolves to `/game/lib/parser.h`, then

```bgl
#includeI6 "parser"
```

emits, into the generated I6:

```
#include "/game/lib/parser.h";
```

The raw form `#includeI6 @"parser.h"` bypasses this and emits `#include "parser.h";` verbatim.

### 3.2.6 Differences from I6 path conventions

Beguile does **not** use I6's `>filename` prefix on `#include` or `#includeI6`. In I6, a leading `>` means "from the same directory as the entry-point source file." Beguile already searches the current source file's directory first for `#include "path"` and `#includeI6 "name"`, so the prefix is redundant.

When porting I6 code into Beguile (`#include "..."` or `#includeI6 "..."`), drop any `>` prefix from the filename:

```
#include ">worldArea1.inf"   // ← I6 convention; Beguile rejects (file '>worldArea1.inf' not found)
#include "worldArea1.inf"    // ← correct in Beguile; resolves relative to the current file
```

The `>` convention still works for I6's own `Include` directives appearing inside raw I6 regions of `.inf`-mode files, because Beguile passes raw I6 through to the I6 compiler untouched. Only Beguile-layer `#include` / `#includeI6` directives need the prefix dropped.

### 3.2.7 `#once`

When placed at the top of a Beguile source file, `#once` marks the file so that any subsequent `#include` of the same file (by any path that resolves to the same absolute location) is silently ignored. Without `#once`, a file may be processed multiple times.

```bgl
#once
// rest of myLibrary.bgl ...
```

`#once` is the recommended guard for any file intended to be included as a library. The language extension files in `beguilib` all use it.

The compiler also enforces a maximum include nesting depth of 255. Exceeding this limit, for example through circular includes in files without `#once`, is a compile-time error.

## 3.3 Preprocessor Symbols

### 3.3.1 `#define`

Defines a named compilation symbol and value. Defining the symbol without a value assigns it a `true` boolean value by default.

```bgl
#define DEBUG         // ← DEBUG equals true
#define MAX_SCORE 100 // ← MAX_SCORE equals 100
```

Symbols defined with `#define` can be tested with `#if`. A value-bearing symbol is also resolved as an inline compile-time literal in Beguile expressions.  That is, the symbol name is replaced by its value at compile time. These numeric values resolve as `intLiteral`; other values resolve as `stringLiteral`.

`#define` and `#undef` are **position-dependent** (linear), like a standard C-style preprocessor: a directive affects only the source that follows it. An `#if` sees a symbol as defined only if a `#define` for it appears *earlier* in the source (and no intervening `#undef` removed it); a `#define` placed after an `#if` does not retroactively affect that `#if`. Redefining or `#undef`-ing a symbol partway through a file changes its meaning from that point forward. Redefining a symbol with a second `#define` is an error; to intentionally redefine a symbol, use `#redef` (§3.3.2). (Beguile compiles in two passes, but both evaluate conditionals with identical linear semantics.)

### 3.3.2 `#redef` and `#undef`

`#redef NAME value` behaves exactly like `#define`, except that it does not error when the symbol is already defined; it simply overwrites the existing value. Use `#define` for a symbol's first definition and `#redef` when a later redefinition is intentional.

`#undef NAME` removes a symbol so that a later `#if` treats it as undefined.

```bgl
#define MAX_SCORE 100

if(score >= MAX_SCORE) print("You win!");  // compiles as: if(score >= 100)
print(MAX_SCORE);                          // compiles as: print(100)
const int maxScore = MAX_SCORE;            // emits: Constant maxscore = 100;
```

Pre-defined compiler symbols (`beguiler`, `beguilerMajor`, etc.) behave the same way; they are resolved inline and do not emit I6 `Constant` declarations unless explicitly assigned to a `const` variable.

### 3.3.3 `#declare` — order-independent symbols

`#declare NAME [value]` defines a symbol like `#define`, but with two differences that make it suited to **capability detection** across files:

- **Order-independent.** A `#declare` is visible to *every* `#if` in the whole program, regardless of where it appears — even in a file that is `#include`d *after* the code testing it. (The compiler gathers all `#declare`s in its pre-scan, before any conditional is evaluated.)
- **Immutable.** A declared symbol cannot be `#undef`'d, `#redef`'d, or shadowed by a `#define` — each is a compile error, as is re-`#declare`ing it with a different value.

In every other respect it matches `#define`: it may carry a value (bare `#declare NAME` is a boolean flag; `#declare NAME value` resolves inline as a literal), and it is tested with the same `#if NAME` / `#if NAME >= x` expressions.

```bgl
// A core file, parsed early:
#if I6_STANDARD_LIBRARY;
    // use something the standard-library binding provides
#else;
    // self-contained fallback
#endif;

// The standard-library binding, #included later:
#declare I6_STANDARD_LIBRARY
```

The core file's `#if` sees `I6_STANDARD_LIBRARY` even though the `#declare` lives in a file included afterwards. With `#define` this is impossible: `#define` is linear (§3.3.1), so the `#if` is evaluated before the definition is seen and takes the `#else` branch.

**`#declare` vs `#define`**

| | `#define` / `#redef` | `#declare` |
|---|---|---|
| Visibility | Linear — affects only code *after* it | Global — visible to every `#if`, before or after |
| Across `#include` | Only if defined in an earlier-parsed file | Anywhere in the program, in any include order |
| Mutability | `#redef` overwrites, `#undef` removes | Immutable — `#undef` / `#redef` / `#define` / conflicting re-`#declare` are errors |
| Intended for | Local configuration, constants, toggles | Capability flags a library or binding advertises for other files to detect |

Use `#define` for ordinary configuration and constants; reach for `#declare` when one file must detect a feature or binding that may be pulled in from elsewhere (or not at all) — the case where `#define`'s linear visibility can't help because the detecting code is parsed first.

### 3.3.4 Pre-Defined Symbols

The compiler pre-defines the following symbols before any source file is parsed:

| Symbol | Example Value | Description |
|--------|-------|-------------|
| `beguiler` | `1023` | Full version encoded as `major*1000 + minor*10 + patch` (e.g. 1.2.3 = `1023`, 1.1.0 = `1010`). Safe on Z-machine (16-bit signed max 32,767 → major up to 32). |
| `beguilerMajor` | `1` | Major version component only. |
| `beguilerMinor` | `2` | Minor version component only. |
| `beguilerPatch` | `3` | Patch version component only. |
| `TARGET_GLULX` | (boolean) | Defined when `#beguilerSettings target = Glulx`. |
| `TARGET_ZCODE` | `5` or `8` | Defined when targeting Z-machine. The value is the Z-machine version number, enabling version comparisons. |

The version symbols are read-only and are calculated automatically from the compiler's internal version constant. They behave identically to user-defined `#define` symbols in `#if` expressions:

```bgl
#if beguiler >= 1010
    // requires Beguile 1.1.0 or later
#endif

#if beguilerMajor >= 2
    // major version 2+
#endif
```

The target symbols are set from `#beguilerSettings target` before parsing begins, so they are available to `#if` throughout both compiler passes:

```bgl
#if TARGET_ZCODE
    // Z-machine specific code
#endif

#if TARGET_ZCODE <= 5
    // Z5 only (excludes Z8)
#endif

#if TARGET_GLULX
    // Glulx specific code
#endif
```

All symbols (both pre-defined and user `#define` symbols) are resolved as inline compile-time literals in Beguile expressions; the symbol name is replaced by its value at compile time. They are not emitted as I6 `Constant` declarations unless explicitly assigned to a `const` variable:

```bgl
if(beguiler >= 1010) { ... }             // resolved at compile time
const int myVer = beguilerMajor;         // emits: Constant myver = 1;
```

### 3.3.5 Conditional Compilation

`#if`, `#elif`, `#else`, and `#endif` conditionally include or exclude blocks of source text. The expression following `#if` or `#elif` is evaluated at compile time against the currently defined symbols.

```bgl
#define DEBUG
#define FEATURE_LEVEL 3

#if DEBUG
    print("debug mode");
#endif

#if DEBUG && beguiler >= 1010
    print("debug mode on Beguile 1.1+");
#endif

#if FEATURE_LEVEL > 2
    // included when FEATURE_LEVEL is 3 or higher
#elif FEATURE_LEVEL == 1
    // included only when FEATURE_LEVEL is exactly 1
#else
    // fallback
#endif

#if TARGET_ZCODE <= 5
    // Z5 only (excludes Z8)
#endif

#if !DEBUG
    // included when DEBUG is not defined
#endif
```

The `#if` expression supports: symbol names (a bare symbol is true when defined; in a comparison it resolves to its value), integer literals, the keywords `true` and `false` (recognized as `1` and `0`), comparison operators (`==`, `!=`, `<`, `>`, `<=`, `>=`), logical operators (`&&`, `||`), negation (`!`), and parenthesized sub-expressions. Nesting is supported. The compiler skips tokens in excluded branches without parsing them.

Conditional directives and `#define`/`#undef` are honored during **both** compiler passes (pre-scan and full parse). This means `#if` can safely guard class, object, and function declarations; excluded declarations will not be registered as forward-reference stubs.

> **Note: there is no `#ifdef`.** Beguile does not have a separate `#ifdef` directive. Use `#if SYMBOL` to test whether a symbol is defined; it evaluates to `false` when the symbol is absent, which is equivalent to `#ifdef SYMBOL` in C-family languages. Similarly, use `#if !SYMBOL` in place of `#ifndef SYMBOL`.

> **Note: definedness vs. value.** A bare `#if SYMBOL` tests whether `SYMBOL` is *defined*, regardless of its value (the `#ifdef` equivalent), so `#if SYMBOL` is true even when the symbol's value is `0` or `false`. A comparison tests the symbol's *value*. The keywords `true` and `false` are recognized as `1` and `0` in both positions. Thus `#define V false` makes `#if V` **true** (V is defined) while `#if V == false` is also **true** (V's value is `false`), and `#if V == true` is **false**.

The `##if` / `##else` / `##endif` forms (double-hash prefix) provide the same conditional logic inside **emitter bodies**; see §14.4.2. They support the same expression syntax as `#if` (symbols, comparisons, `&&`, `||`, `!`). They are evaluated at emit time and are not valid in ordinary Beguile source.

## 3.4 `#beguilerSettings`

The `#beguilerSettings` block configures the transpiler and the downstream Inform 6 invocation. Multiple `#beguilerSettings` blocks are allowed; properties follow **first-writer-wins** semantics (the first block to set a property wins; later blocks are ignored for that property), except for `includePaths`, which is **additive**, so every occurrence adds a directory to the search path.

```bgl
#beguilerSettings {
    target         = Z5;           // bare form
    target         = eTarget.Z5;   // qualified form - equivalent
    informName     = "inform6";
}
```

### Toolchain paths

These settings tell the transpiler where to find external tools. They are not written to the generated output.

| Setting        | Type   | Default      | Description                                                                                                                                                                                        |
| -------------- | ------ | ------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `informPath`   | string | none         | Full path to the Inform 6 compiler binary. Takes precedence over `informName`.                                                                                                                     |
| `informName`   | string | `"inform"`   | Filename of the Inform 6 binary (looked up adjacent to the `beguiler` binary). Use `"none"` to skip the I6 handoff entirely. CLI `-inform=` overrides this.                                        |
| `beguiLibPath` | string | `"beguiLib"` | Path to the Beguile Language Runtime language extensions directory. Overrides the default binary-adjacent search.                                                                                  |
| `includePaths` | string | none         | Adds a directory to the include search path for both `#include` and `#includeI6`. May be specified multiple times (additive). Duplicates are ignored. CLI `-includepaths=` also adds to this list. |

### Compilation settings

These settings control the compilation target and output characteristics.

| Setting              | Type           | Default    | Description                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| -------------------- | -------------- | ---------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `target`             | `eTarget`      | `Glulx`    | Compilation target: `Glulx`, `Z5`, or `Z8`. Either the bare enum value (`target = Z5;`) or the qualified form (`target = eTarget.Z5;`) is accepted.                                                                                                                                                                                                                                                                                                                     |
| `outputPath`         | string         | `"output"` | Directory for the compiled intermediate I6 source file, debug files, and story file. Relative paths are resolved from the source file's directory. CLI `-o` overrides this.                                                                                                                                                                                                                                                                                                   |
| `release`            | int            | `0`        | Sets the story release number. `0` means unset.                                                                                                                                                                                                                                                                                                                                                                                                                               |
| `errorFormat`        | `eErrorFormat` | `E1`       | Error reporting style passed to the I6 compiler. `E1` = Microsoft-style; `E2` = Macintosh-style.                                                                                                                                                                                                                                                                                                                                                                              |
| `omitUnusedRoutines` | bool           | `true`     | When `true` (the default), emits the I6 `!% $OMIT_UNUSED_ROUTINES=1` setting, so routines that are never referenced (and whose address is never taken) are dropped from the story file. Set `false` for debug builds to keep all routines.                                                                                                                                                                                                                                    |
| `economy`            | bool           | `false`    | When `true`, auto-compresses story text: after transpiling, the compiler computes the optimal set of I6 text abbreviations (an `inform6 -u` pre-pass), injects them at the top of the generated `.inf`, and compiles the real pass with `-e` (economy mode). Useful headroom on the Z5 256 KB ceiling. **Opt-in and non-intrusive:** if your source already defines its own `Abbreviate` directives, auto-abbreviation is skipped entirely; your hand-tuned set is respected. |

### Runtime settings

These settings affect the generated code.

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `framePoolSize` | int | `64` | Number of slots in the Z-machine local-variable overflow pool. Active only on Z5/Z8 targets. See §9.2.2. |
| `rewritePaths` | bool | `true` | When `true` (the default), path separators (`/` and `\`) in all file path settings and `#include`/`#includeI6` paths are rewritten to the OS path separator at parse time. Set to `false` to disable this normalization. |

### Game metadata settings

These settings carry game identity information. They feed blorb packaging directly (see §3.4.1) and can be surfaced as Beguile constants; see §3.4.2.

| Setting          | Type                                                                                                                                                          | Default | Description                                                                                                                                                                               |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------- | ----------------------------------------------------------------------------------------- |
| `title`          | string                                                                                                                                                        | `""`    | Game title (iFiction `<title>`                                                                                                                                                            |
| `author`         | string                                                                                                                                                        | `""`    | Game author (iFiction `<author>                                                                                                                                                           |
| `headline`       | string                                                                                                                                                        | `""`    | Subtitle or tagline, e.g. `"An Interactive Mystery"` (iFiction `<headline                                                                                                                 |
| `genre`          | string                                                                                                                                                        | `""`    | Genre, e.g. `"Fantasy"`, `"Mystery"`, `"Science Fiction"` (iFiction `<genr                                                                                                                |
| `description`    | string                                                                                                                                                        | `""`    | Blurb text; may contain line breaks; max ~2400 chars (iFiction `<descripti                                                                                                                |
| `language`       | string                                                                                                                                                        | `""`    | ISO-639 language code, e.g. `"en"`, `"en-US"` (iFiction `<langu                                                                                                                           |
| `series`         | string                                                                                                                                                        | `""`    | Series name (iFiction `<se                                                                                                                                                                |
| `seriesNumber`   | int                                                                                                                                                           | `0`     | Position in series; requires `series` to be set (iFiction `<seriesn                                                                                                                       |
| `firstPublished` | string                                                                                                                                                        | `""`    | Publication date: `"YYYY"` or `"YYYY-MM-DD"` (iFiction `<firstpub                                                                                                                         |
| `forgiveness`    | string                                                                                                                                                        | `""`    | Difficulty: `"Merciful"`, `"Polite"`, `"Tough"`, `"Nasty"`, or `"Cruel"` (iFiction `<forg                                                                                                 |
| `release`        | int                                                                                                                                                           | `0`     | Story release number. `0`                                                                                                                                                                 |
| `ifid`           | strin Treaty of Babel IFID in UUID format (e.g. `"A0B1C2D3-E4F5-6789-ABCD-EF0123456789"`). Auto-generated deterministically if no explicit IFID is provided.  |

All iFiction fields are included in the blorb `IFmd` chunk when blorb packaging is enabled. `<title>` and `<author>` default to "Untitled" / "Anonymous" if not set (Treaty of Babel requires both).

When an `ifid` is set (explicitly or auto-generated), the compiler embeds it in the compiled story file following the Treaty of Babel convention:

```i6
Array UUID_ARRAY string "UUID://A0B1C2D3-E4F5-6789-ABCD-EF0123456789//";
#Ifdef UUID_ARRAY;#Endif;
```

The `UUID_ARRAY` string is searchable in the raw Z-code or Glulx binary by babel-compatible tools (IFDB, the `babel` CLI, interpreters like Lectrote and Gargoyle). The `#Ifdef` suppresses the I6 "declared but not used" warning.

When blorb packaging is enabled, the IFID, title, author, and headline are also written into an iFiction XML metadata record (`IFmd` chunk) inside the blorb file, making the game discoverable on IFDB and other cataloging services.

#### IFID auto-generation and persistence

When `generateBlorb = true` and no explicit `ifid` is provided, the compiler automatically generates a stable IFID:

1. **Deterministic generation** - a UUID v5 is derived from the source filename, author, and title. The same inputs always produce the same IFID, so even if the generated file is deleted, recompilation produces an identical IFID.
2. **Persistence** - the generated IFID is written into `_blorbAssets.bgl` as a `#beguilerSettings { ifid = "..."; }` block. On subsequent builds, this value is read back through normal settings parsing.
3. **Priority** - an explicit `ifid` in the user's source file always takes priority (first-writer-wins). The persisted value in `_blorbAssets.bgl` acts as a fallback.
4. **Stability** - per the Treaty of Babel, the IFID must never change once published. The deterministic generation and persistence mechanisms ensure this across builds, even if intermediate files are regenerated.

All other metadata settings (`title`, `author`, `headline`, `release`, etc.) are not auto-emitted to I6 output. To expose them as I6 constants (e.g. for library integration) you can map these in Beguile source; see §3.4.2. Library bindings handle the mapping to library-specific constant names (e.g. `const string story = #beguilerSettings.title;` for the I6 standard library).

### 3.4.1 Blorb packaging

When `generateBlorb = true`, the transpiler runs two additional steps:

1. **Before parsing** - scans `blorbAssetPath` for image and audio files, writes `_blorbAssets.bgl` containing a single `eAssets` enum. Add `#include "_blorbAssets.bgl"` to your source once after the first run generates it.
2. **After I6 succeeds** - assembles a `.zblorb` (Z-Machine) or `.gblorb` (Glulx) file containing the story file and all discovered assets.

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `generateBlorb` | bool | `false` | Master switch. Both steps are skipped unless this is `true`. |
| `blorbAssetPath` | string | `"assets"` | Directory to scan for assets, relative to the source file. |

Asset files are discovered non-recursively. PNG, JPG, and JPEG files become picture resources; AIFF/AIF files become sound resources. Member names in `eAssets` are derived from the filename: stem in camelCase with a capitalized extension suffix (`priest.png` → `priestPng`, `theme.aiff` → `themeAiff`). IDs are globally sequential starting at 1, pictures first then sounds, sorted alphabetically within each group.

```bgl
#beguilerSettings {
    generateBlorb  = true;
    blorbAssetPath = "assets";   // default - may be omitted
    story          = "Unwelcomed";
    author         = "Jim Fisher";
    release        = 1;
}
```

### 3.4.2 Referencing settings values in Beguile source

Any `#beguilerSettings` property can be read as a compile-time expression using the syntax `#beguilerSettings.propertyName`. The result is a string, integer, or enum value literal substituted inline. Enum-typed properties (such as `target`) return the value name as a string.

```bgl
const string story    = #beguilerSettings.title;
const string author   = #beguilerSettings.author;
const string headline = #beguilerSettings.headline;
const int    ver      = #beguilerSettings.release;
```

Defining a value in `#beguilerSettings` and referencing it with `#beguilerSettings.propertyName` keeps a single source of truth for values that are both packaged as metadata (blorb) and used in compiled code, avoiding a duplicated literal. Both this and a direct `const string story = "My Game";` produce identical I6 output.

Supported properties:

| Category | Property | Type |
|----------|----------|------|
| **Metadata / iFiction** | `title`, `author`, `headline`, `genre`, `description`, `language`, `series`, `firstPublished`, `forgiveness`, `ifid` | string |
| | `release`, `seriesNumber` | int |
| **Compilation** | `target`, `informName`, `outputPath`, `serial` | string |
| | `framePoolSize` | int |
| **Toolchain** | `informPath`, `beguiLibPath`, `includePaths` | string |
| **Blorb** | `blorbAssetPath` | string |
| | `generateBlorb`, `rewritePaths` | bool |

> **Note:** The standard library and PunyInform bindings automatically declare the I6 constants `story` and `headline` from `#beguilerSettings.title` and `#beguilerSettings.headline` respectively, using a top-of-file `#emitfirst` block with `##beguilerSettings.<key>` substitution (see §3.5.6) so the constants are in scope before the binding's `#includeI6` of the I6 library. The `release` and `serial` settings are emitted directly by the compiler as I6 `Release` and `Serial` directives. When using either binding, you do not need to declare these constants yourself.

## 3.5 Diagnostic and Control Directives

### 3.5.1 `#message`

Prints a string literal to the terminal during compilation. The message is written to standard output and does not affect the generated output or compilation result.

```bgl
#message "Loading custom library..."
```

Useful for progress notes or tracing include chains during development.

### 3.5.2 `#warning`

Emits a user-defined warning message in the standard compiler warning format (including file name and line number) and continues compilation.

```bgl
#if !FEATURE_XYZ
    #warning "FEATURE_XYZ is not defined - some functionality will be disabled."
#endif
```

### 3.5.3 `#error`

Halts compilation with a user-defined error message. The message is reported in the standard compiler error format, including file name and line number, and is indistinguishable in appearance from a parser error.

```bgl
#if !PLATFORM_DEFINED
    #error "You must define PLATFORM_DEFINED before including this file."
#endif
```

### 3.5.4 `#exit`

Stops processing the current file immediately, as though end-of-file had been reached. Any open directive nesting (`#if`/`##ifdef` blocks) accumulated in that file is discarded cleanly. Code nesting (open `{` blocks in parsed Beguile source) is not affected, so `#exit` should only be used at the top level of a file.

```bgl
#once
#if !FEATURE_ENABLED
    #exit
#endif
// ... feature implementation follows ...
```

`#exit` can be useful in library files that want to skip their body entirely when a required symbol is not defined.

### 3.5.5 `#startup`

Registers a block of I6 code to be executed at program startup. The body is emitted inside the generated `bglInit()` routine, before any global variable initializers.

```bgl
#startup {
    _initializeStringBuffers();
}
```

The standard IF library bindings (`i6StandardLibrary` and `punyInform`) call `bglInit()` automatically, so users of those bindings do not need to call it themselves. Only games built without a standard binding need to call `bglInit()` explicitly in their starting routine.

**Deduplication.** Each source file contributes its `#startup` blocks at most once, regardless of how many times the file is included. This means `#startup` is safe in library files that do not use `#once`.

**Ordering.** When multiple files each declare a `#startup` block, the blocks are emitted in file-inclusion order (the order the compiler first encounters each file). Startup blocks from all files run before any global variable init emitters.

`#startup` is primarily intended for low-level library code that must initialize runtime infrastructure before any user objects are constructed. User code should prefer placing initialization logic in object init emitters or the game's starting routine.

### 3.5.6 `#emitfirst`

Emits a block of raw I6 code at the beginning of the generated output, after ICL headers but before the `bglInit` routine and all other declarations. This is useful for I6 directives that must appear early in the file, such as `Replace` directives, banner constants (`Story`, `Headline`), or conditional compilation setup.

```bgl
#emitfirst {
    Replace DrawStatusLine;
}
```

**Settings substitution.** Inside the raw I6 body, the marker `##beguilerSettings.<key>` expands at parse time to the compile-time value of the named `#beguilerSettings` property. String properties expand to I6 quoted strings (`"…"`); integer properties expand to a decimal literal. The `##` prefix is the same convention used for `##if`/`##ifdef` inside emitter bodies; it marks Beguile-level processing inside an otherwise-raw-I6 context. The property names are the same set documented in §3.4.2.

```bgl
#emitfirst {
    Constant story    = ##beguilerSettings.title;
    Constant headline = ##beguilerSettings.headline;
}
```

This is the canonical pattern bindings use to expose library-required constants. I6 libraries that consume named constants (e.g. `Story`, `Headline`) read them *during* their own include processing, so the constants must be in scope at the top of the generated `.inf`. `#emitfirst` provides that placement guarantee, and the `##beguilerSettings.<key>` substitution lets the binding pull the value out of `#beguilerSettings` without the user having to re-declare it.

**Deduplication** and **ordering** follow the same rules as `#startup`; each file's block is emitted at most once, in file-inclusion order.

### 3.5.7 `#storedEmitFirst <name>` and `#storedEmitLast <name>`

> This is an advanced directive intended for library (BLR) authors; most programs never use it directly.

Registers a **named, deferred** raw-I6 block. Unlike `#emitfirst` / `#emitlast`, a stored block is NOT emitted by default; however, it emits only if some `##triggerEmitter <name>` annotation fires during compilation. The block, when emitted, lands at the same position as the corresponding non-stored form (top for `#storedEmitFirst`, bottom for `#storedEmitLast`).

```bgl
#storedEmitFirst scratchSupport {
    Array  _bglScratchStack --> 33;
    Global _bglScratchTop = 0;
    [ _bglScratchPush v; … ];
    [ _bglScratchMake count; … ];
}
```

The trigger annotation `##triggerEmitter <name1> <name2> …` is placed inside the body of the construct that *uses* the helper (a compiler builtin template or a Beguile emitter body). Each invocation of the host construct adds the listed names to the fired set; matching stored blocks are emitted once at finalize time. Multiple stored blocks can share a single trigger name, and a single host construct can list multiple trigger names.

The same `##beguilerSettings.<key>` substitution that works in `#emitfirst` also applies here.

**Use case.** Helper routines that some BLR feature needs but most programs don't. Programs that never trigger the helper pay zero bytes; programs that do get the routine emitted exactly once.

**Re-include protection.** Stored blocks are keyed by name; re-registering the same name (e.g. via re-included BLR) is harmless overwrite (latest wins). `#once` on the declaring BLR file is the normal way to prevent shadowing within a single include chain.

### 3.5.8 `#emitlast`

Emits a block of raw I6 code at the very end of the generated output, after all other declarations. This is useful for I6 code that must appear after grammar directives, object definitions, or other late-emitted constructs.

```bgl
#emitlast {
    [ DrawStatusLine; ! replacement routine
        ! ...
    ];
}
```

Supports the same `##beguilerSettings.<key>` substitution as `#emitfirst`. **Deduplication** and **ordering** follow the same rules as `#startup`.

### 3.5.9 `#using`

Imports the members of a class or object into the current file's scope, allowing them to be referenced without qualification:

```bgl
emitter class myPlatform {
    int wordsize { WORDSIZE }
}

#using myPlatform

void main() {
    int ws = wordsize;    // resolves to myPlatform.wordsize → WORDSIZE
}
```

Without `#using`, the full path `myPlatform.wordsize` is required.

**Scope**: `#using` is file-scoped, active from the directive to end of file. It does not leak into included files, and `#using` in an included file does not affect the includer. Each file declares its own imports.

**Resolution priority**: Imported names have lower priority than locals, parameters, class/object members, and globals. If a global variable has the same name as an imported member, the global wins and the compiler issues a warning:

```
warning: global 'wordsize' shadows imported 'myPlatform.wordsize'; use 'myPlatform.wordsize' to access the import
```

**Non-existent target**: If the `#using` target is not a declared class or object, the compiler issues a warning and the directive is ignored. This allows `#using` to appear before the target is declared in a later `#include`, without halting compilation:

```
warning: #using 'myLib': not a declared class or object; directive ignored
```

**Ambiguity**: If two `#using` imports both declare a member with the same name, using that name without qualification is a compile error:

```
error: 'val' is ambiguous - found in both 'libA' and 'libB'; qualify explicitly
```

**What can be imported**:

| `#using` target | What's imported | How it resolves |
|----------------|-----------------|-----------------|
| Emitter class | Value emitters, emitter functions | Body expanded inline |
| Object | Methods, properties, type aliases | Resolved through the object path |
| Static members | Static variables | Resolved as a static member reference |

`#using` a regular class with only non-static instance members is an error; instance methods require a receiver.

**Type alias imports**: When an object contains type aliases (see §5.11), `#using` imports them as bare type names. For example, given `object _bglGlulx { alias window for glulxWindow; }` on `bgl.glulx`:

```bgl
#using bgl.glulx
window myWin;           // resolves to glulxWindow via alias

#using bgl
glulx.window myWin;     // resolves through partial path
```

Both forms resolve through the same alias mechanism; `#using` controls how much of the namespace path can be omitted.

---

# Chapter 4 - Types

## 4.1 Overview

Beguile is statically typed. Every variable, parameter, and return value has a declared type that is known at compile time. Types fall into four categories: primitive types, pseudo-types, user-defined types (classes and enums), and the `var` escape type.

## 4.2 Primitive Types

All primitive types in this table are part of the auto-loaded runtime library (Chapter 15) and require no `#include`; however, `uint` is opt-in via `<uint>`.

| Type | Auto-loaded | Description |
|------|---|---|
| `int` | ✓ | Signed integer value. Maps to the native I6 integer. |
| `uint` |   | Unsigned integer value. Same bit pattern as `int`, but comparison and division operators use unsigned semantics. Requires `#include <uint>`. See §4.3. |
| `float` |   | IEEE 754 single-precision floating-point. **Glulx only** (the Z-machine has no floats); available in core with no include when targeting Glulx. Constructed by cast. See §4.4. |
| `bool` | ✓ | Boolean value (`true` / `false`). |
| `char` | ✓ | A single ZSCII character value. The `<char>` extension (§16.2.2) adds case-conversion and inspection methods. |
| `string` | ✓ | A string value. Auto-loaded `string` provides print, equality, and literal assignment. The `<string>` extension (§16.2.4) adds mutable string operations backed by a runtime pool. |
| `object` | ✓ | The base class for all world objects in the IF model. |
| `verb` | ✓ | An alias class for declaring verb instances. See Chapter 13. |
| `void` | ✓ | Not a value type. A return-type specifier indicating a function returns no value. |

> **Note for I6 authors.** `object`, `string`, `array`, and `verb` are Beguile *types* here. They share names with Inform 6 constructs and compile to them, but are used with Beguile syntax and typing. See §2.8 for the full set of shared names.

## 4.3 The `uint` Type

The `uint` type is an unsigned integer. It shares the same underlying bit pattern as `int`, but its comparison operators route through `_bglMath.unsignedCompare` to produce correct unsigned ordering.

`uint` is not part of the auto-loaded core library. To use it, include the language extension:

```bgl
#include <uint>
```

### Declaration and Initialization

```bgl
uint x = 12;       // intLiteral coercion
uint y = (uint)n;  // explicit cast from int
int z = (int)y;    // explicit cast back to int
```

### Operators

- **Arithmetic** (`+`, `-`, `*`) - identical to `int` (passthrough to native I6).
- **Division** (`/`) and **modulo** (`%`) - route through `_bglMath.unsignedDiv` / `_bglMath.unsignedMod` for correct unsigned semantics. When both operands are non-negative the fast path uses native `/` or `%`.
- **Bitwise** (`&`, `|`, `^`, `<<`, `>>`) - identical to `int`.
- **Equality** (`==`, `!=`) - direct passthroughs (identical for signed/unsigned bit patterns).
- **Relational** (`<`, `<=`, `>`, `>=`) - use `_bglMath.unsignedCompare` from `math.bgl` for correct unsigned ordering.
- **Unary minus** - not supported on `uint`.

### Print

`print(uint)` correctly displays large unsigned values, including those with the sign bit set (values ≥ 2^15 on Z-machine or ≥ 2^31 on Glulx). The implementation uses unsigned digit extraction so that, e.g., `print((uint)-1)` outputs `65535` (Z-machine) or `4294967295` (Glulx) rather than `-1`.

---

## 4.4 The `float` Type

`float` is an IEEE 754 single-precision floating-point number. It is a **Glulx-only** type: the Z-machine has no floating-point opcodes, so `float` is available only when the target is Glulx. It is part of core and needs no `#include`; a Glulx game that never uses `float` pays nothing for it.

A `float` occupies a single native word, the same width as `int`, and its operators reinterpret that word as a float. Because an `int` and a `float` that share a bit pattern are unrelated values, a `float` must be constructed by casting, not by assigning an integer literal:

```bgl
float pi = (float)314 / (float)100;   // 3.14
float x  = 5;                          // ERROR: an int literal is not a float
float y  = (float)5;                   // correct
```

The bare `float x = 5;` is rejected on purpose: the bit pattern of the integer `5` is a tiny denormal, roughly `7e-45`, almost never what the author meant.

`float` supports the arithmetic operators `+` `-` `*` `/` `%` and their compound-assignment forms, the comparisons `==` `!=` `<` `<=` `>` `>=`, casts between `int` and `float` (`(float)n`, `(int)f`), and `print(float)`.

```bgl
#beguilerSettings { target = Glulx; }

void main() {
    float a = (float)5;
    float b = (float)2;
    print(a / b);        // 2.5
    if(a > b) print("^bigger^");
    int n = (int)a;      // 5
}
```

---

## 4.5 Literal Pseudo-Types

Literal pseudo-types are not declared by user code. They are inferred automatically by the compiler when a literal value appears in an expression, and they participate in operator overload resolution independently of their corresponding runtime types.

| Pseudo-type | Example | Notes |
|-------------|---------|-------|
| `intLiteral` | `42` | Compatible with `int` via `operator =` on the `int` class |
| `negativeIntLiteral` | `-1` | A negative integer literal. Converts to `int` implicitly but requires an explicit `(uint)` cast for unsigned assignment. See below. |
| `stringLiteral` | `"hello"`, `@"raw"` | Compatible with `string` via `operator =` on the `string` class; both regular and raw string literals share this pseudo-type |
| `charLiteral` | `'a'` | Compatible with `char` via `operator =` on the `char` class |
| `dictionaryWordLiteral` | `.cloak`, `..cloaks` | Dictionary word; plural form (`..`) sets an internal plural flag |
| `interpolatedStringLiteral` | `$"hello {x}"` | An interpolated string (see §2.5.4); expands to a block of statements rather than a single expression |

Literal pseudo-types are compatible with their corresponding runtime types exclusively through declared operators, specifically `operator =` on the target class. There is no built-in compatibility rule for them; the operator makes this distinction transparent to the user.

`interpolatedStringLiteral` is unique among the literal pseudo-types: because an interpolated string contains multiple segments that emit as separate statements, it cannot be reduced to a single expression value. Instead, an interpolated-string argument emits all of its segments as a block of statements rather than a single value. This enables `print($"...")` and the extended syntax implemented by the `string` language extension: `string s = $"..."`. See §16.2 for details on language extensions.

Literal pseudo-types are first-class types: they are declared as `extern class` in the core library and can be extended to have operators and methods defined against them. This means method calls are valid directly on literal values, if defined in an emitter:

```bgl
"hello".print();       // calls print() on stringLiteral
42.someMethod();       // calls someMethod() on intLiteral
'x'.someMethod();      // calls someMethod() on charLiteral
```

### `negativeIntLiteral`

When a negative integer literal appears in an expression (e.g., `-1`, `-42`), the compiler assigns it the pseudo-type `negativeIntLiteral` rather than `intLiteral`. This type converts to `int` implicitly; however, it is **not** implicitly compatible with `uint`: assigning a negative literal to a `uint` variable requires an explicit cast:

```bgl
int x = -5;            // OK: negativeIntLiteral → int
uint u = -1;           // ERROR: negativeIntLiteral not compatible with uint
uint u = (uint)-1;     // OK: explicit cast
```

This prevents accidental sign-related bugs when working with unsigned values while still allowing intentional conversions (e.g., `(uint)-1` for the maximum unsigned value).

## 4.6 `nothing` and `null`

`nothing` is the built-in absence/unset value. It is the same value I6 uses to mean "no object" (numerically 0) and Beguile exposes it under that name throughout the language. It has the resolved type `object` but is compatible with any type.

```bgl
object o;
if(o == nothing) print("not yet set");

marbleClass m = new marbleClass();
if(m == nothing) print("pool exhausted");
```

`null` is an accepted synonym for `nothing` for users coming from C-family languages. The two are interchangeable; Beguile emits both as I6's `nothing`.

```bgl
object o = null;          // identical to: object o = nothing;
if(o == null) ...         // identical to: if(o == nothing) ...
```

For object references, `nothing` represents the absent/unset state (pool exhaustion, an uninitialized property, a missing parent, etc.). For integers, `nothing` is the value `0`. Comparisons against `nothing` are the canonical check for "this allocation/reference didn't succeed."

## 4.7 Enumerations

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

Individual members may be assigned explicit integer values, including negative values. Auto-numbering resumes by incrementing from the last assigned value.  It is valid for two enum values to be assigned the same value; however, such cases will be indistinguishable at runtime.

```bgl
enum myPhase {
    setup  = 0,
    play   = 10,
    ending          // auto: 11
}

enum direction {
    here = 0,
    up   = -1,      // negative values allowed
    down            // auto: 0 (same as `here` - legal but indistinguishable)
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

Individual members may be assigned an explicit starting value; auto-assignment resumes by doubling from the last assigned value. Explicit values must be a power of 2 (the compiler reports an error otherwise) and must be non-negative (since bnums are unsigned bit patterns).

For Z-code, the maximum number of unique values in a `bnum` is 16; for Glulx this cap is 32. 

#### Shared-base bnums

A bnum may declare a common base bnum to indicate that it occupies a sub-field of the same packed integer:

```bgl
bnum winMethodFlags { }                      // empty marker base - defines the bit-field space

bnum windowPlacement : winMethodFlags {
    left  = 0, right = 1, above = 2, below = 3     // packed 2-bit sub-field
}
bnum windowScale : winMethodFlags {
    fixed = 16, proportional = 32
}
bnum windowBorder : winMethodFlags {
    border = 0, noBorder = 256
}
```

The base must itself be a declared `bnum` (plain `enum` cannot participate). Shared-base bnums relax the power-of-2 rule for explicit values, since children of a packed composite occupy sub-fields whose bit patterns need not be individual bits. `0` remains a legal value in any bnum.

**Combining values**: bitwise `|`, `&`, and `^` between two bnums are permitted only when the operands share a common bnum ancestor (including one being the other's ancestor, or both being the same type). The result types as the shared ancestor. Bnums with no common base cannot be combined; this is a compile-time error.

```bgl
int winMethod = left | fixed | noBorder;     // OK: all share winMethodFlags
int bad       = left | itemFlag.portable;    // ERROR: no common base
```

#### Widening to `int`

Any bnum value converts implicitly to `int` (bnums are integer bitmasks by definition). The reverse direction, `int` to a specific bnum, requires an explicit cast. Enums do **not** widen; enum-to-int requires an explicit cast as well.

#### Code generation

Enum and bnum values are **inlined as integer literals** at every use site. No I6 `Constant` declarations are emitted for the named values; `north` compiles to `1`, `fixed` to `16`, etc. This keeps the generated I6 free of unused-constant warnings. `extern enum` values remain as bare names (e.g. `true`, `false`) since those are I6-defined.

### `extern enum` and `extern bnum`

`extern enum` (or `extern bnum`) declares an enumeration whose values are defined in I6, not by Beguile. The declaration registers the names for type-checking purposes only and produces no I6 output.

```bgl
extern enum eBool { true, false }
extern enum eErrorFormat { E1, E2 }
```

`eBool` is Beguile's boolean-result type: comparison operators and `operator ?()` return `eBool`, and its values `true`/`false` interoperate with `bool`. It appears as a return type throughout this document.

Enum and bnum values may be referenced by bare name (`true`, `false`, `north`, `portable`, etc.) or by qualified name (`eBool.true`, `eTarget.Z5`). The qualified form is useful for autocompletion in the IDE and is required when two different enum types define values with the same name.

## 4.8 The `var` Type

`var` is a universal escape type that bypasses static type checking. It is bidirectional: any value can be assigned **to** a `var`, and a `var` can be assigned **to** any type. This mirrors I6's untyped semantics: all values are word-sized and interchangeable at runtime.

```bgl
var x = 5;          // any value → var
int y = x;          // var → int (no type check)
object o = x;       // var → object (developer's responsibility)
```

Because `var` opts out of type checking in both directions, the developer is responsible for ensuring that the underlying value is meaningful in context. The compiler will not catch type mismatches involving `var`.

In overload resolution, overloads with `var` parameters are treated as a fallback: they are only selected if no typed overload matches. This prevents a catch-all `var` overload from shadowing more specific ones.

```bgl
emitter void print(var val)        { print val; }          // fallback
emitter void print(string str)     { print (string)str; }  // preferred for string
emitter void print(stringLiteral s){ print (string)s; }    // preferred for string literals
```

## 4.9 Arrays

`array<T>` declares a typed word array. The element type `T` is mandatory; bare `array` is not a valid type and is rejected by the compiler. `T` can be any base type (`int`, `bool`, `string`, `object`, `char`, `dictionaryWord`) or any user-defined class.

```bgl
array<int> scores[5];                    // sized, zero-initialized
array<int> primes = {2, 3, 5, 7, 11};   // initialized with values
array<Room> visited;                     // user-class element type
```
Global arrays have no element count limit. However, arrays declared as object properties (see §6.5) are constrained by the Z-machine's property size limits: Z5 and Z8 allow up to 32 word-sized elements per property. Byte arrays (`array<char>`) on object properties do not suffer from this limitation.

Array elements are accessed using subscript syntax:

```bgl
int x = scores[2];       // read element 2 - type is int
scores[0] = 99;          // write element 0 - value type must match int
int n = scores.size();   // number of elements reserved for the array
```

`size()` returns the number of elements *allocated* for the array (the capacity reserved at compile time). It does not depend on whether those slots currently hold meaningful values.

Beguile-declared standalone word arrays carry an explicit, runtime-tracked **length** (the count of "in use" entries), distinct from `size()`. Length is set at allocation time (list-initialized arrays start at length = N, sized arrays at length = 0) and is changed only by *explicit* operations: `setLength()`, `clear()`, and the mutators added by the `<array>` extension. Slot writes (`arr[i] = v`) do **not** change length; the array is treated as a buffer-with-cursor. Length is exposed via the `<array>` opt-in extension (§16.2.7) as `length()` / `setLength()`.

The same method surface works for both Beguile-declared arrays and I6-native extern arrays; `length()` on an untracked array falls back to reporting `size()`. The size-vs-length vocabulary matches the convention used across the Beguile runtime library for `<string>`, `<buf>`, and `<array>`.

The element type is enforced at every subscript site. Reading an element produces a value of type `T`, and writing an element requires a value compatible with `T`. Cross-type assignments (e.g. assigning a `string` to an element of `array<int>`) are compile-time errors.

`array<char>` is a byte array: its elements are stored and accessed as bytes rather than words. All other element types are word arrays.

A byte array holds character *or* small-integer data, matching the dual use of I6's `->` byte arrays. Its initializer and element writes accept both character literals and integer values (`array<char> letters = {'H', 'i'}` or `array<char> scores = {5, 10, 15}`, and `scores[i] = 99`). An integer literal outside a byte's range (0..255) is a compile-time error; a non-literal `int` value that exceeds a byte wraps to its low byte at runtime, as any byte assignment would. Reading an element yields a `char`, which widens to `int` freely (a byte is a byte-sized non-negative integer): `int a = scores[i]` and arithmetic or comparison such as `scores[i] + scores[j]` and `scores[i] > threshold` all work, so a byte array is usable numerically without casts. When accumulating many byte values, sum into an `int` (`total += scores[i]`) rather than a `char`, since a `char` result is byte-wide. Printing a `char` prints it as a character; to print its numeric value, read it into an `int` (or cast) first.

### 4.9.1 `rawArray<T>` - raw I6 word arrays

`rawArray<T>` is a typed view over a **bare I6 word array**, one with *no* length header and *no* tracking marker. Where `array<T>` reserves a count slot at index 0 (so `arr[i]` emits `arr-->(i+1)`), a `rawArray<T>` subscript emits the raw form `arr-->i`, indexing directly from word 0.

```bgl
bool ext_parsererror(int etype, rawArray<var> results) {
    if (etype == NOTHING_PE && ((verb)results[0] == PutOn || (verb)results[0] == Insert))
        rtrue("You are not holding one.");
    rfalse;
}
```

Its purpose is **interop**: raw I6 buffers handed to Beguile from the outside (the `results` array of a `parse_error` entry point, an I6 library table, a `-->` array declared in an `#i6` island) are word arrays with no count slot. Receiving one as a `rawArray<T>` parameter lets you index it with ordinary subscript syntax and still emit the correct raw I6.

| | `array<T>` | `rawArray<T>` |
|---|---|---|
| Layout | count at `-->0`, data at `-->1…` | data at `-->0…`, no header |
| `arr[i]` emits | `arr-->(i+1)` | `arr-->i` |
| `size()` / `length()` | reads the count slot | unavailable (a rawArray has no length header; pass the length explicitly) |
| Length tracking | yes | none (it's a plain pointer) |

Because a `rawArray<T>` carries no length, **`for…in` over a `rawArray<T>` parameter is a compile-time error**: there is no count to bound the loop and no marker to probe at runtime. Iterate with an explicit indexed loop bounded by a length you know:

```bgl
for(int i in 0 to n - 1) { process(buf[i]); }
```

Elements are still type-checked at every subscript site, and an element may be cast (e.g. `(verb)results[0]`) exactly like any other value; the cast resolves through the following operator so `(verb)results[0] == PutOn` is a `bool` usable in `&&`/`||`.

#### File-scope `rawArray<T>` literals - untracked `array<T>`

The layout above describes a `rawArray<T>` **parameter**, a view over an external buffer with no count word. A **file-scope `rawArray<T>` literal** is a different (and complementary) use: it is an **untracked `array<T>`**. It emits a plain I6 `table` (count at `-->0`, data at `-->1…`, subscript `arr[i]` → `arr-->(i+1)`) exactly like `array<T>`, but with **no** `<len>+<magic>` tracking trailer, *even when `#include <array>` is active*:

```bgl
#include <array>
array<string>    trk = { "a", "b", "c" };   // tracked:   Array trk table "a" "b" "c" 3 $9084;   (word0 = count+2)
rawArray<string> raw = { "a", "b", "c" };    // untracked: Array raw table "a" "b" "c";           (word0 = count)
```

This is the shape to use when a **bare I6 array API** must read the array by its count word, e.g. orLibrary's `util.orArray` single-array form (`getSize(arr)` → `arr-->0`, `get(arr, n)` → `arr-->(n+1)`), used for a debug walkthrough command list. Because `<array>`'s tracked layout puts `count+2` at word 0, a plain `array<T>` would over-count by two; the `rawArray<T>` literal keeps word 0 as the true count.

The two file-scope forms are **not interchangeable across the parameter boundary**: a `rawArray<T>` *literal* is count-prefixed (`-->(i+1)`), while a `rawArray<T>` *parameter* is data-only (`-->i`). Passing a literal to a `rawArray<T>` parameter would read the count word as element 0; however, the type system keeps them separate (a literal has the `array` element-covariant type; pass it where an `array<var>` is expected, as the `util.orArray` bare-array overloads do).

### 4.9.2 Local array lifetime - returned local arrays are *ephemeral*

A **local** array (one declared inside a routine body, whether sized `array<int> tmp[5];` or list-initialized) is allocated per-call from the frame pool at routine entry and **freed at routine exit**; this is what makes recursion safe (each call gets its own slice; see §9.2.2). File-scope arrays are not affected: they live in permanent storage and may be returned freely.

Because a local array's storage is reclaimed when the routine returns, **a returned local array is a transient (ephemeral) reference, not an owned value**, exactly like an ephemeral string. The value it points at has already been freed by the time the caller sees it:

```bgl
array<int> build() {
    array<int> tmp[3];
    tmp[0] = 1; tmp[1] = 2; tmp[2] = 3;
    return tmp;            // ← returns a reference to tmp's now-freed slice
}
```

**To keep a returned local array, assign it to a typed local, which copies it** into stable storage (value-semantic copy-on-assign, the same capture mechanism used for ephemeral strings):

```bgl
array<int> keep = build();   // ← snapshots the ephemeral result into `keep`
int n = keep[0];             // safe: independent local copy
```

The free is *logical* (the frame slot is not zeroed), so an ephemeral read often *appears* to work because the data still lingers in the reclaimed slot. **Failures are therefore intermittent**: they only surface when an intervening allocation reuses that slot (for example passing the reference straight into another call that itself allocates, or consuming a returned local array in an expression without first assigning it). Don't rely on the lingering data; capture with an assignment before use.

This applies to chained/`<array>` results too; see the local-array source caveat in §16.2.7.

# Chapter 5 - Classes

> **Emitters and `$` tokens, in brief.** Some of the class members shown in this chapter are *emitters*: methods or operators whose body is raw Inform 6 that the compiler inlines at each call site rather than calling as a routine. Inside an emitter body, `$`-prefixed tokens are substituted at that site: `$self` is the receiver (the object the member was called on), `$val` is the receiver's value (identical to `$self` except when the receiver is a property access), `$target` is the left-hand side of an assignment, and `$paramName` is a named parameter. That is enough to read the examples below; emitters are covered in full in §14.4.

## Declaration Qualifiers

Declarations may be preceded by qualifier keywords: `replace`, `explicit`, `extern`, `emitter`, `const`, `static`, `inline`, `extend`, `alias`, `default`, `superposed`, and `typesealed` (§5.2.7). Qualifiers may appear in **any order**, so `emitter replace void foo()` and `replace emitter void foo()` are equivalent. The compiler validates invalid combinations:

- `explicit` without `operator()` - error (explicit is only valid on conversion operators)
- `const` + `static` - error
- `static` + `emitter` - error
- `explicit` + `const` or `static` - error
- `alias` + `extern` - error
- `alias` + `emitter` - error
- `default` in an object or verb instance body - error (only valid in class declarations)

`inline` is a **class-member** modifier: it marks a member variable as a positional slot for inline object construction (`Type{ v1, v2 }` — §6.2.1). It affects only how positional values map to members and has no runtime effect; it is meaningful only on a member variable declaration inside a class body.

## 5.1 Defining a Class

A class defines a new type in Beguile.  It groups related data and behavior. Class members may be variable properties, methods, or emitters.

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

A class declaration requires a unique type name which is used to instantiate instances of that type. 

## 5.1.1 Type Parameters

A class may declare a type parameter after its name in angle brackets:

```bgl
class Box<T> : object {
    T payload;
    int weight;
}
```

The parameter (here `T`) is a name scoped to the class body. Member declarations may use it wherever a type is expected: return types, parameter types, member-variable types. At use sites, the binding is supplied:

```bgl
Box<Room> roomBox;          // T binds to Room
Box<int>  scoreBox;          // T binds to int
```

Method dispatch on a typed instance substitutes the binding through every `T` in the relevant member's signature. So `roomBox.payload` is typed `Room`, `roomBox` accepts `Room`-typed assignments, and incompatible writes are compile-time errors. The substitution is purely static: no runtime cost and no I6 type information.

Restrictions:

- Single type parameter only (`<T>`, not `<K, V>`). Multi-parameter declarations parse but only the first binds.
- The parameter is not registered as a global type, so it cannot collide with same-named instances elsewhere (e.g. a global `Temperature t;`). It exists only inside its declaring class.
- `extend class Foo<…>` and `alias class Foo<…>` are rejected; type parameters belong to the original declaration.
- Use-site binding is supported in declarations (`Box<int> b;`) but not in inheritance position, where the class-name form is used (`class byteArray : array<char>`).

The standard library's `array<T>` is the primary client of this mechanism; see §4.9.

## 5.2 The Four Class Forms

Beguile has four class declaration forms. Each is introduced by a different keyword combination and carries distinct rules about what members are allowed and what I6 is generated.

| Form | Syntax | I6 class emitted | Members allowed | Used for |
|---|---|---|---|
| Normal class | `class Foo` | Yes | Variables (with or without values), emitter methods, regular methods | Normal type definitions |
| Extern class | `extern class Foo` | No | Typed variable declarations (initializers allowed but ignored in most cases), emitter methods | Declaring types which are defined externally to Beguile, in I6 includes  |
| Emitter class | `emitter class Foo` | No | Emitter methods only (`emitter` keyword optional) | Groups inline emitter functionality under a conceptual class |
| Alias class | `alias class Foo for Parent` | No | Typed variable declarations (no values), emitter methods | Defines a pseudo type, really another type but with predefined member types |

Normal, `extern`, and `emitter` classes support optional inheritance via `: Parent` (see §5.6). Alias classes use `for Parent` to specify the underlying type they map to (see §5.2.3); this is not inheritance but a type-aliasing relationship. All forms support extension via `extend class` (see §5.7). Alias classes require exactly one parent; extern/alias classes do not allow non-emitter methods.

The qualifiers can be combined. The most common combination is `extern emitter class`, used in the core library for primitive-type wrappers like `int`, `bool`, `char`, and `string`. The type already exists in I6 (every primitive is just a word slot, the dictionary maps to a specific I6 type), so `extern` suppresses the I6 class declaration; `emitter` says the wrapper is purely a type label and operator host, so instances have no I6 object backing and emit as plain `global X;` rather than as I6 `Object` directives. See §5.2.3 for the emission rule. A `byVal class Foo` qualifier marks a normal class as value-semantic for parameter passing; see §5.2.6.

## 5.2.1 Normal Classes

A normal class (no qualifier) is the standard Beguile class. It generates an I6 `Class` declaration and supports the full range of members: variables with initializers, non-emitter methods with Beguile statement bodies, and emitter methods with raw I6 bodies.

```bgl
class Animal : object {
    string short_name;
    const int maxAge = 20;

    void speak(){}

    emitter bool operator == (Animal v){ $val == $v }
}
```

Normal classes are the most common form and are used for game-specific types, data structures, and behavioral hierarchies.

#### Typical use

Normal classes define game-specific types that need both data and behavior. A `Room` class might declare `description` and `short_name` properties and provide methods for custom behavior. Objects instantiated from normal classes inherit both the properties and methods, and can override methods using `replace`.

```bgl
class Room : object {
    string short_name;
    string description;
}

class Dog : Animal {
    replace void speak(){ print("Woof!"); }
}
```

## 5.2.2 `extern class`

`extern class` declares a class that is implemented in I6. Beguile uses the declaration for type-checking and emitter dispatch only; no I6 class definition is generated.

```bgl
extern class object {
    parentProp parent;
    attributeList attributes;
    emitter void give(attribute attr){ give $val $attr }
    emitter eBool has(attribute attr){ $val has $attr }
}
```

Rules for `extern class` members:
- **Emitter methods** are allowed and require the `emitter` keyword.
- **Non-emitter methods** are not allowed; this is a compile-time error.
- **Variable declarations** (type and name only, no `=` initializer) are allowed and contribute to type inference on object instances.
- **Variable definitions** (with `=` initializer) are syntactically valid; however, the initializer value is ignored at runtime and no code is generated for it. The value exists solely as metadata and is invisible to normal Beguile programs. Certain compiler-internal classes (such as `beguilerSettingsType`) treat these initializers as default values for directive properties.

#### Marker form for I6-pooled types

```bgl
extern class object[];
```

The empty brackets `[]` mark the class as I6-pooled with the size determined by the I6 declaration, which Beguile does not claim to know. This unlocks `new ClassName(...)` and `delete instance` against the type for compile-time validation (see §5.2.5 and §10.7). The body may be omitted (`extern class object[];`) or present (`extern class object[] { ... }`), same as a non-marker `extern class`.

`extern class Foo[N]` (with a specific size) is a compile-time error: the pool size belongs to the I6 declaration, and Beguile can't enforce a size it doesn't own.

#### Typical use

Extern classes wrap I6 built-in types so that Beguile's type system can reason about them. The core library uses extern classes for the I6-real `object` type and for library-defined types like `attribute` and `attributeList`. Primitive-type wrappers (`int`, `bool`, `char`, `string`) use the combined `extern emitter class` form (see §5.2.3) so their instances emit as `global X;` rather than as I6 `Object` directives. Emitter methods on extern classes provide typed, operator-overloaded access to I6 operations that would otherwise require raw I6 code.

```bgl
extern class object {
    parentProp parent;
    attributeList attributes;
    emitter void give(attribute attr){ give $val $attr }
}

extern emitter class int : _bglObject {
    emitter int operator + (int v){ $val + $v }
    emitter bool operator == (int v){ $val == $v }
}
```

Here `_bglObject` is the BLR root base class that every Beguile type inherits from (see Appendix A); wrapper types name it as their base to pick up the shared member surface.

## 5.2.3 `emitter class`

`emitter class` declares a class that has no I6 backing at all; it exists purely in Beguile's type system to drive emitter dispatch. No I6 class definition is generated, and no I6 object or variable underlies instances of the type.

```bgl
emitter class celsius {
    fahrenheit operator(){ $val * 9 / 5 + 32 }
    celsius operator = (celsius v){ $target = $v; }
}
```

Rules for `emitter class` members:
- All methods are implicitly emitters. The `emitter` keyword may be included for clarity but is not required.
- Variable properties (declared or defined) are not allowed; this is a compile-time error.

#### Typical use

Emitter classes group related inline I6 operations under a conceptual type without any runtime backing. They are useful for defining unit types (temperature, distance) with conversion operators, or for organizing emitter-only functionality into a namespace-like structure. The `style` emitter class in the core library is an example: it provides `style.italics()` and `style.roman()` as pure inline I6 calls.

```bgl
emitter class style {
    emitter void italics() { style underline; }
    emitter void roman()   { style roman; }
}
```

#### Combined `extern emitter class` - primitive-type wrappers

The core library declares each primitive Beguile type (`int`, `bool`, `char`, `string`, `intLiteral`, `negativeIntLiteral`, `charLiteral`, `stringLiteral`, etc.) as `extern emitter class`. The combination expresses two facts about these types:

- **`extern`** - no I6 class declaration is generated. The underlying type already exists in I6 (every primitive is a word slot).
- **`emitter`** - the wrapper is a Beguile-side type label and operator host only. It has no I6 instance backing of its own. Instances at file scope (`int x = 5;`, `bool b = true;`) emit as **`global X;`**, not as `Object X;`.

This is the discriminator the compiler uses at instance-emission time: classes derived from the real I6 `object` (the `extern class object { ... }` in core, plus everything inheriting from it via normal/alias classes) emit as I6 `Object` directives; classes marked `emitter` (including the combined `extern emitter` form) emit as I6 globals.

```bgl
extern emitter class int : _bglObject {  // type label over I6's word-sized integer slot
    emitter int operator + (int v){ $val + $v }
}

int    x = 5;           // emits: global x = 5;
bool   b = true;        // emits: global b = true;
object j;               // emits: object j;   (I6 Object directive - `object` is not emitter)
Room   r;               // emits: object r;   (Room is alias to object → tree citizen)
Tally  t;               // emits: tally t;    (Tally is a normal class : object)
```

The combined form is restricted to library-internal use; user code generally declares its own types as normal `class` or `extern class` and uses the existing primitive types from the library.

## 5.2.4 `alias class`

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

The `for` keyword rather than `:` signals that the RHS is the I6 type being dissolved to, not a superclass being extended. No I6 class declaration is generated for the alias; instances emit using the I6 name of the root non-alias type.

#### Typical use

Alias classes are used primarily for **type inference**: typed member declarations on the alias class let instances omit the type specifier when setting those properties. The instance body still resolves members against both the alias class and the base `object` class.

**I6 name resolution** - the emitted class prefix for instances is determined by walking the parent chain until a non-alias type is found:

```bgl
alias class worldObject for object { ... }    // chain: worldObject → object → "Object"
alias class heavyObject for worldObject { }   // chain: heavyObject → worldObject → object → "Object"
```

Both `worldObject` and `heavyObject` instances emit using I6's `Object` keyword.

Rules for `alias class` members:
- **Emitter methods** are allowed and require the `emitter` keyword.
- **Non-emitter methods** are not allowed; this is a compile-time error.
- **Variable declarations** (type and name only, no `=` initializer) are allowed for type inference.
- **Variable definitions** (with `=`) are not allowed; this is a compile-time error.
- Exactly one parent class is required after `for`. Multiple parents are not allowed.

`alias` and `extern` are mutually exclusive and serve different purposes.

## 5.2.5 Pooled Classes

A normal class can reserve a fixed number of statically-allocated instances at compile time by adding `[N]` after the class name. Instances are then allocated and deallocated dynamically from this pool with `new` and `delete` (see §10.7). This mirrors I6's `Class Foo(N) with ... has ...;` form.

```bgl
class marbleClass[10] : object {
    int rolls = 0;

    void create(){
        rolls = 0;
    }

    void destroy(){
        // cleanup before the slot returns to the pool
    }
}

void main(){
    marbleClass m = new marbleClass();
    // ... use m ...
    delete m;
}
```

The `[10]` reserves ten instance slots in static memory. There is no dynamic allocation at runtime: `new` returns one of the pre-allocated slots, or `nothing` if the pool is exhausted. This matches the Z-machine and Glulx VM model: no malloc, all storage statically sized.

#### Pool size

- `[N]` (N is a positive integer) - sized pool, emits as I6 `Class Foo(N) ...`.
- `[IDENT]` (an identifier) - sized pool whose count comes from a compile-time integer constant declared via `const int IDENT = …;` (or any other emission-equivalent route). The compiler emits the identifier verbatim into the I6 `Class Foo(IDENT) ...` directive; at I6 compile time, `IDENT` must resolve to a positive integer Constant. Useful when the pool size is shared with other code (e.g. a tuning constant in a BLR file).
- `[]` (empty brackets) - **only valid on `extern class`**, see §5.2.2 below. Marks the class as I6-pooled with size opaque to Beguile.
- `[N]` / `[IDENT]` are not valid on `emitter class` or `alias class`; those forms have no I6 instance backing.
- `extend class Foo[...]` is not valid; pool size is part of the original declaration's contract.

#### Pool inheritance

Subclasses share the parent's pool. There is no per-subclass pool size: `class subMarble : marbleClass` adds members but draws instances from `marbleClass`'s pool of 10.

#### `create()` and `destroy()` lifecycle methods

A pooled class may define a `create()` method, called when the slot is allocated, and a `destroy()` method, called before the slot is returned to the pool. Both are optional. They map directly to I6's `create` and `destroy` properties on the class.

```bgl
class marbleClass[10] : object {
    int weight = 0;

    void create(int w){
        weight = w;
    }
}

marbleClass m = new marbleClass(5);   // calls create(5), m.weight = 5 after
```

Rules for `create()`:
- **Return type must be `void`.** I6's create veneer ignores the return value.
- **Up to 3 parameters on Z-machine.** I6's class-message dispatch (`Cl__Ms`) caps argument forwarding at 3 slots. Glulx may allow more; verify per target.
- **One `create` per class.** Multiple Beguile overloads would collide in I6's single-property-per-class create dispatch. If you need multiple allocation styles, add separate `init`-style helpers and call them after `new`.

Rules for `destroy()`:
- **Return type must be `void`.** Like create, the return is ignored.
- **No parameters.** I6's destroy veneer takes no forwarded args.
- **One `destroy` per class.**

#### Owned members in a pool

A pooled class may hold **owned members** (§5.3) — value-helper members that need their own backing instance. Each of the `N` pool slots must have an independent backing, and reused slots must come back clean. Beguile handles this automatically: it preallocates `N` backings per owned member, keeps them on a compile-time free-list, and wires the lifecycle so that

- `new` pops a free backing, attaches it, and **resets its fields to their defaults** (a reused slot always starts fresh); and
- `delete` returns the backing to the free-list.

```bgl
class Box { int _val = 0; int operator(){ return _val; } void operator = (int v){ _val = v; } }
class Thing[3] : object { Box b; }             // each Thing owns its own Box

Thing p = new Thing();
Thing q = new Thing();
p.b = 11;  q.b = 22;                            // independent: 11 and 22
delete p;
Thing r = new Thing();                          // reuses a slot
// r.b reads 0 (reset to defaults); q.b still 22
```

This is transparent — the pop/reset/push is generated into the class's `create`/`destroy` lifecycle, composing with any `create()`/`destroy()` you write (your code runs after the reset). Pools sized with an identifier (`[IDENT]`) do not yet support owned members and emit a warning; use a numeric size.

#### Limitations

- Z-machine class-message dispatch caps `create` arguments at 3.
- A static-instance variable of a pooled type (e.g. `marbleClass M;` declared at file scope, not allocated via `new`) cannot be safely passed to `delete`, because Beguile does not currently distinguish pool references from static instances at compile time. I6 will catch the misuse at runtime via `RT__Err`. Avoid mixing static and pool-allocated instances of the same type.
- Owned members on an identifier-sized pool (`[IDENT]`) are not yet supported (numeric sizes only).

## 5.2.6 `byVal class` - Value-Semantic Class Parameters

A class declared with the `byVal` qualifier passes by **value** when used as a function parameter, so mutations inside the callee don't leak back to the caller's instance. Default class params (and class-typed locals, see §9.2.2) pass by reference, which mirrors the I6 native model (a class instance is an I6 object reference).

```bgl
byVal class Temperature {
    int degrees = 0;
    emitter Temperature operator = (Temperature v) { ... }  // required - see below
}

void heatUp(Temperature t) {
    t.degrees = t.degrees + 10;   // mutates the local copy only
}

Temperature room;
heatUp(room);   // room.degrees unchanged
```

A `byVal class` parameter is passed by value: at each call site the argument is copied in through the class's `operator=`, so mutations to the parameter inside the body affect the local copy rather than the caller's instance.

Rules:
- **`operator=` is required.** The copy-in step calls the class's `operator=` against the parameter type (or `var` as a fallback). Without one, declaring a `byVal class` is a compile error pointing at the missing operator.
- **Cannot inherit from `object`.** Object-derived classes are tree citizens (reference-semantic by the world-tree model); the marker is rejected with an error citing the conflict.
- **Mutually exclusive with `extern`, `emitter`, `extend`, and `alias`.** `byVal` only makes sense on a normal class that owns its own copy-in semantics.
- **Marker is per-class, not inherited.** A subclass of a `byVal class` does NOT inherit the marker. If the subclass wants the same semantics, it must declare `byVal` itself.

Class members called on a parameter (e.g. inside a method body where `self` is the receiver and the receiver is a `byVal` param to another function) participate transparently: the backing is the receiver for I6 emission, and method dispatch resolves against the same class.

### 5.2.7 `typesealed` - Type-Locked Members

A base-class member declared `typesealed` has its **type locked**. Subclasses and object instances may re-initialize the member, but if they re-declare it with a *different* type keyword, the compiler:

1. **keeps the sealed (base) type** for the slot (the written type is ignored); and
2. **emits a warning** naming the sealed type and the ignored one.

```bgl
extern class object : _bglObject {
    typesealed parentProp parent;   // the world-tree slot - always parentProp
    …
}
```

The canonical use is the world-tree `parent` slot. Writing `object parent = <room>` on an object keeps `parent` a `parentProp` (so `obj.parent = X` / `self.parent = X` dispatch to `parentProp`'s `operator=` and emit `move obj to X`) instead of shadowing it to type `object`, which would silently drop the move. The idiomatic form omits the redundant type keyword:

```bgl
place cliffEdge {
    parent = southOfRockWall;       // idiomatic - no warning
    // object parent = southOfRockWall;   // works, but warns: type keyword is dead
}
```

The initializer is still validated against the *written* type (an object value assigns fine), and the slot behaves as the sealed type thereafter. Because `parent` is reserved for the world tree on any `object`-derived class, a class cannot repurpose `parent` as a differently-typed member; give such a link a different name.

## 5.3 Member Variables

Member variables declare properties on the class.

```bgl
class Room {
    string short_name = "A dark room";
    string description;
    int visits = 0;
}
```

### Array Members

Class members may be declared as `array<T>` with the same forms supported at file scope and in object bodies (see §12 - Arrays):

```bgl
class Inventory : object {
    array<int>    slots[6];                       // sized, zero-initialized
    array<object> heldRefs = { lamp, key };       // initializer list
    array<char>   nameBuf[16];                    // byte array
}
```

The class-property emission preserves the declared size and any initializer values, so each instance of the class gets its own per-property storage. Array members work identically in `class` bodies and `object` bodies.

### `const` Members

A `const` member can be initialized in the object declaration but cannot be reassigned in code. The compiler enforces this at compile time; there is no runtime cost. Unlike global `const` declarations (which emit as I6 `Constant` with no runtime storage), a `const` class member is still an I6 property with runtime storage; however, the compiler prevents assignment.

```bgl
class Config {
    const int maxScore = 100;
    const string title = "My Game";
}
```

Attempting to assign to a `const` member produces a compile-time error:

```bgl
config.maxScore = 200;  // error: Cannot assign to const member 'maxScore'
```

### `static` Members

> **Note for I6 authors.** Beguile's `static` is unrelated to Inform 6's `static`, which marks an object as immovable scenery. Here it declares class-level shared state, as in C-family languages.

A `static` member is class-level state shared across all instances. 

```bgl
class Counter {
    static int instanceCount = 0;

    void increment(){
        Counter.instanceCount = Counter.instanceCount + 1;
    }
}
```

Static members are accessed using `ClassName.memberName` syntax, both for reads and writes. Inside a class method, the class name must be used explicitly; bare `instanceCount` would look for an instance property.

`const` and `static` are mutually exclusive.

### Owned members

Most class-typed members hold a **reference**: an object-derived member (a world-tree citizen) or a member you initialize to an existing object points at that object. A member whose type is a **value-helper class** — a class that is *not* `object`-derived, has stored fields, and is declared **without** an initializer — is instead an **owned member**: the compiler bakes a real backing instance for it, so the member is a live object you can call methods and operators on, not a bare `0` slot.

```bgl
class Box { int _val = 0; void set(int v){ _val = v; } }

class thing : object { Box b; }     // `b` is owned — each thing gets its own Box instance
thing t1 {}
thing t2 {}
```

Here `t1.b` and `t2.b` are distinct `Box` instances with independent state; the member declared on the class gives **every** instance its own backing. This is what makes property accessors (§5.6.7) work, and it also applies to a plain value-helper member you call methods on. Owned members allocated through pooled classes (`new`) are covered in §5.2.5.

An owned member is detected structurally — no keyword. If you want reference semantics instead, derive the member's type from `object`, or initialize the member to an existing instance.

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

Within a method body, bare member names are automatically resolved as `self.memberName`. In the example above, `count` on the left-hand side of `count = newCount;` resolves to `self.count`. Explicit `self.memberName` is also accepted and has the same effect.


## 5.5 Lifecycle Emitters

`init` and `deinit` are zero-parameter lifecycle emitters called implicitly by the compiler. See §14.4 for a full description of emitter mechanics.

- `init` fires immediately after a local variable of this type is declared.
- `deinit` fires before every `return` in the enclosing routine, and at the end of the routine if it falls through.

`init` and `deinit` may be declared on any class, extern or otherwise:

```bgl
class Token {
    emitter void init()   { $self = AllocToken(); }
    emitter void deinit() { FreeToken($self); }
}
```

`init` and `deinit` must not declare parameters; doing so is a compile-time error. These must be emitters; non-emitter lifecycle hooks are not supported.

## 5.6 Operator Overloads

Operators can be overloaded on any class as either **emitters** (inlined at call sites) or **regular methods** (emitted as class properties and called at runtime).

### 5.6.1 Emitter Operators

Emitter operators are the most common form: they inline I6 code at each use site, producing no function call overhead:

```bgl
class Counter {
    int value = 0;
    emitter Counter operator ++ (){ $self.value++ }
    emitter bool operator == (Counter v){ $self.value == $v.value }
}
```

### 5.6.2 Non-Emitter Operators

Non-emitter operators are emitted as class methods and called at each use site. They are useful when the operator body is complex, when debugging visibility is needed, or when code size is a concern:

```bgl
class Animal : object {
    int id;

    replace bool operator == (Animal other){
        return self.id == other.id;
    }
}
// a == b  calls the operator method on a, with b as its argument
```

### 5.6.3 Subscript Operators

`operator[]` and `operator[]=` allow a class to support subscript read and write syntax (`obj[n]` and `obj[n] = v`). Although this convention is commonly associated with array-access members, any class may define them, not just `array<T>`.

```bgl
extern class myBuf {
    emitter var  operator[]  (int i)        { $val-->$i }
    emitter void operator[]= (int i, var v) { $val-->$i = $v }
}
```

With these defined:

```bgl
myBuf buf;
var x  = buf[3];    // calls operator[]  - emits: buf-->3
buf[3] = x + 1;    // calls operator[]= - emits: buf-->3 = x + 1
```

`operator[]` takes one parameter (the index) and returns the element type. `operator[]=` takes two parameters: the index first, then the value. Its return type is the developer's choice, commonly the same type as assigned, allowing the assignment to be used as an expression value or enabling the operator to return a transformed value. Both operators are looked up via the class hierarchy, so a subclass inherits subscript support from a parent.

**Subscript-dot chaining:** The result of a subscript read supports dot-access; the member is resolved against the array's element type:

```bgl
array<Room> rooms = {kitchen, hall};
string s = rooms[0].description;    // field access on element type Room
rooms[1].describe();                 // method call on element type Room
```

### 5.6.4 Conversion Operator (`operator()`)

A zero-parameter emitter named `operator()` declares that this type is compatible with the return type, enabling implicit type conversion (see §11). This may be declared on any class:

```bgl
class celsius {
    emitter fahrenheit operator(){ $val * 9 / 5 + 32 }
}
```

A **pass-through** conversion (value unchanged) is declared with a semicolon terminator instead of a body:

```bgl
emitter int operator();                        // pass-through: value is used as-is
emitter fahrenheit operator(){ $val * 9 / 5 + 32 }  // custom: body transforms the value
```

The semicolon form is equivalent to writing `{ $val }`: the value passes through with no transformation. Use the body form when the conversion requires computation.

#### Implicit vs. explicit conversions

By default, conversion operators are **implicit**: the compiler uses them automatically during operator resolution, argument matching, and assignment. An **explicit** conversion operator, marked with the `explicit` qualifier, only fires at explicit `(Type)expr` cast sites:

```bgl
class MyType : _bglObject {
    emitter int operator();                    // implicit: MyType silently converts to int anywhere
    explicit emitter string operator();        // explicit: only via (string)myValue
}

MyType t;
int n = t;             // OK - implicit conversion to int
string s = t;          // ERROR - explicit conversion, requires cast
string s = (string)t;  // OK - explicit cast
```

This prevents silent coercion to surprising overloads while still allowing intentional casts. See §10.6 for cast syntax.

#### Emitter vs. regular-method conversion, and the bare read

`operator()` may be written as an **emitter** (inlined at the use site) or as a **regular method** (emitted as a callable property and executed at runtime). Both forms participate in the same resolution — casts, argument matching, and assignment — so a regular-method conversion is a first-class alternative when the body is complex or you want it visible to the debugger:

```bgl
class heightProxy {
    int _val = 0;
    int operator(){ return _val; }   // regular method: runs at runtime, no `$` substitution
}
```

A regular-method `operator()` also fires on a **bare read** of a member of that type — not only at explicit cast sites — so the value flows out with natural syntax:

```bgl
class thing : object { heightProxy height; }
thing t {}

int a = t.height;        // getter runs: a = t.height's operator()
print(t.height);         // getter runs in argument position
int b = t.height + 1;    // getter runs in an expression
```

The bare-read getter combines with `operator=` (the setter) to form a **property accessor** — a member that looks like a plain field but runs code on read and write. See §5.6.7.

### 5.6.5 Special Operators

In addition to standard arithmetic and comparison operators, these special operators may be overloaded:

| Operator | Syntax | Description |
|----------|--------|-------------|
| `operator switch(type v)` | `switch(x){ case val: }` | Custom switch comparison. Called for each `case` value to test equality. Must be an emitter. See §9.11. |
| `operator ?()` | `x?` | Postfix query / null test. Returns `eBool`. Used by optional chaining (`?.`) and null coalescing (`??`). Must be an emitter. See §10.5. |
| `operator ?=` | `x ?= y` | Overloadable binary comparison at equality precedence, result `eBool`. No built-in type defines it; provide `operator ?=` on a class to give it a custom meaning. |
| `operator !()` | `!x` | Prefix logical NOT. Must be an emitter. |
| `operator()` | `(Type)x` | Conversion operator. Covered in §5.6.4. |
| `operator[]` / `operator[]=` | `x[i]` / `x[i]=v` | Subscript read/write. Covered in §5.6.3. |

### 5.6.6 Overloadable Operator List

The complete list of operators that may be overloaded (as either emitters or regular methods):

**Binary:** `=` `+` `-` `*` `/` `%` `==` `!=` `=~` `<` `>` `<=` `>=` `?=` `&&` `||` `&` `|` `^` `<<` `>>`

**Compound assignment:** `+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `<<=` `>>=`

**Unary:** `++` `--` `prefix++` `prefix--` `!`

**Special:** `switch` `?` `()` `[]` `[]=`

Declaring `operator` with any symbol outside this set is a compile error that names the overloadable operators. This includes valid operator tokens that are not overloadable, such as `?.`, `??`, and `=>`.

### 5.6.7 Property Accessors (Getters and Setters)

A **property accessor** is a member that reads and writes like a plain field but runs code on each access. It is built from two operators you have already seen — a getter (`operator()`, §5.6.4) and a setter (`operator=`, §5.6.2) — declared on a small value-helper class, and then used as a member:

```bgl
class heightProxy {                       // a value-helper class (NOT `: object`)
    int _val = 0;
    int  operator()        { return _val; }         // getter — fires on read
    void operator = (int v){ _val = v * 2; }        // setter — fires on write
}

class thing : object { heightProxy height; }
thing t {}

t.height = 5;            // setter runs: _val = 5 * 2  → 10
int a = t.height;        // getter runs: a = 10
```

Reads dispatch the getter; writes dispatch the setter. The write target is never hijacked by the read path, so `t.height = 5` always calls `operator=`.

#### Owned members

For the accessor to run, the member must be a **real instance**, not a bare property slot. When a member's type is a value-helper class — a class that is **not** `object`-derived, has stored fields, and is declared without an initializer — Beguile treats it as an **owned member** and bakes a backing instance for it automatically (see §5.3, *Owned members*). This is what lets `t.height.operator()` land on a live object. `object`-derived members (world-tree references) and members with an initializer keep ordinary reference semantics.

#### Inline accessors with `auto { … }`

Declaring a named class for every accessor is often noise. The `auto { … }` form declares the accessor inline: the braces hold a class body (backing fields plus the get/set operators), and the compiler synthesizes a hidden class for it:

```bgl
object gadget {
    int scale = 3;
    auto level = {
        int _raw = 0;
        int  operator()        { return _raw; }
        void operator = (int v){ _raw = v * outer.scale; }
    }
}

gadget.level = 4;        // setter runs: _raw = 4 * 3  → 12
int a = gadget.level;    // getter runs: a = 12
```

`auto` (not `var`) is required: the member's type is *inferred* from the synthesized body. The brace is recognized as a class body — rather than an ordinary `{ v1, v2 }` initializer — when it leads with a member declaration or an `operator`.

#### `outer` — the host object

Inside an accessor body, two references are available:

- **`self`** — the accessor instance itself (its own backing fields).
- **`outer`** — the enclosing host object the accessor is declared on (`gadget` above).

`outer` is resolved at compile time to the host, so an accessor can read and write the host's other members (`outer.scale`). It is available in inline `auto { … }` accessors declared directly on an object.

Accessors work on single objects, on **many instances** of a class (each instance gets its own backing — see §5.3), and on **pooled** classes allocated with `new` (see §5.2.5).

## 5.7 Class Inheritance

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

Inside a derived method body, bare identifiers resolve inherited **variable** members from all base classes via this depth-first search. Inherited functions are resolved separately through the method dispatch system. For example:

```bgl
class Flyer : object { int altitude = 0; }
class Swimmer : object { int depth = 0; }

class FlyingFish : Flyer, Swimmer {
    void status() {
        print(altitude);   // resolved from Flyer
        print(depth);      // resolved from Swimmer
    }
}
```

To explicitly dispatch to a specific parent's version of a member, use the type-cast syntax (see §10.6):

```bgl
(Animal)myDog.speak();   // forces dispatch through Animal's speak, not Dog's
```

## 5.8 `extend class` and `replace` for Members

`extend class` adds new members to any already-declared class. If augmenting an `extern` class type, only emitters may be added.  Classes defined in Beguile code may be extended with actual members as well.

```bgl
extend class Counter {
    emitter bool isZero(){ $self.value == 0 }
}

extend extern class int {
    emitter string asHex(){ $self.toHexString() }
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

### Shadowing Base Class Members

When a derived class or object instance defines a method that already exists in a base class, the compiler emits a warning unless `replace` is specified. This applies to both class declarations and object bodies.

```bgl
class Animal : object {
    void speak(){}
}

class Dog : Animal {
    replace void speak(){}     // explicit override - no warning
}

class Cat : Animal {
    void speak(){}             // warning: 'speak' shadows definition in base class 'Animal'
}

object rex : Animal {
    replace void speak(){}     // explicit override on instance - no warning
}
```

The warning helps catch accidental shadowing (e.g., a base class method was renamed but a derived class still has the old name). Using `replace` communicates that the shadowing is intentional.

### The `default` Qualifier

A method marked `default` in a class declaration provides a default implementation that is expected to be overridden. Overriding a `default` method does not require `replace` and produces no shadowing warning:

```bgl
class Animal : object {
    default void speak(){ print("..."); }
}

object rex : Animal {
    void speak(){ print("Woof!"); }    // no warning - base method is 'default'
}
```

`default` is only valid in class declarations; using it in an object or verb instance body is an error.

If a derived class overrides a `default` method *without* marking its own version `default`, the method is no longer freely overridable; further descendants will require `replace` to suppress the shadowing warning. To keep the method overridable through the hierarchy, mark each override `default` as well.

The built-in `verb` class uses this for `handler()` (the verb's action body):

```bgl
alias class verb for object {
    default emitter void handler(){ $selfsub(); }
    ...
}
```

This provides a default bridge to the I6 action routine (`<verbName>Sub`) while allowing each verb to override `handler()` without boilerplate. (Verbs are additionally required by the compiler to define `handler()` unless declared `extern`; see §13.2.)

### Matching rules for `replace` in class/object bodies
- **In `extend class`**: `replace` replaces an existing member on the same class. Adding a duplicate without `replace` is a compile-time error.
- **In `extend object` (§5.9)**: `replace` replaces an existing method or property on the object. Same duplicate/warning rules as `extend class`.
- **In derived classes/objects**: `replace` suppresses the shadowing warning for methods that exist in the base class hierarchy.
- **Emitters**: matched by name and full parameter-type signature, because emitters support overloading.
- **Regular functions**: matched by name alone.
- **Variable properties**: matched by name alone.
- `replace` on a non-existent member in `extend class` or `extend object` issues a compiler warning and adds the member as new.

`replace` also applies to global functions; see §7.4.

## 5.9 `extend` for Objects

Any previously declared object can be extended using `extend ObjectName { }`. The extend body supports:

- **New members** - declared with `=`, just as in a regular object body
- **Replacing members** - `replace` replaces an existing method or property on the object. Adding a duplicate without `replace` is a compile error. Using `replace` on a non-existent member produces a warning.
- **Appending to collections** - `+=` appends elements to an existing collection member (`grammarRuleList` or `array<T>`)
- **Removing from collections** - `-=` removes elements from an existing collection member (`grammarRuleList` or `array<T>`)

```bgl
extend myRoom {
    int turnCount = 0;               // new member
    replace int score = 20;          // replace existing property value
    replace void describe() {        // replace existing method
        print("A dark room.");
    }
    attributes = {light, !scenery};  // see below - attributeList uses `=` only
}
```

`+=` and `-=` operate on existing members of the object. The member must be a collection type. Using `+=`/`-=` on a non-collection member or a member that doesn't exist is a compile error.

**`attributeList` is `=`-only**: unlike `grammarRuleList` and `array<T>`, an `attributeList` member only accepts `=`. To turn off an inherited attribute, prefix it with `!` inside the literal:

```bgl
attributes = {light};            // set/replace the attribute list
attributes = {light, !scenery};  // explicit light, explicit NOT scenery
attributes = {!light};           // strip an inherited `light` (becomes `has ~light`)
attributes = {};                 // strip all inherited attributes
```

Each entry maps directly to the I6 `has` clause: `attributes = {a, !b}` emits `has a ~b`. For runtime attribute changes, use the `give(attr)` / `ungive(attr)` methods on the attributeList.

**Object reference members** - a property can hold a reference to another object. The assigned object must be type-compatible with the declared property type:

```bgl
class Subsystem : object { void activate() { ... } }
Subsystem combat { }

extend gameState {
    Subsystem sys = combat;     // property holds reference to combat object
}

gameState.sys.activate();       // chained method call through property
```

When the assigned value is a declared object instance, it is stored as a direct reference; no `init()` emitter is called.

**Extern objects**: Extending an `extern` object is restricted, because the object's definition is in I6, not Beguile, so only operations that emit independently of the object are allowed. In practice this means only the `grammar` member is extendable: `grammar += { … }` adds lines (emitted as standalone I6 `Verb`/`Extend` directives routed to the library action), `replace grammar = { … }` replaces a word's grammar, line-level `grammar -= { {.w, pat} }` removes grammar **Beguile itself added** (never the library's original — that warns), and word-level `grammar -= { {.w} }` **evicts** the word from the library verb entirely (I6 `Extend only 'w' replace`; see §13.4). Adding new members or methods, or using `-=` on any non-`grammar` member of an extern object, is a compile error.

**Extern object bodies**: An `extern` object may include a `{ }` body for type registration purposes. The body may contain:
- Method declarations without bodies (`int getScore();`), registered for type checking
- Emitter methods and emitter values, expanded at call sites as usual
- Property type declarations without initializers (`string name;`), registered for type checking

Non-emitter methods with bodies and properties with initializers are compile errors; extern objects are defined in I6, not Beguile.

### 5.9.1 `extend` for Arrays — changing a previously defined array

`extend <arrayName> { … }` **changes a previously defined array** by editing its initializer **at compile time**, so the emitted I6 `array` directive reflects the final, edited order — there is **no runtime cost** (nothing runs at startup; the baked data is simply different). This lets one file declare an array and later files (or later in the same file) add to, remove from, or reorder it without touching the original declaration. It is generic: it works on any declared array and knows nothing about what the elements represent.

Three verbs are available inside the block:

```bgl
array<rule> before = { cantTakeYourself, cantTakeScenery };

extend before {
    inject enteringDark  after  cantTakeScenery;   // add a new element, positioned
    inject reachRule     before cantTakeYourself;
    inject fallbackRule  first;                     // at the start
    inject wrapUpRule;                              // no clause = append (end)

    remove cantTakeScenery;                         // take an element out

    move   reachRule     last;                      // reposition an existing element
    move   fallbackRule  after cantTakeYourself;
}
```

- **`inject <element> [after X | before X | first | last]`** — splice an element in at a position (no clause = append). The element is a single reference or value (a named object, or a literal), or an **inline object declaration** `Type{ … }` (§6.2.1) that bakes an anonymous object and injects a reference to it. When the array's element type is an object-backed class, the type may be **omitted** — a bare `inject { … } last;` takes the element type (§6.2.1, *Type inference from the target*).
- **`remove <X>`** — remove an element.
- **`move <X> [after Y | before Y | first | last]`** — reposition an existing element (equivalent to remove-then-inject at the new spot).

**Positioning — any slot, not just the ends.** `inject` and `move` can place an element **anywhere** in the array. `after X` and `before X` position it in the **interior**, immediately next to an existing element `X`; `first` and `last` are shorthands for the two ends (and no clause = `last`). Together these reach every slot — you are not limited to the head and tail:

```bgl
extend rules {
    inject checkLocked  after  checkReachable;   // drop a new rule into the middle
    move   fallbackRule before checkLocked;      // slide an existing rule up next to it
}
```

The reference (`X`, `Y`) is either:
- an **element name** — the identifier of the object an element points to (`after cantTakeScenery`); the array-of-named-references case.
- a **`[N]` index** — a zero-based position (`after [0]`, `before [3]`, `remove [2]`); for value arrays or one-off edits.

**Semantics.** Directives apply in **source order** (a later `move`/`remove` sees the effect of earlier ones), and the compiler bakes the array once at the end. Every reference is checked at compile time: an unknown name or an out-of-range `[N]` is a **compile error** (no silent misses). Because indices are relative to the array's current state as directives apply, prefer **names** over `[N]` when editing a named array across several directives — names don't shift.

Runtime mutation of an array is a separate thing: use the array's methods (`.append()`, `.insert()`, …) inside a routine. `extend … { … }` is build-time only.

## 5.10 `_bglGlobalDeclaration`

A class can define a `_bglGlobalDeclaration` emitter to inject additional top-level I6 declarations for every object instance of that class. This mechanism allows a class definition to generate companion routines, arrays, or other I6 constructs that accompany each instance.

Within a `_bglGlobalDeclaration` emitter body, the standard substitution tokens apply (see §14.4.3); one additional feature-local token is available:

| Variable | Expands to |
|----------|-----------|
| `$selfsub` | The object's name with `sub` appended (e.g., `examinesub`) |

The built-in `verb` class uses this to generate a wrapper routine for each verb:

```bgl
alias class verb for object {
    grammarRuleList grammar;
    default emitter void handler(){ $selfsub(); }
    emitter void perform(object n){ <$self $n> }   // launch the action (the <...> replacement)
    emitter eBool operator == (verb v){ $self == ##$v }
    emitter void _bglGlobalDeclaration() {
        [$selfsub;
            $self.handler();
        ];
    }
}
```

This pattern is available to any user-defined class, not just `verb`, when additional top-level I6 declarations must accompany each instance.

## 5.11 Namespace-Scoped Types

Types can be accessed through dotted namespace paths on objects. This allows organizing types under a hierarchical namespace without polluting the global scope:

```bgl
bgl.glulx.window myWin;
```

To achieve this, types are declared flat at the top level and then aliased onto namespace objects using `alias name for TypeName;` inside the object body:

```bgl
class glulxWindow : object { int handle = 0; }

object _bglGlulx { alias window for glulxWindow; }

extend bgl { auto glulx = _bglGlulx; }

// Now: bgl.glulx.window myWin;
```

The `alias memberName for TypeName;` syntax mirrors `alias class verb for object` at the class level (§5.2.4). The `for` keyword distinguishes type aliasing from instance assignment (`auto x = y;`).

**What can be aliased**: Both classes and enums can be aliased onto objects.

**Where namespace-scoped types work**: Dotted type paths are valid anywhere a type name is accepted: variable declarations, function parameters, return types, and globals:

```bgl
bgl.glulx.window w;                  // variable declaration
void init(bgl.glulx.window win) { }  // function parameter
bgl.glulx.window getWin() { ... }    // return type
global bgl.glulx.window gWin;        // global variable
```

**Namespace-scoped enum values**: When the aliased type is an `enum` or `bnum`, one additional segment may follow to reference a named value of that type:

```bgl
object _bglGlulx { alias winPlacement for bGlulxWindowPlacement; }
extend bgl { auto glulx = _bglGlulx; }

int winMethod = bgl.glulx.winPlacement.above | bgl.glulx.winPlacement.left;
```

The final segment is resolved as an enum/bnum value lookup, equivalent to writing `bGlulxWindowPlacement.above` directly. Value access is permitted only after the path has fully resolved to an enum/bnum; paths ending at a class alias cannot continue into static or member access.

**Interaction with `#using`**: Type aliases are importable via `#using` (§3.5.9). `#using bgl.glulx` makes `window` available as a bare type name; `#using bgl` makes `glulx.window` available as a partial path. Both resolve through the same alias mechanism.

### 5.11.1 Value aliases: `alias name = Target`

The `for` form above aliases a *type*. A namespace member can also alias a *value* (an object instance or a class) so its members are reachable through the namespace path (`bgl.world.getAll()`, `bgl.util.math.pow(...)`). Two forms expose a backing object, and they differ in whether a **runtime property** is emitted on the host:

```bgl
extend bgl { auto  world = _bglWorld; }   // (a) emits a runtime `world` property on bgl
extend bgl { alias world = _bglWorld; }   // (b) compile-time only - no property
```

- **`auto name = obj`** binds `name` as a runtime property whose value is `obj`. `bgl.world` therefore exists as a first-class runtime value; however, because it is a real property, `obj` (and everything it references) is always emitted.
- **`alias name = obj`** is a **compile-time-only indirection**: `bgl.world.getAll()` resolves to `_bglWorld.getAll()`, and **no `world` property is emitted**. Since nothing links the host to `obj` at runtime, `obj` is referenced only where the alias is actually used. Paired with a `superposed` target (§14.10), the whole namespace then costs nothing until it is called:

```bgl
superposed object _bglWorld { array<object> getAll() { ... } ... }
extend bgl { alias world = _bglWorld; }
// A game that never writes `bgl.world` emits neither the object nor its routines.
```

The `= Target` form also accepts a **class**: `alias asm = bglOpCodes` is a class namespace alias (`bgl.asm.add(a,b)` → `bglOpCodes.add(a,b)`), equivalent to `emitter auto asm = bglOpCodes`. Prefer `emitter auto` when the target is specifically an emitter class (which has no runtime object at all, so the `emitter` reads truthfully there); use plain `alias name = Target` for the general "compile-time indirection, no runtime property" case.

Summary of the four member-alias forms:

| Form | Target | Runtime property? | Use |
|------|--------|-------------------|-----|
| `alias name for Type` | class / enum | no | namespace-scoped **types** (§5.11 above) |
| `alias name = Target` | object or class | **no** | compile-time value/dispatch alias; gates a `superposed` target on use |
| `auto name = obj` | object | **yes** | when `namespace.name` must be a runtime value |
| `Type memberName;` (emitter class) | class | no | alias member for namespace composition (§5.11.2 below) |

### 5.11.2 Alias Members on Emitter Classes

An emitter class can hold **alias members**: typed references to other classes that enable hierarchical namespace composition. An alias member is declared as `TypeName memberName;` (no initializer) and is resolved transparently at compile time:

```bgl
emitter class bglStrings {
    void init { _orStr_init() }
    int count { _orStr_count }
}

emitter class bgl { }

extend class bgl {
    bglStrings strings;     // alias member
}

// Usage:
bgl.strings.init;           // resolves through alias: bglStrings.init
int c = bgl.strings.count;  // resolves through alias: bglStrings.count
```

Alias members allow building a single root namespace (e.g. `bgl`) that delegates to sub-namespaces, keeping the global namespace clean. They compose with `#using` (§3.5.9):

```bgl
#using bgl
strings.init;               // resolves: bgl → strings → bglStrings.init
```

Rules:
- The member type must be a declared class (emitter or regular)
- No initializer is allowed; the alias is purely compile-time
- Alias members are only valid on emitter classes (regular objects use property references instead)
- Multi-level aliases are supported: `a.b.c.method()` resolves through any number of alias hops

---

# Chapter 6 - Objects

## 6.1 Overview

An *object* is a named, globally visible instance that maps directly to an I6 object and exists as a concrete entity in the compiled story file. Unlike a class, which is a type definition, an object is a single instance of that type.

Objects are commonly used for IF world-model entities (rooms, things, and characters); however, their use is not restricted to the world model. Any class that inherits from `object` can be instantiated as a named object, including utility classes, data tables, and other non-interactive constructs that benefit from I6's property and method dispatch.

## 6.2 Declaring an Object

An object is declared at global scope with the `object` keyword followed by a name and a body enclosed in braces:

```bgl
object foyer {
    string short_name = "Foyer of the Opera House";
    string description = "A grand hall decorated in red and gold.";
    attributes = {light};
}
```

The object name becomes a globally visible identifier that can be used wherever an `object`-typed value is expected. An object declaration may optionally carry an `as i6name` clause to specify a different name in the emitted I6 (see §7.9).

An object that is defined in I6 rather than Beguile is declared `extern`: `extern object Name;` records just the name (§14.2), while `extern object Name { ... }` also declares the types of its members (bodyless method signatures and typed properties) so calls against it type-check (see §14.2.2).

### 6.2.1 Inline (anonymous) object declaration — `Type{ … }`

An object declaration used in an **expression position, without a name**, creates an **anonymous instance** and evaluates to a **reference** to it:

```bgl
class point : object { inline int x; inline int y; string label; }

array<point> pts = {
    point{ 3, 4 },                 // positional — fills the class's `inline` members, in order
    point{ x = 1; y = 2; },        // named fields
    point{ 5, 6; label = "p"; },   // positional first, then a ';' transition, then named
};
```

This is the same declaration you'd write with a name (`point p { x = 1; y = 2; }`) — it just omits the name and appears where a value is expected. It is a **compile-time declaration**, not runtime construction: the compiler bakes an anonymous static object (exactly as a named object bakes) and the expression is a pointer to it. Nothing runs at startup; it is unrelated to `new`/`create` (§10.7), which allocate at runtime.

**Positional vs. named fields.** A field may be given **positionally** or **by name**, and the separators carry meaning:

- **Positional** values are `,`-separated and read like a list: `{ 3, 4 }`. Each fills the next **`inline`-marked member**, in declaration order, **base class first** (a subclass's `inline` members follow its base's). Only members explicitly declared `inline` participate — this keeps positional order under the class author's control rather than tied to arbitrary declaration order. Supplying more positional values than there are `inline` members is a compile error.
- **Named** members use the declaration style `field = value;` and are `;`-separated — exactly like a normal object/class body. The **`=` is required**; any member (inline or not) may be set by name. Normal object/class bodies remain `;`-only; `,` is never a member separator there.
- **The two may be combined, positional first.** A single `;` **trails the positional section and enters the named section**; after it, only named members (`;`-separated) may follow. So `,` always means "still positional", the first `;` means "now naming", and a `,` sitting before or among named members is an error. Named-before-positional is likewise an error.

**Type inference from the target.** Where the target type is already known, the leading `Type` may be **omitted** — a bare `{ … }` takes the target's type:

```bgl
class rule : object { inline verb action; inline var matcher; inline func<eVerdict> body; }

array<rule> book = {           // each element inferred as `rule` from array<rule>
    { drop, shirt, dropBody },
    { take, cloak, takeBody },
};

rule fallback = { look, any, lookBody };   // object-backed variable initializer folds into `fallback`
```

Inference applies in three slots: an **`array<T>` element**, an **object-backed variable initializer** (`Type name = { … }`, which is the same declaration as `Type name { … }` — the fields bake straight into the named object), and a declarative **`inject`** (§5.9.1). It only ever produces an *object aggregate* when the target type is an **object-backed class**; if the target is an `array<…>` or a value/collection type (one with an `operator=(initializerList)`), a bare `{ … }` stays a braced *list*, exactly as before. An explicit `Type{ … }` is always available and additionally disambiguates a single element from a list.

**Nested aggregate values.** A field value may itself be a `{ … }` aggregate, whose shape is taken from the *member's* type: an `array<T>` member takes a braced **array literal**, and an object-backed member takes a **nested inline object**. An `array<T>` member may itself be declared `inline`, making it a positional slot like any other. So a whole record can be built in one positional expression:

```bgl
class menu : object {
    inline object linkTo;                 // object-ref slot
    inline string title;                  // scalar slot
    inline array<dictionaryWord> words;   // `inline array<>` — a positional slot too
}

menu child = { root, "a child", {.foo, .bar, .baz} };   // 3rd value is a nested array literal
```

**Standalone declaration.** Because it is an ordinary declaration, an inline object may also stand alone as a statement — no name and no assignment:

```bgl
menu{ root, "text to display", {.type, .kind} };
```

This bakes an anonymous object and discards the reference, so the object is reachable only if it **self-links** — for example by setting a positional `parent`/`linkTo` that places it in the I6 object tree. Without such a link it is an intentional but unreferenced baked object (reachable only by a low-level object-tree walk).

Other properties:

- **Generic** — it works for any class that can be declared as a named object; it reuses the object-declaration machinery, so it automatically tracks whatever that supports. It has no meaning for namespace types (`emitter class` / `alias class`) or value types.
- Usable anywhere an expression is: array-literal elements, the element of a declarative `inject` (§5.9.1), a variable initializer, etc.

## 6.3 Object Types

An object may be declared as an instance of a class in two equivalent ways:

**Using the class name as the type keyword** (preferred when the class is the primary type):

```bgl
class worldObject : object {
    string description;
}

worldObject foyer {
    description = "A grand hall.";
}
```

**Using the inheritance syntax after the object name** (required when the object/class inherits from more than one base type):

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

## 6.4 Properties

Properties within an object body declare the object's I6 properties, optionally with initial values. Uninitialized properties default to 0/false/nothing. They are declared using a type-name form, with an optional value:

```bgl
object hook {
    string short_name = "small brass hook";
    int number = 0;
    object parent = cloakroom;
}
```

### 6.4.1 Type Inference from Class and Base Object

When an object is an instance of a class (via either syntax in §6.3), typed member declarations on that class may be set without repeating the type. The compiler searches the object's declared class first, then walks the parent chain, and finally checks the base `object` class.

```bgl
alias class worldObject for object {
    string description;     // declared on worldObject
}

worldObject foyer {
    description = "A grand hall.";  // type inferred: string (from worldObject)
    attributes = {light};           // type inferred: attributeList (from object)
}
```

If a property name without a type specifier is not found on any class in the hierarchy, the compiler reports a compile-time error.

### 6.4.2 Special Properties

The following properties have compiler-level support tied to the underlying I6 world model. They are currently declared on the `object` base class; however, the mechanisms they represent, object tree placement and attribute assignment, are distinct from ordinary property storage and may be applied to other base class hierarchies in the future.

**`parent`** - places the object inside another object at game start:

```bgl
object cloak {
    object parent = selfobj;    // player is carrying it at game start
}
```

**`children`** - the inverse of `parent`: the object's **child collection** in the world tree. It lets a container name its contents (rather than each item naming its container), and it reads as a collection.

*Placement.* In an object body, `children = { a, b, c }` places each listed object inside this one at game start. It desugars to a `parent` set on each child, so it reuses the same compile-time positional-parent tree as `parent` (no runtime cost) — you may declare the container before or after its contents:

```bgl
object table {}
object chair {}

object kitchen {
    children = { table, chair };     // one-line population; equivalent to setting each item's parent
}
```

A world-tree object has exactly one parent, so conflicting placement is a **compile error**: listing an object in two different containers' `children`, or placing it in one container's `children` while it also sets `parent` to a *different* object. Declaring the same link both ways — `kitchen.children = { table }` *and* `table.parent = kitchen` — is redundant but consistent, and is accepted silently.

*Reading.* `obj.children` is a collection: iterate it with `for`, and read its size with `.length()` (or `.size()`, a synonym here — a world-tree collection has no capacity-vs-in-use distinction). These are routines, matching the `size()`/`length()` surface on arrays and buffers:

```bgl
for (object o in kitchen.children) { o.give(seen); }
int n = kitchen.children.length();    // number of direct children
```

*Runtime placement.* Add objects to a container at runtime with `+=`:

```bgl
bowl.children += { apple, pear };     // moves each into `bowl`
```

Plain `=` is **not** allowed at runtime — it reads as "replace all contents", a footgun — and `-=` has no well-defined destination; use `+=` to add, or move objects individually. (The `=` form is reserved for the object-body population above.)

**`attributes`** - an `attributeList` property (equivalent to `array<attribute>`) that declares the object's initial attributes using an initializer list:

```bgl
object foyer {
    attributes = {light};
}
```

Attribute names must be declared before use:

```bgl
attribute myNewAttr;           // declares a new attribute; emits 'Attribute myNewAttr;' to I6
extern attribute light;        // references an attribute already declared in the I6 library
```

## 6.5 Array Properties

A property may be an array, declared with `array<T>`:

```bgl
object scoreboard {
    array<int> highScores = {100, 75, 50, 25};
}
```

Array properties with word-sized elements (`array<int>`, `array<dictionaryWord>`, etc.) emit inline property value lists in the I6 output. Element type-checking applies using the same rules as global arrays (§4.9).

Byte array properties (`array<char>`) are handled differently: since I6 property values are word-sized, byte arrays cannot be stored inline. The compiler emits an external `Array` declaration with a mangled name and stores a pointer to it as the property value. Both string initializers and brace initializers are supported:

```bgl
object foo {
    array<char> greeting = "hello";           // external: Array _foo_greeting string "hello";
    array<char> codes = {'a', 'b', 'c'};      // external: Array _foo_codes -> 3 'a' 'b' 'c';
}
```

This is transparent to the developer: subscript access, `for-in` loops, and the `length()` emitter all work correctly on byte array properties.

## 6.6 Method Properties

An object may define method properties: functions that are properties of the object rather than standalone routines.

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

### 6.6.1 Method dispatch on object receivers

When a method is called on an object, including via `self.method()` from inside an object's own method body, the compiler looks for the method in this order:

1. The object's own member methods (per-instance overrides).
2. The hierarchy of the object's class: the class itself, then its base classes recursively.

The class hierarchy walk is what makes inherited methods like `give`, `provides`, `has`, and `is` from the `object` base class reachable from any object instance. Inside `object Foo {…}`, both `self.give(attr)` and `obj.give(attr)` resolve through this two-step walk; per-instance methods declared on `Foo` shadow the inherited versions.

This dispatch rule applies uniformly: an `object Foo {…}` declared without an explicit class still inherits from `object` implicitly, and the class-hierarchy walk reaches the methods defined there. There is no special-casing of the `object` base class; it is an ordinary class whose methods are reached through the same hierarchy walk as any user-defined base class.

### 6.6.2 Method overloads on objects and classes

A class or object body may define multiple methods that share a name but differ in their parameter signatures. Same-named overloads coexist and dispatch by signature; the general resolution rule (arity, then exact type match, then conversion) is described in §8.4.

```bgl
class Logger {
    void log(int n)      { print(n); }
    void log(string s)   { print(s); }
    void log(int n, int m){ print(n); print(":"); print(m); }
}

Logger lg;
lg.log(5);          // dispatches to log(int)
lg.log("hi");       // dispatches to log(string)
lg.log(3, 7);       // dispatches to log(int, int)
```

Overloads of `operator()`, `operator[]`, and `operator[]=` follow the same rule; operator emitters may likewise be overloaded by parameter type.

## 6.7 Complete Example

The following is a representative object from the Cloak of Darkness demonstration. It uses the `give`, `ungive`, and `has` methods on objects; the `attributes` property is covered in §6.4.2, and the `attributeList` methods (`give`/`ungive`) in §15.3.1.

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

# Chapter 7 - Global Declarations

## 7.1 Overview

Declarations at the outermost level of a source file, outside any function, class, or object body, are *global declarations*. They are visible throughout the entire compilation.

## 7.2 Global Variables

A global variable is declared with a type, a name, and an optional initializer:

```bgl
bool isGood = true;
bool isBad;
int score = 5+3;
string playerName;
```

An initializer may be a literal value or a simple expression; complex expressions that require runtime evaluation are not supported at global scope.

Every global variable name must be unique across the entire compilation. Declaring a second global with the same name as an existing global, class, object, or enum is a compile-time error.

A local variable declared inside a routine may not share a name with any global variable. Doing so is a compile-time error (see §12.5).

**Code generation**: a global whose declared type is a user class with stored members emits as an I6 *object instance* of that class. `MyClass myInstance;` becomes I6 `myclass myinstance;`, giving the variable the class's property table. Globals of primitive types (`int`, `bool`, `char`, `string`, etc.) emit as plain I6 globals. The distinction is automatic; the user does not need to inherit from `object` to get instance semantics; any class with non-emitter, non-static stored fields qualifies.

## 7.3 Global Constants

The `const` modifier marks a global variable as read-only. The compiler prevents assignment to `const` variables.

```bgl
const string story    = "Cloak of Darkness";
const string headline = "A basic IF demonstration.";
const int MAX_SCORE   = 2;
```

Attempting to assign to a `const` variable is a compile-time error.

`extern const` declares a constant that is defined in I6, not in Beguile. It registers the name in the type system for type-checking purposes and produces no I6 output. The compiler enforces read-only access; assigning to a `const` variable (including `++`, `--`, and compound assignment) is a compile-time error:

```bgl
extern const int STUCK_PE;
extern const int CANTSEE_PE;
```

## 7.4 Global Functions

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

Each global function name must be unique; the compiler will produce duplicate-name errors if two regular functions share an identifier. See Chapter 8 for full details on function syntax, parameters, and return rules.

Overloading by parameter type is supported for emitter functions and for methods on classes and objects (see §8.4).

### `replace` for Global Functions

The `replace` qualifier replaces an already-registered global function or emitter.

For **emitters**, replace performs a simple body swap: the existing emitter's body is overwritten. Matching is by name, return type, and full parameter-type signature (since emitters can be overloaded). Chaining via `replaced()` is not supported for emitters; their bodies are small inline text fragments where chaining adds no practical value.

```bgl
// initial definition
emitter void print(string str){ print (string)str; }

// string.bgl - better implementation, loaded later
replace emitter void print(string str){ str.print() }
```

For **regular (non-emitter) functions**, replace supports **chaining**. The new definition replaces the previous one, which remains callable from the replacement as `replaced()`:

```bgl
void DrawStatusLine(){
    // original implementation
}

replace void DrawStatusLine(){
    // new implementation; optionally call the original:
    replaced();
}
```

#### Chaining rules

- **`replaced()`** resolves to the immediate predecessor in the chain. Arguments are passed explicitly and type-checked against the predecessor's signature.
- **Multiple replaces** form a linked chain. Each replacement's `replaced()` calls the version it directly replaced, not the original:

```bgl
int step(int n){ return n; }

replace int step(int n){
    return replaced(n) + 100;   // calls original
}

replace int step(int n){
    return replaced(n) + 200;   // calls first replacement
}
// step(5) → first replacement(5) + 200 → original(5) + 100 + 200 = 305
```

- **Signature flexibility**: the replacement's own signature may differ from the original. Each `replaced()` call is validated against its specific predecessor's parameter types and arity; a mismatch is a compile-time error with the expected signature shown.
- **Dead code elimination**: predecessor versions that are never referenced (via `replaced()`) are not emitted. This avoids emitting unreachable routines.

A compile-time warning is reported if no matching function exists to replace; the definition is treated as a new function.

## 7.5 Extern Variables

`extern` variables are declared in I6 and used by Beguile for type-checking only. They produce no I6 output and cannot be initialized.

```bgl
extern int  score;      // mutable - game code may assign to this
extern object location; // the current room
```

Mutable `extern` variables may be read and assigned. `extern const` variables are read-only (§7.3).

## 7.6 Attributes

An `attribute` declaration introduces a named flag that can be assigned to objects. It emits an I6 `Attribute` directive. Attributes declared with `extern` reference an attribute already defined in I6 (e.g. via an IF library) and produce no I6 output.

```bgl
attribute myAttr;              // new attribute; emits 'Attribute myAttr;' to I6
extern attribute light;        // defined externally; no I6 output
```

Once declared, attributes are available as identifiers of type `attribute` and can be passed to the `give`, `ungive`, and `has` methods defined on `object` and `attributeList`.

## 7.7 Properties

Inform 6 maintains a single global property table, populated implicitly by every member name that appears in any class or object definition. The `obj.provides(name)` test (the I6 `obj provides name` operator, surfaced as a method on `object`) returns whether a given object carries that property at runtime. Beguile auto-registers every class and object member name into this table, so for any member of any declared class, `obj.provides(member)` Just Works without further declaration.

For names that are not members of any Beguile class, typically because they live in I6 code that Beguile interoperates with, or because they are runtime-attached flags with no compile-time owner, Beguile provides a `property` declaration that operates analogously to `attribute`:

```bgl
property hidden_flag;          
extern property libDefinedProp; // defined externally (e.g. by an I6 library)
```

Both forms register the name with the compiler so `obj.provides(name)` resolves in strict mode. The non-`extern` form additionally emits an I6 `Property` directive so I6 itself knows about the name even when no class declares a member of that name. The `extern` form trusts that the I6 stream will declare it elsewhere.

A non-`extern` `property` declaration may be marked **`additive`**, which emits I6's `Property additive name;` directive:

```bgl
additive property my_hooks;    // emits 'Property additive my_hooks;'
```

By default an I6 property is overriding: when an object and one of its ancestor classes both supply the property, the object's value replaces the class's. Marking the *slot* `additive` makes I6 instead *accumulate* the values into an array gathered from the object plus all its ancestors (the same mechanism the standard library uses for the `before`/`after`/`life` hooks). `additive` is a directive-only qualifier in I6, so it is valid only on a non-`extern`, file-scope `property` declaration — not on a class/object member, not on a value type, and not on an `extern property` (whose additivity is owned by the external I6 declaration). 

```bgl
class Box : object { int weight; }
Box g_box;
void main(){
    if(g_box.provides(weight))         { ... }   // class member - implicit
    if(g_box.provides(hidden_flag))    { ... }   // free-standing property decl
    if(g_box.provides(libDefinedProp)) { ... }   // extern property decl
}
```

Declarations of `property` are intentionally untyped: the same property name may appear as a member of two unrelated classes with different Beguile types, and a free-standing decl cannot honestly pick one. A `property` identifier has type `property` and is accepted anywhere the parameter type is `property`; it does not support direct `obj.someName` field access for free-standing decls. Use `provides()` to test, or declare the name as a class member when you want to read or write it.

**`property` parameters treat a bare argument as a property identifier.** When a function or emitter parameter is typed `property`, a bare property-name argument emits as the **bare I6 property constant** (its slot number) rather than as a value read, an implicit `(property)` cast (§10.6). This holds even inside an object method body, where a bare known-property name would otherwise emit as `self.<name>`. Any known property name is accepted: a member of any class or object, or a free-standing `property`/`extern property` decl. This lets an engine that indexes objects by property number (e.g. a task/state tracker whose task IDs are members of a single instance) declare `property`-typed parameters and have callers pass the bare name; no file-scope `extern property` re-declaration of each member is required:

```bgl
extern void achieved(property task);           // engine indexes by property number

orGameState gameState { int taskGetBanana = 1; }  // task IDs are members of the instance

// inside any object method body:
achieved(taskGetBanana);   // emits `achieved(taskgetbanana)` - the bare property constant
```

`object.provides(property name)` is the canonical built-in that consumes a `property` parameter; the same rule now applies to any user- or library-declared `property` parameter.

In strict mode, `obj.provides(unknownName)` is a compile error: declare the name as a class member, or with `property unknownName;` / `extern property unknownName;` at file scope. In loose mode (`#bgl` islands and `.inf` precompiler mode; see §14.6), the check is skipped and the name passes through verbatim, matching the loose-identifier policy applied to all other identifiers in those contexts.

## 7.8 Class Tests

Inform 6 supports a runtime class test via `obj ofclass Cls`, which checks whether an object is an instance of a given class (or any subclass thereof). Beguile surfaces this as the `is()` method on `object`:

```bgl
class Container : object { }
class Box : Container { int weight; }
extern class libDefinedClass : object { }   // defined externally; no I6 emission

Box g_box;
void main(){
    if(g_box.is(Container))      { ... }   // Box inherits Container - true at runtime
    if(g_box.is(Box))            { ... }   // exact class
    if(g_box.is(libDefinedClass)){ ... }   // external class - name registered, no I6 emission
}
```

The argument must be a registered class, either a `class Foo {…}` declaration or `extern class Foo : object {…}`. Both forms register the name with the compiler. The `extern` form also trusts that I6 declares the class itself; `is()` accepts the bare name in either case.

The bare class identifier resolves with type `bglClass`, the parameter type of `object.is()`. Any registered class is type-compatible with `bglClass`, so all class names are accepted. As with `provides()`, strict mode requires the class to be declared; loose mode passes the name through verbatim.

## 7.9 I6 Name Aliasing - the `as` Clause

Global instance declarations may carry an optional `as i6name` clause that specifies the name to emit in the generated I6. The Beguile name is used throughout `.bgl` source for type-checking and identifier resolution; the alias is substituted transparently at every emission site.

```bgl
extern attribute lit as light;     // Beguile name: lit  →  I6 name: light
object myHook as hook { ... }      // Beguile name: myHook  →  I6 name: hook
```

The `as` clause is valid on:
- `T Name as i6name;` - any typed instance declaration
- `object Name as i6name { ... }` - a named object definition (including instances of subclasses such as `room Name as place { }`)
- **Class and object members** - both a property and a method may carry `as i6name`, positioned after the member name:

  ```bgl
  class Widget : object {
      int count as internalCount;                 // property → emits I6 short-name `internalCount`
      void refresh() as _widgetRefresh { ... }    // method   → emits under I6 name `_widgetRefresh`
  }
  ```

  This lets a member's emitted I6 identifier dodge a symbol clash while the Beguile name stays descriptive; e.g. `auto util = _bglUtil as bglUtil` avoids colliding with orLibrary's `object util`. The alias is ignored on operator methods (their I6 name is assigned by the operator/overload mangler).

It is **not** valid on type declarations (e.g., `extern class`, `alias class`) or free function declarations.

This is available any time a chosen Beguile name must differ from the underlying I6 name, for example when the I6 name conflicts with a Beguile keyword, or when a more descriptive Beguile name is preferred in source while the I6 name must remain unchanged for compatibility.

> **`as` vs. `for`:** These serve different purposes:
> - **`as`** renames a single instance for I6 output. You write `myHook` in Beguile, but the name emitted to I6 is `hook`. It's a per-instance name override.
> - **`for`** in `alias class Foo for Bar` declares that `Foo` is another name for the type `Bar`. It affects type resolution, not emission names. See §5.2.4.

---

# Chapter 8 - Functions and Routines

## 8.1 Syntax

A function declaration specifies a return type, a name, a parenthesized parameter list, and a braced body:

```bgl
returnType name(paramType paramName, ...) {
    // body
}
```

The body is a sequence of statements (see Chapter 9). Functions declared at global scope emit I6 routines. Functions declared as class or object members emit I6 method properties.

Global functions may also be declared as `extern` (defined in I6, no Beguile body; see §7.5) or as `emitter` (raw I6 body inlined at call sites; see §14.4).

## 8.2 Return Types

The return type precedes the function name. Every execution path in a non-`void` function must end with a `return` statement; a path that can fall off the end without returning is a compile-time error. See §9.13 for the full path analysis rules.

| Return type | Meaning |
|-------------|---------|
| `void` | The function produces no value. A bare `return;` is permitted; `return expr;` is an error. |
| Any other type | The function must return a value of that type via `return expr;`. |
| `array<T>` | The function returns a typed array. Example: `array<int> getList() { return g_src; }` |

The I6 `rtrue` and `rfalse` keywords may also appear in functions with a return type as `bool`, serving as shorthand for `return true` and `return false` respectively.

Both forms also accept an optional message argument: `rtrue(expr);` / `rfalse(expr);`. The argument is printed (using full `print()` overload dispatch, which accepts any printable type, including `$"..."` interpolated strings) and then the routine returns true / false. Equivalent to writing `print(expr); rtrue;` (or `rfalse;`) but as a single statement. Common in IF idioms like the dark-room block:

```bgl
bool before() {
    case Go:
        if (self.hasnt(light) && noun != n_obj)
            rtrue("Blundering around in the dark isn't a good idea!");
    rfalse;
}
```

## 8.3 Parameters

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

### Named Arguments

Arguments may be passed by name at the call site using `name: value` syntax. Named arguments can appear in any order and are reordered by the compiler to match the parameter list:

```bgl
void spawn(string name, int x, int y, bool hostile = false) { ... }

spawn(x: 10, y: 20, name: "goblin", hostile: true);
// equivalent to: spawn("goblin", 10, 20, true);
```

Named and positional arguments may be mixed. Positional arguments fill the first unoccupied parameter slots; named arguments fill their target slots explicitly. It is an error to:

- Name a parameter that does not exist on the function
- Provide the same parameter both positionally and by name
- Omit a required parameter

### Parameter Types in `extern class`

For non-emitter function declarations inside an `extern class`, parameter names are optional; only the type is required:

```bgl
extern class string {
    bool contains(string);   // parameter name omitted
}
```

## 8.4 Overload Resolution

Multiple functions or methods with the same name but different parameter-type signatures may coexist. Global emitter functions overload naturally (each call site inlines the matching body). Non-emitter methods on classes and objects also overload (see §6.6.2). Resolution proceeds as described in §11.4: exact match, then conversion match, then `var` fallback.

Arity is checked before type compatibility. The required argument count is the number of parameters without default values; parameters with defaults are optional. A call matches if the number of arguments falls between the required count and the total parameter count. A call outside this range will not match any overload regardless of types.

## 8.5 The `self` Keyword

Inside a class method or object method property, `self` refers to the receiver, the object on which the method was called. This keyword is not required; however, it is allowed for clarity.

> **Note for I6 authors.** Beguile's `self` is the receiver (as in C-family languages). This differs from Inform 6's `self`, which is the object that owns the currently-running routine. Inside an emitter body, `$self` expands to the receiver (§14.4.3).

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

# Chapter 9 - Statements

## 9.1 Overview

The body of a function, method, or emitter is a sequence of statements. Statements are executed in order. Each statement is terminated by a semicolon unless it ends with a closing brace.

**Stray semicolons are no-ops.** A `;` between statements with no statement before it is treated as an empty statement and discarded. This makes trailing semicolons on directives, declarations, and brace-terminated constructs optional and idempotent: `#include "x";`, `class Foo {...};`, and `{ ... };` all parse cleanly even though the `;` is technically extra:

```bgl
#include "myLib";              // trailing ; tolerated
class Foo { int x = 0; };      // trailing ; after class body tolerated
{ doStuff(); };                // trailing ; after block tolerated
;;                             // empty - parses but does nothing
```

This applies at every statement boundary, including the top of a function body, between statements, and at file scope.

## 9.2 Variable Declaration

A local variable is declared with a type, a name, and an optional initializer:

```bgl
int count = 0;
string label;
bool found = false;
```

The `auto` keyword infers the type from the initializer expression:

```bgl
auto x = 5;            // inferred as int
auto s = "hello";      // inferred as stringLiteral
auto r = myRoom;       // inferred as the object's class type
```

`auto` requires an initializer; `auto x;` without `=` is a compile error. The inferred type is locked in at declaration; subsequent assignments are type-checked against it. `auto` works in local, global, and member declarations.

The variable is visible from the point of declaration to the end of the enclosing block. A local variable name may not shadow a global variable (compile-time error) or a member of the enclosing class/object (warning). See §12.5 for the full shadowing rules.

If the variable's type defines an `init` emitter, it fires immediately after the declaration, before any initializer assignment (§14.4.11).

### 9.2.1 Class-Typed Locals and Reference Semantics

A local variable of a normal class type has **value semantics** by default: its fields are zero-initialized at routine entry so call-to-call state doesn't leak. Assignment dispatches `operator=` on the LHS class, so `localA = factory();` copies fields rather than aliasing pointers.

```bgl
class Vec2 {
    int x = 0; int y = 0;
    emitter Vec2 operator = (Vec2 v) { ... }
}

void doMath() {
    Vec2 v;             // x=0, y=0 on entry
    v = makeVec(3, 4);  // operator=(Vec2) dispatched - fields copied
}
```

Value semantics are gated:
- **Object-derived classes** (`: object`) use reference semantics instead.
- **Classes without stored fields** have nothing to copy.
- **Locals declared `ref`** opt into reference semantics (see below).

If a class has stored fields, does NOT inherit from `object`, and does NOT declare an `operator=`, assigning into a local of that type is a compile error pointing at three remedies: declare `operator=`, mark the local `ref`, or inherit from `object`.

#### `ref` locals - pointer-alias semantics for a single declaration

The `ref` qualifier on a local variable opts that local into pointer-alias (reference) semantics. Assignment skips `operator=` dispatch and the no-`operator=` error; the local holds a reference to whatever the RHS produces, and subsequent reads and writes go through that reference.

```bgl
class Vec2 { int x; int y; }   // no operator=

void aliasExample() {
    ref Vec2 alias = someInstance;   // alias and someInstance share storage
    alias.x = 99;                    // someInstance.x is now 99
}
```

Rules:
- `ref` is valid on **local variable declarations** and on **class/object member declarations** (see below). Parameters and `extern`/`const` decls reject the qualifier with a targeted error.
- `ref` overrides any `operator=` the class might define for that one local.
- The opt-in is per-declaration; only this local has reference semantics; other locals of the same class follow the default value-semantic rule.

Class parameters use the same model: default reference-semantic, with `byVal class` opting in to value semantics for the whole class (§5.2.6). The `byVal` marker is **class-level**, not parameter-level; there's no per-parameter `byVal` qualifier; mark the class and every parameter of that class type gets the value-semantic copy-in.

#### `ref` members - pointer-alias member fields

A member field may also be declared `ref`, giving it pointer-alias semantics: the field holds a reference to another instance rather than owning a copy. Assignment to a `ref` member is a plain pointer-copy (no `operator=` dispatch), and it defaults to `nothing` until assigned.

```bgl
class Node {
    int id;
    ref Node next;      // a REFERENCE to another Node - not an owned sub-object
    void link(Node n){ self.next = n; }   // pointer-copy, no operator=
}
```

`ref` is the correct (and required) model for a **self-referential** member (a class with a field of its own type). A value member would need `operator=` to copy it, and copying a self-referential field recurses infinitely; a `ref` member sidesteps both. Without `ref`, a stored class-typed member that lacks `operator=` and does not inherit from `object` triggers the same no-`operator=` error as a value local.

Notes:
- A `ref` member's default value is `nothing` until assigned.
- Reading a field *through* a class-typed member chains normally (`self.next.id`, `node.next.next.id`), both self-based and off an external instance.

### 9.2.2 Bypassing the Z-Machine's Local Variable Limit

The Z-machine architecture provides a routine with **15 local variable slots** total (parameters + declared locals combined). This is a hard constraint of the virtual machine.

Beguile handles overflow automatically and transparently. When a routine needs to spill, one slot is reserved for the frame pointer, so a routine with more than 14 user locals spills its excess locals into a global frame pool. This means routines are not limited by the Z-machine slot count, and the spill mechanism is recursion-safe.

- The pool holds up to 64 overflow slots (configurable via `framePoolSize` in `#beguilerSettings`). If all slots are in use and a function attempts to allocate more, the generated code prints a runtime error message and halts.

- Glulx is unaffected; it has no meaningful local variable limit.

## 9.3 Assignment

```bgl
x = expr;
```

The left-hand side must be a previously declared variable or a dotted member path. Type compatibility is checked:

1. If the LHS type defines `emitter T operator = (RHS_type v)`, that emitter is inlined.
2. Else if the RHS type defines `emitter LHS_type operator()`, the conversion emitter is applied.
3. Else if the types are identical or I6-compatible via the conversion operator, a plain assignment is emitted.
4. Otherwise a compile-time type mismatch error is reported.

## 9.4 Compound Assignment

The compound assignment operators modify a variable in-place:

```bgl
n += 2;
score -= penalty;
x *= factor;
```

Supported operators: `+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `<<=` `>>=`

The shift-assign operators `<<=` and `>>=` require `#include <int>`, since the underlying shift operators (`<<`, `>>`) are defined in the int extension.

The LHS type must define the corresponding compound operator. If no matching operator is found and the variable has a known type (not `var`), a compile-time error is reported. The `int` class defines all standard compound assignment operators in the core library.

## 9.5 Increment and Decrement

The `++` and `--` operators are valid as standalone statements in both postfix and prefix forms:

```bgl
n++;    // postfix increment
n--;    // postfix decrement
++n;    // prefix increment
--n;    // prefix decrement
```

The variable's type must define the corresponding operator. The postfix operators (`operator ++`, `operator --`) are required; the prefix-specific overrides (`operator prefix++`, `operator prefix--`) are optional. When a prefix form (`++n`, `--n`) is used, the compiler first looks for the prefix-specific override and falls back to the plain postfix operator if no override exists. This means a type defining only `operator ++` supports both `n++` and `++n` using the same emitter body; a type may provide `operator prefix++` separately only when the two forms should emit different code. If no matching operator is found and the variable has a known type (not `var`), a compile-time error is reported.

## 9.6 Function and Method Calls

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

## 9.7 `if` / `else`

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

## 9.8 `for` Loop

C-style `for` loop with an initializer, condition, and increment expression:

```bgl
for(int i = 0; i < 10; i++) {
    print(i);
}
```

All three parts are required. The initializer may declare a new variable or assign to an existing one;  however, the initializer may not declare a type for previously defined variable, even if it was declared in a the initializer of a previous `for` loop.

### 9.8.1 `for-in` Loop

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

Both forms require the iteration variable's type to be compatible with the array's element type. Incompatible types are a compile error. `var` is accepted as a wildcard for any element type.

```bgl
string p;
for(p in primes) { }  // compile-time error: string ≠ int
```

`auto` infers the element type from the array:

```bgl
for(auto p in primes) { }      // p inferred as int (from array<int>)
for(auto v in {1, 2, 3}) { }   // v inferred as int
```

Element-type inference follows the general `auto` rules, including `operator auto()` (see §14.4.10) and array element typing (§4.9). For inline lists, the element type is inferred from the first element and each element is type-checked against the loop variable type.

### Call-expression form

The right-hand side of `in` may also be a call expression that returns an `array`:

```bgl
for(object o in bglWorld.getAll()) {
    print(o);
}
```

The iteration variable's declared type must match the array's element type. If either side is `var`, no type check is performed (`var` acts as a wildcard). When the RHS is a call expression rather than a named array variable, the element type cannot be statically inferred and is treated as `var`, so any declared type on the iteration variable is accepted.

The array name must be either a declared array variable or a call expression that returns an array. Any other expression form is a compile-time error.

**Nesting** is supported; each loop maintains its own iteration state:

```bgl
for(int row in rows) {
    for(int col in cols) {
        // inner and outer iterations are independent
    }
}
```

### Range form

The `for-in` loop also supports numeric ranges using the `to` keyword:

```bgl
for(int i in 1 to 10) {
    print(i);
}
```

This is syntactic sugar for a C-style counted loop. The range is inclusive on both ends. The above is equivalent to `for(int i = 1; i <= 10; i++)`.

The bounds may be any expression:

```bgl
for(int i in start to start + count - 1) {
    // ...
}
```

## 9.9 `while` Loop

```bgl
while(condition) {
    statements;
}
```

The condition is evaluated before each iteration. If false on first entry, the body is not executed.

## 9.10 `do` / `while` and `do` / `until`

```bgl
do {
    statements;
} while(condition);

do {
    statements;
} until(condition);
```

The body executes at least once. After each iteration, the condition is evaluated:
- `do...while` - repeats while the condition is true
- `do...until` - repeats until the condition becomes true (the condition is the exit test)

Both forms emit directly to their I6 equivalents.

## 9.11 `switch` / `case`

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

Multiple values may share a case by listing them comma-separated. Cases do not fall through by default; the `break` keyword is accepted inside `switch` but is not required and has no effect.

Case values are type-checked against the switch condition type using the full compatibility rules of §11. Integer literals are compatible with `int` conditions; enum values must match the enum type exactly. When the switch expression is of type `verb`, case values are emitted as `##VerbName` action constants automatically.

### Range Cases

A case may specify an inclusive range using the `to` keyword:

```bgl
switch(score){
    case 0: print("Nothing yet.");
    case 1 to 5: print("Getting started.");
    case 6 to 10: print("Making progress.");
}
```

Ranges may be combined with individual values in the same case:

```bgl
case 1, 3, 5 to 10: ...   // matches 1, 3, or anything from 5 to 10
```

Range cases emit as native I6 `to` syntax, which compiles to an efficient range check.

### Comparison Guards

A case may use a comparison operator (`>`, `>=`, `<`, `<=`) instead of a value:

```bgl
switch(score){
    case 0: print("Nothing.");
    case 1 to 49: print("Keep going.");
    case >= 50: print("Well done!");
    case < 0: print("Invalid score.");
}
```

When any case in a `switch` uses a comparison guard, the switch condition is evaluated once and each case is tested against it in order. Values and ranges in the same switch become equality and range checks.

### `operator switch()` - Type-Driven Comparison

When the switch condition's type declares `operator switch()` emitters, those emitters drive each case comparison instead of a native `switch`.

This enables switching on types where I6 `==` is not the correct comparison, for example managed strings:

```bgl
switch(command){
    case "north": goNorth();
    case "look": doLook();
    default: print("I don't understand.");
}
```

The `string` class defines `operator switch(stringLiteral v)` which emits `.equals(v)`, a content comparison rather than a handle comparison. See §14.4.8 for the operator declaration syntax.

Multiple `operator switch()` overloads may be declared for different case value types, enabling mixed-type cases where each value is compared using the appropriate method.

## 9.12 `break` and `continue`

`break` exits the innermost enclosing loop or `switch` immediately.

`continue` skips the remainder of the current loop iteration and re-evaluates the loop condition.

Both require a terminating semicolon. `break` is valid inside `for`, `while`, `do`, and `switch` bodies. `continue` is valid only inside a loop body (`for`, `while`, `do`); using `continue` outside a loop is a compile-time error. When `continue` appears in a loop that contains a `switch`, it advances the loop's iteration as expected; `switch` cases do not intercept it.

```bgl
while(n < 10) {
    if(n == 5) break;
    if(n == 3) continue;
    n++;
}
```

## 9.13 `return`

Exits the current function and optionally returns a value:

```bgl
return;                    // valid in void functions
return someValue;          // identifier
return obj.method(arg);    // call expression
return a + b * c;          // arithmetic expression
```

The return value may be any expression: identifier, method call, arithmetic, or ternary. The expression is type-checked against the function's declared return type.

Returning a value from a `void` function is a compile-time error, with one shorthand exception: `return <void-typed expr>;` is permitted as a synonym for `<void-typed expr>; return;`. This makes the C/I6 idiom `return f();` work when `f` is itself void, letting tail-call patterns port verbatim. The expression must resolve to type `void`; however, any other value-bearing expression in a `void` function still errors:

```bgl
void cleanup() { … }

void doWork(){
    if(skipFastPath) return cleanup();   // OK - cleanup() is void; equivalent to: cleanup(); return;
    if(error)        return 42;          // ERROR - int is not void
}
```

In loose-mode contexts (`#bgl{}` islands and `.inf` precompiler mode; see §14.6), the same shorthand also accepts an expression that resolves to `var`. Unresolved identifiers in those contexts pass through with type `var`; treating that as compatible means `return f();` ports verbatim when `f` is declared in surrounding I6 and not visible to the Beguile resolver.

A non-`void` function must guarantee that every execution path returns a value; the compiler performs control-flow analysis and reports an error if any path can fall off the end of the function without a `return`. Specifically:

- An unconditional `return` at the top level of the function body satisfies the requirement.
- An `if-else` where both the then-block and else-block each satisfy the requirement also satisfies it.
- A `switch` statement where every case body, including a `default` case, satisfies the requirement also satisfies it; a switch without a `default` case does not.
- A loop body alone does not satisfy the requirement, since the loop may not execute.

If the function's local variables have `deinit` emitters, they fire before the `return` is emitted.

## 9.14 `print()` and `log()`

### `print()`

`print()` outputs a value immediately. It is overloaded to accept any value type:

```bgl
print("hello");       // string literal
print(score);         // int or var
print(c);             // char
```

`print()` also supports interpolated strings:

```bgl
print($"Score: {score}  Turns: {turns}");
```

Each segment of the interpolated string is emitted using type-aware dispatch. For example, `print($"Score: {score}")` emits:

```i6
print "Score: ";
print score;
```

When the `<string>` extension is included, `print(string)` is replaced with the managed string printer. See §16.2.4.

### `log()`

`log()` has the same overloads as `print()` but is a **debug-only** output routine. When `DEBUG` is not `#define`d, `log()` calls produce no code at all, not even a conditional check:

```bgl
#define DEBUG
log($"Entering handler for {actor.name}");
```

Without `DEBUG` defined, the above produces zero I6 output. Expressions inside `log()` are still parsed and type-checked in all builds, so errors are caught even in release builds.

## 9.15 `try` / `catch` / `throw`

Beguile provides structured exception handling using the Z-machine's `@catch`/`@throw` opcodes (available on Z-machine v5+ and Glulx). Exceptions unwind the call stack across any number of function calls.

### Basic usage

```bgl
try {
    riskyOperation();
} catch(int errorCode) {
    print("Error: ");
    print(errorCode);
}
```

The `try` block executes normally. If `throw` is executed anywhere during the try block, including inside called functions, execution unwinds to the nearest enclosing `catch` block. The thrown value is assigned to the catch variable.

### `throw`

```bgl
throw 42;              // throw an integer error code
throw errorCode;       // throw a variable's value
throw x + 1;           // throw an expression result
```

`throw` takes a single expression whose value is passed to the catch variable. The thrown value is a single word (matching Z-machine/Glulx word size), so it can be an integer, object reference, or any word-sized value.

If `throw` is executed with no active `try`/`catch` on the call stack, the runtime prints an error message and halts.

### Nesting

Try/catch blocks may be nested. Each `throw` unwinds to the nearest enclosing `catch`:

```bgl
try {
    try {
        throw 42;          // caught by inner catch
    } catch(int inner) {
        throw inner;        // re-thrown to outer catch
    }
} catch(int outer) {
    // outer == 42
}
```

### Cross-frame throwing

`throw` works across call boundaries; the VM unwinds the stack automatically:

```bgl
void deep(){ throw 7; }
void middle(){ deep(); }

void main(){
    try {
        middle();          // throw in deep() unwinds through middle() to here
    } catch(int e) {
        // e == 7
    }
}
```

### Implementation note

Beguile builds `try`/`catch` on the Z-machine's `@catch`/`@throw` opcodes, which provide `setjmp`/`longjmp`-style stack unwinding at the VM level. Nesting works, and no runtime library is required; the VM handles all stack unwinding.

---

# Chapter 10 - Expressions

## 10.1 Overview

Expressions appear as conditions in `if` and loop statements, as initializers in variable declarations, as right-hand sides of assignments, and as arguments to function calls. The compiler assigns each expression a *resolved type*, which drives operator overload resolution, type checking, and emitter dispatch.

## 10.2 Operands

An expression is built from one or more operands joined by operators. Operands are:

- **Literals** - integer (`42`), string (`"hello"`, `@"raw"`), character (`'x'`), dictionary word (`.word`, `..word`)
- **Identifiers** - resolved by scope lookup (see Chapter 12); type is the declared type of the variable, parameter, or enum value
- **`null`** - compatible with any type (see §4.6)
- **`self`** - resolved type of the enclosing class
- **Method call results** - resolved type is the method's return type
- **Subscript expressions** - `arr[i]` resolves as the element type of the array
- **Parenthesized sub-expressions** - `(expr)` has the same type as `expr`

All literal values have an associated pseudo-type (see §4.5) and may have emitter methods called directly on them, e.g. `"hello".print()`, `42.someMethod()`. See §4.5 for the full list of literal pseudo-types.

## 10.3 Binary Operators

Binary operators join two operands. Resolution proceeds as follows:

1. The compiler looks for an operator (emitter or method) on the LHS type matching the operator name and the RHS type.
2. If not found, it checks if the LHS type has a conversion operator (`operator()`) that maps to the RHS type; if so, the operator is emitted verbatim as I6 (raw fallback).
3. If not found, it checks if the RHS type has a conversion operator that maps to a type for which the LHS does have a matching operator.
4. If no match is found and the LHS has a known type, a compile-time error is reported. Untyped (`var`) operands fall through to raw I6.

Comparison operators (`==`, `!=`, `<`, `>`, `<=`, `>=`, `?=`, `=~`) produce `eBool` as their resolved type. Logical operators (`&&`, `||`) also produce `eBool`. Assignment and arithmetic operators preserve the LHS type. See §5.6.6 for the full list of overloadable operators. The `?=` operator has no built-in definition; it exists as an overloadable comparison for a user type to give a custom meaning (§5.6.5).

Beguile follows standard C-style operator precedence. Operators at higher precedence levels bind more tightly. Within the same precedence level, operators associate left-to-right.

| Precedence | Operators | Category |
|:----------:|-----------|----------|
| 11 | `*`  `/`  `%` | Multiplicative |
| 10 | `+`  `-` | Additive |
| 9 | `<<`  `>>` | Shift |
| 8 | `<`  `<=`  `>`  `>=` | Relational |
| 7 | `==`  `!=`  `?=`  `=~` | Equality |
| 6 | `&` | Bitwise AND |
| 5 | `^` | Bitwise XOR |
| 4 | `\|` | Bitwise OR |
| 3 | `&&` | Logical AND |
| 2 | `\|\|` | Logical OR |
| 1 | `? :` | Ternary (always lowest) |

This means `a + b * c` is parsed as `a + (b * c)`, and `a > 0 && b < 10` is parsed as `(a > 0) && (b < 10)`.

Within the same precedence level, chaining resolves left-to-right:

```bgl
str + " followed" + apnd
// resolves as: (str + " followed") + apnd
// each + resolves in turn, using the return type of the previous call as the new LHS type
```

## 10.4 Ternary Operator

The `?:` operator selects between two values based on a condition:

```bgl
condition ? trueExpr : falseExpr
```

The ternary operator may appear as an argument to a function call, on the right-hand side of an assignment, or inside a parenthesized sub-expression. The resolved type is taken from the true branch.

```bgl
print(x > 0 ? "positive" : "non-positive");
int result = (cond ? a : b) + extra;          // ternary inside parens
```

**Constraints:**
- Both branches should be the same type for predictable results.
- Only one ternary may appear per statement.
- Ternaries may not be nested inside another ternary's condition.

A function call argument that is itself a ternary counts as the one permitted ternary for that statement. A ternary in the true or false branch of another ternary is therefore not supported.

### Ternary in `for`-Loop Conditions and Increments

Ternary expressions are supported in `for`-loop conditions and increment clauses. Because I6 `for` syntax does not support ternary directly, the compiler lowers these:

- **Condition**: a ternary in the condition is evaluated into a temporary variable before the `for` statement, and re-evaluated at the end of each iteration.
- **Increment**: a ternary in the increment clause is moved to the end of the `for` body.

```bgl
for(int i = 0; i < (flag ? 10 : 5); i += (flag ? 2 : 1)) {
    // works correctly
}
```

## 10.5 Optional Chaining, Null Coalescing, and Postfix Query

These three operators provide safe navigation through potentially-null object references. They are type-driven: each requires the operand's type to declare an `operator ?()` emitter (see §14.4.8). Types without `operator ?()` cannot use these operators.

### Optional Chaining `?.`

The `?.` operator accesses a member or calls a method only if the left-hand operand is non-null; however, if the operand is null, the entire chain short-circuits to `0`/`nothing`.

```bgl
object parent = noun?.parent();         // null if noun is null
string name = noun?.parent()?.name;     // null if either is null
```

In **expression context**, each `?.` step first tests the current value with the `operator ?()` emitter; if it is null the whole chain short-circuits to null (`nothing`), otherwise it proceeds to the next step:

```bgl
object y = x?.parent();   // null if x is null; otherwise x.parent()
```

In **statement context**, `?.` guards the statement: the operation runs only if the target is non-null.

```bgl
x?.remove();   // removes x only if x is non-null
```

Optional chains may be mixed with regular `.` access. A regular `.` after a `?.` does not add a null guard; if the preceding `?.` short-circuited, the regular `.` executes on the null value (which may produce undefined behavior, just as a regular `.` on null would):

```bgl
object container = noun?.parent();   // guarded: null if noun is null
string desc = noun?.parent().description;  // parent() is guarded, but .description is not
```

### Null Coalescing `??`

The `??` operator provides a fallback value when the left-hand operand is null. The right-hand operand is only evaluated if the left is null (short-circuit semantics).

```bgl
object dest = actor?.destination ?? location;
```

`??` is commonly combined with `?.` but works on its own:

```bgl
object target = customTarget ?? defaultTarget;
```

### Postfix Query `v?`

The postfix `?` operator inlines the type's `operator ?()` emitter directly, producing a boolean result. For the `object` type, `operator ?()` is defined as `$self ~= nothing`, making `noun?` equivalent to `noun != null`. Other types may define `operator ?()` with different semantics (e.g., a string type could test for non-empty).

```bgl
if(noun?) print("something is here");    // equivalent to: if(noun != null)
if(!noun?) print("nothing here");        // equivalent to: if(noun == null)
```

`!v?` correctly negates the result using I6 `~~()` syntax.

**Disambiguation with ternary `?`:** When `?` appears in a precedence sub-expression context, for example as the right-hand side of `==` or another binary operator, it is treated as the ternary operator, not the postfix query. The postfix query only fires when the `?` ends the current expression rather than beginning a ternary condition.

### The `operator ?()` Emitter

All three operators delegate to a zero-parameter emitter declared on the operand's type. The `object` class in `__beguileCore.bgl` defines it as:

```bgl
extern class object {
    emitter eBool operator ?() { $self ~= nothing }
}
```

The core `string` class also defines `operator ?()` as `$self ~= 0`, testing for a valid (non-zero) string handle. This enables `string ?? string` null coalescing and `string?.method()` optional chaining without requiring `#include <string>`.

Other types may define their own semantics. The compiler has no built-in knowledge of what "null" means; it is entirely type-defined.

## 10.6 Type Cast

A type cast overrides the resolved type of an expression at compile time, redirecting operator and method dispatch through the specified type's members:

```bgl
(TypeName)expr
```

No I6 cast is emitted; this is a compile-time-only annotation. It is useful for:

**Explicit parent dispatch** - when a subclass overrides a method and you want to call the parent's version:

```bgl
(Animal)myDog.speak();   // dispatches speak() through Animal, not Dog
```

**Explicit conversion operators** - triggering a conversion operator marked `explicit` that won't fire during implicit resolution (see §5.6.4):

```bgl
explicit emitter string operator();    // only via cast
string s = (string)myValue;            // invokes the explicit conversion
```

**Disambiguation in expressions** - forcing operator resolution through a specific type when the inferred type would resolve differently.

The cast applies to the immediately following identifier or method call. It does not propagate through a chain.

### 10.6.1 Casting to a class vs. an instance

A cast target may be a **class type** *or* a specific **object instance**. Casting to a class exposes that class's members; casting to an instance additionally exposes that instance's **own members**, the fields/methods declared directly on the object, not on any class:

```bgl
class Room : object { int lit; }
Room library { int shelves; }        // `shelves` lives on the instance, not on Room

int a = ((library)obj.parent).shelves;   // instance-only member - reached via the instance cast
int b = ((Room)obj.parent).lit;          // class member - reached via the class cast
```

This is the type-safe way to reach members through a **dynamically-typed value**, most commonly `.parent`, which is statically `object` because the world tree is mutable (see §4.9.2's sibling discussion for the analogous array case). It is an **unchecked downcast**: you're asserting the runtime object really is that class/instance; if it isn't, the read goes through the asserted layout and yields garbage, the same risk as any cast. The member name, however, *is* checked at compile time against the cast target (accessing a member the target doesn't have is an error).

For a fully untyped escape, cast to `var`: `((var)obj.parent).field` emits the raw property read with **no** compile-time member check (use when you don't want, or can't name, a concrete target).

**Forgetting the cast is a compile error, not silent garbage.** Reaching a member that isn't on `object` through an `object`-typed value *without* a cast is diagnosed at the member:

```bgl
int x = obj.parent.cap;   // ERROR: 'cap' is not a member of 'object'. This value is dynamically
                          // typed (e.g. from '.parent') - cast to the concrete type to reach its
                          // members: ((SomeType)obj.parent).cap  (or ((var)obj.parent).cap …).
```

The error points you at the cast idioms above rather than dropping the trailing member and surfacing a confusing downstream type mismatch. It fires only in strict mode (loose `#bgl` islands keep raw I6 passthrough) and only when the member genuinely isn't on `object` (`obj.parent.parent`, reaching `object`'s own `parent`, resolves normally). The method form (`obj.parent.someMethod()`) already reported this accurately ("No method 'someMethod' on type 'object'").

## 10.7 `new` and `delete` for Pooled Classes

`new TypeName(args)` allocates an instance from a pooled class's reserved slots; `delete instance` returns the slot to the pool. Both are valid only for classes declared with `[N]` (sized pool) or `extern class Foo[]` (extern marker). See §5.2.5 for the declaration form.

```bgl
class marbleClass[10] : object {
    int weight = 0;
    void create(int w){ weight = w; }
}

void main(){
    marbleClass m = new marbleClass(5);   // allocate; calls create(5)
    if(m == nothing){
        // pool was exhausted
        return;
    }
    // ... use m ...
    delete m;                             // calls destroy() (if defined), returns slot to pool
}
```

#### `new TypeName(args)` - expression

- Returns a reference to an allocated pool slot, or `nothing` if the pool is exhausted. Always check the result before use.
- Arguments are passed to the class's `create()` method. If `create` is not defined, `new TypeName()` (no args) is the only valid form.
- Z-machine caps `create` arguments at 3.
- Compile-time error: `new` on a non-pooled class.

Emits as I6 `TypeName.create(args)`, the class-message form.

#### `delete identifier` - statement

- Returns the slot to the pool. The class's `destroy()` method (if defined) is called immediately before the slot is freed.
- Compile-time error: `delete` on a variable whose type is not a pooled class.

Emits as I6 `TypeName.destroy(identifier);`.

#### Pool exhaustion

`new` returning `nothing` is the only signal that the pool is full. There is no automatic growth; pool size is fixed at compile time. Patterns:
- Check `if(x == nothing)` after every `new` if exhaustion is recoverable.
- Size the pool conservatively at declaration time if exhaustion would be a logic bug.

## 10.8 Lambda Functions

A lambda function (also called an *anonymous* function) is a function literal that can be assigned to a variable, passed as an argument, or stored as a property. It has no name of its own.

### 10.8.1 The `func<>` Type

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

`func<>` works as a variable, a parameter, a return type, and as the **element type of a generic collection** (including doubly-nested closes like `array<func<T>>`, whose trailing `>>` the type parser splits automatically). A func-typed collection is directly callable through a for-in loop variable:

```bgl
array<func<eVerdict>> rulebook = { ruleA, ruleB };
for(func<eVerdict> r in rulebook) { eVerdict v = r(); ... }   // r() calls the element
```

`func<>` also works fully as a **class/object member (property)** — declare it, assign to it, read it, and call it directly (`obj.handler()` runs the routine stored in the property), including members typed `array<func<...>>`:

```bgl
class Menu : object {
    func<void, int> onSelect;         // a func-typed member
    array<func<eVerdict>> rules;      // a member collection of functions
}
menu.onSelect = pickItem;             // assign
menu.onSelect(3);                     // call through the property
for(func<eVerdict> r in menu.rules) { r(); }
```

Lambdas support **closures**: they can capture variables from the enclosing scope. When a lambda body references a local variable or parameter from the enclosing function, the compiler automatically captures its value at the point where the lambda is created.

```bgl
void test(int multiplier){
    apply((int x) => { return x * multiplier; }, 5);  // captures 'multiplier'
}
```

Multiple variables can be captured, and each is independently stored.

### 10.8.2 Lambda Literal Syntax

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

### 10.8.3 Passing Lambdas as Arguments

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

### 10.8.4 Closures and Capture

A lambda may reference variables from the enclosing function's scope, capturing them for use inside its body.

```bgl
void test(int a, int b){
    // captures both 'a' and 'b'
    apply((int x) => { return x * a + b; }, 2);
}
```

Capture works when the lambda appears inside any control-flow body (`for`, `while`, `do`, or `if`). The capture scan walks the full enclosing function scope, not just the immediate block.

**Capture semantics:**
- For **inline lambdas** (passed directly as arguments), modifications to a captured variable inside the lambda body are visible to the enclosing scope after the call returns.
- Captures work correctly for immediate callbacks (the dominant IF pattern), but a lambda stored for later invocation may see its captured values overwritten by subsequent lambda creations.
- Non-capturing lambdas incur no capture overhead.

### 10.8.5 Constraints

- **No immediate invocation.** The syntax `((int n) => { print(n); })(42)` is not supported. Assign to a variable or pass as an argument first.
- **Capture lifetime.** Captured values are stored in globals and may be overwritten if another closure captures from the same scope. Closures are designed for immediate callback use (e.g. `getFiltered`, `applyToAll`), not long-lived storage.

---

# Chapter 11 - Type Compatibility and Conversion

## 11.1 Overview

Beguile checks type compatibility at every assignment, variable declaration initializer, and function call argument. The compiler applies implicit conversion automatically when an exact type match is not present, using a defined priority order.

## 11.2 Compatibility Rules

A value of type `A` is compatible with a target of type `B` if any of the following hold, checked in order:

1. **`var` wildcard** - `var` is compatible with any type in both directions (source or target).
2. **`null` universal** - `null` is compatible with any target type (emits as `nothing` / `0`).
3. **Exact match** - `A == B`.
4. **Class hierarchy** - if `A` inherits from `B` (directly or through a chain of base classes and aliases), `A` is compatible with `B`. The reverse (assigning a base type to a derived type) is blocked; `object` cannot be assigned to `Room`.
5. **Assignment operator** - type `B` defines `operator = (A v)` (the target type accepts the source type via an operator).
6. **Conversion operator** - type `A` defines `operator()` returning `B` (the source type converts to the target type). Only implicit conversions participate; `explicit` conversions require a cast (see §10.6).
7. **`func<>` / `array<>` generics** - `func` is compatible with any `func<...>` type; `array` is compatible with any `array<T>` type.
8. **bnum → int widening** - any `bnum` value is compatible with `int` (see §4.7); however, the reverse direction (`int` → `bnum`) requires an explicit cast. Plain `enum` types do not widen.

If none apply, the assignment or call is a compile-time type mismatch error.

## 11.3 Priority at Assignment

When assigning `lhs = rhs`:

1. If LHS type has `operator = (RHS_type v)`, use that operator (emitter or method).
2. Else if RHS type has an implicit `operator()` returning the LHS type, apply the conversion. Pass-through conversions (`;` terminated) emit a plain assignment; conversions with a body transform the value.
3. Otherwise, a compile-time error.

This priority is consistent across assignment statements (`x = expr;`), variable declaration initializers (`T x = expr;`), and function call arguments.

## 11.4 Priority at Function Call Arguments

For each argument, the same priority applies: exact type match first, then implicit `operator()` conversion, then `var` fallback. If multiple overloads exist, exact matches win over conversion matches, which win over `var` fallbacks (see §7.4).

## 11.5 Example

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

# Chapter 12 - Scope and Identifier Resolution

## 12.1 Overview

When the compiler encounters an identifier in an expression or statement, it resolves it by searching through a sequence of scopes in priority order. The first match found wins; later scopes are not searched.

## 12.2 Resolution Tiers

Identifiers are resolved through four tiers, searched in order:

**Tier 1 - Local scope**
1. Function parameters of the enclosing function
2. Local variables declared in the current block
3. Local variables declared in enclosing blocks of the same function

**Tier 2 - Class/object scope**
4. Members of the current class or object (when inside a method body), **including inherited members from the class's base chain**. Resolved as `self.memberName` in the emitted I6. Forward references to own members declared later in the object's body resolve correctly; the compiler pre-scans property declarations before parsing method bodies.

**Tier 3 - Global scope**
5. Enum values (all declared enum members are in a flat global namespace)
6. Global variables, constants, extern declarations, and verb names

If no tier matches, the identifier is undeclared, a compile-time error.

**Ambiguity warning.** Inside an object method body, when a bare identifier resolves at Tier 3 (a file-scope candidate, typically an enum value, global, or imported member) AND is **also a property of the enclosing object** (own or inherited), the compiler can't tell which one the user meant. The Tier-3 candidate wins by the precedence rules above; however, a warning is emitted so the genuine ambiguity is surfaced rather than silently picked. The diagnostic points at both meanings and the two qualifications that disambiguate: `self.X` for the property, or `::X` (§12.3) to force the global/file-scope candidate. Method-bearing inherited members (e.g. inherited `print` on `_bglObject`) are intentionally NOT included in this check; methods are always called with parens and don't suffer the same visual collision as bare property reads.

## 12.3 Global-Scope Qualifier `::`

A leading `::` on an identifier forces resolution at **file (global) scope**, skipping Tier 1 (locals/parameters) and Tier 2 (enclosing class/object members). It is the counterpart to `self.X`: where `self.X` names the member, `::X` names the global.

```bgl
verb Frob {
    void handler() {
        ::meta = true;      // the I6 global `meta`, not this verb's `meta` property
        if (::meta) ...     // also valid as an rvalue
    }
}
```

`::X` emits the bare global name (`meta`); the `::` is a compile-time resolution directive, not part of the emitted I6. It works as both an lvalue and an rvalue, and applies to the **head** of a dotted path (`::obj.member` forces `obj` to global scope, then reads `member` on it). Because `::X` is unambiguous, it also **suppresses the member-shadow ambiguity warning** described above. Using `::X` when no such global exists is an "undeclared identifier" error; it never falls back to a member.

## 12.4 The `self` Keyword

`self` refers to the receiver object within a method body. Bare member names are automatically qualified with `self.`; see §5.4 for details. `self` is only valid inside a class or object method body.

## 12.5 Variable Shadowing

Local variables, function parameters, and `for`-loop variables are checked against several scopes. The severity depends on the kind of symbol being shadowed:

**Errors (always fatal):**
- **Global variables** - I6 provides no mechanism to access a shadowed global. Exception: globals declared as symbolic-constant types (`attribute`, `property`, `verb`, and `grammarToken` enum values) do not trigger the shadow error. They name compile-time constants used in specific syntactic contexts (`give`, `provides`, action comparisons, grammar lines), not writable runtime storage, so a parameter or local of the same name doesn't actually shadow anything reachable in code.
- **Registered type names** (classes, enums) - these are stored in the global namespace and collide with user-chosen identifiers by name, even though they lex as a distinct token kind.

**Warnings (non-fatal):**
- **Direct class/object members** - when inside a class or object method body. Use `self.name` to access the shadowed member.
- **Inherited members** - when a local/param/loop variable shadows a member inherited from a base class. Use `self.name` to access the inherited member.
- **Lambda capture conflicts** - when a lambda-local variable shadows a capturable outer local or parameter.
- **Member overriding parent member** - produces a warning; suppress with `replace` or `default` (see §5.8).

```bgl
int score = 0;     // global
class Counter { int n = 0; }

void foo() {
    int score = 5;                           // ERROR: shadows global variable
    int Counter = 0;                         // ERROR: shadows class 'Counter'
    for(int score = 0; score < 10; score++); // ERROR: loop variable shadows global
}

class Room : object {
    string description;
    void init(string description) { }        // WARNING: parameter shadows class member
                                             // use self.description to access the member
}
```

## 12.6 Verb Names vs. Variables

Verbs are objects in Beguile and follow the same name resolution rules as other identifiers. If a local variable has the same name as a verb, the variable takes priority (Tier 1 to 3 before Tier 4). The I6 `##VerbName` prefix is an internal emission detail handled by the `verb` class's `operator ==` emitter; the user never writes `##` directly.

## 12.7 Global Name Collision Detection

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

**Beguile vs. raw I6 collisions** (symbols declared via `#includeI6` or `#i6` blocks) are invisible to the Beguile parser and will surface as I6 compiler errors instead. Using `extern` declarations in a bindings file is the recommended way to make I6 symbols known to the Beguile type system and prevent silent conflicts.

**Naming conventions to avoid collisions:**
- Compiler-generated symbols use the `bgl` and `_bgl` prefixes (reserved; see §2.4.1)
- User code should use application-specific prefixes for any global variables or types intended to coexist with included library files

---

# Chapter 13 - Verbs and Grammar

## 13.1 Overview

Verbs and grammar are the mechanism for adding new player commands to an IF game. A `verb` object defines an action and its handler. Grammar rules define the input patterns that the game parser recognizes as triggering an action.

## 13.2 Verb Declarations

A `verb` declaration creates a named Beguile object that is an instance of the `verb` class. Unlike I6, which uses specialized `Verb` directives and `*` grammar syntax, Beguile verb bodies use standard object member syntax: methods, properties, and typed members are all declared the same way as on any other object:

```bgl
verb Examine {
    grammarRuleList grammar = {
        {.examine, NOUN},
        {.x, NOUN},
    }
    void handler() {
        print("You examine it closely.");
    }
}
```

The type `grammarRuleList` can be inferred from the class declaration, so `grammar = { ... }` without the type name is also valid.

**Single-line shorthand** (a *pretty lie*; see §1.2). When the `grammar = { ... }` assignment's first content token is a dictionary-word literal, the outer braces are interpreted as the lone grammar line's braces, so the inner braces may be omitted:

```bgl
verb Whistle {
    grammar = {.whistle, noun};                 // shorthand
    void handler() { print("You whistle."); }
}

verb Whistle {
    grammar = { {.whistle, noun} };             // canonical, equivalent
    void handler() { print("You whistle."); }
}
```

Multi-trigger via `|`-alternation still works in the shorthand: `grammar = {.hum|.murmur, noun};`. The shorthand only applies when a single grammar line is being declared; multi-line grammars use the canonical form with one set of braces per line. The shorthand is recognized wherever a grammar line list is accepted — verb declarations (`grammar = { … }`), `extend` blocks (`grammar += { … }`, `grammar -= { … }`, `replace grammar = { … }`), standalone grammar objects, and extern verb bodies — because they all go through the same parser path.

### 13.2.1 Defining behavior: `handler()`

`handler()` is the verb's action body: it is called automatically when the player enters a command matching the verb's grammar, and its body becomes the I6 `<verbName>Sub` action routine. The base `verb` class declares `handler()` as a `default` method (see §5.8), so overriding it requires no `replace` qualifier. **A non-extern verb must define `handler()`**; omitting it is a compiler error (the verb would otherwise inherit the default Sub bridge and recurse at runtime). Extern verbs are exempt (their `Sub` is library-defined; see *External Verbs* below).

### 13.2.2 Launching an action: `perform()`

To *run* an action from your own code, the Beguile replacement for I6's `<...>` and `<<...>>` syntax, call `perform()` on a verb:

```bgl
Enter.perform(door);            // run the Enter action on `door`      (I6: <Enter door>)
Take.perform(coin, pouch);      // ...with a second noun                (I6: <Take coin pouch>)
Take.perform(coin); rtrue;      // the <<Take coin>> form: run, then return true (explicit)
```

`perform()` emits the I6 `<...>` form under the hood, so the active I6 library (standard library, PunyInform, …) binds its own action runner: it saves and restores the current `actor`/`action`/`noun`/`second` and runs the surrounding before/after rules. You never write `<...>` yourself, and Beguile never hardcodes the library's runner routine. `perform()` works on **any** verb, native or `extern`, whether or not it defines a `handler()`. The `<<...>>` "run and return true" form is just `perform(...)` followed by an explicit `rtrue;`.

### 13.2.3 External Verbs

A verb with no Beguile handler (one that reuses an I6 library verb's behavior) is declared as `extern verb`:

```bgl
extern verb Take;
extern verb Drop;
extern verb Go;
```

`extern verb` declarations register the name in Beguile's type system for use in `switch(action)` comparisons, grammar lines, `perform()` launches, and method calls. An extern verb needs no `handler()`; its `<verbName>Sub` routine is defined by the I6 library, so it is exempt from the must-define-`handler()` rule. `Take.perform(noun)` launches the library's Take action exactly as it would a native verb (emitting `<Take noun>`).

#### Declaring Claimed Dictionary Words

An I6 verb may claim multiple trigger words (e.g. `'inventory' 'inv' 'i'` all triggering Inv). To make those claims visible to the Beguile compiler, so a new grammar line in user code referencing one of those words emits as an `Extend` directive rather than a duplicate `Verb` directive, declare them inside the extern verb's body using the same `grammar` syntax used by non-extern verbs.

**Pretty form** (most common; a *pretty lie*; see §1.2). When the extern verb's body begins with a dictionary-word literal, the body content is interpreted as the trigger-word section of a single grammar line:

```bgl
extern verb Inv  { .inventory|.inv|.i }
extern verb Take { .take|.carry|.hold|.get|.pick|.peel }
extern verb Quit { .q|.quit|.die }
```

**Canonical form**, equivalent to the pretty form above. Identical to non-extern verb body syntax:

```bgl
extern verb Inv {
    grammar = {
        {.inventory|.inv|.i},
    };
}
```

Both forms produce the same result and the same claimed-words set; choose whichever reads better at the call site. The pretty form is typical for BLR bindings and single-line declarations; the canonical form when the body contains multiple grammar lines or other members.

Three rules apply (canonical form examples shown):

- **Multi-trigger uses `|`-alternation** (see §13.4 *Multi-trigger grammar lines*). The two canonical forms below produce the same claimed-word set:

  ```bgl
  extern verb V { grammar = { {.a|.b}, {.c|.d} }; }   // two lines
  extern verb V { grammar = { {.a|.b|.c|.d} }; }      // one line
  ```

  The pretty form is the second shape:
  ```bgl
  extern verb V { .a|.b|.c|.d }
  ```

- **Pattern tokens after the trigger are ignored** (canonical form only; the pretty form's syntax accepts only trigger words). Beguile never emits I6 grammar for extern verbs (the I6 grammar is defined externally), so anything past the trigger position has no effect. Writing one is allowed (it can document the expected pattern shape) but the compiler issues a warning so the author isn't surprised:

  ```bgl
  extern verb PutOn { grammar = { {.put, noun, .on, noun} }; }
  // warning: extern verb 'PutOn': pattern tokens after the trigger word(s)
  //          in `grammar = {...}` are ignored - extern verbs don't emit
  //          I6 grammar; only the dict words in the first position
  //          contribute to the verb's claimed-words list
  ```

- **A bare `extern verb V;` defaults to a single claimed word, the lowercased verb name.** The body form is only needed when the verb claims additional words or when the primary word differs from the lowercased name.

The first dict word in the first grammar line is the verb's **primary trigger**, the one used as the target word when the verb is extended via `extend V { grammar += { … } }`. Order matters only for that primary-trigger selection; all listed words are equally claimed for collision-detection purposes.

### 13.2.4 Meta Verbs

The `verb` class exposes a `bool meta` property. Setting it to `true` lifts the I6 `meta` keyword onto the emitted `Verb` directive, marking the verb as out-of-world (it runs without advancing game turns or triggering daemons):

```bgl
verb Inventory {
    meta = true;
    grammar = {
        {.inventory},
        {.i},
    };
    void handler() { /* ... */ }
}
```

Emits one I6 `Verb meta` directive per distinct trigger word the verb owns (Beguile groups grammar lines by their first dictionary word):

```inf
verb meta 'inventory'
    * -> inventory;
verb meta 'i//'
    * -> inventory;
```

(`'i//'` is the I6 single-character dictionary-word form, automatically emitted for one-letter words.)

The `meta` property is recognized only on `verb` instances; the compiler suppresses it from the I6 `with` clause and lifts it onto each emitted Verb directive.

Extern verbs cannot be marked meta from Beguile. An extern verb is defined in I6, and the I6 declaration already carries `meta` (or doesn't); there is no Beguile-side runtime use case for re-marking it.

### 13.2.5 Verb Priority

The `verb` class exposes an `int priority` property (default `10`, declared in the Beguile Language Runtime's `_verb.bgl`). Priority controls how grammar rules from multiple sources are ordered when they target the same trigger word. It applies at the **verb** level (to all grammar lines authored in the verb's own block) and also at the **extend** level, where it acts as a block-local directive.

```bgl
verb Take {
    priority = 5;             // this verb's anchor (default would be 10)
    grammar = {
        {.take, noun},
        {.grab, noun},
    };
    void handler() { /* ... */ }
}
```

The priority on a Beguile-defined verb's own block is the **anchor**, the pivot against which extends sort. For purely extern verbs (no Beguile-defined block), the implicit anchor is the BLR default (`10`).

Inside `extend V { … }`, a bare `priority = N;` is a **block-local directive**; it does not become a persistent property on the verb. It stamps onto every grammar line added by the surrounding `grammar += { … }`. It is not a persistent property, so multiple `extend` blocks at different priorities coexist on the same verb without colliding.

```bgl
extend verb Look {
    priority = 5;          // 5 < anchor 10 → emits I6 `Extend 'look' first`
    grammar += { {.peek, noun}; }
}

extend verb Look {
    priority = 12;         // 12 > anchor 10 → emits I6 `Extend 'look'` (default last)
    grammar += { {.look, .carefully, noun}; }
}
```

**Emission rules.** For each trigger word grouped under a verb, grammar lines are partitioned by priority relative to the anchor:

| Bucket | Condition | I6 directive |
|---|---|---|
| Anchor-own | Lines authored in the verb's own block | `Verb 'w' …` (or `Extend 'w' first …` when the verb's own block adds new trigger words to an extern verb) |
| Less than anchor | `priority < anchor` | `Extend 'w' first …` |
| Greater than or equal | `priority ≥ anchor`, not in the verb's own block | `Extend 'w' …` (default last) |

A lower priority *number* means a higher matching priority: the I6 parser tries `Extend first` rules before the anchor's own rules, and the anchor's own rules before plain `Extend` rules. Per-rule priority on grammar objects (see §13.4) participates in the same sort.

`priority` is recognized only on `verb` instances. The compiler suppresses it from the I6 `with` clause; it affects rule ordering and is not a runtime property.

Extern verbs cannot carry a Beguile-side priority. Extends of extern verbs whose only Beguile-side contribution is a `grammar += { … }` with no `priority = N;` use the default anchor (10) against the extern verb's implicit anchor.

## 13.3 Action Comparisons

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

## 13.4 Grammar

Grammar rules define what the player can type and which verb action is triggered. Rules are declared either on a verb (verb-centric) or in a standalone grammar object (grammar-centric). Both produce the same I6 output.

### 13.4.1 Grammar Types

| Type | Purpose |
|------|---------|
| `grammarToken` | An extern enum declared by the library binding (i6StandardLibrary, punyInform). Values include `noun`, `held`, `creature`, etc. Supports parameterized forms: `noun(Routine)` filters matches, `scope(Routine)` sets scope. |
| `dictionaryWord` | A dictionary word used in patterns (`.examine`, `.put`, etc.). Supports `\|` alternatives and plural forms (`..words`). |
| grammar pattern | An `array<patternElement>`, a sequence of `patternElement` values (dictionary words and grammar tokens): `{.examine, noun}`. |
| `grammarRule` | Pairs a verb with a pattern: `{Examine, {.examine, noun}}`. |
| `grammarRuleList` | A collection of grammar rules. The type of the `grammar` member on `verb` and of standalone grammar objects. |

These types can be used as members on any object. A `grammarRule` has two initializer forms:
- **Explicit verb**: `grammarRule r = {Examine, {.examine, noun}}` works in any context. Accepts an optional **trailing positional priority** (third element): `grammarRule r = {Examine, {.examine, noun}, 5}`. Omitted, priority defaults to the BLR `class verb` default (`10`). See *Per-Rule Priority on Grammar Objects* below.
- **Inferred verb**: `grammarRule r = {.examine, noun}` infers the verb from the owning object. The owning object should be a `verb` or a subclass of `verb`; a warning is issued otherwise. Inferred-verb lines inherit their priority from the owning verb's anchor (or from `priority = N;` in the enclosing `extend` block, see §13.2); the per-rule third-element form does not apply here.

`grammarRuleList` is a container for `grammarRule` entries in either form.

### 13.4.2 Grammar Pattern Tokens

Each element of a pattern is one of:

| Token form | Meaning |
|------------|---------|
| `.word` | Dictionary word literal, matches the player typing that exact word |
| `..words` | Plural dictionary word |
| `.word1 \| .word2` | Alternative dictionary words, matches any one of them; may be wrapped in optional parentheses |
| `noun` | Matches any in-scope object |
| `held` | Matches a held object |
| `creature` | Matches a creature or actor |
| `topic` | Matches a topic phrase |
| `multi` | Matches one or more in-scope objects |
| `multiheld` | Matches one or more held objects |
| `multiexcept` | Matches one or more in-scope objects, excluding one already matched (used after a preposition, e.g. `multiexcept 'in' noun`) |
| `multiinside` | Matches one or more objects inside a specific container (used after a preposition, e.g. `multiinside 'from' noun`) |
| `number` | Matches a number typed by the player (range-checked) |
| `anynumber` | Matches any number (no range check) |
| `special` | Matches a number or dictionary word |
| `attributeName` | Matches objects that have that attribute (e.g. `container`, `animate`) |
| `RoutineName` | General parsing routine, a declared global function called by the parser as a custom token; handles its own input matching |
| `noun(Routine)` | Noun filter; the parser matches nouns normally, then calls Routine for each candidate to accept or reject it |
| `scope(Routine)` | Scope setter; Routine modifies which objects the parser considers in scope for this grammar line |

Grammar tokens are values of the `grammarToken` extern enum (declared in the library binding). Both bare access (`noun`) and qualified access (`grammarToken.noun`) are accepted in pattern position. In pattern position a bare `noun` resolves to the grammar-token value even when an `extern object noun` of the same name exists at file scope (e.g. the I6 runtime `noun` global). Outside pattern position the global wins, and `grammarToken.noun` qualified access selects the token if needed.

The compiler validates that bare identifiers in grammar patterns are declared as `grammarToken`, `attribute`, or a global function. Plain object or variable declarations are not valid in this position. Unrecognized or wrong-typed names are a compile error.

### 13.4.3 Advanced Pattern Syntax

**Dictionary word alternatives** - multiple dictionary words separated by `|` match any one of them. Alternation is allowed in any position, including the **first** (the verb trigger word):

```bgl
verb Stow {
    grammar = {
        {.stow, held, .on | .onto | .upon, noun},
        {.stow, held, (.in | .into | .inside), noun},
    }
}
```

**Multi-trigger grammar lines** - when `|`-alternation appears in the first position, the line declares a single shared pattern that fires on any of the listed trigger words:

```bgl
verb TypeNum {
    grammar = {
        {.type | .enter | .put, number, .into | .in | .on | .onto, noun},
    }
    void handler() { print("You can't type anything there."); }
}
```

The alternation is expanded so each trigger word becomes its own line, and each word lowers to its **own** I6 `Verb` directive (per-word emission — every dictionary word owns exactly one I6 verb):

```inf
verb 'type'  * number 'into'/'in'/'on'/'onto' noun -> typenum;
verb 'enter' * number 'into'/'in'/'on'/'onto' noun -> typenum;
verb 'put'   * number 'into'/'in'/'on'/'onto' noun -> typenum;
```

This is more `Verb` directives than I6's grouped `Verb 'type' 'enter' 'put'` form, but it is runtime-identical (each word triggers the same pattern → the same action). The uniform one-word-per-line model is what lets `grammar -=` (§13.4) remove a single word's line, and it routes mixed old/new words correctly with no manual workaround:

- A **new** trigger word emits as a fresh `Verb 'w' …`.
- A trigger word **already claimed** by another verb emits as an `Extend`, adding the pattern to that verb without a combined-`Verb` collision or "verb already defined" warning. Which `Extend` depends on whether the claiming verb is grouped (see "Word-precise library extension" below): a word owned by a *grouped* I6 library verb is word-precise by default (`Extend only 'w'`); a solo library word, or a word claimed by a prior Beguile verb, emits plain `Extend 'w'`.

This applies equally to multi-trigger lines inside `extend V { grammar += { … } }`.

### 13.4.4 Trailing line modifiers (`reverse`, `withI6Synonyms`)

Two optional pseudo-tokens may appear at the **tail** of a grammar line literal, after the pattern, in this order: `[, reverse] [, withI6Synonyms]`. Neither is a pattern token (neither is matched against input); each modifies how the line lowers. `withI6Synonyms`, when present, must be the very last element. (A bare `.reverse` / `.withI6Synonyms` *dictionary* word — with the leading dot — is a normal pattern token and is unaffected.)

- **`reverse`** — emits I6's `-> Action reverse`, swapping `noun`/`second` when the action receives its parsed arguments: `{.give, creature, held, reverse}`.
- **`withI6Synonyms`** — opts out of word-precise library extension (below), spreading the line across the whole I6 verb group.

#### Word-precise library extension (and `withI6Synonyms`)

I6 verbs are often **grouped**: one `Verb` directive claims several synonym words that share a grammar table (`Verb 'enter' 'cross' * …`). Beguile never groups its own verbs (per-word emission), so when a Beguile line's trigger word belongs to a *grouped* library verb, Beguile is **word-precise by default**: it lowers the line to I6 `Extend only 'w'`, which splits `w` off into its own verb — inheriting the library grammar, adding this line, and leaving `w`'s synonyms untouched.

```bgl
verb Keypad {
    grammar = { {.enter, number, .into, noun} };   // 'enter' is grouped in the stdlib (enter/cross/…)
    void handler(){ … }
}
// emits:  extend only 'enter' * number 'into' noun -> Keypad;
//         → "enter 5 into keypad" works; "enter <room>" still navigates; "cross"/"sit" unaffected.
```

This is the object model's rule — *name a word, affect that word* — applied at the library boundary. A **solo** library word (single-trigger I6 verb, e.g. `'put'`) has no synonyms to over-reach, so it stays plain `Extend 'w'`; a **new** word is a fresh `Verb 'w'`. Word-precise lowering composes with everything else: it carries `first`/`last` priority (`extend only 'w' first`) and combines with `replace grammar =` to emit `Extend only 'w' replace` (split *and* wipe the inherited grammar).

To instead invoke I6's whole-group behavior — spreading the line across *all* the verb's synonym words — append the **`withI6Synonyms`** modifier:

```bgl
extend Take { grammar += { {.take, .all, .from, noun, withI6Synonyms} }; }
// emits:  extend 'take' * 'all' 'from' noun -> Take;   (reaches take/get/carry/… together)
```

`withI6Synonyms` is meaningful only for grouped library words; on native or solo words it is a harmless no-op (there is no synonym group). Because synonym grouping is purely an I6 concept, the modifier is named for it — it never applies to anything native.

**Parameterized grammar tokens** - `noun(Routine)` and `scope(Routine)` pass a routine to the I6 parser. The routine must be a declared global function returning `bool`:

- `noun(Routine)` - the I6 parser matches nouns normally, then calls Routine with each candidate object as a parameter. The routine returns true to accept or false to reject.
- `scope(Routine)` - the I6 parser calls Routine to determine which objects are in scope. The routine uses I6 library functions like `PlaceInScope()` or `ScopeWithin()` to add objects.

```bgl
bool isEdible(object obj) { return obj.has(edible); }

verb Taste {
    grammar = {
        {.taste, noun(isEdible)},     // only matches edible objects
    }
}
```

**Bare routine as general parsing token** - a global function name used directly in a pattern acts as a general parsing routine. The I6 parser calls it with a parsing context parameter to handle custom input matching. The routine is responsible for consuming input words and returning true if they match:

```bgl
bool parseColor(int context) {
    // context is provided by the I6 parser
    // routine examines the player's input words and returns true if matched
    return true;
}

verb Paint {
    grammar = {
        {.paint, NOUN, parseColor},    // "paint <object> <color>"
    }
}
```

**Attributes as pattern tokens** - an attribute name in a pattern matches objects that have that attribute:

```bgl
verb Replenish {
    grammar = {
        {.replenish, container},
        {.chat, animate, .about, TOPIC},
    }
}
```

### 13.4.5 Grammar on Verbs

The `grammar` member on a verb is of type `grammarRuleList`. Grammar patterns declared here use the inferred-verb form; the verb is the owning object:

```bgl
verb Examine {
    grammar = {
        {.examine, NOUN},
        {.x, NOUN},
        {.look, .at, NOUN},
    }
    void handler() {
        print("You examine it closely.");
    }
}
```

`grammarRuleList` is a real member type declared on the class; the compiler emits it as `Verb` directives rather than object properties.

### 13.4.6 Grammar Objects

`grammar` is a shorthand for declaring an object of class `grammarRuleList`. Grammar objects provide a grammar-centric, cross-cutting alternative to declaring rules on individual verbs: one grammar object can carry rules targeting many different verbs. Each rule's `grammarRule` member pairs a pattern with an explicit verb reference:

```bgl
grammar customPatterns {
    grammarRule rule1 = {PutOn, {.hang, HELD, .on, NOUN}};
    grammarRule rule2 = {Insert, {.put, HELD, .in, NOUN}};
}
```

Type can be inferred, and `array<grammarRule>` is supported for multiple rules in a single member:

```bgl
grammar customPatterns {
    rule1 = {PutOn, {.hang, HELD, .on, NOUN}};
    array<grammarRule> rules = {{PutOn, {.put, HELD, .on, NOUN}},
                                {Insert, {.put, HELD, .in, NOUN}}};
}
```

`grammarRule` and `array<grammarRule>` have strict initializer shapes: a `grammarRule` member takes a single `{verb, {pattern}}` pair (optionally followed by a priority), while `array<grammarRule>` takes a list of them. Mismatched shapes are a compile error.

### 13.4.7 Per-Rule Priority on Grammar Objects

Grammar objects bind a different verb per rule, so a block-level priority would have no consistent target. Priority is therefore expressed as the optional **third positional element** on each `grammarRule` initializer:

```bgl
grammar additions {
    grammarRule r1 = {Take, {.grab, noun}};             // default 10
    grammarRule r2 = {Drop, {.toss, held}, 5};          // priority 5
    grammarRule r3 = {Look, {.peek, noun}, 15};         // priority 15
}
```

The `array<grammarRule>` form accepts the same third element:

```bgl
grammar additions {
    array<grammarRule> rules = {
        {Take,    {.nab, noun}},
        {Drop,    {.discard, held}, 5},
        {Examine, {.study, noun}, 15},
    };
}
```

Omitted priority is `10` (the `class verb` default). Each rule is sorted against its **target verb's** anchor, not against the grammar object, and contributes to that verb's emission per the rules in §13.2.

There is no block-level default on grammar objects. Authors wanting a layer-wide priority repeat the value on each rule, or split into multiple grammar objects.

### 13.4.8 Extending Verb Grammar

Grammar is added to or removed from an existing verb (including `extern verb` declarations) inside an `extend V { … }` body, using **explicit operators** on the `grammar` member (see §5.9 for the general `extend` mechanism). The operator is required: a bare `grammar = { … }` is valid only in the original `verb` / `extern verb` **declaration**; inside an `extend` it is a compile error. This forces intent, so a stray `=` can never silently append (or be misread as a replace).

The three forms:

- **`grammar += { … }`** — append new grammar lines:

```bgl
extern verb PutOn;
extend PutOn {
    grammar += {
        {.hang, held, .on, noun},
    }
}
```

- **`grammar -= { … }`** — remove grammar: a line (`{.w, pat}`) or a whole word (`{.w}`); on an extern verb the word form *evicts* (see below).
- **`replace grammar = { … }`** — replace the verb's whole grammar (see "Replacing Verb Grammar").

A bare `priority = N;` inside an `extend` body sets a **block-local priority** applied to every line appended by that block's `grammar += { … }` (see §13.2). It is consumed at emit time, not stored as a property, so independent `extend` blocks at different priorities never collide.

#### Removing grammar (`grammar -=`)

`grammar -= { … }` removes grammar. The **grain** of the removal is set by how much of the line the spec names:

- **Line-level** — `-= { {.w, pat…} }` (a trigger word *and* a pattern) removes the one line that structurally matches exactly (same trigger word, pattern tokens, and reverse flag).
- **Word-level** — `-= { {.w} }` (a bare trigger word, no pattern) removes **all** of that word's grammar.

```bgl
verb TypeNum { grammar = { {.type|.dial, .into, noun}, {.dial, .to, noun} }; /* … */ }
extend TypeNum { grammar -= { {.dial, .into, noun} }; }   // line-level: drops that one dial line
extend TypeNum { grammar -= { {.dial} }; }                // word-level: drops EVERY dial line
```

Naming a pattern means "this exact rule"; naming just the word means "all rules for that word." A single bare word is the only many-matching form: **partial-pattern (prefix) matching is not supported.** `-= { {.give, noun} }` removes only the line whose pattern is exactly `noun`, never a more-specific `{.give, noun, .to, noun}` that was not named.

- An alternation in a `-=` spec (`.a|.b|.c`) expands to **one removal per word** — independent removals, each matched (and warned) separately. It applies to either grain.
- Removal is **source-order**: a `-=` only sees lines declared *before* it (the base plus earlier `extend` blocks), so a later `+=` of the same line is unaffected.
- A `-=` that matches nothing emits a **warning** (almost always a typo, or a `-=` written before its matching `+=`).

**Extern verbs — the two-layer model.** Beguile has only partial sight of a library verb: it knows the **words it claims** (from the binding) and the grammar **Beguile itself added** with `+=` (word *and* pattern), but the library's **original** patterns are opaque. The two grains exploit that differently:

- **Line-level `-=`** matches only Beguile's own additions. The library's original patterns are invisible, so a spec naming one cannot match.
- **Word-level `-=` evicts the word.** It needs no patterns, so it reaches the opaque layer: it lowers to I6 **`Extend only 'w' replace`**, peeling the whole word off the library verb. If a native verb also declares `w`, its lines fold into that directive (the word is *reclaimed* and rerouted); if none does, the directive is emitted empty and the word is simply **disabled**.

```bgl
extend Enter  { grammar -= { {.enter} }; }   // evict 'enter' from the library's Enter verb…
verb  Keypad  { grammar = { {.enter, number, .into, noun} }; /* … */ }   // …and reclaim it
extend Disturb { grammar -= { {.xyzzy} }; }  // evict with no reclaimer → 'xyzzy' is disabled
```

Because Beguile can distinguish a *claimed* word from an unknown one, the "nothing matched" diagnostic is precise:

| spec on an extern verb | condition | result |
| --- | --- | --- |
| `-= { {.w, pat} }` | matches a Beguile-added line | remove that line |
| `-= { {.w, pat} }` | no Beguile-added match, but `w` **is** claimed | warn: library grammar for `w` is opaque — use `-= { {.w} }` to evict the whole word |
| `-= { {.w, pat} }` | no match, `w` not claimed | warn: typo — `w` is not a word this verb has |
| `-= { {.w} }` | `w` is claimed or Beguile added it | **evict** (`Extend only 'w' replace`) |
| `-= { {.w} }` | `w` neither claimed nor added | warn: nothing to evict |

Evicting requires the word to be genuinely claimed by a real library verb (Beguile trusts the binding); `Extend only` on a word no library verb defines is an I6 error. Other compound assignments, new members, and methods remain unsupported on extern objects; `-=` on a non-`grammar` member of an extern object is a compile error.

### 13.4.9 Replacing Verb Grammar

To **replace** a verb's grammar rather than append, prefix the assignment with the `replace` qualifier. `replace grammar = { … }` wipes **all** of the verb's grammar — every trigger word, not just the ones named — and the replace lines become the verb's complete new grammar. Trigger words not named in the replace are therefore dropped:

```bgl
verb TypeNum { grammar = { {.type|.dial, .into, noun} }; /* … */ }
extend TypeNum {
    replace grammar = {
        {.dial, .to, noun},
    }
}
```

`TypeNum` is Beguile-owned, so it resolves at compile time to a pure `Verb` (no I6 `Extend`) with the whole prior grammar discarded — `type`'s `into` line is gone, and `type` is dropped because the replace names only `dial`:

```inf
Verb 'dial'
    * 'to' noun -> typenum;
```

Behavior:

- The replace lines **are** the verb's grammar after this point; any base grammar, prior `+=`/`-=`, and other lines are discarded, whatever their source order.
- It affects only **this** verb. A word that another verb also uses keeps its grammar in that other verb.
- Combining `replace grammar = { … }` with a non-default `priority = N;` in the same `extend` block is a compile error: replace semantics wipe the sort target, making priority meaningless.

For a verb Beguile **owns**, replace resolves to pure `Verb` directives (per word). For an **extern** verb Beguile does not own, replace emits I6 `Extend 'w' replace`, which is the path to override stdlib grammar for a word — e.g. taking over the library's `'take'`:

```bgl
extend Take {
    replace grammar = {
        {.take, .firmly, noun},
    }
}
// Emits (extern verb): extend 'take' replace * 'firmly' noun -> Take;
```

### 13.4.10 Verb Synonyms

To make new trigger words behave *exactly* like an existing verb, use the `synonyms` form inside an `extend` body:

```bgl
extend Take {
    synonyms = {.steal, .grab, .pilfer};
}
// Emits: Verb 'steal' 'grab' 'pilfer' = 'take';
```

This emits I6's compact synonym directive, which makes the listed words **true aliases** of the anchor verb's grammar table, not copies: a later `extend` on the anchor automatically flows to every synonym, because they share one underlying grammar identity.

```bgl
extend Take { synonyms = {.steal, .grab}; }   // steal/grab alias take
extend Take { grammar += { {.take, .quietly, noun} }; }   // also reachable as 'steal quietly', 'grab quietly'
```

Contrast with listing the words as separate grammar triggers (`extend Take { grammar += { {.steal, noun}, {.grab, noun} } }`), which **copies** the pattern set at that point in time; a subsequent `extend` on `take` would *not* reach `steal`/`grab`.

Details:

- The **anchor** is the verb being extended; its primary trigger word is resolved at emit time (a verb's first claimed dict word, or its name if it declares none).
- Works on both **extern** (stdlib) verbs, the common case, aliasing new words onto a library verb, and Beguile-native verbs. For a native verb, the synonym directive is emitted *after* the verb's own `Verb` directive, so the anchor's grammar already exists.
- Standard dict-word conventions apply: a one-letter word emits with the `//` form (`.g` → `'g//'`), and the plural flag is preserved (`.mice//p`).
- At least one word is required; an empty `synonyms = { }` is a compile error.

I6's `Extend 'w' only` directive (which is rare in practice) has no Beguile-side keyword; if needed, reach for it through `#i6 { ... }` raw I6 escape (§14.5).

---

# Chapter 14 - Interoperability with Inform 6

## 14.1 Overview

Beguile is built on top of I6: every Beguile program is transpiled to I6, then handed to the I6 compiler. Several language mechanisms exist to use I6 constructs directly, declare I6-defined entities for type-checking, and pass I6 directives through to the generated output unchanged.

### 14.1.1 Two compilation modes

Beguile operates in one of two modes, chosen automatically by the entry-file extension:

| Mode | Entry file | Host language | Guest language | Migration story |
|---|---|---|---|---|
| **Default mode** | `.bgl` | Beguile | I6 (via `#i6{}` I6 islands, see §14.5) | New project, Beguile-from-the-ground-up |
| **Precompiler mode** | `.inf` | I6 | Beguile (via `#bgl{}` Beguile islands, see §14.7) | Existing I6 project, adopt Beguile incrementally |

The two modes are structurally inverted versions of the same idea: *one language is the host, the other is reachable through islands embedded in the host stream* (see §14.1.2 for the formal definition of islands). Both modes produce the same kind of output (an `.inf` file the I6 compiler can build), and both can use every Beguile feature. They differ in **which language owns the file** and **how the user thinks about adding the other**.

#### Default mode (entered via `.bgl`)

The source file is Beguile. Classes, objects, methods, and statements are written in Beguile syntax. Raw I6 is reachable via `#i6{...}` blocks for capabilities that have no Beguile equivalent. The Beguile Language Runtime (BLR) is auto-loaded; the `bgl` namespace is implicitly imported.

```bgl
class Room : object {
    string short_name;
}

Room foyer { short_name = "Foyer"; }

#i6 {
    [ DebugDump x ;
        objectloop(x ofclass Room) print (name) x, "^";
    ];
}
```

#### Precompiler mode (entered via `.inf`)

The source file is I6. The whole file is treated as raw I6 text and passed through to the I6 compiler. Beguile features are reached through file-scope Beguile islands (`#bgl`, `#bglDecl`, `#bglStmt`) or in-routine Beguile islands (the same `#bgl{...}` form as default mode). The BLR is still auto-loaded, but the `bgl` namespace requires explicit `#using bgl;` to import. ICL header lines (`!% -G`, etc.) at the top of the file are passed through verbatim; Beguile does not synthesize its own header.

When Beguile is used as a precompiler in this mode, the author's source is recognizably the target language (I6), with the precompiler stage adding higher-level constructs that the downstream compiler doesn't natively understand.

```inf
!% -G

[ Main ;
    print "Welcome.^";
    bglInit();
    Initialise();
];

#bgl {
    class magicButton[5] : object {
        int strength = 0;
        void create(int s) { strength = s; }
    }
}

[ Initialise mb;
    mb=magicButton(10).create();
];
```

Precompiler mode exists primarily as a **migration ramp**: an I6 author can convert one type, one global, or one routine at a time without restructuring their file or learning Beguile's full project layout. If a file contains zero Beguile islands, precompiler mode is a no-op pass-through (see §14.7.6).

### 14.1.2 Islands

An **island** is a region of one language embedded in a stream of the other. The host stream is whichever language owns the file (Beguile in default mode, I6 in precompiler mode); islands let the author cross into the guest language and back without leaving the file.

There are two kinds, named by their *content*, not by their host:

- **I6 island** - a region of raw I6 embedded in a Beguile stream. Written `#i6 { ... }` or, for a single statement, `#i6 stmt;`. Detailed in §14.5.
- **Beguile island** - a region of Beguile embedded in an I6 stream. Two forms exist:
  - **In-routine Beguile islands** - `#bgl { ... }` inside an I6 routine (i.e., inside an I6 island, or inside the raw I6 of an `.inf` file). Statement-only. Detailed in §14.6.
  - **File-scope Beguile islands** - `#bgl { ... }` / `#bglDecl { ... }` / `#bglStmt { ... }` at the top level of an `.inf` file in precompiler mode. May contain declarations or statements. Detailed in §14.7.

Throughout this spec, "Beguile island" and "I6 island" are used to disambiguate when the host language is ambiguous from context. Bare "island" appears only when context makes the type unambiguous.

**Nesting.** Islands can nest arbitrarily: `#i6 { #bgl { #i6 { #bgl { ... } } } }` is valid in default mode, and the symmetric pattern works in precompiler mode. Islands may nest arbitrarily, and each inherits the loose-identifier-mode rules of its outermost containing Beguile island (see §14.6 and §14.7.2).

The remainder of this chapter covers the mechanisms that bridge the two modes: `extern` declarations (§14.2), `#includeI6` (§14.3), emitter bodies (§14.4.13), I6 islands via `#i6` (§14.5), in-routine Beguile islands via `#bgl` (§14.6), file-scope Beguile islands in precompiler mode (§14.7), and emission ordering rules (§14.8).

## 14.2 `extern` Declarations

`extern` declares that a type, variable, function, attribute, or enum member is defined in I6. Beguile registers the name and type for compile-time checking but emits no I6 definition. None of these produce I6 output; they exist solely to make I6-defined names available in Beguile source with proper typing.

| Form | Detailed in |
|------|-------------|
| `extern enum Name { ... }` | §4.5 |
| `extern class Name { ... }` | §5.2.2 |
| `extern object Name;` | §6.2 |
| `extern object Name { ... }` | §14.2.2 |
| `extern Type Name;` | §7.2 |
| `extern const Type Name;` | §7.3 |
| `extern attribute Name;` | §7.6 |
| `extern verb Name;` | §13.2 |
| `extern RetType Name(params);` | §14.2.1 |

Extern variable declarations may also carry an `as i6name` alias clause (§7.9).

### 14.2.1 Extern functions and `default` stubs

An extern function is declared **bodyless**, a signature terminated with `;`, no braces:

```bgl
extern void ClearScreen();
extern int  ChooseObjects(var obj, var code);   // non-void needs no return - the body is in I6
```

The implementation lives in I6 (a library routine); the declaration only makes the name callable and type-checked. A body is rejected; `extern void ClearScreen() {}` is an error, because empty braces misleadingly read as an (empty) *definition* rather than a declaration.

Because `extern` asserts *"this already exists in I6"*, providing a plain Beguile definition of the same name is normally an **error**; it would silently redefine an external symbol:

```bgl
extern void ClearScreen();
void ClearScreen() { … }        // ERROR: 'ClearScreen' is already declared extern
```

The exception is a **`default`** extern, the marker for an I6 library *stub* (`Stub` in I6), a weak default the library expects a game to override. Marking the declaration `extern default` permits a plain definition to supplant it, with no `replace` needed:

```bgl
extern default void Epilogue();          // library provides a weak Stub
void Epilogue() { print("The End.^"); }  // OK - overrides it; emits [Epilogue;…], no I6 `Replace`
```

This is distinct from `replace` (§5.8), which emits an I6 `Replace` directive and is for replacing strongly-defined library routines. A `default` override emits **no** `Replace`; the weak stub is supplanted at link time by the definition alone. `default` here is the same "expected to be overridden, no `replace` required" qualifier used for class members, applied to a free extern.

### 14.2.2 Extern objects with member signatures

Beyond the bare `extern object Name;` form (§6.2), an extern object may carry a **body of member signatures**. This declares a single I6-defined object *and* the type of its members, so calls and property reads against it type-check in Beguile source, without a hand-written `extern class` + instance pair:

```bgl
extern object playerCommands {
    void pushCommand(string cmd, bool isMeta = false, bool isSilent = false);   // method signature
    object interrupt;                                                            // property
}
```

Used from Beguile: `playerCommands.pushCommand("say hello");` emits the I6 message-send `playercommands.pushcommand("say hello", false, false)` (omitted trailing arguments are filled from their defaults, §8.3).

Rules for the body:
- **Methods are bodyless signatures** (`RetType name(params);`) - the implementation lives in I6. A brace body on a non-emitter method is an error (as with extern free functions, §14.2.1). `emitter` members with a `{ ... }` body are permitted (they inline).
- **Properties are typed, with no initializer** (`Type name;`) - the object is defined in I6, so a Beguile-side initial value is an error.
- Default parameter values (`= false`) are honored for arity/overload resolution at call sites, exactly as for ordinary methods.

The object becomes a referenceable file-scope name (like any global object), but (being `extern`) the compiler emits **no** `Object` directive for it. This is the recommended way to bind a singleton I6 object (e.g. an orLibrary engine object) directly; the older `extern class T : object { emitter ... } / extern T inst;` shim (§5.2.2) remains available when you need a reusable *type* rather than one named instance.

## 14.3 `#includeI6`

Emits an I6 `#include` directive into the generated output. See §3.2.4 for full syntax and path resolution details.

## 14.4 Emitters

### 14.4.1 What Is an Emitter?

An emitter is a function whose body contains raw I6 code that is *inlined* at every call site. Rather than generating an I6 function call, the compiler substitutes the emitter's body text directly into the output, replacing placeholder tokens with the actual argument expressions.

Emitters are the primary mechanism for giving library authors precise control over generated I6 while keeping the Beguile call site type-safe and readable. They may appear on any class.

Unlike regular methods, emitters support **overloading**: multiple emitters with the same name but different parameter types can coexist on the same class. The compiler selects the best match based on argument types at each call site. This is how `print()` handles `string`, `char`, `int`, and `interpolatedStringLiteral` arguments with different I6 output for each. Regular (non-emitter) methods cannot be overloaded because I6 has no function overloading; only one routine per name can exist.

```bgl
class Counter {
    int value = 0;
    emitter void increment(){ $self.value++ }
}

Counter c;
c.increment();
// emits: c.value++;   (body inlined, $self=c)
```

### 14.4.2 Emitter Syntax

An emitter declaration begins with the `emitter` keyword, followed by a return type, a name, a parameter list, and a body enclosed in braces:

```bgl
emitter returnType name(paramType paramName, ...) {
    raw i6 code here
}
```

Emitters may appear:
- As members of a class
- At global scope, as top-level declarations

The body may contain any I6 text. It is not parsed for Beguile syntax; only the placeholder tokens `$self` and `$paramName` are recognized for substitution.

Emitter bodies support conditional directives using the `##` (double-hash) prefix:

| Directive | Meaning |
|-----------|---------|
| `##if expr` | Include following body text only if the expression is true. Supports the same syntax as `#if`: symbols, comparisons, `&&`, `||`, `!`, parentheses. |
| `##else` | Alternate branch |
| `##endif` | Close a conditional block |

These `##` directives are processed at emit time and affect what raw I6 text is written to the output file. They are only meaningful inside an emitter body and have no effect in ordinary Beguile statement blocks.

**Single-hash vs. double-hash in emitter bodies:** Since emitter bodies are raw I6 text, single-hash directives like `#ifdef` are passed through verbatim to the I6 output, where they become I6 compile-time conditionals. Use `##if` for Beguile compile-time conditionals that should be resolved before the I6 is generated.

### 14.4.3 Substitution

When an emitter is called, the compiler performs textual substitution on the body before inlining it:

| Placeholder | Replaced with |
|-------------|---------------|
| `$self` | The **host**, the owning object of the receiver. For a property access (`obj.parent`), `$self` is `obj`. For a bare identifier (`localVar`, global `foo`) or a literal (`5`), there is no separate host, so `$self` equals the expression itself. Mirrors I6's `self` keyword (which refers to the routine's owning object); use this when the body needs to act on the *owner* rather than the property value. |
| `$val` | The **full receiver expression** as written by the author. For a property access (`obj.parent`), `$val` is `obj.parent`. For a bare identifier or literal, `$val` equals the identifier/literal, the same as `$self` in those non-property cases. Use this in value-type operators (`int.operator+` etc.) where the body needs to read the property *value*, not the host. |
| `$host` | The object a **storageless proxy member** is accessed on — the method receiver with its trailing `.member` stripped. For `container.children.length()`, `$self`/`$val` are `container.children` but `$host` is `container`. Use this in emitter methods on a proxy type (a member that represents a relation over its owner rather than storing a value, e.g. `childrenProp`) that need to act on the owner. For a non-proxy method call `$host` equals `$self`. |
| `$paramName` | The corresponding argument expression at the call site. Each parameter is referenced by `$` followed by its declared name. |
| `$prop` | (For array emitters) The property name when the array is an object property; `0` for global arrays. |
| `$target` | The assignment target, the **full lvalue path** (e.g. `obj.prop` for a dotted assignment, just `x` for a bare assignment). Used by primitive `operator=` emitters that perform a literal store, and by emitters that need to store a result directly (e.g. assembly opcodes). When assigned (`int r = foo();`), `$target` is the LHS variable. When called as a statement without assignment (`foo();`), `$target` is a compiler-generated temporary. When `$target` appears in the body, the normal `LHS = RHS` assignment is suppressed; the emitter body handles the store itself. |

All emitter placeholders use the `$` prefix to distinguish them from raw I6 identifiers. This prevents substitution collisions. For example, if a parameter is named `c` and the emitter body also references a variable named `c`, using `$c` for the parameter ensures only the intended token is replaced.

A few features add their own feature-local substitution tokens, documented with the feature rather than here; for example, `_bglGlobalDeclaration` bodies provide `$selfsub` (§5.10).

#### Choosing `$self` vs `$val`

The distinction matters only when the receiver is a property access. For other expressions (locals, globals, literals), the two are identical.

| Class style | Body token | Why |
|---|---|---|
| **Value type** (`int`, `bool`, `string`, …) | `$val` | The body acts on the *value* the property holds. `obj.score + 1` must emit `obj.score + 1`, not `obj + 1`. |
| **Property proxy** (`parentProp`, `attributeList`) | `$self` | The body acts on the host. `o.parent == bar` must emit `parent(o) == bar`, not `parent(o.parent) == bar`. The class has no I6 storage of its own; its operators forward to host-targeting I6 directives (`move`, `give`, `parent()`). |

```bgl
extern class int : _bglObject {
    emitter int  operator +  (int v){ $val + $v }       // value semantics
    emitter int  operator =  (int v){ $target = $v; }   // store the new value into the lvalue
}

extern class parentProp {
    emitter parentProp operator =  (object v){ move $self to $v }  // I6 wants the host
    emitter parentProp operator == (object v){ parent($self) == $v }
    emitter object     operator()              { parent($self) }
}
```

**Choosing `$self`/`$val` vs `$target` for assignment operators**: primitive `operator=` emitters that perform a literal store use `$target` (the full lvalue path), so `obj.prop = v` emits as `obj.prop = v` rather than `obj = v`. Property-proxy `operator=` emitters that redirect to a non-store I6 statement (e.g. `move`) use `$self` so the wrapper sees the owning object, not the property path.

```bgl
extern class bool {
    emitter bool operator = (bool v){ $target = $v; }
}

isBad = true;       // → "isBad = true;"
foo.flag = true;    // → "foo.flag = true;" - $target preserves the dotted lvalue
```

### 14.4.4 Global Emitters

A global emitter is declared at file scope rather than inside a class. It behaves like a regular global function from the caller's perspective, but inlines its body at every call site.

```bgl
emitter void print(stringLiteral str){ print (string)str; }
emitter void print(string str)       { print (string)str; }
emitter void print(var val)          { print val; }
```

Global emitters participate in overload resolution by the same rules as regular global functions (see §7.4). `$self` is not meaningful for global emitters.

### 14.4.5 Emitter Values

An **emitter value** is an emitter without parentheses; it declares a typed inline expansion that can be used as an expression or a standalone statement. Unlike emitter functions, emitter values require no `()` at the use site.

```bgl
emitter int wordSize { WORDSIZE }
emitter int doubleWord { WORDSIZE * 2 }
emitter void setBold { style bold }
```

**As an expression** - the body expands inline wherever the name appears in an expression:

```bgl
int ws = wordSize;             // emits: ws = WORDSIZE;
int dw = 4 + doubleWord;      // emits: dw = 4 + WORDSIZE * 2;
```

**As a statement** - the body expands as a standalone statement when followed by `;`:

```bgl
setBold;                       // emits: style bold;
```

The distinction from an emitter function is the absence of parentheses in both the declaration and at the call site:

| Declaration | Usage | Kind |
|-------------|-------|------|
| `emitter int foo() { body }` | `foo()` | Emitter function |
| `emitter int foo { body }` | `foo` | Emitter value |

Emitter values can be declared at global scope, in class bodies, and in object bodies. On class or object members, `$self` substitution works the same as for emitter functions.

```bgl
emitter class style : _bglObject {
    emitter void bold { style bold }
    emitter void roman { style roman }
}

// Usage:
style.bold;                    // emits: style bold;
```

Emitter values are typed; the declared return type is used for type checking at use sites. `emitter void` values can only be used as statements; typed values (`emitter int`, `emitter string`, etc.) can appear in expressions.

### 14.4.6 Emitter Namespaces

An **emitter namespace** groups related emitter methods under a single name without creating a class, instances, or any I6 backing. It is declared with `emitter` followed directly by the namespace name and a body block, with no `class` keyword:


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
- The namespace name cannot be used as a variable type; it exists solely to group emitter methods.
- Inheritance and `extend` are not supported.

Emitter namespaces are distinguished from `emitter class` by the absence of the `class` keyword. The key difference is usage style: an emitter namespace is intended to be called directly by name (`style.italics()`), while an `emitter class` is intended to be used as a type for variable declarations (`Temp t; t + 5;`). The namespace syntax is shorthand for the common case where no instances are needed.

`style` is a built-in emitter namespace providing I6 style directives (`style.italics()`, `style.roman()`) without requiring an instance variable.

> Emitter classes can also hold **alias members** for hierarchical namespace composition; see §5.11.2.

### 14.4.7 `print()` and `log()`

`print()` and `log()` are core language output functions. They are mentioned here because they are implemented as global emitters in `__beguileCore.bgl`; however, they are fundamental to every Beguile program. See §9.14 for usage documentation.

### 14.4.8 Operator Emitters

An operator emitter defines how a built-in operator is compiled when the left-hand operand is of the declaring class. Operator overloading in general, including the non-emitter method form, is treated authoritatively in §5.6; this section covers only the emitter-specific mechanics. The operator symbol replaces the function name. Operator emitters may be declared on any class:

```bgl
class Counter {
    int value = 0;
    emitter bool operator == (int v){ $self.value == $v }
    emitter Counter operator = (Counter v){ $self.value = $v.value }
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

#### Supported Operators

The full set of operators that may be overloaded via emitters:

| Category | Operators |
|----------|-----------|
| Assignment | `=` |
| Arithmetic | `+` `-` `*` `/` `%` |
| Comparison | `==` `!=` `=~` `<` `>` `<=` `>=` `?=` |
| Bitwise / logical | `&` `\|` `^` `<<` `>>` `&&` `\|\|` |
| Compound assignment | `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` |
| Increment / decrement | `++` `--` `prefix++` `prefix--` |
| Subscript | `[]` `[]=` |
| Query | `?` |
| Switch comparison | `switch` |

The `prefix++` and `prefix--` names distinguish prefix forms (`++n`) from postfix forms (`n++`). The two names distinguish the prefix and postfix forms, allowing a class to provide different behavior for each.

Most operators (arithmetic, comparison, assignment, etc.) follow standard conventions and require no special documentation; they take one parameter (the right-hand operand) and return the expected type. The following operators have unique semantics:

#### Query Operator `?`

The `operator ?()` emitter is a zero-parameter unary operator that defines what "present" (non-null) means for a type. It is used by three language features:

- **Optional chaining** `?.` - the query operator is evaluated at each step of the chain
- **Null coalescing** `??` - the query operator determines whether the fallback is needed
- **Postfix query** `v?` - inlines the query operator directly in a boolean context

```bgl
extern class object {
    emitter eBool operator ?() { $self ~= nothing }
}
```

Types without `operator ?()` cannot use `?.`, `??`, or postfix `?`. Attempting to do so is a compile-time error. See §10.5 for the full description of these operators.

#### Switch Comparison Operator `switch`

The `operator switch()` emitter enables custom comparison logic in `switch` statements. See §9.11 for full details on how the compiler lowers `switch` statements using this operator.

#### Compound Assignment Fallback

If no emitter is defined for a compound assignment operator (e.g., `+=`), the compiler expands it to its equivalent simple assignment:

```
n += 2    →    n = n + 2;
```

This is necessary because I6 does not natively support compound assignment operators.

#### Increment / Decrement Fallback

If no emitter is defined for `++` or `--`, the statement is emitted directly as I6:

```
n++;    →    n++;
++n;    →    ++n;
```

#### Emitter-Required Operators

Most operators may be declared as either emitters or regular functions. However, the following must always be declared as emitters (the compiler will error otherwise):

| Declaration | Reason |
|---|---|
| `operator ?()` | Inlined as the null test in `?.`, `??`, and postfix `?` expressions |
| `operator switch()` | Inlined as the comparison in `if`/`else if` chains lowered from `switch` |
| `init()` | Inlined at variable declaration site; not callable as a method |
| `deinit()` | Inlined at scope exit / `return` sites; not callable as a method |

Declaring any of these without the `emitter` keyword is a compile-time error. Inside an `emitter class`, the `emitter` keyword is implicit as usual.

### 14.4.9 Conversion Operator

Conversion operators are introduced in §5.6.4; this section covers the emitter form. A zero-parameter emitter named `operator()` declares an implicit type conversion from the declaring class to the return type. It enables the compiler to use an existing operator overload when the exact type match is absent, by first converting the value through the conversion operator.

When the body is empty, the source value passes through unchanged to the output:

```bgl
extern class int {
    emitter int operator(){}   // int passes through as-is
}
```

When a body is provided, it is substituted with `$self` replaced by the source expression:

```bgl
extern class celsius {
    emitter fahrenheit operator(){ $self * 9 / 5 + 32 }
}
```

See §11 for the full rules governing when conversion operators are applied.

### 14.4.10 `operator auto()` - Auto-Inference Type

When `auto` is used as a variable type (see §9.2), the compiler infers the type from the initializer expression. By default, `auto` uses the exact resolved type of the RHS. `operator auto()` overrides this; its return type becomes the inferred type instead.

```bgl
extern class intLiteral : _bglObject {
    emitter int operator();       // implicit conversion to int
    int operator auto();          // auto infers int, not intLiteral
}
```

With this declaration, `auto x = 5;` infers `int` (not `intLiteral`), giving `x` full `int` operator support (`+=`, `-=`, etc.) rather than the limited literal type.

Rules:
- `operator auto()` takes no parameters
- It cannot have a body; only the return type matters
- At most one per class; duplicates are a compile error
- The return type is the inferred type for `auto` declarations
- Types without `operator auto()` infer as themselves

The core library declares `operator auto()` on all literal pseudo-types:

| Literal type | `operator auto()` returns | `auto` infers |
|---|---|---|
| `intLiteral` | `int` | `int` |
| `charLiteral` | `char` | `char` |
| `dictionaryWordLiteral` | `dictionaryWord` | `dictionaryWord` |

### 14.4.11 Lifecycle Emitters: `init` and `deinit`

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

`init` and `deinit` must be declared as emitters and must declare zero parameters. Declaring a parameter on either, or omitting the `emitter` keyword, is a compile-time error.

```bgl
void doSomething() {
    string s;           // init fires: s = GetNewString();
    s = "hello";
    return;             // deinit fires first: FreeString(s);
}                       // deinit also fires here on fall-through
```

### 14.4.12 Emitters vs. Regular Functions

Emitters and regular functions serve different purposes and generate different I6 output. Understanding when to use each is key to writing effective Beguile library code.

| | Regular function | Emitter |
|---|---|---|
| I6 output | Generates an I6 routine | Inlines body text at call site |
| Body language | Beguile statements | Raw I6 code |
| Parameters | Referenced by bare name (`myParam`) | Referenced with `$` prefix (`$myParam`) |
| `$self` | Not applicable (use `self` keyword) | Replaced with receiver expression |
| Overloading | Not supported (one routine per name) | Supported (matched by parameter types) |
| Recursion | Supported | Not meaningful (no routine exists) |
| In an `extern class` body | Must have no body (stub only) | Must have a body (or `;` for pass-through) |
| Allowed at global scope | Yes | Yes |

---

### 14.4.13 Emitter Bodies as Raw I6

Emitter bodies are raw I6 text inlined at call sites, the primary path for I6 capabilities that have no Beguile syntax equivalent. See §14.4 for full emitter syntax, substitution rules, and `##if` conditional directives within emitter bodies.

## 14.5 `#i6` - I6 Islands (Inline Raw I6)

The `#i6` directive injects a block of raw Inform 6 code at the directive's source position in the generated output. Useful for I6-specific declarations or routines that have no Beguile equivalent: global variables defined entirely in I6, replacement routines, top-level Verb extensions, and so on.

Two forms:

```bgl
// Single-line: everything up to the next newline is raw I6
#i6 Constant DEBUG_FLAG = 1;

// Multi-line: brace-delimited, raw I6 inside
#i6 {
    [ MyRoutine x; print "hello "; print x; print "^"; ];
    Object foo "Foo Object" with description "An item.";
}
```

The block contents are emitted **verbatim**; Beguile does not parse, type-check, or modify them. The compiler tracks `{}`, string literals (`"..."`), and dictionary-word/character literals (`'...'`) only well enough to find the closing `}` of the multi-line form; everything else is opaque.

`#i6` blocks are emitted in their source-order position relative to other declarations (see §14.8 for ordering guarantees around classes and instances).

### 14.5.1 `#i6replace` - hoisted I6 `Replace` directive

I6's `Replace routine [savedName];` tells the compiler to discard the *first* definition of `routine` it encounters (typically the library's) and use a later one instead. It only works if it appears **before** the library that first defines the routine; placement is order-sensitive and easy to get wrong.

The `#i6replace` directive makes this safe: it emits a `Replace` directive but **hoists it to the top of the generated output** (the emit-first position, above all includes and definitions), so the replacement always takes effect no matter where you write the directive.

```bgl
#i6replace GameEpilogue;              // emits:  Replace GameEpilogue;
#i6replace DrawStatusLine _oldStatus; // emits:  Replace DrawStatusLine _oldStatus;

// ...define your own GameEpilogue later, in Beguile or in an #i6 block...
void GameEpilogue() { /* ... */ }
```

- The routine name is emitted as written.
- The optional second name is I6's rename-the-original form: the replaced library routine remains callable under `savedName`.
- Prefer `#i6replace X;` over a bare `#i6 replace X;`; however, the latter lands verbatim at its source position and only works if it happens to precede the relevant `#includeI6`.

> Note: `#i6replace` is hoisted to the top of the output like `#emitfirst` (§3.5.6), and is skipped in precompiler mode when a `.inf` file has no Beguile islands (the no-island pass-through). In that situation the author owns the I6 stream directly and can write `Replace` by hand.

## 14.6 `#bgl` - In-Routine Beguile Islands

`#bgl` is the inverse of `#i6`; it lets you switch back to Beguile from inside a raw I6 block. The body is parsed as a sequence of Beguile statements (code-block scope: no variable, function, class, or other declarations) and the resulting I6 emission is spliced into the surrounding stream at that point.

```bgl
#i6 {
    [ MyRoutine x;
        print "I6 prologue^";
        #bgl {
            x = bgl.glulx.window.getRoot();   // Beguile method call
            invoke(x);                         // Beguile global function
        }
        print "I6 epilogue^";
    ];
}
```

Two forms (matching `#i6`):

```bgl
#i6 {
    print "...";
    #bgl stmt;                  // single-line, terminated by newline
    #bgl { stmt1; stmt2; }      // multi-line, brace-delimited
}
```

**Use cases**:
- Calling Beguile emitters or methods from inline I6 code without breaking the surrounding `#i6{}` block into multiple pieces.
- Using Beguile's compile-time facilities (closures, generics, namespace resolution) within an otherwise-I6 routine.
- Using Beguile as a *precompiler* over a primarily-I6 source: most of the file is `#i6{...}`, with Beguile features sprinkled in via `#bgl{...}` Beguile islands.

**Restrictions**:
- No variable declarations (`int x = 5;` is rejected). Locals must be declared in the surrounding I6 routine's locals list.
- No function, class, enum, or other top-level declarations. `#bgl{}` is always a code-block scope.
- Allowed: assignments, expressions, method calls, control flow (`if`/`while`/`for`/etc.), and any construct that compiles to inline I6 statements.

**Identifier resolution - loose mode**:

Inside `#bgl{}` blocks, identifier resolution is *loose*. Names not declared in Beguile are passed through verbatim to the I6 stream and resolved at the I6 compile step:

```bgl
#i6 {
    [ MyRoutine local1 local2;
        local1 = 5;
        #bgl {
            local2 = local1 * 2;        // local1, local2 are I6 locals - passed through
            print(bglDeclaredFunc());   // bglDeclaredFunc IS in Beguile - resolved normally
        }
    ];
}
```

Bare identifiers, dotted-method calls, and global function calls all benefit from this.

**Argument type checking is also relaxed** for resolved Beguile calls inside `#bgl{}`. When a method or function name resolves to a Beguile declaration but argument types don't strictly match, the call binds anyway as long as exactly one overload matches by name and arity. Argument expressions emit verbatim and I6 reconciles word-sized values (int, char, bool, var, etc. are all the same word at the I6 level). This avoids needing explicit casts inside `#bgl{}` for every type-mismatched argument:

```bgl
#i6 {
    [ MyRoutine ch;
        ch = 'X';                                  // I6 char literal in I6 local
        #bgl bgl.asm.putCharStream(streamId, ch);  // ch passes through; emitter binds by name+arity
    ];
}
```

The trade-off: a real type bug (passing an `object` where the API truly expects an `int`) won't be caught. Inside `#bgl{}`, you're asserting "I know what I'm doing"; the safety net is reduced.

> ⚠️ **Caveat - typos pass through silently.** Loose mode trades early diagnostics for ergonomics. A misspelled Beguile name (e.g. `blg.asm.foo()` instead of `bgl.asm.foo()`) is treated as an opaque I6 reference, emitted literally, and the error surfaces only at I6 compile time, often in cryptic form (`No such constant as "foo"`). When debugging, double-check that any "I6 didn't recognize this" error inside a `#bgl{}` block isn't actually a typo'd Beguile reference.

**Nesting**: `#bgl` and `#i6` can nest arbitrarily: `#i6{ #bgl{ #i6{ #bgl{ ... } } } }` is valid.

> See §14.7 for the parallel **precompiler-mode** model: Beguile islands at file scope inside an I6 source file, rather than this section's in-routine form.

## 14.7 Precompiler Mode - File-Scope Beguile Islands

When the compiler is invoked on a file with the `.inf` extension, it enters **precompiler mode** (introduced in §14.1.1). The entire file is treated as raw I6, and Beguile features are accessed through Beguile islands embedded in the I6 stream (see §14.1.2). This is the inverse of default mode's §14.6 setup: instead of Beguile source with `#i6{}` I6 islands, precompiler mode is I6 source with `#bgl{}` Beguile islands.

Precompiler mode's primary purpose is the **migration ramp**: an I6 author can convert one piece of their codebase at a time without leaving their `.inf` file. Beguile-declared types, globals, and methods are added incrementally, with the surrounding I6 unchanged.

### 14.7.1 The three Beguile island directives

Precompiler mode recognizes three Beguile-island directive forms at file scope:

| Directive | Content |
|---|---|
| `#bgl { ... }` | Auto-detect (declarations or statements based on first 1-3 tokens) |
| `#bglDecl { ... }` | Declarations only (classes, enums, globals, functions). Statement → compile error. |
| `#bglStmt { ... }` | Statements only (assignments, calls, control flow). Declaration → compile error. |

`#bgl` is the predominant form. The explicit variants exist as escape hatches when the author wants the early-error or when content is genuinely ambiguous.

```inf
!% -G

[ Main ;
    print "Welcome.^";
    bglInit();
    Initialise();
];

#bgl {
    class magicButton[5] : object {
        int strength = 0;

        void create(int s) { strength = s; }

        bool before() {
            switch(action) {
                case push: print("Click. The button does nothing."); rtrue;
            }
            rfalse;
        }
    }
}

[ Initialise ;
    new magicButton(10);
    new magicButton(15);
];
```

### 14.7.2 Loose identifier mode

**All precompiler-mode Beguile islands run in loose mode**, and this looseness propagates through any sub-parses, including class method bodies declared inside a `#bgl` or `#bglDecl` island. References to identifiers Beguile doesn't know about (I6-declared globals, objects, routines, constants) pass through verbatim to the I6 stream:

```inf
Object  RedSpell "redspell";
Constant redtangent = 7;

#bgl {
    class wand : object {
        void cast() {
            switch(action) {
                case redtangent:                              // I6 constant - passes through
                    RedSpell.cast(player, 5);                 // I6 object + method - passes through
            }
        }
    }
}
```

This matches the loose-mode story for §14.6 in-routine Beguile islands, extended uniformly through the entire precompiler-mode Beguile-island sub-parse. The decl-vs-stmt distinction governs *what kind of content* the island accepts at the outer level; once we're inside a method body, references to I6 are always allowed.

> The same trade-off applies as in §14.6: typos pass through silently. A misspelled I6 name surfaces only at I6 compile time.

### 14.7.3 Auto-loaded ICL header

Beguile does not synthesize an ICL header (`!% -G`, etc.) in precompiler mode. The user's own `!%` directives at the top of the `.inf` file are the only authority. They must be on the first lines of the file with no blank lines between them, per I6's stricter ICL parsing.

### 14.7.4 Runtime initialization

In precompiler mode, `#startup` content lives inside `bglInit()`, which is declared but does not run until something calls it. The `.inf` author is responsible for ensuring that call happens, either via a library binding or by calling `bglInit()` explicitly from `Initialise` or `Main`.

### 14.7.5 Required explicit `#using bgl`

The `bgl` namespace is **not** auto-imported in precompiler mode. To use `bgl.glulx.window`, `bgl.asm.*`, etc., declare `#using bgl;` inside a `#bgl{}` Beguile island. Once declared, it applies to all subsequent Beguile islands in the file.

This is opposite to default mode where `#using bgl` is implicit. In precompiler mode, `#using bgl;` is required to enable Beguile islands' use of the `bgl` namespace.

### 14.7.6 No-island fast path

If an `.inf` file in precompiler mode contains zero `#bgl` / `#bglDecl` / `#bglStmt` Beguile islands, the compiler emits an unchanged pass-through to the output `.transpiled.inf`. This makes Beguile a no-op for `.inf` files that don't use any Beguile features, useful for incremental migration: drop the file in unchanged, then start adding Beguile islands.

## 14.8 I6 Emission Ordering

Inform 6 is sensitive to declaration order in several ways: a class must be defined before any instance or derived class that references it, an attribute must be declared before any `has` clause that uses it, and `class Derived class Base` requires `Base` to already exist. Beguile's emitter preserves **source order** almost everywhere, with three rules that guarantee valid I6 ordering:

1. **Lazy class emission** - a `class` declaration is normally emitted at its source-order position. If an earlier declaration (in source order) references the class as an instance type (`ClassName varName;`, `object` instance of that class, etc.), the class is hoisted to emit immediately before its first such instance. This lets you write `_glulxWindow tmpWin;` followed later by `class _glulxWindow { ... }`; the class is pulled up to satisfy I6 without requiring the programmer to reorder the source.

2. **Base classes before derived classes** - before a class is emitted (at its own position or lazily), its base classes (transitive) are emitted first. Because this is driven by the actual inheritance graph, it composes automatically with lazy hoisting.

3. **Extern attributes as source-order gates** - when a class uses `has X`, and `X` is an `extern attribute` declared in a bindings file, the class cannot be emitted until the `extern attribute X;` declaration has been walked past in source order. The extern declaration acts as a **compile-time proxy** for the `#includeI6` that will actually define the attribute in I6; its position marks where the attribute becomes available.

   If a class with `has X` would emit at a position before `X`'s extern declaration, the compiler errors with:
   ```
   error: class 'Name' uses `has X` but its bindings-file declaration `extern attribute X;`
   comes later in source. Move the bindings file before the class or its first instance.
   ```

   **Programmer convention**: include the bindings file before the corresponding `#includeI6`:
   ```bgl
   #include <bindings/i6StandardLibrary>   // activates extern attribute light, etc.
   #includeI6 "parser"                     // actually defines them in I6
   class room : object { attributes = {light}; }
   ```
   Pairing is not mechanically enforced (the compiler doesn't require a 1:1 relationship between bindings files and `#includeI6` directives); however, following the convention keeps the source-order check in sync with the I6 reality.

### Circular inheritance

`class A : B` where `B` transitively inherits from `A` is a compile error, detected when the inheritance clause is parsed:

```
error: class 'A': circular inheritance - 'B' transitively inherits from 'A'
```

This covers direct self-inheritance (`class A : A`) and longer cycles (A → B → C → A). It is independent of emission ordering; a cycle can never produce valid I6.

### Not restricted by ordering

Member types (`class Foo { Bar bar; }`), method-body references, and value references to other classes do **not** impose emission ordering constraints. I6 is word-typed, so a member declaration is just a property name with no type cross-reference at the I6 level; method bodies resolve identifiers at link time. Only the three cases above (instances, base classes, extern attributes) are constrained.

## 14.9 Debug Bundle

When compiled with `--debug`, beguiler produces two debug files alongside the story file:

**`<filename>.bgl.transpiled.inf.bgldbg`** - a plain-text bundle containing three sections:

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

- **`[map]`** - correlates each I6 source line number back to its originating Beguile source file and line. Used by the debugger to navigate between Beguile source and generated I6.
- **`[sym]`** - symbol table mapping Beguile names to their I6 identifiers and declaration kind (`global`, `object`, `function`, `property`).
- **`[types]`** - Beguile type information for all declared variables, enabling display of typed values (including object properties) in the debugger's Variables pane.

**`<filename>.bgl.transpiled.inf.dbg`** - the Inform 6 XML debug database, produced by `inform6 -k`. Contains VM bytecode addresses mapped to I6 source line numbers, routine definitions with local variable frame offsets, global variable addresses, and property numbers. Its format is fixed by the Inform 6 compiler.

Together these two files give the VS Code extension everything it needs for source-level debugging: the chain `VM address → I6 line → .bgl file + line`, full call stack reconstruction, and typed variable display.

## 14.10 `superposed` Routines, Globals, Objects, and Classes

A declaration marked `superposed` is emitted into the story file **only if something references it**; if no use ever names it, it evaporates from the output. It sits in superposition until a use "observes" it.

```bgl
superposed void debugDump() { ... }   // emitted only if debugDump() is called somewhere
```

This is useful for library helpers and data that should cost nothing when unused; a game that never touches them pays no code-size or link cost. It pairs with the `omitUnusedRoutines` compilation setting (§3.4): `superposed` is not emitted unless observed, while `omitUnusedRoutines` asks the I6 compiler to strip routines that were emitted but end up unreferenced.

### File-scope globals and data

`superposed` applies to **file-scope global variables and arrays**, not just routines. This lets an always-loaded library carry lookup tables and constants that cost nothing unless a routine actually reads them:

```bgl
// Always parsed (it lives in an auto-loaded library), but the table is written to
// the story file only if _bglCharIsVowel is itself reached.
superposed array<char> _bglVowels = "aeiou…";

superposed bool _bglCharIsVowel(char c){
    for(char v in _bglVowels) if(v == c) rtrue;   // observing _bglVowels
    rfalse;
}
```

Materialization is **transitive**: observing `_bglCharIsVowel` pulls its body into the output, whose reference to `_bglVowels` then pulls the table in as well. A superposed declaration may freely reference other superposed declarations; each is materialized the moment it is first named in the assembled output.

### Whole objects

`superposed` also applies to a whole **object declaration**: its entire `object X with …;` block is withheld and emitted only if the object's name is referenced. This is how an always-declared namespace/helper object costs nothing until it is used. Combined with a compile-time value alias (§5.11.1), a namespace can gate on use entirely:

```bgl
superposed object _bglWorld {
    array<object> getAll() { ... }        // reference these -> they materialize too
}
extend bgl { alias world = _bglWorld; }   // compile-time alias: NO runtime property

// Calling `bgl.world.getAll()` references the object, which materializes it, which
// references its routines, which reference their buffers - the whole cascade appears
// only in games that actually call bgl.world.
```

The two pieces do distinct jobs: the **alias** removes the runtime property that would otherwise force the object to emit (§5.11.1), and **`superposed`** withholds the object's definition until its name is observed. An I6 object may be declared after its uses (forward references resolve), so append-at-end works for objects just as it does for routines.

### Classes

`superposed` also applies to a **class declaration**: its `Class X …;` directive is withheld and emitted only if the class name is referenced — as a **static instance** (`X inst;`), a **subclass** (`class Y : X`), a **pooled allocation** (`new X()` on `class X[N]`), an **`ofclass` test** (`obj.is(X)`), or any typed use. A superposed class nothing references costs zero bytes.

```bgl
superposed class Weapon { int damage = 0; }   // withheld until referenced

Weapon sword;      // <- references Weapon: the Class directive materializes, ahead of this instance
```

This makes it safe for a library — or an always-included core file — to declare a class that most games never touch: including it is free unless used. (It's also how an inline `auto {}` accessor object stays free: the compiler auto-marks the synthesized accessor class `superposed`, so an accessor-bearing core file like `bgl.ui.statusBar` costs nothing until the accessor is read or written.)

Unlike objects and routines, an I6 `Class` directive must physically **precede** any class that derives from it and any static instance of it. The compiler honors this automatically: a class referenced in a directive position (a static instance, a subclass) is emitted **in place, ahead of** the referrer; a class referenced only from routine bodies (`new`, `ofclass`) is appended at the end, where I6's forward references resolve. When a revived class has a `superposed` **base**, the base is revived **first**, so `Class Base` always appears before `Class Derived`.

`superposed` is rejected on `extern`, `emitter`, and `alias` classes (they emit no directive to withhold) and on `extend class` (it belongs on the original declaration).

### Scope and caveats

- **File scope, whole declarations only.** `superposed` gates free routines, file-scope globals/arrays, whole objects, and whole classes; each is a self-contained block that can be withheld and revived. It does **not** gate an individual **member** of a class or object (a single property or method): members share I6's flat property table and can't be withheld one at a time.
- **Reference matching is case-insensitive**, in keeping with Beguile's case-insensitive identity: a `superposed` `_bglPow` is still observed when it is called as `_bglpow`.
- **Over-inclusion is the safe direction.** A name that also appears inside a string literal or comment falsely materializes its declaration (a harmless dead entry); however, it never drops a genuinely-referenced one. Give superposed helpers distinctive names (a `_bgl`-style prefix) to avoid accidental matches.
- `superposed` may appear in any qualifier order.

---

# Chapter 15 - Runtime Library

## 15.1 Overview

The Beguile runtime library is the set of types, namespaces, and built-in objects that are auto-loaded into every Beguile program, both default-mode (`.bgl` files) and precompiler-mode (`.inf` files with file-scope Beguile islands, see §14.7). No `#include` is needed; these constructs are available the moment the compiler starts processing a file.

Where in this chapter we say a type or value is "auto-loaded", "built-in", or "always available", these all mean the same thing: it's part of every program by default, regardless of where its definition lives in the runtime library files.

This chapter documents:
- The `bgl` namespace and its target-specific sub-namespaces (§15.2)
- IF-domain built-in types: attributes, dictionary words, verbs, grammar (§15.3)
- Built-in objects providing world-tree iteration (§15.4)

The basic primitive types (`int`, `bool`, `char`, `string`, `object`, `var`) and their literal forms are documented in Chapter 4. They are all auto-loaded.

For *opt-in* language extensions that require explicit `#include`, see Chapter 16.

### 15.1.1 `bglInit()` - Runtime Initialization

Some language extensions allocate runtime resources (memory pools, buffers, etc.) that must be set up before use. These extensions register an initialization hook via `#startup` (see §3.5.5); all registered hooks are called by `bglInit()`.

The standard IF library bindings (`i6StandardLibrary` and `punyInform`) call `bglInit()` automatically during game startup. **Most users do not need to call `bglInit()` themselves.** It is only necessary when building a game without a standard binding:

```bgl
void Initialise() {
    bglInit();
    // ... rest of setup
}
```

`bglInit()` is always available; it is a no-op if no extensions that need it have been included. The call is harmless to include unconditionally.

**Which extensions require it:** `string.bgl` requires `bglInit()` to initialize the string pool. Extensions that do not allocate runtime resources (such as `char.bgl`) do not require it but are unaffected by the call.

## 15.2 The `bgl` Namespace

`bgl` is a built-in namespace object that organizes target-specific operations into sub-namespaces. It is reachable from any default-mode source file without any `#include` or `#using`. (In precompiler-mode, the `bgl` namespace is auto-loaded but requires an explicit `#using bgl;` to pull names into local scope; see §14.7.5.)

| Path | Contents | Target |
|---|---|---|
| `bgl.glulx.*` | Glulx-specific operations: window management, file I/O, asset access, ... | Glulx |
| `bgl.zcode.*` | Z-machine-specific operations | Z5/Z8 |
| `bgl.asm.*` | Direct opcode emitters for the active VM (target-routed) | Both |

Each branch is itself a namespace object exposing typed methods (mostly emitters) for capabilities specific to that VM. The compiler does not enforce target-routing: referencing `bgl.glulx.window` from a Z-machine target build compiles through Beguile; however, the resulting I6 fails at the I6 stage because the underlying Glulx routines aren't available.

A typical use of `bgl.glulx.window`:

```bgl
glulx.window mainWin = glulx.createMainWindow();
mainWin.print("Hello, Glulx world.\n");
```

The `#using bgl.glulx;` directive imports the inner namespace so members become reachable bare (e.g. `window` instead of `bgl.glulx.window`). See §12 for namespace import details.

## 15.3 IF-Domain Built-in Types

Auto-loaded class types modeling IF-domain concepts: attributes, dictionary words, verbs, and grammar. Each has its own typed declaration site and operator/method behavior elsewhere in the spec; this section catalogs them as a group so users know what's available.

### 15.3.1 `attribute` and `attributeList`

`attribute` is the type of a single I6 attribute (`light`, `container`, `static`, etc.). `attributeList` is the type of a class or object's `has` line, a comma-separated list of attributes.

```bgl
extern attribute light;
extern attribute static;
extern attribute scenery;

class magicButton[5] : object {
    attributeList attributes = {static};
}

class lampPost : object {
    attributeList attributes = {light, !scenery};   // explicit light, explicit NOT scenery
}

extend myRoom {
    attributes = {light};            // replace any inherited list with just {light}
}
```

Attribute declarations are typically `extern` (declared in I6 libraries), but `attribute` declarations may also appear in user code via `attribute attrName;` (see §7.6).

`attributeList` accepts initializer lists `{a, b, c}` with prefix `!` on any entry to negate an inherited attribute (`!attr` emits as `has ~attr`). Only `=` is supported; `+=` / `-=` are not. To change attributes at runtime use the list's `give(attr)` / `ungive(attr)` methods. The list is emitted as the I6 `has` line on the containing class or object.

### 15.3.2 `property`

`property` is the type of a free-standing I6 property name, an entry in I6's global property table that is not a member of any Beguile class. Member names are already properties, so most code does not need this type; a free-standing `property` decl is used when a property name has no Beguile class to live on (typical for I6 interop) but still needs to participate in `obj.provides(name)` checks. See §7.7 for declaration syntax and `obj.provides()` semantics.

### 15.3.3 `dictionaryWord`

`dictionaryWord` is the type of an I6 dictionary word. Literals are written with `.word` (singular) or `..word` (plural prefix indicating "noun is plural"). See §2.5.6 for the literal syntax.

```bgl
dictionaryWord w = .cloak;
dictionaryWord pl = ..marbles;
```

Dictionary words participate in grammar patterns (Chapter 13) and can be compared with `==`.

### 15.3.4 `verb`

`verb` is an alias class (for `object`) used to declare verbs. Verbs are objects with grammar rules attached. See Chapter 13 for the full verb/grammar story.

```bgl
verb examine {
    grammar = { {.x, NOUN} };
}
```

### 15.3.5 Grammar types

| Type | Purpose |
|---|---|
| `grammarToken` | An extern enum whose values are the I6 grammar-token names (`noun`, `held`, `creature`, …). Library bindings (i6StandardLibrary, punyInform) declare it. Both bare values (`held`) and qualified access (`grammarToken.held`) are valid in pattern position. |
| `patternElement` | Base type for one element of a grammar pattern (dictionary words and grammar-token values); a pattern is an `array<patternElement>`. |
| `grammarRule` | One verb-targeted pattern: `{verb, {pattern}}` |
| `grammarRuleList` | A list of grammar rules, the `grammar` member of a verb or grammar object |

Most of these are handled implicitly through the grammar declaration syntax (§13.4); users rarely manipulate them as named types. They are cataloged here because they're the receiver types for built-in operators on grammar declarations.

### 15.3.6 `bglClass`

The operand type of `object.is(cls)` (see §7.8). Any registered class (declared with `class Foo {…}` or `extern class Foo : object {…}`) is type-compatible with `bglClass`, so all class names are accepted. A `bglClass` value accepts a class name and carries the type identity used to drive the class-test emitter.

### 15.3.7 `parentProp`

`parentProp` is the type of an object's `parent` property. Assigning to `obj.parent` is sugar for the I6 statement `move obj to <newParent>`:

```bgl
extend bag {
    parent = inventory;     // emits: move bag to inventory;
}
```

`parentProp` is auto-declared on every class via the built-in `extern class object` definition. Users do not normally instantiate it.

## 15.4 `bgl.world` - Object Tree Iteration

`bgl.world` replaces I6's `objectloop` with array-returning queries over the Inform 6 object tree. It is an **opt-in extension**; enable it with `#include <bglWorld>`.

Each method walks the object tree, fills a scratch buffer, and returns it as a tracked `array<object>`, so every `<array>` operation works on the result. LINQ chain ops (`filter`, `map`, `take`, `distinct`, …) also work; however, chaining additionally requires `#include <linq>` (`<bglWorld>` itself only pulls in `<array>`).

### Methods

| Method | Description |
|--------|-------------|
| `bgl.world.getAll()` | Returns all objects in the game world |
| `bgl.world.getAll(pred)` | Returns all objects for which `pred(o)` returns true |
| `bgl.world.inParent(parent)` | Returns all direct children of `parent` |
| `bgl.world.inParent(parent, pred)` | Returns direct children of `parent` for which `pred(o)` returns true |
| `bgl.world.instances(cls)` | Returns all objects of class `cls` (replaces the `objectloop(o ofclass C)` idiom) |
| `bgl.world.instances(cls, pred)` | Returns objects of class `cls` for which `pred(o)` returns true |

The optional predicate is a `func<bool, object>`, a function taking one `object` and returning `bool`. It runs **inside** the object-tree walk, so non-matching objects never reach the buffer. Prefer `instances(Animal, p)` over `instances(Animal).filter(p)` when the filter is selective: the unfiltered intermediate is capacity-bound and could overflow before `.filter()` runs.

### Usage

```bgl
#include <bglWorld>

// Iterate all objects            (objectloop(o))
for(object o in bgl.world.getAll()) {
    print(o);
}

// Filter by predicate - optional 2nd argument (named or anonymous function)
bool isLit(object v) { return v.has(light); }
for(object o in bgl.world.getAll(isLit))                 { print(o); }
for(object o in bgl.world.getAll((object v) => v.has(light))) { print(o); }

// Children of a room             (objectloop(o in location))
for(object o in bgl.world.inParent(location)) {
    print(o);
}

// All objects of a class         (objectloop(o ofclass Treasure))
for(object o in bgl.world.instances(Treasure)) {
    print(o);
}
```

### Scratch-buffer semantics

Results live in shared scratch buffers; do **not** store a result across turns. For deep nesting, snapshot into your own `array<object>` first (an assignment copies; see §16.2.7).

### Buffer capacity

Each buffer holds up to `worldBufSize` objects (default **128**). Configure at the top of your `.bgl`:

```bgl
#beguilerSettings { worldBufSize = 256; }
```

Walks that exceed the configured size silently stop at the cap. For typical IF games this is not a concern.

---

# Chapter 16 - Opt-In Language Extensions

## 16.1 Overview

The Beguile language extensions (`beguiLib/`) provide opt-in language features that build on the runtime library (Chapter 15) but are not auto-loaded. Authors include each one explicitly with `#include <name>` when needed.

A separate category of files, the **IF Library Bindings** (`beguiLib/bindings/`), exposes specific external IF library symbols (attributes, globals, actions) to Beguile's type system using `extern` declarations. Bindings are inherently target-dependent and entirely optional; projects that manage their own I6 bindings or use a different library do not need them. See §16.3.

## 16.2 Core Language Extension Files

Core extension files in `beguiLib/`. Each is library-agnostic and works with any IF target. Each is included with `#include <name>`.

### 16.2.1 `<bglAllocated>` - Allocator-Managed Object Mixin

```bgl
#include <bglAllocated>
```

Adds `copy()` and `remaining()` operations to a pooled class (one declared with `[N]`, see §5.2.5). The class opts in by inheriting from `bglAllocated`:

```bgl
class marbleClass[10] : object, bglAllocated {
    int weight = 0;
}

marbleClass a = new marbleClass();
marbleClass b = new marbleClass();
b.copy(a);                      // bulk-copy a's properties into b
int free = a.remaining();       // free slots in marbleClass's pool
```

| Method | Returns | Description |
|---|---|---|
| `instance.copy(other)` | `void` | Bulk-copies properties from `other` into `instance`. Both must be active pool instances of the same (or compatible) pooled class. Wraps I6's class-message `copy`. |
| `instance.remaining()` | `int` | Number of free slots remaining in this class's pool. Wraps I6's built-in `remaining`. |

Copying is performed by an explicit `copy()` method.

Does not require `bglInit()`.

### 16.2.2 `<char>` - Character Utilities

```bgl
#include <char>
```

Extends the built-in `char` type with character inspection, case conversion, and case-insensitive comparison. Includes full support for ZSCII extended characters (diacritics, ligatures, etc.).

**Case inspection:**

| Method | Returns | Description |
|--------|---------|-------------|
| `c.isLower()` | `bool` | True if `c` is a lowercase letter (including diacritics) |
| `c.isUpper()` | `bool` | True if `c` is an uppercase letter (including diacritics) |
| `c.isAlpha()` | `bool` | True if `c` is any letter |
| `c.isNumeric()` | `bool` | True if `c` is a digit (`0` to `9`) |
| `c.isAlphaNumeric()` | `bool` | True if `c` is a letter or digit |
| `c.isVowel()` | `bool` | True if `c` is a vowel (including accented vowels) |
| `c.isConsonant()` | `bool` | True if `c` is a consonant |

**Case conversion:**

| Method | Returns | Description |
|--------|---------|-------------|
| `c.toUpper()` | `char` | Returns uppercase version of `c` |
| `c.toLower()` | `char` | Returns lowercase version of `c` |

**Operators:**

| Operator | Description |
|----------|-------------|
| `c =~ d` | Case-insensitive equality. `'A' =~ 'a'` is true. |

Does not require `bglInit()`.

### 16.2.3 `<buf>` - Tracked Character Buffers

```bgl
#include <buf>
```

A `<buf>` is a length-tracked character buffer: `array<char>` declarations become buffers that track their capacity and current length. The user-visible pointer behaves as a standard I6 hybrid buffer, so tracked bufs can be passed directly into I6 stdlib routines like `print_to_array` and `glk_put_buffer`.

```bgl
#include <buf>
array<char> myBuf[64];     // 64 chars of data + sized-buffer prefix
myBuf.setLength(0);
_bglBuf.set(myBuf, "hello");
print(_bglBuf.length(myBuf));   // 5
print(_bglBuf.size(myBuf));     // 64
```

When `<buf>` is included, `byteArray`'s `size()` and `length()` methods are replaced with tracked-buf-aware versions, and `setLength` / `isTracked` are added. The same operations are available as `_bglBuf.X(buf, ...)` for callers that prefer the namespaced form.

| Method | Returns | Description |
|---|---|---|
| `buf.size()` | `int` | Data capacity (chars) |
| `buf.length()` | `int` | Current used chars |
| `buf.setLength(n)` | `void` | Set the used-chars count. Range-checked against `size()` |
| `buf.isTracked()` | `bool` | True if the receiver is a tracked buf; distinguishes tracked bufs from raw I6 arrays |
| `_bglBuf.set(buf, value)` | `array<char>` | Replace buf contents with a string literal, char, or another buf. Updates length |
| `_bglBuf.append(toBuf, fromBuf)` / `prepend` / `insert(toBuf, fromBuf, pos)` | `array<char>` | Buffer-to-buffer composition with bounds checks |
| `_bglBuf.copy(toBuf, fromBuf, n, toPos, fromPos)` | `array<char>` | Byte-range copy |
| `_bglBuf.delete(buf, pos, count)` | `array<char>` | Remove a range, sliding tail left |
| `_bglBuf.left(toBuf, fromBuf, n)` / `right` / `mid(toBuf, fromBuf, fromPos, n)` | `array<char>` | Substring extraction |
| `_bglBuf.indexOf(buf, search, start)` | `int` | First match index, or `-1` |
| `_bglBuf.replace(buf, search, repl)` / `replaceAll` | `array<char>` | In-place substring replacement |
| `_bglBuf.equals(buf1, buf2, caseInsensitive = false)` | `bool` | Content equality |
| `_bglBuf.startsWith(buf, prefix, caseInsensitive = false)` / `endsWith` | `bool` | Prefix / suffix test |
| `_bglBuf.toUpper(buf)` / `toLower(buf)` | `array<char>` | In-place case conversion |
| `_bglBuf.reverse(buf)` / `trim(buf)` / `trimLeft(buf)` / `trimRight(buf)` | `array<char>` | In-place transforms |
| `_bglBuf.print(buf, len = -1)` | `array<char>` | Print `len` chars (or up to `length()` if `len < 0`) |
| `_bglBuf.capture(buf, maxBytes = 0)` / `release()` | none | Redirect print output into the buffer, then restore the prior stream |

Untracked I6-native `array<char>` declarations are not length-tracked; `isTracked()` distinguishes the two at runtime.

Does not require `bglInit()`. With `<buf>` included, sized `array<char>` buffers (e.g. `array<char> b[N];`) are length-tracked.

### 16.2.4 `<string>` - Mutable String Runtime

```bgl
#include <string>
```

Provides a pool-based mutable string type. Requires `bglInit()` to initialize the string pool.

**Lifecycle:**

Strings are automatically allocated on declaration (`init`) and freed on function exit (`deinit`). The pool size is configurable via the `framePoolSize` setting in `#beguilerSettings`.

**Assignment and concatenation:**

| Method / Operator | Description |
|-------------------|-------------|
| `s = "text"` | Assign from a string literal |
| `s = other` | Copy from another string |
| `s + "text"` | Concatenate (returns new string) |

**Comparison:**

| Operator | Description |
|----------|-------------|
| `s == "text"` | Content equality |
| `switch(s) { case "a": ... }` | Switch on string value (via `operator switch`) |

**Subscript access:**

| Operator | Description |
|----------|-------------|
| `s[i]` | Read character at position `i` |
| `s[i] = c` | Write character at position `i` |

**Methods:**

| Method | Returns | Description |
|--------|---------|-------------|
| `s.print()` | `string` | Print the string |
| `s.append(v)` | `string` | Append a string or character |
| `s.prepend(v)` | `string` | Prepend a value |
| `s.toUpper()` | `string` | Convert to uppercase |
| `s.toLower()` | `string` | Convert to lowercase |
| `s.trim()` | `string` | Trim whitespace from both ends |
| `s.trimLeft()` | `string` | Trim leading whitespace |
| `s.trimRight()` | `string` | Trim trailing whitespace |
| `s.reverse()` | `string` | Reverse the string |
| `s.mid(start, count)` | `string` | Extract substring |
| `s.left(count)` | `string` | Extract left substring |
| `s.right(count)` | `string` | Extract right substring |
| `s.insert(pos, src)` | `string` | Insert at position |
| `s.delete(pos, count)` | `string` | Delete characters |
| `s.replace(search, repl)` | `string` | Replace first occurrence |
| `s.replaceAll(search, repl)` | `string` | Replace all occurrences |
| `s.format(pattern, ...)` | `string` | Formatted output (up to 2 additional args) |
| `s.getLength()` | `int` | String length |
| `s.indexOf(search)` | `int` | Find first occurrence |
| `s.startsWith(prefix)` | `eBool` | Prefix test |
| `s.endsWith(suffix)` | `eBool` | Suffix test |
| `s.contains(search)` | `eBool` | Substring test |
| `s.isEmpty()` | `eBool` | True if empty |
| `s.capture()` | `void` | Capture output to this string |
| `s.release()` | `void` | Release captured output |

### 16.2.5 `<uint>` - Unsigned Integer Type

`#include <uint>` enables the `uint` type documented in §4.3.

### 16.2.6 `<math>` - Mathematical Functions

```bgl
#include <math>
```

Numeric utility emitters not part of the auto-loaded core. The extension provides bit-shift, absolute value, power, and target-correct unsigned arithmetic helpers.

| Function | Returns | Description |
|---|---|---|
| `abs(x)` | `int` | Absolute value of signed `int` |
| `pow(base, exp)` | `int` | Integer exponentiation |
| `shiftLeft(x, n)` | `int` | Logical left shift by `n` bits |
| `shiftRight(x, n)` | `int` | Logical right shift by `n` bits |
| `unsignedCompare(a, b)` | `int` | Returns -1/0/+1 from unsigned comparison; underpins `uint` relational operators |
| `unsignedDiv(a, b)` | `int` | Unsigned integer division; underpins `uint /` |
| `unsignedMod(a, b)` | `int` | Unsigned integer modulo; underpins `uint %` |

The unsigned helpers exist primarily to back the `uint` type's operators (§4.3); including `<math>` directly is rarely necessary unless writing low-level bit manipulation.

### 16.2.7 `<array>` - Extended Array Utilities

```bgl
#include <array>
```

Adds higher-level methods to `array<T>` for searching, mutation, and counting beyond the built-in `[]` subscripts and `size()` capacity getter from the core (§4.9), plus **value-semantic copy-on-assign** (§4.9.2: `dst = src` copies elements). LINQ-style fluent chains (`filter`/`map`/…) are a *separate* opt-in extension, `<linq>` (§16.2.8), so array-only programs don't carry the chain scratch.

| Method | Returns | Description |
|---|---|---|
| `length()` | `int` | Current count of "in use" entries. Set at allocation (= N for list-init, 0 for sized uninit), changed only by explicit operations. Falls back to `size()` for untracked extern arrays |
| `setLength(n)` | `void` | Write the explicit length. Range-checked: must fit in signed range (0..32767 on Z, 0..2^31-1 on Glulx). No-op on untracked extern arrays |
| `isTracked()` | `bool` | True if the receiver is a Beguile-declared array with length tracking; false for I6-native extern arrays. Useful for defensive code |
| `indexOf(item)` | `int` | First index where `item` appears in the array, or `-1` if not found. Scans the full `size()` (finds values past current length too) |
| `find(item)` | `int` | Alias for `indexOf` (matches orLibrary terminology) |
| `contains(item)` | `bool` | True if `item` appears anywhere in the array |
| `clear()` | `void` | Zero every slot up to `size()` and reset `length()` to 0 |
| `swap(pos1, pos2)` | `void` | Exchange the values at the two given indices. Size-bounded; caller is responsible for in-range indices |
| `reverse()` | `void` | Reverse the used range (positions 0..`length()`-1) in place. Slots past length are untouched |
| `append(item)` | `bool` | Add `item` at position `length()`, bumping length by 1. Returns false if the array is full (length == size), true on success |
| `prepend(item)` | `bool` | Insert `item` at position 0, shifting existing items right. Returns false if full |
| `insert(pos, item)` | `bool` | Insert `item` at position `pos`, shifting positions `pos`..`length-1` right. `pos` may equal `length()` (equivalent to append). Returns false if the array is full or `pos` is out of range |
| `remove(pos)` | `void` | Remove the element at `pos`, shifting later elements left. Length drops by 1. Out-of-range `pos` is a silent no-op |
| `removeValue(item)` | `void` | Remove every occurrence of `item` within the used range. Length drops by the number removed |
| `push(item)` | `void` | Stack push: insert at the front. Equivalent to `insert(0, item)` but discards the bool. orLibrary convention: push grows at the front |
| `pop()` | `T` | Stack pop: remove and return the front element. Returns 0 on an empty array |
| `peek()` | `T` | Look at the front element without removing. Returns 0 on an empty array |
| `enqueue(item)` | `void` | Queue entry: add at the back. Equivalent to `append(item)` but discards the bool |
| `dequeue()` | `T` | Queue exit: remove and return the front element. Alias for `pop()` |
| `peekEnd()` | `T` | Look at the back element without removing. Returns 0 on an empty array |
| `popEnd()` | `T` | Remove and return the back element. Returns 0 on an empty array |
| `sort()` | `void` | Sort the used range ascending in place, using the default comparator (signed word compare). Sound for ints, addresses, object IDs |
| `sort(compare)` | `void` | Sort with a user-supplied comparator `func<int, T, T>` returning -1 / 0 / +1. Lambdas work: `arr.sort((int a, int b) => { ... });` |

**Sort algorithm**: insertion sort. O(N) best case for already-sorted or nearly-sorted input, O(N²) worst case for fully-reversed. Stable. Adaptive, well-suited to IF data where arrays often grow incrementally and sit mostly-sorted.

The element-search methods (`indexOf`/`find`/`contains`) and `swap` are type-checked at the call site against the array's element type `T`; passing a value of an incompatible type produces a compile-time error.

`length()` and `setLength()` surface the length tracking defined in §4.9, and `isTracked()` reports whether an array carries it (false for I6-native extern arrays).

**Deque orientation note.** `push`/`peek`/`pop` operate at the front of the array; `enqueue`/`peekEnd`/`popEnd` operate at the back.

### 16.2.8 `<linq>` - LINQ-style Fluent Operations

```bgl
#include <linq>   // auto-includes <array>
```

Chainable transformations on arrays, modeled on C# LINQ syntax. **LINQ is a separate opt-in extension**: it lives in `<linq>`, not `<array>`, so programs that use extended arrays (or `<bglWorld>`) but not chains don't pay for the chain scratch buffers. `#include <linq>` pulls in `<array>` automatically. Calling a chain op without `<linq>` is a compile error that points you here.

Operations split into **non-terminals** (return a typed array, can chain further) and **terminals** (collapse a chain to a single value).

| Method | Returns | Description |
|---|---|---|
| `filter(pred)` | `array<T>` | Keep elements where `pred(elem)` is true. `pred` is `func<bool, T>`; preserves T |
| `map(f)` | `array<var>` | Apply `f(elem)` to each element. `f` is `func<var, T>`; returns `var` since the mapper's output type can't be tracked statically |
| `take(n)` | `array<T>` | Keep first `n` elements. Negative `n` clamps to 0; over-length clamps to `length()` |
| `skip(n)` | `array<T>` | Drop first `n` elements. Negative `n` clamps to 0; over-length clamps to `length()` |
| `takeWhile(pred)` | `array<T>` | Keep elements while `pred(elem)` is true; stops at the first failure |
| `skipWhile(pred)` | `array<T>` | Drop elements while `pred(elem)` is true; keeps everything from the first failure on |
| `distinct()` | `array<T>` | Keep the first occurrence of each value. O(N²) scan, fine at IF scale |
| `orderBy()` | `array<T>` | Return source sorted ascending using the default comparator. Source array is untouched (copies first, then sorts the copy) |
| `orderBy(compare)` | `array<T>` | Same with a user-supplied `func<int, T, T>` comparator |
| `first()` | `T` | Return the first element, or 0 on an empty array (same convention as `pop`/`peek`) |
| `last()` | `T` | Return the last element, or 0 on an empty array |
| `count()` | `int` | Alias of `length()` for LINQ naming familiarity |
| `any(pred)` | `bool` | True if any element matches the predicate; short-circuits. `false` on an empty array |
| `all(pred)` | `bool` | True if every element matches the predicate; short-circuits. `true` on an empty array (vacuous truth) |

**Chain model.** A chain's non-terminal result lives in scratch until the next chain starts (or the next non-terminal step). Terminals just read; they don't write to scratch. To keep a result, capture it (see below).

```bgl
// Result lives in scratch until the next chain starts (or the next non-terminal step).
print(g_nums.filter((int x) => x > 0).map((var y) => y * 2)[0]);
```

**Configurable scratch size.** `#beguilerSettings { linqScratchSize = N; }` controls the per-buffer capacity (default 32 elements). Chain steps that would exceed it print a runtime diagnostic and call `quit;`:

```bgl
#beguilerSettings {
    linqScratchSize = 128;
}
```

**Nesting.** Chains **may be nested**: a chain inside another chain's predicate or mapper lambda (`arr.filter(x => x.subArr.filter(q).any(r))`) runs correctly. Nesting is supported to depth 2 (a chain inside a chain); nesting deeper than that raises a runtime error. To persist a result beyond that, capture it (see below).

**Caveats.**

- **Lifetime ends at the next chain start; capture to keep it.** A chain's non-terminal returns a typed handle into scratch; the next chain that runs overwrites that scratch. Consume the result inline (or via a scalar terminal), OR assign it to a typed array local, which copies it into stable storage that survives later chains via copy-on-assign (§4.9.2): `array<int> evens = arr.filter(isEven);` then use `evens` freely. This is the capture mechanism for both stored results and *sibling* chains in one statement.
- **Local-array source caveat.** Local arrays passed as a chain source are safe within the same statement; however, a *returned* local array is ephemeral: its frame slice is freed on routine exit (§4.9.2). Don't `return arr.filter(p);` from a function with local arrays; assign the result to a typed local first (which copies it) if it must outlive the call.

## 16.3 IF Library Bindings

Files in `beguiLib/bindings/`. Each binding file is a Beguile declaration layer over one particular external IF library, giving Beguile code typed, name-checked access to that library's attributes, globals, and actions. Binding files do not define behavior; they map existing I6 names into the Beguile type system using `extern` declarations.

Different IF libraries require different binding files and would not be used together.

### Available Bindings

**`bindings/i6StandardLibrary`** - Bindings for the Inform 6 standard library (`parser.h`, `verblib.h`, `grammar.h`). Declares standard world-model attributes, mutable library globals, parser variables, compass direction objects, and the full set of standard IF actions as typed verbs. Automatically maps `title`, `headline`, and `release` from `#beguilerSettings` to the I6 constants the library expects.

**`bindings/punyInform`** - Bindings for the PunyInform library (`globals.h`, `puny.h`). Declares PunyInform's attributes (including `reactive`), globals, color settings, and verb actions. PunyInform uses no classes; objects are distinguished by attributes alone. Automatically maps `title` and `headline` from `#beguilerSettings`.

---

# Appendix A - Glossary

Terms of art used throughout this specification. Each entry points to the section with the full treatment.

- **anchor** - In a verb `synonyms` declaration, the verb whose grammar table the synonym words alias; a later `extend` on the anchor flows to every synonym. See Chapter 13.
- **Beguiler** - The Beguile compiler, which transpiles Beguile source to Inform 6 and then invokes the Inform 6 compiler. See §1.3.
- **binding** - A BLR declaration layer that exposes an external Inform 6 library's symbols to Beguile as typed `extern` declarations. See §16.3.
- **BLR (Beguile Language Runtime)** - The library of core types, emitters, and IF-domain bindings that Beguile programs compile against. See Chapters 16 and 17.
- **default mode** - The normal compilation mode: the source is a `.bgl` file that may embed raw Inform 6 through `#i6` islands. Contrast **precompiler mode**. See §14.1.
- **eBool** - The enum `{ true, false }` that comparison operators and `operator ?()` return; Beguile's boolean-result type. See §4.7.
- **emitter** - A method, operator, or function whose body is raw Inform 6 that the compiler inlines at each call site instead of emitting a routine call. See §14.4.
- **emitter class** - A class whose instances have no Inform 6 object backing; it serves as a type label and a host for emitter members. See §5.2.3.
- **ephemeral** - A returned local string or array whose backing storage is reclaimed at routine exit; it must be captured (assigned to a local) to persist. See §4.9.2 and §16.2.4.
- **island** - A region of one language embedded in a file written in the other: an `#i6` island is raw Inform 6 inside a Beguile file; a `#bgl` island is Beguile inside an Inform 6 file. See §14.1.2, §14.5, and §14.6.
- **loose identifier mode** - The relaxed name resolution used inside `#bgl` islands, where an unresolved identifier passes through to Inform 6 rather than raising an error. See §14.6.
- **precompiler mode** - The compilation mode where the source is an `.inf` (Inform 6) file with Beguile embedded through `#bgl` islands. Contrast **default mode**. See §14.1.
- **pretty lie** - A shorthand syntax that desugars to the language's canonical form at parse time. See §1.2.
- **size vs. length** - For a Beguile array, `size()` is the capacity reserved at compile time; `length()` is the runtime count of in-use elements. See §4.9.
- **superposed** - A declaration qualifier marking a routine, global, or object that is emitted only if something references it. See §14.10.
- **tracked / untracked array** - A tracked array carries a runtime length word; an untracked array (a raw Inform 6 array, or a `rawArray` view) does not. See §4.9 and §4.9.1.
- **`$self` / `$val` / `$target` / `$prop`** - Substitution tokens used inside emitter bodies: `$self` is the receiver, `$val` is the receiver's value, `$target` is an assignment's left-hand side, and `$prop` is an array property name. See §14.4.3.
- **`_bglObject`** - The root base class in the BLR that every Beguile type ultimately inherits from, including `object` and the primitive wrappers. It supplies the shared member surface; user code inherits it transitively and rarely names it directly. See §5.2 and Chapter 15.

---
