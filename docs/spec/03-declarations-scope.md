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
<!-- /toc -->


## 3.1 Program Structure

A program is one or more source files. The declarations at the outermost level of a file (types,
classes, enums, variables, functions, objects, verbs and grammar) constitute the **global scope** and
are visible throughout the entire compilation. Declarations may appear in any order, and a name may
be used before it is declared; see §18.3 for the pre-scan.

Every program has a `Main` function as its entry point (§6.7). General-purpose libraries such as
the Inform 6 Standard Library and PunyInform define `Main` themselves and expect a library-specific
entry point, such as `Initialise`, instead (§23.3.1).

A global name must be unique across every kind of global declaration: declaring a variable, function,
class, object or enum with the name of an existing global of any kind is a compile-time error (§19.2).

## 3.2 Declaration Qualifiers

**Syntax**

```syntax
[ ⟨qualifier⟩ … ] ⟨declaration⟩
```

**Description**

A declaration may be preceded by one or more qualifiers, in any order: `emitter replace void foo()`
and `replace emitter void foo()` are equivalent. Each qualifier is specified in the chapter that owns
the construct it modifies.

| Qualifier | Meaning | See |
|---|---|---|
| `const` | Read-only variable or member. | §3.4 |
| `static` | Member belongs to the type rather than to an instance. | §8.3.3 |
| `extern` | Declared in Inform 6; registered for type-checking only, produces no output. | §15.4 |
| `emitter` | The body is an I6 template expanded at each use. | §7.2 |
| `extend` | Adds members to an existing class, object, enum or array. | §8.7.1, §11.10, §12.11 |
| `alias` | Another name for an existing type or value. | §8.2.4, §10.2 |
| `replace` | Replaces an already-declared function or member. | §6.5, §8.7.2 |
| `default` | A base-class member that a derived declaration may override without warning. | §8.7.3 |
| `explicit` | A conversion operator that fires only under a cast. | §9.4 |
| `superposed` | A routine, global, object or class that is emitted only if it is used. | §3.12 |
| `typesealed` | A member whose type a derived class may not change. | §8.2.8 |
| `byVal` | A class whose parameters are passed by value. | §8.2.7 |
| `inline` | A member variable that is a positional slot for inline object construction. | §8.3.5, §11.3.1 |
| `ref` | A local or member that references an instance owned elsewhere. | §3.7 |
| `additive` | A property whose values accumulate along the class chain. | §11.7.2 |

The following combinations are compile-time errors: `explicit` on anything but `operator()`; `const`
with `static`; `static` with `emitter`; `explicit` with `const` or `static`; `alias` with `extern`;
`alias` with `emitter`; `default` in an object or verb body.

`global` is not a qualifier. A variable is global by being declared at file scope (§3.3); the compiler
does not recognize `global` before a declaration.

## 3.3 Global Variables

**Syntax**

```syntax
⟨type⟩ ⟨name⟩ [ = ⟨initializer⟩ ] ;
auto ⟨name⟩ = ⟨initializer⟩ ;
```

**Description**

A variable declared at file scope is a global. An initializer that is a constant expression — a
literal, constant arithmetic, an object or routine name, or a `#define` value — is part of the
declaration. **Any other initializer is applied at startup**, in `bglInit()`, in declaration order:
it may call a routine or read a global declared before it, and the variable holds `0` until
`bglInit()` runs (§21.2). A class-typed global whose type declares a parameterless `init` is applied
at startup for the same reason. `auto` infers the type from the initializer (§3.6). A global name must be unique (§3.1), and a local may not
share a name with a global (§3.10).

**Example**

```bgl
bool isGood = true;
int score = 5 + 3;
string playerName;
```

## 3.4 Constants

**Syntax**

```syntax
const ⟨type⟩ ⟨name⟩ = ⟨value⟩ ;
extern const ⟨type⟩ ⟨name⟩ ;
```

**Description**

`const` marks a variable as read-only. Assigning to it, including `++`, `--` and compound assignment,
is a compile-time error. `extern const` declares a constant that is defined in Inform 6: it is
registered for type-checking, produces no output, and takes no initializer.

**Example**

```bgl
const int MAX_SCORE = 2;
extern const int STUCK_PE;
```

## 3.5 Extern Variables

**Syntax**

```syntax
extern ⟨type⟩ ⟨name⟩ ;
```

**Description**

An `extern` variable is declared in Inform 6 and registered for type-checking only. It produces no
output and cannot be initialized. It may be read and assigned; `extern const` (§3.4) is read-only.
Other `extern` declarations are specified in §15.4.

**Example**

```bgl
extern int score;
extern object location;
```

## 3.6 Local Variables

**Syntax**

```syntax
⟨type⟩ ⟨name⟩ [ = ⟨initializer⟩ ] ;
auto ⟨name⟩ = ⟨initializer⟩ ;
```

**Description**

A local variable is visible from its declaration to the end of the enclosing block (§5.2); reading it
after that block has closed is a compile-time error. Two blocks that do not enclose one another may
each declare the same name (§3.10). `auto`
infers the type from the initializer and requires one; `auto x;` is a compile-time error. The inferred
type is fixed at the declaration and later assignments are checked against it. `auto` is accepted in
local, global and member declarations.

If the variable's type declares an `init` emitter, it fires immediately after the declaration and
before the initializer is assigned (§8.5). Locals beyond the Z-machine's per-routine limit are
spilled to the frame pool by the compiler (§18.10). Shadowing rules are in §3.10.

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
ref ⟨type⟩ ⟨name⟩ := ⟨expression⟩ ;    // local: bound at declaration
ref ⟨type⟩ ⟨name⟩ ;                    // member: starts empty
⟨slot⟩ := ⟨expression⟩ ;               // rebind
```

**Description**

A class-typed slot is either an **owning slot** or a **reference slot**.

**Owning slots.** A local or member of a class type normally owns an instance: a local's members are
zero-initialized at routine entry, and a class-typed member is created with its host (§8.3.4). `=`
copies into an owning slot by dispatching the type's `operator =`. A class that has stored members,
does not inherit from `object`, and declares no `operator =` has no copy semantics, so assigning into
a slot of that type is a compile-time error; the remedies are to declare `operator =`, mark the slot
`ref`, or inherit from `object`. Classes derived from `object` use reference semantics, and classes
with no stored members have nothing to copy.

**Reference slots.** A slot declared `ref` owns nothing; it names an instance owned elsewhere. It is
empty (`nothing`) until bound, so `if (!slot)` distinguishes an unbound slot from a bound one. `ref` is
valid on local variable declarations and on class and object members; on a parameter or on an `extern`
or `const` declaration it is a compile-time error. A member whose type is its own class must be `ref`.

**Binding and assignment.** `:=` binds a reference: it stores the reference and never dispatches
`operator =`. Both sides must be the same class, or the right side a subclass; binding an unrelated
class or a non-instance value is a compile-time error. `:=` is not overloadable. A plain `=` on a
bound reference assigns *through* it, dispatching `operator =` into the referent exactly as on an
owning slot, and so requires the type to have copy semantics. Reads and member writes through a
reference chain normally (`node.next.id`).

**Declaration pairing.** A `ref` declaration binds with `:=`; `=` on a `ref` declaration is a
compile-time error, and so is `:=` on a slot that is not `ref`.

**Example**

```bgl
class Node { int id; ref Node next; }

Node first;   first.id = 1;
Node second;  second.id = 2;
ref Node r := first;          // bind
r.id = 9;                     // through the reference: first.id is now 9
r := second;                  // rebind: first keeps its value
first.next := second;         // a ref member is bound the same way
```

**Notes**

A `ref` slot may be bound to a pooled-class instance created with `new`
(`holder.slot := new pooled();`); pooled classes are specified in §8.2.6. Parameters of class type are
passed by reference unless the class is declared `byVal` (§8.2.7).

**See also** §4.13, §5.15.

## 3.8 Identifier Resolution

An identifier is resolved by searching three tiers in order; the first match wins and later tiers are
not searched. An identifier that matches no tier is undeclared, a compile-time error.

### 3.8.1 Local Scope

1. Parameters of the enclosing function.
2. Local variables of the current block.
3. Local variables of enclosing blocks of the same function.

### 3.8.2 Class and Object Scope

Inside a method body, members of the enclosing class or object, including members inherited through
the base chain. A bare member name resolves as `self.name`; `self` is the receiver (§6.6).
Members declared later in the same body resolve normally.

### 3.8.3 Global Scope

1. Enum values, which share one flat global namespace.
2. Global variables, constants, `extern` declarations and verb names.
3. Members imported with `#using`, which rank below every global; the directive is specified in §10.4.

**Verb names.** A verb is an object and follows the same rules as any other identifier; a local or
parameter with the same name as a verb takes priority.

**Ambiguity.** Inside an object method body, a bare identifier that resolves at this tier and is also
a property of the enclosing object (own or inherited) is resolved to the global candidate and the
compiler issues a warning; `self.X` selects the property and `::X` (§3.9) the global. Inherited *methods* are
not included in this check.

**Members named after types.** A member or method may share a name with a type, including a built-in
type keyword such as `object`; a name following `.` is unambiguously a member. Type names remain
reserved for top-level identifiers (§1.5).

## 3.9 The Global-Scope Qualifier `::`

**Syntax**

```syntax
::⟨name⟩
::⟨name⟩.⟨member⟩
```

**Description**

A leading `::` resolves `⟨name⟩` at global scope, skipping §3.8.1 and §3.8.2. It is the counterpart of
`self.name`: where `self.name` selects the member, `::name` selects the global. It is valid as an
lvalue and as an rvalue and applies to the head of a dotted path. `::name` suppresses the ambiguity
warning of §3.8.3. If no such global exists it is an undeclared-identifier error; it never falls back
to a member.

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

**Description**

Local variables, parameters and `for`-loop variables are checked against the enclosing scopes.

**Errors.**
- Shadowing a global variable. Globals of the symbolic-constant kinds `attribute`, `property`, `verb`
  and `grammarToken` are exempt: they name compile-time constants, not runtime storage.
- Shadowing a registered type name (a class or an enum).
- Re-declaring a local that an enclosing block still has open, or a parameter of the same function.
  A nested declaration does not shadow the outer name — it shares its storage — so the two must have
  different names. Blocks that do not enclose one another may reuse a name freely; their lifetimes
  do not overlap, and a name is not visible after its block closes.

**Warnings.**
- Shadowing a direct member of the enclosing class or object, or a member inherited from a base
  class; `self.name` reaches the member.
- A lambda-local variable shadowing a capturable outer local or parameter (§4.14).
- A member overriding a base-class member; `replace` or `default` suppresses the warning (§8.7.3).

**Two file-scope declarations of one name.** A function and a variable or object may share a
file-scope name, which happens whenever an author declares one of the unprefixed names the library
publishes (`print`, `log`, and the article rules before they moved to `bgl.printRules` — §21.11).
They are told apart by **use**, not by declaration order:

| Use | Resolves to |
|---|---|
| `name(args)` | the function |
| `name.member`, `name = v` | the variable or object |
| a bare `name`, with no variable of that name declared | the function, as a `func<>` reference |

A function reference supports neither a member access nor assignment, so the two readings never
compete. Shadowing a name you then call unqualified resolves to the variable, as ordinary shadowing
does — so do not shadow something you still mean to call.

**Example**

```bgl
int score = 0;
class Counter { int n = 0; }
void foo() {
    int score = 5;       // error: shadows global
    int Counter = 0;     // error: shadows class
}
```

## 3.11 The `asI6` and `asBgl` Clauses

**Syntax**

```syntax
⟨type⟩ ⟨name⟩ asI6 ⟨i6 name⟩ ;
object ⟨name⟩ asI6 ⟨i6 name⟩ { … }
⟨type⟩ ⟨member⟩ asI6 ⟨i6 name⟩ ;              // class or object member
⟨type⟩ ⟨method⟩ ( … ) asI6 ⟨i6 name⟩ { … }
extern ⟨type⟩ ⟨i6 name⟩ asBgl ⟨name⟩ ;
```

**Description**

A declaration carries two names: the one Beguile source uses and the one that reaches the Inform 6
output. They are the same unless a clause says otherwise, and which clause applies follows from
where the thing is defined.

**The declared name is always the name in the language that defines the thing; the clause names it
in the other one.** A plain declaration is defined in Beguile, so the declared name is the Beguile
one and `asI6` *creates* the name Inform 6 will get. An `extern` declaration adopts a symbol Inform 6
already defines, so the declared name is the I6 one and `asBgl` *names* it for Beguile.

```bgl
attribute heightened asI6 excited;   // a new attribute: Beguile says heightened, I6 says excited
extern attribute light asBgl lit;    // I6 already has light; Beguile says lit
```

Each clause is therefore tied to one side of `extern`. `asI6` on an `extern` is a compile-time error
— there is no name to create, because the symbol already exists — and `asBgl` without `extern` is an
error for the mirror reason. That is what keeps the reading unambiguous: **`asI6` never names an
existing symbol, and `asBgl` never invents one.** The pair reads as one mechanism with two
directions: each clause names the side its keyword ends in.

`asI6` is valid on any typed instance declaration, on a named object definition (including instances
of subclasses such as `room Name asI6 place { }`), and on class and object members, where it follows
the member name. It is ignored on operator methods; on a type declaration (`extern class`,
`alias class`) or on a free function it is a compile-time error. The usual reason to reach for either
clause is that the name required on one side is a keyword or reserved word on the other (§15.9).

Both clauses cross the language boundary. `alias` never does: `alias class Foo for Bar` (§8.2.4),
`alias name for Type;` and `alias name = Target;` (§10.2) each introduce a second *Beguile* name for
something already named in Beguile, and leave the emitted I6 untouched.

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
superposed ⟨declaration⟩
```

**Description**

`superposed` is a declaration qualifier (§3.2). A declaration qualified `superposed` is part of the
program only if something references it; a superposed declaration that nothing names is absent from
the story file and costs nothing. It may qualify a global function, a file-scope global variable or
array, a whole object declaration, or a whole class declaration.

- **Reference.** A declaration is **materialized**, made part of the program, the first time its name
  is used. For a class, a use is a static instance, a subclass, `new` on a pooled class, an `is`
  test, or any typed use. Reference matching is case-insensitive, like all Beguile identity.
- **Transitive.** A materialized declaration's own references materialize in turn; a superposed
  declaration may freely reference other superposed declarations.
- **Whole declarations, plus `static` methods.** `superposed` applies to a function, global, object or
  class. Inside a class body it applies only to a `static` method (`static superposed ⟨type⟩ ⟨name⟩(…)`),
  which then materializes only when referenced (§8.4); on a non-`static` method it has no effect and
  the compiler issues a warning.
- **Rejected on** `extern`, `emitter` and `alias` classes, which have no definition to withhold, and
  on `extend class`, where it belongs to the original declaration.
- `superposed` may appear in any position among the qualifiers.

The `omitUnusedRoutines` setting (§17.3) is complementary: `superposed` withholds a declaration that
is never referenced, while `omitUnusedRoutines` asks the I6 compiler to drop routines that were
emitted and remain unreferenced.

**Example**

```bgl
superposed array<char> vowels = "aeiou";

superposed bool charIsVowel(char c){
    for(char v in vowels) if(v == c) rtrue;   // materializes vowels as well
    rfalse;
}

superposed object worldHelpers {
    array<object> getAll() { … }
}
extend bgl { alias world = worldHelpers; }   // bgl.world.getAll() materializes worldHelpers
```

**Notes**

An `alias` value member (§10.2) references its target only where the alias is used, so an alias to
a superposed object keeps the object absent until the alias is used; an `auto` member references its
target unconditionally.

**See also** §10.2, §17.3, §18.7 (placement of a materialized class), §18.9 (emission).
