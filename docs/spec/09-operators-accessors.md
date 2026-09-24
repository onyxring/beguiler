# 9 Operators and Accessors

<!-- toc -->
- [9.1 Overloadable Operators](#91-overloadable-operators)
- [9.2 Emitter and Non-Emitter Operators](#92-emitter-and-non-emitter-operators)
- [9.3 Subscript: `operator []` and `operator []=`](#93-subscript-operator-and-operator-)
- [9.4 Conversion: `operator ()`](#94-conversion-operator-)
- [9.5 Special Operators](#95-special-operators)
- [9.6 `static` Operators and Three-Way Comparison](#96-static-operators-and-three-way-comparison)
- [9.7 Compound Assignment and Increment Fallback](#97-compound-assignment-and-increment-fallback)
- [9.8 Emitter-Required Operators](#98-emitter-required-operators)
- [9.9 Property Accessors](#99-property-accessors)
  - [9.9.1 Getters and Setters](#991-getters-and-setters)
  - [9.9.2 Inline Accessors: `auto { … }`](#992-inline-accessors-auto-)
  - [9.9.3 `outer`](#993-outer)
<!-- /toc -->

Operator overloading and property accessors are the two ways a class gives its values behavior at
sites that are not method calls. An overloaded operator supplies the meaning of `a + b`, `x == y`,
`buf[i]` or a conversion for values of the class; a property accessor runs code on every plain read
or write of a member. Both are class members and follow the member rules of §8: they are declared in
a class body, inherited (§8.6), extended, replaced and hidden (§8.7) like any other member, and each
comes in an emitter form (§7) and a regular-method form. This chapter assumes §7 and §8.

§9.1 through §9.8 specify operators: the overloadable set and the general declaration form, the
emitter and non-emitter forms, subscript, conversion, the special operators `?`, `switch`, `?=`, `<=>`
and `!`, `static` operators, the fallbacks for compound assignment and increment, and the operators
that must be emitters. §9.9 specifies property accessors, which are built from a conversion operator
and an assignment operator declared on a value class.

## 9.1 Overloadable Operators

**Syntax**

```syntax
[ emitter | static ] ⟨type⟩ operator ⟨op⟩ ( [ ⟨parameter⟩ , … ] ) { ⟨body⟩ }
```

**Description**

An operator may be overloaded on any class as an **emitter** (§7.2) or as a **regular method**. The
operator symbol takes the place of the method name; the left operand is the receiver and the right
operand, if any, is the parameter. When `a op b` is compiled, `operator op` is looked up on the type
of `a` with a parameter matching the type of `b`, walking the inheritance hierarchy (§8.6). The
symbols that may follow `operator` are:

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
| Auto inference | `auto` | `T operator auto();` | §7.8 |

`prefix++` and `prefix--` name the prefix forms (`++n`); `++` and `--` name the postfix forms, so a
class may give each its own behavior. Declaring `operator` with any symbol outside this set is a
compile-time error. The valid operator tokens `?.`, `??`, `=>`, and the reference-binding operator
`:=` (§3.7) are not overloadable; `:=` exists precisely to bypass operator dispatch.

The `==` row's `static` shape is the form a generic container calls (§9.6, §12.10).

**See also** §4.4 — binary operator resolution; Appendix C — the same table as a quick reference.

## 9.2 Emitter and Non-Emitter Operators

**Description**

An emitter operator substitutes its body at each use site, with `$self`/`$val` bound to the left
operand and `$paramName` to the right (§7.3).

A non-emitter operator is a method called at each use site, with the right operand as its argument.
Use it when the body is complex, when it should be visible to the debugger, or when code size matters.

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

> **Which overload wins.** When a type declares several overloads of one operator, the right-hand
> operand selects between them in four passes: the operand's **exact** type; the **base type of a
> literal** (`intLiteral` → `int`); a type the base is **convertible** to; and finally a parameter
> declared `var`. `var` accepts anything, so it is the last resort rather than an exact match — which
> is what lets a subclass override an operator it inherits. `object` declares
> `emitter eBool operator == (var v)`, and were `var` treated as exact, a class deriving from
> `object` could never give `==` its own meaning. An operand whose type is itself `var`, or whose
> type is unknown, still matches in the first pass; there is nothing more specific to prefer.

## 9.3 Subscript: `operator []` and `operator []=`

**Syntax**

```syntax
⟨element-type⟩ operator [] ( int ⟨index⟩ ) { ⟨body⟩ }
⟨type⟩ operator []= ( int ⟨index⟩ , ⟨element-type⟩ ⟨value⟩ ) { ⟨body⟩ }
```

`[` and `]` are literal: they are part of the operator name.

**Description**

`operator []` takes the index and returns the element type; `operator []=` takes the index and then
the value, and returns a type of the author's choice — commonly the assigned type, so the assignment
can be used as an expression. Any class may declare them, and a subclass inherits them. The result of
a subscript read supports member access, resolved against the element type.

**Example**

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

**See also** §12.3 — subscripts on `array<T>`.

## 9.4 Conversion: `operator ()`

**Syntax**

```syntax
[ explicit ] emitter ⟨type⟩ operator ( ) ;
[ explicit ] emitter ⟨type⟩ operator ( ) { ⟨i6-template⟩ }
[ explicit ] ⟨type⟩ operator ( ) { ⟨statement⟩ … }
```

**Description**

A zero-parameter `operator ()` declares that a value of this class converts to `⟨type⟩`. The
compatibility rules that decide *when* a conversion is applied — assignment, argument matching,
operator resolution, casts — are in §2.11 and §2.12.

- By default a conversion is **implicit** and is applied automatically wherever the rules of §2.11
  allow. An `explicit` conversion applies only at an explicit cast `(T)expr` (§4.11). `explicit` is
  valid only on `operator ()` and cannot be combined with `const` or `static`.
- The emitter form substitutes its body with `$self`/`$val` bound to the source value.
- The regular-method form runs at runtime and also fires on a **bare read** of a member of this type
  — in an initializer, an argument, or an expression — not only at cast sites. This is the getter
  half of a property accessor (§9.9.1).

**Example**

```bgl
class MyType : _bglObject {
    emitter int operator ();                   // implicit
    explicit emitter string operator ();       // only via (string)x
}
MyType t;
int    n = t;              // OK
string s = (string)t;      // OK; `string s = t;` is a compile-time error

class heightProxy {
    int _val = 0;
    int operator (){ return _val; }            // regular method: fires on bare read
}
```

**Notes**

`emitter T operator ();` (semicolon, no body) is the pass-through conversion; it is
equivalent to `emitter T operator () { $val }`.

## 9.5 Special Operators

**Description**

| Operator | Use site | Description |
|---|---|---|
| `operator ? ()` | `x?`, `x?.m`, `x ?? y` | Defines what "present" (non-null) means for the type, returning `eBool`. Evaluated at each step of `?.`, to decide whether `??` needs its fallback, and directly by postfix `?`. A type without it cannot use any of the three; doing so is a compile-time error. Must be an emitter. |
| `operator switch (U v)` | `switch(x){ case v: }` | The comparison applied to each `case` value. Must be an emitter. Statement semantics: §5.12. |
| `operator ?= (U v)` | `x ?= y` | A binary comparison at equality precedence with result `eBool`. No built-in type defines it; a class gives it a meaning. |
| `operator <=> (U v)` | `x <=> y` | Three-way comparison, result `int`: negative when the left operand orders first, `0` when equivalent, positive otherwise. Declarable `static` or as an instance operator (§9.6). |
| `operator ! ()` | `!x` | Prefix logical not. |

`string` defines `operator ? ()` as a non-zero handle test. The language has no built-in notion of
null; it is entirely type-defined. Expression semantics of `?`, `?.` and `??` are in §4.10.

**Example**

```bgl
extern class object {
    emitter eBool operator ? () { $self ~= nothing }
}
```

## 9.6 `static` Operators and Three-Way Comparison

**Syntax**

```syntax
static ⟨type⟩ operator ⟨op⟩ ( ⟨type⟩ ⟨left⟩ , ⟨type⟩ ⟨right⟩ ) { ⟨statement⟩ … }
static ⟨type⟩ ⟨name⟩ ( [ ⟨parameter⟩ , … ] ) { ⟨statement⟩ … }
```

**Description**

A `static` operator takes **both** operands as parameters and has no receiver. It is a free routine
rather than a member of an instance, which is why it may be declared on an `extern`, `emitter`, or
`alias` class, none of which permit other non-emitter methods. `static` cannot be combined with
`emitter` or `const`.

Generic containers cannot see their element type at runtime. `array<T>`'s search and sort members
compare **words** unless the element type publishes `operator ==` and `operator <=>`, which they
obtain through `$opref` (§7.3.1). Two forms exist because an element may or may not be an object:

| Element at runtime | Form used |
|---|---|
| An object (class instance) | A non-emitter instance operator, sent to the element |
| A bare word (`string`, `float`) | A `static` operator, called with both operands |

A bare-word type can publish only the static form. A type that declares neither keeps word comparison.
Where a type declares both an instance and a static form of one operator, the instance form is used at
ordinary use sites and the static form where generic code needs an address. Which members of `array<T>`
consult which operator is tabulated in §12.10.

`<=>` follows the same rule: `static`, or an instance operator with the left operand as receiver. Either
form is usable in an expression or a comparator lambda.

**Example**

```bgl
extern int compareText(string a, string b);     // an I6 routine: negative, 0 or positive

extend class string {
    static bool operator ==  (string a, string b) { return compareText(a, b) == 0; }
    static int  operator <=> (string a, string b) { return compareText(a, b); }
}

array<string> names = { "cherry", "apple", "banana" };
names.indexOf("apple");                             // → 1, via string's ==
names.sort();                                       // apple, banana, cherry, via <=>

string a = "pear";
string b = "plum";
int r = a <=> b;                                    // -1, 0, or +1
names.sort((string a, string b) => a <=> b);
```

**See also** §4.15 — `Type::operator op`; only a `static` operator is referenceable from ordinary code; §3.12 — `static superposed` methods.

## 9.7 Compound Assignment and Increment Fallback

**Description**

When no `operator op=` is declared for a compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`, `&=`,
`|=`, `^=`, `<<=`, `>>=`), the statement is expanded to the equivalent simple assignment. When no
`operator ++`/`--` (or `prefix++`/`prefix--`) is declared, the increment or decrement is applied
directly to the variable.

**Example**

```bgl
n += 2;         // → n = n + 2;  when n's type declares no operator +=
```

**Notes**

`n op= v` on a type that declares no `operator op=` is equivalent to `n = n op v`;
`n++` and `n--` on a type that declares no increment operator are equivalent to `n = n + 1` and
`n = n - 1`.

**See also** §5.6 — the compound-assignment statement; §5.7 — increment and decrement.

## 9.8 Emitter-Required Operators

**Description**

The following must be declared with `emitter` (implied inside an `emitter class`); declaring any of
them as a regular method is a compile-time error:

| Declaration | Reason |
|---|---|
| `operator ? ()` | Substituted as the null test in `?.`, `??`, and postfix `?` |
| `operator switch (U v)` | Substituted as the comparison of each `case` |
| `init()` | Substituted at the declaration site; not callable as a method |
| `deinit()` | Substituted at scope exit and `return`; not callable as a method |

## 9.9 Property Accessors

A **property accessor** is a member that reads and writes like a plain member but runs code on each
access. It is built from a regular-method `operator ()` (the getter, §9.4) and an `operator =` (the
setter) declared on a value class (§8.2.1), and the value class is then used as an owned member
(§8.3.4) of the host class or object. A read of the member dispatches the getter; a write dispatches
the setter, never the getter.

Accessors work on single objects, on every instance of a class, and on pooled instances obtained with
`new`. An accessor is declared either as a named class that then types the member (§9.9.1) or in place
with `auto { … }` (§9.9.2); only the in-place form, declared directly on an object, may reach its host
through `outer` (§9.9.3).

### 9.9.1 Getters and Setters

**Syntax**

```syntax
class ⟨accessor⟩ {
    [ ⟨type⟩ ⟨name⟩ [ = ⟨value⟩ ] ; … ]
    ⟨type⟩ operator ( ) { ⟨statement⟩ … }
    ⟨type⟩ operator = ( ⟨type⟩ ⟨value⟩ ) { ⟨statement⟩ … }
}
⟨accessor⟩ ⟨member⟩ ;
```

The first form declares the accessor class: optional backing members, a getter and a setter. The
second form declares the accessor as an owned member of a host class or object.

**Description**

- The getter is a regular-method `operator ()` with no parameters. It runs on every bare read of the
  member — in an initializer, an argument, or an expression — and its return type is the type the
  member reads as.
- The setter is an `operator =` taking one parameter. It runs on every assignment to the member; the
  assigned value is its argument.
- The accessor class must be a value class with at least one stored member, and the member must be
  declared without an initializer, so that it is owned (§8.3.4); otherwise the member is an ordinary
  reference slot and no accessor dispatch occurs.
- Inside either operator, `self` is the accessor instance and its backing members. A named accessor
  class has no access to its host; `outer` (§9.9.3) is available only to an inline accessor.

**Example**

```bgl
class heightProxy {
    int _val = 0;
    int  operator ()        { return _val; }
    void operator = (int v) { _val = v * 2; }
}
class thing : object { heightProxy height; }
thing t {}
t.height = 5;               // setter: _val → 10
int a = t.height;           // getter: a → 10
```

**See also** §8.7.4 — `hide member.operator =;` makes an accessor read-only on a subtype.

### 9.9.2 Inline Accessors: `auto { … }`

**Syntax**

```syntax
auto ⟨member⟩ = { ⟨accessor-body⟩ } ;
```

`⟨accessor-body⟩` is the class body of §9.9.1: backing members, a getter and a setter.

**Description**

`auto name = { … }` declares the accessor in place, inside a class or object body: the braces hold a
class body (backing members and the two operators) and the member's type is the synthesized class.
`auto`, not `var`, is required, because the type is inferred from the body. The braces are read as a
class body, rather than a `{ v1, v2 }` initializer list, when they begin with a member declaration or
an `operator`.

**Example**

```bgl
object gadget {
    auto level = {
        int _raw = 0;
        int  operator ()        { return _raw; }
        void operator = (int v) { _raw = v * 3; }
    }
}
gadget.level = 4;           // _raw → 12
int n = gadget.level;       // n → 12
```

**Notes**

`auto level = { int _raw = 0; int operator (){ … } void operator = (int v){ … } }`
is equivalent to declaring a hidden class with that body and writing `HiddenClass level;`.

### 9.9.3 `outer`

**Syntax**

```syntax
outer . ⟨member⟩
```

**Description**

Inside an accessor body two receivers are available: `self`, the accessor instance and its own
backing members; and `outer`, the host object the accessor is declared on. `outer` is resolved at
compile time and may read and write the host's other members. It is available only inside an inline
`auto { … }` accessor (§9.9.2) declared directly in an object body (§11.2); it is a compile-time error
in a named accessor class (§9.9.1) and in an inline accessor declared in a class body.

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
gadget.level = 4;           // _raw → 12
```
