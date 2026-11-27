# 2 Types and Values

<!-- toc -->
- [2.1 Overview](#21-overview)
- [2.2 Primitive Types](#22-primitive-types)
- [2.3 The `float` Type](#23-the-float-type)
- [2.4 Literal Pseudo-Types](#24-literal-pseudo-types)
  - [2.4.1 `negativeIntLiteral`](#241-negativeintliteral)
  - [2.4.2 `interpolatedStringLiteral`](#242-interpolatedstringliteral)
- [2.5 `null`](#25-null)
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
  - [2.9.1 `interpolatedString`](#291-interpolatedstring)
- [2.10 Type Rules](#210-type-rules)
  - [2.10.1 Type Compatibility](#2101-type-compatibility)
  - [2.10.2 Conversion](#2102-conversion)
  - [2.10.3 Primitive, Reference, and Value Types](#2103-primitive-reference-and-value-types)
- [2.11 `selfType`](#211-selftype)
<!-- /toc -->

## 2.1 Overview

Beguile is statically typed:  every variable, parameter, and return value has a type known when you compile.  Types fall into four categories:  primitive types (§2.2), literal pseudo-types (§2.4), user-defined types (enumerations §2.7, unions §2.8, classes §8), and the `var` escape type (§2.6).  The `array` type gets a chapter of its own (§12).

§2.10 covers the rules which apply across all types.

## 2.2 Primitive Types

The primitive types are built in; there is nothing to `#include`.

| Type | Description |
|---|---|
| `int` | Signed integer, one native word:  16 bits on the Z-machine (-32768 to 32767), 32 bits on Glulx. |
| `uint` | Unsigned integer (§21.6.1). |
| `float` | IEEE 754 single-precision floating point.  `[Glulx]` See §2.3. |
| `bool` | A true/false type, backed by the enumeration `eBool` (§2.7.4). |
| `char` | A single character:  its ZSCII code on the Z-machine, its Unicode code point on Glulx (Appendix D). |
| `string` | A reference to static text. |

`object` (§11.1) and `verb` (§13.2) are classes, and `void` is a return type (§6.2).

`==` and `!=` between two `string`s (a literal included) compare the references, not the text:  two literals with the same text are two strings, and compare unequal.  The compiler warns about such a comparison, unless the program has replaced `string`'s operator (§8.7.2).  Comparing their values as integers (`(int)a == (int)b`) asks for the references on purpose, without the warning.  `<string>` adds `equals()` to compare text (§22.3).

## 2.3 The `float` Type

A `float` occupies one native word, the same width as `int`, holding an IEEE 754 single-precision value.  It supports the arithmetic operators `+` `-` `*` `/` `%` and their compound-assignment forms, the comparisons `==` `!=` `<` `<=` `>` `>=`, and `print()`.

A `float` is initialized from a float literal (§1.6.2) or from an `int`, which converts implicitly, preserving its value (`5` becomes `5.0`); an `int` is never reinterpreted bit-for-bit as a `float`.  Converting a `float` to an `int` always requires a cast, `(int)f`, which truncates toward zero.

**Mixed arithmetic.**  If either operand of an arithmetic operator is a `float`, the other is converted to `float` and the result will be a `float`; two `int` operands give an `int`.  The comparison operators convert the same way, although they yield `bool`.

`print()` writes a `float` as a decimal rounded to four places:  `2.0 / 3` prints `0.6667`, and `0.99999` prints `1.0000`.

**Example**

```bgl
#beguilerSettings { target = Glulx; }

void Main() {
    float a = 2.5;
    float b = 5;                    // -> 5.0
    float c = (float)314 / 100;     // -> 3.14: one float operand makes the division float
    int   d = 314 / 100;            // -> 3: two ints divide as integers
    print(b / a);                   // -> 2.0000
    if(b > a) print("^bigger^");
    int n = (int)c;                 // -> 3
}
```

> ***[Glulx]:** `float` is available only when the target is Glulx; the Z-machine simply has no floating-point support.*

## 2.4 Literal Pseudo-Types

A literal has a **pseudo-type**, which the compiler infers from the way it is written.  The names are written mostly to extend one, or to declare an overload which accepts one.  `auto` infers a pseudo-type's corresponding type when the pseudo-type declares `operator auto()` (§9.5.1); `stringLiteral` doesn't, so `auto s = "hello";` declares a `stringLiteral` (§3.6).  Pseudo-types take part in operator and overload resolution independently of the runtime types they correspond to.

| Pseudo-type | Written | Corresponding type |
|---|---|---|
| `intLiteral` | `42`, `$FF`, `$$1010` | `int` |
| `negativeIntLiteral` | `-1` | `int` |
| `stringLiteral` | `"hello"`, `@"raw"` | `string` |
| `charLiteral` | `'a'` | `char` |
| `dictionaryWordLiteral` | `.cloak`, `..cloaks` | `dictionaryWord` (§21.5.3) |
| `interpolatedStringLiteral` | `$"hello {x}"` | none; see §2.4.2 |

A pseudo-type converts to its corresponding type only through a conversion operator on the pseudo-type (`intLiteral` declares one to `int`) or an `operator =` on the target type which accepts it (`float` declares `operator = (intLiteral)`); §2.10.1 gives the order.

Pseudo-types are first-class types, declared as `primitive class` (§8.2.5) and extensible with `extend` (§8.7.1).  As such, a method defined against one can be called directly on a literal.

**Example**

```bgl
extend class intLiteral {
    emitter void twice(){ print $val * 2; }
}

// … in a routine:
"hello".print();       // a method on stringLiteral
42.twice();            // -> 84
```

### 2.4.1 `negativeIntLiteral`

A negated integer literal such as `-1` has the pseudo-type `negativeIntLiteral` rather than `intLiteral`.  It converts to `int` implicitly; however, it is not compatible with `uint` (§2.2), and assigning one to a `uint` requires an explicit cast.

**Example**

```bgl
int  x = -5;         // -> -5
uint u = -1;         // compile-time error
uint v = (uint)-1;   // the largest unsigned value
```

### 2.4.2 `interpolatedStringLiteral`

An interpolated string (§1.6.5) has no corresponding runtime type.  Its literal type, `interpolatedStringLiteral`, can only be passed to an emitter which declares a parameter of that type, where it expands in place into a block of statements; passing it to a non-emitter function's `interpolatedStringLiteral` parameter is a compile-time error.  Passed to a parameter which takes a routine (`func<void>`, `var`, or `interpolatedString`), it becomes a routine instead (§2.9.1).  `print()` and `bgl.output.log()` accept it (§21.4).  The call must be a statement of its own; a call inside an expression is a compile-time error.  An expression-bodied lambda whose body is such a call, `=> print($"…")`, *is* that statement (§4.14).

## 2.5 `null`

`null` is the absent or unset value, numerically `0`.

Its type (§4.1) is the literal pseudo-type `nullLiteral`, which is compatible with every type.

A struct can't hold `null` (§8.2.7).

> ***Note:** Inform 6's `nothing` is not a Beguile word; Beguile writes `null`.*

**Example**

<!-- doctest: skip -->
```bgl
object o;
if(o == null) print("not yet set");

marbleClass m = new marbleClass();
if(m == null) print("pool exhausted");
```

> ***Note:** `null` is emitted as the Inform 6 constant `nothing`, which is what you will see in generated code and what is written in emitter bodies (§7).*

## 2.6 The `var` Type

`var` is a universal escape type which bypasses static type checking in both directions:  any value can be assigned to a `var`, and a `var` can be assigned to any type.

In overload resolution an overload with `var` parameters is a fallback, selected only when no typed overload matches (§6.4).

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
enum NAME { MEMBER [ = INTEGER ] [ , MEMBER [ = INTEGER ] … ] }
```

An `enum` declares a named set of integer constants.  Values start at 1 and automatically increment by 1.  A member can be given an explicit value, including a negative one, and numbering resumes from the last assigned value.  Two members can hold the same value, although they are then indistinguishable at run time.

Enumerations can carry emitter methods, declared in the body or added with `extend enum` (`extend bnum` for a bnum); §7.8 covers the form.

**Example**

```bgl
enum direction { north, south, east, west }   // -> 1, 2, 3, 4

enum myPhase {
    setup  = 0,
    play   = 10,
    ending           // -> 11
}
```

### 2.7.2 `bnum`

**Syntax**

```syntax
bnum NAME [ : BASE-BNUM ] { MEMBER [ = INTEGER ] [ , MEMBER [ = INTEGER ] … ] }
```

A `bnum` is a bit-flag enumeration.  Values start at 1 and double with each member, so the members are non-overlapping powers of two, suitable for combining as flags.  An explicit value must be a non-negative power of two, and numbering resumes by doubling from it.  `0` is always a legal value.

**Shared base.**  A `bnum` can name a base `bnum`, declaring that it occupies a sub-field of the same packed integer.  The base must itself be a `bnum`.  Within a shared base the power-of-two rule is relaxed, since a sub-field's members need not be single bits.

**Combining values.**  Bitwise `|`, `&`, and `^` between two `bnum` values are allowed only when the operands share a common `bnum` ancestor (one can be the other's ancestor, or both the same type).  The result has the shared ancestor's type.  Combining values with no common base is a compile-time error.

**Example**

```bgl
bnum itemFlag { portable, fragile, lit, locked }   // -> 1, 2, 4, 8

bnum winMethodFlags { }                          // empty base: defines the bit-field space
bnum windowPlacement : winMethodFlags { left = 0, right = 1, above = 2, below = 3 }
bnum windowScale     : winMethodFlags { fixed = 16, proportional = 32 }
bnum windowBorder    : winMethodFlags { border = 0, noBorder = 256 }

int winMethod = left | fixed | noBorder;    // all share winMethodFlags
int bad       = left | itemFlag.portable;   // compile-time error
```

> ***[Z-machine/Glulx difference]:** A `bnum` can have at most 16 distinct values on the Z-machine and 32 on Glulx.*

### 2.7.3 Widening to `int`

A `bnum` value converts implicitly to `int`; the reverse requires an explicit cast.  A plain `enum` does not automatically widen:  converting between an `enum` and `int`, in either direction, always requires an explicit cast.

**Example**

```bgl
bnum itemFlag { portable, fragile }
enum direction { north, south }

int a = fragile;               // -> 2: a bnum widens to int
itemFlag f = (itemFlag)2;      // int to bnum: explicit cast
int b = (int)south;            // -> 2: enum to int: explicit cast
direction d = north;
int c = d;                     // compile-time error: an enum does not widen
```

### 2.7.4 `extern enum` and `extern bnum`

**Syntax**

```syntax
extern enum NAME { MEMBER [ , MEMBER … ] }
extern bnum NAME { MEMBER [ , MEMBER … ] }
```

An ordinary `enum` or `bnum` exists only in Beguile:  each use of a member is emitted as its number, and the enumeration itself never appears in the generated Inform 6.

An `extern` enumeration instead names values defined in I6.  Its members have no values in Beguile; each use of one is emitted as its name, and Inform 6 supplies the value.  `eBool`, Beguile's boolean-result type (§2.2), is declared this way.

**Example**

<!-- doctest: skip -->
```bgl
extern enum eBool { true, false }
extern enum eErrorFormat { E1, E2 }
```

### 2.7.5 Naming Members

An enumeration member can be referenced by its bare name (`north`, `true`) or qualified by its type (`direction.north`, `eBool.true`).

Where a value of an enumeration type is expected (an initializer, an assignment, an argument, an array element of that type), a bare name which is both a member of that enumeration and some other global, such as the attribute `door`, is the member.  Elsewhere the qualified form is required when two enumeration types, or an enumeration and another global, share the name.

## 2.8 Union Types

**Syntax**

```syntax
TYPE | TYPE [ | TYPE … ]
```

The `|` here is the union type operator, a literal token.

A union type declares that a value is one of several types, distinguished at run time.  It is valid at every type position:  parameter, local, return type, class or object member, and array element type.

A union type is also *inferred* in one place:  a ternary whose two branches have unrelated types resolves to their union (§4.9).

**Canonical form.**  Members are order-independent and de-duplicated:  `string | func<void>` and `func<void> | string` name the same type, and a union of a type with itself is simply that type.

**Operations.**  A union value can be assigned, passed, returned, and compared with `==` and `!=`.  It can't be called, printed, member-accessed, or used in arithmetic while it is still a union; doing so is a compile-time error.  The value must first be **narrowed** with an ordinary cast, `(MEMBER)x`, which retypes the value without converting or checking it; discriminate it first with `typeof` (§2.8.1).

**Compatibility.**  A value fits a union when it fits one of the union's members:  a `string` can be stored in a `string | func<void>`.  One union fits another when each of its members fits the other:  a `string | int` can be stored in a `string | int | func<void>`.  A union does not fit one of its member types directly; narrow it first with a cast.  §6.4 ranks an overload which takes a union.

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
typeof( EXPRESSION )
```

`typeof` returns the run-time machine category of any value, including a `var`, as an `eType`.  It needs no `#include` and works on both targets.

Note that `typeof` reports machine categories, *not* source types:

- `bool`, `char`, and enumeration values are represented as `int` and report `eType.int`.
- A scalar which happens to equal a valid object number, or a string or routine address, is reported as that reference category.  A union mixing a scalar with a reference type (e.g., `int | string`) must be discriminated by a test of your own, then narrowed with a cast.
- `null` reports `eType.unknown`, as does any value whose category can't be determined.

Objects and classes report `eType.object` and `eType.class`.  To test a value's class, use `x.is(SomeClass)` (§21.5.6).

**Example**

`eType` is declared as follows (shown for reference; a program does not declare it).

<!-- doctest: skip -->
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
union NAME = TYPE | TYPE [ | TYPE … ] ;
union NAME = TYPE | TYPE [ | TYPE … ] { MEMBER … }
extend NAME { MEMBER … }
```

The `|` here is the union type operator, a literal token.

A named union gives a union a name and a place for members.  It must have at least two distinct member types and takes no qualifiers.  The form without a body can be given members later, with `extend`.

Compatibility is a bit of a hybrid.  For assignment and passing, a named union is transparent (a `textOrRoutine` is a `string | func<void>`, and satisfies a parameter of that anonymous type); for member lookup it is nominal (only a value statically typed as the named union sees its members).

Members are emitters (§7.2), inlined by static type, or `static` methods.  A named union is printed through a global `print(NAME)` overload, not a member (§21.4).

**Example**

```bgl
union textOrRoutine = string | func<void> ;

void show(textOrRoutine v) {              // also accepts a string | func<void>
    if(typeof(v) == eType.routine) { func<void> f = (func<void>)v; f(); }
    else print((string)v);
}
```

> ***Note:** The library bindings ship `iString` and `iStringOrObject`, with a `print` overload and members reporting which type a value holds, for the properties the library prints, such as `description` (§21.5.10, §23.3.7).*

## 2.9 Function Types

**Syntax**

```syntax
func< RETURN-TYPE [ , PARAMETER-TYPE … ] >
```

The `<` and `>` here are literal tokens enclosing the type arguments.

`func<>` is the type of a value which refers to a function.  The first type argument is the return type; the remaining arguments are the parameter types, in order.  A function with no parameters has type `func<RETURN-TYPE>`; one returning nothing has `func<void, …>`.

`func<>` is valid wherever a type is, including nested forms such as `array<func<T>>`.  A `func<>` value is called like a function, wherever it is held, and so is any parenthesized expression:  `((func<void>)v)()` narrows a `var` or union value to a function and calls it.

The name of a function, written without a call, is a function value whose type is the function's signature:  `int twice(int n)` gives a `func<int, int>`.  It can be stored in, passed to, or returned as a `func<…>` of that signature; a different signature is a compile-time error.  A lambda (§4.14) is checked the same way:  its parameter types are as written, and its return type is inferred from its body.  A bare function name used as a value (`func<int,int> f = twice;`) must name exactly one routine, so an overloaded name is a compile-time error.  Simply wrap the overload you want in a function of its own and reference that.

A global function or a `static` method is a function value (`func<int, int> f = Calc.twice;`).  A non-static method of an object or class is not, since it needs its object.  Naming one without a call is a compile-time error, and the compiler will suggest a lambda which supplies the object:  `(int n) => fx.succ(n)`.

**Example**

```bgl
func<void, int> printer;    // takes one int, returns nothing
func<int, int>  doubler;    // takes one int, returns int
func<void>      callback;   // takes nothing, returns nothing

int twice(int n) { return n * 2; }
doubler = twice;            // a function name is a func value
int x = doubler(4);         // -> 8: called through a variable
obj.handler(3);             // called through a member

array<func<eVerdict>> rulebook = { ruleA, ruleB };
for(func<eVerdict> r in rulebook) { eVerdict v = r(); … }   // called through a loop variable
```

### 2.9.1 `interpolatedString`

Under the covers, an `interpolatedString` is code which evaluates and prints values at runtime:  a string literal prefixed with `$` defines one.  Conceptually, it's a string with values which are reevaluated each time it prints, *not* when it is defined, so printing the same `interpolatedString` multiple times may produce different results.  The parts print left to right as they are reached, so a call in a span which prints output of its own will print it at that point.

Used as a value anywhere but an emitter's `interpolatedStringLiteral` parameter (§2.4.2), an interpolated string becomes a routine which prints it each time it runs:  a `var`, an `interpolatedString`, a `func<void>`, or a type which holds one will take it that way.  Interpolated strings may be assigned to variables or properties; in such cases the actual assignment is usually a `func<void>` routine.  An `interpolatedString` is printed, not called:  `print(live)` runs it, and `live()` is a compile-time error.  Assigned or added (`+=`) to a `stringObj`, its output is captured instead, at the time it is assigned (§22.3).

As a member of an object, an interpolated string has access to all of the object's members.  It reads them when printed, `print(lamp.label)`.

In a routine's body, an interpolated string is a lambda, and captures the locals it reads as a lambda does (§4.14).

**Example**

```bgl
#include <string>

int watts = 60;

object lamp {
    int watts = 60;
    short_name = $"{watts}W lamp";        // a library property that takes a string or a routine
}

// … in a routine:
stringObj snap          = $"{watts}W";   // the text now: "60W"
interpolatedString live = $"{watts}W";   // a routine that prints it
watts = 100;

print(snap);   // -> 60W
print(live);   // -> 100W
```

## 2.10 Type Rules

The rules here apply across all types:  when a value of one type can be used where another is expected (§2.10.1), how a value converts from one type to another (§2.10.2), and what assigning a class-typed value does (§2.10.3).

### 2.10.1 Type Compatibility

Compatibility is checked at every assignment, declaration initializer, and function-call argument.  A value of type `A` is compatible with a target of type `B` when any of the following rules holds, tested in order.  If none of them holds, the assignment or call is a compile-time error.

**1. `var`:**  either side is `var` (§2.6).

```bgl
var any = 5;
int a = any;
```

**2. `null`:**  the value is `null` (§2.5).

```bgl
class Dog : object { }
Dog d = null;
```

**3. Assignment operator:**  `B` declares an `operator =` which accepts `A`.  Candidates are tried in this order, and the first one found is used:

1. an emitter whose parameter type is exactly `A`;
2. an emitter whose parameter type is `var`;
3. an emitter whose parameter type is the same generic type as `A` with different type arguments (`operator = (array<T>)` accepts an `array<int>`);
4. a non-emitter whose parameter type is exactly `A`;
5. a non-emitter whose parameter type is `var`;
6. an emitter whose parameter type is a base class of `A`;
7. only when `A` is not otherwise compatible with `B`:  an emitter, then a non-emitter, whose parameter type `A` is compatible with under these rules (this is how a literal reaches an `operator = (int)`).

```bgl
float n = 42;                    // float declares operator = (intLiteral)
uint u = -1;                     // compile-time error: uint accepts no negative literal (§2.4.1)
```

**4. Exact match:**  `A` and `B` are the same type.

```bgl
bnum itemFlag { portable, fragile }
enum direction { north, south }
itemFlag f = fragile;
itemFlag g = f;
itemFlag h = north;              // compile-time error: a different enumeration
```

**5. Class hierarchy:**  `A` inherits from `B`, directly or through a chain of base classes and aliases.  The reverse is not compatible.

```bgl
class Animal : object { }
class Dog : Animal { }
Dog rex { }
Animal pet = rex;                // a Dog is an Animal
Dog d = pet;                     // compile-time error: an Animal need not be a Dog
```

**6. Conversion operator:**  `A` declares an implicit `operator()` returning `B` (§2.10.2); an emitter conversion is preferred to a non-emitter one.

```bgl
class Celsius { int degrees = 0; int operator () { return degrees * 9 / 5 + 32; } }
Celsius t { }
int degrees = t;                 // Celsius converts to int
string s = t;                    // compile-time error: no conversion to string
```

**7. Generic types:**  a function name is compatible with a `func<…>` type of the same signature (§2.9); an `array<T>` is compatible with an `array<U>` when `T` is compatible with `U`.

```bgl
void tick() { }
func<void> callback = tick;      // tick's signature is func<void>
func<int, int> bad = tick;       // compile-time error: a different signature
array<int> numbers = { 1, 2 };
array<string> words = numbers;   // compile-time error: int does not fit string
```

**8. `bnum` widening:**  `A` is a `bnum` and `B` is `int` (§2.7.3).

```bgl
bnum itemFlag { portable, fragile }
int bits = portable | fragile;   // a bnum widens to int
itemFlag back = bits;            // compile-time error: int to bnum needs a cast (§2.7.3)
```

**9. Unions:**  `B` is a union with a member compatible with `A`, or both are unions and every member of `A` is compatible with some member of `B` (§2.8).

```bgl
class Dog : object { }
Dog rex { }
string | func<void> v = "hello"; // a string is one of the members
string | func<void> w = rex;     // compile-time error: Dog fits no member
```

### 2.10.2 Conversion

A type can declare a **conversion operator**, `operator()`, returning another type (§9.4 covers the declaration syntax).  A conversion is **implicit** by default; one qualified `explicit` is applied only under a cast, as in `(string)t` below.

The other casts (narrowing a union, `int` to `bnum`, `float` to `int`) are covered with their types; the full cast syntax is in §4.11.  §2.10.1 gives the order in which conversions are tried, and §6.4 the order among overloads.

**Example**

```bgl
class Celsius {
    int degrees = 0;
    int operator () { return degrees * 9 / 5 + 32; }          // implicit: Celsius -> int
    explicit string operator () { return "a temperature"; }   // explicit: only under a cast
}

Celsius t { }
int n = t;              // implicit conversion
string s = t;           // compile-time error
string u = (string)t;   // explicit cast
```

### 2.10.3 Primitive, Reference, and Value Types

Every type can have methods (§2.4); what differs is what a variable of the type holds:

| Declaration | Kind | A variable holds | `=` | Passing and returning | `ref` |
|---|---|---|---|---|---|
| `primitive class int` | primitive | the value itself | copies the value | copies the value | compile-time error |
| `class Room` | object (reference) | its own object when declared with no value; else the instance it is given | refers to the same instance | shares the instance | a reference, empty until bound |
| `class Note` with `operator = (Note o)` | object that copies | as an object | copies into its own instance | shares the instance | a reference, empty until bound |
| `array<int> a[4]` | object that copies | its own storage | copies into its own storage | shares the array | a reference, empty until bound |
| `struct Vec2` | struct (value) | its own instance | copies into its own instance | copies | shares one instance |

- **Primitive types** (§8.2.5):  `int`, `uint`, `bool`, `char`, `float`, `string` (the address of fixed text), `dictionaryWord`, `attribute`, `property`, `bglClass` (a class name's value), the literal pseudo-types, `glulxImage`, and every enum.
- **Objects:**  instances of a class.  A global, member, or local declared with no value has an object of its own:  at file scope the declaration is that object (`Room hall;` is `Room hall { }`), a member's is created with its host, and a local's is reset at routine entry.  One declared with a value (`Room r = hall;`) refers to that instance and creates nothing; so do parameters and loop variables.  A slot which may be empty is declared `ref` (§3.7).
- **Objects that copy:**  a class which declares a copy operator (§8.2.8), and an array with storage (§12.5).
- **Structs** (§8.2.7):  a global, local, member, or parameter owns an instance of its own, whose members are zeroed (a local's at routine entry, and again by its declaration on each pass of a loop it is declared in).  A struct is never null and has no subclasses.

Classes which manage their own storage through `init`/`deinit` emitters (e.g., `stringObj`, `array<T>`) follow their own emitters (§8.5).

**Example**

```bgl
struct Vec2 { int x = 0; int y = 0; }   // copies field by field
class Marker : object { int x = 0; } // a reference class
Marker home { }
Vec2 unit;

void compare() {
    Vec2 v;              // its own instance: x = 0, y = 0 on entry
    unit.x = 1;
    v = unit;            // copies the members: v.x -> 1
    v.x = 5;             // unit.x is still 1

    Marker m = home;     // m and home are the same instance
    m.x = 5;             // home.x -> 5
}
```

## 2.11 `selfType`

**Syntax**

```syntax
selfType
```

`selfType` is written in a class's member declarations, including those of a class which other classes list as a parent (§8.2.1), to mean "the class this member is used on".  At each use it is the static class of the receiver:  the class of the variable the member is called or read through, or an object's class.  Inside the declaring class's own body it is that class.

It can be a parameter type, a return type, a member's type, or an array's element type.  It affects type checking only.

- A **parameter** of type `selfType` accepts the receiver's class or a subclass of it:  `bglAllocated.copy(selfType src)` (§21.8) copies a subclass into a base class but not the reverse.
- A **return type** keeps the subclass's type through a method a base class declares, so a chain can go on to the subclass's own methods.
- An **operator's parameter** (`int operator <=> (selfType o)`) compares a class with its own kind.
- A **member or element type** (`selfType next; array<selfType> kids[4];`) is the subclass's class on a subclass instance.

If you know TypeScript, its polymorphic `this` type is the nearest equivalent.

**Example**

```bgl
#include <array>
class Widget : object { int width; selfType wide(int n) { width = n; return self; } }
class Button : Widget { int col; selfType color(int c) { col = c; return self; } }
Button ok { }

class Node : object { int key; selfType next; array<selfType> kids[4]; }
class Room2 : Node { int area = 5; }
Room2 r1 { }
Room2 r2 { area = 3; }

void Main() {
    ok.wide(5).color(3);    // wide() returns Button here, so color() is found
    r1.next = r2;
    print(r1.next.area);    // -> 3: next is a Room2 on a Room2
}
```
