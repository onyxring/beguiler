# 9 Operators and Accessors

<!-- toc -->
- [9.1 Overloadable Operators](#91-overloadable-operators)
- [9.2 Emitter and Non-Emitter Operators](#92-emitter-and-non-emitter-operators)
- [9.3 Subscript: `operator []` and `operator []=`](#93-subscript-operator-and-operator-)
- [9.4 Conversion: `operator ()`](#94-conversion-operator-)
- [9.5 Special Operators](#95-special-operators)
  - [9.5.1 `operator auto()`](#951-operator-auto)
- [9.6 `static` Operators and Three-Way Comparison](#96-static-operators-and-three-way-comparison)
- [9.7 Compound Assignment and Increment Fallback](#97-compound-assignment-and-increment-fallback)
- [9.8 Emitter-Required Operators](#98-emitter-required-operators)
- [9.9 Property Accessors](#99-property-accessors)
  - [9.9.1 Getters and Setters](#991-getters-and-setters)
  - [9.9.2 Inline Accessors: `auto { … }`](#992-inline-accessors-auto-)
  - [9.9.3 `outer`](#993-outer)
<!-- /toc -->

Operator overloading and property accessors are the two ways a class gives its values behavior at sites which are not method calls.  An overloaded operator supplies the meaning of `a + b`, `x == y`, `buf[i]`, or a conversion for values of the class; a property accessor runs code on every plain read or write of a member.

Both are class members and follow the member rules of §8, and each comes in an emitter form (§7) and a regular-method form.

## 9.1 Overloadable Operators

**Syntax**

```syntax
[ emitter | static ] TYPE operator OP ( [ PARAMETER , … ] ) { BODY }
```

An operator can be overloaded on any class, as an **emitter** (§7.2) or as a **regular method**.  The operator symbol takes the place of the method name; the left operand is the receiver and the right operand, if any, is the parameter.  When `a op b` is compiled, `operator op` is looked up on the type of `a` with a parameter matching the type of `b`, walking the inheritance hierarchy (§8.6).  The symbols which can follow `operator` are:

| Group | Operators | Declaration shape | Result |
|---|---|---|---|
| Assignment | `=` | `T operator = (U v)` | Author's choice |
| Arithmetic | `+` `-` `*` `/` `%` | `T operator + (U v)` | Author's choice |
| Comparison | `==` `!=` `=~` `<` `>` `<=` `>=` `?=` | `eBool operator == (U v)`; `==` also `static eBool operator == (T a, U b)` | `eBool` |
| Three-way | `<=>` | `int operator <=> (U v)` or `static int operator <=> (T a, U b)` | `int` (negative / 0 / positive) |
| Logical | `&&` `\|\|` | `eBool operator && (U v)` | `eBool` |
| Bitwise, shift | `&` `\|` `^` `<<` `>>` | `T operator & (U v)` | Author's choice |
| Compound assignment | `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | `T operator += (U v)` | Author's choice |
| Increment, decrement | `++` `--` `prefix++` `prefix--` | `T operator ++ ()` | Author's choice |
| Logical not | `!` | `eBool operator ! ()` | `eBool` |
| Subscript | `[]` `[]=` | `E operator [] (int i)`, `R operator []= (int i, E v)` | Element type; author's choice |
| Conversion | `()` | `[explicit] T operator ()` | The target type |
| Query | `?` | `emitter eBool operator ? ()` | `eBool`; emitter only |
| Switch comparison | `switch` | `emitter eBool operator switch (U v)` | `eBool`; emitter only |
| Auto inference | `auto` | `T operator auto();` | §9.5.1 |

`prefix++` and `prefix--` name the prefix forms (`++n`), while `++` and `--` name the postfix forms, so a class can give each its own behavior.  Declaring `operator` with any symbol outside this set is a compile-time error.

The valid operator tokens `?.`, `??`, `=>`, the reference-binding operator `:=` (§3.7), the identity comparison `:==` (§4.6), and the global-scope qualifier `::` (§3.9) are not overloadable.

**Choosing `operator =`.**  An assignment `x = v` uses the `operator =` of `x`'s class (or one it inherits) whose parameter is the type of `v` or, failing that, the nearest ancestor of that type.  Every class inherits a root assignment from its root, `_bglObject` or `_bglPrimitive` (§21.5.8).  An operator whose parameter is `x`'s class or one of its ancestors assigns a value *of that class*, so it applies only when `v` is one.  An `operator =` taking another type is a conversion (a setter's `operator = (int)`).  One taking `var` would never be chosen, and declaring it is a compile-time error.

An `operator =` which takes the class itself (or an ancestor) is a **copy operator** (§8.2.7, §8.2.8).

**See also** §4.4 (binary operator resolution); Appendix C (the same table as a quick reference).

## 9.2 Emitter and Non-Emitter Operators

An emitter operator substitutes its body at each use site, with `$self`/`$val` bound to the left operand and `$paramName` to the right (§7.3).

A non-emitter operator is a method called at each use site, with the right operand as its argument.

**Example**

```bgl
class Counter {
    int value = 0;
    emitter Counter operator ++ (){ $self.value++ }
    emitter bool    operator == (Counter v){ $self.value == $v.value }
}

class Animal : object {
    int id;
    replace bool operator == (Animal other){ return self.id == other.id; }
}
```

## 9.3 Subscript: `operator []` and `operator []=`

**Syntax**

```syntax
ELEMENT-TYPE operator [] ( int INDEX ) { BODY }
TYPE operator []= ( int INDEX , ELEMENT-TYPE VALUE ) { BODY }
```

`[` and `]` are literal:  they are part of the operator name.

`operator []` takes the index and returns the element type.  `operator []=` takes the index and then the value, and returns a type of your choosing, commonly the assigned type.  A subscript assignment is a statement, like any assignment (§5.5), so the returned value is not read.

Either can be an emitter or an ordinary method.  Any class can declare them, and a subclass inherits them.  The result of a subscript read supports member access, resolved against the element type.

**Example**

<!-- doctest: skip -->
```bgl
extern class myBuf {
    emitter var  operator []  (int i)        { $val-->$i }
    emitter void operator []= (int i, var v) { $val-->$i = $v }
}
myBuf buf;
var x  = buf[3];
buf[3] = x + 1;

array<Room> rooms = { kitchen, hall };
string s = rooms[0].description;        // member access on the element type
```

**See also** §12.3 (subscripts on `array<T>`).

## 9.4 Conversion: `operator ()`

**Syntax**

```syntax
[ explicit ] emitter TYPE operator ( ) ;
[ explicit ] emitter TYPE operator ( ) { I6-TEMPLATE }
[ explicit ] TYPE operator ( ) { STATEMENT … }
```

A zero-parameter `operator ()` declares that a value of this class converts to `TYPE`.  The rules which decide *when* a conversion is applied are in §2.10.1 and §2.10.2.

- By default, a conversion is **implicit** and is applied automatically wherever the rules of §2.10.1 allow.  An `explicit` conversion applies only at an explicit cast `(T)expr` (§4.11).  `explicit` is valid only on `operator ()` and can't be combined with `const` or `static`.
- The emitter form substitutes its body with `$self`/`$val` bound to the source value.
- The regular-method form runs at runtime and also fires on a **bare read** of a member of this type (in an initializer, an argument, or an expression), not only at cast sites.  This is the getter half of a property accessor (§9.9.1).

**Example**

```bgl
class MyType {
    emitter int operator ();                   // implicit
    explicit emitter string operator ();       // only via (string)x
}
MyType t { }
int    n = t;              // OK
string s = (string)t;      // OK
string e = t;              // compile-time error: the conversion is explicit

class heightProxy {
    int _val = 0;
    int operator (){ return _val; }            // regular method: fires on bare read
}
```

> ***Note:** `emitter T operator ();` (semicolon, no body) is the pass-through conversion; it is equivalent to `emitter T operator () { $val }`.*

## 9.5 Special Operators

The special operators, and where each one is used, are as follows:

| Operator | Use site | Description |
|---|---|---|
| `operator ? ()` | `x?`, `x?.m`, `x ?? y` | Defines what "present" (non-null) means for the type, returning `eBool`.  A type without it can't use any of the three; doing so is a compile-time error.  Must be an emitter. |
| `operator switch (U v)` | `switch(x){ case v: }` | The comparison applied to each `case` value.  Must be an emitter.  Statement semantics:  §5.12. |
| `operator ?= (U v)` | `x ?= y` | A binary comparison at equality precedence with result `eBool`.  No built-in type defines it; a class gives it a meaning. |
| `operator <=> (U v)` | `x <=> y` | Three-way comparison, result `int`:  negative when the left operand orders first, `0` when equivalent, positive otherwise.  Declarable `static` or as an instance operator (§9.6). |
| `operator ! ()` | `!x` | Prefix logical not. |

`null` is a built-in value (§2.5); however, what counts as present is type-defined:  a type takes part in `?`, `?.`, and `??` only through its `operator ? ()`.  The expression semantics of `?`, `?.`, and `??` are in §4.10.

**Example**

<!-- doctest: skip -->
```bgl
extern class object {
    emitter eBool operator ? () { $self ~= nothing }
}
```

### 9.5.1 `operator auto()`

**Syntax**

```syntax
TYPE operator auto() ;
```

When a variable is declared with `auto` (§3.6), its type is normally the resolved type of its initializer.  A class which declares `operator auto()` overrides this:  the declared return type is the type `auto` infers for a value of that class.

- Takes no parameters and has no body; only the return type matters.
- At most one per class; a duplicate is a compile-time error.
- A type without `operator auto()` infers as itself.

The literal pseudo-types declare it so that `auto x = 5;` will infer `int` rather than `intLiteral`:

| Literal type | `operator auto()` returns |
|---|---|
| `intLiteral` | `int` |
| `charLiteral` | `char` |
| `dictionaryWordLiteral` | `dictionaryWord` |

**Example**

<!-- doctest: skip -->
```bgl
primitive class intLiteral {
    emitter int operator();     // implicit conversion to int
    int operator auto();        // auto infers int
}
```

**See also** §2.4 (literal pseudo-types), §5.9.1 (`auto` in `for-in`).

## 9.6 `static` Operators and Three-Way Comparison

**Syntax**

```syntax
static TYPE operator OP ( TYPE LEFT , TYPE RIGHT ) { STATEMENT … }
static TYPE NAME ( [ PARAMETER , … ] ) { STATEMENT … }
```

A `static` operator takes **both** operands as parameters and has no receiver.  It is a free routine rather than a member of an instance, which is why it can be declared on an `extern`, `emitter`, or `alias` class, none of which allow other non-emitter methods.  `static` can't be combined with `emitter` or `const`.

Where a type declares both an instance and a static form of one operator, the instance form is used at ordinary use sites and the static form where generic code needs an address; which form `array<T>` needs from its element type is in §12.10.

`<=>`, like `==`, is declared `static` or as an instance operator with the left operand as its receiver.

A type with `<=>` but without `<`, `>`, `<=`, or `>=` gets the missing ones from it:  `a < b` means `(a <=> b) < 0`, and likewise for the others.  An ordering operator the type declares, or inherits, takes precedence.

`==` and `!=` are never derived from `<=>`, and `!=` is not derived from `==`.  A class which declares `operator ==` without `operator !=` keeps the inherited `!=`, which compares identity.  The compiler will warn about such a class; declare both.

**Example**

```bgl
#include <array>
extern int compareText(string a, string b);     // an I6 routine: negative, 0 or positive

extend class string {
    static bool operator ==  (string a, string b) { return compareText(a, b) == 0; }
    static int  operator <=> (string a, string b) { return compareText(a, b); }
}

// … in a routine:
array<string> names = { "cherry", "apple", "banana" };
names.indexOf("apple");                             // -> 1, via string's ==
names.sort();                                       // apple, banana, cherry, via <=>

string a = "pear";
string b = "plum";
int r = a <=> b;                                    // -1, 0, or +1
names.sort((string a, string b) => a <=> b);
```

**See also** §4.15 (`Type::operator op`; only a `static` operator is referenceable from ordinary code); §3.12 (`static superposed` methods).

## 9.7 Compound Assignment and Increment Fallback

When no `operator op=` is declared for a compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`), `n op= v` is expanded to `n = n op v`.  Likewise, when no `operator ++`/`--` (or `prefix++`/`prefix--`) is declared, `n++` and `n--` become `n = n + 1` and `n = n - 1`.

**Example**

<!-- doctest: skip -->
```bgl
n += 2;         // -> n = n + 2;  when n's type declares no operator +=
```

**See also** §5.6 (the compound-assignment statement); §5.7 (increment and decrement).

## 9.8 Emitter-Required Operators

The following must be declared with `emitter` (implied inside an `emitter class`); declaring any of them as a regular method is a compile-time error:

| Declaration | Reason |
|---|---|
| `operator ? ()` | Substituted as the null test in `?.`, `??`, and postfix `?` |
| `operator switch (U v)` | Substituted as the comparison of each `case` |

The instance forms of `init()` and `deinit()` must be emitters too (§8.5).

## 9.9 Property Accessors

A **property accessor** is a member which reads and writes like a plain member but runs code on each access.  It is built from a regular-method `operator ()` (the getter, §9.4) and an `operator =` (the setter) declared on a struct (§8.2.7), which is then used as an owned member (§8.3.4) of the host class or object.  A read of the member dispatches the getter; a write dispatches the setter, never the getter.

An operator the accessor class declares applies to the member itself, ahead of the getter (§4.4).  An ordering the class gets from its `<=>` counts as declared.  An operator the class doesn't declare works on the value the getter reads.

Accessors work on single objects, on every instance of a class, and on pooled instances obtained with `new`.  An accessor is declared either as a named class which then types the member (§9.9.1) or in place with `auto { … }` (§9.9.2).

### 9.9.1 Getters and Setters

**Syntax**

```syntax
struct ACCESSOR {
    [ TYPE NAME [ = VALUE ] ; … ]
    TYPE operator ( ) { STATEMENT … }
    TYPE operator = ( TYPE VALUE ) { STATEMENT … }
}
ACCESSOR MEMBER ;
```

The first form declares the accessor class:  optional backing members, a getter, and a setter.  The second form declares the accessor as an owned member of a host class or object.

- The getter is a regular-method `operator ()` with no parameters.  It runs on every bare read of the member (§9.4), and its return type is the type the member reads as.
- The setter is an `operator =` taking one parameter.  It runs on every assignment to the member; the assigned value is its argument.
- The accessor class must be a struct (§8.2.7) with at least one stored member, and the member must be declared without an initializer or `ref`, so that it is owned (§8.3.4); otherwise the member refers to an instance owned elsewhere and no accessor dispatch occurs.  An in-place accessor (§9.9.2) is a struct implicitly.
- A struct variable of the accessor's own type which is assigned or initialized from the member (`heightProxy h = t.height;`) is a copy of the accessor, backing members included; the getter doesn't run.
- Inside either operator, `self` is the accessor instance and its backing members.

**Example**

```bgl
struct heightProxy {
    int _val = 0;
    int  operator ()        { return _val; }
    void operator = (int v) { _val = v * 2; }
}
class thing : object { heightProxy height; }
thing t {}

// … in a routine:
t.height = 5;               // setter: _val -> 10
int a = t.height;           // getter: a -> 10
```

**See also** §8.7.4 (`hide member.operator =;` makes an accessor read-only on a subtype).

### 9.9.2 Inline Accessors: `auto { … }`

**Syntax**

```syntax
auto MEMBER = { ACCESSOR-BODY } ;
```

`ACCESSOR-BODY` is the class body of §9.9.1:  backing members, a getter, and a setter.

`auto name = { … }` declares the accessor in place, inside a class or object body.  `auto`, not `var`, is required, since the type is inferred from the body.  The braces are read as a class body, rather than as a `{ v1, v2 }` initializer list, when they begin with a member declaration or an `operator`.

**Example**

```bgl
object gadget {
    auto level = {
        int _raw = 0;
        int  operator ()        { return _raw; }
        void operator = (int v) { _raw = v * 3; }
    }
}

// … in a routine:
gadget.level = 4;           // _raw -> 12
int n = gadget.level;       // n -> 12
```

> ***Note:** `auto level = { int _raw = 0; int operator (){ … } void operator = (int v){ … } }` is equivalent to declaring a hidden class with that body and writing `HiddenClass level;`.*

### 9.9.3 `outer`

**Syntax**

```syntax
outer . MEMBER
```

Inside an accessor body two receivers are available:  `self`, the accessor instance and its own backing members; and `outer`, the host object the accessor is declared on.  `outer` is resolved at compile time and can read and write the host's other members.

`outer` is available only inside an inline `auto { … }` accessor (§9.9.2) declared directly in an object body (§11.2).  It is a compile-time error in a named accessor class (§9.9.1), and in an inline accessor declared in a class body.

**Example**

```bgl
object gadget {
    int scale = 3;
    auto level = {
        int _raw = 0;
        int  operator ()        { return _raw; }
        void operator = (int v) { _raw = v * outer.scale; }
    }
}

// … in a routine:
gadget.level = 4;           // _raw -> 12
```
