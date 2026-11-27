# 8 Classes

<!-- toc -->
- [8.1 Class Declaration](#81-class-declaration)
  - [8.1.1 Type Parameters](#811-type-parameters)
- [8.2 Class Forms](#82-class-forms)
  - [8.2.1 Normal Classes](#821-normal-classes)
  - [8.2.2 `extern class`](#822-extern-class)
  - [8.2.3 `emitter class`](#823-emitter-class)
  - [8.2.4 `alias class`](#824-alias-class)
  - [8.2.5 Primitive Classes (`primitive class`)](#825-primitive-classes-primitive-class)
  - [8.2.6 Pooled Classes](#826-pooled-classes)
  - [8.2.7 Structs (`struct`)](#827-structs-struct)
  - [8.2.8 Making a Reference Type Behave Like a Value Type](#828-making-a-reference-type-behave-like-a-value-type)
- [8.3 Members](#83-members)
  - [8.3.1 Member Variables](#831-member-variables)
  - [8.3.2 `const` Members](#832-const-members)
  - [8.3.3 `static` Members](#833-static-members)
  - [8.3.4 Owned Members](#834-owned-members)
  - [8.3.5 `inline` Members](#835-inline-members)
  - [8.3.6 `required` Members](#836-required-members)
  - [8.3.7 `typesealed` Members](#837-typesealed-members)
- [8.4 Methods](#84-methods)
- [8.5 Lifecycle: `init` and `deinit`](#85-lifecycle-init-and-deinit)
  - [8.5.1 Held Temporaries](#851-held-temporaries)
- [8.6 Inheritance](#86-inheritance)
- [8.7 Extending and Replacing Members](#87-extending-and-replacing-members)
  - [8.7.1 `extend class`](#871-extend-class)
  - [8.7.2 `replace`](#872-replace)
  - [8.7.3 Shadowing and `default`](#873-shadowing-and-default)
  - [8.7.4 `hide`](#874-hide)
  - [8.7.5 Matching Rules](#875-matching-rules)
<!-- /toc -->

A class declares a type:  the members its instances hold, the methods and emitters that act on them, and the parents it inherits from.

## 8.1 Class Declaration

**Syntax**

```syntax
[ QUALIFIER … ] class NAME [ <T> ] [ : PARENT [ , PARENT … ] ] {
    MEMBER …
}
[ QUALIFIER … ] class NAME[N] [ : PARENT [ , PARENT … ] ] {
    MEMBER …
}
```

`<` and `>` in the first form, and `[` and `]` in the second, are literal:  `<T>` declares a type parameter (§8.1.1) and `[N]` a pool size (§8.2.6).

A class's members are member variables, methods, emitters, and operators (§9).  The class name must be unique among types.  Member variables follow the same `TYPE NAME [ = VALUE ]` form as global variables (§3.3).  Qualifiers (`extern`, `emitter`, `alias`, `extend`, `replace`, `superposed`, …) are the declaration qualifiers of §3.2 and can appear in any order.  The names a class and its members take in the generated Inform 6 are covered in §16.9.

**Example**

```bgl
class Point {
    int x = 0;
    int y = 0;
    void describe(){ print(x, y); }
}
```

### 8.1.1 Type Parameters

Beguile has no generic classes.  The `<T>` form belongs to the two array types, `array<T>` and `rawArray<T>` (§12.1); a type parameter on any other class, including one derived from `array`, is a compile-time error.  `array<Room>` binds `T` to `Room` in the array's method signatures.

An `extend class array` block adds emitters to every array, and `T` in their signatures binds the same way.  The extension does not repeat the `<T>`.  Writing a body against the array's layout is covered in §12.12.

**Example**

```bgl
extend class array {
    emitter T second() { $i6Expr($self[1]) }
}
array<int> scores = {4, 9, 2};
void main(){ int s = scores.second(); print(s); }   // 9
```

## 8.2 Class Forms

A class comes in one of the following forms, which differ in what an instance is and in the members each allows:

| Form | Syntax | Instances | Members permitted |
|---|---|---|---|
| Normal | `class Foo` | Objects with their own storage, shared by reference | Variables (with or without initializers), methods, emitters |
| Value | `struct Foo` | Objects each variable owns, copied on assignment | As normal class; a copy operator to be assigned |
| Extern | `extern class Foo` | Defined outside Beguile (I6) | Variable declarations, emitters, `static` methods |
| Emitter | `emitter class Foo` | None (type label only) | Emitters (`emitter` implied), `static` methods, alias members |
| Alias | `alias class Foo for Parent` | Same as `Parent` | Variable declarations (no initializer), emitters, `static` methods |
| Primitive | `primitive class Foo` | None: a variable holds the value itself | As emitter class |
| Pooled | `class Foo[N]` | `N` preallocated slots | As normal class, plus `create()`/`destroy()` |

Normal, `extern`, and `emitter` classes can inherit with `: Parent` (§8.6).  An alias class names exactly one `Parent` after `for`; this is a type-aliasing relationship, not inheritance.  Every form can be extended with `extend class` (§8.7.1), or `extend struct` for a struct (§8.2.7).  `alias` and `extern` are mutually exclusive.

### 8.2.1 Normal Classes

**Syntax**

```syntax
class NAME [ : PARENT [ , PARENT … ] ] { MEMBER … }
```

A class with no form qualifier supports the full member set:  variables with initializers, methods with statement bodies, and emitters.  Instances are objects with their own storage.

A normal class is a **reference class** (§2.10.3).  Assignment makes a slot refer to the assigned instance, and a `ref` slot is empty until bound (§3.7).  A reference class can declare a copy operator, which makes `=` copy (§8.2.8).  For a value type (i.e., one owned by every variable and parameter, and copied), declare a `struct` (§8.2.7).

A class which other classes list as a parent can write `selfType` (§2.11) in its members to mean the class each one is used on:  a parameter, return type, or field which is the subclass on a subclass.

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
extern class NAME [ : PARENT ] { MEMBER … }
extern class NAME[] ;
```

In the second form `[]` is literal:  it is the marker form described below.

An `extern class` describes a type implemented outside Beguile.  The declaration is used for type checking and emitter dispatch only:

- Emitters are allowed and require the `emitter` keyword.
- A method which is not an emitter must be `static` and have a body; it emits as a free routine, since it needs no instance.  Any other non-emitter method is a compile-time error.
- Variable declarations (type and name) are allowed and drive type inference on instances.
- Variable definitions (with `=`) are accepted; however, the value is metadata only:  no code is generated for it and it can't be observed from Beguile.

The **marker form** `extern class Name[];` declares that the type is pooled externally, with a pool size Beguile does not know.  It allows `new Name(…)` and `delete` on the type (§8.2.6).  The body can be present or omitted.  `extern class Name[N]` with an explicit size is a compile-time error.

**Example**

<!-- doctest: skip -->
```bgl
extern class object {
    parentProp    parent;
    attributeList attributes;
    emitter void  give(attribute attr){ give $val $attr }
    emitter eBool has(attribute attr){ $val has $attr }
}
```

**See also** §16.4.3 (the interoperability rules for extern classes).

### 8.2.3 `emitter class`

**Syntax**

```syntax
emitter class NAME [ : PARENT [ , PARENT … ] ] { MEMBER … }
```

An `emitter class` is a type with no instance storage:  it exists to give a name to a set of emitters and operators.  Like any class it can name more than one parent.  Every method is an emitter, so the `emitter` keyword on a member is optional.  A member variable is a compile-time error, except for an **alias member** (§10.3), which has no storage of its own and can be `const` or `inline` (the root `_bglObject` declares the object name this way, §11.5.4).  An emitter class is used as a type (variables can be declared of it), which distinguishes it from an emitter namespace (§7.7), which is only called by name.

A type label over a word which is *not* a primitive value (a class which manages its own storage through `init`/`deinit`, such as `array<T>`) is declared `extern emitter class`; such labels are sometimes called *veneer classes*.  A primitive value's label is a `primitive class` (§8.2.5).

**Example**

<!-- doctest: skip -->
```bgl
emitter class celsius {
    fahrenheit operator(){ $val * 9 / 5 + 32 }
    celsius operator = (celsius v){ $target = $v; }
}
```

### 8.2.4 `alias class`

**Syntax**

```syntax
alias class NAME for PARENT { MEMBER … }
```

An `alias class` is the same type as its parent under another name; it adds typed member declarations, which let instances omit a type keyword when initializing those members.  An instance of an alias class is an instance of the root non-alias type reached by following `for` through any chain of aliases, and its body resolves members against the alias and every class along that chain.

- Emitters are allowed and require the `emitter` keyword.
- Non-`static`, non-emitter methods are a compile-time error.
- Variable declarations (type and name) are allowed; a definition with `=` is a compile-time error.
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

### 8.2.5 Primitive Classes (`primitive class`)

**Syntax**

```syntax
primitive class NAME [ : BASE ] { MEMBER … }
```

A `primitive class` declares a **primitive type** (§2.10.3):  a variable of the type holds the value itself, one word.  There is no instance and nothing is allocated.  Assignment and passing copy the word, and `ref` is a compile-time error.  The class adds a type and behavior (emitters, operators), not storage.  `primitive` implies `extern emitter`:  I6 already knows the representation, and every member is an emitter.  The primitive types are listed in §2.10.3.

A value of the base type is a complete instance:  given a matching `operator =`, a primitive over `int` will accept an `int` (or a value that widens to one, such as a `char`) directly; an enum member takes a cast (§2.7.3).

A primitive over another type is a *newtype*, not an alias:  `glulxImage` and `int` are different types that share a representation, and conversion between them is explicit (`operator()` to the base, `operator =(base)` from it).  An `alias class` (§8.2.4) is the *same* type under another name.

**Example**

<!-- doctest: skip -->
```bgl
primitive class int {
    emitter int operator + (int v){ $val + $v }
}

glulxImage cover = eImages.coverArt;    // a primitive over int accepts the int
int w = cover.width;                    // behavior without storage
```

### 8.2.6 Pooled Classes

**Syntax**

```syntax
class NAME[N] [ : PARENT ] { MEMBER … }
extern class NAME[] ;
```

`[` and `]` are literal:  `N` is a positive integer literal or the name of a compile-time integer constant, and the empty `[]` is the extern marker form (§8.2.2).

A normal class can reserve a fixed number of instances by adding `[N]` after its name.  Instances are then obtained and released with `new` (§4.13) and `delete` (§5.15).  There is no dynamic allocation:  `new` returns one of the `N` preallocated slots, or `null` when the pool is exhausted, and the pool never grows.  A pooled instance has no name of its own:  printed without a `short_name` (or a `print()` of its class), it will show the Inform 6 name of its slot, such as `_bglClass_Token_1`.

**Pool size.**

- `[N]`, a positive integer literal.
- `[IDENT]`, an identifier naming a compile-time integer constant (`const int IDENT = …;`).  Owned members (§8.3.4) are not allowed on an identifier-sized pool.
- `[]` is valid only on `extern class` (§8.2.2).
- `[N]`/`[IDENT]` are compile-time errors on `emitter class` and `alias class`.
- `extend class Name[…]` is a compile-time error, since the pool size belongs to the original declaration.

**Inheritance.**  Each pooled class has its own pool, as in Inform 6:  a subclass used with `new` declares its own `[N]`, and inherits the parent's members, `create()`, and `destroy()`.  A subclass without `[N]` can't be allocated with `new`, even when its parent is pooled.

**`create()` and `destroy()`.**  A pooled class can declare a `create()` method, run when a slot is allocated, and a `destroy()` method, run before a slot is returned to the pool.  Both are optional.

- `create()` returns `void` and can declare parameters; the arguments of `new Name(args)` are passed to it.  If `create()` is not declared, `new Name()` with no arguments is the only valid form.
- **[Z-machine]** `create()` can declare at most three parameters; a fourth is a compile-time error.  The limit doesn't apply on Glulx.
- `destroy()` returns `void` and takes no parameters.
- One `create` and one `destroy` per class; overloads are compile-time errors.

**Members in a pool.**  `new` resets every member of the slot to its declared value.  Each slot also has its own backing for every owned member (§8.3.4), which `new` attaches and `delete` releases.  Any `create()`/`destroy()` the class declares runs after this reset.

**Example**

```bgl
class marbleClass[10] : object {
    int weight = 0;
    void create(int w){ weight = w; }
    void destroy(){ }
}

void Main(){
    marbleClass m = new marbleClass(5);     // create(5) runs; m.weight -> 5
    if(m == null) return;                   // pool exhausted
    delete m;                               // destroy() runs, slot returns to the pool
}
```

> ***Note:** A file-scope instance of a pooled type (`Name m { }` at file scope, not obtained from `new`) is not part of the pool.  Passing such an instance to `delete`, or otherwise mixing file-scope and pooled instances of one type, is not detected at compile time, and the behavior is undefined.*

### 8.2.7 Structs (`struct`)

**Syntax**

```syntax
struct NAME { MEMBER … }
extend struct NAME { MEMBER … }
```

A `struct` declares a **value type** (§2.10.3), as in C#:  a global, local, member, or parameter of the type owns an instance of its own, whose members are zeroed (a local's at routine entry; a member's is created with its host, §8.3.4).  A struct local declared in a loop body is that one instance, zeroed again each time its declaration runs.  A routine which calls itself keeps each call's locals and parameters its own across the inner call.  A slot with an initializer starts as a copy of it.  Assignment copies into that instance, a parameter receives a copy of its argument, and a function returns a copy.  A struct is never `null`.

A struct has fields, methods, operators, and accessors as a class does, and `extend struct` (not `extend class`) adds more.

Copying goes through the struct's **copy operator**, `operator =` taking the struct.  A struct which declares none gets one which copies each field, as an assignment of that field would:  a number is copied, a struct field goes through its own copy operator, and a `stringObj` field's text and an array field's elements are copied (§12.5); however, a class field or a `ref` field is a reference, and the copy will refer to the same object.  It covers fields which `extend struct` adds.  To copy differently, a struct simply declares its own copy operator.

- A struct doesn't inherit:  it has no bases, and a class can't derive from it.
- Assigning to a field of a function's result (`getPos().x = 5;`) is a compile-time error.  Calling a method on one (`getPos().move()`) runs on the copy.
- A struct can't hold `null` (§2.5):  assigning `null` to one, or returning `null` from a routine which returns one, is a compile-time error.
- A `ref` variable, parameter, or member of a struct refers to an instance owned elsewhere instead:  it is bound with `:=`, and `=` through it copies into the referent (§3.7).  A `ref` can be bound to `null` (`r := null;`).
- Each slot of an `array<NAME>` declared with a capacity or a list owns an instance too, and a store into a slot copies; an array with no capacity of its own holds its source's instances (§12.10.1).
- A struct can't be `extern`, `emitter`, `alias`, or `primitive`.
- An instance is an Inform 6 object and its members are properties, so a member can't take the name of an additive property such as `name` unless it is declared the way that property is (a `rawArray<T>`).

**Example**

```bgl
struct Vec2 { int x; int y; }      // copied field by field
extend struct Vec2 {
    int sum() { return x + y; }
}

void nudge(Vec2 v){ v.x++; }

// … in a routine:
Vec2 a;                     // owns an instance
Vec2 b = a;                 // a copy
nudge(a);                   // the function gets a copy: a.x unchanged
ref Vec2 p := a;            // p refers to a's instance
```

> ***Note:** In C++, `struct` is a class whose members are public by default.  In Beguile, as in C#, it is a value type.*

### 8.2.8 Making a Reference Type Behave Like a Value Type

**Syntax**

```syntax
class NAME [ : PARENT … ] { … NAME operator = ( NAME PARAM ) { … } … }
```

A reference class (§8.2.1) which declares a copy operator (§9.1) copies on `=`, as a struct does:  `=` is the type's assignment everywhere (§3.7), and this type's assignment is its copy operator.  A subclass chains to its ancestor's copy operator with a cast, `(Base)self = o;`.

- A plain global, member, or local owns an instance of its own, whether or not it is given a value.  Given one, the value is copied into it:  a local's when the declaration runs, a member's from the named object's values, a global's at startup.
- `=` on a plain slot copies into its instance.  `:=` on it is a compile-time error, since it owns its instance.
- A `ref` slot refers to an instance owned elsewhere:  it binds with `:=`, which never calls the copy operator, and `=` through it copies into the instance it refers to.  A `ref` that is still empty has nothing to copy into.
- Parameters and loop variables stay references, as for any reference class:  passing doesn't copy.

> ***Note:** A plain slot is like a C++ object with an `operator=`; a `ref` is like a pointer whose `*p = x` copies and whose `p = &x` re-points (`:=`).*

**Example**

```bgl
class Card : object {
    int n;
    Card operator = (Card o){ n = o.n; return self; }
}
Card a { n = 1; }
void f(Card c){ c.n = 5; }

// … in a routine:
Card b = a;            // its own Card, a copy of a
f(a);                  // c refers to a: a.n is now 5
ref Card r := a;       // r refers to a
r = b;                 // copies b into a
r := b;                // r now refers to b
```

**See also** §3.7, §8.2.7, §9.1.

## 8.3 Members

### 8.3.1 Member Variables

**Syntax**

```syntax
TYPE NAME [ = VALUE ] ;
array<TYPE> NAME[N] ;
array<TYPE> NAME = { VALUE , … } ;
```

`<`, `>`, `[` and `]` are literal in the array forms, which are those of §12.2.

Member variables declare the per-instance state of a class.  An `array<T>` member accepts the same forms as at file scope and in object bodies (§12.2, §12.7), either a sized declaration or an initializer list, and each instance gets its own storage for it.  Class-typed members hold a reference unless they are owned (§8.3.4).

**Example**

```bgl
object lamp {}  object key {}
class Inventory : object {
    array<int>    slots[6];
    array<object> heldRefs = { lamp, key };
    array<char>   nameBuf[16];
}
```

### 8.3.2 `const` Members

**Syntax**

```syntax
const TYPE NAME [ = VALUE ] ;
```

A `const` member can be initialized in the class or in an object declaration; assigning it afterward is a compile-time error.  A value which an object declaration gives an inherited `const` member is that member's one assignment:  it stays `const`.  `const` and `static` are mutually exclusive.

**Example**

```bgl
class Config {
    const int    maxScore = 100;
    const string title    = "My Game";
}
Config gameConfig { }

// … in a routine:
gameConfig.maxScore = 200;  // compile-time error
```

### 8.3.3 `static` Members

**Syntax**

```syntax
static TYPE NAME [ = VALUE ] ;
```

A `static` member is class-level state shared by all instances, read and written as `ClassName.member`.  Inside the class's own code (its methods and inline accessors) the bare name reaches it too, as in C# and C++; outside the class, the class name is required.  An inline accessor's hidden class can't be named, so its own statics are reached only by the bare name.  Note that Beguile's `static` is unrelated to Inform 6's `static` (an immovable object).

A class name reaches **only** what belongs to the class:  its `static` members, its `static` methods, its emitter values, and its alias members.  Naming a per-instance member through the type (`Counter.someField`, or through a type alias, §10.1) is a compile-time error.  A subclass name reaches its base classes' statics too:  `Sword.Made` names `Item.Made`, one variable, the declaring class's; a subclass's own static of the same name shadows it.

A `static` `func<>` member is called directly (`Policy.step(10)`).  Which methods are function values is covered in §2.9.

**Example**

```bgl
class Counter {
    static int instanceCount = 0;
    void increment(){ instanceCount = instanceCount + 1; }   // or Counter.instanceCount
}
```

**See also** §9.6 (`static` methods and operators).

### 8.3.4 Owned Members

**Syntax**

```syntax
STRUCT NAME [ = STRUCT{ … } ] ;
```

A class-typed member declared without `ref` is an **owned member** when its type is a struct (§8.2.7) with stored members, declared without an initializer or with an inline-object initializer (§11.3), or when its type is a reference class and it is declared without an initializer.  An owned member is a live instance of its own (every instance of the enclosing class has an independent backing) rather than a bare reference slot.  A member of a reference class which is given a value refers to that instance (a declared object is stored as a direct reference, and no `init()` is called); a `ref` member is empty until bound (§3.7).  A member whose type is its own class would own another of itself without end, so it must be `ref`.  A class-typed member of an `extern` class or object is declared `ref` (`ref object n_to;`).

An inline-object initializer gives the owned member its starting value (`Coins price = Coins{ cents = 3; };`); every instance will start with that value, including a pooled instance made with `new`.

**Example**

```bgl
struct Box { int _val = 0; void set(int v){ _val = v; } }
class thing : object { Box b; }         // b is owned: each thing has its own Box
thing t1 {}
thing t2 {}                              // t1.b and t2.b are distinct instances
```

### 8.3.5 `inline` Members

**Syntax**

```syntax
inline TYPE NAME [ = VALUE ] ;
inline EXISTING-MEMBER ;
```

An `inline` member is a positional slot:  when an instance is written with positional values, in a declaration body (`Pet rex { "rex", 3; }`, §11.2) or an inline object (`Pet{ "rex", 3 }`, §11.3), the values fill the class's positional members in order.  A member which is not positional can be set only by name.  `inline` can be declared on any class, and an `array<T>` member can be `inline`.

The second form, with no type, **re-lists** an existing member (inherited, or the class's own) as positional at that point in this class's order.  The member itself is unchanged; it keeps its type, its `const`, and where it is declared.  Naming a member which the class doesn't have, or one already positional in this class, is a compile-time error.

A class's positional order is its own list (its `inline` members and the members it re-lists, in declaration order), followed by each base's order, **subclass first**.  A member appears once, where it is first listed, so a subclass which re-lists an inherited member decides its position.  Slots which an `extern` class lists come after the program's own, as the root's name does, unless a class of the program re-lists them itself.  The positional and named forms are covered in §11.3.1.  A positional value is type-checked against the member it fills, as a named one is.

**With `extend class`** (§8.7.1), both forms append to the extended class's own list:  `extend class A { inline int a2; }` adds `a2` after A's existing positional members, and `extend class A { inline n; }` makes the existing member `n` positional there.  The order is a property of the program, not of source position:  every instance of the class, or of a class derived from it, uses the extended order, including instances declared before the `extend`.

**Example**

```bgl
class point : object { inline int x; inline int y; string label; }
point origin = point{ 0, 0 };            // x, y positional; label by name only

// `_bglObject` declares `inline const string instanceName;` (§11.5.4), so every class ends with it.
object lamp { "brass lamp"; }                                    // the name is object's only slot
class Animal : object { inline int legs; }                       // legs, then the name
class Pet : object { inline instanceName; inline int legs; }     // name first: re-listed
Animal cat { 4, "the cat"; }
Pet rex { "rex", 3; }

class A : object { inline int a1; int n; }
class B : A { inline int b1; }
extend class A { inline int a2; inline n; }   // A: a1, a2, n
B item { 1, 2, 3, 4, "the item"; }             // b1, a1, a2, n, name
```

### 8.3.6 `required` Members

**Syntax**

```syntax
required TYPE NAME ;
required TYPE NAME ( PARAMETERS ) ;
```

A `required` member is one which every object of the class must get a value for.  The requirement is met by the nearest declaration which gives one:  the object itself, or any class between the object and the class which declared the member.  An object which gets no value is a compile-time error naming the object and the member.

A required routine is declared with its parameters and a `;` in place of a body; each object, or a subclass, gives the body.  A `required default` routine has a body which doesn't meet the requirement:  it is the fallback for `extern` objects, which the requirement doesn't cover.

- `required` applies to class members; in an object body it is a compile-time error.
- A `required` member which is given a value in the same declaration is a warning, since nothing needs to supply it.  A `default` body is the exception (above).
- A class which marks `required` a member an ancestor already gives a value is a warning, and has no effect.

**Example**

```bgl
class Shape : object {
    required array<int> sides;
    required int area();
}
class Square : Shape {
    array<int> sides = {4, 4, 4, 4};
    int area() { return 16; }
}
Square tile { }                                          // Square gives both
Shape wedge { sides = {3, 4, 5}; int area() { return 6; } }
```

> ***Note:** C#'s `required` makes the code which creates an object set the member; a Beguile object declaration is that creation.  There are no abstract classes:  a class which leaves a member required can still have objects, and each one meets the requirement.*

### 8.3.7 `typesealed` Members

**Syntax**

```syntax
typesealed TYPE NAME ;
```

A member declared `typesealed` in a base class has its type locked.  A subclass or object instance can re-initialize the member; however, if it re-declares it with a different type keyword, the sealed type is kept, the written type is ignored, and a warning is reported.  The initializer is still validated against the written type.

**Example**

```bgl
class place : object { }
object southOfRockWall { }

place cliffEdge {
    parent = southOfRockWall;               // idiomatic: no type keyword
    // object parent = southOfRockWall;     // accepted, with a warning; stays parentProp
}
```

## 8.4 Methods

**Syntax**

```syntax
[ QUALIFIER … ] TYPE NAME ( [ PARAMETER , … ] ) { STATEMENT … }
```

A method is a function declared in a class body.  Within it, a bare member name resolves to `self.name`; the explicit `self.name` form is equivalent.  Methods can be overloaded by parameter-type signature; however, two methods with the same name and the same signature are a compile-time error (§11.9.2).  A method can be declared `static` (§9.6), `default`, `replace` (§8.7.2, §8.7.3), or `static superposed` (§3.12; `superposed` on a non-`static` method has no effect and is a warning).

**Example**

```bgl
class Counter {
    int count = 0;
    void incrementBy(int amount){ count = count + amount; }    // count -> self.count
    bool isAbove(int threshold){ return count > threshold; }
}
```

## 8.5 Lifecycle: `init` and `deinit`

**Syntax**

```syntax
emitter void init() { I6-TEMPLATE }
emitter void init( OWN-TYPE SOURCE ) { I6-TEMPLATE }
emitter void deinit() { I6-TEMPLATE }
static TYPE deinit( TYPE VALUE ) { STATEMENT … }
```

A class of any form can declare `init` and `deinit`, which run automatically for a variable of that type.  A class which manages its own storage (such as `stringObj`, which holds a pool buffer) uses them to acquire and release it.

- **Local.**  `init` fires immediately after the variable is declared, before any initializer assignment.  `deinit` fires at the end of the block the variable was declared in, before any `return` that leaves that block, and before a `break` or `continue` that leaves it (for that block and every block between it and the loop).  What a `throw` releases is covered in §5.16.  Releases run in reverse declaration order.  A reference class has no `init` or `deinit` to fire (its locals are in §2.10.3); a pooled instance is created and released with `new` and `delete` (§8.2.6).
- **Parameter.**  A parameter which owns its instance (a struct, §8.2.7, or an emitter class with both `init` and `deinit`) runs them as a local does:  `init` fires at entry, before the argument is copied in through the class's `operator =` taking its own type, and `deinit` fires at the routine's end and before every `return`.  An emitter class with no copy operator can be passed only by `ref` (a compile-time error otherwise).  When the class also declares the **copy form**, `init` with one parameter of its own type, a parameter is set up by that one call with the argument instead of `init` followed by `operator =`.  The copy form sees the argument before the parameter's instance is acquired.  A reference-class parameter shares its argument, so neither fires.
- **Global.**  `init` fires at startup, in `bglInit`, before the declared value is applied.  A global is never released.
- **`ref`.**  A `ref` local or parameter owns nothing, so neither fires:  it shares the referent.
- **Returned value.**  A local returned from a routine has already been released when the caller receives it, so the caller must copy it at once (the ephemeral pattern, §22.3; held temporaries, §8.5.1).
- In these instance forms both must be emitters, and `deinit` declares no parameters; `init` declares none, or one of its own type (the copy form, used only for parameters).  Any other form is a compile-time error, and `init` can't be `static`.  Neither can be called as a method:  `init` is substituted at the declaration site, and `deinit` at scope exit and `return`.

A `static deinit` with exactly one parameter is the **value form**:  it releases a value a container holds without a receiver, and is what `array<T>` calls for dropped elements (§12.10).

**Example**

```bgl
emitter class stringObj {
    emitter void init()   { $self = _bglStr.new(); }
    emitter void deinit() { $self.free(); }
    emitter stringObj operator = (stringObj v){ $self.setFromStr($v); }
    …
}

void doSomething(){
    stringObj s;        // init fires
    s = "hello";
    return;             // deinit fires first
}                       // deinit also fires on fall-through

void shout(stringObj s){ s += "!"; }     // s is a copy: init, then the argument copied in
void shoutHere(ref stringObj s){ s += "!"; }   // shares the caller's buffer
```

**See also** §12.10 (the value form of `deinit` as `array<T>` uses it).

### 8.5.1 Held Temporaries

A class can declare three emitters which keep a returned value, already released (§8.5), safe inside a call:  `int temporaryMark()`, `T holdTemporary(T v)` and `var releaseTemporaries(var result, int marked)`.  When a call or operator is given a value of that type which is a call's result, beside another operand that is a call's result too, each such value is passed through `holdTemporary`, and the call through `releaseTemporaries(CALL, temporaryMark())`; operands which are names or literals are left alone.  `temporaryMark` opens a mark, and the matching `releaseTemporaries` releases what was held since then and closes it; the marks nest with the calls, and the type keeps them, so `marked` only makes the mark come first.  `holdTemporary` must leave a value which isn't released untouched.  `stringObj` declares them, so `join(s.left(3), s.right(4))` will receive both substrings intact.

## 8.6 Inheritance

**Syntax**

```syntax
class NAME : PARENT [ , PARENT … ] { MEMBER … }
```

A class inherits every member of the parents listed after the colon, and multiple inheritance is allowed.  Circular inheritance (`class A : B` where `B` derives, directly or transitively, from `A`) is a compile-time error, detected when the inheritance clause is parsed, independent of emission.  A class declared without a parent derives from a root (§21.5.8):  `_bglPrimitive` for a `primitive` class and `_bglObject` for any other.  Member lookup walks the hierarchy depth-first, left to right, and the first match wins; when two parents declare the same member name, the first-listed parent's member is used.  Inside a method, bare identifiers resolve inherited **variable** members from all bases by this search, while inherited methods resolve through method dispatch.

To dispatch to a specific ancestor's version of a member, cast the receiver:  `((Animal)myDog).speak()` (§4.11).

**Example**

```bgl
class Flyer   : object { int altitude = 0; }
class Swimmer : object { int depth = 0; }
class FlyingFish : Flyer, Swimmer {
    void status(){ print(altitude, depth); }            // from Flyer, from Swimmer
}
```

## 8.7 Extending and Replacing Members

### 8.7.1 `extend class`

**Syntax**

```syntax
extend class NAME { MEMBER … }
```

`extend class` adds members to an already-declared class.  A class declared in Beguile accepts any member, and every instance will get each new variable with its initializer, or the type's default (`0`) when it has none.

An `extend` can't give an `extern` class's instances new storage:  a variable declared in it gives the type of a property Inform 6 already provides, as one in an `extern class` declaration does (§8.2.2); it can't take a value there, since the instances aren't Beguile's to initialize.  Whether a class is `extern` is its declaration's to say:  `extern` on an `extend` is a compile-time error.

Such a variable, on an `extern` class or in the binding's or a program's `extend class object { … }`, gives no object the property:  an object holds a property only when its class, or the object itself, declares or sets it.  Writing one on an object which doesn't hold it fails at run time, not at compile time.  Declare the property in the class that writes it (`object with_key;`, `string short_name;`).

`extend class` can't change a type parameter (§8.1.1), a pool size (§8.2.6), or the class's form (`primitive`, `superposed`).

An extension never adds parents:  `extend class C : A { … }` is a compile-time error.

**Order.**  Source order matters only while a class's definition is being assembled:  its declaration comes first, then its `extend`s, applied in the order they appear.  An `extend` which precedes the declaration is a compile-time error.  Every use of the class (its instances, subclasses, method calls, and positional values) works on the class's **final** definition, wherever the use appears:  an instance declared between the class and an `extend` will have the members and the positional order which the `extend` adds.

**Example**

```bgl
class Counter { int value; }
extend class Counter {
    emitter bool isZero(){ $self.value == 0 }
}
extend class int {
    emitter string asHex(){ $self.toHexString() }
}
extend class object {
    string description;       // the library's property, typed for Beguile
}
```

**See also** §11.10 (`extend` for objects); §12.11 (`extend` for arrays).

### 8.7.2 `replace`

**Syntax**

```syntax
replace MEMBER-DECLARATION
```

Inside `extend class`, the `replace` qualifier replaces an existing member instead of adding one.  It is required when the new member would duplicate an existing one; a duplicate without `replace` is a compile-time error.  `replace` on a member which does not exist is a warning, and the member is added.

**Example**

<!-- doctest: skip -->
```bgl
extend class stringObj {
    replace emitter stringObj operator = (stringLiteral v){ $self.setFromLit($v); }
}
```

### 8.7.3 Shadowing and `default`

**Syntax**

```syntax
default METHOD-DECLARATION
```

When a derived class or an object body declares a method which already exists in a base class, a warning is reported unless the declaration carries `replace`, which states that the override is intentional.

A base-class method marked `default` is expected to be overridden:  overriding it needs no `replace` and produces no warning.  `default` is valid only in a class declaration; in an object or verb body it is a compile-time error.  An override which is not itself marked `default` ends the chain (further descendants again need `replace`).  In an `extend class`, `replace default` (in either order) gives a method a new body and keeps it `default`; a plain `replace` there ends the chain.

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
hide MEMBER ;
hide MEMBER . operator OP [ ( TYPE , … ) ] ;
hide METHOD [ ( TYPE , … ) ] ;
```

`hide` removes an **inherited** member, or one operator of it, from a subtype's static surface, so accessing it *through that type* is a compile-time error.  It can be written in a subclass body and in `extend class`.

- `hide member;` hides the whole member, read and write.
- `hide member.operator op;` hides only that operator; `hide height.operator =;` blocks writes and leaves reads intact.
- A parenthesized operand list narrows the hide to one overload; without it, all overloads are hidden.
- Only inherited members can be hidden; a `hide` naming a member which is not inherited, or an operator its type does not have, is a warning with no effect.

Hiding changes only what resolves through the subtype; the member still exists, and a base type which doesn't hide it still reaches it.

**Example**

```bgl
struct dim {                     // a getter and a setter (§9.9.1)
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

// … in a routine:
side.height = 5;                      // compile-time error
((baseWin)side).height = 5;           // OK: the base surface still has the write
```

**See also** §9.9 (property accessors, the usual target of `hide member.operator =;`).

### 8.7.5 Matching Rules

The following rules decide whether two declarations name the same member, for both `replace` and shadowing:

- In `extend class`:  `replace` replaces a member of the same class.  In `extend` on an object (§11.10):  the same rules, applied to the object's members.
- In a derived class or object body:  `replace` suppresses the shadowing warning for a method found anywhere in the base hierarchy.
- **Methods** and **emitters** match by name and full parameter-type signature, since both can be overloaded.  A method in a derived class with new parameter types adds an overload beside the base's, with no warning.
- **Member variables** match by name alone.

**Example**

```bgl
class Animal : object {
    int  legs = 4;
    void speak(){ print("..."); }
    emitter bool fits(int size){ $self.legs <= $size }
}

extend class Animal {
    replace int  legs = 2;                          // member variable: matched by name
    replace void speak(){ print("Hello."); }        // method, same signature: replaced
    emitter bool fits(string label){ $self.legs }   // emitter, new signature: added, no replace
    replace emitter bool fits(int size){ $self.legs == $size }   // emitter, same signature: replaced
}

class Dog : Animal {
    replace void speak(){ print("Woof!"); }         // found in the base hierarchy: no warning
    void speak(int times){ print("Woof x", times); }         // new signature: an added overload
}
```
