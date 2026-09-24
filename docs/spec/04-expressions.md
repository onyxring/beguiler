# 4 Expressions and Operators

<!-- toc -->
- [4.1 Evaluation](#41-evaluation)
- [4.2 Operands](#42-operands)
- [4.3 Operator Precedence](#43-operator-precedence)
- [4.4 Binary Operator Resolution](#44-binary-operator-resolution)
- [4.5 Arithmetic Operators](#45-arithmetic-operators)
- [4.6 Comparison Operators](#46-comparison-operators)
- [4.7 Logical Operators](#47-logical-operators)
- [4.8 Bitwise and Shift Operators](#48-bitwise-and-shift-operators)
- [4.9 Ternary Operator](#49-ternary-operator)
- [4.10 Optional Chaining, Null Coalescing and Postfix Query](#410-optional-chaining-null-coalescing-and-postfix-query)
- [4.11 Casts](#411-casts)
- [4.12 Address-of `&`](#412-address-of-)
- [4.13 `new`](#413-new)
- [4.14 Lambdas](#414-lambdas)
- [4.15 Operator References](#415-operator-references)
<!-- /toc -->


## 4.1 Evaluation

Every expression has a **resolved type**, which drives operator resolution, type-checking and emitter
dispatch. Operators of equal precedence group left to right, and each binary operator resolves in
turn using the resolved type of the expression to its left. `&&` and `||` evaluate the right operand
only when the left operand does not decide the result; `??` evaluates its right operand only when the
left is null (§4.10); a ternary evaluates only the selected branch (§4.9).

## 4.2 Operands

An expression is one or more operands joined by operators. An operand is one of:

| Operand | Resolved type |
|---|---|
| Literal: integer, string, `@"raw"`, `$"interpolated"`, character, dictionary word | The literal's pseudo-type (§2.4) |
| Identifier | The declared type (resolution: §3.8) |
| `null` | Compatible with any type (§2.5) |
| `self` | The enclosing class (§6.6) |
| Member access `expr.member` | The member's type |
| Call `f(args)` / `expr.m(args)` | The function's return type |
| Subscript `arr[i]` | The array's element type (§12.3) |
| `(expr)` | The type of `expr` |
| `(Type)expr` | The cast target (§4.11) |
| `&expr` | `int` (§4.12) |
| `new Type(args)` | `Type` (§4.13) |
| Lambda literal | `func<…>` (§4.14) |

A literal may have emitter methods called on it directly: `"hello".print()`, `42.someMethod()`.

## 4.3 Operator Precedence

Higher levels bind more tightly. `a + b * c` is `a + (b * c)`; `a > 0 && b < 10` is
`(a > 0) && (b < 10)`; `a <=> b < 0` is `(a <=> b) < 0`.

| Prec | Operators | Kind | Assoc | Meaning |
|:---:|---|---|---|---|
| 14 | `.` `?.` `[]` `()` `v?` `++` `--` | postfix | left | Member access, optional access, subscript, call, postfix query, postfix increment/decrement |
| 13 | `!` `-` `&` `(Type)` `++` `--` | prefix | right | Logical not, negation, address-of, cast, prefix increment/decrement |
| 11 | `*` `/` `%` | infix | left | Multiplicative |
| 10 | `+` `-` | infix | left | Additive |
| 9 | `<<` `>>` `<=>` | infix | left | Shift, three-way comparison |
| 8 | `<` `<=` `>` `>=` | infix | left | Relational |
| 7 | `==` `!=` `?=` `=~` | infix | left | Equality |
| 6 | `&` | infix | left | Bitwise and |
| 5 | `^` | infix | left | Bitwise exclusive or |
| 4 | `\|` | infix | left | Bitwise or |
| 3 | `&&` | infix | left | Logical and |
| 2 | `\|\|` | infix | left | Logical or |
| 1 | `? :` | ternary | — | Conditional (one per statement, §4.9) |
| 1 | `??` | infix | — | Null coalescing (§4.10) |
| 0 | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` `:=` | infix | right | Assignment (§5.5), compound assignment (§5.6), reference binding (§3.7) |

There is no unary bitwise-not operator.

## 4.4 Binary Operator Resolution

A binary operator is resolved against the resolved type of its left operand:

1. Look for `operator op` (emitter or method) on the left type whose parameter accepts the right type.
2. Otherwise, if the left type has a conversion operator (`operator()`) to the right type, fall back
   to the built-in operator.
3. Otherwise, if the right type has a conversion operator to a type for which the left type does
   declare `operator op`, apply that conversion and use the operator.
4. Otherwise, if the left type is known, it is a compile-time error. A `var` operand falls back to the
   built-in operator.

Assignment and arithmetic operators keep the left operand's type. The overloadable operators are
listed in §9.1; the declaration forms are specified in §9.

**Example**

```bgl
class Money {
    int cents = 0;
    emitter bool operator == (Money v){ $self.cents == $v.cents }
    int operator () { return cents; }          // implicit conversion to int
}
Money a; Money b; int five = 5;
bool same = a == b;       // 1: Money declares operator == (Money)
int  sum  = a + five;     // 2: no operator + on Money; a converts to int, built-in + applies
```

## 4.5 Arithmetic Operators

The built-in operators are declared by the runtime core, need no `#include`, and are ordinary
overloadable operators (§9.1) that a type may declare for itself. The table lists every built-in
binary and prefix operator by the types that define it; the compound-assignment forms follow the
binary operator they are built from (§5.6), and the postfix query, optional chaining and null
coalescing are in §4.10.

| Operators | Built in for | Result | Example |
|---|---|---|---|
| `+` `-` `*` `/` `%` | `int`, `uint` (§21.6.1), `float` (§2.3); `+` and `-` also for `char` with a `char` or `int` right operand (§21.7) | The left operand's type | `7 / 2` → `3`; `7 % 2` → `1`; `'a' + 1` → `'b'` |
| `==` `!=` `<` `>` `<=` `>=` | `int`, `uint`, `float`, `char`; `==` and `!=` also for `bool`, `string` (identity of the text) and `object` | `eBool` | `score >= 50`; `noun == lamp` |
| `?=` | No built-in type; it exists so that a type may give it a meaning (§9.5) | `eBool` | — |
| `=~` | `char`: case-insensitive equality (§21.7); `string` with `<string>` (§22.3) | `eBool` | `'A' =~ 'a'` → `true` |
| `<=>` | No built-in type; `<string>` declares it for `string` (§22.3). A type provides it as `operator <=>`, `static` with both operands as parameters or as an instance operator (§9.6); generic containers use it to obtain an ordering | `int`: negative, `0` or positive | `(a <=> b) < 0` |
| `&&` `\|\|` `!` | `bool` and `eBool` (§2.2); `!` on a type that declares `operator !()` uses that emitter (§9.5) | `eBool` | `a > 0 && b < 10` |
| `&` `\|` `^` `<<` `>>` | `int`, `uint`; `&`, `\|` and `^` also combine `bnum` values that share a base (§2.7.2) | The left operand's type | `flags & lit`; `1 << 4` → `16` |

A leading `-` on an integer literal forms a negative literal, whose pseudo-type is
`negativeIntLiteral` (§2.4.1). `<=>` binds more tightly than the relational operators (§4.3).

## 4.6 Comparison Operators

`== != < > <= >= ?= =~ <=>` are tabulated in §4.5.

## 4.7 Logical Operators

`&&`, `||` and prefix `!` are tabulated in §4.5.

## 4.8 Bitwise and Shift Operators

`& | ^ << >>` are tabulated in §4.5; the compound forms `&= |= ^= <<= >>=` are in §5.6.

## 4.9 Ternary Operator

**Syntax**

```syntax
⟨condition⟩ ? ⟨trueExpr⟩ : ⟨falseExpr⟩
```

**Description**

Selects one of two values. A ternary may appear as a call argument, on the right-hand side of an
assignment, in a parenthesized sub-expression, and in the condition and increment parts of a `for`
loop. At most one ternary may appear per statement (a call argument that is a ternary counts), and a
ternary may not be nested in another ternary's condition or branches.

**Type of the result.** When the branches have the same type, that is the type. When one branch's
type is assignable to the other's — a derived class and its base, for instance — the result takes the
more general of the two, whichever side it is written on. A literal counts as the type it denotes,
so `-1` and `0` are both `int`. A branch typed `var` imposes nothing.

When the branches have unrelated types, the result is the **union** of the two (§2.8): a ternary over
a string and a routine is what `string | func<void>` describes, and a union value occupies one word
either way. The destination then decides whether the expression is legal — a union-typed target
accepts it and discriminates with `typeof` (§2.8.1), while a target of one branch's type rejects it
as the mistyping it is.

**Example**

```bgl
print(x > 0 ? "positive" : "non-positive");
int result = (cond ? a : b) + extra;

string | func<void> action = useText ? "nothing happens" : doSomething;  // union of both branches
int wrong = cond ? 1 : "text";   // error: 'int|string' is not assignable to 'int'
```

## 4.10 Optional Chaining, Null Coalescing and Postfix Query

**Syntax**

```syntax
⟨expr⟩?.⟨member⟩
⟨expr⟩?.⟨method⟩(⟨args⟩)
⟨expr⟩ ?? ⟨fallback⟩
⟨expr⟩?
```

**Description**

All three operators are type-driven: the operand's type must declare an `operator ?()` emitter, whose
result is the null test (the declaration is specified in §9.5; the BLR defines it for `object` as
"not `nothing`" and for `string` as "non-zero handle"). Using them on a type without `operator ?()`
is a compile-time error.

**`?.`** accesses a member or calls a method only if the left operand is non-null. In an expression,
each `?.` step tests the value so far; if it is null the whole chain yields `nothing`, otherwise the
step proceeds. As a statement (`x?.remove();`) the operation runs only if the target is non-null.

A `?.` guards **only the step it is attached to**. A plain `.` later in the same chain would
therefore read from whatever that step produced, including `nothing`, so it is a compile-time
error: `x?.a.b` is rejected, and the author writes `x?.a?.b` to guard the second step or `x.a?.b`
when `x` is always present. A chain that begins with a plain `.` is unaffected.

**`??`** yields the left operand if it is non-null, otherwise the right operand, which is evaluated only
in that case.

**Postfix `?`** yields the null test as an `eBool`; `noun?` is `noun != null` for objects, and `!noun?`
negates it. Where a `?` could begin a ternary, as the right operand of a binary operator, it is the
ternary; the postfix query applies only when the `?` ends the expression.

**Example**

```bgl
object dest = actor?.destination ?? location;
string desc = noun?.parent.description;   // parent guarded, .description not
if (noun?) print("something is here");
```

## 4.11 Casts

**Syntax**

```syntax
(⟨type⟩)⟨expr⟩
(⟨instanceName⟩)⟨expr⟩
```

**Description**

A cast sets the resolved type of the immediately following identifier or call; it does not propagate
through a chain. It has three uses.

**Ancestor-qualified dispatch.** Method dispatch is dynamic: `myDog.speak()` runs the most-derived
override. Casting the receiver to a strict ancestor of its static type selects that ancestor's version:
`(Animal)myDog.speak()`. On `self` inside an override this calls the overridden method without
recursion. An identity cast, a downcast or a cast to an unrelated type keeps dynamic dispatch. The
receiver must be a class or object with real methods; an ancestor cast on an `emitter` method is a
compile-time error. The cast qualifies method dispatch only; it does not apply to member access or
`operator =` (`(Base)obj.field = x`). A base-typed *variable* stays dynamic: `Animal a = myDog;
a.speak();` runs `Dog`'s override.

**Explicit conversion.** A conversion operator declared `explicit` fires only under a cast:
`string s = (string)myValue;` (§9.4). A cast also forces resolution through a specific type when the
inferred type would resolve differently.

**Class vs. instance.** The target may be a class or a named object. Casting to a class exposes the
class's members; casting to an instance also exposes members declared on that object alone. This is
the way to reach members through a dynamically-typed value such as `.parent`, which is statically
`object`. The cast is an unchecked downcast: the member name is checked against the target at compile
time, but the runtime object is assumed to be of that type. Reaching a member that `object` does not
have through an `object`-typed value without a cast is a compile-time error. `(var)` gives an
untyped read with no member check.

**Example**

```bgl
class Room : object { int lit; }
Room library { int shelves; }
object obj;                                // statically object
int a = ((library)obj.parent).shelves;     // instance member
int b = ((Room)obj.parent).lit;            // class member
```

**See also** §2.12 (conversion operators), §8.6 (inheritance and overriding).

## 4.12 Address-of `&`

**Syntax**

```syntax
&⟨expr⟩
```

**Description**

Prefix `&⟨expr⟩` yields the raw machine address of its operand as an `int`; it is exactly
`(int)⟨expr⟩`. On an array, buffer or object it gives the base address; on a scalar it yields the
value itself. It does not compute the storage address of a variable or property; Beguile has no
pointer types. A `&` with a left operand is the bitwise and (§4.8).

**Example**

```bgl
extern void FillBuffer(int address, int length);   // an I6 routine
array<char> buf[32];
FillBuffer(&buf, 32);                              // the base address of buf
int n = 7;
int m = &n;                                        // → 7: a scalar yields its value
```

**See also** §15.4.1 (`extern` functions).

## 4.13 `new`

**Syntax**

```syntax
new ⟨type⟩(⟨args⟩)
```

**Description**

Allocates an instance of a pooled class and yields a reference to it, or `nothing` if the pool is
exhausted; the result must be checked before use. The arguments are passed to the class's `create()`
method; if the class declares no `create`, `new ⟨type⟩()` is the only valid form. `new` on a class
that is not pooled is a compile-time error. Pooled classes, pool size and the `create`/`destroy`
lifecycle are specified in §8.2.6; the `delete` statement in §5.15.

**Example**

```bgl
class Marble[10] : object { }
Marble m = new Marble();
if (m == nothing) print("The pool is exhausted.");
```

**Notes**

`create()` argument limit: see §8.2.6.

## 4.14 Lambdas

**Syntax**

```syntax
(⟨type⟩ ⟨param⟩, …) => { ⟨body⟩ }
(⟨type⟩ ⟨param⟩, …) => ⟨expr⟩
() => { ⟨body⟩ }
```

**Description**

A lambda is an anonymous function literal. It may be assigned to a `func<>` variable or member,
passed as an argument, or stored in a collection (`func<>` is specified in §2.9). The return type is
inferred: from the expression of a `return expr;` in the body, or `void` if the body has no `return`.
A lambda in argument position is passed by reference, like a named `func<>` value.

The expression-bodied form `(⟨params⟩) => ⟨expr⟩` is equivalent to
`(⟨params⟩) => { return ⟨expr⟩; }`; the return type is inferred from the expression. In both forms
every parameter is typed; `x => …` is not a lambda.

**Capture.** A lambda body may use locals and parameters of the enclosing function, wherever in that
function the lambda appears, including inside loop and `if` bodies. Each captured variable's value is
copied when the lambda is created. For a lambda passed directly as an argument, changes the body makes
to a captured variable are visible in the enclosing scope after the call returns. Captures are
intended for immediate callbacks: for a lambda stored and invoked later, the behavior is undefined. A
lambda that captures nothing costs nothing extra.

**Constraints.** A lambda literal may not be invoked immediately (`((int n) => { … })(42)`); assign it
or pass it first.

**Example**

```bgl
array<int> scores = {3, 1, 2};

void applyToAll(array<int> arr, func<void, int> fn) {
    for (int item in arr) fn(item);
}
void test(int multiplier) {
    applyToAll(scores, (int x) => { print(x * multiplier); });   // captures multiplier
    scores.sort((int a, int b) => b - a);                        // expression-bodied: returns b - a
}
```

## 4.15 Operator References

**Syntax**

```syntax
⟨type⟩::operator ⟨op⟩
⟨type⟩::operator ⟨op⟩(⟨type⟩)
```

**Description**

Names an operator that `⟨type⟩` declares, yielding its address for use wherever a `func<>` is
expected. Only a `static` operator is referenceable; referencing an instance operator is a
compile-time error. When a type declares several static overloads of the operator, the parenthesized
operand type selects one. The rules for declaring static operators are in §9.6.

**Example**

```bgl
#include <string>
class Money {
    int cents = 0;
    static bool operator == (Money a, int b) { return a.cents == b; }
}
array<string> names = {"cherry", "apple"};

names.sort(string::operator <=>);
func<bool, Money, int> byCents = Money::operator ==(int);
```

**See also** §7.3.1 (`$opref`, the same lookup inside an emitter body).
