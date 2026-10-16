# 12 Arrays

<!-- toc -->
- [12.1 Overview](#121-overview)
- [12.2 Declaring Arrays](#122-declaring-arrays)
- [12.3 Subscripts, Size and Length](#123-subscripts-size-and-length)
- [12.4 Byte Arrays — `array<char>`](#124-byte-arrays-arraychar)
- [12.5 Assignment and Copy Semantics](#125-assignment-and-copy-semantics)
- [12.6 Local Arrays, the Heap and Collection](#126-local-arrays-the-heap-and-collection)
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

Subscripting, `size`, `length` and `for … in` are built in. Assigning `length`, `clear()`,
value-semantic assignment and the remaining methods (`append`, `indexOf`, `sort`, …) come from the
`<array>` extension, which a program enables with `#include <array>`; they are cataloged in §22.4.
A program that doesn't include it carries none of its code.

## 12.2 Declaring Arrays

**Syntax**

```syntax
array<⟨type⟩> ⟨name⟩[⟨n⟩] ;
array<⟨type⟩> ⟨name⟩ = { ⟨value⟩ , … } ;
array<⟨type⟩> ⟨name⟩ = ⟨value⟩ ;
array<⟨type⟩> ⟨name⟩[⟨n⟩] = { ⟨value⟩ , … } ;
```

`<`, `>`, `[` and `]` are literal. The first form is a sized array: capacity `⟨n⟩`, zero-initialized,
length 0. `⟨n⟩` is any compile-time integer: an integer literal, a `#define`d symbol whose value is
an integer (§14.2.1), a `const int` initialized with an integer literal (§3.4), or an integer
`#beguilerSettings` property (§17.7). The second is an initialized array: capacity and length equal to the number of values. The
third is the one-element form of the second: a single value of the element type needs no braces, so
`array<int> x = 3;` is `array<int> x = {3};`. A value that is itself an array initializes `⟨name⟩`
from that array instead; the value's type decides which is meant. A local declared from a call's or a
chain's result takes that array; one declared from another array gets a copy of its own, with that array's
capacity; a sized declaration (`array<int> x[⟨n⟩] = src;`) copies into its declared capacity (§12.5, §12.6).
At file scope the value is copied in at startup. The same shorthand applies to a
list-typed class or object member (§11.5.3, §12.7). The fourth combines the first two: capacity `⟨n⟩`,
length the number of values, the remaining slots zeroed. A list longer than `⟨n⟩` is a compile-time
error.

An array always has storage: a capacity of at least 1, or at least one element. A global or local
array declared with neither (`array<int> g;`), with an empty `{}`, or with a capacity of 0 is a
compile-time error. To share an array that lives elsewhere, declare a `ref` instead (§3.7).

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
array<Room> visited[20];
```

## 12.3 Subscripts, Size and Length

**Syntax**

```syntax
⟨array⟩[⟨index⟩]
⟨array⟩[⟨index⟩] = ⟨value⟩
⟨array⟩ . size
⟨array⟩ . length
⟨array⟩ . length = ⟨n⟩
⟨array⟩ . clear ( )
```

`[` and `]` are literal.

**Description**

Subscripts are zero-based. `size` is the number of elements allocated for the array, whether or
not those slots hold meaningful values. It is read-only.

Every subscript of an `array<T>` is checked against its `size`, for reads and writes alike. A
constant index outside `0` to `size - 1` of an array whose size is declared is a compile-time error;
any other out-of-range index prints a run-time message, and the read (which yields `0`) or the write
is skipped. A subscript never consults or changes the length. `rawArray<T>` subscripts are not
checked (§12.8).

An array also carries an explicit *length*, the count of in-use entries. Length is set at allocation
(N for an initializer list, 0 for a sized array) and changes only through explicit operations:
assigning `length`, `clear()`, and the mutators of `<array>` (§22.4). A slot write (`arr[i] = v`) does
not change length; the array behaves as a buffer with a cursor. An assigned `length` is limited to
`size`, and a negative one is a runtime error; `clear()` zeroes every slot up to `size` and resets
length to 0.

Every traversal in `<array>` (`indexOf()`, `contains()`, `removeValue()`, `sort()`, `first()`,
`last()`, …) walks the in-use range only; slots beyond `length` are not searched, sorted or matched.
`clear()` is the one capacity-wide operation. A sized array filled only by slot writes therefore has
`length == 0` and reads as empty; use `+=`, `insert()` or assign `length` to make the slots live.

On an `array<char>` (§12.4), `size` is its capacity, kept in the word just before the buffer, and
`length` is the buffer's own length word, which starts at the declared capacity (with `<buf>`, a
sized declaration starts empty); assigning its `length` needs `<buf>` (§22.4). On a `rawArray<T>` (§12.8) `size`, `length` and `for … in` are
compile-time errors, except that a raw member array reports its property length from both. `isTracked()`
(§22.4) tells a tracked array from an untracked one.

**Example**

```bgl
#include <array>
array<int> scores[5];

int x = scores[2];
scores[0] = 99;
int n = scores.size;            // → 5
int used = scores.length;       // → 0: a slot write does not change length
scores.length = 1;              // → length is now 1
scores.clear();                 // every slot 0, length 0
```

**Notes**

> **[Z-machine]** `length` accepts 0..32767, and no more than `size`.

> **[Glulx]** `length` accepts 0..2^31-1, and no more than `size`.

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
⟨ref destination⟩ := ⟨source⟩ ;
```

**Description**

An array owns its storage, as an object with a copy operator owns its instance (§2.10.3): `dst = src` copies
`src`'s elements and length into `dst`'s storage, up to `dst`'s capacity, and the two stay independent. That
holds for every array with storage of its own: a global, a local, a member (whose storage is its property's
data, §12.7). `dst.copyFrom(src)` from `<array>` does the same. A source nothing else holds, a chain's result
or a call's returned array, is freed once it is copied.

Only a `ref array` (§3.7) re-points: `r := a` makes `r` refer to `a`'s array, and `=` through `r` copies into
the array it refers to. Parameters and returns share, as for every object: a function's array parameter refers
to the caller's array, and a returned array passes to the caller (§12.6). `a == b` is true when they are the
same array.

**Example**

```bgl
#include <array>
array<int> src = {1, 2, 3};
array<int> dst[3];

dst = src;                      // dst has its own copy
dst[0] = 99;                    // src[0] is still 1
ref array<int> same := src;     // same refers to src's array
same[0] = 5;                    // src[0] is now 5
```

## 12.6 Local Arrays, the Heap and Collection

**Description**

A local array (declared inside a function body, sized or initialized) takes its storage from the **heap** when its
routine is entered, one array per call, so each call, a recursive one included, has its own. A local array declared
in a loop body is that one array, emptied each time its declaration runs. File-scope and member arrays live in
permanent storage and are never freed.

A local array declared from a call's result or a chain's result (`array<int> e = evens(nums);`) takes that array:
nothing is copied. One declared from another array (`array<int> c = a;` or `= obj.arr`) gets a copy of its own.

A heap array lives as long as something refers to it, as in C# and TypeScript: a function may return its local
array, append it to an array of arrays, or bind a `ref` global or member to it with `:=`, and the reference stays
good. `<linq>` results (§22.5) are heap arrays too.

Arrays are freed in two ways:

- **At the routine's exit.** A local array that no reference has left is freed as its routine returns,
  and so is an array a local took from a call or a chain.
  A returned array passes to the caller instead. A routine containing an `#i6` block leaves its local
  arrays to the collector, since the raw code may have kept one.
- **By the collector.** The rest are freed by a mark-and-sweep collector when nothing refers to them.
  It runs at the prompt, through the library binding (§23), and acts only once a quarter of the heap
  has been allocated since it last ran. Lifetime is reachability, not turns: an array kept in a global,
  a property or another live array survives any number of prompts.

The heap's size is the `heapSize` setting (§17.4). Running out is a run-time error that names it.

`bgl.gc` (§21.15) frees one array at once (`bgl.gc.free(a)`), runs a collection (`bgl.gc.collect()`),
and reports the heap's figures. A program that never reaches a prompt, or a library with a turn loop of
its own, calls `bgl.gc.collect()` at a point where no array it still needs is held only in a local.

**Example**

```bgl
array<int> build() {
    array<int> tmp[3];
    tmp[0] = 1; tmp[1] = 2; tmp[2] = 3;
    return tmp;                 // the caller gets this array
}

array<int> keep = build();      // keep takes build's array
int n = keep[0];                // → 1
```

> **Conservative collection.** The collector treats every word of memory outside the heap as a
> possible reference. An integer that happens to equal an array's address keeps that array until the
> next collection; a live array is never freed.

> **Element instances.** An array of a struct owns its slots' instances (§12.10), and freeing the
> array releases them. A function can return such an array; the instances go with it.

## 12.7 Member Arrays

**Syntax**

```syntax
array<⟨type⟩> ⟨name⟩[⟨n⟩] ;
array<⟨type⟩> ⟨name⟩ = { ⟨value⟩ , … } ;
array<⟨type⟩> ⟨name⟩[⟨n⟩] = { ⟨value⟩ , … } ;
required array<⟨type⟩> ⟨name⟩ ;
ref array<⟨type⟩> ⟨name⟩ ;
```

`<`, `>`, `[` and `]` are literal. These forms appear in a class or object body.

**Description**

An `array<T>` declared as a class or object member (§8.3.1, §11.8) is its property's data, as Inform 6 has it:
the elements are in the property, so Inform 6 code (the library's `found_in`, an extension walking `obj.&prop`)
reads it directly. `length`, `append()`, `pop()` and the rest behave as for any array, and an emitter on a member
array sees `$self` as the owner and `$prop` as the property (§8.1.1). Every instance of a class has an array of
its own, with the declared capacity and initializer.

`holder.rows = src;` copies into the member, and `d = holder.rows;` copies out of it (§12.5); `array<int> c =
holder.rows;` gives a local a copy of its own. A member array is its owner's data, with no address of its own, so
it can't be passed as an argument: pass its owner, or copy it into an array first.

A member array is declared with a capacity or at least one element. In a class body, a member every
object must give is declared `required` with neither (§8.3.6); one that may stay empty declares room,
`array<int> items[4];`. Otherwise a member array with neither, or with an empty `{}`, is a
compile-time error, except in an `extern` or `emitter` class, whose body describes a layout rather
than storage.

A member may also be declared `ref`: it holds a reference to an array owned elsewhere, is bound with `:=`
(§3.7), and owns no storage. A `ref` member can be passed as an argument.

**Example**

```bgl
#include <array>
class Inventory : object {
    array<object> held[8];                  // per-instance, in the property
}

array<int> shared[8];
object w { ref array<int> log; }

w.log := shared;      // bind
w.log += 5;           // appends to shared
```

**Notes**

> **[Z-machine]** A property holds at most 32 words. A member array larger than that (counting the length
> word) is kept in a table of its own, with the property holding its address; it behaves the same, but Inform 6
> code no longer finds the elements in the property. A `rawArray<T>` member can't exceed the limit (§12.8.3).

## 12.8 `rawArray<T>`

### 12.8.1 Raw Views

**Syntax**

```syntax
rawArray<⟨type⟩> ⟨name⟩
```

`<` and `>` are literal. The form appears as a parameter, an `extern` declaration, or a member.

**Description**

`rawArray<T>` is a typed view over a bare I6 word array: no length header and no tracking.
`rawArray<char>` is the byte form, a view over a bare I6 byte array (`Array X ->`, or an address inside
the input buffer); like `array<char>` (§12.4) it is indexed by byte, from offset 0. It is
declarable at file scope, as an `extern`, as a parameter type, and as a class or object member. Its
purpose is interoperability: an I6 buffer handed to Beguile (the `results` array of a `parse_error`
entry point, a library table, an array declared in an `#i6` island, §15.2) has no count word, and
receiving it as a `rawArray<T>` parameter allows ordinary subscript syntax on it.

| | `array<T>` | `rawArray<T>` |
|---|---|---|
| Layout | an I6 `table`: the count word (the capacity), then the elements; the length and a marker word sit in the two words before it | elements only |
| `size` / `length` | available | unavailable; the length is passed explicitly |
| Length tracking | yes | none |

Because a `rawArray<T>` carries no length, `for … in` over a `rawArray<T>` parameter is a compile-time
error; iteration uses an indexed loop bounded by a length supplied separately. Elements are
type-checked at every subscript and may be cast like any other value.

**Example**

<!-- doctest: skip -->
```bgl
bool ext_parsererror(eParserError etype, rawArray<var> results) {   // PutOn, Insert: library actions
    if (etype == eParserError.nothingToDo && ((verb)results[0] == PutOn || (verb)results[0] == Insert))
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

A file-scope `rawArray<T>` declared with an initializer is emitted as exactly the I6 array its values
make: the elements, with no count word, length or marker word. It has the same elements-only
layout as a `rawArray<T>` parameter, so it is passed wherever a `rawArray<T>` is expected, and its
length is known only to the code that declared it. An I6 API that reads an array by a leading count
word takes an `array<T>`, which is an I6 table whose count word is its capacity.

**Example**

```bgl
array<string>    trk = { "a", "b", "c" };   // tracked
rawArray<string> raw = { "a", "b", "c" };   // Array raw --> "a" "b" "c";
```

### 12.8.3 Member `rawArray<T>`

**Syntax**

```syntax
rawArray<⟨type⟩> ⟨name⟩ = { ⟨value⟩ , … } ;
```

`<` and `>` are literal. The form appears in a class or object body.

**Description**

A `rawArray<T>` member is a bare property array. It is required for members that contribute to an
`additive` property, and its `size`, `length` and permitted operations on such members are
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
`array<T>` and supports `length` and the array surface; `name[i][j]` reads or writes an element,
and `name[i][j].member = v` writes through an object element. `for (array<T> row in name)` iterates
the outer array with `row` typed `array<T>`. A nested array may be declared as a local; its inner
arrays are allocated per call with it (§12.6). A wrong
element type or a wrong nesting depth in an initializer is a compile-time error.

A slot holds a reference to its inner array. `grid[i] = row;` copies `row` into the array the slot holds, as `=`
on any array does (§12.5); appending (`grid += row;`) stores `row` itself in the new slot, and the outer array then
keeps it alive.

**Example**

```bgl
array<array<int>> grid = { {1,2,3}, {4,5} };

int v = grid[0][1];                    // 2
int rows = grid.length;              // 2
int cols = grid[1].length;           // 2
grid[0][1] = 99;
int total = 0;
for (array<int> row in grid) {
    for (int i = 0; i < row.length; i++) { total += row[i]; }
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
which copies the incoming value into the buffer the slot owns. That store is `<array>`'s, so an
element write into an array of an owning type without `#include <array>` is a compile-time error. Passing an already-allocated value
allocates a second time; the original is not released.

**Arrays of a struct.** An array of a struct (§8.2.7) declared with a capacity or a list owns an instance per slot,
as a variable of the struct does. Struct instances are Inform 6 objects, which exist only at compile time, so the
compiler makes them: one set per struct, holding as many instances as the program's arrays of that struct declare
slots, handed to a slot the first time it is stored into or one of its members is reached (`a[0].n = 7` on a slot
never stored into), starting from the struct's defaults. `a[i] = v` and `a += v` copy `v` into the slot's instance
through the struct's copy operator. `remove`, `removeValue`, `-=`, `clear` and a local array's exit give instances
back, so each call of a routine, a recursive one included, has instances of its own; a program that holds more
filled slots at once than it declared runs out, which is a run-time error.

An array of a struct with no capacity of its own (a `<linq>` result, a returned array, a local declared from
another array) holds the instances of the array it came from: changing an element through it changes the source,
and removing one gives nothing back.

- The initial elements of a file-scope or object member array are the slots' instances, as static
  data names an instance (§3.7). An inline object `T{ … }` is an instance of its own; a named
  instance (`T named { … }`) is shared with the array; a global variable of the class (`T v = T{ … };`)
  gives its slot a copy of its initial value.
- A member array of a struct declared in a class can't have initial elements, since every
  instance of the class would share them; that is a compile-time error.
- `pop()`, `popEnd()` and `dequeue()` hand back the slot's instance, which stays the array's.
- An element store into a member array of a class that manages its own storage (`stringObj`) reads
  the slot twice, so its index is a variable or a constant; a computed index is a compile-time error.

**Example**

```bgl
#include <array>
#include <string>
array<stringObj> slots[4];

slots += "alpha";                    // the slot allocates and owns
slots[0] = "replaced";               // reuses slot 0's buffer

stringObj tmp = "beta";              // allocated on declaration (§22.3)
slots += tmp;                        // copies into a new slot allocation; tmp keeps its own buffer,
                                     // released at scope exit, not by the array
```

**See also** §9.6 — `static` operators; §8.5 — the value form of `deinit`; §22.4 — the `<array>` methods that
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

<!-- doctest: skip -->
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
