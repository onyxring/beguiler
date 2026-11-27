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

Every expression has a **resolved type**.  `&&` and `||` evaluate the right operand only when the left operand doesn't decide the result; `??` evaluates its right operand only when the left is null (§4.10); and a ternary evaluates only the selected branch (§4.9).

Apart from those, the order in which an expression's operands are evaluated is unspecified, as in C++:  in `f() + g()`, either call can run first, and so can the arguments of one call.

## 4.2 Operands

An expression is one or more operands joined by operators.  An operand is one of the following:

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

## 4.3 Operator Precedence

Higher levels bind more tightly.

| Prec | Operators | Kind | Assoc | Meaning |
|:---:|---|---|---|---|
| 14 | `.` `?.` `[]` `()` `v?` `++` `--` | postfix | left | Member access, optional access, subscript, call, postfix query, postfix increment/decrement |
| 13 | `!` `-` `&` `(Type)` `++` `--` | prefix | right | Logical not, negation, address-of, cast, prefix increment/decrement |
| 11 | `*` `/` `%` | infix | left | Multiplicative |
| 10 | `+` `-` | infix | left | Additive |
| 9 | `<<` `>>` `<=>` | infix | left | Shift, three-way comparison |
| 8 | `<` `<=` `>` `>=` | infix | left | Relational |
| 7 | `==` `!=` `?=` `=~` `:==` | infix | left | Equality, identity (§4.6) |
| 6 | `&` | infix | left | Bitwise and |
| 5 | `^` | infix | left | Bitwise exclusive or |
| 4 | `\|` | infix | left | Bitwise or |
| 3 | `&&` | infix | left | Logical and |
| 2 | `\|\|` | infix | left | Logical or |
| 1 | `? :` | ternary | right | Conditional (§4.9) |
| 1 | `??` | infix | right | Null coalescing (§4.10) |
| 0 | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` `:=` | infix | right | Assignment (§5.5), compound assignment (§5.6), reference binding (§3.7) |

There is no unary bitwise-not operator, and Inform 6's `~` and `~~` are not Beguile operators; write `!` for logical not.

## 4.4 Binary Operator Resolution

A binary operator is resolved against the resolved type of its left operand, in these steps:

1. Look for `operator op` (emitter or method) on the left type whose parameter accepts the right type:  its exact type, a literal's base type, a type it converts to, or a class it derives from.
2. Otherwise, if the left type has a conversion operator (`operator()`) to the right type, fall back to the built-in operator.
3. Otherwise, if the right type has a conversion operator to a type for which the left type does declare `operator op`, apply that conversion and use the operator.
4. Otherwise, if the left type is known, it is a compile-time error.  A `var` operand falls back to the built-in operator.

Assignment and arithmetic operators keep the left operand's type, except that a `float` operand on either side of an arithmetic operator makes the result `float` (§2.3).  The overloadable operators are listed in §9.1.

> ***Note:** When a type declares several overloads of one operator, the right-hand operand selects between them in four passes:  the operand's **exact** type; the **base type of a literal** (`intLiteral` gives `int`); a type the base is **convertible** to, or an **ancestor** of the operand's class; and finally a parameter declared `var`.  Each pass looks at the type's own operators before the inherited ones.  An operand whose type is itself `var`, or whose type is unknown, still matches in the first pass.*

**Example**

```bgl
class Money {
    int cents = 0;
    emitter bool operator == (Money v){ $self.cents == $v.cents }
    int operator () { return cents; }          // implicit conversion to int
}
Money a { } Money b { } int five = 5;
bool same = a == b;       // 1: Money declares operator == (Money)
int  sum  = a + five;     // 2: no operator + on Money; a converts to int, built-in + applies
```

## 4.5 Arithmetic Operators

The built-in operators need no `#include`.  They are ordinary overloadable operators (§9.1), which a type can declare for itself.  The compound-assignment forms are in §5.6, and the postfix query, optional chaining, and null coalescing are in §4.10.

| Operators | Built in for | Result | Example |
|---|---|---|---|
| `+` `-` `*` `/` `%` | `int`, `uint` (§21.6.1), `float` (§2.3); `+` and `-` also for `char` with a `char` or `int` right operand (§21.7) | The left operand's type; `float` when either operand is a `float` (§2.3) | `7 / 2` gives `3`; `7 % 2` gives `1`; `'a' + 1` gives `'b'` |

**`int` arithmetic.**  Arithmetic which leaves the range of an `int` (§2.2) wraps (`32767 + 1` gives `-32768` on the Z-machine), on literals as on variables.  `/` truncates toward zero (`-7 / 2` gives `-3`) and `%` takes the sign of the left operand (`-7 % 2` gives `-1`, `7 % -2` gives `1`).  Dividing by zero, with `/` or `%`, is a run-time error of the virtual machine.

## 4.6 Comparison Operators

The comparison operators are built in for these types:

| Operators | Built in for | Result | Example |
|---|---|---|---|
| `==` `!=` `<` `>` `<=` `>=` | `int`, `uint`, `float`, `char`; `==` and `!=` also for `bool`, `string` (identity of the text), and `object` | `eBool` | `score >= 50`; `noun == lamp` |
| `?=` | None; a type gives it its meaning (§9.5) | `eBool` | - |
| `=~` | `char`:  case-insensitive equality (§21.7); `string` with `<string>` (§22.3) | `eBool` | `'A' =~ 'a'` gives `true` |
| `<=>` | None; `<string>` declares it for `string` (§22.3), and any type can declare it (§9.6) | `int`:  negative, `0`, or positive | `(a <=> b) < 0` |

**Identity.**  `a :== b` is true when `a` and `b` are the same instance.  It compares references and never calls an `operator ==`, just as `:=` bypasses `operator =` (§3.7).  Both operands are instances:  objects, instances of any class (structs and `stringObj` included), arrays, or `null`.  On a primitive it is a compile-time error ("use '=='").  `:==` can't be overloaded, and neither can `:=`.

```bgl
#include <string>

// … in a routine:
stringObj s = "hi";
stringObj t = "hi";
ref stringObj u := s;
print(s == t);       // true: the same text
print(s :== t);      // false: two strings
print(u :== s);      // true: u refers to s
```

## 4.7 Logical Operators

`&&`, `||`, and prefix `!` are built in for these types:

| Operators | Built in for | Result | Example |
|---|---|---|---|
| `&&` `\|\|` `!` | `bool` and `eBool` (§2.2); `!` on a type that declares `operator !()` uses that emitter (§9.5) | `eBool` | `a > 0 && b < 10` |

An operand of `&&` or `||` which is a reference (an object, a class instance, a `ref`, a string, or a function value) is its null test, as it is when it stands alone as a condition:  `if (ready && door.lock)` is `if (ready && door.lock?)`.

## 4.8 Bitwise and Shift Operators

`& | ^ << >>` are built in for these types:

| Operators | Built in for | Result | Example |
|---|---|---|---|
| `&` `\|` `^` `<<` `>>` | `int`, `uint`; `&`, `\|`, and `^` also combine `bnum` values which share a base (§2.7.2) | The left operand's type; for two `bnum` values, their shared ancestor's type (§2.7.2) | `flags & lit`; `1 << 4` gives `16` |

`>>` on an `int` is arithmetic, keeping the sign (`-256 >> 4` gives `-16`); on a `uint` it fills with zeros (§21.6.1).  The compound forms `&= |= ^= <<= >>=` are in §5.6.

## 4.9 Ternary Operator

**Syntax**

```syntax
CONDITION ? TRUE-EXPR : FALSE-EXPR
```

The ternary operator selects one of two values, and only the selected branch is evaluated.

**Type of the result.**  When the branches have the same type, that is the type.  When one branch's type is assignable to the other's (e.g., a derived class and its base), the result takes the more general of the two, regardless of which side it is written on.  A literal counts as the type it denotes, and a branch typed `var` imposes nothing.

When the branches have unrelated types, the result is the **union** of the two (§2.8).  A union-typed target accepts it (§2.8.1); a target of one branch's type rejects it.

**Example**

<!-- doctest: skip -->
```bgl
print(x > 0 ? "positive" : "non-positive");
int result = (cond ? a : b) + extra;

string | func<void> action = useText ? "nothing happens" : doSomething;  // union of both branches
int wrong = cond ? 1 : "text";   // error: 'int|string' is not assignable to 'int'
```

## 4.10 Optional Chaining, Null Coalescing and Postfix Query

**Syntax**

```syntax
EXPR?.MEMBER
EXPR?.METHOD(ARGS)
EXPR ?? FALLBACK
EXPR?
```

All three operators are type-driven:  the operand's type must declare an `operator ?()` emitter (§9.5), whose result is the null test.  For `object` the test is "not `null`", and for `string` it is "non-zero handle".

**`?.`** accesses a member or calls a method only if the left operand is non-null.  In an expression, each `?.` step tests the value so far, and if it is null the whole chain yields `null`.  As a statement (`x?.remove();`), the target can be any expression, such as a call or an element (`find()?.remove();`, `a[i]?.remove();`).  Note that a plain `.` after a `?.` adds no guard:  if the preceding `?.` yielded null, the member access runs on `null`, and the result is undefined.

**`??`** yields the left operand if it is non-null, otherwise the right operand, which is evaluated only in that case.  The left operand's type must have a null test:  a `?.` chain which ends in an `int` or another primitive can't take `??`.

**Postfix `?`** yields the null test as an `eBool`; `noun?` is `noun != null` for objects.  A function value (`func<…>`) is tested the same way.  Where a `?` could begin a ternary, as the right operand of a binary operator, it is the ternary; the postfix query applies only when the `?` ends the expression.

**Example**

<!-- doctest: skip -->
```bgl
object dest = actor?.destination ?? location;
string desc = noun?.parent.description;   // parent guarded, .description not
if (noun?) print("something is here");
```

## 4.11 Casts

**Syntax**

```syntax
(TYPE)EXPR
(INSTANCE-NAME)EXPR
```

A cast applies to the whole operand which follows it, member accesses and calls included, as in C++ and C#:  `(Dog)a.barks` casts the value of `a.barks`.  To treat the receiver as another type, parenthesize the cast and the receiver together:  `((Dog)a).barks` reads `Dog`'s member.  The result of a cast is not assignable, so `(Dog)a.barks = 9` is a compile-time error; however, `(T)name = v;` assigns through `T`'s `operator =`.

> ***Note:** `(float)(314 / 100)` divides the integers first and then converts the result, giving `3.0`.*

A cast has four uses.

**Ancestor-qualified dispatch.**  Casting the receiver to a strict ancestor of its static type selects that ancestor's version:  `((Animal)myDog).speak()`.  On `self` inside an override, this calls the overridden method without recursion.  An identity cast, a downcast, or a cast to an unrelated type keeps dynamic dispatch.  The receiver must be a class or object with real methods; an ancestor cast on an `emitter` method is a compile-time error.  On a member variable the receiver cast resolves the member against the cast type, which reaches a member the subtype hides (§8.7.4).  A base-typed *variable* stays dynamic:  `Animal a = myDog; a.speak();` will run `Dog`'s override.

> ***Note:** In C#, `((Animal)myDog).Speak()` still runs `Dog`'s override; the base version is reached only through `base.Speak()` inside the class.  C++ writes `myDog.Animal::speak()`.*

**Explicit conversion.**  A conversion operator declared `explicit` fires only under a cast:  `string s = (string)myValue;` (§9.4).  A cast also forces resolution through a specific type when the inferred type would resolve differently.

**Reinterpretation.**  Between a reference type and a primitive (`object` and `dictionaryWord`, `int` and `object`), or between two primitives without a conversion operator, a cast changes only the type:  the value is the same word, read as the target type.  It is unchecked, like `(var)`.

**Class vs. instance.**  The target can be a class or a named object.  Casting to a class exposes the class's members; casting to an instance also exposes members declared on that object alone.  The cast is an unchecked downcast:  the member name is checked against the target at compile time; however, the runtime object is assumed to be of that type.  Reaching a member which `object` doesn't have through an `object`-typed value without a cast is a compile-time error.  `(var)` gives an untyped read with no member check.

**Example**

<!-- doctest: compile -->
```bgl
class Room : object { int lit; }
Room library { int shelves; }
object obj;                                // statically object
int a = ((library)obj.parent).shelves;     // instance member
int b = ((Room)obj.parent).lit;            // class member
```

**See also** §2.10.2 (conversion operators), §8.6 (inheritance and overriding).

## 4.12 Address-of `&`

**Syntax**

```syntax
&EXPR
```

Prefix `&EXPR` yields the raw machine address of its operand as an `int`; it is exactly `(int)EXPR`.  On an array, buffer, or object it gives the base address; on a scalar it yields the value itself.  It never gives the storage address of a variable or property.  A `&` with a left operand is the bitwise and (§4.8).

**Example**

<!-- doctest: compile -->
```bgl
extern void FillBuffer(int address, int length);   // an I6 routine

// … in a routine:
array<char> buf[32];
FillBuffer(&buf, 32);                              // the base address of buf
int n = 7;
int m = &n;                                        // -> 7: a scalar yields its value
```

**See also** §16.4.1 (`extern` functions).

## 4.13 `new`

**Syntax**

```syntax
new TYPE(ARGS)
```

`new` allocates an instance of a pooled class and yields a reference to it, or `null` if the pool is exhausted; the result must be checked before use.  The arguments are passed to the class's `create()` method; if the class declares no `create`, `new TYPE()` is the only valid form.  `new` on a class which is not pooled is a compile-time error.  Pooled classes, pool size, and the `create`/`destroy` lifecycle are covered in §8.2.6; the `delete` statement is in §5.15.

**Example**

```bgl
class Marble[10] : object { }

// … in a routine:
Marble m = new Marble();
if (m == null) print("The pool is exhausted.");
```

> ***Note:** For the `create()` argument limit, see §8.2.6.*

## 4.14 Lambdas

**Syntax**

```syntax
(TYPE PARAM, …) => { BODY }
(TYPE PARAM, …) => EXPR
() => { BODY }
=> { BODY }
=> EXPR
```

A lambda is an anonymous function literal, whose type is a `func<>` (§2.9).  The return type is inferred:  from the expression of a `return expr;` in the body, or `void` if the body has no `return`.  A lambda in argument position is passed by reference, like a named `func<>` value.

The expression-bodied form `(PARAMS) => EXPR` is equivalent to `(PARAMS) => { return EXPR; }`.  In both forms every parameter is typed; `x => …` is not a lambda.  A lambda with no parameters can omit the empty parameter list:  `=> BODY` is equivalent to `() => BODY`.

An expression body with no value, such as a call to a `void` function, is the lambda's body rather than its return value.  An assignment has no value either, so a lambda whose expression body is an assignment (`=> hits += 10`) is `void` (`func<void>`), as in C#.

**Capture.**  A lambda body can use locals and parameters of the enclosing function, wherever in that function the lambda appears.  Each captured variable's value is copied when the lambda is created.  For a lambda passed directly as an argument, changes the body makes to a captured variable will be visible in the enclosing scope after the call returns, and in the `catch` when the call throws (§5.16).  Inside a method, a lambda which reads a member captures `self`.

Captures are for callbacks which run before the enclosing function moves on.  A lambda which captures a variable can't outlive it, so it is a compile-time error to return one, to store one in a global or a member, or to pass one to a `literal` parameter (§6.3).  Storing one in a local is allowed, except inside a loop, where storing a capturing lambda (assigning it, writing it to an element, or passing it to an array method which keeps its argument, such as `push`) is a compile-time error.  A lambda used during the statement, such as a predicate passed to `filter`, can capture loop variables freely.  A capture of a `const` local initialized from a compile-time value is exempt from all of these.

**Constraints.**  A lambda literal can't be invoked immediately (`((int n) => { … })(42)`); assign it or pass it first.

**Example**

```bgl
#include <array>
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
TYPE::operator OP
TYPE::operator OP(TYPE)
```

An operator reference names an operator which `TYPE` declares, yielding its address for use wherever a `func<>` is expected.  Only a `static` operator is referenceable; referencing an instance operator is a compile-time error.  When a type declares several static overloads of the operator, the parenthesized operand type selects one.  The rules for declaring static operators are in §9.6.

**Example**

```bgl
#include <array>
#include <string>
class Money {
    int cents = 0;
    static bool operator == (Money a, int b) { return a.cents == b; }
}

// … in a routine:
array<string> names = {"cherry", "apple"};

names.sort(string::operator <=>);
func<bool, Money, int> byCents = Money::operator ==(int);
```

**See also** §7.3.1 (`$opref`, the same lookup inside an emitter body).
