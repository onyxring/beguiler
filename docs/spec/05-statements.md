# 5 Statements and Control Flow

<!-- toc -->
- [5.1 Statements](#51-statements)
- [5.2 Block Statement](#52-block-statement)
- [5.3 Expression Statement](#53-expression-statement)
- [5.4 Declaration Statement](#54-declaration-statement)
- [5.5 Assignment](#55-assignment)
- [5.6 Compound Assignment](#56-compound-assignment)
- [5.7 Increment and Decrement](#57-increment-and-decrement)
- [5.8 `if` / `else`](#58-if-else)
- [5.9 `for`](#59-for)
  - [5.9.1 `for-in`](#591-for-in)
- [5.10 `while`](#510-while)
- [5.11 `do` / `while` and `do` / `until`](#511-do-while-and-do-until)
- [5.12 `switch`](#512-switch)
- [5.13 `break` and `continue`](#513-break-and-continue)
- [5.14 `return`, `rtrue` and `rfalse`](#514-return-rtrue-and-rfalse)
- [5.15 `delete`](#515-delete)
- [5.16 `try` / `catch` / `throw`](#516-try-catch-throw)
<!-- /toc -->


## 5.1 Statements

The body of a function, method or emitter is a sequence of statements executed in order. A statement
ends with a semicolon unless it ends with a closing brace. A stray `;` is an empty statement and is
discarded, so a trailing semicolon after a directive, a declaration, a class body or a block is
accepted at every statement boundary and at file scope.

## 5.2 Block Statement

**Syntax**

```syntax
{ ⟨statement⟩ … }
```

**Description**

A block groups statements and introduces a scope: a variable declared in the block is visible from
its declaration to the block's closing brace (§3.6). A block may stand wherever a statement may.

## 5.3 Expression Statement

**Syntax**

```syntax
⟨expression⟩ ;
```

**Description**

An expression followed by a semicolon is a statement. The expressions that may stand alone are
function and method calls, including chained calls (`str.trim().print();`), optional-chained calls
(`x?.remove();`), increment and decrement (§5.7), and assignment (§5.5, §5.6). Arguments and arity
are checked at compile time, and a method call is resolved against the declared type of its receiver;
each call in a chain is resolved against the return type of the previous call.

## 5.4 Declaration Statement

**Syntax**

```syntax
⟨type⟩ ⟨name⟩ [ = ⟨expression⟩ ] ;
```

**Description**

A local variable declaration is a statement. The forms, including `auto`, are specified in §3.6;
`ref` and `:=` in §3.7.

## 5.5 Assignment

**Syntax**

```syntax
⟨lvalue⟩ = ⟨expression⟩ ;
```

**Description**

The left-hand side is a declared variable or a dotted member path. The assignment is permitted when
the right-hand type is compatible with the left-hand type; the compatibility rules, tested in order,
are in §2.11. Reference binding (`:=`) is specified in §3.7.

**Example**

```bgl
score = score + 10;
lamp.parent = library;
```

## 5.6 Compound Assignment

**Syntax**

```syntax
⟨lvalue⟩ ⟨op⟩= ⟨expression⟩ ;
```

`⟨op⟩` is one of `+ - * / % & | ^ << >>`.

**Description**

`+= -= *= /= %= &= |= ^= <<= >>=` modify the variable in place. The left type must declare the
corresponding compound operator; if it does not and the type is known (not `var`), it is a
compile-time error. The BLR defines all of them for `int` (§2.2) and `uint` (§21.6.1).

`n += 2` is equivalent to `n = n + 2` when the type declares no `operator +=`; the
fallback is specified in §9.7.

**Example**

```bgl
int n = 5;
n += 2;      // → 7
n <<= 1;     // → 14
```

## 5.7 Increment and Decrement

**Syntax**

```syntax
⟨variable⟩++ ;   ⟨variable⟩-- ;   ++⟨variable⟩ ;   --⟨variable⟩ ;
```

**Description**

The variable's type must declare `operator ++` / `operator --`. A prefix form uses
`operator prefix++` / `operator prefix--` if the type declares it and otherwise falls back to the
postfix operator, so a type that declares only `operator ++` supports both `n++` and `++n`. If no
operator is found and the type is known, it is a compile-time error.

**Example**

```bgl
int n = 5;
n++;         // → 6
--n;         // → 5
```

## 5.8 `if` / `else`

**Syntax**

```syntax
if (⟨condition⟩) ⟨statement⟩
if (⟨condition⟩) { … } else { … }
if (⟨condition⟩) { … } else if (⟨condition⟩) { … } else { … }
```

**Description**

The body may be a single statement or a block. `else` is optional.

`else if (⟨condition⟩) { … }` is equivalent to `else { if (⟨condition⟩) { … } }`:
an `else if` chain is an `if` nested in the `else`.

**Example**

```bgl
if (score < 0)       print("Invalid score.");
else if (score < 50) print("Keep going.");
else                 print("Well done!");
```

## 5.9 `for`

**Syntax**

```syntax
for (⟨initializer⟩; ⟨condition⟩; ⟨increment⟩) ⟨statement⟩
```

**Description**

All three parts are required. The initializer may declare a new variable or assign to an existing
one; it may not redeclare a variable that already exists, including one declared in the initializer
of an earlier `for`. A ternary is permitted in the condition and increment parts (§4.9).

**Example**

```bgl
for (int i = 0; i < 3; i++) print(i);    // → 012
```

### 5.9.1 `for-in`

**Syntax**

```syntax
for (⟨type⟩ ⟨v⟩ in ⟨source⟩) ⟨statement⟩
for (⟨v⟩ in ⟨source⟩) ⟨statement⟩
for (⟨type⟩ ⟨v⟩ in ⟨first⟩ to ⟨last⟩) ⟨statement⟩
```

In the second form `⟨v⟩` is a variable declared earlier.

**Description**

Iterates over every element of `⟨source⟩`, which is one of:

| Source | Element type |
|---|---|
| A declared array variable | The array's element type |
| An inline list `{a, b, …}` | The type of the first element; each element is checked against `v` |
| A call expression returning an array | `var`: any declared type for `v` is accepted |
| A range `first to last` | Integer; inclusive at both ends; the bounds are any expressions |

The loop variable may be declared in the loop head or beforehand. Its type must be compatible with
the element type or it is a compile-time error; `var` matches any element type, and `auto` infers the
element type from the source, including through `operator auto()` (§7.8). Any other source expression
is a compile-time error. Loops nest; each keeps its own iteration state.

**Example**

```bgl
array<int> primes = {2, 3, 5, 7};
int start = 10;
int count = 3;

for (auto p in primes) print(p);                        // → 2357
for (object o in bgl.world.getAll()) print(o);
for (int i in start to start + count - 1) print(i);     // → 101112
```

**See also** §21.9 (`bgl.world`).

## 5.10 `while`

**Syntax**

```syntax
while (⟨condition⟩) ⟨statement⟩
```

**Description**

The condition is evaluated before each iteration; if it is false on entry the body does not run.

**Example**

```bgl
int n = 3;
while (n > 0) { print(n); n--; }    // → 321
```

## 5.11 `do` / `while` and `do` / `until`

**Syntax**

```syntax
do { … } while (⟨condition⟩) ;
do { … } until (⟨condition⟩) ;
```

**Description**

The body runs at least once and the condition is evaluated after each iteration. `do`/`while`
repeats while the condition is true; `do`/`until` repeats until it becomes true.

**Example**

```bgl
int n = 0;
do { n++; } while (n < 3);    // n → 3
do { n--; } until (n == 0);   // n → 0
```

## 5.12 `switch`

**Syntax**

```syntax
switch (⟨expression⟩) {
    case ⟨value⟩ [, ⟨value⟩ …] :   ⟨statements⟩
    case ⟨low⟩ to ⟨high⟩ :           ⟨statements⟩
    case ⟨op⟩ ⟨value⟩ :              ⟨statements⟩
    default :                        ⟨statements⟩
}
```

`⟨op⟩` is one of `>`, `>=`, `<`, `<=`.

**Description**

A case lists one or more values separated by commas. Cases do not fall through. A `break` in a case
body leaves the `switch` — at the top of the body or nested in a block within it, and whichever
lowering the switch takes. Because cases do not fall through, a `break` as the last statement of a
case body does nothing; a `break` before the end of the body ends the case there, leaving the rest of
it unreachable. A `break` inside a loop within a case body belongs to that loop. Case values
are type-checked against the switch expression (§2.11): integer literals match an `int`, and an enum
value must be of the switch expression's enum type. When the switch expression is a `verb`, case
values are verb names (§13.2).

**Ranges.** `low to high` matches the inclusive range and may be mixed with single values in one case:
`case 1, 3, 5 to 10:`.

**Comparison guards.** `case >= 50:` tests the switch value against a comparison. When any case in a
switch is a guard, the switch expression is evaluated once and the cases are tested in order; values
and ranges in the same switch become equality and range tests.

**Type-driven comparison.** When the switch expression's type declares `operator switch()`, that
operator performs each case comparison; `string` declares one so that `case "north":` compares
content. Overloads for different case-value types may coexist. The declaration form is in §9.5.

**Example**

```bgl
switch (score) {
    case 0:        print("Nothing yet.");
    case 1 to 49:  print("Keep going.");
    case >= 50:    print("Well done!");
    case < 0:      print("Invalid score.");
}
```

## 5.13 `break` and `continue`

**Syntax**

```syntax
break ;
continue ;
```

**Description**

`break` exits the innermost enclosing `for`, `while` or `do` loop, or the innermost enclosing
`switch` when that is nearer (§5.12). A `break` in neither is reported by the Inform 6 stage.
`continue` skips the rest of the current iteration of the innermost `for`, `while` or `do` and
re-evaluates the loop condition; a `switch` inside the loop does not intercept it. `continue` outside
a loop is a compile-time error.

**Example**

```bgl
for (int i = 0; i < 10; i++) {
    if (i == 3) break;            // leaves the loop
    if (i % 2 == 0) continue;     // skips the even values
    print(i);
}                                 // → 1
```

## 5.14 `return`, `rtrue` and `rfalse`

**Syntax**

```syntax
return ;
return ⟨expression⟩ ;
rtrue ;   rfalse ;
rtrue(⟨expression⟩) ;   rfalse(⟨expression⟩) ;
```

**Description**

`return` exits the function, with a value if given. The value may be any expression and is
type-checked against the declared return type. In a `void` function `return expr;` is a compile-time
error unless `expr` is itself of type `void`. In loose mode (`#bgl` islands and precompiler mode,
§15.3.3) an expression of type `var` is also accepted there.

In a `void` function, `return expr;` where `expr` is of type `void` is equivalent to
`expr; return;`.

In a function whose return type is `bool`, `rtrue;` is equivalent to `return true;`
and `rfalse;` to `return false;`.

`rtrue(expr)` and `rfalse(expr)` print `expr`, with full `print()` overload dispatch including `$"…"`
strings (§21.4), and then return.

**Return-path analysis.** A non-`void` function must return on every path; a path that can reach the
end of the body without returning is a compile-time error. A path is satisfied by an unconditional
`return` at the top level of the body; by an `if`/`else` whose two branches are both satisfied; by a
`switch` with a `default` whose every case body is satisfied (a `switch` without `default` is not); or
by an `#i6` island that returns (§15.2). A loop body alone does not satisfy it.

If a local's type declares a `deinit` emitter, it fires before the function returns (§8.5).

**Example**

```bgl
bool isOpen(object door) {
    if (door.has(open)) rtrue;
    rfalse("It is closed.");
}
```

**See also** §6.2 (return types).

## 5.15 `delete`

**Syntax**

```syntax
delete ⟨identifier⟩ ;
```

**Description**

Returns a pooled-class instance to its pool, calling the class's `destroy()` method first if one is
declared. `delete` on a variable whose type is not a pooled class is a compile-time error. Pooled
classes are specified in §8.2.6; allocation with `new` in §4.13.

**Example**

```bgl
Marble m = new Marble();
if (m != nothing) delete m;    // destroy() runs; the slot returns to the pool
```

## 5.16 `try` / `catch` / `throw`

**Syntax**

```syntax
try { … } catch (⟨type⟩ ⟨name⟩) { … }
throw ⟨expression⟩ ;
```

**Description**

If `throw` executes anywhere during the `try` block, including inside called functions at any depth,
execution unwinds to the nearest enclosing `catch` and the thrown value is assigned to the catch
variable. The thrown value is one word: an integer, an object reference or any other word-sized value.
`try` blocks nest; a `throw` inside a `catch` re-throws to the next enclosing `catch`. If `throw`
executes with no active `try` on the call stack, the program prints an error and halts.

**Example**

```bgl
void deep() { throw 7; }
try { deep(); } catch (int e) { print(e); }    // → 7
```

**Notes**

> **[Z-machine]** Requires version 5 or later.
