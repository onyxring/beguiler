# Summary of Changes to Beguile

Changes are listed in the order they were implemented.

---

## 1. ICL `include_path` Formatting Fix

Multiple include paths in `beguilerSettings` were being emitted as separate `!%` lines with line-feed separators, but the Inform 6 compiler requires them comma-separated on a single line.

```bgl
beguilerSettings {
    includePath = "/path/one";
    includePath = "/path/two";
}
```
Before:
```
!% +include_path=/path/one
!% +include_path=/path/two
```
After:
```
!% +include_path=/path/one,/path/two
```

---

## 2. `emitClass` Fully Implemented

The `emitClass` method in `i6Emitter.cpp` was stubbed out and produced empty class bodies. It was fully implemented to emit all non-emitter members in I6 `Class` format, with variables as properties and functions as methods.

```bgl
class test {
    int one = 1;
    var three = 3;
}
```
I6 output:
```inform6
Class test
  with
    one 1,
    three 3,
;
```

---

## 3. Inline Emitter Bodies for Assignment (`operator =`)

When a class defines an `emitter operator =`, the raw I6 body is now inlined at the assignment site, with `$self` substituted for the LHS variable and the parameter name substituted for the RHS value.

```bgl
extern class string {
    emitter string operator = (stringLiteral v){ $self.set(v); }
}

string s;
s = "hello";
// emits: s.set("hello");
```

---

## 4. `extend class` and `replace` Keywords

The `extend` keyword adds members to a previously defined class without redefining it. Inside an extend block, the `replace` keyword replaces an existing member. A compile error is thrown for duplicate members without `replace`; a warning is issued if `replace` targets a member that does not exist.

```bgl
extern class string {
    emitter string operator = (stringLiteral v){ $self=v; }
}

extend extern class string {
    replace emitter string operator = (stringLiteral v){ $self.set(v); }  // replaces original
    emitter string operator + (stringLiteral v){ $self.append(v) }         // new member
}
```

Note: `extend class` requires the class name as a `dataType` token (already registered), while a plain `class` declaration requires an `identifier` token (not yet registered).

---

## 5. `else` Clause

The `else` branch was previously stubbed. It now fully parses and emits, supporting both single-statement and block bodies.

```bgl
if(x > 0) {
    print("positive");
} else {
    print("non-positive");
}
```

---

## 6. `for` Loop

C-style `for` loop support was added: `for(init; condition; increment)`.

```bgl
for(int i=0; i<10; i++) {
    print(i);
}
```

---

## 7. Inline Conditional Assignment (`?:`)

Ternary-style conditional assignment `var = condition ? trueVal : falseVal` emits as an `if`/`else` assignment pair. Full emitter dispatch runs on each branch independently, so string assignments correctly emit `str.set(v)` on both sides.

```bgl
str = (j == 3) ? "hello" : "goodbye";
// emits:
//   if (j==3) str.set("hello"); else str.set("goodbye");

int j;
j = x > 0 ? 1 : 0;
// emits:
//   if (x>0) j=1; else j=0;
```

---

## 8. Class Member Default Values

Previously, declaring a class member with an initial value caused a parse error (`Unexpected = token`). The class member parser now accepts `=` followed by an expression as an optional default value.

```bgl
class test {
    int one = 1;     // default value of 1
    var three = 3;   // default value of 3
}
```

---

## 9. Class Method Body Parsing and Emission

Non-emitter methods inside class declarations were previously not parsed — the body tokens were left in the stream. They are now parsed as full statement blocks and emitted in I6 method format (`name[params; locals; body]`).

```bgl
class test {
    int one = 1;

    void say(int two, int three) {
        print(one);
        print(two);
    }
}
```
I6 output:
```inform6
Class test
  with
    one 1,
    say[two three;
        print self.one;
        print two;
    ],
;
```

---

## 10. `currentClass` Scope and `self.member` Resolution

Within a class method, bare member names (e.g. `one`) are now automatically resolved as `self.one` in the I6 output. Explicit `self.name` is also accepted. Previously, class member names were undeclared inside methods.

```bgl
void say(int two) {
    print(one);       // emits: print self.one;
    print(self.one);  // same
}
```

---

## 11. Global Scope Fallback in Identifier Resolution

Identifier lookup now checks four scopes in order: function params → local variables → class members (emitted as `self.x`) → global variables. Previously, the global scope was not checked and global identifiers triggered an undeclared error inside functions.

---

## 12. `var` Type — Deferred Overload Matching

The `var` type acts as a universal variant that bypasses type checking. When resolving overloaded functions, overloads with `var` parameters are saved as a fallback but never chosen if a typed overload matches. This prevents a general `var` overload from shadowing specific typed ones.

```bgl
emitter void print(var val){ print val; }             // fallback — used only if no typed match
emitter void print(stringLiteral str){ print (string)str; }  // preferred for string literals
emitter void print(string str){ str.print() }                 // preferred for string variables
```

---

## 13. `#include <library>` Directive and `eTokenType::name`

A new token type `name` was added that matches either `identifier` or `dataType`. This allows the parser to accept any word token regardless of registration status — used by `#include <filename>` (where the filename like `string` is already a registered type) and `extend class typename`.

```bgl
#include <string>            // "string" is a dataType, but #include just needs it as a name
extend extern class string { ... }  // "string" must be dataType — already registered
```

---

## 14. Expression Parsing

A general `parseExpression` function was implemented that reads a sequence of tokens into an `expression` object — a flat list of token strings with a resolved type. Expressions are used for variable initial values, assignment RHS values, `if`/`for` conditions, and function call arguments.

The expression parser handles:
- **Integer literals** — resolved type `intLiteral`
- **String literals** — resolved type `stringLiteral`
- **Identifiers** — type resolved via scope lookup (params → locals → class members → globals)
- **Period notation** — `self.member` (class member access) and `EnumType.value` (enum value)
- **Operator dispatch** — see section 15

```bgl
int bloof = 4 + 5;
string s;
s = "jim";
if(str == "jim") print(str);
```

---

## 15. Operator Dispatch in Expressions

Operators in expressions (`==`, `+`, `-`, etc.) are dispatched through the LHS variable's class definition. If the class defines an emitter for the operator accepting the RHS type, the emitter body is inlined. If no match is found, the raw operator is emitted verbatim.

```bgl
extern class bool {
    emitter eBool operator == (bool v){ $self == v }
    emitter eBool operator == (eBool v){ $self == v }
}

if(isBad == true) { ... }
// emits: if (isBad == true) { ... }
```

---

## 16. Conversion Operator — `operator()`

A zero-param emitter with a return type can be declared as `emitter <type> operator(){}`. This declares that the LHS class is I6-compatible with that type, enabling mixed-type operators (e.g. `int == intLiteral`) without requiring per-operator overloads for every possible RHS type.

```bgl
extern class int {
    emitter intLiteral operator(){}  // declares int is I6-compatible with intLiteral
}

// Without this, "j == 3" would error (int has no == accepting intLiteral).
// With it, the operator falls back to raw I6 emission: j==3
if(j == 3) { ... }
```

---

## 17. `init` and `deinit` Emitters

A class can declare zero-param `init` and `deinit` emitters. When a variable of that type is declared inside a routine:

- The `init` body is injected **before** the variable's initial-value assignment (so the object exists before `.set()` etc. are called on it).
- The `deinit` body is injected **before every `return`** and at the implicit end of the routine.

This enables RAII-style patterns, such as allocating an I6 object on declaration and freeing it on function exit.

```bgl
extend extern class string {
    emitter void init(){
        $self = GetNewString();   ! allocate a new I6 string object on declaration
    }
    emitter void deinit(){
        FreeString($self);        ! release the I6 string object on routine exit
    }
}

void doSomething() {
    string s;       // init fires here:   s = GetNewString();
    s = "hello";    // assignment after init
    return;         // deinit fires first: FreeString(s);
}                   // deinit also fires here on implicit fall-through
```

---

## 18. `switch` Statement

Full `switch`/`case`/`default` support. Since I6 cases do not fall through by default, `break` is accepted but silently dropped.

```bgl
switch(j) {
    case 1: print("one");
    case 2: print("two");
    default: print("other");
}
```
I6 output:
```inform6
switch (j) {
    1:
        print "one";
    2:
        print "two";
    default:
        print "other";
}
```

---

## 19. Case-Insensitive Parsing

All non-string-literal tokens are normalised to lowercase at lex time, making the language fully case-insensitive. This matches I6's own behaviour and means keywords, type names, and identifiers can be written in any case. String literal contents are preserved as-is.

```bgl
// All of these are equivalent:
IF(x == 1) print("yes");
If(x == 1) Print("yes");
if(x == 1) print("yes");
```

The type registry (`bglLanguageService`) also normalises names to lowercase on registration and lookup, so `Int`, `INT`, and `int` all resolve to the same type.

---

## 20. `isExternal` Guard in `emitGlobal`

`extern` declarations are type-system-only and must not produce any I6 output. `emitGlobal` now returns immediately when `varNode->isExternal` is true. Previously, extern variable declarations would emit a spurious `Global name;` line.

---

## 21. `i6Lib_types.bgl` (formerly `parser.bgl`)

The Inform standard library type definition file was renamed from `parser.bgl` to `i6Lib_types.bgl` (previously briefly named `iStdLibTypeDef.bgl`). All `const int` declarations were changed to `extern const int` with no assigned values, since they are defined externally by the I6 compiler/library.

`InformLibrary` and `LibraryExtensions` were promoted from untyped `extern object` declarations to fully typed classes:

- `extern class informLibraryType` — declares `play`, `end_turn_sequence`, `begin_action`, and `actor_act` emitter methods; instance declared as `extern informLibraryType InformLibrary`
- `extern class libraryExtensionsType` — declares `RunAll`, `RunUntil`, `RunWhile` methods and all `ext_*` hook properties; instance declared as `extern libraryExtensionsType LibraryExtensions`

This enables type-checked calls to library routines (`InformLibrary.begin_action(...)`, `LibraryExtensions.RunAll(...)`) rather than relying on untyped `var`.

---

## 22. `array<T>` Type

A new `array<T>` syntax declares word arrays backed by an I6 helper object. Arrays can be declared at global scope or as class members (properties).

```bgl
array<int> scores[5];                    // sized array
array<int> primes = {2, 3, 5, 7, 11};   // initialized array
```

**Global scope** emits native I6 `Array` directives:

```inform6
Array scores table 5;
Array primes --> 5 2 3 5 7 11;   ! count is auto-prepended
```

**Class member (property array)** emits inline property values:

```bgl
class Room {
    array<object> exits[6];   // emits: exits 0 0 0 0 0 0,
}
```

The `array` class is declared in `system.bgl` with three emitter methods — `get`, `set`, and `length` — each taking two context tokens:

- **`$self`** — the array name (global) or the owning object (property)
- **`$prop`** — `0` for global arrays, the property name for property arrays, or `"<$prop undefined>"` if used outside an array context

```bgl
extern class array {
    emitter var get(int i) {
        arrayHelper.get($self, $prop, i)
    }
    emitter void set(int i, var v) {
        arrayHelper.set($self, $prop, i, v)
    }
    emitter int length() {
        arrayHelper.length($self, $prop)
    }
}
extern object arrayHelper;
```

Subscript read syntax (`arr[i]`) is parsed and dispatched to the `get` emitter:

```bgl
int x = scores[2];   // emits: arrayHelper.get(scores, 0, 2)
```

---

## 23. `resolvePathType` for Dotted Path Resolution

A new `resolvePathType` function resolves the type of a dotted path expression (e.g. `"player.inventory"`) by looking up the head type in scope then finding the tail member in the resulting class. This enables multi-level dot-chains in method calls and subscript expressions:

```bgl
player.inventory.get(0)    // get() called on the inventory array of player
player.inventory[0]        // subscript form — same dispatch
```

---

## 24. `peekToken()` Bug Fix — Initializer List Semicolons

`peekToken()` called with no arguments internally calls `peekToken(0)`, which loops zero times and returns an empty default token. Any `if(file.peekToken().is(token::endStatement)) file.getToken()` guard therefore never fired, leaving the trailing `;` after `{ ... }` initializer lists unconsumed. The same bug also prevented `else` clauses from being detected after an `if` body.

The fix changes the no-arg overload in `fileLexer.cpp` from `return peekToken(0)` to `return peekToken(1)`, so it correctly saves the stream position, reads one token, restores the position, and returns that token.

---

## 25. `isValidIdentifier` Accepts Underscore-Prefixed Names

`token::isValidIdentifier()` previously required the first character to be alphabetic (`isalpha`), which excludes `_`. Tokens starting with `_` were therefore classified as `unclassifiedText` rather than `identifier`, causing the parser to reject them wherever an `identifier` was expected — including class declarations and variable declarations.

The condition was changed from:
```cpp
if(!(isalpha(value[0]) && value[0]!='_')) return false;
```
to:
```cpp
if(!(isalpha(value[0]) || value[0]=='_')) return false;
```

This allows names like `_bglRuntimeInitType` and `_bglRuntimeInit` to be declared as Beguile classes and variables, consistent with the convention of using underscore-prefix for internal library identifiers.

---

## 26. Source Map Infrastructure

The transpiler now records a source map alongside the transpiled `.inf` file, written to `<filename>.transpiled.inf.map`. Each entry maps an I6 output line number back to the originating Beguile source file and line number, enabling bidirectional navigation for future debugger and IDE tooling.

**Implementation:**
- `sourceLocation` struct (`file`, `line`) added to `typeDef.h`; a `src` field added to `statement` and `functionDef`.
- `fileLexer::currentLocation()` returns a `sourceLocation` for the current token position.
- `bglParser.cpp` stamps `src` on every statement node at parse time.
- `i6Emitter` accumulates `vector<tuple<int,string,int>> sourceMap` entries — one per statement — and writes them via `writeSourceMap()`.
- `beguiler.cpp` calls `emitter.writeSourceMap(filename + ".map")` after writing the `.inf` file.

Map file format (tab-separated):
```
<i6LineNumber>\t<bglSourceFile>\t<bglLineNumber>
```

---

## 27. `extern attribute` and Native `attributeCollection` Syntax

The `i6lib_types.bgl` library file (renamed from `i6Lib_types.bgl` — the `#include` resolver lowercases names) now declares all 26 standard Inform library attributes using a new `extern attribute` declaration:

```bgl
extern attribute light;
extern attribute worn;
extern attribute clothing;
// ... 23 more
```

Objects can now declare their initial attributes in native Beguile syntax instead of a `#i6{}` block:

```bgl
object foyer {
    string short_name = "Foyer";
    attributeCollection attributes = {light};
}
```

The emitter already handled `attributeCollection` members by emitting them as an I6 `has` clause — only the `extern attribute` type declarations were missing. With `#include <i6Lib_types>` in the source file, `light` (and the other attributes) are registered as known identifiers and the initializer list type-checks correctly.

---

## 28. `extern const <type>` Declarations

The parser previously only handled `const` before `extern` (i.e. `const extern`). The modifier check sequence in `processNextStatement` now accepts `const` either before or after `extern`, so `extern const int name;` is valid.

This matters for library declarations where the value is defined by the I6 compiler — the name is external, and Beguile should treat it as a read-only constant:

```bgl
extern const int STUCK_PE;    // parser error code — defined by I6 library
extern const int CANTSEE_PE;
```

The `isConst` and `isExternal` flags are both forwarded to `processVariableDeclaration` as before; no changes were needed in the emitter (extern declarations produce no I6 output regardless).

---

## 29. Mutable vs. Constant Extern Globals in `i6lib_types.bgl`

Following the addition of `extern const int` support, the extern declarations in `i6lib_types.bgl` were audited and split into two categories:

**`extern int` (mutable — game code may write these):**
- Game state: `deadflag`, `lightflag`, `score`, `last_score`, `turns`, `the_time`, `time_rate`, `time_step`
- Mode flags: `notify_mode`, `lookmode`, `transcript_mode`, `pretty_flag`, `before_first_turn`
- Parse-routine writable: `keep_silent`, `parsed_number`, `wn`

**`extern const int` (true read-only constants):**
- Parser error codes (`STUCK_PE`, `UPTO_PE`, etc.)
- Scope search reasons (`PARSING_REASON`, etc.)
- Color constants (`CLR_*`), window constants, debug flags, misc constants

This allows game code to write to mutable globals (e.g. `score = score + 1;`, `deadflag = 2;`) without using `#i6{}` blocks.

---

## 30. Enum Value Assignment

Enum and bnum declarations now support explicit value assignment for individual members:

```bgl
enum myPhase {
    setup  = 0,
    play   = 10,
    ending          // auto: 11
}
```

If a member has `= <integer>` after its name, that value is used directly and the auto-counter continues from there. Members without `=` continue incrementing (or bit-shifting for `bnum`) from the previous value, whether that was auto or explicit.

Only the parser was changed (`processEnumDeclaration` in `bglParser.cpp`). The `enumValueDef` struct and `emitEnum` in `i6Emitter.cpp` were already value-based and required no modification.
