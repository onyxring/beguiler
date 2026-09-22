# 21 Runtime Core

<!-- toc -->
- [21.1 Overview](#211-overview)
- [21.2 `bglInit()`](#212-bglinit)
- [21.3 The `bgl` Namespace](#213-the-bgl-namespace)
- [21.4 `print()`, `log()` and Article Helpers](#214-print-log-and-article-helpers)
- [21.5 IF-Domain Types](#215-if-domain-types)
  - [21.5.1 `attribute` and `attributeList`](#2151-attribute-and-attributelist)
  - [21.5.2 `property`](#2152-property)
  - [21.5.3 `dictionaryWord`](#2153-dictionaryword)
  - [21.5.4 `verb`](#2154-verb)
  - [21.5.5 Grammar Types](#2155-grammar-types)
  - [21.5.6 `bglClass`](#2156-bglclass)
  - [21.5.7 `parentProp` and `childrenProp`](#2157-parentprop-and-childrenprop)
  - [21.5.8 `_bglObject`](#2158-bglobject)
  - [21.5.9 `eType` and `typeof()`](#2159-etype-and-typeof)
  - [21.5.10 `stringOrRoutine`](#21510-stringorroutine)
- [21.6 Numeric Utilities](#216-numeric-utilities)
  - [21.6.1 `uint`](#2161-uint)
  - [21.6.2 `bgl.util.math`](#2162-bglutilmath)
  - [21.6.3 `bgl.util.random`](#2163-bglutilrandom)
- [21.7 Character Utilities](#217-character-utilities)
- [21.8 `bglAllocated`](#218-bglallocated)
- [21.9 `bgl.world`](#219-bglworld)
- [21.10 `bgl.ui`](#2110-bglui)
- [21.11 `bgl.printRules`](#2111-bglprintrules)
- [21.12 Utility Types](#2112-utility-types)
- [21.13 `bgl.asm`](#2113-bglasm)
  - [21.13.1 Glulx Opcodes](#21131-glulx-opcodes)
  - [21.13.2 Glk Calls](#21132-glk-calls)
  - [21.13.3 Z-machine Opcodes](#21133-z-machine-opcodes)
<!-- /toc -->

## 21.1 Overview

The Beguile Language Runtime (BLR) is the library of Beguile source that every program compiles against. Its *core* is loaded automatically: no `#include` is needed, in either default mode or precompiler mode (§15.1). Everything in this chapter is part of the core. The extensions, each enabled with `#include <…>` except `<array>`, which the core includes itself (§22.1), are in §22; the IF library bindings are in §23.

The core provides:

- the `bgl` namespace and its target-specific branches (§21.3);
- the output routines `print()` and `log()` and the article helpers (§21.4);
- the IF-domain types: `attribute`, `property`, `dictionaryWord`, `verb`, the grammar types, `bglClass`, `parentProp`, `childrenProp`, `_bglObject`, `eType` (§21.5);
- the numeric, character and allocation utilities: `uint`, `bgl.util.math`, `bgl.util.random`, the `char` methods, `bglAllocated` (§21.6–§21.8);
- object-tree queries, `bgl.world` (§21.9);
- the user-interface roots `bgl.ui.mainWin` / `bgl.ui.statusBar` and the print rules (§21.10–§21.11);
- the utility types `bglSize` and the blorb asset enums (§21.12).

The primitive types (`int`, `bool`, `char`, `string`, `float`, `object`, `var`) and their literals are documented in §2.2; they are also core.

In precompiler mode the core is loaded but the `bgl` namespace is not imported: a `#bgl` island must declare `#using bgl;` before using `bgl.…` (§15.1.2).

## 21.2 `bglInit()`

**Syntax**

```syntax
bglInit();
```

**Description**

`bglInit()` is a routine the compiler synthesizes for every program; its position in the generated file is given in §18.8. It runs at most once however often it is called and, in order:

1. the length headers of sized tracked arrays and byte arrays (§12.3, §22.2, §22.4);
2. every `#startup` block in the program, in file-inclusion order (§14.4.1);
3. the deferred initializers of class-typed globals whose type declares a parameterless `init` (§8.5): the `init` body, then the `operator =` that applies a declared value.

A program built on an IF library binding does not call `bglInit()` itself: the binding wraps the library's `main` so that `bglInit()` runs first (§23.3.1). A program built without a binding, or with `autoInitialize = false` (§17.4), must call `bglInit()` from its entry point before using anything that depends on it. The call is always available and is harmless when nothing has registered work.

Extensions that need `bglInit()` say so in their entry in §22: `<string>` and `<linq>` do, and so do `<array>` and `<buf>`, whose sized, uninitialized tracked arrays have no length header until it runs: before `bglInit()`, such an array reports its raw header word from `size()` and `length()` and `append` fails; arrays declared with an initializer list are complete at compile time. `<ui>`, `<glulxWindow>` and `<glulxImage>` do not need it.

**Example**

```bgl
void Main() {
    bglInit();
    // …
}
```

**See also** §14.4.1, §15.1.2, §18.8, §23.3.1.

## 21.3 The `bgl` Namespace

`bgl` is the root namespace object of the runtime. Its members are reached by dotted path (`bgl.asm.add(a, b)`, `bgl.util.math.pow(2, 8)`); `#using bgl.glulx;` and the other `#using` forms import a branch so its members are reachable bare (§10.4).

| Path | Contents | Target |
|---|---|---|
| `bgl.wordsize` | The word size of the active target: `2` on the Z-machine, `4` on Glulx. | both |
| `bgl.asm.*` | Direct opcode emitters for the active virtual machine; the set of members differs per target (§21.13). | both |
| `bgl.glulx.*` | Glulx-specific names: the enum aliases `eStyleType`, `eWinType`, `eImgAlign`, `eImgDimension`, `bWinBorder`, `bWinPlacement`, `bWinScale` (§22.7.10); the window types and `color` when `<glulxWindow>` is included (§22.7). | Glulx |
| `bgl.zcode.*` | Z-machine-specific names. Empty in the current core. | Z-machine |
| `bgl.util.*` | Utilities: `math` (§21.6.2), `random` (§21.6.3), and `buf` when `<buf>` is included (§22.2). | both |
| `bgl.world.*` | Object-tree queries (§21.9). | both |
| `bgl.ui.*` | `mainWin`, `statusBar` (§21.10); `screen` with `<glulxWindow>` (§22.7.2); `hideCursor`, `showCursor`, `waitForKey` with `<ui>` (§22.6). | both |
| `bgl.printRules.*` | Text-style print rules for interpolated strings (§21.11). | both |
| `bgl.story.*` | Story-file identity values. Provided by the `i6StandardLibrary` binding (§23.3.9). | both |

Only the branch for the active target is loaded: a Glulx build loads `bgl.glulx` and the Glulx `bgl.asm`; a Z-machine build loads `bgl.zcode` and the Z-machine `bgl.asm`. Referencing a member of the other target's branch is a compile-time error.

**Example**

```bgl
int bits = bgl.wordsize * 8;             // 16 on Z-machine, 32 on Glulx
int roll = bgl.util.random.get(6);       // 1..6
```

**See also** §10, §10.4, §22.

## 21.4 `print()`, `log()` and Article Helpers

**Syntax**

```syntax
print( ⟨value⟩ ) ;
print( $"⟨text⟩ {⟨expr⟩} ⟨text⟩" ) ;
log( ⟨value⟩ ) ;
a( ⟨obj⟩ ) ;   cA( ⟨obj⟩ ) ;   the( ⟨obj⟩ ) ;   cThe( ⟨obj⟩ ) ;   printName( ⟨obj⟩ ) ;
```

**Description**

`print()` writes a value to the current output stream immediately. It is overloaded on the argument's type; the core supplies overloads for every primitive type and the IF-domain types, and extensions and bindings add or replace overloads for the types they introduce (`<string>` replaces `print(string)`; a binding adds `print(stringOrRoutine)`). An interpolated string (§1.6.5) prints each segment with the overload for that segment's type.

`print(obj)` on a value whose type derives from `_bglObject` (§21.5.8) calls the value's own `print()` method when it defines one; otherwise it prints the object's short name. A class therefore customizes how its instances print by defining `void print()`.

The core declares `short_name` as a `string` member of `object`: the text (or routine) the article helpers and `printName()` print as the object's name. No binding redeclares it (§23.3.4).

`log()` accepts the same arguments as `print()` and is a debug-only output: it produces output only when the symbol `DEBUG` is defined (§14.2.1). Its arguments are parsed and type-checked in every build, so a release build still diagnoses errors inside a `log()` call.

The article helpers print a world-tree object with an article: `a(obj)` → "a lamp", `cA(obj)` → "A lamp", `the(obj)` → "the lamp", `cThe(obj)` → "The lamp", `printName(obj)` → "lamp" (the bare short name, no article).

**Example**

```bgl
extern attribute light;
object lamp { short_name = "brass lamp"; }

void Main() {
    print($"You see {a(lamp)}.");    // → You see a brass lamp.
    cThe(lamp); print(" glows.");      // → The brass lamp glows.
    log("reached Main");               // output only with #define DEBUG
}
```

**Notes**

The article forms depend on the active IF library's article and naming conventions (for example the `proper` attribute), which the library, not the core, defines.

**See also** §1.6.5, §14.2.1, §22.3.

## 21.5 IF-Domain Types

### 21.5.1 `attribute` and `attributeList`

**Syntax**

```syntax
attribute ⟨name⟩ ;
extern attribute ⟨name⟩ ;
attributeList attributes = { ⟨attr⟩ [ , !⟨attr⟩ ] … } ;
⟨obj⟩.give( ⟨attr⟩ )   ⟨obj⟩.ungive( ⟨attr⟩ )   ⟨obj⟩.has( ⟨attr⟩ )   ⟨obj⟩.hasnt( ⟨attr⟩ )
```

The `attributeList` form appears in a class or object body; the `!` is literal.

**Description**

`attribute` is the type of a single Inform 6 attribute (`light`, `container`, `static`, …). Attributes are usually bound with `extern attribute` by a library binding (§23.3.4); a program may also declare its own (§11.6).

`attributeList` is the type of a class's or object's `attributes` member: the set of attributes the object initially has. It takes an initializer list. A `!` prefix on an entry negates an inherited attribute, so the object starts *without* it even though its class has it. Only `=` is accepted on `attributes`; `+=` and `-=` are compile-time errors. `attributes = {…}` in an `extend` block is permitted only when the object's declaration has no `attributes` member; the list is additive relative to the class (§11.5.3).

At runtime an object's attributes change through `give()` and `ungive()` and are tested with `has()` and `hasnt()`; all four are defined on `object` and on `attributeList`.

`NO_ATTRIBUTE` is an `attribute`-typed constant meaning "no attribute". It is the only value other than a real attribute that may be assigned to an attribute-valued member; a bare `0` is a type error.

**Example**

```bgl
extern attribute light;
extern attribute scenery;
extern attribute static;

class lampPost : object {
    attributeList attributes = { light, static };
}
object brokenPost : lampPost {
    attributes = { !light };          // a lampPost, but not lit
}

void Main() {
    if (brokenPost.hasnt(light)) brokenPost.give(light);
}
```

**See also** §11.5.3, §11.6, §23.3.4.

### 21.5.2 `property`

**Syntax**

```syntax
property ⟨name⟩ ;
extern property ⟨name⟩ ;
[ extern ] additive property ⟨name⟩ ;
⟨obj⟩.provides( ⟨name⟩ )
```

**Description**

`property` is the type of a free-standing Inform 6 property name: a name in the global property table that is not a member of any Beguile class. Class and object members register as property names automatically, so a `property` declaration is needed only when a name has no class to live on (typical for I6 interop) but must still be usable with `obj.provides(name)` or as a `property`-typed value.

A `property` value is the property identifier, a word-sized value. It supports `=`, `==` and `!=` only; arithmetic on a property identifier is a compile-time error.

The core declares one property itself: `name`, which is `additive` in the Inform 6 compiler. A program must not redeclare it. Every other additive property belongs to a library and is declared by its binding (§23.3.5).

**Example**

```bgl
extern property door_to;      // defined by an I6 library
property visited;             // no Beguile class owns it

object cellar { }

void Main() {
    if (cellar.provides(door_to)) print("leads somewhere");
    property p = visited;     // a property-typed value
    if (p == visited) print("same identifier");
}
```

**See also** §11.7, §23.3.5.

### 21.5.3 `dictionaryWord`

**Syntax**

```syntax
dictionaryWord ⟨name⟩ = .⟨word⟩ ;
dictionaryWord ⟨name⟩ = ..⟨word⟩ ;
```

**Description**

`dictionaryWord` is the type of an Inform 6 dictionary word. Literals are written `.word` (singular) or `..word` (plural); the literal syntax is in §1.6.7 and the use of dictionary words in grammar is in §13.4.2.

`print()` on a dictionary word prints the word's text. This applies to a literal, a `dictionaryWord` variable, and an element read from a `rawArray<dictionaryWord>` member alike. Dictionary words compare with `==` and `!=`.

**Example**

```bgl
object sword { rawArray<dictionaryWord> name = { .blade, .sword }; }

void Main() {
    print(sword.name[1]);      // → sword
}
```

**See also** §1.6.7, §13.1.

### 21.5.4 `verb`

`verb` is the core class from which verbs are declared; its members and the whole verb model are specified in §13.2.

### 21.5.5 Grammar Types

| Type | Purpose |
|---|---|
| `patternElement` | The base type of one element of a grammar pattern. A pattern element is a dictionary word, a grammar token, an attribute, or a parser-hook function. |
| `grammarToken` | An `extern enum` of the parser's token names (`noun`, `held`, `creature`, …). It is declared by the library binding, not by the core (§23.3.6). |
| `grammarRule` | One verb-targeted pattern with a priority: `{verb, {pattern}[, priority]}`. |
| `grammarRuleList` | A list of grammar rules; the type of a verb's `grammar` member and of a grammar object. |

These types are the receivers of the grammar operators (`=`, `+=`, `-=`, `replace … =`). Programs rarely name them; the grammar declaration syntax and its rules are in §13.4.

**Example**

```bgl
#include <i6StandardLibrary>

grammar extraLines {
    grammarRule hang = { PutOn, {.hang, held, .on, noun} };   // a verb and a pattern of pattern elements
}
```

### 21.5.6 `bglClass`

**Syntax**

```syntax
⟨obj⟩.is( ⟨class⟩ )
```

**Description**

`bglClass` is the parameter type of `object.is()`, the runtime class test (true when `obj` is an instance of the class or of any subclass). Every registered class — declared with `class Name {…}` or `extern class Name : object {…}` — is type-compatible with `bglClass`, so any class name is accepted as the argument. In default mode the class must be declared; in loose identifier mode (§15.3.3) the name passes through unchecked.

**Example**

```bgl
class Container : object { }
class Box : Container { int weight; }
Box crate;

void Main() {
    if (crate.is(Container)) print("a container");   // true: Box inherits Container
}
```

**See also** §8.6, §15.3.3.

### 21.5.7 `parentProp` and `childrenProp`

**Syntax**

```syntax
⟨obj⟩.parent = ⟨newParent⟩ ;
⟨obj⟩.parent
for ( object ⟨name⟩ in ⟨obj⟩.children ) ⟨statement⟩
⟨obj⟩.children.length()
⟨obj⟩.children.size()
⟨obj⟩.children += { ⟨obj⟩ , … } ;
```

**Description**

`parentProp` is the type of the `parent` member that every `object` has. Assigning to `obj.parent` *moves* the object in the world tree; reading it yields the parent object; `==` and `!=` compare against an object. The member is `typesealed`: an object body may re-initialize `parent` but not change its type (§8.2.8).

`childrenProp` is the type of the `children` member: the collection of an object's direct children. It is iterable with `for … in`, reports its count with `length()` or `size()` (synonyms here: a world-tree collection has no capacity), is populated in an object body with `children = { … }`, and grows at runtime with `+=`. It is a storageless member: it has no slot of its own and reads the world tree through its owner. The placement rules are in §11.5.

**Example**

```bgl
object cave { }
object lamp { parent = cave; }

void Main() {
    lamp.parent = player;                    // move lamp to player
    int n = cave.children.length();          // 0
}
```

**See also** §11.5.

### 21.5.8 `_bglObject`

**Syntax**

```syntax
class ⟨name⟩ : _bglObject { … }
```

**Description**

`_bglObject` is the root base class of the runtime: an empty `emitter class` from which `object`, the primitive wrappers (`int`, `char`, `string`, …), the IF-domain types above and the runtime's own namespace objects derive.

Deriving from `_bglObject` gives a class with stored members **reference semantics**: locals and members of the type hold an identity, not a copy. `object` adds world-tree citizenship (`parent`, `children`, attributes) on top of that. A class with no base is a value class and is copied on assignment. The veneer classes `int`, `bool`, `char` and `string` (§8.2.5) also derive from `_bglObject` but have no stored members, so there is nothing to share and they behave as values. `_bglObject` is never inherited implicitly; a program names it as a base only to obtain reference semantics without the world tree — the window types of `<glulxWindow>` are an example (§22.7.1).

`print(x)` dispatches on `_bglObject` as described in §21.4.

**See also** §2.10, §8.2.

### 21.5.9 `eType` and `typeof()`

**Syntax**

```syntax
typeof( ⟨expr⟩ )
```

**Description**

`typeof(v)` returns the machine category of a value as an `eType`: `unknown` (`0`: `null`/`nothing`, or undeterminable), `int` (any scalar word — `bool`, `char` and enum values report as `int`), `string`, `routine`, `object`, `class`. It is the discriminator for union-typed values; the type semantics, casts and limits are in §2.8.1.

**Example**

```bgl
object lamp { }

void Main() {
    var x = lamp;
    if (typeof(x) == eType.object) print("an object");   // → an object
    bool b = typeof(true) == eType.int;                  // → true: bool reports as int
}
```

**See also** §2.8.

### 21.5.10 `stringOrRoutine`

**Syntax**

```syntax
stringOrRoutine ⟨name⟩ ;
⟨value⟩.isRoutine()
print( ⟨value⟩ ) ;
```

**Description**

`stringOrRoutine` is the named union `string | func<void>`: a value that is either printable text or a routine to run, the classic Inform "string-or-routine" property (`description`, `cant_go`, …). It is provided by the IF library bindings, not by the core: it is in scope whenever a binding is included (§23.3.7). `isRoutine()` reports which the value currently holds. `print(x)` prints the string or runs the routine; the overload carries no library dependency.

**Example**

```bgl
#include <i6StandardLibrary>
void describeLamp() { print("It flickers."); }
object lamp { stringOrRoutine description; }

void Main() {
    lamp.description = "a brass lamp";   // prints the text
    lamp.description = describeLamp;     // runs the routine
    print(lamp.description);
}
```

**See also** §2.8.2, §23.3.7.

## 21.6 Numeric Utilities

### 21.6.1 `uint`

**Syntax**

```syntax
uint ⟨name⟩ [ = ⟨expr⟩ ] ;
(uint) ⟨expr⟩
(int) ⟨expr⟩
```

**Description**

`uint` is an unsigned integer with the same bit pattern and width as `int` (16 bits on the Z-machine, 32 on Glulx). There is no implicit conversion between `int` and `uint` in either direction; a non-negative integer literal converts implicitly, a negative literal only with an explicit `(uint)` cast, so `uint x = -1;` is a compile-time error.

| Operators | Behavior |
|---|---|
| `+` `-` `*` `++` `--` `+=` `-=` `*=` | As for `int`; overflow wraps. |
| `/` `%` `/=` `%=` | Unsigned division and modulo. |
| `==` `!=` | As for `int`. |
| `<` `<=` `>` `>=` | Unsigned ordering: a value with the high bit set is large, not negative. |
| `&` `\|` `^` `&=` `\|=` `^=` `<<` `<<=` | As for `int`. |
| `>>` `>>=` | Logical (zero-fill) right shift. |
| unary `-` | Not defined on `uint`. |

`print(uint)` prints the full unsigned value: `print((uint)-1)` prints `65535` on the Z-machine and `4294967295` on Glulx.

**Example**

```bgl
uint x = 12;          // literal
uint y = (uint)n;     // explicit cast from int
int  z = (int)y;      // explicit cast back
uint w = (uint)-1;    // a negative literal requires the cast
```

**See also** §2.2, §2.4.1.

### 21.6.2 `bgl.util.math`

**Description**

`bgl.util.math` holds the integer helpers that have no operator form.

| Function | Returns | Description |
|---|---|---|
| `bgl.util.math.abs(x)` | `int` | Absolute value. |
| `bgl.util.math.pow(base, exp)` | `int` | Integer exponentiation; a negative `exp` divides repeatedly. |
| `bgl.util.math.shiftLeft(x, n)` | `int` | Shift left by `n` bits. |
| `bgl.util.math.shiftRight(x, n)` | `int` | Arithmetic shift right by `n` bits. |
| `bgl.util.math.min(a, b)` / `max(a, b)` | `int` | Smaller / larger of two values. |
| `bgl.util.math.clamp(v, lo, hi)` | `int` | `v` limited to the range `lo..hi`. |
| `bgl.util.math.sign(v)` | `int` | `-1`, `0` or `1`. |
| `bgl.util.math.unsignedCompare(a, b)` | `int` | `-1`, `0` or `1` comparing `a` and `b` as unsigned values. |
| `bgl.util.math.unsignedDiv(a, b)` / `unsignedMod(a, b)` | `int` | Unsigned division / modulo; `b == 0` yields `0`. |

The unsigned helpers are what the `uint` operators use; calling them directly is only needed for unsigned arithmetic on plain `int` values.

**Example**

```bgl
int p = bgl.util.math.pow(2, 8);              // → 256
int c = bgl.util.math.clamp(120, 0, 100);     // → 100
int s = bgl.util.math.sign(-7);               // → -1
```

### 21.6.3 `bgl.util.random`

**Description**

`bgl.util.random` is the runtime's random-number source: a uniform integer in a range, a uniform choice from a list, or a seed for a reproducible sequence.

| Function | Returns | Description |
|---|---|---|
| `bgl.util.random.get(n)` | `int` | A uniformly random value in `1..n`. |
| `bgl.util.random.get(a, b[, c …])` | `var` | One of the listed values, chosen uniformly. Two to eight values may be listed. |
| `bgl.util.random.seed(s)` | `void` | Seed the generator: `s == 0` re-seeds from the environment; `s > 0` selects a deterministic sequence. |

**Example**

```bgl
int d6 = bgl.util.random.get(6);
object prize = bgl.util.random.get(coin, gem, key);
```

**Notes**

> **[Z-machine/Glulx difference]** Both targets accept the same `seed(s)` calls; the underlying mechanism differs but the contract above holds on both.

## 21.7 Character Utilities

**Description**

The `char` type carries classification, case conversion and case-insensitive comparison in the core. All handle the ZSCII extended characters (accented letters, ligatures).

| Member | Returns | Description |
|---|---|---|
| `c.isLower()` / `c.isUpper()` | `bool` | Lowercase / uppercase letter, including accented letters. |
| `c.isAlpha()` | `bool` | Any letter. |
| `c.isNumeric()` | `bool` | A digit `0`..`9`. |
| `c.isAlphaNumeric()` | `bool` | A letter or digit. |
| `c.isVowel()` / `c.isConsonant()` | `bool` | Vowel (including accented vowels) / consonant. |
| `c.toUpper()` / `c.toLower()` | `char` | Case conversion; a character with no case maps to itself. |
| `c =~ d` | `bool` | Case-insensitive equality: `'A' =~ 'a'` is true. |

The `char` comparison and arithmetic operators are in §4.5 and §4.6.

**Example**

```bgl
char c = 'a';
bool v = c.isVowel();        // → true
char u = c.toUpper();        // → 'A'
bool same = c =~ 'A';        // → true
```

## 21.8 `bglAllocated`

**Syntax**

```syntax
class ⟨name⟩ [ ⟨n⟩ ] : object, bglAllocated { … }
⟨instance⟩.copy( ⟨other⟩ ) ;
⟨instance⟩.remaining()
```

The `[ ⟨n⟩ ]` brackets are literal: the pool size of §8.2.6.

**Description**

`bglAllocated` is a mixin for pooled classes (§8.2.6). A pooled class that inherits it gains two operations:

| Method | Returns | Description |
|---|---|---|
| `instance.copy(other)` | `void` | Copies every property of `other` into `instance`. Both must be live pool instances of the same or a compatible pooled class. |
| `instance.remaining()` | `int` | The number of free slots remaining in the class's pool. |

Copying is explicit; assigning one pooled instance to another does not copy.

**Example**

```bgl
class marble[10] : object, bglAllocated { int weight = 0; }

void Main() {
    marble a = new marble();
    marble b = new marble();
    b.copy(a);
    int free = a.remaining();     // 8
}
```

**See also** §4.13, §5.15, §8.2.6.

## 21.9 `bgl.world`

**Syntax**

```syntax
bgl.world.getAll( [ ⟨pred⟩ ] )
bgl.world.inParent( ⟨parent⟩ [ , ⟨pred⟩ ] )
bgl.world.instances( ⟨class⟩ [ , ⟨pred⟩ ] )
```

**Description**

`bgl.world` queries the object tree and returns the matching objects as a tracked `array<object>`, so the `<array>` operations (and `<linq>` chains, when `<linq>` is included) apply to the result.

| Method | Returns |
|---|---|
| `bgl.world.getAll()` | Every object in the program. |
| `bgl.world.inParent(parent)` | The direct children of `parent`. |
| `bgl.world.instances(cls)` | Every object of class `cls` or a subclass. |

Each method takes an optional predicate `func<bool, object>`; only objects for which it returns true are returned. The predicate runs inside the walk, so a selective predicate is preferable to filtering the full result afterwards.

Results live in a shared rotating set of four scratch buffers of 128 objects each; the buffer size is fixed and is not a setting. A result is valid until the fourth subsequent query; nested queries inside a `for (o in bgl.world.…)` loop are therefore safe to a depth of three. A result must not be stored across turns; a result that is needed later is copied into a program-declared `array<object>` (assignment copies, §22.4). A walk that would exceed 128 objects stops silently at 128.

**Example**

```bgl
extern attribute light;
class Treasure : object { }

void Main() {
    for (object o in bgl.world.instances(Treasure)) print(o);
    for (object o in bgl.world.inParent(location, (object v) => v.has(light))) print(o);
}
```

**See also** §4.14, §22.4, §22.5.

## 21.10 `bgl.ui`

**Syntax**

```syntax
bgl.ui.mainWin.id
bgl.ui.statusBar.id
bgl.ui.statusBar.height [ = ⟨lines⟩ ]
```

**Description**

The core defines the two root windows of the display as objects under `bgl.ui`. Their members are the same on both targets; an IF library binding replaces the members that the library itself controls (§23.3.8), and `<glulxWindow>` adds the window API to the same objects (§22.7.2).

| Member | Type | Description |
|---|---|---|
| `bgl.ui.mainWin.id` | `int` | The main window handle. `0` until a binding or the program assigns it. |
| `bgl.ui.statusBar.id` | `int` | The status window handle. `0` until assigned; `null` under a binding on the Z-machine, which has no window handles. |
| `bgl.ui.statusBar.height` | `int` | The status window's height in lines. Writing it splits the status window to that height. |

**Example**

```bgl
bgl.ui.statusBar.height = 2;
```

> **[Glulx]** Without a binding, writing `statusBar.height` opens a text-grid window above the root window on first write and re-arranges it thereafter.

> **[Z-machine]** Without a binding, writing `statusBar.height` splits the upper window to that height.

**See also** §22.6, §22.7, §23.3.8.

## 21.11 `bgl.printRules`

**Syntax**

```syntax
$"… {bgl.printRules.⟨rule⟩} …"
$"… {bgl.printRules.img( ⟨image⟩ [ , ⟨align⟩ [ , ⟨width⟩ [ , ⟨height⟩ ] ] ] )} …"
```

**Description**

`bgl.printRules` holds print rules for use inside interpolated strings (§1.6.5): each is a value-less emitter that switches the output style at that point in the text. With `#using bgl.printRules;` the rules are reachable bare.

| Rule | Effect |
|---|---|
| `bold` | Bold text. |
| `italics` | Italic text (rendered as underline where the target has no italics). |
| `underline` | Underlined text. |
| `reverse` | Reverse video. |
| `fixed` | Fixed-pitch text. |
| `roman` | Return to plain text. |
| `img(image[, align[, width[, height]]])` | Draw an image inline in the main window. **[Glulx]**, and only when `generateBlorb` is true (§17.6). `image` is an `eImages` value; `align` is an `eGlulxImageAlign` (default `inlineCenter`); a `0` dimension is computed from the other, preserving aspect ratio, and `0, 0` draws at natural size. |

**Example**

```bgl
print($"The troll {bgl.printRules.italics}hit{bgl.printRules.roman} the table.");
#using bgl.printRules;
print($"{bold}Warning{roman}");
```

**See also** §1.6.5, §17.6, §22.8.

## 21.12 Utility Types

**Description**

**`bglSize`.** A value class with `int width` and `int height` members and a copying `operator =`. It is the return type of the image-metadata calls in `<glulxImage>` (§22.8) and the argument type of the runtime's scaling helpers.

**Blorb asset enums.** The core declares three enums, empty unless blorb packaging is enabled, and one union over them:

| Type | Contents |
|---|---|
| `eImages` | Picture resource ids. |
| `eSounds` | Sound resource ids. |
| `eUnknownAsset` | Data resource ids: any packaged file that is neither an image nor a sound. |
| `eAssets` | The union `eImages \| eSounds \| eUnknownAsset`, for APIs that accept any resource. |

When `generateBlorb` is true the compiler extends `eImages` and `eSounds` with one member per asset file found (§17.6.1). A value of `eImages` is the raw resource id; with `<glulxImage>` it also answers `width()`, `height()` and `size()` (§22.8).

**See also** §2.7, §2.8, §17.6.1, §22.8.

## 21.13 `bgl.asm`

**Description**

`bgl.asm` is the namespace of direct opcode emitters for the active target. It is an alias
(§10.2) for a target-specific class: on Glulx the Glulx opcode class, on the Z-machine the
Z-machine opcode class; each is available only on its own target. Members are either emitters,
which inline the opcode at the call site, or `static superposed` functions (§3.12), which wrap a
value-returning opcode in a free routine that exists only in programs that call it. Kind **E** below
is an emitter; **S** is a `static superposed` function.

**Example**

```bgl
#using bgl;
int n = asm.random(6);        // Glulx: @random; Z-machine: @random
asm.streamchar('!');          // Glulx only
```

### 21.13.1 Glulx Opcodes

| Member | Signature | Kind | Meaning |
|---|---|---|---|
| `add` / `sub` / `mul` / `div` / `mod` | `int ⟨op⟩(int a, int b)` | E | Arithmetic |
| `neg` | `int neg(int a)` | E | `-a` |
| `bitand` / `bitor` | `int ⟨op⟩(int a, int b)` | E | Bitwise and, or |
| `bitxor` | `int bitxor(int a, int b)` | S | `@bitxor` |
| `bitnot` | `int bitnot(int a)` | E | `~a` |
| `shiftl` | `int shiftl(int a, int b)` | S | Logical shift left |
| `sshiftr` | `int sshiftr(int a, int b)` | S | Arithmetic shift right |
| `ushiftr` | `int ushiftr(int a, int b)` | S | Logical shift right |
| `aload` / `aloads` / `aloadb` / `aloadbit` | `int ⟨op⟩(int addr, int idx)` | S | Load word, short, byte, bit |
| `astore` / `astores` / `astoreb` / `astorebit` | `void ⟨op⟩(int addr, int idx, int v)` | E | Store word, short, byte, bit |
| `copy` | `int copy(int v)` | E | `@copy` |
| `sexs` / `sexb` | `int ⟨op⟩(int v)` | S | Sign-extend short, byte |
| `stkcount` | `int stkcount()` | S | `@stkcount` |
| `stkpeek` | `int stkpeek(int idx)` | S | `@stkpeek` |
| `stkswap` | `void stkswap()` | E | `@stkswap` |
| `stkroll` | `void stkroll(int count, int dir)` | E | `@stkroll` |
| `stkcopy` | `void stkcopy(int count)` | E | `@stkcopy` |
| `callf` / `callfi` / `callfii` / `callfiii` | `int ⟨op⟩(int addr, …)` | S | Call with 0–3 arguments |
| `streamchar` / `streamnum` / `streamstr` / `streamunichar` | `void ⟨op⟩(int v)` | E | Stream output |
| `getstringtbl` | `int getstringtbl()` | S | `@getstringtbl` |
| `setstringtbl` | `void setstringtbl(int addr)` | E | `@setstringtbl` |
| `getmemsize` | `int getmemsize()` | S | `@getmemsize` |
| `setmemsize` | `int setmemsize(int size)` | S | `@setmemsize` |
| `malloc` | `int malloc(int size)` | S | Heap allocate |
| `mfree` | `void mfree(int addr)` | E | `@mfree` |
| `mzero` | `void mzero(int count, int addr)` | E | `@mzero` |
| `mcopy` | `void mcopy(int count, int src, int dst)` | E | `@mcopy` |
| `linearsearch` / `binarysearch` | `int ⟨op⟩(int key, int keySize, int start, int structSize, int numStructs, int keyOffset, int options)` | S | Table search |
| `linkedsearch` | `int linkedsearch(int key, int keySize, int start, int keyOffset, int nextOffset, int options)` | S | `@linkedsearch` |
| `random` | `int random(int range)` | S | `@random` |
| `setrandom` | `void setrandom(int seed)` | E | `@setrandom` |
| `quit` / `restart` | `void ⟨op⟩()` | E | `@quit`, `@restart` |
| `save` / `restore` | `int ⟨op⟩(int stream)` | S | `@save`, `@restore` |
| `saveundo` / `restoreundo` | `int ⟨op⟩()` | S | `@saveundo`, `@restoreundo` |
| `protect` | `void protect(int addr, int len)` | E | `@protect` |
| `verify` | `int verify()` | S | `@verify` |
| `debugtrap` | `void debugtrap(int val)` | E | `@debugtrap` |
| `glk` | `int glk(int sel, int argc)` | E | Raw Glk dispatch |
| `setiosys` | `void setiosys(int mode, int rock)` | E | `@setiosys` |
| `numtof` | `int numtof(int n)` | S | Integer to float bits |
| `ftonumz` / `ftonumn` | `int ⟨op⟩(int f)` | S | Float to integer, truncating / rounding |
| `fadd` / `fsub` / `fmul` / `fdiv` | `int ⟨op⟩(int a, int b)` | S | Float arithmetic |
| `ceil` / `floor` / `sqrt` / `exp` / `log` | `int ⟨op⟩(int f)` | S | Float unary functions |
| `pow` | `int pow(int a, int b)` | S | `@pow` |
| `sin` / `cos` / `tan` / `asin` / `acos` / `atan` | `int ⟨op⟩(int f)` | S | Float trigonometry |
| `atan2` | `int atan2(int a, int b)` | S | `@atan2` |
| `accelfunc` | `void accelfunc(int idx, int addr)` | E | `@accelfunc` |
| `accelparam` | `void accelparam(int idx, int val)` | E | `@accelparam` |

### 21.13.2 Glk Calls

Each of these is an emitter that performs the named Glk call; the member name is the Glk function
name in camel case without the `glk_` prefix (`windowOpen` is `glk_window_open`). Parameters are
`int` and follow the Glk argument order unless noted.

| Member | Signature | Glk call |
|---|---|---|
| `exit` | `void exit()` | `glk_exit` |
| `tick` | `void tick()` | `glk_tick` |
| `gestalt` | `int gestalt(int a, int b)` | `glk_gestalt` |
| `gestaltExt` | `int gestaltExt(int a, int b, int c, int d)` | `glk_gestalt_ext` |
| `windowIterate` | `int windowIterate(int a, int b)` | `glk_window_iterate` |
| `windowGetRock` | `int windowGetRock(int a)` | `glk_window_get_rock` |
| `windowGetRoot` | `int windowGetRoot()` | `glk_window_get_root` |
| `windowOpen` | `int windowOpen(int splitWinId, eGlulxWindowType type, int size, bGlulxWindowMethodFlags winMethod = noBorder, int winRock = 0)` | `glk_window_open` (arguments reordered to Glk order) |
| `windowClose` | `void windowClose(int a, int b)` | `glk_window_close` |
| `windowGetSize` | `void windowGetSize(int a, int b, int c)` | `glk_window_get_size` |
| `windowSetArrangement` | `void windowSetArrangement(int a, int b, int c, int d)` | `glk_window_set_arrangement` |
| `windowGetArrangement` | `void windowGetArrangement(int a, int b, int c, int d)` | `glk_window_get_arrangement` |
| `windowGetType` | `int windowGetType(int a)` | `glk_window_get_type` |
| `windowGetParent` | `int windowGetParent(int a)` | `glk_window_get_parent` |
| `windowClear` | `void windowClear(int a)` | `glk_window_clear` |
| `windowMoveCursor` | `void windowMoveCursor(int a, int b, int c)` | `glk_window_move_cursor` |
| `windowGetStream` | `int windowGetStream(int a)` | `glk_window_get_stream` |
| `windowSetEchoStream` | `void windowSetEchoStream(int a, int b)` | `glk_window_set_echo_stream` |
| `windowGetEchoStream` | `int windowGetEchoStream(int a)` | `glk_window_get_echo_stream` |
| `setWindow` | `void setWindow(int a)` | `glk_set_window` |
| `windowGetSibling` | `int windowGetSibling(int a)` | `glk_window_get_sibling` |
| `streamIterate` | `int streamIterate(int a, int b)` | `glk_stream_iterate` |
| `streamGetRock` | `int streamGetRock(int a)` | `glk_stream_get_rock` |
| `streamOpenFile` | `int streamOpenFile(int a, int b, int c)` | `glk_stream_open_file` |
| `streamOpenMemory` | `int streamOpenMemory(int a, int b, int c, int d)` | `glk_stream_open_memory` |
| `streamClose` | `void streamClose(int a, int b)` | `glk_stream_close` |
| `streamSetPosition` | `void streamSetPosition(int a, int b, int c)` | `glk_stream_set_position` |
| `streamGetPosition` | `int streamGetPosition(int a)` | `glk_stream_get_position` |
| `streamSetCurrent` | `void streamSetCurrent(int a)` | `glk_stream_set_current` |
| `streamGetCurrent` | `int streamGetCurrent()` | `glk_stream_get_current` |
| `streamOpenResource` | `int streamOpenResource(int a, int b)` | `glk_stream_open_resource` |
| `filerefCreateTemp` | `int filerefCreateTemp(int a, int b)` | `glk_fileref_create_temp` |
| `filerefCreateByName` | `int filerefCreateByName(int a, int b, int c)` | `glk_fileref_create_by_name` |
| `filerefCreateByPrompt` | `int filerefCreateByPrompt(int a, int b, int c)` | `glk_fileref_create_by_prompt` |
| `filerefDestroy` | `void filerefDestroy(int a)` | `glk_fileref_destroy` |
| `filerefIterate` | `int filerefIterate(int a, int b)` | `glk_fileref_iterate` |
| `filerefGetRock` | `int filerefGetRock(int a)` | `glk_fileref_get_rock` |
| `filerefDeleteFile` | `void filerefDeleteFile(int a)` | `glk_fileref_delete_file` |
| `filerefDoesFileExist` | `int filerefDoesFileExist(int a)` | `glk_fileref_does_file_exist` |
| `filerefCreateFromFileref` | `int filerefCreateFromFileref(int a, int b, int c)` | `glk_fileref_create_from_fileref` |
| `putChar` / `putCharStream` | `void putChar(int a)` / `void putCharStream(int a, int b)` | `glk_put_char`, `glk_put_char_stream` |
| `putString` / `putStringStream` | `void putString(int a)` / `void putStringStream(int a, int b)` | `glk_put_string`, `glk_put_string_stream` |
| `putBuffer` / `putBufferStream` | `void putBuffer(int a, int b)` / `void putBufferStream(int a, int b, int c)` | `glk_put_buffer`, `glk_put_buffer_stream` |
| `setStyle` / `setStyleStream` | `void setStyle(int a)` / `void setStyleStream(int a, int b)` | `glk_set_style`, `glk_set_style_stream` |
| `getCharStream` | `int getCharStream(int a)` | `glk_get_char_stream` |
| `getLineStream` | `int getLineStream(int a, int b, int c)` | `glk_get_line_stream` |
| `getBufferStream` | `int getBufferStream(int a, int b, int c)` | `glk_get_buffer_stream` |
| `charToLower` / `charToUpper` | `int ⟨op⟩(int a)` | `glk_char_to_lower`, `glk_char_to_upper` |
| `stylehintSet` | `void stylehintSet(int a, int b, int c, int d)` | `glk_stylehint_set` |
| `stylehintClear` | `void stylehintClear(int a, int b, int c)` | `glk_stylehint_clear` |
| `styleDistinguish` | `int styleDistinguish(int a, int b, int c)` | `glk_style_distinguish` |
| `styleMeasure` | `int styleMeasure(int a, int b, int c, int d)` | `glk_style_measure` |
| `select` / `selectPoll` | `void ⟨op⟩(int a)` | `glk_select`, `glk_select_poll` |
| `requestLineEvent` | `void requestLineEvent(int a, int b, int c, int d)` | `glk_request_line_event` |
| `cancelLineEvent` | `void cancelLineEvent(int a, int b)` | `glk_cancel_line_event` |
| `requestCharEvent` / `cancelCharEvent` | `void ⟨op⟩(int a)` | `glk_request_char_event`, `glk_cancel_char_event` |
| `requestMouseEvent` / `cancelMouseEvent` | `void ⟨op⟩(int a)` | `glk_request_mouse_event`, `glk_cancel_mouse_event` |
| `requestTimerEvents` | `void requestTimerEvents(int a)` | `glk_request_timer_events` |
| `imageGetInfo` | `int imageGetInfo(int a, int b, int c)` | `glk_image_get_info` |
| `imageDraw` | `int imageDraw(int a, int b, int c, int d)` | `glk_image_draw` |
| `imageDrawScaled` | `int imageDrawScaled(int win, var img, var alignOrX, int y, int width, int height)` | `glk_image_draw_scaled` |
| `windowFlowBreak` | `void windowFlowBreak(int a)` | `glk_window_flow_break` |
| `windowEraseRect` | `void windowEraseRect(int a, int b, int c, int d, int e)` | `glk_window_erase_rect` |
| `windowFillRect` | `void windowFillRect(int a, int b, int c, int d, int e, int f)` | `glk_window_fill_rect` |
| `windowSetBackgroundColor` | `void windowSetBackgroundColor(int a, int b)` | `glk_window_set_background_color` |
| `schannelIterate` | `int schannelIterate(int a, int b)` | `glk_schannel_iterate` |
| `schannelGetRock` | `int schannelGetRock(int a)` | `glk_schannel_get_rock` |
| `schannelCreate` / `schannelCreateExt` | `int schannelCreate(int a)` / `int schannelCreateExt(int a, int b)` | `glk_schannel_create`, `glk_schannel_create_ext` |
| `schannelDestroy` | `void schannelDestroy(int a)` | `glk_schannel_destroy` |
| `schannelPlay` / `schannelPlayExt` | `int schannelPlay(int a, int b)` / `int schannelPlayExt(int a, int b, int c, int d)` | `glk_schannel_play`, `glk_schannel_play_ext` |
| `schannelPlayMulti` | `int schannelPlayMulti(int a, int b, int c, int d, int e)` | `glk_schannel_play_multi` |
| `schannelStop` / `schannelPause` / `schannelUnpause` | `void ⟨op⟩(int a)` | `glk_schannel_stop`, `glk_schannel_pause`, `glk_schannel_unpause` |
| `schannelSetVolume` / `schannelSetVolumeExt` | `void schannelSetVolume(int a, int b)` / `void schannelSetVolumeExt(int a, int b, int c, int d)` | `glk_schannel_set_volume`, `glk_schannel_set_volume_ext` |
| `soundLoadHint` | `void soundLoadHint(int a, int b)` | `glk_sound_load_hint` |
| `setHyperlink` / `setHyperlinkStream` | `void setHyperlink(int a)` / `void setHyperlinkStream(int a, int b)` | `glk_set_hyperlink`, `glk_set_hyperlink_stream` |
| `requestHyperlinkEvent` / `cancelHyperlinkEvent` | `void ⟨op⟩(int a)` | `glk_request_hyperlink_event`, `glk_cancel_hyperlink_event` |
| `bufferToLowerCaseUni` / `bufferToUpperCaseUni` | `int ⟨op⟩(int a, int b, int c)` | `glk_buffer_to_lower_case_uni`, `glk_buffer_to_upper_case_uni` |
| `bufferToTitleCaseUni` | `int bufferToTitleCaseUni(int a, int b, int c, int d)` | `glk_buffer_to_title_case_uni` |
| `bufferCanonDecomposeUni` / `bufferCanonNormalizeUni` | `int ⟨op⟩(int a, int b, int c)` | `glk_buffer_canon_decompose_uni`, `glk_buffer_canon_normalize_uni` |
| `putCharUni` / `putStringUni` | `void ⟨op⟩(int a)` | `glk_put_char_uni`, `glk_put_string_uni` |
| `putBufferUni` | `void putBufferUni(int a, int b)` | `glk_put_buffer_uni` |
| `putCharStreamUni` / `putStringStreamUni` | `void ⟨op⟩(int a, int b)` | `glk_put_char_stream_uni`, `glk_put_string_stream_uni` |
| `putBufferStreamUni` | `void putBufferStreamUni(int a, int b, int c)` | `glk_put_buffer_stream_uni` |
| `getCharStreamUni` | `int getCharStreamUni(int a)` | `glk_get_char_stream_uni` |
| `getBufferStreamUni` / `getLineStreamUni` | `int ⟨op⟩(int a, int b, int c)` | `glk_get_buffer_stream_uni`, `glk_get_line_stream_uni` |
| `streamOpenFileUni` | `int streamOpenFileUni(int a, int b, int c)` | `glk_stream_open_file_uni` |
| `streamOpenMemoryUni` | `int streamOpenMemoryUni(int a, int b, int c, int d)` | `glk_stream_open_memory_uni` |
| `streamOpenResourceUni` | `int streamOpenResourceUni(int a, int b)` | `glk_stream_open_resource_uni` |
| `requestCharEventUni` | `void requestCharEventUni(int a)` | `glk_request_char_event_uni` |
| `requestLineEventUni` | `void requestLineEventUni(int a, int b, int c, int d)` | `glk_request_line_event_uni` |
| `setEchoLineEvent` | `void setEchoLineEvent(int a, int b)` | `glk_set_echo_line_event` |
| `setTerminatorsLineEvent` | `void setTerminatorsLineEvent(int a, int b, int c)` | `glk_set_terminators_line_event` |
| `currentTime` | `void currentTime(int a)` | `glk_current_time` |
| `currentSimpleTime` | `int currentSimpleTime(int a)` | `glk_current_simple_time` |
| `timeToDateUtc` / `timeToDateLocal` | `void ⟨op⟩(int a, int b)` | `glk_time_to_date_utc`, `glk_time_to_date_local` |
| `simpleTimeToDateUtc` / `simpleTimeToDateLocal` | `void ⟨op⟩(int a, int b, int c)` | `glk_simple_time_to_date_utc`, `glk_simple_time_to_date_local` |
| `dateToTimeUtc` / `dateToTimeLocal` | `void ⟨op⟩(int a, int b)` | `glk_date_to_time_utc`, `glk_date_to_time_local` |
| `dateToSimpleTimeUtc` / `dateToSimpleTimeLocal` | `int ⟨op⟩(int a, int b)` | `glk_date_to_simple_time_utc`, `glk_date_to_simple_time_local` |
| `garglkSetZcolors` / `garglkSetZcolorsStream` | `void garglkSetZcolors(int a, int b)` / `void garglkSetZcolorsStream(int a, int b, int c)` | `garglk_set_zcolors`, `garglk_set_zcolors_stream` |
| `garglkSetReversevideo` / `garglkSetReversevideoStream` | `void garglkSetReversevideo(int a)` / `void garglkSetReversevideoStream(int a, int b)` | `garglk_set_reversevideo`, `garglk_set_reversevideo_stream` |

### 21.13.3 Z-machine Opcodes

| Member | Signature | Kind | Meaning |
|---|---|---|---|
| `add` / `sub` / `mul` / `div` / `mod` | `int ⟨op⟩(int a, int b)` | E | Arithmetic |
| `bitor` / `bitand` | `int ⟨op⟩(int a, int b)` | E | Bitwise or, and |
| `bitnot` | `int bitnot(int a)` | E | `~a` |
| `loadw` / `loadb` | `int ⟨op⟩(int arr, int idx)` | S | `@loadw`, `@loadb` |
| `storew` / `storeb` | `void ⟨op⟩(int arr, int idx, int v)` | E | `@storew`, `@storeb` |
| `push` | `void push(int v)` | E | `@push` |
| `pull` | `int pull()` | S | `@pull` |
| `getParent` / `getChild` / `getSibling` | `int ⟨op⟩(int obj)` | S | `@get_parent`, `@get_child`, `@get_sibling` |
| `insertObj` | `void insertObj(int obj, int dst)` | E | `@insert_obj` |
| `removeObj` | `void removeObj(int obj)` | E | `@remove_obj` |
| `printObj` | `void printObj(int obj)` | E | `@print_obj` |
| `getProp` / `getPropAddr` / `getNextProp` | `int ⟨op⟩(int obj, int prop)` | S | `@get_prop`, `@get_prop_addr`, `@get_next_prop` |
| `getPropLen` | `int getPropLen(int propAddr)` | S | `@get_prop_len` |
| `putProp` | `void putProp(int obj, int prop, int v)` | E | `@put_prop` |
| `setAttr` / `clearAttr` | `void ⟨op⟩(int obj, int attr)` | E | `@set_attr`, `@clear_attr` |
| `inc` / `dec` | `void ⟨op⟩(int variable)` | E | `@inc`, `@dec` |
| `printChar` / `printNum` / `printAddr` / `printPaddr` | `void ⟨op⟩(int v)` | E | `@print_char`, `@print_num`, `@print_addr`, `@print_paddr` |
| `newLine` | `void newLine()` | E | `@new_line` |
| `splitWindow` | `void splitWindow(int lines)` | E | `@split_window` |
| `setWindow` | `void setWindow(int win)` | E | `@set_window` |
| `outputStream` | `void outputStream(int n)` / `void outputStream(int n, int table)` | E | `@output_stream` |
| `inputStream` | `void inputStream(int n)` | E | `@input_stream` |
| `random` | `int random(int range)` | S | `@random` |
| `quit` / `restart` | `void ⟨op⟩()` | E | `@quit`, `@restart` |
| `verify` | `int verify()` | E | `@verify` |
| `soundEffect` | `void soundEffect(int num, int effect, int vol)` | E | `@sound_effect` |

The following members exist only when `TARGET_ZCODE` is greater than 3 (Appendix F).

| Member | Signature | Kind | Meaning |
|---|---|---|---|
| `eraseWindow` | `void eraseWindow(int win)` | E | `@erase_window` |
| `eraseLine` | `void eraseLine(int v)` | E | `@erase_line` |
| `setCursor` | `void setCursor(int line, int col)` | E | `@set_cursor` |
| `getCursor` | `void getCursor(int arr)` | E | `@get_cursor` |
| `setTextStyle` | `void setTextStyle(int style)` | E | `@set_text_style` |
| `bufferMode` | `void bufferMode(int flag)` | E | `@buffer_mode` |
| `readChar` | `int readChar(int dev = 1)` | S | `@read_char` |
| `scanTable` | `int scanTable(int x, int table, int len)` | S | `@scan_table` |
| `call1s` / `call2s` | `int call1s(int routine)` / `int call2s(int routine, int a)` | S | `@call_1s`, `@call_2s` |
| `logShift` / `artShift` | `int ⟨op⟩(int a, int places)` | S | `@log_shift`, `@art_shift` |
| `copyTable` | `void copyTable(int src, int dst, int size)` | E | `@copy_table` |
| `printUnicode` | `void printUnicode(int ch)` | E | `@print_unicode` |
| `setColour` | `void setColour(int fg, int bg)` | E | `@set_colour` |
| `setFont` | `int setFont(int font)` | S | `@set_font` |
| `tokenise` | `void tokenise(int text, int parse)` | E | `@tokenise` |
| `encodeText` | `void encodeText(int zscii, int len, int from, int coded)` | E | `@encode_text` |
| `printTable` | `void printTable(int zscii, int width)` | E | `@print_table` |
| `save` / `restore` | `int ⟨op⟩(int table, int bytes, int name)` | S | `@save`, `@restore` |
| `saveUndo` / `restoreUndo` | `int ⟨op⟩()` | S | `@save_undo`, `@restore_undo` |
| `call1n` / `call2n` | `void call1n(int routine)` / `void call2n(int routine, int a)` | E | `@call_1n`, `@call_2n` |
| `checkUnicode` | `int checkUnicode(int ch)` | S | `@check_unicode` |

**See also** §3.12; §10.2; §22.7.
