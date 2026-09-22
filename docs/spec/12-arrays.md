# 12 Arrays

<!-- toc -->
- [12.1 Overview](#121-overview)
- [12.2 Declaring Arrays](#122-declaring-arrays)
- [12.3 Subscripts, Size and Length](#123-subscripts-size-and-length)
- [12.4 Byte Arrays — `array<char>`](#124-byte-arrays-arraychar)
- [12.5 Assignment and Copy Semantics](#125-assignment-and-copy-semantics)
- [12.6 Local Arrays and Lifetime](#126-local-arrays-and-lifetime)
- [12.7 Member Arrays](#127-member-arrays)
- [12.8 `rawArray<T>`](#128-rawarrayt)
  - [12.8.1 Raw Views](#1281-raw-views)
  - [12.8.2 File-scope `rawArray<T>` Literals](#1282-file-scope-rawarrayt-literals)
  - [12.8.3 Member `rawArray<T>`](#1283-member-rawarrayt)
- [12.9 Arrays of Arrays](#129-arrays-of-arrays)
- [12.10 Element Type Requirements](#1210-element-type-requirements)
- [12.11 `extend` for Arrays](#1211-extend-for-arrays)
<!-- /toc -->

## 12.1 Overview

`array<T>` is a typed word array with a capacity fixed at declaration and a run-time tracked length.
The element type `T` is mandatory; bare `array` is not a type. `T` may be any base type (`int`,
`bool`, `string`, `object`, `char`, `dictionaryWord`), any user-defined class, or another array type
(§12.9). `rawArray<T>` (§12.8) is the untracked form used at the I6 boundary.

The `<array>` extension is loaded by the runtime core, so every array operation is available
without an explicit `#include <array>`. Subscripting, `size()`, `length()` and `for … in` are built
in; `setLength()`, `clear()`, value-semantic assignment and the remaining methods (`append`,
`indexOf`, `sort`, …) are provided by `<array>` and are cataloged in §22.4.

## 12.2 Declaring Arrays

**Syntax**

```syntax
array<⟨type⟩> ⟨name⟩[⟨n⟩] ;
array<⟨type⟩> ⟨name⟩ = { ⟨value⟩ , … } ;
array<⟨type⟩> ⟨name⟩ ;
```

`<`, `>`, `[` and `]` are literal. The first form is a sized array: capacity `⟨n⟩`, zero-initialized,
length 0. The second is an initialized array: capacity and length equal to the number of values. The
third is declared without capacity.

**Description**

Arrays may be declared at file scope, as locals inside a function body (§12.6), and as class or object
members (§12.7). A global array has no element-count limit. Element type checking is enforced at every
subscript site and in initializers: reading an element yields a `T`, writing requires a value
compatible with `T`, and a cross-type assignment (a `string` into an `array<int>`) is a compile-time
error.

**Example**

```bgl
array<int> scores[5];
array<int> primes = {2, 3, 5, 7, 11};
array<Room> visited;
```

## 12.3 Subscripts, Size and Length

**Syntax**

```syntax
⟨array⟩[⟨index⟩]
⟨array⟩[⟨index⟩] = ⟨value⟩
⟨array⟩ . size ( )
⟨array⟩ . length ( )
⟨array⟩ . setLength ( ⟨n⟩ )
⟨array⟩ . clear ( )
```

`[` and `]` are literal.

**Description**

Subscripts are zero-based. `size()` returns the number of elements allocated for the array, whether or
not those slots hold meaningful values.

An array also carries an explicit *length*, the count of in-use entries. Length is set at allocation
(N for an initializer list, 0 for a sized array) and changes only through explicit operations:
`setLength()`, `clear()`, and the mutators of `<array>` (§22.4). A slot write (`arr[i] = v`) does not
change length; the array behaves as a buffer with a cursor. `setLength(n)` is range-checked to the
signed word range of the target; `clear()` zeroes every slot up to `size()` and resets length to 0.

Every traversal in `<array>` (`indexOf()`, `contains()`, `removeValue()`, `sort()`, `first()`,
`last()`, …) walks the in-use range only; slots beyond `length()` are not searched, sorted or matched.
`clear()` is the one capacity-wide operation. A sized array filled only by slot writes therefore has
`length() == 0` and reads as empty; use `+=`, `insert()` or `setLength()` to make the slots live.

On an `array<char>` (§12.4), `size()` and `length()` both read the buffer's length word, which starts
at the declared capacity. On a `rawArray<T>` (§12.8) `size()`, `length()` and `for … in` are
compile-time errors, except that a raw member array reports its property length from both. `isTracked()`
(§22.4) tells a tracked array from an untracked one.

**Example**

```bgl
array<int> scores[5];

int x = scores[2];
scores[0] = 99;
int n = scores.size();          // → 5
int used = scores.length();     // → 0: a slot write does not change length
scores.setLength(1);            // → length() is now 1
scores.clear();                 // every slot 0, length 0
```

**Notes**

> **[Z-machine]** `setLength(n)` accepts 0..32767.

> **[Glulx]** `setLength(n)` accepts 0..2^31-1.

## 12.4 Byte Arrays — `array<char>`

**Syntax**

```syntax
array<char> ⟨name⟩[⟨n⟩] ;
array<char> ⟨name⟩ = { ⟨value⟩ , … } ;
array<char> ⟨name⟩ = "⟨text⟩" ;
```

`<`, `>`, `[` and `]` are literal.

**Description**

`array<char>` is a byte array: elements are stored and accessed as bytes. All other element types are
word arrays. A byte array holds character or small-integer data; its initializer and element writes
accept both character literals and integers (`{'H', 'i'}`, `{5, 10, 15}`, `bytes[i] = 99`). An
integer literal outside 0..255 is a compile-time error; a non-literal `int` that exceeds a byte wraps
to its low byte at run time. A string initializer fills the array with the characters of the text.

Reading an element yields a `char`, which widens to `int` freely, so `int a = bytes[i]`,
`bytes[i] + bytes[j]` and `bytes[i] > threshold` all work without casts. A `char` result is
byte-wide; accumulate into an `int`. Printing a `char` prints a character; to print its numeric value,
read it into an `int` first.

**Example**

```bgl
array<char> bytes = {'H', 'i', 5, 10};
array<char> word  = "hello";

bytes[2] = 99;                  // an integer into a byte slot
int big = 300;
bytes[3] = big;                 // → 44: wraps to the low byte at run time
int total = bytes[2] + bytes[3];
print(word[0]);                 // → h
int code = word[0];
print(code);                    // → 104
```

## 12.5 Assignment and Copy Semantics

**Syntax**

```syntax
⟨destination⟩ = ⟨source⟩ ;
```

**Description**

Assigning one array to another (`dst = src`) copies the elements; the two arrays are independent
afterwards. This is the mechanism that captures an ephemeral array
result (§12.6). Copying into an element of an owning element type goes through that type's
`static operator =` (§12.10).

**Example**

```bgl
array<int> src = {1, 2, 3};
array<int> dst[3];

dst = src;                      // copies the elements
dst[0] = 99;                    // src[0] is still 1
```

## 12.6 Local Arrays and Lifetime

**Description**

A local array (declared inside a function body, sized or initialized) is allocated per call at
function entry and freed at function exit, so each call, including a recursive one, has its own
storage (§18.10). File-scope arrays live in permanent storage and may be returned freely.

Because a local array's storage is reclaimed on return, a returned local array is an *ephemeral*
reference, not an owned value, exactly like an ephemeral string (§22.3): the storage it names has been
freed by the time the caller sees it. To keep the result, assign it to a typed local, which copies it
(§12.5). If the reference is consumed in an expression, or passed straight into another call, without
first being assigned, the behavior is undefined. The same applies to results of `<array>` methods and
chains on a local array (§22.4, §22.5).

**Example**

```bgl
array<int> build() {
    array<int> tmp[3];
    tmp[0] = 1; tmp[1] = 2; tmp[2] = 3;
    return tmp;                 // ephemeral
}

array<int> keep = build();      // copies into stable storage
int n = keep[0];                // safe
```

## 12.7 Member Arrays

**Syntax**

```syntax
array<⟨type⟩> ⟨name⟩[⟨n⟩] ;
array<⟨type⟩> ⟨name⟩ = { ⟨value⟩ , … } ;
ref array<⟨type⟩> ⟨name⟩ ;
```

`<`, `>`, `[` and `]` are literal. These forms appear in a class or object body.

**Description**

An `array<T>` declared as a class or object member (§8.3.1, §11.8) has the same semantics as any other
array: `length()`, `append()`, `pop()` and the rest behave identically. Storage is per instance: every
instance of a class has its own copy of a member array, with the declared capacity and initializer.

| Declared capacity | Storage |
|---|---|
| fits in one property | inline in the object's property, with the length in a trailing slot |
| too large for one property | separate storage owned by that instance; the property refers to it |

Two member kinds always keep the bare I6 layout and are never moved to separate storage: a
`rawArray<T>` member (§12.8.3), where exceeding the property limit is an error, and a member bound to
an `additive` property, which must be declared `rawArray<T>` (§11.7.2).

A member may also be declared `ref`, in which case it holds a reference to an array owned elsewhere,
is bound with `:=` (§3.7), and owns no storage of its own.

**Example**

```bgl
class Inventory : object {
    array<object> held[8];                  // per-instance storage
}

array<int> shared[8];
object w { ref array<int> log; }

w.log := shared;      // bind
w.log += 5;           // appends to shared
```

**Notes**

> **[Z-machine]** A property holds at most 32 words, counting the length slot, so a member array
> larger than that uses separate storage. Byte-array members (`array<char>`) are not subject to the
> property limit.

> **[Glulx]** Properties have no practical size limit; member arrays are always stored inline.

## 12.8 `rawArray<T>`

### 12.8.1 Raw Views

**Syntax**

```syntax
rawArray<⟨type⟩> ⟨name⟩
```

`<` and `>` are literal. The form appears as a parameter, an `extern` declaration, or a member.

**Description**

`rawArray<T>` is a typed view over a bare I6 word array: no length header and no tracking. It is
declarable at file scope, as an `extern`, as a parameter type, and as a class or object member. Its
purpose is interoperability: an I6 buffer handed to Beguile (the `results` array of a `parse_error`
entry point, a library table, an array declared in an `#i6` island, §15.2) has no count word, and
receiving it as a `rawArray<T>` parameter allows ordinary subscript syntax on it.

| | `array<T>` | `rawArray<T>` parameter |
|---|---|---|
| Layout | count word, then elements | elements only |
| `size()` / `length()` | available | unavailable; the length is passed explicitly |
| Length tracking | yes | none |

Because a `rawArray<T>` carries no length, `for … in` over a `rawArray<T>` parameter is a compile-time
error; iteration uses an indexed loop bounded by a length supplied separately. Elements are
type-checked at every subscript and may be cast like any other value.

**Example**

```bgl
bool ext_parsererror(int etype, rawArray<var> results) {      // NOTHING_PE, PutOn, Insert: library names
    if (etype == NOTHING_PE && ((verb)results[0] == PutOn || (verb)results[0] == Insert))
        rtrue("You are not holding one.");
    rfalse;
}

void process(int v) { print(v); }

void walk(rawArray<int> buf, int n) {
    for (int i in 0 to n - 1) { process(buf[i]); }
}
```

### 12.8.2 File-scope `rawArray<T>` Literals

**Syntax**

```syntax
rawArray<⟨type⟩> ⟨name⟩ = { ⟨value⟩ , … } ;
```

`<` and `>` are literal.

**Description**

A file-scope `rawArray<T>` declared with an initializer is an *untracked* `array<T>`: it has the
count-word-then-elements layout of `array<T>`, and its count word holds the true element count, but it
carries no length-tracking trailer even though `<array>` is loaded. This is the form to use
when a bare I6 array API reads the array by its count word (for example the single-array form of
orLibrary's `util.orArray`, §15.10); a tracked `array<T>` would over-count there.

A file-scope `rawArray<T>` literal and a `rawArray<T>` parameter are not interchangeable: the literal
is count-prefixed and the parameter is elements-only. The type system keeps them apart; a literal has
the `array` element-covariant type and is passed where an `array<var>` is expected.

**Example**

```bgl
array<string>    trk = { "a", "b", "c" };   // tracked
rawArray<string> raw = { "a", "b", "c" };   // untracked; count word = 3
```

### 12.8.3 Member `rawArray<T>`

**Syntax**

```syntax
rawArray<⟨type⟩> ⟨name⟩ = { ⟨value⟩ , … } ;
```

`<` and `>` are literal. The form appears in a class or object body.

**Description**

A `rawArray<T>` member is a bare property array. It is required for members that contribute to an
`additive` property, and its `size()`, `length()` and permitted operations on such members are
specified in §11.7.2.

**Example**

```bgl
class Room { rawArray<dictionaryWord> name = {.box, .crate}; }
```

## 12.9 Arrays of Arrays

**Syntax**

```syntax
array<array<⟨type⟩>> ⟨name⟩ = { { ⟨value⟩ , … } , … } ;
⟨name⟩[⟨i⟩]
⟨name⟩[⟨i⟩][⟨j⟩]
```

`<`, `>`, `[` and `]` are literal. `⟨name⟩[⟨i⟩]` is an `array<⟨type⟩>`; `⟨name⟩[⟨i⟩][⟨j⟩]` is an
element of `⟨type⟩`.

**Description**

The element type of an array may itself be an array type, to any depth. A nested initializer supplies
one braced list per inner array, and the inner arrays may differ in length. `name[i]` is an
`array<T>` and supports `length()` and the array surface; `name[i][j]` reads or writes an element,
and `name[i][j].member = v` writes through an object element. `for (array<T> row in name)` iterates
the outer array with `row` typed `array<T>`. A nested array may be declared as a local. A wrong
element type or a wrong nesting depth in an initializer is a compile-time error.

**Example**

```bgl
array<array<int>> grid = { {1,2,3}, {4,5} };

int v = grid[0][1];                    // 2
int rows = grid.length();              // 2
int cols = grid[1].length();           // 2
grid[0][1] = 99;
int total = 0;
for (array<int> row in grid) {
    for (int i = 0; i < row.length(); i++) { total += row[i]; }
}
```

## 12.10 Element Type Requirements

**Description**

`array<T>` cannot see the element type at run time. It asks `T` for four operations, each supplied as
an operator reference (§7.3.1), and every one is optional. A type that publishes nothing gets plain
word semantics and pays nothing.

| `T` publishes | Used by | Absent |
|---|---|---|
| `operator ==` (static or instance) | `indexOf`, `find`, `contains`, `removeValue`, `-=` | word comparison (identity) |
| `operator <=>` (static or instance) | `sort()` | signed word ordering, and a compile-time warning for a class (ordering by object address) |
| `static operator =` | `[i] =`, `append`, `prepend`, `insert` | raw word store |
| `static deinit(T)` | `remove`, `removeValue`, `-=`, `clear`, local scope exit | nothing is released |

For `int`, `char`, `bool` and `object` the defaults are correct. The default is silently wrong for a
type whose value semantics differ from its word: a content-comparing type without `operator ==`
matches on address (`string` and `stringObj` publish one for this reason); `float` is sign-magnitude,
so it publishes `operator <=>` to sort negatives correctly; a type that owns storage without
`static operator =` receives a raw word store and the slot holds a value the array does not own; a
type that owns storage without `static deinit` leaks every dropped element, and a local array of it
leaks all of them at scope exit.

**Static versus instance.** `==` and `<=>` may be either form; the instance form is sent to the left
operand. A bare-word element type (`string`, `float`) has no object to receive a message and must
publish the static form; only an object-backed element type may use the instance form. `operator =`
must be `static`: on a first write the slot holds 0, so there is no receiver. An instance
`operator =` is ignored and the store is a word write. `deinit` has both forms and they do different
jobs: `emitter deinit()` releases a receiver at scope exit, `static deinit(T v)` releases a slot the
container has no receiver for. A type that owns storage generally wants both (§8.5).

**Assignment copies.** Every store into an owning element type goes through `static operator =`,
which copies the incoming value into the buffer the slot owns. Passing an already-allocated value
allocates a second time; the original is not released.

**Example**

```bgl
array<stringObj> slots[4];

slots += "alpha";                    // the slot allocates and owns
slots[0] = "replaced";               // reuses slot 0's buffer

stringObj tmp = "beta";              // allocated on declaration (§22.3)
slots += tmp;                        // copies into a new slot allocation; tmp keeps its own buffer,
                                     // released at scope exit, not by the array
```

**See also** §9.6 — `static` operators; §8.5 — the value form of `deinit`; §22.4 — the `<array>` methods that.
consult these operators.

## 12.11 `extend` for Arrays

**Syntax**

```syntax
extend ⟨array⟩ {
    inject ⟨element⟩ [ after ⟨ref⟩ | before ⟨ref⟩ | first | last ] ;
    remove ⟨ref⟩ ;
    move   ⟨ref⟩ [ after ⟨ref⟩ | before ⟨ref⟩ | first | last ] ;
}
```

`⟨ref⟩` is an element name or a zero-based index written `[⟨n⟩]`, where `[` and `]` are literal.

**Description**

`extend arrayName { … }` edits the initializer of a previously declared array at compile time, so a
later file (or a later point in the same file) may add to, remove from or reorder the array without
touching the original declaration. It is build-time only; nothing runs at startup. Run-time mutation
uses the array's methods (§22.4). The body holds *array extension statements*: `inject`, `remove`
and `move`.

- `inject element [position]` splices an element in. The element is a named object, a literal, or an
  inline object `Type{ … }` (§11.3); when the element type is an object-backed class the type may be
  omitted (`inject { … } last;`). With no position clause the element is appended.
- `remove X` removes an element.
- `move X [position]` repositions an existing element (remove, then inject at the new position).

`after X` and `before X` place the element immediately next to an existing element; `first` and
`last` name the two ends, and no clause means `last`. A reference `X` or `Y` is either an element
name (the identifier of the object an element refers to) or a zero-based index `[N]`
(`after [0]`, `remove [2]`).

The statements apply in source order, so a later `move` or `remove` sees the effect of earlier ones, and
`[N]` indices are relative to the array's state at that point. An unknown name or an out-of-range
`[N]` is a compile-time error.

**Example**

```bgl
array<rule> before = { cantTakeYourself, cantTakeScenery };

extend before {
    inject enteringDark  after  cantTakeScenery;
    inject reachRule     before cantTakeYourself;
    inject fallbackRule  first;
    inject wrapUpRule;                              // append
    remove cantTakeScenery;
    move   reachRule     last;
    move   fallbackRule  after cantTakeYourself;
}
```

**See also** §11.10 — `extend` for objects; §13.5 — `extend` for verb grammar; §8.7.1 — `extend class`.
