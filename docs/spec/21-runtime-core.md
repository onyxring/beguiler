# 21 Runtime Core

<!-- toc -->
- [21.1 Overview](#211-overview)
- [21.2 `bglInit()`](#212-bglinit)
- [21.3 The `bgl` Namespace](#213-the-bgl-namespace)
- [21.4 `bgl.output`: `print()`, `printLine()`, `log()` and Article Helpers](#214-bgloutput-print-printline-log-and-article-helpers)
- [21.5 IF-Domain Types](#215-if-domain-types)
  - [21.5.1 `attribute` and `attributeList`](#2151-attribute-and-attributelist)
  - [21.5.2 `property`](#2152-property)
  - [21.5.3 `dictionaryWord`](#2153-dictionaryword)
  - [21.5.4 `verb`](#2154-verb)
  - [21.5.5 Grammar Types](#2155-grammar-types)
  - [21.5.6 `bglClass`](#2156-bglclass)
  - [21.5.7 `parentProp` and `childrenProp`](#2157-parentprop-and-childrenprop)
  - [21.5.8 `_bglObject` and `_bglPrimitive`](#2158-bglobject-and-bglprimitive)
  - [21.5.9 `eType` and `typeof()`](#2159-etype-and-typeof)
  - [21.5.10 `iString`, `iStringOrObject` and `iStringOrRoutine`](#21510-istring-istringorobject-and-istringorroutine)
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
- [21.14 `bgl.header`](#2114-bglheader)
- [21.15 `bgl.gc`](#2115-bglgc)
<!-- /toc -->

## 21.1 Overview

The Beguile Language Runtime (BLR) is the library of Beguile source every program compiles against.  Its *core*, the subject of this chapter, is loaded automatically, with no `#include`, in both default and precompiler mode (§16.1).  Language extensions, each enabled with `#include <…>`, are covered in §22;  IF library bindings are covered in §23.

The core provides:

- the `bgl` namespace and its target-specific branches (§21.3);
- the output routines in `bgl.output` and the article helpers (§21.4);
- the IF-domain types:  `attribute`, `property`, `dictionaryWord`, `verb`, the grammar types, `bglClass`, `parentProp`, `childrenProp`, `_bglObject`, `_bglPrimitive`, `eType` (§21.5);
- the numeric, character, and allocation utilities:  `uint`, `bgl.util.math`, `bgl.util.random`, the `char` methods, `bglAllocated` (§21.6 to §21.8);
- object-tree queries, `bgl.world` (§21.9);
- the user-interface roots `bgl.ui.mainWin` / `bgl.ui.statusBar` and the print rules (§21.10 to §21.11);
- the utility types `bglSize` and the blorb asset enums (§21.12).

The built-in types (`int`, `uint`, `bool`, `char`, `float`, `string`, §2.2), the rest of the primitive types (§2.10.3), the `var` type (§2.6), and their literals are also core, as is the `object` class (§11.1).

In precompiler mode the `bgl` namespace is not imported:  a `#bgl` island must declare `#using bgl;` before using `bgl.…` (§16.1.2).

## 21.2 `bglInit()`

**Syntax**

```syntax
bglInit();
```

The compiler synthesizes `bglInit()` for every program (§18.8).  It will run at most once, regardless of how often it is called, working through the following in order:

1. the length headers of sized byte arrays (§12.4, §22.2);
2. every `#startup` block in the program, in file-inclusion order (§14.4.1);
3. the deferred initializers of globals, in declaration order (§3.6):  for a class-typed global whose type declares a parameterless `init` (§8.5), the `init` body and then the `operator =` which applies a declared value; for any global whose initializer is not a constant expression, that initializer.

With an IF library binding, you don't call `bglInit()` yourself:  the binding runs it first (§23.3.1).  A program built without a binding, or with `autoInitialize = false` (§15.4), must call `bglInit()` from its entry point before using anything which depends on it.  The call is always available, and harmless when nothing has registered work.

Leaving the call out stops neither the build nor the program, which simply runs on uninitialized data; the compiler will warn you when the program has initialization to do and nothing in the transpiled file calls `bglInit()` (§19.3).

An extension which needs `bglInit()` says so in its entry in §22.

**Example**

```bgl
void Main() {
    bglInit();
    // …
}
```

**See also** §14.4.1, §16.1.2, §18.8, §23.3.1.

## 21.3 The `bgl` Namespace

`bgl` is the root namespace object of the runtime.  Its members are reached by dotted path (`bgl.asm.add(a, b)`, `bgl.util.math.pow(2, 8)`); alternatively, `#using bgl.glulx;` and the other `#using` forms import a branch so its members can be reached bare (§10.4).

| Path | Contents | Target |
|---|---|---|
| `bgl.wordsize` | The word size of the active target:  `2` on the Z-machine, `4` on Glulx. | both |
| `bgl.asm.*` | Direct opcode emitters for the active virtual machine; the set of members differs per target (§21.13). | both |
| `bgl.glulx.*` | Glulx-specific names:  the short aliases `eStyleType`, `eWinType`, `eImgAlign`, `eImgDimension`, `bWinBorder`, `bWinPlacement`, and `bWinScale`, for the core enums `eGlulxStyleType`, `eGlulxWindowType`, `eGlulxImageAlign`, `eGlulxImageDimension`, `bGlulxWindowBorder`, `bGlulxWindowPlacement`, and `bGlulxWindowScale` (§22.7.10); the window types and `color` when `<glulxWindow>` is included (§22.7). | Glulx |
| `bgl.zcode.*` | Z-machine-specific names.  Currently empty. | Z-machine |
| `bgl.util.*` | Utilities:  `math` (§21.6.2), `random` (§21.6.3), and `buf` when `<buf>` is included (§22.2). | both |
| `bgl.world.*` | Object-tree queries (§21.9). | both |
| `bgl.ui.*` | `mainWin`, `statusBar` (§21.10); `screen` with `<glulxWindow>` (§22.7.2); `hideCursor`, `showCursor`, `waitForKey` with `<ui>` (§22.6). | both |
| `bgl.printRules.*` | Text-style print rules for interpolated strings (§21.11). | both |
| `bgl.header.*` | The release number and serial code from the story file header (§21.14). | both |

Only the branch for the active target is loaded, so referencing a member of the other target's branch is a compile-time error.

**Example**

```bgl
int bits = bgl.wordsize * 8;             // 16 on Z-machine, 32 on Glulx
int roll = bgl.util.random.get(6);       // 1..6
```

**See also** §10, §10.4, §22.

## 21.4 `bgl.output`: `print()`, `printLine()`, `log()` and Article Helpers

**Syntax**

```syntax
print( VALUE [ , VALUE … ] ) ;
print( $"TEXT {EXPR} TEXT" ) ;
printLine( [ VALUE ] ) ;
bgl.output.spaces( N ) ;
bgl.output.log( VALUE ) ;
printName( OBJ ) ;
bgl.printRules.a( OBJ ) ;   bgl.printRules.cA( OBJ ) ;
bgl.printRules.the( OBJ ) ; bgl.printRules.cThe( OBJ ) ;
```

The output routines are emitters on `bgl.output`.  `print`, `printLine`, and `printName` are also global names, through aliases (§10.5), so they need no path; `spaces` and `log` do.

The global `print` is a distributor (§6.8):  given several arguments, it prints each in turn, using the overload for that argument's type.

`print()` writes a value to the current output stream immediately, and is overloaded on the argument's type, with an overload for every primitive type and the IF-domain types.  Extensions and bindings add or replace overloads for the types they introduce, using `extend _bglOutput`; your program can also declare a global `print(T)` of its own, which joins the same overload set.  An interpolated string (§1.6.5) prints each segment with the overload for that segment's type.

Given a value whose type derives from `_bglObject` (§21.5.8), `print(obj)` calls the value's own `print()` method if it defines one; otherwise, it prints the object's name with I6's `(name)` rule.

The name `(name)` prints is the object's `instanceName` (§11.5.4).  Both library bindings declare `short_name` on `object` (§23.3.11):  the text (or routine) which the library prints in its place when the object has one, and what the article helpers and `printName()` print.

`printLine(v)` prints `v` exactly as `print(v)` would, and then a newline; `printLine()` prints just the newline.  Unlike `print`, `printLine` is not a distributor (§6.8):  it takes one argument.  Note that it is not I6's `print_ret`:  it does not return from the enclosing function.

`bgl.output.spaces(n)` prints `n` spaces, or nothing when `n` is 0 or less.

`bgl.output.log()` accepts the same arguments as `print()`; however, it produces output only when the symbol `DEBUG` is defined:  in a debug build (§20.3), or when the program turns it on with `#redef DEBUG` (§14.2.2).  Its arguments are parsed and type-checked in every build.

The article helpers `a`, `cA`, `the`, and `cThe` print a world-tree object with an article.  They are members of `bgl.printRules` (§21.11), so you either write them qualified or bring in the short spelling with `#using bgl.printRules;` (§10.4).  `printName(obj)` gives "lamp", the bare short name with no article.

> ***Note:** An Inform 6 program can freely name a variable or object `the`, and a log is a likely object in a game, so these names stay out of file scope.*

**Example**

```bgl
object lamp { short_name = "brass lamp"; }

void Main() {
    print($"You see {bgl.printRules.a(lamp)}.");   // -> You see a brass lamp.
    bgl.printRules.cThe(lamp); print(" glows.");   // -> The brass lamp glows.
    printLine($"Weight: {2}");                     // -> Weight: 2, then a newline
    bgl.output.log("reached Main");                // output only in a debug build
}
```

> ***Note:** The article forms depend on the active IF library's article and naming conventions (e.g., the `proper` attribute).*

**See also** §1.6.5, §14.2.1, §22.3.

## 21.5 IF-Domain Types

### 21.5.1 `attribute` and `attributeList`

**Syntax**

```syntax
attribute NAME ;
extern attribute NAME ;
attributeList attributes = { ATTR [ , !ATTR ] … } ;
OBJ.give( ATTR )   OBJ.ungive( ATTR )   OBJ.has( ATTR )   OBJ.hasnt( ATTR )
```

The `attributeList` form appears in a class or object body; the `!` is literal.

`attribute` is the type of a single Inform 6 attribute (`light`, `container`, `static`, …).  A library binding usually binds them with `extern attribute` (§23.3.4); however, a program can also declare its own (§11.6).

`attributeList` is the type of a class's or object's `attributes` member:  the set of attributes the object initially has, written as an initializer list; its declaration rules are in §11.5.3.

At runtime, you change an object's attributes with `give()` and `ungive()` and test them with `has()` and `hasnt()`; all four are defined on `object` and on `attributeList`.

`NO_ATTRIBUTE` is an `attribute`-typed constant meaning "no attribute".  It is the only value, other than a real attribute, that can be assigned to an attribute-valued member; a bare `0` is a type error.

**Example**

```bgl
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
property NAME ;
extern property NAME ;
[ extern ] additive property NAME ;
OBJ.provides( NAME )
```

`property` is the type of a free-standing Inform 6 property name:  a name in the global property table which is not a member of any Beguile class.  When a name needs a `property` declaration is covered in §11.7.1.

A `property` value is the property identifier, one word in size.  It supports `=`, `==`, and `!=` only; arithmetic on a property identifier is a compile-time error.

The built-in `name` property and the other additive properties are covered in §11.7.2.

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
dictionaryWord NAME = .WORD ;
dictionaryWord NAME = ..WORD ;
```

`dictionaryWord` is the type of an Inform 6 dictionary word.  Literals are written `.word` (singular) or `..word` (plural); the literal syntax is covered in §1.6.7, and the use of dictionary words in grammar in §13.4.2.

`print()` on a dictionary word prints the word's text.  Dictionary words compare with `==` and `!=`.

**Example**

```bgl
object sword { rawArray<dictionaryWord> name = { .blade, .sword }; }

void Main() {
    print(sword.name[1]);      // -> sword
}
```

**See also** §1.6.7, §13.1.

### 21.5.4 `verb`

`verb` is the class from which verbs are declared; its members and the whole verb model are covered in §13.2.

### 21.5.5 Grammar Types

The types which stand behind a grammar declaration, `patternElement`, `grammarToken`, `grammarRule`, and `grammarRuleList`, are covered in §13.4.1.  You will rarely name them; the grammar declaration syntax and its rules are in §13.4.

**Example**

```bgl
#include <bindings/i6StandardLibrary>

grammar extraLines {
    grammarRule hang = { PutOn, {.hang, HELD, .on, NOUN} };   // a verb and a pattern of pattern elements
}
```

### 21.5.6 `bglClass`

**Syntax**

```syntax
OBJ.is( CLASS )
```

`bglClass` is the parameter type of `is()` on `_bglObject` (§21.5.8), the runtime class test (true when `obj` is an instance of the class or of any subclass).  It is a primitive class:  a class name's value is the number I6 assigns the class.  Every registered class, whether declared with `class Name {…}` or `extern class Name : object {…}`, is type-compatible with `bglClass`.  Two class values compare with `==` and `!=`, and a class name can stand on the right (`k == Box`).  In default mode the class must be declared; in loose identifier mode (§16.3.3) the name passes through unchecked.

**Example**

```bgl
class Container : object { }
class Box : Container { int weight; }
Box crate { }

void Main() {
    if (crate.is(Container)) print("a container");   // true: Box inherits Container
}
```

**See also** §8.6, §16.3.3.

### 21.5.7 `parentProp` and `childrenProp`

**Syntax**

```syntax
OBJ.parent = NEW-PARENT ;
OBJ.parent
for ( object NAME in OBJ.children ) STATEMENT
OBJ.children.length
OBJ.children.size
OBJ.children += { OBJ , … } ;
```

`parentProp` is the type of the `parent` member that every `object` has (§11.5.1).  Reading it yields the parent object, and `==` and `!=` compare against an object.  The member is `typesealed`:  an object body can re-initialize `parent` but can't change its type (§8.3.7).

`childrenProp` is the type of the `children` member:  the collection of an object's direct children, whose reading and placement rules are in §11.5.2.  It is a storageless member, with no slot of its own.

**Example**

```bgl
object cave { }
object lamp { parent = cave; }

void Main() {
    lamp.parent = player;                    // move lamp to player
    int n = cave.children.length;          // 0
}
```

**See also** §11.5.

### 21.5.8 `_bglObject` and `_bglPrimitive`

**Syntax**

```syntax
class NAME : _bglObject { … }
primitive class NAME : _bglPrimitive { … }
```

Every class derives one of two roots, both `emitter class`es.  A class declared without a base derives the one which fits its form (§8.6):

- **`_bglObject`**:  the root of every other class, whose values are I6 objects:  classes and structs, `object`, and the namespace objects (`bgl`, `_bglUtil`, …).  An emitter class derives it too, unless it is `primitive`.  It carries what I6 gives any object:

  | Member | Meaning |
  |---|---|
  | `instanceName` (`inline const string`) | the object's name, set in its body; the last positional member (§11.5.4) |
  | `operator =` (`_bglObject`) | the root assignment:  the variable refers to the assigned instance |
  | `operator ?()` | true unless `nothing` |
  | `operator ==` / `operator !=` (`_bglObject`) | identity:  the same instance |
  | `provides(property)` | the object declares the property |
  | `is(bglClass)` | the object is of the class, or of one derived from it |
  | `print()` | prints the object's short name; what `print(obj)` calls |

  A struct copies through its own copy operator instead of the root assignment (§2.10.3, §9.1).  `object` adds world-tree citizenship (`parent`, `children`, attributes) on top.

- **`_bglPrimitive`**:  the root of every primitive class (`int`, `char`, `string`, `bglClass`, …, §8.2.5), whose value is held directly.  It declares only the root assignment, which copies the word.  Everything else a primitive supports, it declares itself.

**See also** §2.10.3, §8.2.

### 21.5.9 `eType` and `typeof()`

**Syntax**

```syntax
typeof( EXPR )
```

`typeof(v)` returns the machine category of a value as an `eType`.  It is the discriminator for union-typed values; the members of `eType`, what each value reports, and the casts and limits are in §2.8.1.

**Example**

```bgl
object lamp { }

void Main() {
    var x = lamp;
    if (typeof(x) == eType.object) print("an object");   // -> an object
    bool b = typeof(true) == eType.int;                  // -> true: bool reports as int
}
```

**See also** §2.8.

### 21.5.10 `iString`, `iStringOrObject` and `iStringOrRoutine`

**Syntax**

```syntax
iString NAME ;
iStringOrObject NAME ;
iStringOrRoutine NAME ;
VALUE.isInterpolated()
VALUE.isObject()
VALUE.isRoutine()
print( VALUE ) ;
```

`iString` is the named union `string | interpolatedString`:  a string, plain or interpolated.  An interpolated string assigned to one becomes a routine which prints it, with its expressions evaluated each time it prints (§2.9.1).  `isInterpolated()` reports which form the value holds, and `print(x)` prints either.

`iStringOrObject` is the named union `string | interpolatedString | object`:  an `iString` or an object.  `isObject()` reports which the value holds.

`iStringOrRoutine` is the named union `string | interpolatedString | func<void>`:  an `iString` or a routine which prints.  `isRoutine()` reports whether the value is a routine (an interpolated string included), and `print(x)` prints the string or runs the routine.

All three are provided by the IF library bindings (§23.3.7).  An object can give such a property a method instead of a value (§23.3.11).

**Example**

```bgl
#include <bindings/punyInform>
extend class object { iString description; iStringOrObject n_to; iStringOrObject s_to; iStringOrObject e_to; }
int storms = 3;
object bar { }
object cloakroom { }

object foyer {
    description = "A spacious hall.";                    // a plain string
    s_to = bar;                                          // the room the exit leads to
    n_to = $"The {storms} storms block the way.^";       // printed instead of moving, read each time
    object e_to() {                                      // decided each time
        if (cloakroom.has(light)) return cloakroom;
        rtrue("The cloakroom door is stuck.");
    }
}
```

**See also** §2.8.2, §2.9.1, §23.3.7.

## 21.6 Numeric Utilities

### 21.6.1 `uint`

**Syntax**

```syntax
uint NAME [ = EXPR ] ;
(uint) EXPR
(int) EXPR
```

`uint` is an unsigned integer with the same bit pattern and width as `int` (16 bits on the Z-machine, 32 on Glulx).  There is no implicit conversion between `int` and `uint` in either direction.  A non-negative integer literal converts implicitly; however, a negative literal converts only with an explicit `(uint)` cast, so `uint x = -1;` is a compile-time error.

| Operators | Behavior |
|---|---|
| `+` `-` `*` `++` `--` `+=` `-=` `*=` | As for `int`; overflow wraps. |
| `/` `%` `/=` `%=` | Unsigned division and modulo. |
| `==` `!=` | As for `int`. |
| `<` `<=` `>` `>=` | Unsigned ordering:  a value with the high bit set is large, not negative. |
| `&` `\|` `^` `&=` `\|=` `^=` `<<` `<<=` | As for `int`. |
| `>>` `>>=` | Logical (zero-fill) right shift. |
| unary `-` | Not defined on `uint`. |

`print(uint)` prints the full unsigned value:  `print((uint)-1)` prints `65535` on the Z-machine and `4294967295` on Glulx.

**Example**

```bgl
int  n = 7;
uint x = 12;          // literal
uint y = (uint)n;     // explicit cast from int
int  z = (int)y;      // explicit cast back
uint w = (uint)-1;    // a negative literal requires the cast
```

**See also** §2.2, §2.4.1.

### 21.6.2 `bgl.util.math`

`bgl.util.math` holds the math helpers which have no operator form.

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

Call the unsigned helpers directly only for unsigned arithmetic on plain `int` values.

> ***[Glulx]:** On Glulx, `bgl.util.math` also takes `float` arguments:  `abs(x)` and `pow(a, b)` have float forms, and `sqrt(x)`, `exp(x)`, `log(x)`, `sin(x)`, `cos(x)`, `tan(x)`, `asin(x)`, `acos(x)`, `atan(x)`, `atan2(y, x)`, `ceil(x)`, and `floor(x)` take and return `float`.  Angles are in radians.*

**Example**

```bgl
int p = bgl.util.math.pow(2, 8);              // -> 256
int c = bgl.util.math.clamp(120, 0, 100);     // -> 100
int s = bgl.util.math.sign(-7);               // -> -1
```

### 21.6.3 `bgl.util.random`

`bgl.util.random` is the random-number source.

| Function | Returns | Description |
|---|---|---|
| `bgl.util.random.get(n)` | `int` | A uniformly random value in `1..n`. |
| `bgl.util.random.get(a, b[, c …])` | `var` | One of the listed values, chosen uniformly.  Two to eight values can be listed. |
| `bgl.util.random.seed(s)` | `void` | Seed the generator:  `s == 0` re-seeds from the environment; `s > 0` selects a deterministic sequence. |

**Example**

```bgl
object coin {}  object gem {}  object key {}

// … in a routine:
int d6 = bgl.util.random.get(6);
object prize = bgl.util.random.get(coin, gem, key);
```

> ***[Z-machine/Glulx difference]:** Both targets accept the same `seed(s)` calls, and the contract above holds on both.*

## 21.7 Character Utilities

The `char` type provides classification, case conversion, and case-insensitive comparison.  All of these handle the accented letters and ligatures:  on the Z-machine the ZSCII extended characters, and on Glulx the Latin-1 range (through Glk's case tables) and `œ`/`Œ`.

| Member | Returns | Description |
|---|---|---|
| `c.isLower()` / `c.isUpper()` | `bool` | Lowercase / uppercase letter, including accented letters. |
| `c.isAlpha()` | `bool` | Any letter. |
| `c.isNumeric()` | `bool` | A digit `0`..`9`. |
| `c.isAlphaNumeric()` | `bool` | A letter or digit. |
| `c.isVowel()` / `c.isConsonant()` | `bool` | Vowel (including accented vowels) / consonant. |
| `c.toUpper()` / `c.toLower()` | `char` | Case conversion; a character with no case maps to itself. |
| `c =~ d` | `bool` | Case-insensitive equality:  `'A' =~ 'a'` is true. |

The `char` comparison and arithmetic operators are in §4.5 and §4.6.

**Example**

```bgl
char c = 'a';
bool v = c.isVowel();        // -> true
char u = c.toUpper();        // -> 'A'
bool same = c =~ 'A';        // -> true
```

## 21.8 `bglAllocated`

**Syntax**

```syntax
class NAME [ N ] : object, bglAllocated { … }
INSTANCE.copy( OTHER ) ;
INSTANCE.remaining()
```

The `[ N ]` brackets are literal:  the pool size of §8.2.6.

`bglAllocated` is a mixin for pooled classes (§8.2.6).  A pooled class inheriting it gains two operations:

| Method | Returns | Description |
|---|---|---|
| `instance.copy(other)` | `void` | Copies every property of `other` into `instance`.  `other` is `selfType` (§2.11):  `instance`'s own class or a subclass of it. |
| `instance.remaining()` | `int` | The number of free slots remaining in the class's pool. |

Copying is explicit; assigning one pooled instance to another does not copy.  Copying a base-class instance into a subclass instance, or between unrelated classes, is a compile-time error.

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
bgl.world.getAll( [ PRED ] )
bgl.world.inParent( PARENT [ , PRED ] )
bgl.world.instances( CLASS [ , PRED ] )
```

`bgl.world` queries the object tree and returns the matching objects as a tracked `array<object>`.

| Method | Returns |
|---|---|
| `bgl.world.getAll()` | Every object in the program. |
| `bgl.world.inParent(parent)` | The direct children of `parent`. |
| `bgl.world.instances(cls)` | Every object of class `cls` or a subclass. |

Each method takes an optional predicate, a `func<bool, object>`; only the objects for which it returns true are returned.

Results live in a shared, rotating set of four scratch buffers, each holding `worldBufSize` objects (default 128, §15.4).  The number of buffers is fixed.  A result is valid until the fourth subsequent query.  Don't store a result across turns; if you need it later, copy it into an `array<object>` of your own (assignment copies, §12.5).  Note that a walk which would exceed a buffer stops silently at its capacity.

**Example**

```bgl
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
bgl.ui.statusBar.height [ = LINES ]
bgl.ui.mainWin.width
bgl.ui.statusBar.width
bgl.ui.statusBar.moveCursor( COL , LINE )
bgl.ui.recoverWindows()
```

The two root windows of the display are objects under `bgl.ui`.  Their members are the same on both targets; however, an IF library binding replaces the members that the library itself controls (§23.3.8), and `<glulxWindow>` adds the window API to the same objects (§22.7.2).

| Member | Type | Description |
|---|---|---|
| `bgl.ui.mainWin.id` | `int` | The main window handle.  `0` until a binding or the program assigns it. |
| `bgl.ui.statusBar.id` | `int` | The status window handle.  `0` until assigned; `0` and read-only under a binding on the Z-machine, which has no window handles. |
| `bgl.ui.statusBar.height` | `int` | The status window's height in lines.  Writing it splits the status window to that height. |
| `bgl.ui.mainWin.width` | `int` | The main window's width in characters.  Read-only; `<glulxWindow>` makes it writable (§22.7.4). |
| `bgl.ui.statusBar.width` | `int` | The status window's width in characters.  Read-only; `<glulxWindow>` makes it writable (§22.7.4). |
| `bgl.ui.statusBar.moveCursor(col, line)` | `void` | Moves the status window's cursor to column `col`, line `line`, both counted from 0.  Output must be going to the status window. |
| `bgl.ui.recoverWindows()` | `void` | After a restore, undo or restart, brings the windows on screen back in line with the program's:  the status bar gets back the program's height, and with `<glulxWindow>` its windows are kept, closed or opened again (§22.7.6).  Calling it again has no effect. |

**Example**

```bgl
bgl.ui.statusBar.height = 2;
```

> ***[Glulx]:** Without a binding, writing `statusBar.height` opens a text-grid window above the root window on the first write, and re-arranges it thereafter.*

> ***[Z-machine]:** Without a binding, writing `statusBar.height` splits the upper window to that height.*

> ***[Glulx]:** `width` is 80 until the window is open.  Glk measures a text-buffer window in units of its font's `0`, so with a proportional font the main window's width is approximate.*

> ***[Z-machine]:** Both windows are as wide as the screen, so both `width`s read the header's screen width.*

> ***[Glulx]:** Glk windows live outside the program's memory, so a restore, undo or restart rolls back what the program believes about its windows but not what is on screen.  The Inform 6 standard library binding calls `recoverWindows()` for you once the library has found its own windows; a program with no library, or another library, calls it after its own restore, undo or restart.*

> ***[Z-machine]:** `recoverWindows()` does nothing.*

**See also** §22.6, §22.7, §23.3.8.

## 21.11 `bgl.printRules`

**Syntax**

```syntax
$"… {bgl.printRules.RULE} …"
$"… {bgl.printRules.img( IMAGE [ , ALIGN [ , WIDTH [ , HEIGHT ] ] ] )} …"
$"… {bgl.printRules.link( TEXT , TARGET )} …"
```

`bgl.printRules` holds print rules for use inside interpolated strings (§1.6.5).  The style rules switch the output style at that point in the text, while the article rules take the object to print.  With `#using bgl.printRules;` the rules can be reached bare.

| Rule | Effect |
|---|---|
| `a(obj)` | The object's short name with an indefinite article:  "a lamp". |
| `cA(obj)` | As `a`, capitalized:  "A lamp". |
| `the(obj)` | The object's short name with the definite article:  "the lamp". |
| `cThe(obj)` | As `the`, capitalized:  "The lamp". |
| `bold` | Bold text. |
| `italics` | Italic text (rendered as underline where the target has no italics). |
| `underline` | Underlined text. |
| `reverse` | Reverse video. |
| `fixed` | Fixed-pitch text. |
| `roman` | Return to plain text. |
| `img(image[, align[, width[, height]]])` | Draw an image inline in the main window.  **[Glulx]**, and only when `generateBlorb` is true (§15.6).  `image` is an `eImages` value; `align` is an `eGlulxImageAlign` (default `inlineCenter`); a `0` dimension is computed from the other, preserving aspect ratio, and `0, 0` draws at natural size. |
| `link(text, target)` | Print `text` as a hyperlink.  **[Glulx]**; a compile-time error on the Z-machine.  `target` is a `literal func<void> \| int`; what a click does depends on it.  See *Hyperlinks* below. |

**Example**

```bgl
#using bgl.printRules;

// … in a routine:
print($"The troll {bgl.printRules.italics}hit{bgl.printRules.roman} the table.");
print($"{bold}Warning{roman}");
print($"You cannot open {the(noun)}.");
```

**Hyperlinks**

The `target` of a `link` decides what a click does:

- A **function**, whether a lambda (§4.14) or a named function, is called.  The parameter is `literal` (§6.3):  the function runs when the player clicks, long after the `print`, so a lambda capturing a variable is a compile-time error.
- An **`int`** or an **enum value** is delivered to `HandleGlkEvent` (§6.7.1) as the event's link value, as in Inform 6.  A `target` of `0` makes no link.

<!-- doctest: skip -->
```bgl
#using bgl.printRules;
print($"There's a {link("button", => { print("Click.^"); })} on the machine.");  // run code
print($"There's a {link("lever", handlePullLever)} near it.");                   // call a function
print($"Under it is {link("broom", eLinks.broom)}.");                            // HandleGlkEvent
```

Glk does not allow printing to a window waiting for a line.  When a function target is clicked at a command prompt, the runtime suspends line input while the function runs.  The player's partial input is echoed, and the prompt is then reprinted with that input restored.  The library binding provides this for the Inform 6 library, but not for a program's own `HandleGlkEvent` (§6.7.1).

An interpreter without hyperlink support prints the link text plainly.

**See also** §1.6.5, §4.14, §15.6, §22.8.

## 21.12 Utility Types

**`bglSize`.**  A struct with `int width` and `int height` members and a copying `operator =`.

**Blorb asset enums.**  There are three enums, empty unless blorb packaging is enabled, and one union over them:

| Type | Contents |
|---|---|
| `eImages` | Picture resource ids. |
| `eSounds` | Sound resource ids. |
| `eUnknownAsset` | Data resource ids:  any packaged file which is neither an image nor a sound. |
| `eAssets` | The union `eImages \| eSounds \| eUnknownAsset`, for APIs which accept any resource. |

When `generateBlorb` is true, the compiler extends `eImages` and `eSounds` with one member per asset file found (§15.6.1).  A value of `eImages` is the raw resource id; with `<glulxImage>` it also has the properties `width`, `height`, and `size` (§22.8).

**See also** §2.7, §2.8, §15.6.1, §22.8.

## 21.13 `bgl.asm`

`bgl.asm` is the namespace of direct opcode emitters for the active target, an alias (§10.2) for a target-specific class.  A member is either an emitter, which inlines the opcode at the call site, or a `static superposed` function (§3.12), which wraps a value-returning opcode in a free routine.  In the tables below, kind **E** is an emitter and **S** a `static superposed` function.

**Example**

<!-- doctest: glulx -->
```bgl
#using bgl;

// … in a routine:
int n = asm.random(6);        // Glulx: @random; Z-machine: @random
asm.streamchar('!');          // Glulx only
```

### 21.13.1 Glulx Opcodes

On Glulx, `bgl.asm` provides the following opcodes:

| Member | Signature | Kind | Meaning |
|---|---|---|---|
| `add` / `sub` / `mul` / `div` / `mod` | `int OP(int a, int b)` | E | Arithmetic |
| `neg` | `int neg(int a)` | E | `-a` |
| `bitand` / `bitor` | `int OP(int a, int b)` | E | Bitwise and, or |
| `bitxor` | `int bitxor(int a, int b)` | S | `@bitxor` |
| `bitnot` | `int bitnot(int a)` | E | `~a` |
| `shiftl` | `int shiftl(int a, int b)` | S | Logical shift left |
| `sshiftr` | `int sshiftr(int a, int b)` | S | Arithmetic shift right |
| `ushiftr` | `int ushiftr(int a, int b)` | S | Logical shift right |
| `aload` / `aloads` / `aloadb` / `aloadbit` | `int OP(int addr, int idx)` | S | Load word, short, byte, bit |
| `astore` / `astores` / `astoreb` / `astorebit` | `void OP(int addr, int idx, int v)` | E | Store word, short, byte, bit |
| `copy` | `int copy(int v)` | E | `@copy` |
| `sexs` / `sexb` | `int OP(int v)` | S | Sign-extend short, byte |
| `stkcount` | `int stkcount()` | S | `@stkcount` |
| `stkpeek` | `int stkpeek(int idx)` | S | `@stkpeek` |
| `stkswap` | `void stkswap()` | E | `@stkswap` |
| `stkroll` | `void stkroll(int count, int dir)` | E | `@stkroll` |
| `stkcopy` | `void stkcopy(int count)` | E | `@stkcopy` |
| `callf` / `callfi` / `callfii` / `callfiii` | `int OP(int addr, …)` | S | Call with 0 to 3 arguments |
| `streamchar` / `streamnum` / `streamstr` / `streamunichar` | `void OP(int v)` | E | Stream output |
| `getstringtbl` | `int getstringtbl()` | S | `@getstringtbl` |
| `setstringtbl` | `void setstringtbl(int addr)` | E | `@setstringtbl` |
| `getmemsize` | `int getmemsize()` | S | `@getmemsize` |
| `setmemsize` | `int setmemsize(int size)` | S | `@setmemsize` |
| `malloc` | `int malloc(int size)` | S | Heap allocate |
| `mfree` | `void mfree(int addr)` | E | `@mfree` |
| `mzero` | `void mzero(int count, int addr)` | E | `@mzero` |
| `mcopy` | `void mcopy(int count, int src, int dst)` | E | `@mcopy` |
| `linearsearch` / `binarysearch` | `int OP(int key, int keySize, int start, int structSize, int numStructs, int keyOffset, int options)` | S | Table search |
| `linkedsearch` | `int linkedsearch(int key, int keySize, int start, int keyOffset, int nextOffset, int options)` | S | `@linkedsearch` |
| `random` | `int random(int range)` | S | `@random` |
| `setrandom` | `void setrandom(int seed)` | E | `@setrandom` |
| `quit` / `restart` | `void OP()` | E | `@quit`, `@restart` |
| `save` / `restore` | `int OP(int stream)` | S | `@save`, `@restore` |
| `saveundo` / `restoreundo` | `int OP()` | S | `@saveundo`, `@restoreundo` |
| `protect` | `void protect(int addr, int len)` | E | `@protect` |
| `verify` | `int verify()` | S | `@verify` |
| `debugtrap` | `void debugtrap(int val)` | E | `@debugtrap` |
| `glk` | `int glk(int sel, int argc)` | E | Raw Glk dispatch |
| `setiosys` | `void setiosys(int mode, int rock)` | E | `@setiosys` |
| `numtof` | `int numtof(int n)` | S | Integer to float bits |
| `ftonumz` / `ftonumn` | `int OP(int f)` | S | Float to integer, truncating / rounding |
| `fadd` / `fsub` / `fmul` / `fdiv` | `int OP(int a, int b)` | S | Float arithmetic |
| `ceil` / `floor` / `sqrt` / `exp` / `log` | `int OP(int f)` | S | Float unary functions |
| `pow` | `int pow(int a, int b)` | S | `@pow` |
| `sin` / `cos` / `tan` / `asin` / `acos` / `atan` | `int OP(int f)` | S | Float trigonometry |
| `atan2` | `int atan2(int a, int b)` | S | `@atan2` |
| `accelfunc` | `void accelfunc(int idx, int addr)` | E | `@accelfunc` |
| `accelparam` | `void accelparam(int idx, int val)` | E | `@accelparam` |

### 21.13.2 Glk Calls

Each of these is an emitter which performs the named Glk call; the member name is the Glk function name in camel case, without the `glk_` prefix (`windowOpen` is `glk_window_open`).  Parameters are `int` and follow the Glk argument order unless noted.

| Member | Signature | Glk call |
|---|---|---|
| `exit` | `void exit()` | `glk_exit` |
| `tick` | `void tick()` | `glk_tick` |
| `gestalt` | `int gestalt(int a, int b)` | `glk_gestalt` |
| `gestaltExt` | `int gestaltExt(int a, int b, int c, int d)` | `glk_gestalt_ext` |
| `windowIterate` | `int windowIterate(int a, int b)` | `glk_window_iterate` |
| `windowGetRock` | `int windowGetRock(int a)` | `glk_window_get_rock` |
| `windowGetRoot` | `int windowGetRoot()` | `glk_window_get_root` |
| `windowOpen` | `int windowOpen(int splitWinId, eGlulxWindowType type, int size, bGlulxWindowMethodFlags winMethod = noBorder, int winRock = 0)` | `glk_window_open` (the parameters are not in Glk order; the call is made in Glk order) |
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
| `charToLower` / `charToUpper` | `int OP(int a)` | `glk_char_to_lower`, `glk_char_to_upper` |
| `stylehintSet` | `void stylehintSet(int a, int b, int c, int d)` | `glk_stylehint_set` |
| `stylehintClear` | `void stylehintClear(int a, int b, int c)` | `glk_stylehint_clear` |
| `styleDistinguish` | `int styleDistinguish(int a, int b, int c)` | `glk_style_distinguish` |
| `styleMeasure` | `int styleMeasure(int a, int b, int c, int d)` | `glk_style_measure` |
| `select` / `selectPoll` | `void OP(int a)` | `glk_select`, `glk_select_poll` |
| `requestLineEvent` | `void requestLineEvent(int a, int b, int c, int d)` | `glk_request_line_event` |
| `cancelLineEvent` | `void cancelLineEvent(int a, int b)` | `glk_cancel_line_event` |
| `requestCharEvent` / `cancelCharEvent` | `void OP(int a)` | `glk_request_char_event`, `glk_cancel_char_event` |
| `requestMouseEvent` / `cancelMouseEvent` | `void OP(int a)` | `glk_request_mouse_event`, `glk_cancel_mouse_event` |
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
| `schannelStop` / `schannelPause` / `schannelUnpause` | `void OP(int a)` | `glk_schannel_stop`, `glk_schannel_pause`, `glk_schannel_unpause` |
| `schannelSetVolume` / `schannelSetVolumeExt` | `void schannelSetVolume(int a, int b)` / `void schannelSetVolumeExt(int a, int b, int c, int d)` | `glk_schannel_set_volume`, `glk_schannel_set_volume_ext` |
| `soundLoadHint` | `void soundLoadHint(int a, int b)` | `glk_sound_load_hint` |
| `setHyperlink` / `setHyperlinkStream` | `void setHyperlink(int a)` / `void setHyperlinkStream(int a, int b)` | `glk_set_hyperlink`, `glk_set_hyperlink_stream` |
| `requestHyperlinkEvent` / `cancelHyperlinkEvent` | `void OP(int a)` | `glk_request_hyperlink_event`, `glk_cancel_hyperlink_event` |
| `bufferToLowerCaseUni` / `bufferToUpperCaseUni` | `int OP(int a, int b, int c)` | `glk_buffer_to_lower_case_uni`, `glk_buffer_to_upper_case_uni` |
| `bufferToTitleCaseUni` | `int bufferToTitleCaseUni(int a, int b, int c, int d)` | `glk_buffer_to_title_case_uni` |
| `bufferCanonDecomposeUni` / `bufferCanonNormalizeUni` | `int OP(int a, int b, int c)` | `glk_buffer_canon_decompose_uni`, `glk_buffer_canon_normalize_uni` |
| `putCharUni` / `putStringUni` | `void OP(int a)` | `glk_put_char_uni`, `glk_put_string_uni` |
| `putBufferUni` | `void putBufferUni(int a, int b)` | `glk_put_buffer_uni` |
| `putCharStreamUni` / `putStringStreamUni` | `void OP(int a, int b)` | `glk_put_char_stream_uni`, `glk_put_string_stream_uni` |
| `putBufferStreamUni` | `void putBufferStreamUni(int a, int b, int c)` | `glk_put_buffer_stream_uni` |
| `getCharStreamUni` | `int getCharStreamUni(int a)` | `glk_get_char_stream_uni` |
| `getBufferStreamUni` / `getLineStreamUni` | `int OP(int a, int b, int c)` | `glk_get_buffer_stream_uni`, `glk_get_line_stream_uni` |
| `streamOpenFileUni` | `int streamOpenFileUni(int a, int b, int c)` | `glk_stream_open_file_uni` |
| `streamOpenMemoryUni` | `int streamOpenMemoryUni(int a, int b, int c, int d)` | `glk_stream_open_memory_uni` |
| `streamOpenResourceUni` | `int streamOpenResourceUni(int a, int b)` | `glk_stream_open_resource_uni` |
| `requestCharEventUni` | `void requestCharEventUni(int a)` | `glk_request_char_event_uni` |
| `requestLineEventUni` | `void requestLineEventUni(int a, int b, int c, int d)` | `glk_request_line_event_uni` |
| `setEchoLineEvent` | `void setEchoLineEvent(int a, int b)` | `glk_set_echo_line_event` |
| `setTerminatorsLineEvent` | `void setTerminatorsLineEvent(int a, int b, int c)` | `glk_set_terminators_line_event` |
| `currentTime` | `void currentTime(int a)` | `glk_current_time` |
| `currentSimpleTime` | `int currentSimpleTime(int a)` | `glk_current_simple_time` |
| `timeToDateUtc` / `timeToDateLocal` | `void OP(int a, int b)` | `glk_time_to_date_utc`, `glk_time_to_date_local` |
| `simpleTimeToDateUtc` / `simpleTimeToDateLocal` | `void OP(int a, int b, int c)` | `glk_simple_time_to_date_utc`, `glk_simple_time_to_date_local` |
| `dateToTimeUtc` / `dateToTimeLocal` | `void OP(int a, int b)` | `glk_date_to_time_utc`, `glk_date_to_time_local` |
| `dateToSimpleTimeUtc` / `dateToSimpleTimeLocal` | `int OP(int a, int b)` | `glk_date_to_simple_time_utc`, `glk_date_to_simple_time_local` |
| `garglkSetZcolors` / `garglkSetZcolorsStream` | `void garglkSetZcolors(int a, int b)` / `void garglkSetZcolorsStream(int a, int b, int c)` | `garglk_set_zcolors`, `garglk_set_zcolors_stream` |
| `garglkSetReversevideo` / `garglkSetReversevideoStream` | `void garglkSetReversevideo(int a)` / `void garglkSetReversevideoStream(int a, int b)` | `garglk_set_reversevideo`, `garglk_set_reversevideo_stream` |

### 21.13.3 Z-machine Opcodes

On the Z-machine, `bgl.asm` provides the following opcodes:

| Member | Signature | Kind | Meaning |
|---|---|---|---|
| `add` / `sub` / `mul` / `div` / `mod` | `int OP(int a, int b)` | E | Arithmetic |
| `bitor` / `bitand` | `int OP(int a, int b)` | E | Bitwise or, and |
| `bitnot` | `int bitnot(int a)` | E | `~a` |
| `loadw` / `loadb` | `int OP(int arr, int idx)` | S | `@loadw`, `@loadb` |
| `storew` / `storeb` | `void OP(int arr, int idx, int v)` | E | `@storew`, `@storeb` |
| `push` | `void push(int v)` | E | `@push` |
| `pull` | `void pull(var into)` | E | `@pull` into a variable; a routine has its own stack, so `pull` is a statement, not a value |
| `getParent` / `getChild` / `getSibling` | `int OP(int obj)` | S | `@get_parent`, `@get_child`, `@get_sibling` |
| `insertObj` | `void insertObj(int obj, int dst)` | E | `@insert_obj` |
| `removeObj` | `void removeObj(int obj)` | E | `@remove_obj` |
| `printObj` | `void printObj(int obj)` | E | `@print_obj` |
| `getProp` / `getPropAddr` / `getNextProp` | `int OP(int obj, int prop)` | S | `@get_prop`, `@get_prop_addr`, `@get_next_prop` |
| `getPropLen` | `int getPropLen(int propAddr)` | S | `@get_prop_len` |
| `putProp` | `void putProp(int obj, int prop, int v)` | E | `@put_prop` |
| `setAttr` / `clearAttr` | `void OP(int obj, int attr)` | E | `@set_attr`, `@clear_attr` |
| `inc` / `dec` | `void OP(int variable)` | E | `@inc`, `@dec` |
| `printChar` / `printNum` / `printAddr` / `printPaddr` | `void OP(int v)` | E | `@print_char`, `@print_num`, `@print_addr`, `@print_paddr` |
| `newLine` | `void newLine()` | E | `@new_line` |
| `splitWindow` | `void splitWindow(int lines)` | E | `@split_window` |
| `setWindow` | `void setWindow(int win)` | E | `@set_window` |
| `outputStream` | `void outputStream(int n)` / `void outputStream(int n, int table)` | E | `@output_stream` |
| `inputStream` | `void inputStream(int n)` | E | `@input_stream` |
| `random` | `int random(int range)` | S | `@random` |
| `quit` / `restart` | `void OP()` | E | `@quit`, `@restart` |
| `verify` | `int verify()` | E | `@verify` |
| `soundEffect` | `void soundEffect(int num, int effect, int vol)` | E | `@sound_effect` |
| `eraseWindow` | `void eraseWindow(int win)` | E | `@erase_window` |
| `eraseLine` | `void eraseLine(int v)` | E | `@erase_line` |
| `setCursor` | `void setCursor(int line, int col)` | E | `@set_cursor` |
| `getCursor` | `void getCursor(int arr)` | E | `@get_cursor` |
| `setTextStyle` | `void setTextStyle(int style)` | E | `@set_text_style` |
| `bufferMode` | `void bufferMode(int flag)` | E | `@buffer_mode` |
| `readChar` | `int readChar(int dev = 1)` | S | `@read_char` |
| `scanTable` | `int scanTable(int x, int table, int len)` | S | `@scan_table` |
| `call1s` / `call2s` | `int call1s(int routine)` / `int call2s(int routine, int a)` | S | `@call_1s`, `@call_2s` |
| `logShift` / `artShift` | `int OP(int a, int places)` | S | `@log_shift`, `@art_shift` |
| `copyTable` | `void copyTable(int src, int dst, int size)` | E | `@copy_table` |
| `printUnicode` | `void printUnicode(int ch)` | E | `@print_unicode` |
| `setColour` | `void setColour(int fg, int bg)` | E | `@set_colour` |
| `setFont` | `int setFont(int font)` | S | `@set_font` |
| `tokenise` | `void tokenise(int text, int parse)` | E | `@tokenise` |
| `encodeText` | `void encodeText(int zscii, int len, int from, int coded)` | E | `@encode_text` |
| `printTable` | `void printTable(int zscii, int width)` | E | `@print_table` |
| `save` / `restore` | `int OP(int table, int bytes, int name)` | S | `@save`, `@restore` |
| `saveUndo` / `restoreUndo` | `int OP()` | S | `@save_undo`, `@restore_undo` |
| `call1n` / `call2n` | `void call1n(int routine)` / `void call2n(int routine, int a)` | E | `@call_1n`, `@call_2n` |
| `checkUnicode` | `int checkUnicode(int ch)` | S | `@check_unicode` |

**See also** §3.12, §10.2, §22.7.

## 21.14 `bgl.header`

`bgl.header` reads fields of the story file header:  the release number and serial code the game was compiled with (§15.3).  It needs no library.

| Member | Returns | Description |
|---|---|---|
| `bgl.header.release` | `int` | The release number. |
| `bgl.header.serialChar(i)` | `char` | Character `i` (`0..5`) of the six-character serial code. |
| `bgl.header.printSerial()` | `void` | Prints the serial code. |

**Example**

```bgl
print($"Release {bgl.header.release} / Serial number {bgl.header.printSerial()}");
```

## 21.15 `bgl.gc`

`bgl.gc` is the heap from which local arrays and `<linq>` results are allocated, along with its collector (§12.6).  It needs no library.

| Member | Returns | Description |
|---|---|---|
| `bgl.gc.free(a)` | `void` | Frees array `a` now instead of at the next collection.  A later use of it through another reference reads freed memory. |
| `bgl.gc.keep(a)` | `void` | Pins array `a`, for I6 code that holds on to it (§12.6):  it lives until `unpin` or `free`, whatever refers to it. |
| `bgl.gc.unpin(a)` | `void` | Hands a pinned array back to the collector, which frees it once nothing refers to it. |
| `bgl.gc.collect()` | `void` | Frees every heap array that nothing refers to. |
| `bgl.gc.used` | `int` | Words of the heap in blocks in use.  The same arrays can read slightly differently as the heap fragments. |
| `bgl.gc.size` | `int` | Words the heap holds for blocks. |
| `bgl.gc.peak` | `int` | The most words ever in blocks in use:  what `heapSize` must hold. |

The library bindings collect at the prompt (§23); when a program without one calls `collect()`, and what the collector can see, are covered in §12.6.

**Example**

```bgl
#include <array>
array<int> row(int n) {
    array<int> r[4];
    for (int i = 0; i < 4; i++) r += n * i;
    return r;
}

void Main() {
    bglInit();
    for (int n = 1; n <= 20; n++) {
        array<int> r = row(n);
        print(r[3], " ");
        bgl.gc.free(r);         // released now, not at the next prompt
    }
}
```

> ***Note:** Calling `bgl.gc.free` on an array already freed, or on a file-scope or member array (which isn't on the heap), is a run-time error (§19.4).*
