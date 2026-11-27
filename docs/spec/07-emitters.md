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
- [7.8 Emitter Members on Enums and Bnums](#78-emitter-members-on-enums-and-bnums)
- [7.9 Emitters and Functions Compared](#79-emitters-and-functions-compared)
<!-- /toc -->

## 7.1 What an Emitter Is

An **emitter** is a function-like declaration whose body is an Inform 6 template rather than Beguile statements.  No routine exists for it:  the body text is substituted in place at every use site, with its **substitution tokens** (`$self`, `$paramName`, …) replaced by the expressions at that site.  The use site itself is ordinary, type-checked Beguile:  an emitter has a return type and typed parameters, and its result takes part in expressions like any other value.

Emitters can be declared at global scope (§7.5), as members of any class, as members of an enum or bnum (§7.8), or grouped in an emitter namespace (§7.7).

Like functions, emitters can be **overloaded** (§6.4).  Class-member emitters with rules of their own (operator emitters, `init`/`deinit`, and the conversion and accessor operators) are covered in §8.5 and §9.

## 7.2 Emitter Functions

**Syntax**

```syntax
emitter RETURN-TYPE NAME( [ TYPE PARAM [, …] ] ) { I6-TEMPLATE }
```

`emitter` is a declaration qualifier (§3.2) and can appear in any order with the other qualifiers.  The body between the braces is raw I6 text:  it is not parsed as Beguile, and only substitution tokens (§7.3) and `##` directives (§7.4) are recognized in it.  Everything else, including single-hash directives such as `#ifdef`, passes through to the output unchanged.

- The return type is the type of the substituted expression at the use site.  An `emitter void` function can be used only as a statement.
- Parameters are referenced in the body as `$name`.  The bare name is *not* substituted, so a body can freely mention an I6 identifier which happens to share a parameter's name.
- An emitter always has a body.  The one exception is the bodiless conversion operator `emitter T operator();`, which is a pass-through (§9.4).
- `static` and `emitter` can't be combined.
- Recursion is meaningless; an emitter body can't refer to itself as a routine, and a `$i6Expr` chain which comes back around to a body already being expanded is a compile-time error (§7.3.4).

**Example**

```bgl
class Counter {
    int value = 0;
    emitter void increment(){ $self.value++ }
}

Counter c { }

// … in a routine:
c.increment();      // the body is substituted here with $self = c
```

**See also** §8.2 (which class forms require, permit, or imply `emitter` on members).

## 7.3 Substitution Tokens

Every substitution token begins with `$`.  The tokens below are recognized in every emitter body; a feature can add a token of its own, documented with that feature (`$selfsub`, §16.8).  Appendix G is the one-page index.

| Token | Replaced with |
|---|---|
| `$self` | In an operator or assignment emitter, the receiver expression with its trailing `.member` removed when the receiver is a member access (`obj` for `obj.score + 1`); otherwise, and in a method emitter, the receiver itself (`x` for `x + 1`, `container.children` for `container.children.length`); a method emitter on `parent` or `attributes` (§11.5) is the exception and receives the owner.  Not meaningful in a global emitter. |
| `$val` | The full receiver expression as written:  `obj.score` for `obj.score + 1`; otherwise identical to `$self`. |
| `$host` | The object a proxy member (§7.3.2) is accessed on, the owner of the proxy:  the receiver with its trailing `.member` removed, in every kind of emitter.  For a receiver that is not a member access, `$host` equals `$self`. |
| `$paramName` | The argument expression supplied for the parameter of that name. |
| `$target` | The assignment target as a full lvalue path (`obj.prop`, or `x`); see below. |
| `$prop` | In an `array<T>` emitter, the property name when the array is a class or object member (§12.7); `0` for a global or local array.  Using it in any other emitter is a compile-time error. |
| `$opref(op[, T])` | A callable reference to the receiver type's `operator op` (§7.3.1). |
| `$oprefReq(op[, T])` | As `$opref`, but a missing operator is a compile-time warning (§7.3.1). |
| `$i6Name(path)` | The identifier Inform 6 knows the named declaration by (§7.3.3). |
| `$i6Expr(expr)` | The I6 which a Beguile expression emits, expanded inline (§7.3.4). |

**`$target`.**  When the emitter's result is assigned (`int r = f();`), `$target` is the left-hand side; when the emitter is used as a bare statement, it is a compiler-supplied temporary.  A body which mentions `$target` performs the store itself:  the usual `lhs = body` assignment is suppressed.

### 7.3.1 `$opref` and `$oprefReq`

**Syntax**

```syntax
$opref( OP [, OPERAND-TYPE] )
$oprefReq( OP [, OPERAND-TYPE] )
```

`$opref` is a lookup, not a value.  It substitutes a reference to one of a type's operators.  The type searched is the receiver's **element type** inside an `array<T>` emitter, and the receiver's own type elsewhere.  What is substituted depends on how the operator was declared:

| The operator is | `$opref` substitutes |
|---|---|
| `static` | The name of the free routine; call it as `op(a, b)` |
| an instance member | A routine that sends the operator to its first argument; call it as `op(a, b)` |
| an emitter, or absent | `0` |

When it substitutes `0`, the runtime uses its default.

When a type publishes more than one referenceable overload of the operator, the reference is ambiguous and is a compile-time error; name the operand type to select one:

```bgl
class Money {
    int cents;
    static bool operator == (Money a, Money b) { return a.cents == b.cents; }
    static bool operator == (Money a, int b)   { return a.cents == b; }
    emitter int refMoney(){ $opref(==, Money) }     // $opref(==) alone is ambiguous
}
```

`$oprefReq` resolves exactly as `$opref` does and substitutes `0` in the same cases; however, it also reports a **compile-time warning** when the lookup is empty.  No warning is raised for the built-in word types (`int`, `char`, `object`, …), nor for a call which supplies its own comparator.

**Example**

<!-- doctest: skip -->
```bgl
emitter int  indexOf(T item) { _bglArray.indexOf($self, $prop, $item, $opref(==)) }
emitter void sort()          { _bglArray.sortDefault($self, $prop, $oprefReq(<=>)) }
```

**See also** §4.15 (`Type::operator op`, the same lookup in ordinary code), §9.6.

### 7.3.2 Choosing `$self`, `$val` and `$target`

Which of `$self`, `$val` and `$target` to use depends on the type:

- A **value type** (`int`, `bool`, `string`, …) uses `$val`:  the body acts on the value the property holds, so `obj.score + 1` must substitute `obj.score`, not `obj`.
- A **proxy member** (a storageless type such as `parentProp` whose operators act on the owning object) uses `$self`:  `o.parent == bar` must act on `o`, not on `o.parent`.
- An `operator =` that performs a literal store uses `$target`, so `obj.prop = v` stores through the dotted lvalue.  A proxy `operator =` that redirects to a non-store I6 statement uses `$self`.

**Example**

<!-- doctest: skip -->
```bgl
primitive class int {
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
$i6Name( PATH )
$i6Name( PATH( TYPE [, …] ) )
```

`$i6Name` substitutes the identifier Inform 6 knows a declaration by, which can differ from its Beguile name (an `asI6` clause, §3.11, or a mangled `static` method).

`PATH` is a Beguile path, resolved as an ordinary name would be, including through namespace aliases (`bgl.asm.readChar`).  What is substituted depends on what the path names:

| The path names | `$i6Name` substitutes |
|---|---|
| A global function | Its routine name, or its `asI6` name |
| A `static` method | The free routine's name, `_bgl_CLASS_METHOD`, with the overload discriminator when the class declares more than one |
| An instance method | Its property name; call it as `receiver.(NAME)(…)` |
| A data member | Its property name, or its `asI6` name |
| An object, class or enum | Its emitted name, or its `asI6` name |
| An enum value | The value's bare word |

When the path names an overloaded method, the parenthesized type list selects one.  Omitting it where more than one candidate exists is a compile-time error naming the candidates.

Two cases are compile-time errors rather than a substituted `0`:

- **An emitter.**  Use `$i6Expr` to expand it inline instead (§7.3.4).
- **An unresolved path.**

A body is expanded only where it is used, so a `$i6Name` in an emitter that is never called is never resolved and never reports.

**Example**

```bgl
extern attribute light asBgl lit;
object myHook asI6 hook;

emitter bool isLit(object o)      { ($o has $i6Name(lit)) }          // -> ($o has light)
emitter int  readKey()            { $i6Name(bgl.asm.readChar)(1) }   // -> _bgl_bglOpCodes_readChar(1)
emitter int  scale(int a, int b)  { $i6Name(Money.scale(int, int))($a, $b) }
```

> ***Note:** A global function with no `asI6` clause is emitted under its own name, so a body can simply write that name.  Use `$i6Name` where the emitted identifier is **not** the Beguile one:  anything carrying an `asI6` clause, a member or method (which are emitted as property names), and above all a `static` method, whose mangled name changes as soon as a second overload is declared.*

**See also** §3.11 (`asI6`), §7.3.1 (`$opref`, the same lookup for operators), §4.15, §16.2 (the same token in an `#i6` island).

### 7.3.4 `$i6Expr`

**Syntax**

```syntax
$i6Expr( EXPRESSION )
```

`$i6Expr` substitutes the I6 which a Beguile expression emits.  This is how one emitter reaches another, since an emitter has no routine to call by name.  A body which names a global emitter as plain text is a compile-time error.

The payload is ordinary Beguile, parsed and type-checked at the use site, and a callee's own `##if` gating (§7.4) comes with it.

**Binding.**  The body's tokens stand for **typed values** inside the payload:  `$v` denotes a value of `v`'s declared type whose emitted text is the argument at that use site, and `$self`/`$val`/`$host` do likewise at the receiver's type.  A parameter declared `var` takes the type of the argument at that use site instead.

The payload can therefore mention only this emitter's own parameters and receiver; any other `$name` is a compile-time error.  It can't mention a bare I6 identifier either, only a Beguile symbol.

**Extent.**  The payload is exactly one expression:  either one which produces a value, or a single call which returns nothing.  A `;`-separated list is not accepted; multi-statement logic belongs in a routine, which an emitter body can reach by name (§7.3.3).

**Termination.**  A `$i6Expr` chain which reaches a body already being expanded is a compile-time error, as is a chain deeper than eight expansions.

> ***Note:** A body which names `$v` twice already duplicates the argument expression, evaluating its side effects twice.  Chaining multiplies this:  an outer body passing `$self` into an inner one that names `$val` twice yields four copies of the receiver.  Prefer a routine where the argument is expensive or has side effects.*

**Example**

```bgl
emitter int twice(int v){ ($v * 2) }
emitter int quad (int v){ $i6Expr(twice(twice($v))) }        // -> ((v * 2) * 2)

// bgl.asm.add is an emitter: no routine exists for it, so only expansion can reach it
emitter int sum(int a, int b){ $i6Expr(bgl.asm.add($a, $b)) }

// the callee owns the target gating; this caller inherits it
emitter void reseed(){ $i6Expr(bgl.util.random.seed(0)); }   // z-code -> random(-0)
                                                             // Glulx  -> @setrandom 0
class Counter : object {
    int value;
    emitter int doubled(){ $i6Expr(bgl.asm.add($self.value, $self.value)) }
}
```

**See also** §7.3.3 (`$i6Name`, for a name where no expression is legal), §3.12 (`superposed`), §7.4, §16.2 (the same token in an `#i6` island).

## 7.4 Conditional Text: `##if`, `##else`, `##endif`

**Syntax**

```syntax
##if EXPRESSION
    I6-TEXT
[ ##else
    I6-TEXT ]
##endif
```

Inside an emitter body, the double-hash directives select which body text is substituted.  `EXPRESSION` accepts the same forms as `#if` (§14.2.5), with the same definedness-versus-value rule.  The directives are evaluated when the emitter is substituted, and are not valid in ordinary Beguile source, where the conditional is `#if`.  `##ifdef` and `##ifndef` don't exist at all:  use `##if SYMBOL` here, `#if SYMBOL` there.

As with `#if`, the condition runs to the end of its line, so the text it controls starts on the next line.  Text after the condition on the same line is a compile-time error.

Single-hash directives are raw I6 and pass through to the output, where they act as I6 compile-time conditionals.  Any other `##name` (for instance, an I6 action constant such as `##Take`) also passes through unchanged, so `$self == ##$v` substitutes the I6 action name of `$v`.

One further double-hash form, `##beguilerSettings.key`, is recognized only in the raw-I6 directive bodies of §14.4.5; it is not substituted in emitter bodies.

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
emitter RETURN-TYPE NAME( [ PARAMS ] ) { I6-TEMPLATE }
```

The declaration is written at file scope.

A global emitter is used like a global function.  `print()` is a global emitter (§21.4).

**Example**

```bgl
emitter void print(stringLiteral str){ print (string)str; }
emitter void print(string str)       { print (string)str; }
emitter void print(var val)          { print val; }
```

## 7.6 Emitter Values

**Syntax**

```syntax
emitter TYPE NAME { I6-TEMPLATE }
```

An emitter without a parameter list is an **emitter value**:  a typed inline expansion used by its bare name, without `()`.  A typed value (`emitter int`, `emitter string`, …) can appear wherever an expression of that type is accepted; an `emitter void` value can be used only as a statement, terminated by `;`.  Emitter values can be declared at global scope, in class bodies, and in object bodies.  On a member, `$self` substitutes as it does for an emitter function.

An emitter value and an emitter function are distinct declarations with distinct uses:

| Declaration | Use |
|---|---|
| `emitter int foo() { … }` | `foo()` |
| `emitter int foo { … }` | `foo` |

Using a value with parentheses is a compile-time error.  A function is used only with parentheses.  A type can therefore expose a value and a zero-argument function of the same name as different members.

**Example**

```bgl
emitter int  wordSize   { WORDSIZE }
emitter void setBold    { style bold }

// … in a routine:
int ws = wordSize;          // -> WORDSIZE
setBold;                    // statement form
```

## 7.7 Emitter Namespaces

**Syntax**

```syntax
emitter NAME {
    RETURN-TYPE MEMBER( [ PARAMS ] ) { I6-TEMPLATE }
    …
}
```

An emitter namespace groups emitters under one name without declaring a class.  Members are called as `name.member(…)`.

- Every member is an emitter; the `emitter` keyword on a member is optional.
- The namespace name is not a type and can't declare a variable.
- An emitter namespace can't be inherited from or extended; either is a compile-time error.

An `emitter class` (§8.2.3), by contrast, *is* a type.

`style` is a built-in emitter namespace.  It coexists with the `style` struct of `<glulxWindow>` (§22.7.7), since a call `style.member(…)` reaches the namespace while the inline-object form `style { … }` builds a value of the class.

**Example**

```bgl
emitter style {
    void italics() { style underline; }
    void roman()   { style roman; }
}

// … in a routine:
print($"{style.italics()}Italic text{style.roman()}");
```

**See also** §10 (namespaces in general:  objects and emitter classes as containers, `alias`, `#using`); §10.3 (alias members on emitter classes, for composing namespaces hierarchically).

## 7.8 Emitter Members on Enums and Bnums

**Syntax**

```syntax
enum NAME { VALUE, …, EMITTER-MEMBER … }
bnum NAME { VALUE, …, EMITTER-MEMBER … }
extend enum NAME { EMITTER-MEMBER … }
extend bnum NAME { EMITTER-MEMBER … }

EMITTER-MEMBER ::= emitter TYPE METHOD( [ PARAMS ] ) { I6-TEMPLATE }
                   | emitter TYPE PROPERTY { I6-TEMPLATE }
```

An enum or bnum can host emitter members.  They are substituted at the use site with `$self` bound to the value and `$paramName` to each argument.  They are declared in the enum body, or added later with `extend enum` (`extend bnum` for a bnum), using the same member forms as an emitter class.  A member can be used on a bare value or on an enum-typed variable, and before the declaration that adds it.

Only emitter methods and values can be attached.  `operator` overloads, `static` members, and plain members are compile-time errors in an enum body.  The methods are not reachable as a nameable type.

**Example**

```bgl
enum eDirection {
    north, south, east, west,
    emitter int bump()      { ($self + 100) }
    emitter int plus(int n) { ($self + $n)  }
}
extend enum eDirection {
    emitter int tenfold     { ($self * 10) }
}

int a = north.bump();       // -> 101
eDirection d = south;
int b = d.bump();           // -> 102
int c = east.plus(10);      // -> 13
int e = west.tenfold;       // -> 40
```

**See also** §2.7 (enumerations).

## 7.9 Emitters and Functions Compared

The following table lays the two side by side:

| | Function | Emitter |
|---|---|---|
| Body | Beguile statements, compiled to a routine | I6 template, substituted at each use site |
| Parameters in the body | Bare name (`myParam`) | `$myParam` |
| Receiver in the body | `self` | `$self` / `$val` / `$host` |
| Overloading | Supported, by parameter types (§6.4) | Supported, by parameter types |
| Recursion | Supported | Not meaningful; a `$i6Expr` chain which revisits a body is an error (§7.3.4) |
| In an `extern class` body | `static` only, with a body (§16.4.3) | Must have a body (or `;` for a pass-through conversion) |
| At global scope | Yes | Yes |

For a raw-I6 block inside a function body, see `#i6` (§16.2).
