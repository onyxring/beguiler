# 9 Objects

<!-- toc -->
- [9.1 Overview](#91-overview)
- [9.2 Declaring an Object](#92-declaring-an-object)
- [9.3 Inline Objects — `Type{ … }`](#93-inline-objects-type-)
  - [9.3.1 Positional and Named Members](#931-positional-and-named-members)
  - [9.3.2 Type Inference from the Target](#932-type-inference-from-the-target)
  - [9.3.3 Inline Objects as Arguments](#933-inline-objects-as-arguments)
  - [9.3.4 Constant and Run-time Members](#934-constant-and-run-time-members)
  - [9.3.5 Nested Aggregates](#935-nested-aggregates)
  - [9.3.6 Standalone Declarations](#936-standalone-declarations)
- [9.4 Members and Type Inference](#94-members-and-type-inference)
- [9.5 Special Members: `parent`, `children`, `attributes`](#95-special-members-parent-children-attributes)
  - [9.5.1 `parent`](#951-parent)
  - [9.5.2 `children`](#952-children)
  - [9.5.3 `attributes`](#953-attributes)
- [9.6 Attribute Declarations](#96-attribute-declarations)
- [9.7 Property Declarations](#97-property-declarations)
  - [9.7.1 `property` and `extern property`](#971-property-and-extern-property)
  - [9.7.2 `additive` Properties](#972-additive-properties)
  - [9.7.3 Computed Property Access and `property` Parameters](#973-computed-property-access-and-property-parameters)
- [9.8 Array Members](#98-array-members)
- [9.9 Methods](#99-methods)
  - [9.9.1 Dispatch on Object Receivers](#991-dispatch-on-object-receivers)
  - [9.9.2 Overloads](#992-overloads)
- [9.10 `extend` for Objects](#910-extend-for-objects)
- [9.11 `extern object`](#911-extern-object)
<!-- /toc -->

## 9.1 Overview

An *object* is a named, globally visible instance that exists as a concrete entity in the story file.
A class (§8.1) is a type; an object is a single instance of a type. Objects are commonly used for
world-model entities (rooms, things, characters), but any class that inherits from `object` may be
instantiated as a named object, including utility classes and data tables.

## 9.2 Declaring an Object

**Syntax**

```syntax
object ⟨name⟩ [ as ⟨i6name⟩ ] { ⟨member⟩ … }
object ⟨name⟩ [ as ⟨i6name⟩ ] : ⟨base⟩ [ , ⟨base⟩ … ] { ⟨member⟩ … }
⟨class⟩ ⟨name⟩ [ as ⟨i6name⟩ ] { ⟨member⟩ … }
```

**Description**

An object is declared at global scope. The name becomes a globally visible identifier usable wherever
an `object`-typed value is expected. The body holds member declarations in the same form as a class
body (§8.3.1): a member is written `Type name [= value];` and members are `;`-separated. A member
with no initializer defaults to `0`, `false` or `nothing` according to its type.

The class an object instantiates is given in one of two equivalent ways. Using the class name as the
type keyword (`ClassName Name { … }`) is the usual form. The inheritance form (`object Name : Base`)
is required when the object inherits from more than one base. An object declared with neither
(`object Name { … }`) is an instance of `object`.

An object declared with no base, or with a base that derives from `object`, is an instance of
`object` and carries `parent`, `children` and `attributes` (§9.5). `object` is **not** added
implicitly: an object whose bases do not derive from `object` has the static type of its first base,
is not assignable to an `object` variable, and lacks the `object` members. Give such a declaration
`object` as an explicit base when the world-model members are wanted.

The optional `as i6name` clause names the object differently in the emitted I6 (§3.12).

**Example**

```bgl
class worldObject : object {
    string description;
}

worldObject foyer {
    description = "A grand hall decorated in red and gold.";
    attributes = {light};
}

class Robot  { int power; }
class Animal { string short_name; void describe() { print(short_name); } }

object dog : object, Animal, Robot {   // object listed explicitly: dog is a world-model object
    string short_name = "shaggy dog";
    int power = 10;
}
```

**Notes**

> **Shorthand.** `ClassName Name { … }` is equivalent to `object Name : ClassName { … }`.

**See also** §9.4 — members and type inference; §9.11 — `extern object`.

## 9.3 Inline Objects — `Type{ … }`

**Syntax**

```syntax
⟨type⟩{ ⟨value⟩ , … }
⟨type⟩{ ⟨name⟩ = ⟨value⟩ ; … }
⟨type⟩{ ⟨value⟩ , … ; ⟨name⟩ = ⟨value⟩ ; … }
{ … }
```

The last form omits `⟨type⟩`; it is permitted only where the target type is known (§9.3.2).

**Description**

An object declaration written in expression position without a name declares an *anonymous* object
and evaluates to a reference to it. It is the same declaration as the named form
(`point p { x = 1; y = 2; }`) with the name omitted, and it is a compile-time declaration: nothing runs
at startup, and it is unrelated to `new` (§4.13), which allocates at run time.

The form is available for any class that can be declared as a named object. It has no meaning for
namespace types (`emitter class`, `alias class`) or value classes. It may appear anywhere an
expression may: an array-literal element, an `inject` element in `extend` for arrays (§10.11), a
variable initializer, a call argument, or standing alone as a statement.

### 9.3.1 Positional and Named Members

**Description**

A member is given *positionally* or *by name*, and the separators carry meaning:

| Form | Separator | Fills |
|---|---|---|
| Positional: `{ 3, 4 }` | `,` | The next `inline` member (§8.3.5), in declaration order, base class first. Only `inline` members participate. |
| Named: `{ x = 1; y = 2; }` | `;` | The member named, `inline` or not. The `=` is required. |
| Combined: `{ 5, 6; label = "p"; }` | `,` then a single `;` | Positional values first; the first `;` ends the positional section and begins the named section. |

Supplying more positional values than there are `inline` members is a compile-time error. After the
`;` that ends the positional section only named members may follow; a `,` before or among named
members is a compile-time error, as is a named member before a positional one. Ordinary object and
class bodies remain `;`-only; `,` is never a member separator there.

**Example**

```bgl
class point : object { inline int x; inline int y; string label; }

array<point> pts = {
    point{ 3, 4 },                 // positional
    point{ x = 1; y = 2; },        // named
    point{ 5, 6; label = "p"; },   // positional, then named
};
```

### 9.3.2 Type Inference from the Target

**Description**

Where the target type is already known, the leading `Type` may be omitted and a bare `{ … }` takes the
target's type. Inference applies in four positions:

- an `array<T>` element;
- an object-backed variable initializer (`Type name = { … }`);
- the element of a declarative `inject` (§10.11);
- a call argument whose parameter is an object-backed class.

A bare `{ … }` produces an object only when the target type is an object-backed class. If the target
is an `array<…>`, or a value class or collection type with an `operator =(initializerList)`, a bare
`{ … }` is a braced list. An explicit `Type{ … }` is always available and also distinguishes a single
element from a list.

**Example**

```bgl
enum eVerdict { pass, halt }
object shirt {}
object cloak {}
object any {}                                   // matches every noun
eVerdict dropBody() { return eVerdict.halt; }
eVerdict takeBody() { return eVerdict.halt; }
eVerdict lookBody() { return eVerdict.pass; }

class rule : object { inline verb action; inline var matcher; inline func<eVerdict> body; }

array<rule> book = {
    { Drop, shirt, dropBody },     // each element inferred as rule
    { Take, cloak, takeBody },
};

rule fallback = { Look, any, lookBody };
```

**Notes**

> **Shorthand.** `Type name = { … };` is equivalent to the named object declaration
> `Type name { … }`.

### 9.3.3 Inline Objects as Arguments

**Description**

A `Type{ … }` or bare `{ … }` may be passed directly as a call argument, positionally or as a named
argument (§6.3), and on a method call.

For the bare form the parameter type must resolve unambiguously: the type is inferred only when every
viable overload of that name expects the same aggregate-constructible class at that argument position
(an object-backed class, or a value class declaring `inline` members). If the overloads disagree, or
the parameter cannot take an aggregate, it is a compile-time error; write an explicit `Type{ … }`.

**Example**

```bgl
void place(point p) { … }
place(point{3, 4});
place({3, 4});
place(p: {3, 4});
obj.method({ x = 1; y = 2; });
```

### 9.3.4 Constant and Run-time Members

**Description**

When every member value is a compile-time constant, the expression denotes one *constant instance*,
so repeated evaluations yield a reference to the *same* object. When a member value is a run-time
expression (a local or parameter, as in `foo({ width, height })`), the expression denotes a per-site
instance whose run-time members are populated immediately before each evaluation, so each evaluation
sees freshly populated values. A recursive call that re-enters the same site repopulates that site's
single instance, so after the inner call returns the outer activation sees the inner call's values.

**Example**

```bgl
class dims : object { inline int width; inline int height; }
void report(dims d) { print(d.width); print(d.height); }

void Main() {
    report({ 3, 4 });            // constant: one constant instance
    int w = 5;
    int h = 6;
    report({ w, h });            // run-time: populated before each evaluation
}
```

### 9.3.5 Nested Aggregates

**Description**

A member value may itself be a `{ … }` aggregate, whose shape is taken from the member's type: an
`array<T>` member takes a braced array literal, and an object-backed member takes a nested inline
object. An `array<T>` member may be declared `inline`, making it a positional slot like any other.

**Example**

```bgl
class menu : object {
    inline object linkTo;
    inline string title;
    inline array<dictionaryWord> words;
}

menu child = { root, "a child", {.foo, .bar, .baz} };
```

### 9.3.6 Standalone Declarations

**Description**

An inline object may stand alone as a statement, with no name and no assignment. The reference is
discarded, so the object is reachable only if it links itself, for example by setting a positional
`parent` or `linkTo` that places it in the object tree. Without such a link it is an unreferenced
object, reachable only by an object-tree walk (§19.9).

**Example**

```bgl
menu{ root, "text to display", {.type, .kind} };
```

## 9.4 Members and Type Inference

**Description**

Members of an object body are declared with a type, as in a class body. When the object is an instance
of a class, a member already declared on that class may be set without repeating the type. The
compiler searches the object's declared class first, then its base classes, and finally the base
`object` class. A typeless member name that is found on no class in the hierarchy is a compile-time
error.

**Object references.** A member may hold a reference to another object. The assigned object must be
type-compatible with the declared member type, and a declared object instance is stored as a direct
reference; no `init()` is called.

**Example**

```bgl
alias class worldObject for object {
    string description;
}

worldObject foyer {
    description = "A grand hall.";      // string, from worldObject
    attributes = {light};               // attributeList, from object
}

class Subsystem : object { void activate() { … } }
Subsystem combat { }

object gameState {
    Subsystem sys = combat;             // an object reference
}

gameState.sys.activate();
```

**See also** §8.3.4 — owned members; §3.7 — `ref` members; §9.8 — array members.

## 9.5 Special Members: `parent`, `children`, `attributes`

Three members declared on the base `object` class have compiler-level support tied to the world model:
`parent` and `children` place objects in the object tree, and `attributes` sets the object's attribute
flags. They are available on every object.

### 9.5.1 `parent`

**Syntax**

```syntax
object parent = ⟨container⟩ ;
⟨object⟩ . parent = ⟨container⟩ ;
```

The first form appears in an object body; the second is a run-time statement.

**Description**

In an object body, `parent` places the object inside another object at game start. At run time,
assigning to `obj.parent` moves the object.

**Example**

```bgl
object cloak {
    object parent = selfobj;     // the player carries it at game start
}
```

### 9.5.2 `children`

**Syntax**

```syntax
children = { ⟨object⟩ , … } ;
⟨object⟩ . children += { ⟨object⟩ , … } ;
for ( object ⟨name⟩ in ⟨object⟩ . children ) ⟨statement⟩
⟨object⟩ . children . length ( )
```

The first form appears in an object body; the others are run-time expressions and statements.

**Description**

`children` is the inverse of `parent`: the object's child collection in the object tree.

**Placement.** In an object body, `children = { a, b, c }` places each listed object inside this one
at game start; it is equivalent to setting `parent` on each child, and the container may be declared
before or after its contents. An object has exactly one parent, so conflicting placement is a
compile-time error: listing an object in two containers' `children`, or listing it in one container's
`children` while it sets `parent` to a different object. Declaring the same link both ways
(`kitchen.children = { table }` and `table.parent = kitchen`) is accepted.

**Reading.** `obj.children` is a collection: it is iterated with `for … in`, and `.length()` (or its
synonym `.size()`) returns the number of direct children.

**Run-time placement.** `obj.children += { … }` moves each listed object into `obj`. `=` on
`children` is permitted only in an object body, and `-=` is not permitted; to remove an object, move
it by assigning its `parent`.

**Example**

```bgl
object table {}
object chair {}
object kitchen {
    children = { table, chair };
}
object bowl {}
object apple {}
object pear {}

for (object o in kitchen.children) { o.give(seen); }
int n = kitchen.children.length();
bowl.children += { apple, pear };
```

### 9.5.3 `attributes`

**Syntax**

```syntax
attributes = { [ ! ] ⟨attribute⟩ , … } ;
```

**Description**

`attributes` is an `attributeList` member (§19.5.1) that declares the object's initial attributes.
Each entry names an attribute; a `!` prefix explicitly clears an attribute the object would otherwise
inherit from its class. The list is additive relative to the class: attributes the class gives are
kept unless negated, and `attributes = {}` clears nothing. In an `extend` block, `attributes =` is
permitted only when the object's own declaration has no `attributes` member; otherwise it is a
compile-time error, even with `replace`.

`attributeList` accepts `=` only; `+=` and `-=` are not permitted. To change attributes at run time
use `give(attr)` and `ungive(attr)`, and test them with `has(attr)` (§19.5.1).

**Example**

```bgl
object foyer {
    attributes = {light};
}

class post : object { attributeList attributes = {scenery}; }
post lampPost {}

extend lampPost {
    attributes = {light, !scenery};
}
```

## 9.6 Attribute Declarations

**Syntax**

```syntax
attribute ⟨name⟩ ;
extern attribute ⟨name⟩ [ as ⟨i6name⟩ ] ;
```

**Description**

An `attribute` declaration introduces a named flag that can be given to objects. An attribute must be
declared before use. The `extern` form refers to an attribute defined in I6 (typically by the IF
library binding, §21.3.4). Once declared, the name is an identifier of type `attribute` and may be
used in `attributes` lists and passed to `give`, `ungive` and `has`.

**Example**

```bgl
attribute myNewAttr;
extern attribute light;
```

## 9.7 Property Declarations

### 9.7.1 `property` and `extern property`

**Syntax**

```syntax
property ⟨name⟩ ;
extern property ⟨name⟩ ;
```

**Description**

Every member name that appears in any class or object declaration is a property name, and
`obj.provides(name)` (§19.5.2) tests at run time whether `obj` carries that property. No declaration
is needed for a member of a declared class.

A `property` declaration introduces a property name that is not a member of any Beguile class,
typically because it lives in I6 code, or because it is a run-time flag with no compile-time owner.
The plain form defines the property; the `extern` form refers to one defined in I6. Both make the name
available to `obj.provides(name)`.

A property declaration carries no type. The type lives at each use site: every class or object member
that contributes to the property declares its own type there. A non-additive property is
unconstrained; because an object's value overrides its class's, the contributions never share
storage, and each may be whatever type it needs. The same property name may therefore be a member of
two unrelated classes with different types.

A `property` identifier has type `property` and is accepted wherever a `property` parameter is
expected. A free-standing declaration does not grant `obj.name` access; declare the name as a class
member to read or write it.

In default mode, `obj.provides(unknownName)` on an undeclared name is a compile-time error. In loose
mode (`#bgl` islands and precompiler mode, §13.3.3) the name passes through unchecked.

**Example**

```bgl
property hidden_flag;
extern property libDefinedProp;

class Box : object { int weight; }
Box g_box;

void Main() {
    if (g_box.provides(weight))         { … }   // class member
    if (g_box.provides(hidden_flag))    { … }   // free-standing declaration
    if (g_box.provides(libDefinedProp)) { … }   // extern declaration
}
```

### 9.7.2 `additive` Properties

**Syntax**

```syntax
additive property ⟨name⟩ ;
extern additive property ⟨name⟩ ;
```

**Description**

A property is normally overriding: when an object and one of its ancestor classes both supply it, the
object's value replaces the class's. An `additive` property instead accumulates: the object's
contribution and all of its ancestors' are gathered into one contiguous run of words. `additive` is
meaningful only on a `property` declaration, not on a class or object member and not on a type
declaration. On an `extern property` it records that the I6 declaration is already additive.

`name` is additive in I6 itself. The core BLR declares it, and the core is always loaded, so the rules
below apply to `name` in every program; a program that repeats `extern property name;` is in error.
Every other additive property is declared by the IF library binding that defines it (§21.3.5).

**Contributions are raw arrays.** An additive property has no length word, which is the
`rawArray<T>` layout (§10.8.1), so every member that contributes to it must be a `rawArray<T>` or a
routine. The element type is fixed by the highest ancestor that declares the member, walking the
class hierarchy root-first; every other contribution in that hierarchy must use the same element type.
Unrelated hierarchies may each fix their own. An inference-typed override (`name = {.wooden};`) takes
the ancestor's type and is always consistent. A member declared `array<T>` or as a scalar
(`int name`, `stringObj name`) on an additive property is a compile-time error, and so is an element
type that disagrees with the hierarchy's.

**Extent and operations.** A contributing member's extent is the total accumulated across every
layer, so `size()` and `length()` on `r1.name` in the example both answer 3. `size()`, `length()`
and subscripting work; every operation that needs a length word (`setLength`, `clear`,
`append`, `insert`, `prepend`, `remove`, `removeValue`, `push`, `pop`, `dequeue`, `enqueue`,
`popEnd`, `peek`, `peekEnd`, `indexOf`, `reverse`, `sort`) is a compile-time error on
such a member.

**Example**

```bgl
class Room       { rawArray<dictionaryWord> name = {.box, .crate}; }   // fixes the element type
object r1 : Room { name = {.wooden}; }                                 // matches all three words
```

**See also** §10.8.3 — member `rawArray<T>`.

### 9.7.3 Computed Property Access and `property` Parameters

**Syntax**

```syntax
⟨object⟩ . ⟨property-variable⟩
⟨object⟩ . ⟨property-variable⟩ ( [ ⟨argument⟩ , … ] )
```

**Description**

A local or parameter declared `property` (or `var`) may be dereferenced against a receiver: `obj.p`
reads the property `p` names, and `obj.m(2)` sends the message, binding `self` to `obj`. The property
is resolved at run time. The name after the dot is treated this way only when it is such a local or
parameter *and* it is not a real member of the receiver's type; a genuine member always wins. A
file-scope `property foo;` does not qualify.

When a function or emitter parameter is typed `property`, a bare property-name argument is taken as
the property identifier (an implicit `(property)` cast, §4.11) rather than as a value read. This
holds inside an object method body, where a bare member name would otherwise mean `self.name`. Any
known property name is accepted: a member of any class or object, or a free-standing `property` or
`extern property` declaration. `obj.provides(property)` is the canonical consumer.

**Example**

```bgl
var p = (property) val;
int v = obj.p;
int r = obj.m(2);

extern void achieved(property task);

object gameState { int taskGetBanana = 1; }

// inside any object method body:
achieved(taskGetBanana);        // passes the property identifier
```

## 9.8 Array Members

**Syntax**

```syntax
array<⟨type⟩> ⟨name⟩ = { ⟨value⟩ , … } ;
array<⟨type⟩> ⟨name⟩[⟨n⟩] ;
array<char> ⟨name⟩ = "⟨text⟩" ;
rawArray<⟨type⟩> ⟨name⟩ = { ⟨value⟩ , … } ;
```

`<`, `>`, `[` and `]` are literal.

**Description**

A member may be an array. It has the same semantics as any other array: subscripting, `for … in`,
`length()` and the `<array>` methods (§20.4) behave identically, and element type checking follows
the rules for global arrays (§10.2). A byte-array member (`array<char>`) accepts a string initializer
or a brace initializer. Storage rules for member arrays, including the Z-machine property-size limit
and `ref` members, are given in §10.7; the `rawArray<T>` member form is covered in §10.8.3 and its
use for additive properties in §9.7.2.

**Example**

```bgl
object scoreboard {
    array<int> highScores = {100, 75, 50, 25};
}

object foo {
    array<char> greeting = "hello";
    array<char> codes = {'a', 'b', 'c'};
}
```

## 9.9 Methods

**Syntax**

```syntax
⟨type⟩ ⟨name⟩ ( [ ⟨parameter⟩ , … ] ) { ⟨statement⟩ … }
```

**Description**

An object may define methods: functions that are members of the object rather than free functions.

**Example**

```bgl
object bar {
    bool before() {
        switch (action) {
            case Go:
                print("You can't go that way.");
                rtrue;
            default:
                rfalse;
        }
    }
}
```

### 9.9.1 Dispatch on Object Receivers

**Description**

When a method is called on an object, including `self.method()` from inside the object's own method
body, the method is looked up in this order:

1. the object's own members (per-instance overrides);
2. the object's class, then its base classes recursively.

Every object that derives from `object` (§9.2) reaches the methods declared there
(`give`, `ungive`, `has`, `provides`, `is`, …; §19.5) through the same walk. Per-instance methods
shadow inherited ones. There is no special case for the `object` base
class; it is an ordinary class reached through the ordinary hierarchy walk.

**Example**

```bgl
class Animal : object {
    void speak() { print("..."); }
}
Animal cat {
    replace void speak() { print("Meow."); }   // per-instance override
}
Animal cow { }

cat.speak();            // Meow.        — the object's own member
cow.speak();            // ...          — the class's method
cat.give(light);        // from object, through the hierarchy walk
```

### 9.9.2 Overloads

**Description**

A class or object body may declare several methods with the same name and different parameter
signatures. Overloads coexist and are dispatched by signature under the general resolution rule
(§6.4). Overloads of `operator()`, `operator[]` and `operator[]=` follow the
same rule; operator emitters may likewise be overloaded by parameter type.

**Example**

```bgl
class Logger {
    void log(int n)        { print(n); }
    void log(string s)     { print(s); }
    void log(int n, int m) { print(n); print(":"); print(m); }
}

Logger lg;
lg.log(5);          // log(int)
lg.log("hi");       // log(string)
lg.log(3, 7);       // log(int, int)
```

## 9.10 `extend` for Objects

**Syntax**

```syntax
extend ⟨object⟩ {
    ⟨type⟩ ⟨name⟩ = ⟨value⟩ ;
    replace ⟨type⟩ ⟨name⟩ = ⟨value⟩ ;
    replace ⟨type⟩ ⟨name⟩ ( [ ⟨parameter⟩ , … ] ) { ⟨statement⟩ … }
    ⟨name⟩ += { ⟨value⟩ , … } ;
    ⟨name⟩ -= { ⟨value⟩ , … } ;
}
```

**Description**

Any previously declared object may be extended. Inside the body:

- A new member is declared with `=`, as in an object body.
- `replace` replaces an existing method or member value. Adding a duplicate member without `replace`
  is a compile-time error; `replace` on a member that does not exist is a warning.
- `+=` appends to, and `-=` removes from, an existing collection member (`grammarRuleList` or
  `array<T>`). The member must exist and be a collection type; otherwise it is a compile-time error.
  `attributeList` accepts `=` only (§9.5.3). The grammar forms are specified in §11.5.

Extending an `extern` object (§9.11) is restricted to its `grammar` member: `grammar += { … }`,
`replace grammar = { … }` and `grammar -= { … }` are permitted (§11.5). Adding a member or method, or
using `-=` on any other member of an extern object, is a compile-time error.

**Example**

```bgl
object myRoom {
    int score = 10;
    void describe() { print("A room."); }
}

extend myRoom {
    int turnCount = 0;
    replace int score = 20;
    replace void describe() { print("A dark room."); }
    attributes = {light, !scenery};
}
```

**See also** §8.9.1 — `extend class`; §8.9.5 — `replace` matching rules; §10.11 — `extend` for arrays.

## 9.11 `extern object`

**Syntax**

```syntax
extern object ⟨name⟩ ;
extern object ⟨name⟩ {
    ⟨type⟩ ⟨method⟩ ( [ ⟨parameter⟩ , … ] ) ;
    emitter ⟨type⟩ ⟨method⟩ ( [ ⟨parameter⟩ , … ] ) { ⟨i6-template⟩ }
    ⟨type⟩ ⟨member⟩ ;
}
```

**Description**

An object defined in I6 rather than Beguile is declared `extern`. The bare form records only the name.
The body form also declares the types of the object's members, so calls and member reads against it
type-check. The object is a referenceable file-scope name like any global object, but no object is
defined for it in the story file.

Rules for the body:

- Methods are bodyless signatures; the implementation is in I6. A brace body on a non-emitter method
  is a compile-time error. `emitter` members with a body are permitted.
- Members are typed and carry no initializer; an initializer is a compile-time error.
- Default parameter values are honored for arity and overload resolution at call sites, as for
  ordinary methods (§6.3).

**Example**

```bgl
extern object playerCommands {
    void pushCommand(string cmd, bool isMeta = false, bool isSilent = false);
    object interrupt;
}

playerCommands.pushCommand("say hello");
```

**See also** §13.4.4 — extern objects at the I6 boundary; §8.2.2 — `extern class`.
