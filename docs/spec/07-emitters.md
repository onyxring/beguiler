# 7 Emitters

<!-- toc -->
- [7.1 What an Emitter Is](#71-what-an-emitter-is)
- [7.2 Emitter Functions](#72-emitter-functions)
- [7.3 Substitution Tokens](#73-substitution-tokens)
  - [7.3.1 `$opref` and `$oprefReq`](#731-opref-and-oprefreq)
  - [7.3.2 Choosing `$self`, `$val` and `$target`](#732-choosing-self-val-and-target)
  - [7.3.3 `$i6Name`](#733-i6name)
  - [7.3.4 `$i6Expr`](#734-i6expr)
- [7.4 Conditional Text: `##if`, `##else`, `##endif`](#74-conditional-text-if-else-endif)
- [7.5 Global Emitters](#75-global-emitters)
- [7.6 Emitter Values](#76-emitter-values)
- [7.7 Emitter Namespaces](#77-emitter-namespaces)
- [7.8 `operator auto()`](#78-operator-auto)
- [7.9 Emitter Methods on Enums and Bnums](#79-emitter-methods-on-enums-and-bnums)
- [7.10 Emitters and Functions Compared](#710-emitters-and-functions-compared)
<!-- /toc -->

## 7.1 What an Emitter Is

An **emitter** is a function-like declaration whose body is an Inform 6 template rather than Beguile
statements. No routine exists for it: at every use site the body text is substituted in place, with
its **substitution tokens** (`$self`, `$paramName`, …) replaced by the expressions at that site. The
use site itself is ordinary, type-checked Beguile: an emitter has a return type and typed parameters,
and its result participates in expressions like any other value.

Emitters are the mechanism by which library code gives a Beguile type precise control over the
generated I6 without giving up type safety. They may be declared at global scope (§7.5), as members
of any class, as members of an enum or bnum (§7.9), or grouped in an emitter namespace (§7.7).

Unlike functions, emitters may be **overloaded**: several emitters with the same name and different
parameter types may coexist, and the best match is chosen from the argument types at each use site.

Class-member emitters that have their own rules — operator emitters, `init`/`deinit`, conversion and
accessor operators — are specified in §8.5 and §9; this chapter covers the emitter mechanism itself.

## 7.2 Emitter Functions

**Syntax**

```syntax
emitter ⟨returnType⟩ ⟨name⟩( [ ⟨type⟩ ⟨param⟩ [, …] ] ) { ⟨i6-template⟩ }
```

**Description**

`emitter` is a declaration qualifier (§3.2) and may appear in any order with the other qualifiers. The
body between the braces is raw I6 text: it is not parsed as Beguile, and only substitution tokens
(§7.3) and `##` directives (§7.4) are recognized inside it. Everything else, including single-hash
directives such as `#ifdef`, passes through unchanged to the output.

- The return type is the type of the substituted expression at the use site. An `emitter void`
  function may be used only as a statement.
- Parameters are referenced in the body as `$name`. The bare name is *not* substituted, so a body may
  freely mention an I6 identifier that happens to share a parameter's name.
- An emitter always has a body. The one exception is the bodiless conversion operator
  `emitter T operator();`, which is a pass-through (§9.4).
- `static` and `emitter` cannot be combined: an emitter has no routine to make static.
- Recursion is meaningless; an emitter body cannot refer to itself as a routine. A body may
  reach another emitter through `$i6Expr` (§7.3.4), and a chain that comes back round to a body
  already being expanded is a compile-time error rather than an unbounded expansion.

**Example**

```bgl
class Counter {
    int value = 0;
    emitter void increment(){ $self.value++ }
}

Counter c;
c.increment();      // the body is substituted here with $self = c
```

**See also** §8.2 (which class forms require, permit, or imply `emitter` on members).

## 7.3 Substitution Tokens

Every substitution token begins with `$`, which keeps it distinct from any raw I6 identifier. The
tokens below are recognized in every emitter body; a feature may add a feature-local token, which is
documented with that feature (`$selfsub`, §15.8). Appendix G is the one-page index.

| Token | Replaced with |
|---|---|
| `$self` | In an operator or assignment emitter, the receiver expression with its trailing `.member` removed when the receiver is a member access (`obj` for `obj.score + 1`); otherwise, and in a method emitter, the receiver itself (`x` for `x + 1`, `container.children` for `container.children.length()`); a method emitter on `parent` or `attributes` (§11.5) is the exception and receives the owner. Not meaningful in a global emitter. |
| `$val` | The full receiver expression as written: `obj.score` for `obj.score + 1`; otherwise identical to `$self`. |
| `$host` | The object a proxy member (§7.3.2) is accessed on, the owner of the proxy: the receiver with its trailing `.member` removed, in every kind of emitter. For a receiver that is not a member access, `$host` equals `$self`. |
| `$paramName` | The argument expression supplied for the parameter of that name. |
| `$target` | The assignment target as a full lvalue path (`obj.prop`, or `x`); see below. |
| `$prop` | In an `array<T>` emitter, the property name when the array is an object member; `0` for a global array. |
| `$opref(op[, T])` | A callable reference to the receiver type's `operator op` (§7.3.1). |
| `$oprefReq(op[, T])` | As `$opref`, but a missing operator is a compile-time warning (§7.3.1). |
| `$i6Name(path)` | The identifier Inform 6 knows the named declaration by (§7.3.3). |
| `$i6Expr(expr)` | The I6 that a Beguile expression emits, expanded inline (§7.3.4). |

**`$self` and `$host`.** The two differ in which emitters strip the trailing member: `$self` strips
it only in an operator or assignment emitter, whereas `$host` strips it in a method emitter as well,
which is how a method on a proxy member reaches the owner. For `container.children.length()`, `$self`
and `$val` are `container.children` and `$host` is `container`.

**`$target`.** When the emitter's result is assigned (`int r = f();`), `$target` is the left-hand
side; when the emitter is used as a bare statement it is a compiler-supplied temporary. A body that
mentions `$target` performs the store itself: the usual `lhs = body` assignment is suppressed.

### 7.3.1 `$opref` and `$oprefReq`

**Syntax**

```syntax
$opref( ⟨op⟩ [, ⟨operandType⟩] )
$oprefReq( ⟨op⟩ [, ⟨operandType⟩] )
```

**Description**

`$opref` is a lookup, not a value: it substitutes a reference to one of a type's operators so that a
shared runtime routine can be handed a type-aware operation. The type searched is the receiver's
**element type** inside an `array<T>` emitter and the receiver's own type elsewhere. What is
substituted depends on how the operator was declared:

| The operator is | `$opref` substitutes |
|---|---|
| `static` | The name of the free routine; call it as `op(a, b)` |
| an instance member | The property name; call it as `a.(op)(b)` |
| an emitter, or absent | `0` — an emitter has no address and is never referenceable |

The receiving routine must tell the two callable forms apart (`metaclass()` distinguishes a routine
from a property). Publishing an operator is opt-in: a type that declares none yields `0` and the
runtime uses its default.

When a type publishes more than one referenceable overload of the operator, the reference is
ambiguous and is a compile-time error; name the operand type to select one:

```bgl
class Money {
    static bool operator == (Money a, Money b) { return a.cents == b.cents; }
    static bool operator == (Money a, int b)   { return a.cents == b; }
    emitter int refMoney(){ $opref(==, Money) }     // $opref(==) alone is ambiguous
}
```

`$oprefReq` resolves exactly as `$opref` does and substitutes `0` in the same cases, but additionally
reports a **compile-time warning** when the lookup is empty. A library uses it where the default is
not a sound fallback: absence of `==` still finds an element by identity, but absence of `<=>` orders a
class by object address, so `sort()` uses `$oprefReq(<=>)`. No warning is raised for the built-in word
types (`int`, `char`, `object`, …), for which word semantics are the correct answer, nor for a call
that supplies its own comparator.

**Example**

```bgl
emitter int  indexOf(T item) { _bglArray.indexOf($self, $prop, $item, $opref(==)) }
emitter void sort()          { _bglArray.sortDefault($self, $prop, $oprefReq(<=>)) }
```

**See also** §4.15 (`Type::operator op`, the same lookup in ordinary code), §9.6.

### 7.3.2 Choosing `$self`, `$val` and `$target`

**Description**

`$self` and `$val` differ only when the receiver is a property access.

- A **value type** (`int`, `bool`, `string`, …) uses `$val`: the body acts on the value the property
  holds, so `obj.score + 1` must substitute `obj.score`, not `obj`.
- A **proxy member** (a storageless type such as `parentProp` whose operators act on the owning
  object) uses `$self`: `o.parent == bar` must act on `o`, not on `o.parent`.
- An `operator =` that performs a literal store uses `$target`, so `obj.prop = v` stores through the
  dotted lvalue. A proxy `operator =` that redirects to a non-store I6 statement uses `$self`.

**Example**

```bgl
extern class int : _bglObject {
    emitter int  operator +  (int v){ $val + $v }
    emitter int  operator =  (int v){ $target = $v; }
}

extern class parentProp {
    emitter parentProp operator =  (object v){ move $self to $v }
    emitter parentProp operator == (object v){ parent($self) == $v }
}
```

**See also** §21.5.7 (`parentProp` and `childrenProp`).

### 7.3.3 `$i6Name`

**Syntax**

```syntax
$i6Name( ⟨path⟩ )
$i6Name( ⟨path⟩( ⟨type⟩ [, …] ) )
```

**Description**

Substitutes the identifier Inform 6 knows a declaration by. A body is raw I6, so calling a Beguile
routine from one means writing that routine's *emitted* name — which is not the author's to know: an
`asI6` clause renames it (§3.11), a `static` method emits as `_bgl_⟨class⟩_⟨method⟩`, and that mangling
grows a parameter-type discriminator as soon as a second overload of the name is declared. A body
that spells the name itself is therefore wrong the moment an unrelated overload appears. `$i6Name`
asks the compiler for it instead.

`⟨path⟩` is a Beguile path, resolved as an ordinary name would be, including through namespace
aliases (`bgl.asm.readChar`). What is substituted depends on what the path names:

| The path names | `$i6Name` substitutes |
|---|---|
| A global function | Its routine name, or its `asI6` name |
| A `static` method | The free routine's name, `_bgl_⟨class⟩_⟨method⟩`, with the overload discriminator when the class declares more than one |
| An instance method | Its property name — call it as `receiver.(⟨name⟩)(…)` |
| A data member | Its property name, or its `asI6` name |
| An object, class or enum | Its emitted name, or its `asI6` name |
| An enum value | The value's bare word |

When the path names an overloaded method, the parenthesized type list selects one; omitting it where
more than one candidate exists is a compile-time error naming the candidates. The same selection
form appears in `$opref(op, T)` (§7.3.1) and `Type::operator op(T)` (§4.15).

Two cases are compile-time errors rather than a substituted `0`:

- **An emitter.** No routine and no constant is emitted for it, so there is nothing to name. Use
  `$i6Expr` to expand it inline instead. (`$opref` yields `0` for the same situation only because a
  missing operator has a sound fallback in word semantics; a missing *name* has none.)
- **An unresolved path.** An empty substitution would reach Inform 6 as malformed text with nothing
  pointing back at the cause.

A body is expanded only where it is used, so a `$i6Name` in an emitter that is never called is never
resolved and never reports.

**Example**

```bgl
extern attribute lit asI6 light;
object myHook asI6 hook;

emitter bool isLit(object o)      { ($o has $i6Name(lit)) }          // → ($o has light)
emitter int  readKey()            { $i6Name(bgl.asm.readChar)(1) }   // → _bgl_bglOpCodes_readChar(1)
emitter int  scale(int a, int b)  { $i6Name(Money.scale(int, int))($a, $b) }
```

> **When to reach for it.** A global function with no `asI6` clause emits under its own name, so a body
> may simply write that name; `$i6Name` buys nothing there. Use it where the emitted identifier is
> *not* the Beguile one: anything carrying an `asI6` clause, a member or method (which emit as property
> names), a promoted member array, and above all a `static` method — its `_bgl_⟨class⟩_⟨method⟩`
> mangling gains a parameter-type discriminator as soon as a second overload of that name is
> declared, so a spelling that is correct today is silently wrong after an unrelated edit elsewhere.

**See also** §3.11 (`asI6`), §7.3.1 (`$opref`, the same lookup for operators), §4.15, §15.2
(the same token in an `#i6` island).

### 7.3.4 `$i6Expr`

**Syntax**

```syntax
$i6Expr( ⟨expression⟩ )
```

**Description**

Substitutes the I6 that a Beguile expression emits. This is how one emitter reaches another: an
emitter has no routine, so a body cannot call one by name (§7.3.3) — it has to be *expanded*, and
`$i6Expr` is what expands it.

The payload is ordinary Beguile, parsed and type-checked at the use site, so it gets overload
resolution, and a callee's own `##if` gating (§7.4) comes with it rather than being re-written by
every caller.

**Binding.** The body's tokens stand for **typed values** inside the payload: `$v` denotes a value of
`v`'s declared type whose emitted text is the argument at that use site, and `$self`/`$val`/`$host`
likewise at the receiver's type. The payload may therefore mention only this emitter's own
parameters and receiver; any other `$name` is a compile-time error. It may not mention a bare I6
identifier either, since that is not a Beguile symbol — `$i6Expr(__glkHook(33, $a))` resolves only
because `__glkHook` *is* a Beguile function.

**Extent.** Exactly one expression: either one that produces a value, or a single call that returns
nothing, which is what lets an `emitter void` body chain. A `;`-separated list is not accepted —
without locals, control flow or `return` it would look like a function body without being one.
Multi-statement logic still belongs in a routine, which an emitter body can then reach by name
(§7.3.3); a `superposed` routine (§3.12) costs nothing when no body references it.

**Termination.** Because a body is substituted rather than called, a `$i6Expr` chain that reaches a
body already being expanded has no base case. Such a cycle is a compile-time error, as is a chain
deeper than eight expansions.

> **Substituted text is duplicated text.** A body that names `$v` twice already duplicates the
> argument expression, evaluating its side effects twice. Chaining multiplies this: an outer body
> passing `$self` into an inner one that names `$val` twice yields four copies of the receiver.
> Prefer a routine where the argument is expensive or effectful.

**Example**

```bgl
emitter int twice(int v){ ($v * 2) }
emitter int quad (int v){ $i6Expr(twice(twice($v))) }        // → ((v * 2) * 2)

// bgl.asm.add is an emitter — no routine exists for it, so only expansion can reach it
emitter int sum(int a, int b){ $i6Expr(bgl.asm.add($a, $b)) }

// the callee owns the target gating; this caller inherits it
emitter void reseed(){ $i6Expr(bgl.util.random.seed(0)); }   // z-code → random(-0)
                                                             // Glulx  → @setrandom 0
class Counter : object {
    int value;
    emitter int doubled(){ $i6Expr(bgl.asm.add($self.value, $self.value)) }
}
```

**See also** §7.3.3 (`$i6Name`, for a name where no expression is legal), §3.12 (`superposed`),
§7.4, §15.2 (the same token in an `#i6` island).

## 7.4 Conditional Text: `##if`, `##else`, `##endif`

**Syntax**

```syntax
##if ⟨expression⟩
    ⟨i6-text⟩
[ ##else
    ⟨i6-text⟩ ]
##endif
```

**Description**

Inside an emitter body, the double-hash directives select which body text is substituted.
`⟨expression⟩` accepts the same forms as `#if` (§14.2.5): symbols, comparisons, `&&`, `||`, `!` and
parentheses, with the same definedness-versus-value rule. They are evaluated when the emitter is
substituted, and are not valid in ordinary Beguile source, where the conditional is `#if` (§14.2.5).
`##ifdef` and `##ifndef` do not exist at all: use `##if SYMBOL` here, `#if SYMBOL` there.

Single-hash directives are raw I6 and pass through to the output, where they act as I6 compile-time
conditionals. Any other `##name` (for instance an I6 action constant such as `##Take`) also passes
through unchanged, so `$self == ##$v` substitutes the I6 action name of `$v`.

One further double-hash form, `##beguilerSettings.key`, is recognized only in the raw-I6 directive
bodies of §14.4.5; it is not substituted in emitter bodies.

**Example**

```bgl
emitter void newline(){
##if TARGET_GLULX
    glk_put_char(10);
##else
    new_line;
##endif
}
```

**See also** §14.2.4 (pre-defined symbols), Appendix F.

## 7.5 Global Emitters

**Syntax**

```syntax
emitter ⟨returnType⟩ ⟨name⟩( [ ⟨params⟩ ] ) { ⟨i6-template⟩ }
```

The declaration is written at file scope.

**Description**

A global emitter is declared at file scope and is used like a global function, with its body
substituted at each use site. `$self` has no meaning in a global emitter. Global emitters may be
overloaded; the overload is selected by argument type. `print()` and `log()` are global emitters
(§21.4).

**Example**

```bgl
emitter void print(stringLiteral str){ print (string)str; }
emitter void print(string str)       { print (string)str; }
emitter void print(var val)          { print val; }
```

## 7.6 Emitter Values

**Syntax**

```syntax
emitter ⟨type⟩ ⟨name⟩ { ⟨i6-template⟩ }
```

**Description**

An emitter without a parameter list is an **emitter value**: a typed inline expansion used by bare
name, without `()`. A typed value (`emitter int`, `emitter string`, …) may appear wherever an
expression of that type is accepted; an `emitter void` value may be used only as a statement,
terminated by `;`. Emitter values may be declared at global scope, in class bodies, and in object
bodies; on a member, `$self` substitutes as for an emitter function.

An emitter value and an emitter function are distinct declarations with distinct use:

| Declaration | Use |
|---|---|
| `emitter int foo() { … }` | `foo()` |
| `emitter int foo { … }` | `foo` |

Using a value with parentheses is a compile-time error, whether the name is bare, imported with
`#using`, or qualified. A function is used only with parentheses. A type may therefore expose a value
and a same-named zero-argument function as different members.

**Example**

```bgl
emitter int  wordSize   { WORDSIZE }
emitter void setBold    { style bold }

int ws = wordSize;          // → WORDSIZE
setBold;                    // statement form
```

## 7.7 Emitter Namespaces

**Syntax**

```syntax
emitter ⟨name⟩ {
    ⟨returnType⟩ ⟨member⟩( [ ⟨params⟩ ] ) { ⟨i6-template⟩ }
    …
}
```

**Description**

An emitter namespace groups emitters under one name without declaring a class. It is written as
`emitter` followed directly by the name (no `class` keyword) and a body. Members are called as
`name.member(…)`.

- Every member is an emitter; the `emitter` keyword on a member is optional.
- The namespace name is not a type and cannot declare a variable.
- An emitter namespace cannot be inherited from or extended; either is a compile-time error.

An `emitter class` (§8.2.3) differs in that it *is* a type — its purpose is to declare variables of
it — whereas an emitter namespace exists only to be called by name. `style` is a built-in emitter
namespace; it coexists with the `style` value class of `<glulxWindow>` (§22.7.7), because a call
`style.member(…)` reaches the namespace while the inline-object form `style { … }` builds a value of
the class.

**Example**

```bgl
emitter style {
    void italics() { style underline; }
    void roman()   { style roman; }
}

print($"{style.italics()}Italic text{style.roman()}");
```

**See also** §10 — namespaces in general (objects and emitter classes as containers, `alias`, `#using`);
§10.3 — alias members on emitter classes, for composing namespaces hierarchically.

## 7.8 `operator auto()`

**Syntax**

```syntax
⟨type⟩ operator auto() ;
```

**Description**

When a variable is declared with `auto` (§3.6), its type is normally the resolved type of its
initializer. A class that declares `operator auto()` overrides this: the declared return type is the
type `auto` infers for a value of that class.

- Takes no parameters and has no body; only the return type matters.
- At most one per class; a duplicate is a compile-time error.
- A type without `operator auto()` infers as itself.

The literal pseudo-types declare it so that `auto x = 5;` infers `int` rather than `intLiteral`:

| Literal type | `operator auto()` returns |
|---|---|
| `intLiteral` | `int` |
| `charLiteral` | `char` |
| `dictionaryWordLiteral` | `dictionaryWord` |

**Example**

```bgl
extern class intLiteral : _bglObject {
    emitter int operator();     // implicit conversion to int
    int operator auto();        // auto infers int
}
```

**See also** §2.4 (literal pseudo-types), §5.9.1 (`auto` in `for-in`).

## 7.9 Emitter Methods on Enums and Bnums

**Syntax**

```syntax
enum ⟨name⟩ { ⟨value⟩, …, emitter ⟨type⟩ ⟨method⟩( [ ⟨params⟩ ] ) { ⟨i6-template⟩ } }
extend enum ⟨name⟩ { emitter ⟨type⟩ ⟨method⟩( [ ⟨params⟩ ] ) { ⟨i6-template⟩ } }
```

**Description**

An enum or bnum value is a bare word, so an enum can host emitter methods: members substituted at the
call site with `$self` bound to the value and `$paramName` to each argument. They are declared in the
enum body, or added later with `extend enum`, using the same member form as an emitter class. A
method may be called on a bare value or on an enum-typed variable, and may be used before the
declaration that adds it.

Only emitter methods may be attached. `operator` overloads, `static` members, plain members, and
emitter values are compile-time errors in an enum body. A value with methods costs exactly what a
plain value costs, and the methods are not reachable as a nameable type.

**Example**

```bgl
enum eDirection {
    north, south, east, west,
    emitter int bump()      { ($self + 100) }
    emitter int plus(int n) { ($self + $n)  }
}
extend enum eDirection {
    emitter int tenfold()   { ($self * 10) }
}

int a = north.bump();       // → 101
eDirection d = south;
int b = d.bump();           // → 102
int c = east.plus(10);      // → 13
```

**See also** §2.7 (enumerations).

## 7.10 Emitters and Functions Compared

| | Function | Emitter |
|---|---|---|
| Body | Beguile statements, compiled to a routine | I6 template, substituted at each use site |
| Parameters in the body | Bare name (`myParam`) | `$myParam` |
| Receiver in the body | `self` | `$self` / `$val` / `$host` |
| Overloading | Global functions: not supported (one routine per name); class and object methods: by parameter signature (§8.4) | Supported, by parameter types |
| Recursion | Supported | Not meaningful — a `$i6Expr` chain that revisits a body is an error (§7.3.4) |
| In an `extern class` body | Declaration only, no body | Must have a body (or `;` for a pass-through conversion) |
| At global scope | Yes | Yes |

An emitter body is the primary route to I6 capabilities that have no Beguile syntax; for a raw-I6
block inside a function body see `#i6` (§15.2).
