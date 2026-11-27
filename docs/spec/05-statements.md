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

The body of a function, method, or emitter is a sequence of statements executed in order.  A statement ends with a semicolon unless it ends with a closing brace.  A stray `;` is an empty statement and is discarded, at every statement boundary and at file scope.

## 5.2 Block Statement

**Syntax**

```syntax
{ STATEMENT … }
```

A block groups statements and introduces a scope:  a variable declared in the block is visible from its declaration to the block's closing brace (§3.6).

## 5.3 Expression Statement

**Syntax**

```syntax
EXPRESSION ;
```

An expression followed by a semicolon is a statement.  The expressions which can stand alone are function and method calls (chained and optional-chained calls included), increment and decrement (§5.7), and assignment (§5.5, §5.6).

## 5.4 Declaration Statement

**Syntax**

```syntax
TYPE NAME [ = EXPRESSION ] ;
```

A local variable declaration is a statement.  The forms, including `auto`, are covered in §3.6; `ref` and `:=` are in §3.7.

## 5.5 Assignment

**Syntax**

```syntax
LVALUE = EXPRESSION ;
```

The left-hand side is a declared variable or a dotted member path.  The assignment is allowed when the right-hand type is compatible with the left-hand type (§2.10.1).  Reference binding (`:=`) is covered in §3.7.

An assignment is a statement and has no value:  chaining one, `a = b = c;`, or using one as a condition, `if(a = 3)`, is a compile-time error.

**Example**

<!-- doctest: skip -->
```bgl
score = score + 10;
lamp.parent = library;
```

## 5.6 Compound Assignment

**Syntax**

```syntax
LVALUE OP= EXPRESSION ;
```

`OP` is one of `+ - * / % & | ^ << >>`.

A compound assignment modifies the variable in place.  When the left type declares the compound operator, that operator runs.  When it declares none, `n op= v` is `n = n op (v)` (§9.7), and it is a compile-time error only when that assignment is.  All of them are defined for `int` (§2.2) and `uint` (§21.6.1).

**Example**

```bgl
int n = 5;
n += 2;      // -> 7
n <<= 1;     // -> 14
```

## 5.7 Increment and Decrement

**Syntax**

```syntax
VARIABLE++ ;   VARIABLE-- ;   ++VARIABLE ;   --VARIABLE ;
```

An increment or decrement uses the type's `operator ++` / `operator --`.  A prefix form uses `operator prefix++` / `operator prefix--` if the type declares it; otherwise, it falls back to the postfix operator.  A type which declares neither takes `n = n + 1` (or `n = n - 1`) instead (§9.7); if that isn't available either, it is a compile-time error.

**Example**

```bgl
int n = 5;
n++;         // -> 6
--n;         // -> 5
```

## 5.8 `if` / `else`

**Syntax**

```syntax
if (CONDITION) STATEMENT
if (CONDITION) { … } else { … }
if (CONDITION) { … } else if (CONDITION) { … } else { … }
```

The body can be a single statement or a block, and `else` is optional.

`else if (CONDITION) { … }` is equivalent to `else { if (CONDITION) { … } }`.

**Example**

```bgl
if (score < 0)       print("Invalid score.");
else if (score < 50) print("Keep going.");
else                 print("Well done!");
```

## 5.9 `for`

**Syntax**

```syntax
for (INITIALIZER; CONDITION; INCREMENT) STATEMENT
```

All three parts are required.  The initializer can declare a new variable or assign to an existing one; however, it can't redeclare a variable which is still in scope (§3.10).  A later loop can declare the same name again.  A ternary can be used in the condition and increment parts (§4.9).

**Example**

```bgl
for (int i = 0; i < 3; i++) print(i);    // -> 012
```

### 5.9.1 `for-in`

**Syntax**

```syntax
for (TYPE V in SOURCE) STATEMENT
for (V in SOURCE) STATEMENT
for (TYPE V in FIRST to LAST) STATEMENT
```

In the second form, `V` is a variable declared earlier.

A `for-in` loop iterates over every element of `SOURCE`, which is one of the following:

| Source | Element type |
|---|---|
| A declared array variable | The array's element type |
| An inline list `{a, b, …}` | The type of the first element; each element is checked against `v` |
| A call expression returning an array | `var`:  any declared type for `v` is accepted |
| A range `first to last` | Integer; inclusive at both ends; the bounds are any expressions |
| An object's `children` | The class which declares `children`; the body can move the current child (§11.5.2) |

A loop variable declared in the head belongs to the loop:  after the loop, the name means what it meant before, and a later loop can declare it again with another type.  Its type must be compatible with the element type or it is a compile-time error; `var` matches any element type, and `auto` infers the element type from the source, including through `operator auto()` (§9.5.1).  Any other source expression is a compile-time error.

A loop variable of a struct owns its instance, as any local of the class does (§8.2.7):  each element is copied into it, so a write to the variable won't change the array.

**Example**

```bgl
array<int> primes = {2, 3, 5, 7};
int start = 10;
int count = 3;

for (auto p in primes) print(p);                        // -> 2357
for (object o in bgl.world.getAll()) print(o);
for (int i in start to start + count - 1) print(i);     // -> 101112
```

**See also** §21.9 (`bgl.world`).

## 5.10 `while`

**Syntax**

```syntax
while (CONDITION) STATEMENT
```

The condition is evaluated before each iteration.

**Example**

```bgl
int n = 3;
while (n > 0) { print(n); n--; }    // -> 321
```

## 5.11 `do` / `while` and `do` / `until`

**Syntax**

```syntax
do { … } while (CONDITION) ;
do { … } until (CONDITION) ;
```

The body runs at least once, and the condition is evaluated after each iteration.  `do`/`while` repeats while the condition is true; `do`/`until` repeats until it becomes true.

**Example**

```bgl
int n = 0;
do { n++; } while (n < 3);    // n -> 3
do { n--; } until (n == 0);   // n -> 0
```

## 5.12 `switch`

**Syntax**

```syntax
switch (EXPRESSION) {
    case VALUE [, VALUE …] :   STATEMENTS
    case LOW to HIGH :           STATEMENTS
    case OP VALUE :              STATEMENTS
    default :                        STATEMENTS
}
```

`OP` is one of `>`, `>=`, `<`, `<=`.

A case lists one or more values separated by commas.  Cases don't fall through, so an empty case written straight before another (`case Take: case Drop: …`) does nothing and draws a warning.

A `break` in a case body leaves the `switch`, whether it is at the top of the body or nested in a block within it; however, a `break` inside a loop within a case body belongs to that loop.

Case values are type-checked against the switch expression (§2.10.1):  integer literals match an `int`, and an enum value must be of the switch expression's enum type.  When the switch expression is a `verb`, case values are verb names (§13.2).  A case value is a constant:  a literal, an enum value, a `#define`, a `const`, an object, or a verb.  A variable (a local, a global variable, a member) is a compile-time error.

**Ranges.**  `low to high` matches the inclusive range and can be mixed with single values in one case:  `case 1, 3, 5 to 10:`.

**Comparison guards.**  `case >= 50:` tests the switch value against a comparison.  When any case in a switch is a guard, the switch expression is evaluated once and the cases are tested in order.

**Type-driven comparison.**  When the switch expression's type declares `operator switch()`, that operator performs each case comparison; with `#include <string>`, `string` declares one so that `case "north":` compares content.  Overloads for different case-value types can coexist.  A range or a guard case in such a switch is a compile-time error, and a string literal case on a type which declares no `operator switch()` is one as well.  The declaration form is in §9.5.

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

`break` exits the innermost enclosing `for`, `while`, or `do` loop, or the innermost enclosing `switch` when that is nearer (§5.12).  A `break` in neither is reported by the Inform 6 stage.  `continue` skips the rest of the current iteration of the innermost `for`, `while`, or `do` and re-evaluates the loop condition; a `switch` inside the loop doesn't intercept it.  `continue` outside a loop is a compile-time error.

**Example**

```bgl
for (int i = 0; i < 10; i++) {
    if (i == 3) break;            // leaves the loop
    if (i % 2 == 0) continue;     // skips the even values
    print(i);
}                                 // -> 1
```

## 5.14 `return`, `rtrue` and `rfalse`

**Syntax**

```syntax
return ;
return EXPRESSION ;
rtrue ;   rfalse ;
rtrue(EXPRESSION) ;   rfalse(EXPRESSION) ;
```

`return` exits the function, with a value if given, which is type-checked against the declared return type.  In a `void` function `return expr;` is a compile-time error unless `expr` is itself of type `void`, in which case it is equivalent to `expr; return;`.  In loose mode (`#bgl` islands and precompiler mode, §16.3.3) an expression of type `var` is also accepted there.

In a function whose return type is `bool`, `rtrue;` is equivalent to `return true;` and `rfalse;` to `return false;`.

`rtrue(expr)` and `rfalse(expr)` print `expr` as `print()` would (§21.4), and then return.

**Return-path analysis.**  A non-`void` function must return on every path; a path which can reach the end of the body without returning is a compile-time error.  A path is satisfied by an unconditional `return` or `throw` (§5.16) at the top level of the body; by an `if`/`else` whose two branches are both satisfied; by a `switch` with a `default` whose every case body is satisfied (a `switch` without `default` is not); by a `try`/`catch` whose `try` body and `catch` body are both satisfied; or by an `#i6` island which returns (§16.2).  A loop body alone doesn't satisfy it.

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
delete IDENTIFIER ;
```

`delete` returns a pooled-class instance to its pool, calling the class's `destroy()` method first if one is declared.  The instance leaves the object tree, and the slot is free for the next `new`, which will start it again from its members' declared values.  `delete` on a variable whose type is not a pooled class is a compile-time error.  Pooled classes are covered in §8.2.6; allocation with `new` is in §4.13.

**Example**

<!-- doctest: skip -->
```bgl
Marble m = new Marble();
if (m != null) delete m;       // destroy() runs; the slot returns to the pool
```

## 5.16 `try` / `catch` / `throw`

**Syntax**

```syntax
try { … } catch (TYPE NAME) { … }
throw EXPRESSION ;
```

If `throw` executes anywhere during the `try` block, including inside called functions at any depth, execution unwinds to the nearest enclosing `catch` and the thrown value is assigned to the catch variable.  The thrown value is one word:  an integer, an object reference, or any other word-sized value.  `try` blocks nest; a `throw` inside a `catch` re-throws to the next enclosing `catch`.  If `throw` executes with no active `try` on the call stack, the program will print an error and halt.

Before it unwinds, a `throw` releases the owning locals (§8.5) of the blocks it leaves in its own routine, as far as the `try` there which catches it, or all of the routine's when nothing in the routine catches it.  A routine which the throw passes through on its way to a `catch` in a caller doesn't release its locals.  The routine which catches it releases the owning locals of its own `try` block before the `catch` body runs.

**Example**

```bgl
void deep() { throw 7; }

// … in a routine:
try { deep(); } catch (int e) { print(e); }    // -> 7
```
