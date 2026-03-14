# Beguile Language Specification

---

# Chapter 1 — Introduction

## 1.1 What Is Beguile?

Beguile is a statically-typed, compiled language designed for authoring interactive fiction (IF). It provides a structured, C-like syntax that transpiles to Inform 6 (I6), a low-level language traditionally used to target the Z-Machine and Glulx virtual machines — the runtime platforms used by most modern IF interpreters.

Beguile is not a general-purpose language. Its type system, object model, and standard library are shaped by the needs of interactive fiction: rooms, objects, attributes, verbs, and the grammar that connects player input to game logic.

## 1.2 Design Goals

- **Familiar syntax.** Beguile reads like C or a C-adjacent language. Developers comfortable with C, C++, Java, or similar languages should find the syntax intuitive.
- **Strong typing.** All variables, parameters, and return values carry a declared type. Type mismatches are caught at compile time, not at runtime.
- **Transparency.** The generated I6 is readable and maps closely to the Beguile source. Developers who know I6 can inspect or supplement the output.
- **Extensibility through emitters.** Performance-sensitive or platform-specific operations can be expressed as *emitters* — inline I6 fragments that are substituted at the call site. This gives library authors full control over the generated code without sacrificing type safety at the Beguile level.
- **IF-native constructs.** Verbs, grammar, attributes, and world objects are first-class concepts in the language, not simulated through generic data structures.

## 1.3 Compilation Model

A Beguile source file (`.bgl`) is processed in a single forward pass by the Beguile compiler. The compiler:

1. **Lexes and parses** the `.bgl` source, resolving types and checking compatibility as it goes.
2. **Emits** an Inform 6 source file (`.inf`) that is semantically equivalent to the Beguile source.
3. **Invokes the Inform 6 compiler** (`inform`) on the generated `.inf` file to produce the final story file (`.ulx` for Glulx or `.z8` for Z-Machine).

The intermediate `.inf` file is retained alongside the story file. Source maps (`.inf.map`) are also produced, correlating positions in the generated I6 back to lines in the original `.bgl` source.

## 1.4 Relationship to Inform 6

Beguile is built on top of Inform 6, not a replacement for it. The generated I6 is valid, human-readable Inform 6 source. Several Beguile features exist specifically to bridge the two languages cleanly:

- `extern` declarations allow Beguile to use types, functions, attributes, and constants that are defined in I6 without re-implementing them.
- `#includeI6` passes raw I6 include directives through to the generated file.
- Emitter bodies contain literal I6 code, giving library authors precise control over the output.

Authors who need capabilities beyond what Beguile exposes can always drop down to I6 through these mechanisms.

## 1.5 A Note on Scope

This specification describes the Beguile language as recognized by the compiler. It does not describe the Beguile standard library (beyond noting where standard types are used in examples), the Inform 6 language itself, or the behavior of the Z-Machine or Glulx virtual machines. Readers wanting background on those topics should consult the Inform 6 documentation and the relevant virtual machine specifications.

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
if(x == 1) print("yes");
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

Identifiers beginning with `_bgl` are reserved for compiler-generated symbols (loop counters, frame pool variables, lambda functions, scratch temporaries, etc.). User code must not declare names that begin with `_bgl`. Collisions will produce undefined or erroneous I6 output.

## 2.5 Literals

### 2.5.1 Integer Literals

A sequence of decimal digits. Negative values are formed by applying the unary `-` operator to a positive literal.

```bgl
42
0
1000
```

### 2.5.2 String Literals

A string literal is enclosed in double quotes. The following escape sequences are recognized:

| Escape | Meaning |
|--------|---------|
| `\n`   | Newline (emitted as `^` in I6) |
| `\"`   | Double-quote character (emitted as `~` in I6) |
| `\\`   | Literal backslash |

```bgl
"Hello, world!"
"She said, \"well done.\""
"Line one\nLine two"
```

### 2.5.3 Character Literals

A single character enclosed in single quotes. Escape sequences follow the same rules as string literals.

```bgl
'a'
'\n'
'\\'
```

Character literals are typed as `charLiteral` and participate in operator overload resolution independently of integer literals.

### 2.5.4 Dictionary Word Literals

Dictionary word literals represent I6 dictionary entries — the tokens the parser uses to match player input.

- A **singular** dictionary word is written with a leading `.`: `.cloak`, `.hook`, `.velvet`
- A **plural** dictionary word is written with a leading `..`: `..cloaks`

```bgl
array<dictionaryWord> name = { .small, .brass, .hook };
```

Both forms resolve to type `dictionaryWord`. The plural flag is carried internally by the token and determines the I6 output (`'word'` vs. `'word/p'`). Dictionary word literals are recognized only when they do not immediately follow an identifier or type name — in that position, `.` is interpreted as the member-access operator instead.

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
| `#include <name>` | Include a standard library file from `beguilib/` |
| `#include "path"` | Include a Beguile source file by relative path |
| `#includeI6 "name"` | Emit an I6 `#include` directly into the generated output |
| `#define NAME` | Define a boolean compilation flag |
| `#define NAME value` | Define a symbol with a literal value |
| `#if expr` | Conditionally compile the following block |
| `#elif expr` | Alternative branch in a conditional block |
| `#else` | Final alternative branch |
| `#endif` | Close a conditional block |

Directives are described in full in Chapter 3.

## 2.8 Reserved Keywords

The following identifiers are reserved and may not be used as variable, function, type, or object names:

`if` `else` `for` `while` `switch` `case` `default` `break` `continue` `return`
`class` `extend` `extern` `object` `enum` `verb` `grammar` `attribute` `emitter`
`replace` `const` `array` `null` `self` `true` `false`
`int` `bool` `string` `void` `var`

---

# Chapter 3 — Program Structure

## 3.1 Source Files

A Beguile program consists of one or more `.bgl` source files. There is no designated entry point function; the Inform 6 runtime drives execution through lifecycle routines (`Initialise`, `begin_action`, etc.) that the author defines as ordinary Beguile functions.

Declarations at the outermost level of a file — types, classes, enums, variables, functions, objects, verbs, and grammar — constitute the *global scope*. Declarations may appear in any order within a file, but a name must be declared before it is referenced.

## 3.2 Include Directives

### 3.2.1 `#include <name>`

Includes a file from the Beguile standard library (`beguilib/` directory). The `.bgl` extension is appended automatically. The compiler performs a case-insensitive search of the library directory, so `#include <String>` and `#include <string>` are equivalent regardless of the file system. If two files in the library directory differ only by case, the compiler will select one arbitrarily.

```bgl
#include <system>
#include <string>
```

`system.bgl` is always the first file to include; it declares the core types (`int`, `bool`, `string`, `object`, `verb`, etc.) and the `print` family of functions.

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

### 3.3.2 Conditional Compilation

`#if`, `#elif`, `#else`, and `#endif` conditionally include or exclude blocks of source text. The expression following `#if` or `#elif` is evaluated at compile time against the currently defined symbols.

```bgl
#define DEBUG

#if DEBUG
    print("debug mode");
#endif
```

Nesting is supported. The compiler skips tokens in excluded branches without parsing them.

## 3.4 `#beguilerSettings`

The `#beguilerSettings` block configures the transpiler and the downstream Inform 6 invocation. It appears at most once per compilation and is not a function or class.

Each property may optionally be prefixed with a type name (which the parser ignores):

```bgl
#beguilerSettings {
    eTarget target   = Z5;
    string informPath = "/usr/local/bin/inform6";
    string includePath = "/inform6/lib";
    string includePath = "/myproject/lib";
    int    release    = 3;
}
```

### Toolchain paths

These settings tell the transpiler where to find external tools. They are not written to the generated I6 output.

| Setting | Type | Description |
|---------|------|-------------|
| `informPath` | string | Path to the Inform 6 compiler binary. Overrides the `INFORM6` environment variable and the default binary-adjacent search. |
| `beguiLibPath` | string | Path to the Beguile standard library directory. Overrides the `BEGUILE_LIB` environment variable and the default binary-adjacent search. |

### ICL directives

These settings emit `!%` ICL (Inform Control Language) lines at the top of the generated `.inf` file, which are interpreted by the Inform 6 compiler.

| Setting | Type | Description |
|---------|------|-------------|
| `target` | `eTarget` | Compilation target: `Glulx` (default), `Z3`, `Z5`, or `Z8`. Emits `!% -G` (Glulx) or `!% -v3`/`-v5`/`-v8` (Z-machine). If omitted, defaults to `Glulx` with a warning. |
| `includePath` | string | Adds a directory to the I6 compiler's include search path (`!% +include_path=...`). May be specified multiple times; paths are comma-joined into a single directive. |
| `release` | int | Sets the story release number (`!% Release N;`). Omit to leave unset. |
| `errorFormat` | string | Sets the I6 error format flag (`!% -EN`). For example, `"1"` emits `!% -E1` (Microsoft-style errors). |

### Runtime settings

These settings affect the generated I6 code rather than the ICL header.

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `framePoolSize` | int | `64` | Number of slots in the Z-machine local-variable overflow pool. Active only on Z3/Z5/Z8 targets. See §10.2.1. |

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
| `intLiteral` | `42` | Distinct from `int`; allows separate overloads for literal vs. variable integers |
| `stringLiteral` | `"hello"` | Distinct from `string` |
| `charLiteral` | `'a'` | Distinct from `char` |
| `dictionaryWord` | `.cloak`, `..cloaks` | Dictionary word; plural form (`..`) sets an internal flag that emits `'word/p'` in I6 |

A class may declare `emitter T operator(){}` (the conversion operator) to assert that it is I6-compatible with another type. This is how `int` supports mixed operations with `intLiteral` without requiring a separate overload for every operator (see §5.5.3).

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
```

Enum and bnum values are referenced by name directly (not qualified by the enumeration type name): `true`, `false`, `north`, `portable`, etc.

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

At global scope, arrays emit I6 `Array` directives. As class members, they emit inline property value lists. Array operations use `get`, `set`, and `length` methods:

```bgl
int x = scores.get(2);
scores.set(0, 99);
int n = scores.length();
```

Subscript syntax is equivalent to `get`:

```bgl
int x = scores[2];   // same as scores.get(2)
```

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

### 5.1.1 Class Aliases

An optional `alias` clause after the class name specifies a different name to use in the emitted I6 output. The Beguile name is used everywhere inside the compiler (type checking, member lookup, emitter dispatch); the alias name appears only in the generated `.inf` file.

```bgl
class MyVec alias Vector3 {
    int x = 0;
    int y = 0;
}
```

The I6 output will declare `class Vector3` rather than `class MyVec`. Any reference to `MyVec` as a base class in I6 will also use `Vector3`.

The alias is specified once at the initial declaration and cannot be changed by `extend class`.

## 5.2 Extern Classes

`extern class` declares a class that is implemented in I6. Beguile uses the declaration for type-checking and emitter dispatch only — no I6 class definition is generated.

An `alias` clause may be added when the Beguile name for the class differs from its I6 name. Because `extern class` generates no I6 output itself, the alias is used when the class name appears as a base class reference in a non-extern subclass or as a type in a global variable declaration.

```bgl
extern class string alias orString {
    emitter string operator = (stringLiteral v){ $self = v; }
    bool contains(string sub);
}
```

Rules for `extern class` members:
- **Emitters** must have a body.
- **Non-emitter functions** must not have a body; they are terminated with `;`. Parameter names are optional.
- **Variable properties** are declared normally, with or without a default value.

## 5.2.1 `emitter class`

`emitter class` declares a class that has no I6 backing at all — it exists purely in Beguile's type system to drive emitter dispatch. No I6 class definition is generated, and no I6 object or variable underlies instances of the type.

```bgl
emitter class celsius {
    emitter fahrenheit operator(){ $self * 9 / 5 + 32 }
    emitter celsius operator = (celsius v){ $self = v; }
}
```

Because there is no I6 structure to hold them, **all members of an `emitter class` must be emitters**. Non-emitter methods and variable properties are compile-time errors.

This distinguishes `emitter class` from `extern class`:

| | `extern class` | `emitter class` |
|---|---|---|
| I6 backing | Yes — defined in I6 | No — type system only |
| Non-emitter members | Allowed (declared as stubs) | Not allowed |
| I6 output | None (extern) | None |
| Combined with the other | Compile-time error | Compile-time error |

## 5.3 Member Variables

Member variables declare properties on the class. They are emitted as I6 `with` properties.

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

Methods are emitted as I6 method properties (`name[params; locals; body]`).

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
`+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `++` `--` `prefix++` `prefix--`

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

`extend class` adds new members to any already-declared class — extern or otherwise. It is commonly used to augment `extern class` types defined in the standard library, but it applies equally to user-defined classes.

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

The object name becomes a globally visible identifier that can be used wherever an `object`-typed value is expected.

## 6.3 Object Class

An object may be associated with a class by following the object name with a colon and a class name. The object will be an instance of that class in I6, inheriting its default property values and methods.

```bgl
class Animal {
    string short_name;
    void describe() { print(short_name); }
}

object dog : Animal {
    string short_name = "shaggy dog";
}
```

If no class is specified, the object is a plain I6 object with no class membership beyond the base `object` type.

## 6.4 Properties

Properties within an object body set initial values for the object's I6 properties. They are declared using a type-name-value form:

```bgl
object hook {
    string short_name = "small brass hook";
    int number = 0;
    object parent = cloakroom;
}
```

### 6.4.1 Type Inference from Base Object Class

Properties declared on `extern class object` in the standard library may be set without repeating the type. The compiler looks up the property name on the base object class and infers the type.

```bgl
object foyer {
    attributes = {light};       // type inferred: attributeCollection
}
```

If the name is not found on the base object class, a compile-time error is reported.

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

Attribute names must be declared with `extern attribute` (typically via `#include <i6Lib_types>`).

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

## 6.7 Forward Declarations

An object may be referenced before it is defined, provided an `extern object` declaration appears first:

```bgl
extern object message;   // forward declaration

object hook {
    bool description() {
        print(cloak.parent() == hook ? "A cloak hangs here." : "Just a hook.");
        rtrue;
    }
}

object message {         // full definition later in the file
    string short_name = "scrawled message";
    // ...
}
```

`extern object` produces no I6 output; it only registers the name in Beguile's type system so that subsequent references are valid.

## 6.8 Complete Example

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

## 7.5 Operator Emitters

An operator emitter defines how a built-in operator is compiled when the left-hand operand is of the declaring class. The operator symbol replaces the function name. Operator emitters may be declared on any class:

```bgl
class Counter {
    int value = 0;
    emitter bool operator == (Counter v){ $self.value == v.value }
    emitter Counter operator = (Counter v){ $self.value = v.value }
}
```

When the compiler encounters `a == b` and `a` has type `bool`, it looks for an `operator ==` emitter on `bool` whose parameter type matches the type of `b`. If found, the body is inlined with `$self` = `a` and `v` = `b`.

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

## 7.6 Conversion Operator

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

## 7.7 Lifecycle Emitters: `init` and `deinit`

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

## 7.8 Emitters vs. Regular Functions

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

Declarations at the outermost level of a source file — outside any function, class, or object body — are *global declarations*. They are visible throughout the entire compilation and emit corresponding I6 global constructs.

## 8.2 Global Variables

A global variable is declared with a type, a name, and an optional initializer:

```bgl
bool isGood = true;
bool isBad;
int score = 0;
string playerName;
```

Global variables emit I6 `Global` directives. An initializer may be a literal value or a simple expression; complex expressions that require runtime evaluation are not supported at global scope.

Every global variable name must be unique across the entire compilation. Declaring a second global with the same name as an existing global, class, object, or enum is a compile-time error that reports the original definition's location (see §13.6).

A local variable declared inside a routine may not share a name with any global variable. Doing so is a compile-time error (see §13.4).

## 8.3 Global Constants

The `const` modifier marks a global variable as read-only. The compiler prevents assignment to `const` variables.

```bgl
const string story    = "Cloak of Darkness";
const string headline = "A basic IF demonstration.";
const int MAX_SCORE   = 2;
```

Constants are emitted as I6 `Constant` directives. Attempting to assign to a `const` variable is a compile-time error.

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

Global functions emit I6 routines. Each global function name must be unique; I6 does not permit two routines with the same name, so the compiler will produce duplicate-name errors if two regular functions share an identifier. See Chapter 9 for full details on function syntax, parameters, and return rules.

Overloading by parameter type is supported only for emitters (see §7.3), because emitters are inlined at the call site and never emitted as named I6 routines.

### `replace` for Global Functions

The `replace` qualifier replaces the body of an already-registered global function or emitter. The replaced entry keeps its position in the global list; emission order is unchanged.

For **emitters**, matching is by name and full parameter-type signature (since emitters can be overloaded). For **regular functions**, matching is by name alone (since only one routine per name can exist in I6).

```bgl
// system.bgl — initial definition
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

An `extern attribute` declaration registers an I6 attribute name so that it can be used in `attributes` initializer lists and in attribute tests.

```bgl
extern attribute light;
extern attribute worn;
extern attribute clothing;
```

Attributes are declared in `i6lib_types.bgl` for the standard Inform library set. User-defined I6 attributes may be declared the same way.

Once declared, attributes are available as identifiers of type `attribute` and can be passed to the `give`, `ungive`, and `has` methods defined on `object` and `attributeCollection`.

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

All three parts are required. The initializer may declare a new variable (scoped to the loop) or assign to an existing one.

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

The array must be declared before the loop. Using an undeclared name is a compile-time error.

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

When the switch expression is of type `verb`, case values are emitted as `##VerbName` action constants automatically.

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
return;           // valid in void functions
return someValue; // valid in non-void functions
```

Returning a value from a `void` function is a compile-time error. A non-`void` function must have at least one reachable `return` statement; its absence is a compile-time error.

If the function's local variables have `deinit` emitters, they fire before the `return` is emitted.

---

# Chapter 11 — Expressions

## 11.1 Overview

Expressions appear as conditions in `if` and loop statements, as initializers in variable declarations, as right-hand sides of assignments, and as arguments to function calls. The compiler assigns each expression a *resolved type*, which drives operator overload resolution, type checking, and emitter dispatch.

## 11.2 Operands

An expression is built from one or more operands joined by operators. Operands are:

- **Integer literals** — resolved type `intLiteral`
- **String literals** — resolved type `stringLiteral`
- **Character literals** — resolved type `charLiteral`
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

The ternary operator may appear as an argument to a function call or on the right-hand side of an assignment. The compiler lowers it to an `if`/`else` pre-statement using a scratch variable (`_bgl_temp`), emitted immediately before the containing statement. The resolved type is taken from the true branch.

```bgl
print(x > 0 ? "positive" : "non-positive");
// emits:
//   if (x>0) _bgl_temp = "positive"; else _bgl_temp = "non-positive";
//   print (string)_bgl_temp;
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

### 11.6.3 Lifting

The compiler lifts each lambda to a named global routine `_bglLambda_N` (where *N* is a per-compilation counter). The variable is then assigned the address of that routine. This is fully transparent to the caller.

```bgl
func<void, int> printer = (int n) => { print(n); };
// emits:
//   [_bglLambda_0 n;
//       print n;
//   ];
//   ...
//   printer = _bglLambda_0;
```

### 11.6.4 Passing Lambdas as Arguments

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

### 11.6.5 Constraints

- **No captures.** A lambda body may not reference local variables from the enclosing scope. Globals and parameters of the lambda itself are accessible; locals of the enclosing function are not.
- **No immediate invocation.** The syntax `((int n) => { print(n); })(42)` is not supported. Assign to a variable or pass as an argument first.
- **`#i6raw` blocks.** Variable references inside raw I6 (`#i6raw { ... }`) are not rewritten by the lambda mechanism.

---

# Chapter 12 — Type Compatibility and Conversion

## 12.1 Overview

Beguile checks type compatibility at every assignment, variable declaration initializer, and function call argument. The compiler applies implicit conversion automatically when an exact type match is not present, using a defined priority order.

## 12.2 Compatibility Rules

A value of type `A` is compatible with a target of type `B` if any of the following hold, checked in order:

1. **Exact match** — `A == B`.
2. **Assignment operator** — type `B` defines `emitter B operator = (A v)` (the target type accepts the source type).
3. **Conversion operator** — type `A` defines `emitter B operator()` (the source type converts itself to the target type).
4. **I6 compatibility** — type `A` defines `emitter C operator(){}` where `C` is I6-compatible with `B` (an empty-body conversion, meaning both types share the same underlying I6 representation).

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
myGame.bgl:42:5: error: 'score' is already defined (originally declared at beguilib/i6lib_types.bgl:17)
```

Collisions detected at global scope:

| Conflict | Error |
|----------|-------|
| Global variable vs. global variable | `'name' is already defined (originally declared at file:line)` |
| Global variable vs. class/object/enum type | `'name' is already defined as a type (originally declared at file:line)` |
| Class vs. class | detected; error cites original class declaration |
| Object vs. object or class | detected; error cites original declaration |
| Enum vs. enum or other type | detected; error cites original declaration |

**Beguile vs. raw I6 collisions** (symbols declared via `#includeI6` or `#i6` blocks) are invisible to the Beguile parser and will surface as I6 compiler errors instead. Using `extern` declarations in a bridging file (such as `i6lib_types.bgl`) is the recommended way to make I6 symbols known to the Beguile type system and prevent silent conflicts.

**Naming conventions to avoid collisions:**
- Compiler-generated symbols use the `_bgl` prefix (reserved — see §2.4.1)
- Standard library internal symbols use the `or` prefix (e.g., `orString`, `orBufferWrapper`)
- User code should use application-specific prefixes for any global variables or types intended to coexist with the standard library

---

# Chapter 14 — Verbs and Grammar

## 14.1 Overview

Verbs and grammar declarations are the mechanism for adding new player commands to an IF game. A `verb` declaration defines an action and its handler routine. A `grammar` declaration adds input patterns — sequences of words and grammar tokens — that the Inform parser recognizes as triggering that verb.

## 14.2 Verb Declarations

A `verb` declaration registers a named action and optionally defines its `do` handler:

```bgl
verb Examine {
    do() {
        print("You examine it closely.");
    }
}
```

The `do` body is an ordinary Beguile function body. It is emitted as the I6 action routine `VerbNameSub`.

A verb with no handler (one that reuses a library verb's behavior) is declared as `extern verb`:

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

A `grammar` declaration adds input patterns for a named verb. The verb must already be declared (`verb` or `extern verb`) before the `grammar` block.

```bgl
grammar PutOn {
    {.hang, held, .on, noun},
    {.put, held, .on, noun},
}
```

Each line inside the braces is a comma-separated grammar pattern enclosed in `{}`. Patterns are positioned after `#includeI6 "grammar"` in the source file.

### Grammar Pattern Tokens

Each element of a pattern is one of:

| Token form | Meaning |
|------------|---------|
| `.word` | Dictionary word literal — matches the player typing that exact word |
| `..words` | Plural dictionary word |
| `noun` | I6 grammar token — matches any in-scope object |
| `held` | Matches a held object |
| `creature` | Matches a creature/actor |
| `topic` | Matches a topic phrase |
| Any identifier | Passed through as an I6 grammar token name |
| `tokenName=Routine` | Parameterized grammar token with a filter routine |

### Emitted I6

The grammar block above emits:

```inform6
Verb 'hang' 'put'
    * held 'on' noun -> PutOn;
```

(The first dictionary word in each line becomes the I6 `Verb` trigger word; subsequent identical triggers on different lines are merged.)

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
| `extern attribute Name;` | Registers an I6 attribute |
| `extern int Name;` | Mutable I6 global variable |
| `extern const int Name;` | Read-only I6 constant |
| `extern var Name;` | Untyped I6 variable (when type is unknowable) |

None of these produce any I6 output. They exist solely to make I6-defined names available in Beguile source with proper typing.

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

## 15.5 Source Maps

The compiler produces a source map file alongside the generated `.inf`, at `<filename>.transpiled.inf.map`. Each line maps an I6 output line number back to the originating Beguile source file and line number:

```
<i6LineNumber>\t<bglSourceFile>\t<bglLineNumber>
```

This supports future debugger and IDE tooling that needs to navigate between the Beguile source and the generated I6.

---

# Chapter 16 — Standard Library

## 16.1 Overview

The Beguile standard library is a set of `.bgl` files in the `beguilib/` directory. It is not built into the compiler — it is included explicitly by the author. The library files declare the core types, emitters, and utilities that most programs will need.

## 16.2 `system.bgl`

`system.bgl` is the foundation. It must be the first library file included. It declares:

- `extern enum eTarget` — compilation targets (`Glulx`, `Z3`, `Z5`, `Z8`)
- `extern enum eBool` — I6 boolean values (`true`, `false`)
- `extern class attribute` — base type for I6 attributes
- `extern class attributeCollection` — proxy for an object's attribute set; provides `give`, `ungive`, and `has` emitters
- `extern class grammarToken` — an I6 grammar token
- `extern class dictionaryWord` — an I6 dictionary entry; provides `print` emitter and `operator =`
- `extern class object` — the base object type; provides `print`, `operator =`, `operator ==`, `give`, `ungive`, `has`, `hasnt`, `move`, `remove`, `parent`, `child`, `sibling`
- `extern class int` — integer type with `operator =` (literal and int) and an empty `operator()` declaring I6 compatibility with `intLiteral`
- `extern class char` — ZSCII character; `operator =` from `charLiteral`, `print`
- `extern class bool` — boolean; `print`, `operator ==`, `operator =`
- `extern class string` — base string declaration (extended by `string.bgl`)
- `extern class array` — array type with `get`, `set`, and `length` emitters
- `extern class verb` — action constant type with `operator ==` emitting `##VerbName`
- `extern object arrayHelper` — runtime helper for array operations
- `var _bgl_temp` — scratch variable for ternary expression lowering
- `emitter void print(...)` — overloaded print for `var`, `stringLiteral`, `string`, and `char`
- `emitter void log(...)` — debug-conditional print; compiled out unless `DEBUG` is defined (see below)

### 16.2.1 `log()` — Debug Output

`log()` is an overloaded emitter that prints a value only when the I6 symbol `DEBUG` is defined at compile time. It accepts the same argument types as `print()`:

```bgl
log("entering room handler");
log(score);
log(playerName);
```

Each call emits an I6 conditional block:

```i6
#ifdef DEBUG; print (string)"entering room handler"; #endif;
```

To enable debug output, define `DEBUG` before any `#includeI6` in your source:

```bgl
#i6 Constant DEBUG;
#includeI6 "parser"
```

When `DEBUG` is not defined, the `log()` calls are completely compiled out with no runtime cost.

## 16.3 `string.bgl`

`string.bgl` provides the full string runtime, backed by a pool-allocated I6 string object (`orString`). It requires `#include <system>` before it.

Key additions to `string`:

| Member | Description |
|--------|-------------|
| `init` / `deinit` | Allocate/free string object from pool on declaration/exit |
| `operator = (stringLiteral)` | Set string from a literal |
| `operator = (string)` | Copy from another string |
| `operator + (stringLiteral/string)` | Concatenate, returning an ephemeral copy |
| `operator == (stringLiteral/string)` | Equality comparison |
| `print()` | Print string contents |
| `append(v)` / `prepend(v)` | Mutate string in place |
| `toUpper()` / `toLower()` / `trim()` / `reverse()` | Return modified copy |
| `mid(start, count)` / `left(n)` / `right(n)` | Substring operations |
| `length()` | Character count |
| `indexOf(text)` / `contains(text)` | Search |
| `startsWith(p)` / `endsWith(s)` | Prefix / suffix test |
| `replace(search, replace)` / `replaceAll(...)` | Substitution |
| `isEmpty()` | True if zero-length |
| `format(pattern, ...)` | Pattern substitution with `$0`, `$1`, etc. |

The `string` pool size defaults to 10 simultaneous strings. Override before including:

```bgl
// In your .bgl file before #include <string>:
// (use #i6 block or beguilerSettings to set orStringPoolReserve)
```

## 16.4 `i6lib_types.bgl`

`i6lib_types.bgl` bridges the Inform standard library. It declares:

- All 26 standard I6 attributes as `extern attribute` (e.g., `light`, `worn`, `clothing`, `openable`, `container`, `supporter`, etc.)
- Mutable library globals as `extern int` (e.g., `score`, `deadflag`, `location`, `turns`, `the_time`, `notify_mode`, etc.)
- Read-only constants as `extern const int` (parser error codes, scope reasons, color constants, etc.)
- Parser variables as `extern var` (`noun`, `second`) and `extern verb action`
- Typed classes for `InformLibrary` and `LibraryExtensions` with their methods
- All standard game actions and meta actions as `extern verb` (e.g., `Take`, `Drop`, `Go`, `Look`, `Examine`, `Open`, etc.)

---
