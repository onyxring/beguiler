# 6 Functions

<!-- toc -->
- [6.1 Function Declarations](#61-function-declarations)
- [6.2 Return Types](#62-return-types)
- [6.3 Parameters](#63-parameters)
- [6.4 Overload Resolution](#64-overload-resolution)
- [6.5 `replace` and `replaced()`](#65-replace-and-replaced)
- [6.6 `self`](#66-self)
- [6.7 `Main`](#67-main)
<!-- /toc -->


## 6.1 Function Declarations

**Syntax**

```syntax
⟨returnType⟩ ⟨name⟩(⟨type⟩ ⟨param⟩, …) { ⟨body⟩ }
```

**Description**

A function has a return type, a name, a parenthesized parameter list and a block body (§5.2). A
function declared at global scope is a **global function**; its name must be unique among non-emitter
functions, and among all globals (§3.8.3). Functions may also be declared as members of a class (§8.4)
or object (§11.9). A function declared `extern` has no body and is defined in Inform 6 (§15.4.1); a
function declared `emitter` has an I6 template body (§7.2).

**Example**

```bgl
object foyer { }

void DeathMessage() { print("You have lost"); }
bool Initialise() { location = foyer; rtrue; }
```

## 6.2 Return Types

**Description**

| Return type | Meaning |
|---|---|
| `void` | No value. `return;` is permitted; `return expr;` is an error unless `expr` is `void` (§5.14). |
| Any other type | Every path must end in `return expr;` with `expr` compatible with the type (§5.14). |
| `array<T>` | Returns a typed array. A returned *local* array is ephemeral (§12.6). |

`rtrue`, `rfalse` and the return-path rules are specified in §5.14.

**Example**

```bgl
array<int> primes = {2, 3, 5};

void greet()            { print("Hello."); }        // void: no value
int  twice(int n)       { return n * 2; }           // every path returns an int
array<int> table()      { return primes; }          // a typed array
int  sign(int n)        { if (n < 0) return -1; }   // compile-time error: no return when n >= 0
```

## 6.3 Parameters

**Syntax**

```syntax
(⟨type⟩ ⟨name⟩ [ = ⟨default⟩ ], …)
()
```

**Description**

Parameters are local to the body. A parameter with a default value is optional; required parameters
precede optional ones. A call must supply between the required count and the total count of
arguments, or it is a compile-time error.

**Named arguments.** An argument may be passed as `name: value`. Named arguments may appear in any
order and may be mixed with positional arguments: positional arguments fill the first unfilled
parameters in order, and named arguments fill their target parameter. It is an error to name a
parameter the function does not have, to supply a parameter both positionally and by name, or to omit
a required parameter.

Parameter names may be omitted in non-emitter declarations inside an `extern class` (§15.4.3).

**Example**

```bgl
void spawn(string name, int x, int y, bool hostile = false) { … }
spawn(x: 10, y: 20, name: "goblin", hostile: true);   // spawn("goblin", 10, 20, true)
```

## 6.4 Overload Resolution

**Description**

Functions and methods with the same name and different parameter-type signatures may coexist:
functions at global scope, emitter or not, and emitter and non-emitter methods on classes and
objects, including `operator()`, `operator[]` and `operator[]=`.

Two declarations of the same name must differ in their **parameters** — the return type is not part
of the signature, since a call is resolved before its result is used. Declaring the same name twice
with the same parameter types is a compile-time error.

Because Inform 6 has no overloading, each member of a set emits as its own routine or property under
a mangled name (`add_2_int_int`); a name with a single definition keeps the name the author wrote.
The mangled name is not part of the language: from raw I6, reach an overload through
`$i6Name(add(int,int))` (§7.3.3) rather than by spelling it out.

A call is resolved in two steps:

1. **Arity.** A candidate matches only if the argument count lies between its required and total
   parameter counts.
2. **Types.** Among candidates that match by arity, an exact type match for every argument wins over a
   match through an implicit conversion (`operator()`, §9.4), which wins over a match through `var`.

Type compatibility itself is specified in §2.10.1.

**A reference is not a call.** A bare function name used as a value (`func<int,int> f = twice;`)
must name exactly one routine. An overloaded name names the set, and nothing in a reference says
which member is meant, so it is a compile-time error; wrap the overload you want in a function of
its own and reference that.

## 6.5 `replace` and `replaced()`

**Syntax**

```syntax
replace ⟨returnType⟩ ⟨name⟩(⟨params⟩) { ⟨body⟩ }
replaced(⟨args⟩)
```

**Description**

`replace` replaces an already-declared global function. If no matching function exists, the compiler
issues a warning and the declaration is treated as a new function.

**Emitter functions.** The body is swapped. Matching is by name, return type and full parameter-type
signature, since emitters overload. `replaced()` is not available in an emitter.

**Non-emitter functions.** A replacement names the overload it replaces by its own parameter types;
with a name that has one definition, the parameters need not match it. Replacements chain. Inside the replacement body, `replaced(args)` calls the
immediately preceding definition; the arguments are type-checked against that predecessor's signature,
which may differ from the replacement's own. Successive `replace` declarations form a chain in which
each `replaced()` calls the version it directly replaced. A predecessor that no replacement calls is
not part of the program.

Replacing an `extern` routine defined by an I6 library works the same way in the source; the
I6-side rules are in §15.6.

**Example**

```bgl
int step(int n) { return n; }
replace int step(int n) { return replaced(n) + 100; }   // calls the original
replace int step(int n) { return replaced(n) + 200; }   // calls the first replacement
// step(5) → 305
```

**See also** §8.7.2 (`replace` for class and object members).

## 6.6 `self`

**Syntax**

```syntax
self
self.⟨member⟩
```

**Description**

Inside a class method or an object method, `self` is the receiver: the instance the method was called
on. A bare member name in a method body resolves as `self.member` (§3.8.2), so `self.` is optional and
has the same effect. `self` is not valid outside a method body.

`self` differs from Inform 6's `self`, which is the object that owns the running routine. In an
emitter body, `$self` is the receiver (§7.3).

**Example**

```bgl
class Counter {
    int count = 0;
    void increment() {
        count = count + 1;              // same as the next line
        self.count = self.count + 1;
    }
}
```

## 6.7 `Main`

**Syntax**

```syntax
void Main() { … }
```

**Description**

`Main` is the program's entry point. The compiler does not check for it: a program built without an
IF library binding defines `Main` itself, and a missing or duplicated `Main` is reported by the
Inform 6 stage. A library such as the Inform 6 Standard Library or PunyInform defines `Main` itself
and calls a library-specific entry point, such as `Initialise`, that the program supplies instead
(§23.3.1).

**Example**

```bgl
void Main() {
    print("Hello, world!^");
}
```

**See also** §23.3.1 (entry point under a binding).
