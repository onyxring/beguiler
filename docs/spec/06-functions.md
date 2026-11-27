# 6 Functions

<!-- toc -->
- [6.1 Function Declarations](#61-function-declarations)
- [6.2 Return Types](#62-return-types)
- [6.3 Parameters](#63-parameters)
- [6.4 Overload Resolution](#64-overload-resolution)
- [6.5 `replace` and `replaced()`](#65-replace-and-replaced)
- [6.6 `self`](#66-self)
- [6.7 `Main`](#67-main)
  - [6.7.1 `HandleGlkEvent`](#671-handleglkevent)
- [6.8 Distributors](#68-distributors)
<!-- /toc -->

## 6.1 Function Declarations

**Syntax**

```syntax
RETURN-TYPE NAME(TYPE PARAM, …) { BODY }
```

A function declared at global scope is a **global function**.  Functions of one name overload by parameter types (§6.4); however, the name can't be shared with any other kind of global (§3.1).

Functions can also be members of a class (§8.4) or an object (§11.9).  An `extern` function has no body, since it is defined in Inform 6 (§16.4.1); an `emitter` function has an I6 template for its body (§7.2).

**Example**

```bgl
object foyer { }

void DeathMessage() { print("You have lost"); }
bool Initialise() { location = foyer; rtrue; }
```

## 6.2 Return Types

The return type determines what a function's `return` statements look like:

| Return type | Meaning |
|---|---|
| `void` | No value.  `return;` is allowed; `return expr;` is an error unless `expr` is `void` (§5.14). |
| Any other type | Every path must end in `return expr;` with `expr` compatible with the type (§5.14). |
| `array<T>` | Returns a typed array.  A returned local array passes to the caller (§12.6). |

`rtrue`, `rfalse`, and the return-path rules are in §5.14.

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
( [ literal ] TYPE NAME [ = DEFAULT ], …)
( literal TYPE | TYPE … NAME, … )
()
```

Parameters are local to the body.  A parameter with a default value is optional, and required parameters come before optional ones.  A call must supply between the required and the total number of arguments, or it is a compile-time error.  A default is one value, worked out at each call which leaves its argument out; one which needs statements to run first (a `?.` chain, `??`) is a compile-time error.  A parameter's name can be omitted (`void f(int) { … }`):  the argument is still passed, but the body can't read it.

**Named arguments.**  An argument can be passed as `name: value`, in any order and mixed with positional arguments:  the positional ones fill the first unfilled parameters in order, and each named one fills its target.  It is an error to name a parameter the function doesn't have, to supply one both positionally and by name, or to omit a required one.

**Passing.**  An argument is passed as its type's kind dictates (§2.10.3):

- a primitive value is copied;
- a reference-class parameter refers to the argument's instance;
- a struct parameter owns an instance of its own, into which the argument is copied through the class's copy operator; the class's `init` and `deinit`, if it has them, run at entry and exit (§8.5);
- a parameter of a class which manages its own storage (`stringObj`) is given its own copy the same way (§8.5);
- a `ref` parameter of a struct or storage-managing class is bound to the argument itself (§3.7).

> ***[Z-machine]:** A routine call passes at most five arguments natively; parameters beyond the fifth travel through compiler-emitted globals, which appear in the header when any routine declares more than five.*

**Literal parameters.**  A parameter whose type is qualified `literal` accepts only a value known at compile time.  For a data type, that is a literal, constant arithmetic, a `#define` value, an enum member, or a `const` initialized from one of these; a variable, a member read, or a call is a compile-time error.  For a function type, it is a named function, or a lambda whose captured variables (§4.14) are all `const` locals with such a value.

In a union, `literal` qualifies the one member it precedes.  An argument is checked when it fits a `literal` member and no other:  in `literal func<void> | int`, a function must be literal, although an `int` can be a variable.  A `literal` parameter is itself literal inside the function.  `literal` on a variable, a member, or an array's element type is covered in §3.13.

**Example**

```bgl
void spawn(string name, int x, int y, bool hostile = false) { … }
spawn(x: 10, y: 20, name: "goblin", hostile: true);   // spawn("goblin", 10, 20, true)

void onClick(literal func<void> action) { … }        // keeps `action` to call later
onClick(=> print("Click."));                          // accepted: captures nothing
string dir = "north";
onClick(=> print(dir));                               // error: the lambda captures 'dir'
```

> ***Note:** A capturing lambda keeps its captured values in one slot per lambda, not one per call, so a kept lambda will read whatever was captured last.  `literal` turns that runtime surprise into a compile-time error, at the call that would cause it.*

## 6.4 Overload Resolution

Functions and methods with the same name and different parameter-type signatures can coexist:  functions at global scope, emitter or not, and emitter and non-emitter methods on classes and objects, including `operator()`, `operator[]`, and `operator[]=`.

Two declarations of the same name must differ in their **parameters**.  The return type is not part of the signature.  Declaring the same name twice with the same parameter types is a compile-time error.

An overload's emitted name is not part of the language; from raw I6, reach it through `$i6Name(add(int,int))` (§7.3.3).

A call is resolved in two steps:

1. **Arity.**  A candidate matches only if the argument count lies between its required and total parameter counts.
2. **Types.**  Among the candidates that match by arity, an exact type match for every argument wins over a match through an implicit conversion (`operator()`, §9.4), which in turn wins over a match through `var`.  An overload which takes a union is chosen only if no more specific overload matches.  A parameter of a value type (a struct, or a primitive class such as `float`) also takes an argument that the type's `operator =` accepts, converted by it.  A reference class's `operator =` converts no argument.  A declared object is an exact match for its class, just as a variable of that class is.

Two candidates which fit a call equally well, and differ only in parameters left to their defaults, make the call a compile-time error.

Type compatibility itself is in §2.10.1, and an overloaded name used as a value is covered in §2.9.

## 6.5 `replace` and `replaced()`

**Syntax**

```syntax
replace RETURN-TYPE NAME(PARAMS) { BODY }
replaced(ARGS)
```

`replace` replaces an already-declared global function.  If no matching function exists, the compiler will issue a warning and treat the declaration as a new function.

**Emitter functions.**  The body is swapped.  Matching is by name, return type, and the full parameter-type signature.  `replaced()` is not available in an emitter.

**Non-emitter functions.**  A replacement names the overload it replaces by its own parameter types; with a name that has only one definition, the parameters don't need to match it.  Replacements chain:  inside each replacement body, `replaced(args)` calls the immediately preceding definition, the one it directly replaced.  The arguments are type-checked against that predecessor's signature, which can differ from the replacement's own.  A predecessor that no replacement calls is not part of the program.

Replacing an `extern` routine defined by an I6 library works the same way in the source; the I6-side rules are in §16.6.

**Example**

```bgl
int step(int n) { return n; }
replace int step(int n) { return replaced(n) + 100; }   // calls the original
replace int step(int n) { return replaced(n) + 200; }   // calls the first replacement
// step(5) -> 305
```

**See also** §8.7.2 (`replace` for class and object members).

## 6.6 `self`

**Syntax**

```syntax
self
self.MEMBER
```

Inside a class or object method, `self` is the receiver:  the instance the method was called on.  A bare member name in a method body resolves as `self.member` (§3.8.2).  `self` is not valid outside a method body.

Beguile's `self` differs from Inform 6's `self`, which is the object that owns the running routine.  In an emitter body, `$self` is the receiver (§7.3).

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

`Main` is the program's entry point.  The compiler doesn't check for it:  a program built without an IF library binding defines `Main` itself, and a missing `Main` is reported when Inform 6 runs.

**Example**

```bgl
void Main() {
    print("Hello, world!^");
}
```

**See also** §23.3.1 (entry point under a binding).

### 6.7.1 `HandleGlkEvent`

`HandleGlkEvent(event, context, buffer)` is an `extern default` entry point on Glulx, with the Inform 6 library's meaning:  `event` is the four-word Glk event, `context` is `0` while a line is being read and `1` while a key is awaited, and the return value is `0` to carry on, `2` to end the input, or `-1` to keep waiting.  The runtime wraps whatever `HandleGlkEvent` the program defines.  On a hyperlink event it renews the window's hyperlink request and calls a function target itself (§21.11); every other event goes to the program's function.  A program will behave the same with or without a library.

A program declares it as `int HandleGlkEvent(var event, var context, var buffer)`.  You read the event's words with `bgl.asm.aload(event, n)` (§21.13.1):  word 0 is its type, an `eGlulxEventType` (`none`, `timer`, `charInput`, `lineInput`, `mouseInput`, `arrange`, `redraw`, `soundNotify`, `hyperlink`, `volumeNotify`), and for a hyperlink, word 2 is the link's value.

<!-- doctest: compile -->
```bgl
#beguilerSettings { target = Glulx; }
enum eLinks { wait = 2 }
int HandleGlkEvent(var event, var context, var buffer){
    if (bgl.asm.aload(event, 0) == (int)eGlulxEventType.hyperlink
        && bgl.asm.aload(event, 2) == (int)eLinks.wait) print("[waiting]");
    return 0;
}
void main(){ }
```

Note that a program's own `HandleGlkEvent` which prints at the prompt must cancel line input itself, as in Inform 6 (§21.11).

## 6.8 Distributors

**Syntax**

```syntax
distributor NAME ;
```

A **distributor** is a function which can be called with several arguments, each of which becomes a call of its own, in the order written.  The declaration names a function which already exists, and declares no parameters and no return type:

```bgl
void say(string line) { print(line); print("^"); }
distributor say;

// … in a routine:
say("One.", "Two.", "Three.");     // say("One."); say("Two."); say("Three.");
```

Each argument is resolved against the function's overloads separately (§6.4).  An argument is evaluated when its call is reached:  the first call has returned before the second argument is read.

A **distributed call** is a statement and has no value, although it can be the whole body of a lambda (`() => print("a", n)`).  Using one where a value is wanted is a compile-time error, as is naming an argument (§6.3).  The function itself can return a value; however, each call's result is discarded.  A call with one argument, or with none, is an ordinary call.

A call with several arguments is the call as written when an overload accepts those arguments, and a distributed call otherwise.  The same holds for parameters with defaults (§6.3).

A distributor's overloads are restricted:  no overload's second parameter can take a value which any overload's first parameter takes, or the declaration is a compile-time error:

```bgl
void say(string line, string end = "^") { print(line, end); }
distributor say;      // ERROR: 'end' takes what a first argument takes

void echo(string line, int times = 1) { … }
void echo(int n) { … }
distributor echo;     // ERROR: 'times' takes an int, and echo(int) takes one first
```

A second parameter of type `var` takes every value, so a function with one can't be a distributor.  The check covers every overload of the name, including one declared after the `distributor` line.

> ***Note:** A mistyped call can be a distributed call.  `echo("twice", "2")`, where `2` was meant, is two calls, since each argument is acceptable as a first argument.*

A distributor is declared at global scope, for a global function, or in the body of a class or an object, for a method of it.  A class's distributors are inherited.  Naming something which is not a function there is a compile-time error.  The declaration can come before or after the function and the calls.

```bgl
class journal : object {
    void add(string line) { print(line, "^"); }
    void add(int n)       { print(n, "^"); }
    distributor add;
}
journal log { }

// … in a routine:
log.add("Entered the cave.", 3, "bats.");
```

The receiver of a distributed method call can't contain a call:  `nextJournal().add("one", "two")` is a compile-time error.

> ***Note:** `print` is a distributor (§21.4):  `print("You have ", count, " coins.^");`.*

**See also** §6.4 (overload resolution), §21.4 (`print`).
