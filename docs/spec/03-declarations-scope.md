# 3 Declarations, Variables and Scope

<!-- toc -->
- [3.1 Program Structure](#31-program-structure)
- [3.2 Declaration Qualifiers](#32-declaration-qualifiers)
- [3.3 Global Variables](#33-global-variables)
- [3.4 Constants](#34-constants)
- [3.5 Extern Variables](#35-extern-variables)
- [3.6 Local Variables](#36-local-variables)
- [3.7 References: `ref` and `:=`](#37-references-ref-and-)
- [3.8 Identifier Resolution](#38-identifier-resolution)
  - [3.8.1 Local Scope](#381-local-scope)
  - [3.8.2 Class and Object Scope](#382-class-and-object-scope)
  - [3.8.3 Global Scope](#383-global-scope)
- [3.9 The Global-Scope Qualifier `::`](#39-the-global-scope-qualifier-)
- [3.10 Shadowing](#310-shadowing)
- [3.11 The `asI6` and `asBgl` Clauses](#311-the-asi6-and-asbgl-clauses)
- [3.12 `superposed`](#312-superposed)
- [3.13 `literal`](#313-literal)
<!-- /toc -->

## 3.1 Program Structure

A program is one or more source files.  The declarations at the outermost level of a file (types, classes, enums, variables, functions, objects, verbs, and grammar) make up the **global scope**, visible throughout the compilation.  They can appear in any order, and you can use a name before it is declared (§18.3).

Every program has a `Main` function as its entry point (§6.7).

A global name must be unique across every kind of global declaration, ignoring case:  a variable, array, function, class, object, enum, or verb with the name of an existing global of any kind is a compile-time error (§19.2), unless one of the two is given an `asI6` name (§3.11).  There are three exceptions:  functions of one name overload by parameter types (§6.4), an emitter function can share its name with a variable or object (e.g., `print` or `log`, §21.4), and a verb can share its name with a class (§13.2).

The emitter function and the variable or object which share a name are told apart by **use**, not by declaration order:

| Use | Resolves to |
|---|---|
| `name(args)` | the function |
| `name.member`, `name = v` | the variable or object |
| a bare `name`, with no variable of that name declared | the function, as a `func<>` reference |

Calling a shadowed name unqualified resolves to the variable, as ordinary shadowing does (§3.10).

**Names shared with the library.**  Uniqueness includes the globals a binding declares.  A program's global array, variable, or object can't share its name, ignoring case, with a library verb:  an array `order` clashes with the library's `Order`.  A function can, since it is always called.  Nor can any of the program's globals (function, variable, array, or object) share its name with a global, attribute, or object the library binding declares:  a function `Score` clashes with the library global `score`.  A program's variable, array, or object can't take a library routine's name either (an object `cap` clashes with `Cap`); a function can, since it defines the routine.  Each clash is a compile-time error that names the other declaration.  A program can still define the library's entry points by name (§23.3.4).  A verb named like a library verb is covered in §13.2.

## 3.2 Declaration Qualifiers

**Syntax**

```syntax
[ QUALIFIER … ] DECLARATION
```

A declaration can be preceded by one or more qualifiers, in any order:  `emitter replace void foo()` and `replace emitter void foo()` are equivalent.

| Qualifier | Meaning | See |
|---|---|---|
| `const` | Read-only variable or member. | §3.4 |
| `static` | Member belongs to the type rather than to an instance. | §8.3.3 |
| `extern` | Declared in Inform 6; registered for type-checking only, produces no output. | §16.4 |
| `emitter` | The body is an I6 template expanded at each use. | §7.2 |
| `extend` | Adds members to an existing class, object, enum, or array. | §8.7.1, §11.10, §12.11 |
| `alias` | Another name for an existing type or value. | §8.2.4, §10.2 |
| `replace` | Replaces an already-declared function or member. | §6.5, §8.7.2 |
| `default` | A base-class member that a derived declaration can override without warning. | §8.7.3 |
| `explicit` | A conversion operator that fires only under a cast. | §9.4 |
| `superposed` | A routine, global, object, or class which is emitted only if it is used. | §3.12 |
| `typesealed` | A member whose type a derived class can't change. | §8.3.7 |
| `primitive` | A class whose variables hold the value itself (`primitive class`). | §8.2.5 |
| `struct` | A value type:  each variable owns an instance, and assignment copies. | §8.2.7 |
| `inline` | A member variable that is a positional slot for inline object construction. | §8.3.5, §11.3.1 |
| `ref` | A variable, member, or parameter which refers to an instance owned elsewhere, empty until bound. | §3.7 |
| `required` | A class member every object must get a value for. | §8.3.6 |
| `additive` | A property whose values accumulate along the class chain. | §11.7.2 |
| `literal` | A parameter, variable, member, or array element type that holds only values known at compile time. | §3.13, §6.3 |

The following combinations are compile-time errors:  `explicit` on anything but `operator()`; `const` with `static`; `static` with `emitter`; `explicit` with `const` or `static`; `alias` with `extern`; `alias` with `emitter`; `default` in an object or verb body.

`global` is not a qualifier:  a variable is global by being declared at file scope (§3.3), and writing `global` before a declaration is a compile-time error.

## 3.3 Global Variables

**Syntax**

```syntax
TYPE NAME [ = INITIALIZER ] ;
auto NAME = INITIALIZER ;
```

A variable declared at file scope is a global.  An initializer that is a constant expression (a literal, constant arithmetic, an object or routine name, or a `#define` value) is part of the declaration.  **Any other initializer is applied at startup**, in `bglInit()`, in declaration order:  it can call a routine or read a global declared before it; however, the variable will hold `0` until `bglInit()` runs (§21.2).  A class-typed global whose type declares a parameterless `init` is also applied at startup.

`auto` infers the type from the initializer (§3.6).

**Example**

```bgl
bool isGood = true;
int score = 5 + 3;
string playerName;
```

## 3.4 Constants

**Syntax**

```syntax
const TYPE NAME = VALUE ;
extern const TYPE NAME ;
```

`const` fixes what a name stands for.  Assigning to it (including `++`, `--`, and compound assignment) is a compile-time error, and a `const` declaration must give its value.  It applies to the types whose value is one word:  the primitives (`int`, `float`, `char`, `bool`, …), `string`, enums, and references to objects.

On a reference, `const` fixes which object the name stands for, *not* the object itself:  `const Thing pick = rock;` will always stand for `rock`, although `rock`'s members can still change.

Two kinds of type are *storage* rather than a one-word value, and `const` is a compile-time error on both:

| Type | What a variable of it holds | Why `const` has nothing to fix |
|---|---|---|
| an array (`array<T>`, `rawArray<T>`) | the array's elements | the name always stands for the same storage; assigning to it copies elements in (§12) |
| a struct (§8.2.7) | an instance of its own, with storage for its fields | the name always stands for its own instance; assigning to it copies fields in |

**Example**

<!-- doctest: compile -->
```bgl
struct Money {
    int cents;
    Money operator = (int c) { cents = c; return self; }
}
class Thing : object { int weight; }
Thing rock { weight = 5; }

const int     PRICE_CENTS = 325;    // a one-word value: an I6 Constant
const Thing   pick = rock;          // a reference: pick always stands for rock

void main() {
    pick.weight = 7;                // fine: const fixes the name, not rock's members
    Money price = PRICE_CENTS;      // a Money made from the constant's value
}
```

> ***Note:** `const Money price = 325;` is an error:  `price` would hold its own `Money`, whose `cents` can always be changed.  Keep the constant on the plain value, as `PRICE_CENTS` is above, and assign it where a `Money` is needed.*

> ***Note:** `const array<int> bands = {5, 10, 20};` is an error for the same reason:  `bands` is the array's own storage.  Drop the `const`.*

`extern const` declares a constant defined in Inform 6:  it is registered for type-checking, produces no output, and takes no initializer.

**Example**

```bgl
const int MAX_SCORE = 2;
extern const int STUCK_PE;
```

## 3.5 Extern Variables

**Syntax**

```syntax
extern TYPE NAME ;
```

An `extern` variable is declared in Inform 6 and registered for type-checking only.  It produces no output and can't be initialized.  You can read it and assign to it; `extern const` (§3.4) is read-only.  The other `extern` declarations are covered in §16.4.

`extern CLASS NAME;` declares an I6 *variable* of that class's type, which a member initializer can't read.  An I6 object typed by its class is declared `extern const CLASS NAME;`, which an initializer can name (`parent = trayA;`).

**Example**

```bgl
extern int score;
extern object location;
```

## 3.6 Local Variables

**Syntax**

```syntax
TYPE NAME [ = INITIALIZER ] ;
auto NAME = INITIALIZER ;
```

A local variable is visible from its declaration to the end of the enclosing block (§5.2); reading it after that block has closed is a compile-time error.

`auto` infers the type from the initializer and requires one; `auto x;` is a compile-time error.  The inferred type is fixed at the declaration, and later assignments will be checked against it.  `auto` is accepted in local, global, and member declarations.

If the variable's type declares an `init` emitter, it fires immediately after the declaration and before the initializer is assigned (§8.5).  A routine can declare more locals than the Z-machine's per-routine limit (§18.10).

**Example**

```bgl
class Room : object { }
Room myRoom { }

auto x = 5;          // int
auto s = "hello";    // stringLiteral
auto r = myRoom;     // Room, the object's class
```

## 3.7 References: `ref` and `:=`

**Syntax**

```syntax
ref TYPE NAME := EXPRESSION ;    // bound at declaration
ref TYPE NAME ;                    // starts empty
SLOT := EXPRESSION ;               // rebind
```

A class-typed slot is either an **owning slot** or a **reference slot**.  A global, member, or local declared without `ref` and with no value owns an instance:  always for a struct (§8.2.7), and for a reference class as its own object (§2.10.3).  A reference-class slot declared with a value refers to that instance, as do parameters of a reference class and loop variables.  A slot of a primitive type holds the value itself (§2.10.3) and can't take `ref`.

**Reference slots.**  A slot declared `ref` owns nothing; it names an instance owned elsewhere.  It is empty (`null`) until bound.  `ref` is valid on file-scope and local variables, on parameters, and on class and object members, of any class type; on a primitive type, or on an `extern` or `const` declaration, it is a compile-time error.  A `ref` parameter is bound to its argument.  A slot which can be empty is declared `ref`.  A member of a struct whose type is that class must be `ref`.

**Binding and assignment.**  `:=` binds a `ref` slot:  it stores the reference and never dispatches `operator =`.  Both sides must be the same class, or the right side a subclass; binding an unrelated class or a non-instance value is a compile-time error.  `:=` is not overloadable, and on a slot not declared `ref` it is a compile-time error.

`=`, by contrast, is the assignment of the slot's type, the same in a declaration and a statement and on a plain or a `ref` slot:  a reference class's re-points, and a struct's copies, into the referent when the slot is a bound reference.  Reads and member writes through a reference chain normally (`node.next.id`).

**Declaration pairing.**  A `ref` declaration, a member's included, binds with `:=`, whatever its type; `=` on a `ref` declaration is a compile-time error.

**Example**

```bgl
struct Node { int id; ref Node next; Node operator = (Node o){ id = o.id; return self; } }

// … in a routine:
Node first;   first.id = 1;
Node second;  second.id = 2;
ref Node r := first;          // bind
r.id = 9;                     // through the reference: first.id is now 9
r := second;                  // rebind: first keeps its value
first.next := second;         // a ref member is bound the same way
```

**See also** §4.13, §5.15.

## 3.8 Identifier Resolution

An identifier is resolved by searching three tiers in order, and the first match wins.  An identifier which matches no tier is undeclared, a compile-time error.

### 3.8.1 Local Scope

The first tier is the enclosing function, searched in this order:

1. Parameters of the enclosing function.
2. Local variables of the current block.
3. Local variables of enclosing blocks of the same function.

### 3.8.2 Class and Object Scope

Inside a method body, the second tier is the members of the enclosing class or object, including members inherited through the base chain.  A bare member name resolves as `self.name`; `self` is the receiver (§6.6).  Members declared later in the same body resolve normally.

Class scope comes before file scope:  inside a class's or object's code, a bare name which is both a member (method or variable) and a global (function, variable, or type) means the member, as in C++ and C#.  `::name` (§3.9) reaches the file-scope declaration.

**Example**

<!-- doctest: compile -->
```bgl
int twice(int n) { return n * 2; }
class Spot { }

object counter {
    int spot = 0;
    int twice(int n) { return n + n; }
    int run(object x) {
        int a = twice(3);           // the member
        int b = ::twice(3);         // the global function
        if(x.is(::Spot)) a++;       // the type, which the member `spot` hides
        return a + b;
    }
}
```

### 3.8.3 Global Scope

The last tier is everything declared at file scope, searched in this order:

1. Enum values, which share one flat global namespace.
2. Global variables, constants, `extern` declarations, and verb names.
3. Members imported with `#using`, which rank below every global; the directive is covered in §10.4.

**Verb names.**  A verb is an object and follows the same rules as any other identifier; a local or parameter with the same name as a verb takes priority.  A verb can share its name with a `#define` symbol.  The name then means the symbol, except where a verb is expected:  assigned to a `verb`, passed to a `verb` parameter, in an `array<verb>`, as a `case` label on an action, or on either side of `==` or `!=` with a verb.

**Members named after types.**  A member or method can share a name with a type, including a built-in type keyword such as `object`, since a name following `.` is unambiguously a member.  Type names remain reserved for top-level identifiers (§1.5).

## 3.9 The Global-Scope Qualifier `::`

**Syntax**

```syntax
::NAME
::NAME.MEMBER
```

A leading `::` resolves `NAME` at global scope, skipping §3.8.1 and §3.8.2.  It is the counterpart of `self.name`, which selects the member.  It is valid as an lvalue or an rvalue and applies to the head of a dotted path.  Inside a class's or object's code, it reaches a global hidden by a member of the same name (§3.8.2).  If no such global exists, it is an undeclared-identifier error; `::` never falls back to a member.

**Example**

```bgl
int count = 0;

object tally {
    int count = 0;
    void bump() {
        self.count++;       // this object's member
        ::count++;          // the global
    }
}
```

## 3.10 Shadowing

Local variables, parameters, and `for`-loop variables are checked against the enclosing scopes.  A local can shadow a global; inside its scope the bare name is the local and `::name` (§3.9) is the global.

**Errors.**
- Re-declaring a local which an enclosing block still has open, or a parameter of the same function.  A nested declaration doesn't shadow the outer name, so the two must have different names.  Blocks which don't enclose one another can reuse a name freely.

**Warnings.**
- Shadowing a global variable, function (a library routine's too), object, or type name; the warning names what is shadowed.  Globals of the symbolic-constant kinds `attribute`, `property`, `verb`, and `grammarToken` are exempt, since they name compile-time constants, not runtime storage.
- A local or loop variable shadowing a direct member of the enclosing class or object, or a member inherited from a base class; `self.name` reaches the member.  A parameter can share a member's name without a warning, as in C# and C++:  `void resize(int width) { self.width = width; }`.
- A lambda-local variable shadowing a capturable outer local or parameter (§4.14).

A member overriding a base-class member is covered in §8.7.3, and two file-scope declarations of one name in §3.1.

**Example**

```bgl
int score = 0;
void foo() {
    int score = 5;       // warning: shadows global
    score++;             // the local: 6
    ::score = score;     // the global: 6
}
```

## 3.11 The `asI6` and `asBgl` Clauses

**Syntax**

```syntax
TYPE NAME asI6 I6-NAME ;
object NAME asI6 I6-NAME { … }
TYPE MEMBER asI6 I6-NAME ;              // class or object member
TYPE METHOD ( … ) asI6 I6-NAME { … }
extern TYPE I6-NAME asBgl NAME ;
extern TYPE I6-ROUTINE ( … ) asBgl NAME ;
```

A declaration carries two names:  the one Beguile source uses and the one which reaches the Inform 6 output.  They are the same unless a clause says otherwise.

**The declared name is always the name in the language which defines the thing; the clause names it in the other one.**  A plain declaration is defined in Beguile, so the declared name is the Beguile one and `asI6` *creates* the name Inform 6 will get.  An `extern` declaration adopts a symbol Inform 6 already defines, so the declared name is the I6 one and `asBgl` *names* it for Beguile.

```bgl
attribute heightened asI6 excited;   // a new attribute: Beguile says heightened, I6 says excited
extern attribute light asBgl lit;    // I6 already has light; Beguile says lit
```

`asI6` on an `extern` is a compile-time error, and so is `asBgl` without `extern`.

`asI6` is valid on any typed instance declaration, on a named object definition (including instances of subclasses such as `room Name asI6 place { }`), and on class and object members, where it follows the member name.  It is ignored on operator methods; on a type declaration (`extern class`, `alias class`) or on a free function it is a compile-time error.  On an `extern` function `asBgl` follows the parameters, as `asI6` does on a method:  `extern int I6Twice(int x) asBgl twice;`.  Usually, you will reach for either clause because the name required on one side is a keyword or reserved word on the other (§16.9).

A member named like a class declared in Beguile, such as `Box box;` beside `class Box`, needs no `asI6` (§16.9).

Both clauses cross the language boundary; `alias` never does.  `alias class Foo for Bar` (§8.2.4), `alias name for Type;`, and `alias name = Target;` (§10.2) each introduce a second *Beguile* name for something already named in Beguile, and leave the emitted I6 untouched.

**Example**

```bgl
extern attribute light asBgl lit;
object myHook asI6 hook { … }
class Widget : object {
    int count asI6 internalCount;
    void refresh() asI6 _widgetRefresh { … }
}
```

## 3.12 `superposed`

**Syntax**

```syntax
superposed DECLARATION
```

A declaration with the `superposed` qualifier (§3.2) is part of the program only if something references it; one that nothing names is absent from the story file and costs nothing.  It can qualify a global function, a file-scope global variable or array, a whole object declaration, or a whole class declaration.

- **Reference.**  A declaration is **materialized**, made part of the program, the first time its name is used.  For a class, a use is a static instance, a subclass, `new` on a pooled class, an `is` test, or any typed use.
- **Transitive.**  A materialized declaration's own references materialize in turn; a superposed declaration can freely reference other superposed declarations.
- **Whole declarations, plus methods.**  Inside a class body it applies only to a `static` method (`static superposed TYPE NAME(…)`), which then materializes only when referenced (§8.4); on a non-`static` method it has no effect and the compiler will issue a warning.  Inside an object body it applies to a method, which stays in the object only if it is called:  as `OBJECT.METHOD` from code which is part of the program, or from another of the object's methods which stays.  A method called only from Inform 6 code which the compiler doesn't emit is not seen, and drops.  On an object's data member it is a compile-time error.
- **Rejected on** `extern`, `emitter`, and `alias` classes, which have no definition to withhold, and on `extend class`, where it belongs to the original declaration.

The `omitUnusedRoutines` setting (§15.3) is complementary:  `superposed` withholds a declaration which is never referenced, while `omitUnusedRoutines` asks the I6 compiler to drop routines which were emitted and remain unreferenced.

**Example**

```bgl
superposed array<char> vowels = "aeiou";

superposed bool charIsVowel(char c){
    for(char v in vowels) if(v == c) rtrue;   // materializes vowels as well
    rfalse;
}

superposed object worldHelpers {
    superposed array<object> getAll() { … }      // dropped unless something calls worldHelpers.getAll
    superposed int count() { … }
}
extend bgl { alias world = worldHelpers; }   // bgl.world.getAll() materializes worldHelpers
```

> ***Note:** An `alias` value member (§10.2) references its target only where the alias is used, so an alias to a superposed object keeps the object absent until the alias is used; an `auto` member references its target unconditionally.*

**See also** §10.2, §15.3, §18.7 (placement of a materialized class), §18.9 (emission).

## 3.13 `literal`

`literal` qualifies a variable, a member, or an array's element type, as well as a parameter (§6.3).  On a variable or member it covers the whole type:

<!-- doctest: skip -->
```bgl
literal func<void> onStart = intro;              // a global
class door { literal func<void> onOpen; }        // a member
array<literal func<eVerdict>> rules = { r1 };    // the elements
```

An initializer, an assignment, a subscript write, `+=`, or an array method which stores its argument must then supply a literal value (§6.3).  Reading a `literal` variable or member yields a literal value.
