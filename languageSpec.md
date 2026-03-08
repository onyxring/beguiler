# Beguile Language Specification

Beguile is a statically-typed, C-like language that transpiles to Inform 6 (I6).
It is part of the ORBIT toolchain (OnyxRing Beguile-Inform Transpiler).

---

## Comments

```bgl
// Single-line comment

/* Multi-line
   comment */
```

---

## Directives

### Include standard library

```bgl
#include <system>
#include <string>
```

Pulls in a `.bgl` file from the standard library (`beguilib/`). The file is parsed and its declarations become available.

### Include raw I6

```bgl
#includeI6 "parser"
```

Emits an I6 `#include` directly into the transpiled output. Use this to include I6 files that have no Beguile wrapper.

---

## Types

### Primitive types

| Type          | Description                                              |
|---------------|----------------------------------------------------------|
| `int`         | Integer value                                            |
| `bool`        | Boolean value; underlying I6 representation is `eBool`  |
| `string`      | String object (backed by an I6 string utility class)     |
| `void`        | No return value (functions only)                         |
| `object`      | Base class for all IF world objects                      |

### Literal pseudo-types

These types are not declared by user code; they are inferred by the compiler when a literal value appears.

| Pseudo-type     | Example        |
|-----------------|----------------|
| `intLiteral`    | `42`           |
| `stringLiteral` | `"hello"`      |

They participate in operator overload resolution (e.g. `operator = (intLiteral v)`).

### Enums

```bgl
extern enum eBool {
    true,
    false
}
```

`extern` indicates the enum is defined in I6 and Beguile is only declaring it for type-checking purposes.

---

## Global Declarations

### Variables

```bgl
bool isGood = true;
bool isBad;
```

Global variables are declared at file scope. An initialiser is optional.

### Functions

```bgl
void initialise(string bad) {
    // body
}
```

See [Functions](#functions) for full details.

---

## Classes

### Defining a class

```bgl
class MyClass {
    int count;
}
```

Members may be variables or functions (including operators and emitters).

### Declaring an extern class

```bgl
extern class string {
    emitter string operator = (stringLiteral v) { $self.set(v); }
    bool jump(int);
}
```

`extern` means the class is implemented in I6. Beguile uses the declaration for type-checking and emitter dispatch only.

Rules for `extern class` members:
- **Emitters** must have a body (see [Emitters](#emitters)).
- **Non-emitter functions** must *not* have a body — they are terminated with `;`.
- Parameter names are optional in non-emitter declarations: `bool jump(int);` is valid.

---

## Functions

```bgl
returnType name(paramType paramName, ...) {
    // body
}
```

### Parameters

```bgl
void foo(int x, string label) { ... }
```

#### Optional / default parameters

```bgl
void foo(int x, int y = 0) { ... }
```

A parameter with a default value may be omitted at the call site. Required parameters (no default) must come before optional ones. Arity is validated at compile time against the required-to-total range.

---

## Emitters

An `emitter` function contains raw I6 code that is inlined at the call site rather than generating a real I6 function call.

```bgl
emitter returnType name(params) {
    raw i6 code here
}
```

Within the body:

- **`$self`** — replaced with the receiver object's name (for class member emitters).
- **Parameter names** — replaced with the actual argument expressions at the call site.

### Operator emitters

```bgl
emitter string operator + (string v) {
    $self.append(v)
}
```

Defines how the `+` operator is compiled when applied to a `string` on the left and a `string` on the right. The body is inlined wherever `str + other` appears.

Supported operator symbols: `=`, `+`, `-`, `*`, `/`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `?=`, `&`, `|`, `^`, `<<`, `>>`.

### Conversion operator emitters

```bgl
emitter bool operator() {
    $self.equals("true")
}
```

Declared inside a class. Converts `$self` (the source value) to the return type. Zero parameters — the return type fully specifies the target type.

The compiler automatically applies a conversion operator when an argument of the source type is passed to a parameter of the target type (and no exact match exists).

---

## Type Compatibility

An argument of type `A` is compatible with a parameter of type `B` if any of the following hold:

1. `A == B` (exact match)
2. Type `B` declares `emitter B operator = (A v)` (target accepts source via assignment operator)
3. Type `A` declares `emitter B operator()` (source converts itself to target)

These rules apply at:
- Variable assignment (`variable = expr;`)
- Function call arguments
- Method call arguments

An incompatible assignment or call is a compile-time error.

---

## Statements

### Variable declaration

```bgl
string str;
int bloof = 4 + 5;
```

Local variables are declared inside a function body. An initialiser is optional.

### Assignment

```bgl
str = "jim is still here";
isBad = true;
```

The left-hand side must be a declared variable. If the left-hand type has an `emitter operator =` matching the right-hand type, it is inlined; otherwise a plain I6 assignment is emitted.

### Function call

```bgl
print("jim was here");
```

The compiler resolves the call against all declared global functions with that name. Errors are reported for:
- Undeclared function name
- Wrong number of arguments (outside required-to-total range)
- Incompatible argument types

### Method call

```bgl
bad.jump(1);
str.print();
```

Resolved against the class of the receiver variable. Same arity and type-compatibility rules apply.

### If statement

```bgl
if (str == "jim") print(str);

if (str == "jim") {
    print(str);
}
```

Condition is an expression. Both single-statement and braced-block forms are supported.

### Return

```bgl
return;
return someValue;
```

---

## Expressions

Expressions are composed of:

- **Identifiers**: variable names, enum values (`true`, `false`)
- **Literals**: integer literals (`42`), string literals (`"hello"`)
- **Binary operators**: `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `&`, `|`, `^`, `<<`, `>>`
- **Chained operators**: `str + " followed" + apnd` — resolved left-to-right

The compiler infers a `resolvedType` for each expression, which drives operator and emitter dispatch.

---

## Standard Library

### `<system>`

Declares the base types and print functions.

| Declaration                          | Description                        |
|--------------------------------------|------------------------------------|
| `extern class object {}`            | Base class for IF world objects    |
| `extern class int { ... }`          | Integer type with assignment emitter |
| `extern class bool { ... }`         | Boolean type with print and operators |
| `extern enum eBool { true, false }` | The underlying I6 boolean enum     |
| `emitter void print(stringLiteral)` | Print a string literal             |
| `emitter void print(intLiteral)`    | Print an integer literal           |
| `emitter void print(int)`           | Print an int variable              |

### `<string>`

Declares the string type and string print function.

| Declaration                                      | Description                              |
|--------------------------------------------------|------------------------------------------|
| `emitter string operator = (stringLiteral v)`   | Assign from a string literal             |
| `emitter string operator = (string v)`          | Assign from another string               |
| `emitter string operator + (stringLiteral v)`   | Append a string literal                  |
| `emitter string operator + (string v)`          | Append another string                    |
| `emitter eBool operator == (stringLiteral v)`   | Equality comparison with a string literal|
| `bool jump(int);`                               | Extern I6 method (example)               |
| `string print();`                               | Extern I6 print method                   |
| `emitter bool operator() { ... }`              | Conversion: string → bool (example)      |
| `emitter void print(string str)`               | Global print for string variables        |

---

## Example Program

```bgl
#include <system>
#include <string>
#includeI6 "parser"

bool isGood = true;
bool isBad;

void initialise(string bad) {
    string str;
    string apnd;

    bad.jump(1);

    str = "jim is still here";
    apnd = "joot";

    str = str + " followed" + apnd;

    if (str == "jim") print(str);

    int bloof = 4 + 5;

    isBad = true;
    print("jim was here");

    return;
}
```
