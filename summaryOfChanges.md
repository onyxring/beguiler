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

## 4. `extend class` and `replace` for Class Members

The `extend` keyword adds members to a previously defined class without redefining it. Any member type is supported: emitter functions, non-emitter functions, and variable properties.

```bgl
extern class string {
    emitter string operator = (stringLiteral v){ $self=v; }
}

extend extern class string {
    replace emitter string operator = (stringLiteral v){ $self.set(v); }  // replaces original
    emitter string operator + (stringLiteral v){ $self.append(v) }         // new member
}
```

### `replace` inside `extend class`

The `replace` qualifier is used inside an `extend class` block to replace an existing member. It is **required** when the new member would duplicate an existing one; adding a duplicate without `replace` is a compile error.

- **For functions:** matching is by name and full parameter-type signature (arity + each param type in order). The old entry is removed from the member list and the new one appended.
- **For variable properties:** matching is by name alone. The old entry is removed and the new one appended.
- **`replace` on a non-existent member** — issues a compiler warning and adds the member as new (no error).
- **`replace` outside `extend class`** — compile error.

```bgl
extend extern class string {
    // function replace: must match name + param types exactly
    replace emitter void print() { str.print() }

    // property replace: matches by name
    replace int capacity = 256;
}
```

Note: `extend class` requires the class name as a `dataType` token (already registered), while a plain `class` declaration requires an `identifier` token (not yet registered).

See also: §35 for `replace` applied to global functions (same semantics, different scope).

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

---

## 31. Property-Type-Inheritance in Object Bodies

Inside an object body, a property can now be set without repeating the type, provided the property is declared on the base `object` class:

```bgl
object foyer {
    string short_name = "Foyer of the Opera House";
    attributes = {light};    // type inferred from base object class
}
```

Previously this required:
```bgl
    attributeCollection attributes = {light};
```

**How it works:**
- `attributeCollection attributes;` was added to `extern class object` in `system.bgl`, registering `attributes` as a base object property.
- In `processObjectDeclaration`, when the first token is an `identifier` (not a registered data type), the parser looks up the name in the `object` class's member list to find the type.
- If found, parsing continues normally using the inherited type — including initializer list type-checking.
- If not found, a compile error is raised: `'name' is not a property defined on the base object class`.

This extends naturally to any future properties added to `extern class object` in `system.bgl` or library files via `extend extern class object`.

---

## 32. Statement-Level Method Chaining

Method calls can now be chained as a statement: `c.toLower().print()`. Previously only single method calls worked as standalone statements; a trailing `.method()` would cause a parse error.

```bgl
c.toLower().print();    // toLower() returns char, .print() called on result
str.trim().print();     // works across any chain of emitter methods
```

**Implementation:** After parsing the argument list of a method call statement, the parser checks for a trailing `.` or `dictionaryWord` token (after `)`, the lexer classifies `.method` as a single `dictionaryWord` token because `prevTokenType` is `symbol`). Each chained call resolves its method on the return type of the previous call, substitutes `$self` with the fully-resolved body of the accumulated chain, and collapses everything into a single `functionCallStatement`. Chains of arbitrary depth are supported.

---

## 33. `init`/`deinit` Parameter Rule

A compile-time error is now raised if an `emitter init` or `emitter deinit` declares any parameters. These lifecycle emitters are called implicitly by the compiler and cannot accept arguments.

```bgl
emitter void init(int x) { ... }   // error: Emitter 'init' cannot accept parameters
emitter void deinit()    { ... }   // ok
```

---

## 34. Global Function Overload Resolution — Exact Match Priority

The global function overload resolver now uses three priority tiers:

1. **Exact type match** — all param types equal arg types (no conversion needed); first match wins and short-circuits the search.
2. **Conversion match** — arg is compatible via a `operator()` conversion; recorded but search continues in case an exact match appears later.
3. **`var` fallback** — overload uses `var` for the matching param.

Previously, only tiers 1 and 3 existed (no separation between exact and conversion matches), so a conversion-compatible overload from a file loaded earlier could shadow an exact-match overload from a file loaded later. This caused `print(str)` (where `str` is `string`) to match `print(stringLiteral str)` via the `emitter stringLiteral operator(){}` conversion, producing empty arg text at emit time.

```bgl
// system.bgl — loaded first
emitter void print(stringLiteral str){ print (string)str; }
emitter void print(string str){ print (string)str; }   // conversion match for string args
emitter void print(var val){ print val; }              // var fallback

// string.bgl — loaded later (after #include <string>)
replace emitter void print(string str){ str.print() }  // exact match — now correctly wins
```

---

## 35. `replace` Qualifier for Global Functions

The `replace` qualifier can precede a global function declaration to patch the body of an already-registered function with the same name and parameter types, rather than adding a new overload.

```bgl
// In system.bgl:
emitter void print(string str){ print (string)str; }

// In string.bgl (loaded later via #include <string>):
replace emitter void print(string str){ str.print() }
// The existing print(string) entry in globals has its body replaced in-place.
```

This is used when a library file wants to provide a better implementation of a function that was declared with a placeholder body in `system.bgl`. The replaced entry keeps its original position in the globals list, so emission order is unchanged. A compile error is raised if no matching function is found to replace.

See also: §4 for `replace` applied to class members inside `extend class` (same semantics, different scope).

---

## 36. Ternary `?:` as a General Expression

The `?:` operator can now appear as an argument to a function call or on the RHS of an assignment, not only as a top-level assignment (which was already supported — see §7).

```bgl
print(cloak.parent() == hook ? "with a cloak hanging on it." : "screwed to the wall.");
```

**Implementation:** A global scratch variable `_bgl_temp` (declared as `var` in `system.bgl`) is used as a staging register. When `parseExpression` encounters `?` at paren depth 0, it:

1. Captures the already-accumulated condition text
2. Parses the true-branch (terminated by `:`) and false-branch (terminated by the outer terminator)
3. Builds a pre-statement: `if (cond) _bgl_temp = trueExpr; else _bgl_temp = falseExpr;`
4. Queues it in `bglParser::pendingInjections`
5. Replaces the expression with just `_bgl_temp`, carrying the true-branch's resolved type forward

Before each function-call or assignment statement is pushed to the statement block, any queued injections are drained first, so the pre-statement is emitted immediately before the containing statement:

```inform6
if (parent(cloak)==hook) _bgl_temp = "with a cloak hanging on it."; else _bgl_temp = "screwed to the wall.";
print (string)_bgl_temp;
```

The resolved type carried forward from the true branch drives overload resolution on the containing call (e.g., `print(stringLiteral str)` is selected above, correctly emitting `print (string)_bgl_temp`).

**Known limitations:**

- **Both branches must be the same type.** If one branch is a `stringLiteral` and the other is a `string` object, the two assignments store fundamentally different I6 values into `_bgl_temp` (a packed string address vs. an object handle). Downstream code expecting one will fail on the other. The `emitter stringLiteral operator(){}` conversion declared on `string` is a body-less compatibility declaration — no conversion code is generated — so it cannot bridge this gap in the ternary context. It is theoretically possible to convert a string literal to a string object when the other branch is an object, but this is not currently implemented.
- **Nested ternaries sharing `_bgl_temp`.** A ternary inside another ternary's condition would overwrite `_bgl_temp` before the outer ternary consumes it. The safe scope is: one ternary per statement.
- **Ternary as argument to a function that is itself an argument.** The injection mechanism operates at the statement level; ternaries inside a sub-expression that is itself an argument to another call cannot inject a pre-statement. Ternaries must appear as direct arguments to a top-level statement-level call.

---

## 37. Returning a Value from a Void Routine — Compile Error

Any attempt to return a value from a `void` routine is a compile-time error. This covers all three forms:

```bgl
void short_name(){
    rtrue;            // error: Cannot use 'rtrue' in void routine 'short_name'
    rfalse;           // error: Cannot use 'rfalse' in void routine 'short_name'
    return someVal;   // error: Cannot return a value from void routine 'short_name'
}
```

A bare `return;` (no value) is still permitted in void routines.

---

## 38. Non-Void Routine with No Return — Compile Error

A routine declared with a non-`void` return type that has no `return` statement anywhere in its body (including inside `if`/`while`/`for`/`do`/`switch` branches) is now a compile-time error.

```bgl
bool check(){
    // no return statement → error: Non-void routine 'check' has no return statement
}
```

The check is performed by a recursive `hasReturn()` helper that walks the entire statement block tree after the body is parsed.

---

## 39. Variable Priority Over Verb Names in Identifier Resolution

When an identifier matches both a declared variable and a verb name, the variable always wins. The verb-name `##VerbName` interpretation is only reached if no variable, parameter, local, or global declaration matches.

This fixes a bug where `score = ##Score + 1;` was incorrectly emitting `##Score = ##Score + 1;` because the verb lookup ran before the variable lookup.

The priority order in `qualifyIdentifier` and `resolveIdentifierType` is:
1. Function parameters
2. Local variables (current block)
3. Enclosing function body locals
4. Object/class members → `self.name`
5. Enum values and globals
6. Verb names (Tier 4, new) — plain name returned; `##` is added only by emitter bodies or switch case emission

---

## 40. `verb` Type — Action Constant Comparisons

A `verb` type class is declared in `system.bgl` with a `==` emitter that correctly generates `##VerbName` for the RHS action constant in comparisons:

```bgl
extern class verb{
    emitter eBool operator == (verb v){ $self == ##v }
}
```

The `action` library variable is declared as `extern verb action` (changed from `extern const int action`), giving it type `verb` so that comparisons against action constants use the emitter above.

**Emission rules:**
- `action == Take` → LHS type `verb`, fires `operator ==(verb v)` → `action == ##Take` ✓
- `switch(action) { case Take: ... }` → case value type `verb` → i6Emitter prepends `##` → `##Take:` ✓
- `score.go()` → verb identifier used as plain name, no `##` → `scoreSub()` ✓
- `int == verb` — no emitter defined → compile error ✓

**Switch case type enforcement:** The switch parser validates that case value types match the condition type, with a special exemption that `verb` case values are always permitted (since they resolve to integer action constants compatible with any integer switch condition).

**Implementation:** Verb names resolve to their plain I6 name via `qualifyIdentifier` Tier 4. The `##` prefix is applied only by the `operator ==` emitter body (`##v`) and by the i6Emitter switch case emission path (which prepends `##` when `sc->values[i]->resolvedType == "verb"`). The `verb` type is registered naturally when `extern class verb { ... }` is processed in `system.bgl`, not pre-registered in the language service constructor.

---

## 41. `break` and `continue`

`break` and `continue` are valid statements inside `while`, `for`, `do-while`, and `switch` bodies.

```bgl
while(n < 10){
    if(n == 5) break;
    if(n == 3) continue;
    n++;
}
```

`break` exits the enclosing loop or switch immediately. `continue` skips the remainder of the current loop iteration and re-evaluates the loop condition. Both require a terminating semicolon.

---

## 42. Compound Assignment Operators

The compound assignment operators `+=`, `-=`, `*=`, `/=`, `%=`, `|=`, and `&=` are supported as statements.

```bgl
n += 2;
score -= penalty;
```

**Emitter override:** If the LHS variable's type defines a matching emitter for the operator, that emitter body is used, with `$self` substituted for the LHS variable and the parameter name substituted for the RHS expression. This is the same mechanism as `operator =`.

```bgl
extern class score {
    emitter score operator += (int v){ $self = $self + v }
}
```

**Fallback:** If no emitter is defined, the compound form is expanded to its equivalent simple assignment, since I6 does not natively support compound assignment operators:

```
n += 2        →   n = n + 2;
score -= pen  →   score = score - pen;
```

---

## 43. Increment and Decrement Operators

The `++` and `--` operators are supported as standalone statements in both postfix and prefix forms.

```bgl
n++;   // postfix increment
n--;   // postfix decrement
++n;   // prefix increment
--n;   // prefix decrement
```

**Emitter override:** A type may define separate emitters for the postfix and prefix forms. The operator name used when declaring the emitter distinguishes them:

| Form | Emitter name |
|---|---|
| `n++` | `operator ++` |
| `n--` | `operator --` |
| `++n` | `operator prefix++` |
| `--n` | `operator prefix--` |

Example:

```bgl
extern class counter {
    emitter counter operator ++      (){ $self + 1 }
    emitter counter operator prefix++(){ $self + 1 }
}
```

**Fallback:** If no emitter is defined for the form used, the statement is emitted directly to I6 as `n++;`, `--n;`, etc.

---

## 44. `null` Keyword

`null` is a reserved keyword representing the null object reference. It maps to the I6 value `nothing`.

```bgl
object o = null;
if(parent(o) == null) print("no parent");
```

`null` has the resolved type `object`, allowing it to be used anywhere an object-typed value is expected without a type error. It cannot be used as an integer or string value.

---

## 45. Implicit Type Conversion at Assignment

When assigning a value of type `A` to a variable of type `B`, the compiler applies implicit conversion using the following priority:

1. **LHS `operator =`** — if type `B` defines `emitter B operator = (A v){ ... }`, that emitter is used.
2. **RHS `operator()`** — if type `A` defines `emitter B operator(){ ... }` (a no-parameter conversion operator returning `B`), the RHS expression is rewritten using that emitter body with `$self` substituted, and a plain assignment is emitted.
3. **Error** — if neither applies, a compile-time type mismatch error is reported.

This applies to both assignment statements (`x = expr;`) and variable declaration initialisers (`B x = expr;`).

```bgl
extern class celsius {
    emitter fahrenheit operator(){ $self * 9 / 5 + 32 }
}

fahrenheit f = someTemp;   // fires celsius.operator() if someTemp is celsius
f = anotherTemp;           // same rule applies to plain assignment
```

The LHS `operator =` always takes precedence over the RHS `operator()`. Implicit conversion also applies to function call arguments (unchanged from prior behaviour). The priority is consistent across all three contexts.

---

## 46. Global Variable Shadowing — Prohibited

A local variable declared inside a routine **may not share a name with any global variable**. If a local declaration would shadow a global of the same name, the compiler reports a compile-time error and halts translation.

```bgl
global int score = 0;

routine foo(){
    int score = 5;   // ERROR: local 'score' shadows global variable of the same name
}
```

**Rationale:** Because Beguile compiles to I6, and I6 provides no syntax to access a global variable that has been shadowed by a local, the Beguile compiler disallows the condition entirely. Rename the local variable to avoid the conflict.

This restriction applies to variable declarations inside routines, methods, and emitters. It does not apply to function parameters, which are handled separately (see §10).
