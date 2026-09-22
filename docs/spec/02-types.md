# 2 Types and Values

<!-- toc -->
- [2.1 Overview](#21-overview)
- [2.2 Primitive Types](#22-primitive-types)
- [2.3 The `float` Type](#23-the-float-type)
- [2.4 Literal Pseudo-Types](#24-literal-pseudo-types)
  - [2.4.1 `negativeIntLiteral`](#241-negativeintliteral)
  - [2.4.2 `interpolatedStringLiteral`](#242-interpolatedstringliteral)
- [2.5 `nothing` and `null`](#25-nothing-and-null)
- [2.6 The `var` Type](#26-the-var-type)
- [2.7 Enumerations](#27-enumerations)
  - [2.7.1 `enum`](#271-enum)
  - [2.7.2 `bnum`](#272-bnum)
  - [2.7.3 Widening to `int`](#273-widening-to-int)
  - [2.7.4 `extern enum` and `extern bnum`](#274-extern-enum-and-extern-bnum)
  - [2.7.5 Naming Members](#275-naming-members)
- [2.8 Union Types](#28-union-types)
  - [2.8.1 `typeof` and `eType`](#281-typeof-and-etype)
  - [2.8.2 Named Unions](#282-named-unions)
- [2.9 Function Types](#29-function-types)
- [2.10 Class Types as Values](#210-class-types-as-values)
- [2.11 Type Compatibility](#211-type-compatibility)
- [2.12 Conversion](#212-conversion)
<!-- /toc -->


## 2.1 Overview

Beguile is statically typed: every variable, parameter and return value has a type known at compile
time. Types fall into four categories: primitive types (§2.2), literal pseudo-types (§2.4),
user-defined types (enumerations §2.7, unions §2.8, classes §8) and the `var` escape type (§2.6).
Arrays are covered in §12.

## 2.2 Primitive Types

**Description**

The primitive types are declared by the runtime core (§21) and need no `#include`.

| Type | Description |
|---|---|
| `int` | Signed integer, one native word. |
| `uint` | Unsigned integer, the same width as `int`. A non-negative integer literal converts to it implicitly; any other conversion between `int` and `uint` is an explicit cast (§2.4.1). Its operators are specified in §21.6.1. |
| `float` | IEEE 754 single-precision floating point. `[Glulx]` See §2.3. |
| `bool` | Boolean value, `true` or `false`. The comparison and logical operators and `operator ?()` yield `eBool`, the enumeration `{ true, false }` (§2.7.4); `eBool` and `bool` interoperate, so a comparison may be stored in a `bool` and a `bool` tested where an `eBool` is expected. Tables elsewhere in this specification write `bool` for either. |
| `char` | A single ZSCII character value. The runtime core (§21.7) adds case-conversion and inspection methods. |
| `string` | A reference to static text. The core provides printing, equality and literal assignment; the `<string>` extension (§22.3) compares content and adds `stringObj` for text that is built or changed. |
| `object` | The base class of every world object in the IF model (§11.1). |
| `verb` | The class from which verbs are declared (§13.2). |
| `void` | Not a value type: the return type of a function that returns nothing. |

`object`, `string`, `array` and `verb` share their names with I6 constructs and compile to them, but
are used with Beguile syntax and typing.

**Notes**

Beyond the primitive types, the library provides `array<T>` and `rawArray<T>` (§12.2, §12.8) and,
with `#include <string>`, `stringObj` for text that is built or changed (§22.3).

## 2.3 The `float` Type

**Description**

A `float` occupies one native word, the same width as `int`, holding an IEEE 754 single-precision
value. It supports the arithmetic operators `+` `-` `*` `/` `%` and their compound-assignment forms,
the comparisons `==` `!=` `<` `<=` `>` `>=`, and `print()`.

A `float` is initialized from a float literal (§1.6.2) or by assignment from an `int`, which converts
the numeric value (`5` becomes `5.0`). Conversion in either direction by cast is explicit:
`(float)n` and `(int)f` (the latter truncates). An `int` is never reinterpreted bit-for-bit as a
`float`.

**Example**

```bgl
#beguilerSettings { target = Glulx; }

void Main() {
    float a = 2.5;
    float b = 5;                    // → 5.0
    float c = (float)314 / (float)100;   // → 3.14
    print(b / a);                   // → 2
    if(b > a) print("^bigger^");
    int n = (int)c;                 // → 3
}
```

**Notes**

> **[Glulx]** `float` is available only when the target is Glulx; the Z-machine has no floating-point
> support. It is part of the core and needs no `#include`.

## 2.4 Literal Pseudo-Types

**Description**

A literal has a **pseudo-type** that is inferred by the compiler and never written by the author.
Pseudo-types take part in operator and overload resolution independently of the runtime types they
correspond to.

| Pseudo-type | Written | Corresponding type |
|---|---|---|
| `intLiteral` | `42`, `$FF`, `$$1010` | `int` |
| `negativeIntLiteral` | `-1` | `int` |
| `stringLiteral` | `"hello"`, `@"raw"` | `string` |
| `charLiteral` | `'a'` | `char` |
| `dictionaryWordLiteral` | `.cloak`, `..cloaks` | `dictionaryWord` (§21.5.3) |
| `interpolatedStringLiteral` | `$"hello {x}"` | none; see §2.4.2 |

A pseudo-type is compatible with its corresponding type only through an `operator =` declared on that
type (§2.11); there is no built-in rule. Pseudo-types are first-class types, declared as
`extern class` (§8.2.2) in the core and extensible with `extend` (§8.7.1), so a method defined
against one may be called directly on a literal.

**Example**

```bgl
"hello".print();       // a method on stringLiteral
42.someMethod();       // a method on intLiteral
```

### 2.4.1 `negativeIntLiteral`

**Description**

A negated integer literal such as `-1` has the pseudo-type `negativeIntLiteral` rather than
`intLiteral`. It converts to `int` implicitly but is not compatible with `uint` (§2.2); assigning
one to a `uint` requires an explicit cast.

**Example**

```bgl
int  x = -5;         // → -5
uint u = -1;         // compile-time error
uint v = (uint)-1;   // the largest unsigned value
```

### 2.4.2 `interpolatedStringLiteral`

**Description**

An interpolated string (§1.6.5) contains several segments that cannot be reduced to a single value, so
it has no corresponding runtime type. It may be passed only to an emitter that declares an
`interpolatedStringLiteral` parameter, where it expands into a block of statements; passing it to a
non-emitter function is a compile-time error. The core `print()` and `log()` accept it (§21.4); with the
`<string>` extension a `stringObj` may be assigned one, `stringObj s = $"…";` (§22.3).

## 2.5 `nothing` and `null`

**Description**

`nothing` is the absent or unset value. It is the value I6 uses to mean "no object", numerically `0`,
and Beguile exposes it under that name throughout the language. Its resolved type (§4.1) is `object`,
but it is compatible with every type. `null` is a synonym; the two are interchangeable.

For a reference, `nothing` is the absent state (a failed `new` on a pooled class, §8.2.6; an unset
member; a missing parent); for an integer it is `0`.

**Example**

```bgl
object o;
if(o == nothing) print("not yet set");

marbleClass m = new marbleClass();
if(m == null) print("pool exhausted");
```

## 2.6 The `var` Type

**Description**

`var` is a universal escape type that bypasses static type checking in both directions: any value may
be assigned to a `var`, and a `var` may be assigned to any type. The author is responsible for the
underlying value being meaningful; the compiler catches no mismatch involving `var`.

In overload resolution an overload with `var` parameters is a fallback, selected only when no typed
overload matches (§6.4).

**Example**

```bgl
var x = 5;
int y = x;        // no type check
object o = x;     // no type check
```

## 2.7 Enumerations

### 2.7.1 `enum`

**Syntax**

```syntax
enum ⟨name⟩ { ⟨member⟩ [ = ⟨integer⟩ ] [ , ⟨member⟩ [ = ⟨integer⟩ ] … ] }
```

**Description**

An `enum` declares a named set of integer constants. Values start at 1 and increment by 1. A member
may be given an explicit value, including a negative one; numbering resumes from the last assigned
value. Two members may hold the same value, but they are then indistinguishable at run time.

**Example**

```bgl
enum direction { north, south, east, west }   // → 1, 2, 3, 4

enum myPhase {
    setup  = 0,
    play   = 10,
    ending           // → 11
}
```

### 2.7.2 `bnum`

**Syntax**

```syntax
bnum ⟨name⟩ [ : ⟨base bnum⟩ ] { ⟨member⟩ [ = ⟨integer⟩ ] [ , ⟨member⟩ [ = ⟨integer⟩ ] … ] }
```

**Description**

A `bnum` is a bit-flag enumeration. Values start at 1 and double with each member, so members are
non-overlapping powers of two suitable for combination as flags. An explicit value must be a
non-negative power of two, and numbering resumes by doubling from it. `0` is always a legal value.

**Shared base.** A `bnum` may name a base `bnum`, declaring that it occupies a sub-field of the same
packed integer. The base must itself be a `bnum`; naming a plain `enum` as the base is a compile-time
error. Within a shared base the power-of-two rule is relaxed, since a sub-field's members need not be
single bits.

**Combining values.** Bitwise `|`, `&` and `^` between two `bnum` values are permitted only when the
operands share a common `bnum` ancestor (one may be the other's ancestor, or both the same type). The
result has the shared ancestor's type. Combining values with no common base is a compile-time error.

**Example**

```bgl
bnum itemFlag { portable, fragile, lit, locked }   // → 1, 2, 4, 8

bnum winMethodFlags { }                          // empty base: defines the bit-field space
bnum windowPlacement : winMethodFlags { left = 0, right = 1, above = 2, below = 3 }
bnum windowScale     : winMethodFlags { fixed = 16, proportional = 32 }
bnum windowBorder    : winMethodFlags { border = 0, noBorder = 256 }

int winMethod = left | fixed | noBorder;    // all share winMethodFlags
int bad       = left | itemFlag.portable;   // compile-time error
```

**Notes**

> **[Z-machine/Glulx difference]** A `bnum` may have at most 16 distinct values on the Z-machine and
> 32 on Glulx.

### 2.7.3 Widening to `int`

**Description**

A `bnum` value converts implicitly to `int`. The reverse, `int` to a `bnum`, requires an explicit
cast. A plain `enum` does not widen in either direction; converting between an `enum` and `int`
requires an explicit cast.

**Example**

```bgl
bnum itemFlag { portable, fragile }
enum direction { north, south }

int a = fragile;               // → 2: a bnum widens to int
itemFlag f = (itemFlag)2;      // int to bnum: explicit cast
int b = (int)south;            // → 2: enum to int: explicit cast
direction d = north;
int c = d;                     // compile-time error: an enum does not widen
```

### 2.7.4 `extern enum` and `extern bnum`

**Syntax**

```syntax
extern enum ⟨name⟩ { ⟨member⟩ [ , ⟨member⟩ … ] }
extern bnum ⟨name⟩ { ⟨member⟩ [ , ⟨member⟩ … ] }
```

**Description**

An `extern` enumeration names values that are defined in I6, not by Beguile. The declaration registers
the names for type checking and produces no output. `eBool`, Beguile's boolean-result type (§2.2), is
declared this way by the runtime core.

**Example**

```bgl
extern enum eBool { true, false }
extern enum eErrorFormat { E1, E2 }
```

### 2.7.5 Naming Members

**Description**

An enumeration member may be referenced by bare name (`north`, `true`) or qualified by its type
(`direction.north`, `eBool.true`). The qualified form is required when two enumeration types declare
a member of the same name.

Enumerations may carry emitter methods, declared in the body or added with `extend enum`; the form is
given in §7.9.

## 2.8 Union Types

**Syntax**

```syntax
⟨type⟩ | ⟨type⟩ [ | ⟨type⟩ … ]
```

The `|` here is the union type operator, a literal token.

**Description**

A union type declares that a value is one of several types, distinguished at run time. It is valid at
every type position: parameter, local, return type, class or object member, and array element type.

A union complements overloading rather than replacing it: an overload dispatches on the caller's
static type at compile time, whereas a union carries a value whose type is known only at run time (one
read from a member or an array) and is discriminated with `typeof` (§2.8.1).

**Canonical form.** Members are order-independent and de-duplicated: `string | func<void>` and
`func<void> | string` name the same type, and a union of a type with itself is that type. Members may be listed in any order.

**Operations.** A union value may be assigned, passed, returned and compared with `==` and `!=`. It
may not be called, printed, member-accessed or used in arithmetic while it is still a union; doing so
is a compile-time error. The value is first **narrowed** with an ordinary cast `(⟨member⟩)x`. Because
every member shares one machine word, the cast retypes the value without converting it; it is an
assertion, and the author is responsible for having discriminated correctly.

**Compatibility.** A value of type `T` is assignable to a union `U` when `T` is compatible with some
member of `U`. A union `U₁` is assignable to a union `U₂` when every member of `U₁` is compatible with
some member of `U₂`. A union is not assignable to a plain member type without a narrowing cast. In
overload resolution a union parameter is the widest candidate: an exact or member-typed overload always
wins, and the union catches only arguments whose static type is itself a union.

**Example**

```bgl
string | func<void> L = "hello";
string | func<void> pick(int n) { … }
class Slot { string | func<void> handler; }
array<string | func<void>> items;

void describe(string | func<void> x) {
    if(typeof(x) == eType.routine) {
        func<void> f = (func<void>)x;
        f();
    } else {
        print((string)x);
    }
}
```

### 2.8.1 `typeof` and `eType`

**Syntax**

```syntax
typeof( ⟨expression⟩ )
```

**Description**

`typeof` returns the run-time machine category of any value, including a `var`, as an `eType`. It is
a function declared in the core, needs no `#include`, and works on both targets.

`typeof` reports machine categories, not source types:

- `bool`, `char` and enumeration values are represented as `int` and report `eType.int`. Two union
  members that share a representation, such as `int | bool`, cannot be told apart by `typeof`.
- A scalar that happens to equal a valid object number, or a string or routine address, is reported as
  that reference category. A union mixing a scalar with a reference type, such as `int | string`, must
  be discriminated by the author's own test, then narrowed with a cast.
- `nothing` and `null` report `eType.unknown`, as does any value whose category cannot be determined.

Objects and classes report `eType.object` and `eType.class`; a value's class is tested with
`x.is(SomeClass)` (§21.5.6).

**Example**

The core declares `eType` as follows; it is shown for reference, and a program does not declare it.

```bgl
enum eType { unknown = 0, int, string, routine, object, class }
```

```bgl
if(typeof(x) == eType.routine) …
switch(typeof(x)) { case eType.string: …  case eType.object: … }
```

**See also** §21.5.9.

### 2.8.2 Named Unions

**Syntax**

```syntax
union ⟨name⟩ = ⟨type⟩ | ⟨type⟩ [ | ⟨type⟩ … ] ;
union ⟨name⟩ = ⟨type⟩ | ⟨type⟩ [ | ⟨type⟩ … ] { ⟨member⟩ … }
extend ⟨name⟩ { ⟨member⟩ … }
```

The `|` here is the union type operator, a literal token.

**Description**

A named union gives a union a name and a place for members. It must have at least two distinct member
types and takes no qualifiers. The bodyless form may be given members later with `extend`.

Compatibility is hybrid: for assignment and passing, a named union is transparent (a
`stringOrRoutine` is a `string | func<void>`, and satisfies a parameter of that anonymous type); for
member lookup it is nominal (only a value statically typed as the named union sees its members).

Members are emitters (§7.2), inlined by static type, or `static` methods. Printing a named union is
provided by a global `print(⟨name⟩)` overload rather than a member, because `print` dispatches on the
argument's static type (§21.4).

**Example**

```bgl
union stringOrRoutine = string | func<void> ;

void show(stringOrRoutine v) {              // also accepts a string | func<void>
    if(typeof(v) == eType.routine) { func<void> f = (func<void>)v; f(); }
    else print((string)v);
}
```

**Notes**

The library bindings ship `stringOrRoutine`, with its `print` overload and an `isRoutine` member, for
the I6 "string-or-routine" properties such as `description` (§21.5.10, §23.3.7).

## 2.9 Function Types

**Syntax**

```syntax
func< ⟨return type⟩ [ , ⟨parameter type⟩ … ] >
```

The `<` and `>` here are literal tokens enclosing the type arguments.

**Description**

`func<>` is the type of a value that refers to a function. The first type argument is the return
type; the remaining arguments are the parameter types in order. A function with no parameters has type
`func<⟨return type⟩>`; one returning nothing has `func<void, …>`.

`func<>` is valid as a variable, parameter, return and member type, and as the element type of a
generic collection, including nested forms such as `array<func<T>>`. A value of `func<>` type is
called with ordinary call syntax, including through a member (`obj.handler(3)`) and through a
`for…in` loop variable. The bare name `func` is compatible with every `func<…>` type (§2.11).

Function values are written as named functions (§6.1) or as lambda literals; lambda syntax and
variable capture are in §4.14.

**Example**

```bgl
func<void, int> printer;    // takes one int, returns nothing
func<int, int>  doubler;    // takes one int, returns int
func<void>      callback;   // takes nothing, returns nothing

array<func<eVerdict>> rulebook = { ruleA, ruleB };
for(func<eVerdict> r in rulebook) { eVerdict v = r(); … }
```

## 2.10 Class Types as Values

**Description**

Value and reference semantics concern classes with stored members: what a variable of the type holds,
and what assignment copies. A class that does not derive from `object` (or otherwise from the
runtime's root class `_bglObject`, §21.5.8) is a **value class**. A variable whose type is a class
with stored members holds either the members themselves or a reference to an instance owned elsewhere:

- A local of a value class type has **value semantics**: its members are zero-initialized at routine
  entry, and assignment dispatches `operator =` on the class, copying members rather than aliasing. If
  such a class has stored members and declares no `operator =`, assigning into a local of that type is
  a compile-time error.
- A class derived from `_bglObject`, including every class derived from `object`, has **reference
  semantics**: the variable holds the instance, and assignment makes the variable refer to the
  right-hand instance.
- A class with no stored members has nothing to copy, and neither semantics applies. The veneer
  classes `int`, `bool`, `char` and `string` (§8.2.5) derive from `_bglObject` but store nothing: a
  variable of such a type holds the bare word, and assignment copies the word.
- A local declared `ref` opts into reference semantics regardless of its class, and is bound with
  `:=` (§3.7).

Class parameters follow the same model; a `byVal class` opts a whole class into value semantics for
parameter passing (§8.2.7).

**Example**

```bgl
class Vec2 {
    int x = 0; int y = 0;
    void operator = (Vec2 v) { x = v.x; y = v.y; }
}

Vec2 unit;

void doMath() {
    Vec2 v;              // x = 0, y = 0 on entry
    unit.x = 1;
    v = unit;            // operator = copies the members
}
```

## 2.11 Type Compatibility

Compatibility is checked at every assignment, declaration initializer and function-call argument. A
value of type `A` is compatible with a target of type `B` when any of the following holds, tested in
order:

1. **`var`** — either side is `var` (§2.6).
2. **`null`** — the value is `nothing` or `null` (§2.5).
3. **Assignment operator** — `B` declares an `operator =` that accepts `A`. Candidates are tried in
   this order, and the first found is used:
   1. an emitter whose parameter type is exactly `A`;
   2. an emitter whose parameter type is `var`;
   3. an emitter whose parameter type is the same generic type as `A` with different type arguments
      (`operator = (array<T>)` accepts an `array<int>`);
   4. a non-emitter whose parameter type is exactly `A`;
   5. a non-emitter whose parameter type is `var`;
   6. an emitter whose parameter type is a base class of `A`;
   7. only when `A` is not otherwise compatible with `B`: an emitter, then a non-emitter, whose
      parameter type `A` is compatible with under these rules (this is how a literal reaches an
      `operator = (int)`).
4. **Exact match** — `A` and `B` are the same type.
5. **Class hierarchy** — `A` inherits from `B`, directly or through a chain of base classes and
   aliases. The reverse is not compatible: an `object` cannot be assigned to a `Room`.
6. **Conversion operator** — `A` declares an implicit `operator()` returning `B` (§2.12); an emitter
   conversion is preferred to a non-emitter one.
7. **Generics** — `func` is compatible with every `func<…>`; `array` is compatible with every
   `array<T>`.
8. **`bnum` widening** — `A` is a `bnum` and `B` is `int` (§2.7.3).
9. **Unions** — `B` is a union with a member compatible with `A`, or both are unions and every member
   of `A` is compatible with some member of `B` (§2.8).

If none holds, the assignment or call is a compile-time error.

**Example**

```bgl
class Animal : object { }
class Dog : Animal { }
class Celsius { int degrees = 0; int operator () { return degrees * 9 / 5 + 32; } }
bnum itemFlag { portable, fragile }
Dog rex { }
void run(func f) { … }

var any = 5;         int a = any;             // 1: var on either side
object o = nothing;                            // 2: nothing is compatible with every type
int n = 42;                                    // 3: int declares operator = (intLiteral)
itemFlag f = fragile; itemFlag g = f;          // 4: exact match
Animal pet = rex;                              // 5: class hierarchy; `Dog d = pet;` is an error
Celsius t;           int degrees = t;          // 6: conversion operator
func<int, int> fn;   run(fn);                  // 7: func accepts every func<…>
int bits = portable | fragile;                 // 8: a bnum widens to int
string | func<void> u = "hello";               // 9: a member of the union
```

## 2.12 Conversion

**Description**

A type may declare a **conversion operator**, `operator()`, returning another type; the declaration
syntax is in §9.4. A conversion is **implicit** by default: the compiler applies it during
assignment, argument matching and operator resolution. A conversion qualified `explicit` is applied
only at a cast site, `(⟨type⟩)expr`.

A **pass-through conversion**, declared without a body, leaves the value unchanged and merely retypes it.
A conversion written as a regular method rather than an emitter also fires on a bare read of a member
of that type, which is the basis of property accessors (§9.9).

Beyond conversion operators, a cast is required to narrow a union (§2.8), to convert `int` to a
`bnum` or an `enum` to `int` (§2.7.3), and to convert between `int` and `float` (§2.3). The full cast
syntax is in §4.11.

**Priority.** The order in which an assignment operator, a conversion operator and the other
compatibility rules are tried is stated once, in §2.11. Among overloads, an exact match wins over a
conversion match, which wins over a `var` fallback (§6.4).

**Example**

```bgl
class Celsius {
    int degrees = 0;
    int operator () { return degrees * 9 / 5 + 32; }          // implicit: Celsius → int
    explicit string operator () { return "a temperature"; }   // explicit: only under a cast
}

Celsius t;
int n = t;              // implicit conversion
string s = t;           // compile-time error
string u = (string)t;   // explicit cast
```
