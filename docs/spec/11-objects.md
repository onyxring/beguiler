# 11 Objects

<!-- toc -->
- [11.1 Overview](#111-overview)
- [11.2 Declaring an Object](#112-declaring-an-object)
- [11.3 Inline Objects: `Type{ … }`](#113-inline-objects-type-)
  - [11.3.1 Positional and Named Members](#1131-positional-and-named-members)
  - [11.3.2 Type Inference from the Target](#1132-type-inference-from-the-target)
  - [11.3.3 Inline Objects as Arguments](#1133-inline-objects-as-arguments)
  - [11.3.4 Constant and Run-time Members](#1134-constant-and-run-time-members)
  - [11.3.5 Nested Aggregates](#1135-nested-aggregates)
  - [11.3.6 Standalone Declarations](#1136-standalone-declarations)
  - [11.3.7 Members Declared with a Body](#1137-members-declared-with-a-body)
- [11.4 Members and Type Inference](#114-members-and-type-inference)
- [11.5 Special Members: `parent`, `children`, `attributes`, `instanceName`](#115-special-members-parent-children-attributes-instancename)
  - [11.5.1 `parent`](#1151-parent)
  - [11.5.2 `children`](#1152-children)
  - [11.5.3 `attributes`](#1153-attributes)
  - [11.5.4 `instanceName`](#1154-instancename)
- [11.6 Attribute Declarations](#116-attribute-declarations)
- [11.7 Property Declarations](#117-property-declarations)
  - [11.7.1 `property` and `extern property`](#1171-property-and-extern-property)
  - [11.7.2 `additive` Properties](#1172-additive-properties)
  - [11.7.3 Computed Property Access and `property` Parameters](#1173-computed-property-access-and-property-parameters)
- [11.8 Array Members](#118-array-members)
- [11.9 Methods](#119-methods)
  - [11.9.1 Dispatch on Object Receivers](#1191-dispatch-on-object-receivers)
  - [11.9.2 Overloads](#1192-overloads)
- [11.10 `extend` for Objects](#1110-extend-for-objects)
- [11.11 `extern object`](#1111-extern-object)
<!-- /toc -->

## 11.1 Overview

An *object* is a named, globally visible instance which exists as a concrete entity in the story file:  where a class (§8.1) is a type, an object is a single instance of one.  **Any** class can be instantiated as a named object; however, most are world-model entities (rooms, things, characters), which inherit from `object` to get the world-model members.

## 11.2 Declaring an Object

**Syntax**

```syntax
object NAME [ asI6 I6NAME ] { [ VALUE , … ; ] MEMBER … }
object NAME [ asI6 I6NAME ] : BASE [ , BASE … ] { [ VALUE , … ; ] MEMBER … }
CLASS NAME [ asI6 I6NAME ] { [ VALUE , … ; ] MEMBER … }
object NAME ;
CLASS NAME ;
```

An object is declared at global scope.  Without a body, `object NAME;` and `CLASS NAME;` (of a reference class) declare an object exactly as an empty body does; a global which refers to an object instead is declared `ref` (§3.7).

The body holds member declarations in the same form as a class body (§8.3.1):  a member is written `Type name [= value];` and members are `;`-separated.  A member with no initializer will default to `0`, `false`, or `null` according to its type.  The one exception is `static`, which is a compile-time error in an object body, since an object has no class-wide members of its own.  An inline accessor declared in an object (`auto x = { … }`, §9.9.2) is a class, though, so its own members can be `static`.

The body can open with a **positional section**:  `,`-separated values which fill the class's positional members in order (§8.3.5), ended by the first `;`, exactly as in an inline object (§11.3.1).  A member given positionally can't also be given by name.

```bgl
object hall { "hall"; }
class Pet : object { inline instanceName; inline int legs; string owner; }
Pet rex { "rex", 3; owner = "Jim"; parent = hall; }
object lamp { "brass lamp"; }        // the name, positional on every class (§11.5.4)
```

The class name as the type keyword (`ClassName Name { … }`) is the usual form; the inheritance form (`object Name : Base`) is required when the object inherits from more than one base.  An object declared with neither (`object Name { … }`) is an instance of `object`.

An object declared with no base, or with a base which derives from `object`, is an instance of `object` and carries `parent`, `children`, and `attributes` (§11.5).  `object` is **not** added implicitly; to get the world-model members for an object whose bases don't derive from `object`, give it `object` as an explicit base.

**Every declared object is its own type**, regardless of which form declares it:  a type with one instance, derived from the object's class, which holds the members its body adds.  A verb object (§13) is the exception:  its value is an action, typed by its verb class.

The optional `asI6` clause names the object differently in the emitted I6 (§3.11).

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

> ***Note:** `ClassName Name { … }` is equivalent to `object Name : ClassName { … }`.*

**See also** §11.4 (members and type inference); §11.11 (`extern object`).

## 11.3 Inline Objects: `Type{ … }`

**Syntax**

```syntax
TYPE{ VALUE , … }
TYPE{ NAME = VALUE ; … }
TYPE{ VALUE , … ; NAME = VALUE ; … }
{ … }
```

The last form omits `TYPE`; it can be used only where the target type is known (§11.3.2).

An object declaration written in expression position without a name declares an *anonymous* object and evaluates to a reference to it.  It is the same declaration as the named form (`point p { x = 1; y = 2; }`) with the name left off, and it is a compile-time declaration, unrelated to `new` (§4.13), which allocates at run time.

The form is available for any class which can be declared as a named object; it has no meaning for namespace types (`emitter class`, `alias class`).  For a struct (§8.2.7) it is a value of that class with the given members:  a variable or argument receives a copy through the class's `operator =`, and an owned member takes it as its starting value (§8.3.4).

An inline object can appear anywhere an expression can, including standing alone as a statement (§11.3.6).

### 11.3.1 Positional and Named Members

A member is given *positionally* or *by name*, and the separators carry meaning:

| Form | Separator | Fills |
|---|---|---|
| Positional: `{ 3, 4 }` | `,` | The next positional member (§8.3.5):  the class's own `inline` and re-listed members, then its bases', subclass first.  Only positional members participate. |
| Named: `{ x = 1; y = 2; }` | `;` | The member named, `inline` or not.  The `=` is required. |
| Combined: `{ 5, 6; label = "p"; }` | `,` then a single `;` | Positional values first; the first `;` ends the positional section and begins the named section. |

Supplying more positional values than there are positional members is a compile-time error.  After the `;` which ends the positional section only named members can follow; a `,` before or among named members is a compile-time error, as is a named member before a positional one.  A declaration body takes the same positional section (§11.2); however, a class body is `;`-only, and `,` is never a member separator there.

**Example**

```bgl
class point : object { inline int x; inline int y; string label; }

array<point> pts = {
    point{ 3, 4 },                 // positional
    point{ x = 1; y = 2; },        // named
    point{ 5, 6; label = "p"; },   // positional, then named
};
```

### 11.3.2 Type Inference from the Target

Where the target type is already known, a bare `{ … }` takes it and the leading `Type` can be left off.  Inference applies in six positions:

- an `array<T>` element;
- an object-backed variable initializer (`Type name = { … }`);
- the element of a declarative `inject` (§12.11);
- a call argument whose parameter is an object-backed class;
- an argument to an `array<T>` method whose parameter is `T`, when `T` is an object-backed class or a struct with `inline` members (`pts.append({ 1, 2 })`);
- a member's value inside an aggregate or object body, when the member's type is a struct with `inline` members (`Pair{ { 1 }, { 2 } }`, `Panel p1 { fill = { 40, 200, 40 }; }`).

A bare `{ … }` produces an object only when the target type is an object-backed class.  If the target is an `array<…>`, or a struct or collection type with an `operator =(initializerList)`, a bare `{ … }` is a braced list.  An explicit `Type{ … }` is always available, and also distinguishes a single element from a list.

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

> ***Note:** `Type name = { … };` is equivalent to the named object declaration `Type name { … }`.*

### 11.3.3 Inline Objects as Arguments

For a bare `{ … }` argument (positional, named (§6.3), or on a method call) the parameter type must resolve unambiguously:  the type is inferred only when every viable overload of that name expects the same aggregate-constructible class at that argument position (an object-backed class, or a struct declaring `inline` members).  If the overloads disagree, or the parameter can't take an aggregate, it is a compile-time error; write an explicit `Type{ … }` instead.

**Example**

```bgl
void place(point p) { … }
place(point{3, 4});
place({3, 4});
place(p: {3, 4});
obj.method({ x = 1; y = 2; });
```

### 11.3.4 Constant and Run-time Members

When every member value is a compile-time constant, the expression denotes one *constant instance*, so repeated evaluations will yield a reference to the *same* object.  When a member value is a run-time expression (a local or parameter, as in `foo({ width, height })`), the expression denotes a per-site instance whose run-time members are populated immediately before each evaluation.  A recursive call which re-enters the same site repopulates that site's single instance, so the outer call will see the inner call's values once it returns.

**Example**

```bgl
class dims : object { inline int width; inline int height; }
void report(dims d) { print(d.width, d.height); }

void Main() {
    report({ 3, 4 });            // constant: one constant instance
    int w = 5;
    int h = 6;
    report({ w, h });            // run-time: populated before each evaluation
}
```

### 11.3.5 Nested Aggregates

A member value can itself be a `{ … }` aggregate, whose shape is taken from the member's type:  an `array<T>` member takes a braced array literal, and an object-backed member takes a nested inline object.  An `array<T>` member can be declared `inline`, which makes it a positional slot like any other.

**Example**

```bgl
class menu : object {
    inline object linkTo;
    inline string title;
    required inline array<dictionaryWord> words;
}

object root { }
menu child = { root, "a child", {.foo, .bar, .baz} };
```

### 11.3.6 Standalone Declarations

An inline object can stand alone as a statement, with no name and no assignment.  The reference is discarded, so the object is reachable only through a link it sets itself (e.g., a positional `parent` or `linkTo` which places it in the object tree) or, without one, by an object-tree walk (§21.9).

**Example**

<!-- doctest: skip -->
```bgl
menu{ root, "text to display", {.type, .kind} };
```

### 11.3.7 Members Declared with a Body

**Syntax**

```syntax
CLASS-TYPE NAME { POSITIONAL-AND-NAMED-MEMBERS } [ ; ]
```

A member of a class type can be given its instance with a body, just as a file-scope instance is (§11.2):  `Pet dog { "rex", 3; }` in an object body means `Pet dog = Pet{ "rex", 3 };`.  A member of any other type, or a `ref` member, takes its value with `=`.

**Example**

```bgl
class Pet { inline string title; inline int legs; }

object house {
    Pet dog { "rex", 3; }
    Pet cat { title = "tom"; legs = 4; }
}
```

## 11.4 Members and Type Inference

Members of an object body are declared with a type, as in a class body; however, when the object is an instance of a class, a member already declared on that class can be set without repeating the type.  The compiler searches the object's declared class first, then its base classes, and finally the base `object` class.  A typeless member name found on no class in the hierarchy is a compile-time error.

A member can also hold a reference to another object (§8.3.4), as `sys` does below.

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

**See also** §8.3.4 (owned members); §3.7 (`ref` members); §11.8 (array members).

## 11.5 Special Members: `parent`, `children`, `attributes`, `instanceName`

Four members have compiler-level support.  `parent`, `children`, and `attributes` are declared on the base `object` class and tied to the world model.  `instanceName`, which gives the object its name, is declared on `_bglObject` (§21.5.8), so it is available on every object, world-model or not.

### 11.5.1 `parent`

**Syntax**

```syntax
object parent = CONTAINER ;
OBJECT . parent = CONTAINER ;
```

The first form appears in an object body; the second is a run-time statement.

In an object body, `parent` places the object inside another object at game start.  At run time, assigning to `obj.parent` moves the object; assigning `null` takes it out of the object tree, so it is nowhere until it is moved again.

Both library bindings (§23) make the object's name and its `parent` positional, in that order (§8.3.5).

**Example**

```bgl
object cloak {
    object parent = selfobj;     // the player carries it at game start
}
```

**See also** §8.3.7 (`typesealed` members); §21.5.7 (`parentProp`, the type of `parent`).

### 11.5.2 `children`

**Syntax**

```syntax
children = { OBJECT , … } ;
OBJECT . children += { OBJECT , … } ;
for ( object NAME in OBJECT . children ) STATEMENT
OBJECT . children . length
```

The first form appears in an object body; the others are run-time expressions and statements.

`children` is the inverse of `parent`:  the object's child collection in the object tree.

**Placement.**  In an object body, `children = { a, b, c }` places each listed object inside this one at game start.  It is equivalent to setting `parent` on each child, and the container can be declared before or after its contents.  Since an object has exactly one parent, conflicting placement is a compile-time error:  listing an object in two containers' `children`, or listing it in one container's `children` while it sets `parent` to a different object.  Declaring the same link both ways (`kitchen.children = { table }` and `table.parent = kitchen`) is accepted.

**Reading.**  `obj.children` is a collection:  you iterate it with `for … in`, and `.length` (or its synonym `.size`) returns the number of direct children.  It is the live tree, not an array, so it can't initialize or be assigned to an `array<object>`; `bgl.world.inParent(obj)` returns one (§21.9).

**Iterating while moving.**  `for … in obj.children` reads each child's next sibling before running the body, so the body can move the current child elsewhere.  A body which moves *other* children of `obj`, or adds new ones, changes what the remaining iterations see; to visit exactly the children present at the start, iterate the snapshot `bgl.world.inParent(obj)` instead.

**First child and next sibling.**  `obj.child` is the object's first child, or `null`; `obj.sibling` is the next object with the same parent, or `null`.

**Run-time placement.**  `obj.children += { … }` moves each listed object into `obj`.  `=` on `children` can be used only in an object body, and `-=` can't be used at all; to remove an object, move it by assigning its `parent`.

**Example**

```bgl
attribute seen;
object table {}
object chair {}
object kitchen {
    children = { table, chair };
}
object bowl {}
object apple {}
object pear {}

// … in a routine:
for (object o in kitchen.children) { o.give(seen); }
int n = kitchen.children.length;
bowl.children += { apple, pear };
```

### 11.5.3 `attributes`

**Syntax**

```syntax
attributes = { [ ! ] ATTRIBUTE , … } ;
attributes = [ ! ] ATTRIBUTE ;
```

`attributes` is an `attributeList` member (§21.5.1) which declares the object's initial attributes.  Each entry names an attribute; a `!` prefix clears an attribute the object would otherwise inherit from its class.  The list is additive relative to the class:  attributes the class gives are kept unless negated, and `attributes = {}` clears nothing.  In an `extend` block, `attributes =` is allowed only when the object's own declaration has no `attributes` member; otherwise it is a compile-time error, even with `replace`.

A single attribute needs no braces:  `attributes = light;` is `attributes = {light};`, the one-element form which every list-typed member accepts (§12.2).

`attributeList` accepts `=` only; `+=` and `-=` are not allowed.  To change attributes at run time, use `give(attr)` and `ungive(attr)`, and test them with `has(attr)` (§21.5.1).

**Example**

```bgl
object foyer {
    attributes = {light};
}
object pantry {
    attributes = light;                 // one element: the same as {light}
}

class post : object { attributeList attributes = {scenery}; }
post lampPost {}

extend lampPost {
    attributes = {light, !scenery};
}
```

### 11.5.4 `instanceName`

**Syntax**

```syntax
instanceName = STRING-LITERAL ;
```

The form appears only in an object body.  The name is also positional (§8.3.5):  `_bglObject` declares it `inline`, so it is the last positional member of every class (`object lamp { "brass lamp"; }`, `point p { 3, 4, "origin"; }`).  A class which adds positional members re-lists it (`inline instanceName;`) to put it first.

`instanceName` is the object's name:  what `print(obj)` prints, with or without a library.  An object without one will print as its identifier in parentheses, `(lamp)`.

It is a `const string` member, fixed when the program is compiled:

- its value must be a string literal;
- assigning it anywhere else is a compile-time error (§8.3.2);
- it can't be read as a value;
- a class body can't set it:  each object names itself.

A library binding's `short_name` property can print a different name (§23.3.11).

**Example**

```bgl
object hall { instanceName = "Great Hall"; }
object lamp { "brass lamp"; parent = hall; }           // positional

class Pet : object { inline instanceName; inline int legs; }
Pet rex { "rex", 3; }

void Main(){
    print(lamp);                   // -> brass lamp
    print($"You see {lamp}.");     // -> You see brass lamp.
}
```

## 11.6 Attribute Declarations

**Syntax**

```syntax
attribute NAME [ asI6 I6NAME ] ;
extern attribute I6NAME [ asBgl NAME ] ;
```

An `attribute` declaration introduces a named flag which can be given to objects.  An attribute must be declared before use.  The `extern` form refers to an attribute defined in I6 (typically by the IF library binding, §23.3.4).  Once declared, the name is an identifier of type `attribute`.

**Example**

```bgl
attribute myNewAttr;
extern attribute light;
```

## 11.7 Property Declarations

### 11.7.1 `property` and `extern property`

**Syntax**

```syntax
property NAME ;
extern property NAME ;
```

Every member name which appears in any class or object declaration is a property name, and `obj.provides(name)` (§21.5.2) tests at run time whether `obj` carries that property.  No declaration is needed for a member of a declared class.

A `property` declaration introduces a property name which is not a member of any Beguile class.  The plain form defines the property; the `extern` form refers to one defined in I6.  Both make the name available to `obj.provides(name)`.  A defined property is an Inform 6 *individual* property, which the Z-machine places no limit on; only an `additive` property (§11.7.2) is a common one.

A property declaration carries no type; every class or object member which contributes to the property declares its own.  A non-additive property is unconstrained, so the same property name can be a member of two unrelated classes with different types.

A `property` identifier has type `property` and is accepted wherever a `property` parameter is expected.  A free-standing declaration does not grant `obj.name` access; declare the name as a class member to read or write it.

In default mode, `obj.provides(unknownName)` on an undeclared name is a compile-time error.  In loose mode (`#bgl` islands and precompiler mode, §16.3.3) the name passes through unchecked.

**Example**

```bgl
property hidden_flag;
extern property libDefinedProp;

class Box : object { int weight; }
Box g_box { }

void Main() {
    if (g_box.provides(weight))         { … }   // class member
    if (g_box.provides(hidden_flag))    { … }   // free-standing declaration
    if (g_box.provides(libDefinedProp)) { … }   // extern declaration
}
```

### 11.7.2 `additive` Properties

**Syntax**

```syntax
additive property NAME ;
extern additive property NAME ;
```

By default, a property is overriding:  when an object and one of its ancestor classes both supply it, the object's value replaces the class's.  An `additive` property accumulates instead:  the object's contribution and all of its ancestors' are gathered into one contiguous run of words.  `additive` is meaningful only on a `property` declaration, not on a class or object member and not on a type declaration.  On an `extern property` it records that the I6 declaration is already additive.

`object` declares `name`, a `rawArray<dictionaryWord>` holding the dictionary words which the parser matches for the object, and any object can set it without a type (`name = {.brass, .key};`).  `name` is additive and always declared, so the rules below apply to it in every program; a program which repeats `extern property name;` is in error.  Every other additive property is declared by the IF library binding which defines it (§23.3.5).

**Contributions are raw arrays.**  An additive property has no length word, which is the `rawArray<T>` layout (§12.8.1), so every member which contributes to it must be a `rawArray<T>` or a routine.  The element type is fixed by the highest ancestor which declares the member, walking the class hierarchy root-first; every other contribution in that hierarchy must use the same element type.  Unrelated hierarchies can each fix their own.  A single value is a one-element list:  `rawArray<int> hook = 3;` contributes `{3}`, and where an ancestor declares the member, `hook = 3;` does the same.  A member declared `array<T>` or as a scalar (`int name`, `stringObj name`) on an additive property is a compile-time error.

**Routine contributions add up too.**  A method bound to an additive property (`before`, `after`, …) contributes its routine alongside every ancestor's.  How the routines are called, and when the calling stops, is up to the code which reads the property (the IF library, for its hooks).

**Nothing is replaced.**  Every class's and object's contribution is kept, so `replace` on a contribution is a compile-time error.  A class or object can still replace its own earlier contribution in an `extend` body (`extend lamp { replace bool after() { … } }`).

**Extent and operations.**  A contributing member's extent is the total accumulated across every layer.  `size`, `length`, and subscripting work; however, every operation which needs a length word (assigning `length`, `clear`, `append`, `insert`, `prepend`, `remove`, `removeValue`, `push`, `pop`, `dequeue`, `enqueue`, `popEnd`, `peek`, `peekEnd`, `indexOf`, `reverse`, `sort`) is a compile-time error on such a member.

**Example**

```bgl
class Room       { rawArray<dictionaryWord> name = {.box, .crate}; }   // fixes the element type
object r1 : Room { name = {.wooden}; }                                 // matches all three words
object r2 : Room { name = .shed; }                                     // one word: {.shed}
```

**See also** §12.8.3 (member `rawArray<T>`).

### 11.7.3 Computed Property Access and `property` Parameters

**Syntax**

```syntax
OBJECT . PROPERTY-VARIABLE
OBJECT . PROPERTY-VARIABLE ( [ ARGUMENT , … ] )
```

A local or parameter declared `property` (or `var`) can be dereferenced against a receiver:  `obj.p` reads the property which `p` names, and `obj.m(2)` sends the message, binding `self` to `obj`.  The property is resolved at run time.  The name after the dot is treated this way only when it is such a local or parameter *and* it is not a real member of the receiver's type; a genuine member always wins.  A file-scope `property foo;` does not qualify.

When a function or emitter parameter is typed `property`, a bare property-name argument is taken as the property identifier (an implicit `(property)` cast, §4.11) rather than as a value read.  This holds inside an object method body, where a bare member name would otherwise mean `self.name`.  Any known property name is accepted:  a member of any class or object, or a free-standing `property` or `extern property` declaration.

**Example**

<!-- doctest: skip -->
```bgl
var p = (property) val;
int v = obj.p;
int r = obj.m(2);

extern void achieved(property task);

object gameState { int taskGetBanana = 1; }

// inside any object method body:
achieved(taskGetBanana);        // passes the property identifier
```

## 11.8 Array Members

**Syntax**

```syntax
array<TYPE> NAME = { VALUE , … } ;
array<TYPE> NAME[N] ;
array<char> NAME = "TEXT" ;
rawArray<TYPE> NAME = { VALUE , … } ;
```

`<`, `>`, `[`, and `]` are literal.

A member can be an array, with the same semantics and element type checking as a global array (§12.2).  The storage rules for member arrays, including `ref` members, are given in §12.7; the `rawArray<T>` member form is covered in §12.8.3, and its use for additive properties in §11.7.2.

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

## 11.9 Methods

**Syntax**

```syntax
TYPE NAME ( [ PARAMETER , … ] ) { STATEMENT … }
```

An object can define methods:  functions which are members of the object rather than free functions.

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

### 11.9.1 Dispatch on Object Receivers

When a method is called on an object, including `self.method()` from inside the object's own method body, the method is looked up in the following order:

1. the object's own members (per-instance overrides);
2. the object's class, then its base classes recursively.

There is no special case for the `object` base class, which is an ordinary class; an object which derives from it reaches its methods (§21.5) through the same walk.

**Example**

```bgl
class Animal : object {
    void speak() { print("..."); }
}
Animal cat {
    replace void speak() { print("Meow."); }   // per-instance override
}
Animal cow { }

// … in a routine:
cat.speak();            // Meow.        - the object's own member
cow.speak();            // ...          - the class's method
cat.give(light);        // from object, through the hierarchy walk
```

### 11.9.2 Overloads

A class or object body can declare several methods with the same name and different parameter signatures, dispatched by signature under the general resolution rule (§6.4).  Overloads of `operator()`, `operator[]`, and `operator[]=` follow the same rule; operator emitters can likewise be overloaded by parameter type.

**Example**

```bgl
class Logger {
    void log(int n)        { print(n); }
    void log(string s)     { print(s); }
    void log(int n, int m) { print(n, ":", m); }
}

Logger lg { }

// … in a routine:
lg.log(5);          // log(int)
lg.log("hi");       // log(string)
lg.log(3, 7);       // log(int, int)
```

## 11.10 `extend` for Objects

**Syntax**

```syntax
extend OBJECT {
    TYPE NAME = VALUE ;
    replace TYPE NAME = VALUE ;
    replace TYPE NAME ( [ PARAMETER , … ] ) { STATEMENT … }
    NAME += { VALUE , … } ;
    NAME -= { VALUE , … } ;
}
```

Any previously declared object can be extended.  Inside the body:

- A new member is declared with `=`, as in an object body.
- `replace` replaces an existing method or member value.  Adding a duplicate member without `replace` is a compile-time error; `replace` on a member which does not exist is a warning.
- A replaced member keeps its kind:  an accessor (`auto NAME = { … }`, §9.9) is replaced by an accessor, and a value by a value.  Changing the kind is a compile-time error.
- `+=` appends to, and `-=` removes from, an existing collection member (`grammarRuleList` or `array<T>`).  The member must exist and be a collection type; otherwise it is a compile-time error.  `attributeList` accepts `=` only (§11.5.3).  The grammar forms are covered in §13.5.

Extending an `extern` object (§11.11) is restricted to its `grammar` member:  `grammar += { … }`, `replace grammar = { … }`, and `grammar -= { … }` are allowed (§13.5).  Adding a member or method, or using `-=` on any other member of an extern object, is a compile-time error.

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

**See also** §8.7.1 (`extend class`); §8.7.5 (`replace` matching rules); §12.11 (`extend` for arrays).

## 11.11 `extern object`

**Syntax**

```syntax
extern object NAME ;
extern object NAME {
    TYPE METHOD ( [ PARAMETER , … ] ) ;
    emitter TYPE METHOD ( [ PARAMETER , … ] ) { I6-TEMPLATE }
    TYPE MEMBER ;
}
```

An object defined in I6 rather than Beguile is declared `extern`.  The bare form records only the name.  The body form also declares the types of the object's members, so calls and member reads against it will type-check.  The object is a referenceable file-scope name like any global object; however, no object is defined for it in the story file.

An I6 object typed by its class is declared `extern const CLASS NAME;` (§3.5).

In the body:

- Methods are bodyless signatures; the implementation is in I6.  A brace body on a non-emitter method is a compile-time error.  `emitter` members with a body are allowed.
- Members are typed and carry no initializer; an initializer is a compile-time error.
- Default parameter values are honored for arity and overload resolution at call sites, as for ordinary methods (§6.3).

**Example**

<!-- doctest: compile -->
```bgl
extern object playerCommands {
    void pushCommand(string cmd, bool isMeta = false, bool isSilent = false);
    object interrupt;
}

// … in a routine:
playerCommands.pushCommand("say hello");
```

**See also** §16.4 (declaring I6 names); §8.2.2 (`extern class`).
