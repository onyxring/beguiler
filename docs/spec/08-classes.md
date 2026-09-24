# 8 Classes

<!-- toc -->
- [8.1 Class Declaration](#81-class-declaration)
  - [8.1.1 Type Parameters](#811-type-parameters)
- [8.2 Class Forms](#82-class-forms)
  - [8.2.1 Normal Classes](#821-normal-classes)
  - [8.2.2 `extern class`](#822-extern-class)
  - [8.2.3 `emitter class`](#823-emitter-class)
  - [8.2.4 `alias class`](#824-alias-class)
  - [8.2.5 Veneer Classes (`extern emitter class`)](#825-veneer-classes-extern-emitter-class)
  - [8.2.6 Pooled Classes](#826-pooled-classes)
  - [8.2.7 `byVal class`](#827-byval-class)
  - [8.2.8 `typesealed` Members](#828-typesealed-members)
- [8.3 Members](#83-members)
  - [8.3.1 Member Variables](#831-member-variables)
  - [8.3.2 `const` Members](#832-const-members)
  - [8.3.3 `static` Members](#833-static-members)
  - [8.3.4 Owned Members](#834-owned-members)
  - [8.3.5 `inline` Members](#835-inline-members)
- [8.4 Methods](#84-methods)
- [8.5 Lifecycle: `init` and `deinit`](#85-lifecycle-init-and-deinit)
- [8.6 Inheritance](#86-inheritance)
- [8.7 Extending and Replacing Members](#87-extending-and-replacing-members)
  - [8.7.1 `extend class`](#871-extend-class)
  - [8.7.2 `replace`](#872-replace)
  - [8.7.3 Shadowing and `default`](#873-shadowing-and-default)
  - [8.7.4 `hide`](#874-hide)
  - [8.7.5 Matching Rules](#875-matching-rules)
<!-- /toc -->

A class declares a type: the members its instances hold, the methods and emitters that act on them,
and the parents it inherits from. This chapter specifies the declaration and its forms (normal,
`extern`, `emitter`, `alias`, veneer, pooled and `byVal`), the kinds of member, methods, the `init`
and `deinit` lifecycle, inheritance, and extending and replacing members. Two further topics have
chapters of their own: overloaded operators and property accessors, which are class members, are
specified in §9; the use of objects and emitter classes as namespaces is specified in §10. Objects,
the instances declared at file scope, are specified in §11.

## 8.1 Class Declaration

**Syntax**

```syntax
[ ⟨qualifier⟩ … ] class ⟨name⟩ [ <⟨T⟩> ] [ : ⟨parent⟩ [ , ⟨parent⟩ … ] ] {
    ⟨member⟩ …
}
[ ⟨qualifier⟩ … ] class ⟨name⟩[⟨n⟩] [ : ⟨parent⟩ [ , ⟨parent⟩ … ] ] {
    ⟨member⟩ …
}
```

In the first form `<` and `>` are literal: `<⟨T⟩>` declares a type parameter (§8.1.1). In the second
form `[` and `]` are literal: `[⟨n⟩]` declares a pool size (§8.2.6).

**Description**

A class declares a new type. Its members are member variables, methods, emitters, and operators (§9).
The class name must be unique among types; it is the type name used to declare instances. Member
variables follow the same `⟨type⟩ ⟨name⟩ [ = ⟨value⟩ ]` form as global variables (§3.3). Qualifiers
(`extern`, `emitter`, `alias`, `byVal`, `extend`, `replace`, `superposed`, …) are the declaration
qualifiers of §3.2 and may appear in any order.

**Example**

```bgl
class Point {
    int x = 0;
    int y = 0;
    void describe(){ print(x); print(y); }
}
```

### 8.1.1 Type Parameters

**Syntax**

```syntax
class ⟨name⟩<⟨T⟩> [ : ⟨parent⟩ ] { ⟨member⟩ … }
⟨name⟩<⟨type⟩> ⟨variable⟩ ;
```

`<` and `>` are literal.

**Description**

A class may declare one type parameter after its name. The parameter is a name scoped to the class
body and may be used wherever a type is expected in a member declaration: return types, parameter
types, member-variable types. At a use site the binding is supplied, and every `T` in the relevant
member's signature is replaced by it, so `Box<Room> b;` gives `b.payload` the type `Room` and rejects
incompatible writes at compile time. The substitution is purely static.

- Only the first type parameter binds; `<K, V>` parses but only `K` is used.
- The parameter is not a global type; it exists only inside its declaring class.
- `extend class Name<…>` and `alias class Name<…>` are compile-time errors; type parameters belong
  to the original declaration.
- A binding may be supplied in a declaration but not in inheritance position, where the class-name
  form is used (`class byteArray : array<char>` is written as a class name).

**Example**

```bgl
class Box<T> : object {
    T   payload;
    int weight;
}
Box<Room> roomBox;      // T = Room
Box<int>  scoreBox;     // T = int
```

**See also** §12.1 — `array<T>` is the principal client.

## 8.2 Class Forms

| Form | Syntax | Instances | Members permitted |
|---|---|---|---|
| Normal | `class Foo` | Objects with their own storage | Variables (with or without initializers), methods, emitters |
| Extern | `extern class Foo` | Defined outside Beguile (I6) | Variable declarations, emitters, `static` methods |
| Emitter | `emitter class Foo` | None (type label only) | Emitters (`emitter` implied), `static` methods, alias members |
| Alias | `alias class Foo for Parent` | Same as `Parent` | Variable declarations (no initializer), emitters, `static` methods |
| Veneer | `extern emitter class Foo : Base` | A bare word | As emitter class |
| Pooled | `class Foo[N]` | `N` preallocated slots | As normal class, plus `create()`/`destroy()` |
| By value | `byVal class Foo` | As normal; parameters copy | As normal class; `operator =` required |

Normal, `extern`, and `emitter` classes may inherit with `: Parent` (§8.6). An alias class names
exactly one `Parent` after `for`; this is a type-aliasing relationship, not inheritance. Every form
may be extended with `extend class` (§8.7.1). `alias` and `extern` are mutually exclusive.

### 8.2.1 Normal Classes

**Syntax**

```syntax
class ⟨name⟩ [ : ⟨parent⟩ [ , ⟨parent⟩ … ] ] { ⟨member⟩ … }
```

**Description**

A class with no form qualifier supports the full member set: variables with initializers, methods
with statement bodies, and emitters. Instances are objects with their own storage.

A normal class that derives from neither `object` nor `_bglObject` (§21.5.8) is a **value class**:
an instance holds its members directly, a local or member of the type is an instance in its own
right, and assignment copies the members through the class's `operator =` (§2.10). A class derived
from `object` or `_bglObject` has reference semantics: a variable of the type holds a reference to an
instance owned elsewhere. The term *value class* is used throughout this specification for the
former.

**Example**

```bgl
class Animal : object {
    string short_name;
    const int maxAge = 20;
    void speak(){}
    emitter bool operator == (Animal v){ $val == $v }
}
class Dog : Animal {
    replace void speak(){ print("Woof!"); }
}
```

### 8.2.2 `extern class`

**Syntax**

```syntax
extern class ⟨name⟩ [ : ⟨parent⟩ ] { ⟨member⟩ … }
extern class ⟨name⟩[] ;
```

In the second form `[]` is literal: it is the marker form described below.

**Description**

An `extern class` describes a type that is implemented outside Beguile. The declaration is used for
type checking and emitter dispatch only.

- Emitters are permitted and require the `emitter` keyword.
- Non-`static` methods are a compile-time error.
- Variable declarations (type and name) are permitted and drive type inference on instances.
- Variable definitions (with `=`) are accepted, but the value is metadata only: no code is generated
  for it and it is not observable from Beguile.

The **marker form** `extern class Name[];` declares that the type is pooled externally, with a pool
size Beguile does not know. It permits `new Name(…)` and `delete` on the type (§8.2.6). The body may
be present or omitted. `extern class Name[N]` with an explicit size is a compile-time error.

**Example**

```bgl
extern class object {
    parentProp    parent;
    attributeList attributes;
    emitter void  give(attribute attr){ give $val $attr }
    emitter eBool has(attribute attr){ $val has $attr }
}
```

**See also** §15.4.3 — the interoperability rules for extern classes.

### 8.2.3 `emitter class`

**Syntax**

```syntax
emitter class ⟨name⟩ [ : ⟨parent⟩ ] { ⟨member⟩ … }
```

**Description**

An `emitter class` is a type with no instance storage: it exists to give a name to a set of emitters
and operators. Every method is an emitter; the `emitter` keyword on a member is optional. A member
variable is a compile-time error, except an **alias member** (§10.3). An emitter class is used as
a type — variables may be declared of it — which distinguishes it from an emitter namespace (§7.7),
which is only called by name.

**Example**

```bgl
emitter class celsius {
    fahrenheit operator(){ $val * 9 / 5 + 32 }
    celsius operator = (celsius v){ $target = $v; }
}
```

### 8.2.4 `alias class`

**Syntax**

```syntax
alias class ⟨name⟩ for ⟨parent⟩ { ⟨member⟩ … }
```

**Description**

An `alias class` is the same type as its parent under another name; it adds typed member
declarations that let instances omit a type keyword when initializing those members. An instance of
an alias class is an instance of the root non-alias type reached by following `for` through any
chain of aliases, and its body resolves members against the alias and every class along that chain.

- Emitters are permitted and require the `emitter` keyword.
- Non-`static`, non-emitter methods are a compile-time error.
- Variable declarations (type and name) are permitted; a definition with `=` is a compile-time error.
- Exactly one parent follows `for`.

**Example**

```bgl
alias class worldObject for object {
    string description;
}
worldObject foyer {
    description = "A grand hall.";      // string, from worldObject
    attributes  = {light};              // attributeList, from object
}
```

### 8.2.5 Veneer Classes (`extern emitter class`)

**Syntax**

```syntax
extern emitter class ⟨name⟩ : ⟨base⟩ { ⟨member⟩ … }
```

**Description**

A class declared `extern emitter` is a **veneer class**: a distinct type with no representation of
its own. Its value *is* the word it wraps; the class adds a type and behavior (emitters, operators)
but no storage. An instance is a bare variable, not a world-tree object. The primitive types (`int`,
`bool`, `char`, `string`, the literal pseudo-types) are declared this way, naming `_bglObject` as
their base to take the shared member surface (§21.5.8).

Because a veneer has nothing to initialize beyond the word it wraps, a value of the base type is a
complete instance: given a matching `operator =`, a veneer over `int` accepts an `int` (or any
int-compatible value such as an enum member) directly.

A veneer is a *newtype*, not an alias: `glulxImage` and `int` are different types that share a
representation, and conversion between them is explicit (`operator()` to the base, `operator =(base)`
from it). An `alias class` (§8.2.4) is the *same* type under another name.

**Example**

```bgl
extern emitter class int : _bglObject {
    emitter int operator + (int v){ $val + $v }
}

glulxImage cover = eAssets.coverArt;    // a veneer over int accepts the int
int w = cover.width();                  // behavior without storage
```

### 8.2.6 Pooled Classes

**Syntax**

```syntax
class ⟨name⟩[⟨n⟩] [ : ⟨parent⟩ ] { ⟨member⟩ … }
extern class ⟨name⟩[] ;
```

`[` and `]` are literal: `⟨n⟩` is a positive integer literal or the name of a compile-time integer
constant, and the empty `[]` is the extern marker form (§8.2.2).

**Description**

A normal class may reserve a fixed number of instances by adding `[N]` after its name. Instances are
then obtained and released with `new` (§4.13) and `delete` (§5.15). There is no dynamic allocation:
`new` returns one of the `N` preallocated slots, or `nothing` when the pool is exhausted, and the pool
never grows; the result of `new` must be tested before use.

**Pool size.**
- `[N]`, a positive integer literal.
- `[IDENT]`, an identifier naming a compile-time integer constant (`const int IDENT = …;`). Owned
  members (§8.3.4) are not permitted on an identifier-sized pool.
- `[]` is valid only on `extern class` (§8.2.2).
- `[N]`/`[IDENT]` are compile-time errors on `emitter class` and `alias class`.
- `extend class Name[…]` is a compile-time error; the pool size belongs to the original declaration.

**Inheritance.** Subclasses share the parent's pool; there is no per-subclass size.

**`create()` and `destroy()`.** A pooled class may declare a `create()` method, run when a slot is
allocated, and a `destroy()` method, run before a slot is returned to the pool. Both are optional.

- `create()` returns `void` and may declare parameters; the arguments of `new Name(args)` are passed
  to it. If `create()` is not declared, `new Name()` with no arguments is the only valid form.
- **[Z-machine]** `create()` may declare at most three parameters; a fourth is a compile-time
  error. I6's class-message veneer enumerates the arguments it forwards on the Z-machine and
  raises a run-time error past three. Glulx forwards them all, so the limit does not apply there.
- `destroy()` returns `void` and takes no parameters.
- One `create` and one `destroy` per class; overloads are compile-time errors.

**Owned members in a pool.** Each slot has its own backing for every owned member (§8.3.4). `new`
attaches a backing and resets its members to their declared defaults, so a reused slot always starts
fresh; `delete` releases it. Any `create()`/`destroy()` the class declares runs after this reset.

**Example**

```bgl
class marbleClass[10] : object {
    int weight = 0;
    void create(int w){ weight = w; }
    void destroy(){ }
}

void Main(){
    marbleClass m = new marbleClass(5);     // create(5) runs; m.weight → 5
    if(m == nothing) return;                // pool exhausted
    delete m;                               // destroy() runs, slot returns to the pool
}
```

**Notes**

A file-scope instance of a pooled type (`Name m;` at file scope, not obtained from `new`) is not part
of the pool. Passing such an instance to `delete`, or otherwise mixing file-scope and pooled instances
of one type, is not detected at compile time and the behavior is undefined.

### 8.2.7 `byVal class`

**Syntax**

```syntax
byVal class ⟨name⟩ { ⟨member⟩ … }
```

**Description**

By default a class-typed parameter is passed by reference. A `byVal class` parameter is passed by
**value**: at each call the argument is copied into the parameter through the class's `operator =`,
so mutations inside the callee do not affect the caller's instance.

- `operator =` accepting the class (or `var`) is required; declaring a `byVal class` without one is a
  compile-time error.
- A `byVal class` cannot inherit from `object`.
- `byVal` cannot be combined with `extern`, `emitter`, `extend`, or `alias`.
- The marker is not inherited; a subclass must declare `byVal` itself.

**Example**

```bgl
byVal class Temperature {
    int degrees = 0;
    emitter Temperature operator = (Temperature v){ $target = $v; }
}
void heatUp(Temperature t){ t.degrees = t.degrees + 10; }   // local copy only

Temperature room;
heatUp(room);       // room.degrees unchanged
```

### 8.2.8 `typesealed` Members

**Syntax**

```syntax
typesealed ⟨type⟩ ⟨name⟩ ;
```

**Description**

A member declared `typesealed` in a base class has its type locked. A subclass or object instance may
re-initialize the member; if it re-declares it with a different type keyword, the sealed type is
kept, the written type is ignored, and a warning is reported. The initializer is still validated
against the written type. The canonical sealed member is `object.parent` (`parentProp`), so no
`object`-derived class can repurpose `parent` as a differently-typed member.

**Example**

```bgl
class place : object { }
object southOfRockWall { }

place cliffEdge {
    parent = southOfRockWall;               // idiomatic: no type keyword
    // object parent = southOfRockWall;     // accepted, with a warning; stays parentProp
}
```

## 8.3 Members

### 8.3.1 Member Variables

**Syntax**

```syntax
⟨type⟩ ⟨name⟩ [ = ⟨value⟩ ] ;
array<⟨type⟩> ⟨name⟩[⟨n⟩] ;
array<⟨type⟩> ⟨name⟩ = { ⟨value⟩ , … } ;
```

`<`, `>`, `[` and `]` are literal in the array forms, which are those of §12.2.

**Description**

Member variables declare the per-instance state of a class. An `array<T>` member accepts the same
forms as at file scope and in object bodies (§12.2, §12.7): a sized declaration or an initializer
list, and each instance gets its own storage for it. Class-typed members hold a reference unless they
are owned (§8.3.4).

**Example**

```bgl
class Inventory : object {
    array<int>    slots[6];
    array<object> heldRefs = { lamp, key };
    array<char>   nameBuf[16];
}
```

### 8.3.2 `const` Members

**Syntax**

```syntax
const ⟨type⟩ ⟨name⟩ [ = ⟨value⟩ ] ;
```

**Description**

A `const` member may be initialized in the class or in an object declaration and cannot be assigned
afterwards; assignment is a compile-time error. `const` and `static` are mutually exclusive.

**Example**

```bgl
class Config {
    const int    maxScore = 100;
    const string title    = "My Game";
}
Config config;
config.maxScore = 200;      // compile-time error
```

### 8.3.3 `static` Members

**Syntax**

```syntax
static ⟨type⟩ ⟨name⟩ [ = ⟨value⟩ ] ;
```

**Description**

A `static` member is class-level state shared by all instances, read and written as
`ClassName.member`. Inside a method the class name is required; a bare name resolves to an instance
member. Beguile's `static` is unrelated to Inform 6's `static` (an immovable object).

A class name reaches **only** what belongs to the class: its `static` members, its `static` methods,
its emitter values and its alias members. Naming a per-instance member through the type
(`Counter.someField`, or through a type alias — §10.2) is a compile-time error; a per-instance
member exists only on an instance.

**Example**

```bgl
class Counter {
    static int instanceCount = 0;
    void increment(){ Counter.instanceCount = Counter.instanceCount + 1; }
}
```

**See also** §9.6 — `static` methods and operators.

### 8.3.4 Owned Members

**Syntax**

```syntax
⟨value-class⟩ ⟨name⟩ ;
```

**Description**

A class-typed member is an **owned member** when all three hold: its type is a value class (§8.2.1),
that type has stored members, and the member is declared without an initializer. An owned member is
a live instance of its own — every instance of the enclosing class has an independent backing —
rather than a bare reference slot, so methods and operators may be called on it. Ownership is
detected structurally; there is no keyword. To keep reference semantics instead, derive the member's
type from `object` or initialize the member to an existing instance.

Owned members are what make property accessors work (§9.9). In a pooled class each slot has its own
backing, reset on `new` (§8.2.6). Owned members are not permitted on an identifier-sized pool.

**Example**

```bgl
class Box { int _val = 0; void set(int v){ _val = v; } }
class thing : object { Box b; }         // b is owned: each thing has its own Box
thing t1 {}
thing t2 {}                              // t1.b and t2.b are distinct instances
```

### 8.3.5 `inline` Members

**Syntax**

```syntax
inline ⟨type⟩ ⟨name⟩ [ = ⟨value⟩ ] ;
```

**Description**

An `inline` member is a positional slot for inline object construction: when an instance is written
as `Type{ a, b, … }`, the positional values fill the class's `inline` members in declaration order,
base class first. A member that is not `inline` can be set only by name. `inline` may be declared on
an `object`-derived class or on a value class; an `array<T>` member may be `inline`. The positional
and named forms, their separators and the error cases are specified in §11.3.1.

**Example**

```bgl
class point : object { inline int x; inline int y; string label; }
point origin = point{ 0, 0 };            // x, y positional; label by name only
```

## 8.4 Methods

**Syntax**

```syntax
[ ⟨qualifier⟩ … ] ⟨type⟩ ⟨name⟩ ( [ ⟨parameter⟩ , … ] ) { ⟨statement⟩ … }
```

**Description**

A method is a function declared in a class body. Within it, a bare member name resolves to
`self.name`; the explicit `self.name` form is equivalent. Methods may be overloaded by parameter-type
signature; two methods with the same name and the same signature are a compile-time error (§11.9.2).
A method may be declared `static` (§9.6), `default`, `replace` (§8.7.2, §8.7.3), or
`static superposed` (§3.12; `superposed` on a non-`static` method has no effect and is a warning).
An operator member follows the same declaration form with an operator symbol in place of the name;
operators are specified in §9.

**Example**

```bgl
class Counter {
    int count = 0;
    void incrementBy(int amount){ count = count + amount; }    // count → self.count
    bool isAbove(int threshold){ return count > threshold; }
}
```

## 8.5 Lifecycle: `init` and `deinit`

**Syntax**

```syntax
emitter void init() { ⟨i6-template⟩ }
emitter void deinit() { ⟨i6-template⟩ }
static ⟨type⟩ deinit( ⟨type⟩ ⟨value⟩ ) { ⟨statement⟩ … }
```

**Description**

A class of any form may declare `init` and `deinit`, which run automatically for a local variable of
that type.

- `init` fires immediately after the variable is declared, before any initializer assignment.
- `deinit` fires before every `return` in the enclosing routine and at the routine's end if it falls
  through.
- In these instance forms both must be emitters and declare no parameters; either violation is a
  compile-time error. `init` has no other form and cannot be `static`.

A `static deinit` with exactly one parameter is the **value form**: it releases a value a container
holds without a receiver, and is what `array<T>` calls for dropped elements (§12.10). A type that
owns storage generally declares both.

**Example**

```bgl
extend extern class string {
    emitter void init()   { $self = GetNewString(); }
    emitter void deinit() { FreeString($self); }
}

void doSomething(){
    string s;           // init fires
    s = "hello";
    return;             // deinit fires first
}                       // deinit also fires on fall-through
```

**See also** §9.8 — `init` and `deinit` among the members that must be emitters; §12.10 — the value
form of `deinit` as `array<T>` uses it.

## 8.6 Inheritance

**Syntax**

```syntax
class ⟨name⟩ : ⟨parent⟩ [ , ⟨parent⟩ … ] { ⟨member⟩ … }
```

**Description**

A class inherits every member of the parents listed after the colon. Multiple inheritance is
permitted. Member lookup walks the hierarchy depth-first, left to right, and the first match wins;
when two parents declare the same member name, the first-listed parent's member is used. Inside a
method, bare identifiers resolve inherited **variable** members from all bases by this search;
inherited methods resolve through method dispatch.

To dispatch to a specific ancestor's version of a member, cast the receiver: `(Animal)myDog.speak()`
(§4.11).

**Example**

```bgl
class Flyer   : object { int altitude = 0; }
class Swimmer : object { int depth = 0; }
class FlyingFish : Flyer, Swimmer {
    void status(){ print(altitude); print(depth); }     // from Flyer, from Swimmer
}
```

## 8.7 Extending and Replacing Members

### 8.7.1 `extend class`

**Syntax**

```syntax
extend [ extern ] class ⟨name⟩ { ⟨member⟩ … }
```

**Description**

`extend class` adds members to an already-declared class; the name must already be a type. An
`extern` class accepts only emitters and `static` members; a class declared in Beguile accepts any
member. `extend class` may not change a type parameter (§8.1.1) or a pool size (§8.2.6).

**Example**

```bgl
extend class Counter {
    emitter bool isZero(){ $self.value == 0 }
}
extend extern class int {
    emitter string asHex(){ $self.toHexString() }
}
```

**See also** §11.10 — `extend` for objects; §12.11 — `extend` for arrays.

### 8.7.2 `replace`

**Syntax**

```syntax
replace ⟨member-declaration⟩
```

**Description**

Inside `extend class`, the `replace` qualifier replaces an existing member instead of adding one.
It is required when the new member would duplicate an existing one; a duplicate without `replace` is
a compile-time error. `replace` on a member that does not exist is a warning, and the member is added.

`replace` also applies to global functions (§6.5) and to `extend` on objects (§11.10).

**Example**

```bgl
extend extern class string {
    replace emitter string operator = (stringLiteral v){ $self.set(v); }
}
```

### 8.7.3 Shadowing and `default`

**Syntax**

```syntax
default ⟨method-declaration⟩
```

**Description**

When a derived class or an object body declares a method that already exists in a base class, a
warning is reported unless the declaration carries `replace`, which states that the override is
intentional.

A base-class method marked `default` is expected to be overridden: overriding it needs no `replace`
and produces no warning. `default` is valid only in a class declaration; in an object or verb body
it is a compile-time error. An override that is not itself marked `default` ends the chain — further
descendants again need `replace` — so mark each override `default` to keep a method freely
overridable. The `verb` class declares `handler()` this way (§13.2.1).

**Example**

```bgl
class Animal : object {
    default void speak(){ print("..."); }
    void eat(){}
}
class Dog : Animal {
    void speak(){ print("Woof!"); }     // no warning: base is default
    replace void eat(){}                // explicit override: no warning
}
class Cat : Animal {
    void eat(){}                        // warning: shadows Animal.eat
}
```

### 8.7.4 `hide`

**Syntax**

```syntax
hide ⟨member⟩ ;
hide ⟨member⟩ . operator ⟨op⟩ [ ( ⟨type⟩ , … ) ] ;
hide ⟨method⟩ [ ( ⟨type⟩ , … ) ] ;
```

**Description**

`hide` removes an **inherited** member, or one operator of it, from a subtype's static surface, so
that accessing it *through that type* is a compile-time error. It is permitted in a subclass body and
in `extend class`.

- `hide member;` hides the whole member, read and write.
- `hide member.operator op;` hides only that operator; `hide height.operator =;` blocks writes and
  leaves reads intact.
- A parenthesized operand list narrows the hide to one overload; without it, all overloads are
  hidden.
- Only inherited members may be hidden; a `hide` naming a member that is not inherited, or an
  operator its type does not have, is a warning with no effect.

Hiding changes only what resolves through the subtype; the member still exists. A value of a base type
that does not hide it reaches it, whether by upcast or by passing the value to a base-typed parameter.

**Example**

```bgl
class dim {                           // a value class with a getter and a setter (§9.9.1)
    int _val = 0;
    int  operator ()        { return _val; }
    void operator = (int v) { _val = v; }
}
class baseWin : object {
    dim width;
    dim height;
    void setColor(int c){ }
}
class vertWin : baseWin {
    hide height.operator =;           // height is read-only on vertWin
    hide setColor();
}

vertWin side {}
side.height = 5;                      // compile-time error
(baseWin)side.height = 5;             // OK: the base surface still has the write
```

**See also** §9.9 — property accessors, the usual target of `hide member.operator =;`.

### 8.7.5 Matching Rules

**Description**

How `replace` and shadowing decide that two declarations name the same member:

- In `extend class`: `replace` replaces a member of the same class. In `extend` on an object
  (§11.10): the same rules, applied to the object's members.
- In a derived class or object body: `replace` suppresses the shadowing warning for a method found
  anywhere in the base hierarchy.
- **Emitters** match by name and full parameter-type signature, because emitters may be overloaded.
- **Methods** and **member variables** match by name alone.

**Example**

```bgl
class Animal : object {
    int  legs = 4;
    void speak(){ print("..."); }
    emitter bool fits(int size){ $self.legs <= $size }
}

extend class Animal {
    replace int  legs = 2;                          // member variable: matched by name
    replace void speak(){ print("Hello."); }        // method: matched by name
    emitter bool fits(string label){ $self.legs }   // emitter, new signature: added, no replace
    replace emitter bool fits(int size){ $self.legs == $size }   // emitter, same signature: replaced
}

class Dog : Animal {
    replace void speak(){ print("Woof!"); }         // found in the base hierarchy: no warning
}
```
