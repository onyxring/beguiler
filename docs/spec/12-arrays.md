# 12 Arrays

<!-- toc -->
- [12.1 Overview](#121-overview)
- [12.2 Declaring Arrays](#122-declaring-arrays)
- [12.3 Subscripts, Size and Length](#123-subscripts-size-and-length)
- [12.4 Byte Arrays: `array<char>`](#124-byte-arrays-arraychar)
- [12.5 Assignment and Copy Semantics](#125-assignment-and-copy-semantics)
- [12.6 Local Arrays, the Heap and Collection](#126-local-arrays-the-heap-and-collection)
- [12.7 Member Arrays](#127-member-arrays)
- [12.8 `rawArray<T>`](#128-rawarrayt)
  - [12.8.1 Raw Views](#1281-raw-views)
  - [12.8.2 File-scope `rawArray<T>` Literals](#1282-file-scope-rawarrayt-literals)
  - [12.8.3 Member `rawArray<T>`](#1283-member-rawarrayt)
- [12.9 Arrays of Arrays](#129-arrays-of-arrays)
- [12.10 Element Type Requirements](#1210-element-type-requirements)
  - [12.10.1 Arrays of Structs](#12101-arrays-of-structs)
- [12.11 `extend` for Arrays](#1211-extend-for-arrays)
- [12.12 Emitters on Arrays](#1212-emitters-on-arrays)
<!-- /toc -->

## 12.1 Overview

`array<T>` is a typed word array with a capacity fixed at declaration and a length tracked at run time.  The element type `T` is mandatory; a bare `array` is not a type.  `T` can be any base type (`int`, `bool`, `string`, `object`, `char`, `dictionaryWord`), any user-defined class, or another array type (§12.9).  `rawArray<T>` (§12.8) is the untracked form used at the I6 boundary.

Subscripting, `size`, `length`, assignment (which copies, §12.5), and `for … in` are built in.  Assigning `length`, `clear()`, and the remaining methods (`append`, `indexOf`, `sort`, …) come from the `<array>` extension, which you enable with `#include <array>`; they are cataloged in §22.4.  A program which doesn't include it carries none of its code.

## 12.2 Declaring Arrays

**Syntax**

```syntax
array<TYPE> NAME[N] ;
array<TYPE> NAME = { VALUE , … } ;
array<TYPE> NAME = VALUE ;
array<TYPE> NAME[N] = { VALUE , … } ;
```

`<`, `>`, `[`, and `]` are literal.  The first form is a sized array:  capacity `N`, zero-initialized, length 0.  `N` is any compile-time integer:  an integer literal, a `#define`d symbol whose value is an integer (§14.2.1), a `const int` initialized with an integer literal (§3.4), or an integer `#beguilerSettings` property (§15.7).  The second is an initialized array, with capacity and length equal to the number of values.

The third is the one-element form of the second:  a single value of the element type needs no braces, so `array<int> x = 3;` is `array<int> x = {3};`.  A value which is itself an array initializes `NAME` from that array instead; the value's type decides which is meant.  A local declared from a call's or a chain's result takes that array; one declared from another array gets a copy of its own, with that array's capacity; a sized declaration (`array<int> x[N] = src;`) copies into its declared capacity (§12.5, §12.6).  These forms are for locals:  a file-scope or member array declared from another array or a run-time value is a compile-time error; copy into it inside a routine instead.  A local `array<char>` declared from another needs a capacity unless its source is a file-scope array, whose capacity it takes.

The fourth combines the first two:  capacity `N`, length the number of values, the remaining slots zeroed.  A list longer than `N` is a compile-time error.

An array always has storage:  a capacity of at least 1, or at least one element.  A global or local array declared with neither (`array<int> g;`), with an empty `{}`, or with a capacity of 0 is a compile-time error.  To share an array which lives elsewhere, declare a `ref` instead (§3.7).

Arrays can be declared at file scope, as locals inside a function body (§12.6), and as class or object members (§12.7).  A global array has no element-count limit.  Element types are checked at every subscript site and in initializers, so a cross-type assignment (e.g., a `string` into an `array<int>`) is a compile-time error.

**Example**

```bgl
array<int> scores[5];
array<int> primes = {2, 3, 5, 7, 11};
array<Room> visited[20];
```

## 12.3 Subscripts, Size and Length

**Syntax**

```syntax
ARRAY[INDEX]
ARRAY[INDEX] = VALUE
ARRAY . size
ARRAY . length
ARRAY . length = N
ARRAY . clear ( )
```

`[` and `]` are literal.

Subscripts are zero-based.  `size` is the number of elements allocated for the array, regardless of whether those slots hold meaningful values.  It is read-only.

Every subscript of an `array<T>` is checked against its `size`, for reads and writes alike.  A constant index outside `0` to `size - 1` of an array whose size is declared is a compile-time error; any other out-of-range index will print a run-time message, and the read (which yields `0`) or the write is skipped.  A subscript never consults or changes the length.  `rawArray<T>` subscripts are not checked (§12.8).

An array also carries an explicit *length*, the count of in-use entries.  Length is set at allocation (N for an initializer list, 0 for a sized array) and changes only through explicit operations:  assigning `length`, `clear()`, and the mutators of `<array>` (§22.4).  A slot write (`arr[i] = v`) does not change length; effectively, the array behaves as a buffer with a cursor.  An assigned `length` is limited to `size`, and a negative one is a run-time error; `clear()` zeroes every slot up to `size` and resets length to 0.

Every traversal in `<array>` walks the in-use range only, so a sized array filled only by slot writes reads as empty; use `+=`, `insert()`, or assign `length` to make the slots live.

On an `array<char>` (§12.4), `size` is its capacity, and `length` starts at the declared capacity (with `<buf>`, a sized declaration starts empty); assigning its `length` needs `<buf>` (§22.4).  On a `rawArray<T>` (§12.8) `size`, `length`, and `for … in` are compile-time errors, except that a raw member array reports its property length from both.  On an untracked `extern` array, `length` reads as `size`, and assigning it has no effect.  `isTracked()` (§22.4) tells a tracked array from an untracked one.

**Example**

```bgl
#include <array>

// … in a routine:
array<int> scores[5];

int x = scores[2];
scores[0] = 99;
int n = scores.size;            // -> 5
int used = scores.length;       // -> 0: a slot write does not change length
scores.length = 1;              // -> length is now 1
scores.clear();                 // every slot 0, length 0
```

> ***[Z-machine]:** `length` accepts 0..32767, and no more than `size`.*

> ***[Glulx]:** `length` accepts 0..2^31-1, and no more than `size`.*

## 12.4 Byte Arrays: `array<char>`

**Syntax**

```syntax
array<char> NAME[N] ;
array<char> NAME = { VALUE , … } ;
array<char> NAME = "TEXT" ;
```

`<`, `>`, `[`, and `]` are literal.

`array<char>` is a byte array:  its elements are stored and accessed as bytes.  All other element types are word arrays.  A byte array holds character or small-integer data; its initializer and element writes accept both character literals and integers (`{'H', 'i'}`, `{5, 10, 15}`, `bytes[i] = 99`).  An integer literal outside 0..255 is a compile-time error; a non-literal `int` which exceeds a byte will wrap to its low byte at run time.  A string initializer fills the array with the characters of the text.

Reading an element yields a `char`, which widens to `int` freely.  A `char` result is byte-wide, so accumulate into an `int`.  Printing a `char` prints a character; to print its numeric value, simply read it into an `int` first.

With `<buf>` (which `<string>` includes), a sized `array<char>` starts empty, and a slot write doesn't change its length:  after filling one by index, assign its `length` (§12.3, §22.2).  `clear()` and the other `<array>` methods don't apply to an `array<char>`, whose operations are `<buf>`'s (§22.2).  An `array<char>` can't be passed to a `rawArray<char>` parameter (§12.8).

**Example**

```bgl
array<char> bytes = {'H', 'i', 5, 10};
array<char> word  = "hello";

bytes[2] = 99;                  // an integer into a byte slot
int big = 300;
bytes[3] = big;                 // -> 44: wraps to the low byte at run time
int total = bytes[2] + bytes[3];
print(word[0]);                 // -> h
int code = word[0];
print(code);                    // -> 104
```

## 12.5 Assignment and Copy Semantics

**Syntax**

```syntax
DESTINATION = SOURCE ;
REF-DESTINATION := SOURCE ;
```

An array owns its storage, just as an object with a copy operator owns its instance (§2.10.3):  `dst = src` copies `src`'s elements and length into `dst`'s storage, up to `dst`'s capacity, and the two stay independent.  `dst.copyFrom(src)` from `<array>` does the same.  A source which nothing else holds (a chain's result or a call's returned array) is freed once it is copied.

Only a `ref array` (§3.7) re-points:  `r := a` makes `r` refer to `a`'s array, and `=` through `r` copies into the array it refers to.  Parameters and returns share:  a function's array parameter refers to the caller's array, and a returned array passes to the caller (§12.6).  `a == b` is true when they are the same array.

**Example**

```bgl
#include <array>

// … in a routine:
array<int> src = {1, 2, 3};
array<int> dst[3];

dst = src;                      // dst has its own copy
dst[0] = 99;                    // src[0] is still 1
ref array<int> same := src;     // same refers to src's array
same[0] = 5;                    // src[0] is now 5
```

## 12.6 Local Arrays, the Heap and Collection

A local array (declared inside a function body, sized or initialized) takes its storage from the **heap** when its routine is entered, one array per call, so each call (a recursive one included) has its own.  A local array declared in a loop body is that one array, emptied each time its declaration runs.  File-scope and member arrays live in permanent storage and are never freed.

A local `array<char>` is the exception:  it lives in its routine's frame (§18.10), so returning one is a compile-time error; return a file-scope `array<char>` or a `stringObj` instead.

A heap array lives as long as something refers to it, as in C# and TypeScript.  `<linq>` results (§22.5) are heap arrays too.

Arrays are freed in three ways:

- **At the end of the statement.**  A call's or a chain's result which nothing takes is freed once the statement which used it is done:  `f()[0]`, `f().length`, `a.filter(…) == b`.  A result is taken by a local (an array, `ref`, `var`, or union local), a parameter, a `for … in` (for the loop), anything which outlives the routine, or a pin.  An Inform 6 routine given a result takes nothing; pin the array if the I6 code keeps it.  A function whose every `return` hands back a file-scope array returns that array itself, so there is nothing to free or own.
- **At the routine's exit.**  A local array which no reference has left is freed as its routine returns, and so is an array a local took from a call or a chain, and the rows of a local array of arrays which its own `{ … }` initializer made and nothing else took.  Rows it gained later are left to the collector.  A returned array passes to the caller instead.  A routine containing an `#i6` block leaves its local arrays to the collector.
- **By the collector.**  The rest are freed by a mark-and-sweep collector when nothing refers to them.  It runs at the prompt, through the library binding (§23), and acts only once a quarter of the heap has been allocated since it last ran.

The collector finds references from declared types.  Its roots are the globals and properties whose type can hold an array (an array type, `var`, or a union with one of them), the elements of file-scope and member arrays of such a type, the local arrays and struct variables of routines still running, and the arrays pinned with `bgl.gc.keep` (§21.15).  It follows them through the arrays they reach.  A reference it can't see (i.e., one held only by Inform 6 code or in a local variable) doesn't keep an array; pin the array instead.  An Inform 6 global declared to Beguile with an array type (`extern array<int> held;`) is typed, so it is a root.

The heap's size is the `heapSize` setting (§15.4).  When it runs out, the collector runs at once and the allocation is tried again; running out after that is a run-time error which names the setting.  A debug build's `heap` verb reports the figures for setting it (§20.3).

`bgl.gc` (§21.15) frees one array at once (`bgl.gc.free(a)`), runs a collection (`bgl.gc.collect()`), and reports the heap's figures.  A program which never reaches a prompt, or a library with a turn loop of its own, calls `bgl.gc.collect()` at a point where no array it still needs is held only in a local.

**Example**

```bgl
array<int> build() {
    array<int> tmp[3];
    tmp[0] = 1; tmp[1] = 2; tmp[2] = 3;
    return tmp;                 // the caller gets this array
}

array<int> keep = build();      // keep takes build's array
int n = keep[0];                // -> 1
```

> ***Note:** I6 code which keeps an array it was given, in a global of its own or an `#i6` block's variable, needs one that lives on:  pin it with `bgl.gc.keep(a)` first (§21.15), and `bgl.gc.unpin(a)` once the I6 code lets go, or pass a file-scope array.  An array which Inform 6 code holds without `bgl.gc.keep` is freed at the next collection, and a later use of it will read freed memory.*

> ***Note:** An array of a struct declared with a capacity or a list owns its slots' instances (§12.10.1), and freeing the array releases them.  A function can return such an array; the instances go with it.  A view of a routine's own array of structs can't be returned (§12.10.1).*

## 12.7 Member Arrays

**Syntax**

```syntax
array<TYPE> NAME[N] ;
array<TYPE> NAME = { VALUE , … } ;
array<TYPE> NAME[N] = { VALUE , … } ;
required array<TYPE> NAME ;
ref array<TYPE> NAME ;
```

`<`, `>`, `[`, and `]` are literal.  These forms appear in a class or object body.

An `array<T>` declared as a class or object member (§8.3.1, §11.8) is its property's data, as Inform 6 has it:  the elements are in the property, so Inform 6 code reads it directly.  An emitter on a member array sees `$self` as the owner and `$prop` as the property (§12.12).  Every instance of a class has an array of its own, with the declared capacity and initializer.

A member array is its owner's data, with no address of its own, so it can't be passed as an argument or returned; pass its owner, or copy it into an array first.

A member array is declared with a capacity or at least one element.  In a class body, a member which every object must give is declared `required` with neither (§8.3.6); one which can stay empty declares room, `array<int> items[4];`.  Otherwise a member array with neither, or with an empty `{}`, is a compile-time error, except in an `extern` or `emitter` class, whose body describes a layout rather than storage.

Alternatively, a member can be declared `ref`:  it holds a reference to an array owned elsewhere, is bound with `:=` (§3.7), and owns no storage.  A `ref` member can be passed as an argument.

An element store into a member array of a class which manages its own storage (`stringObj`) reads the slot twice, so its index is a variable or a constant; a computed index is a compile-time error.

**Example**

```bgl
#include <array>
class Inventory : object {
    array<object> held[8];                  // per-instance, in the property
}

array<int> shared[8];
object w { ref array<int> log; }

// … in a routine:
w.log := shared;      // bind
w.log += 5;           // appends to shared
```

> ***[Z-machine]:** A property holds at most 32 words.  A member array larger than that (counting the length word) is kept in a table of its own, with the property holding its address; it behaves the same, although Inform 6 code will no longer find the elements in the property, and an emitter sees it as a global array (`$self` the table, `$prop` `0`, §12.12).  A `rawArray<T>` member can't exceed the limit (§12.8.3).*

## 12.8 `rawArray<T>`

### 12.8.1 Raw Views

**Syntax**

```syntax
rawArray<TYPE> NAME
```

`<` and `>` are literal.  The form appears as a parameter, an `extern` declaration, or a member.

`rawArray<T>` is a typed view over a bare I6 word array:  no length header and no tracking.  `rawArray<char>` is the byte form, a view over a bare I6 byte array (`Array X ->`, or an address inside the input buffer); like `array<char>` (§12.4) it is indexed by byte, from offset 0.  It is for interoperability:  an I6 buffer handed to Beguile (e.g., the `results` array of a `parse_error` entry point) has no count word, and receiving it as a `rawArray<T>` parameter lets you use ordinary subscript syntax on it.  An I6 byte array or buffer reached by name is declared the same way, `extern rawArray<char>`.

| | `array<T>` | `rawArray<T>` |
|---|---|---|
| Layout | an I6 `table`: the count word (the capacity), then the elements; the length and a marker word sit in the two words before it | elements only |
| `size` / `length` | available | unavailable; the length is passed explicitly |
| Length tracking | yes | none |

Since a `rawArray<T>` carries no length, `for … in` over a `rawArray<T>` parameter is a compile-time error; iteration uses an indexed loop bounded by a length supplied separately.  Elements are type-checked at every subscript and can be cast like any other value.

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
rawArray<TYPE> NAME = { VALUE , … } ;
```

`<` and `>` are literal.

A file-scope `rawArray<T>` declared with an initializer is emitted as exactly the I6 array its values make:  the elements, with no count word, length, or marker word.  It has the same elements-only layout as a `rawArray<T>` parameter, so it can be passed wherever a `rawArray<T>` is expected, and its length is known only to the code which declared it.  An I6 API which reads an array by a leading count word takes an `array<T>`.

**Example**

```bgl
array<string>    trk = { "a", "b", "c" };   // tracked
rawArray<string> raw = { "a", "b", "c" };   // Array raw --> "a" "b" "c";
```

### 12.8.3 Member `rawArray<T>`

**Syntax**

```syntax
rawArray<TYPE> NAME = { VALUE , … } ;
```

`<` and `>` are literal.  The form appears in a class or object body.

A `rawArray<T>` member is a bare property array.  It is required for members which contribute to an `additive` property; its `size`, `length`, and allowed operations on such members are covered in §11.7.2.

**Example**

```bgl
class Room { rawArray<dictionaryWord> name = {.box, .crate}; }
```

## 12.9 Arrays of Arrays

**Syntax**

```syntax
array<array<TYPE>> NAME = { { VALUE , … } , … } ;
NAME[I]
NAME[I][J]
```

`<`, `>`, `[`, and `]` are literal.  `NAME[I]` is an `array<TYPE>`; `NAME[I][J]` is an element of `TYPE`.

The element type of an array can itself be an array type, two deep at most:  `array<array<T>>`.  A third level is a compile-time error; keep it in a class member instead.  A struct can't hold an array of arrays either; however, a struct member can be a `ref` to one kept elsewhere.

A nested initializer supplies one braced list per inner array, and the inner arrays can differ in length.  The inner arrays of an `array<array<char>>` are byte arrays (§12.4); each row is written as a braced list (`{'o','n','e'}`), since a string literal is not accepted as a row.  A nested array can be declared as a local; its inner arrays are allocated per call with it (§12.6).  A wrong element type or a wrong nesting depth in an initializer is a compile-time error.

A slot holds a reference to its inner array.  `grid[i] = row;` copies `row` into the array the slot holds, as `=` on any array does (§12.5).  Appending (`grid += row;`) stores `row` itself in the new slot, and the outer array then keeps it alive.  A member array has no address of its own, so storing one as a row (or in a `var`) stores a copy.  A whole `grid = other;` treats each row as an element:  a row `grid` already holds is copied into, and a slot past `grid`'s length takes a copy of `other`'s row, so the two stay independent.

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

`array<T>` can't see the element type at run time.  It asks `T` for four operations, each supplied as an operator reference (§7.3.1), and every one is optional.  A type which publishes nothing gets plain word semantics.

| `T` publishes | Used by | Absent |
|---|---|---|
| `operator ==` (static or instance) | `indexOf`, `find`, `contains`, `removeValue`, `-=` | word comparison (identity) |
| `operator <=>` (static or instance) | `sort()` | signed word ordering, and a compile-time warning for a class (ordering by object address) |
| `static operator =` | `[i] =`, `append`, `prepend`, `insert` | raw word store |
| `static deinit(T)` | `remove`, `removeValue`, `-=`, `clear`, local scope exit | nothing is released |

For `int`, `char`, `bool`, and `object` the defaults are correct.  The default is silently wrong for a type whose value semantics differ from its word, such as a content-comparing type (`string` and `stringObj` publish `operator ==`), a type which owns storage, or `float`, which is sign-magnitude and publishes `operator <=>` to sort negatives correctly.

**Static versus instance.**  `==` and `<=>` can be either form; the instance form is sent to the left operand.  A bare-word element type (`string`, `float`) has no object to receive a message and must publish the static form; only an object-backed element type can use the instance form.  `operator =` must be `static`:  on a first write the slot holds 0, so there is no receiver.  An instance `operator =` is ignored and the store is a word write.  `deinit` has both forms and they do different jobs:  `emitter deinit()` releases a receiver at scope exit, while `static deinit(T v)` releases a slot the container has no receiver for.  A type which owns storage generally wants both (§8.5).

**Assignment copies.**  Every store into an owning element type goes through `static operator =`, which copies the incoming value into the buffer the slot owns.  That store is `<array>`'s, so an element write into an array of an owning type without `#include <array>` is a compile-time error.  Passing an already-allocated value allocates a second time; the original is not released.

A method of `array<T>`, its own or one added by `extend class array` (§12.12), which stores an element marks its parameter `literal T`:  `emitter void push(literal T item)`.  The mark applies only when the receiver's element type is `literal` (§3.13).

**Example**

```bgl
#include <array>
#include <string>

// … in a routine:
array<stringObj> slots[4];

slots += "alpha";                    // the slot allocates and owns
slots[0] = "replaced";               // reuses slot 0's buffer

stringObj tmp = "beta";              // allocated on declaration (§22.3)
slots += tmp;                        // copies into a new slot allocation; tmp keeps its own buffer,
                                     // released at scope exit, not by the array
```

**See also** §9.6 (`static` operators); §8.5 (the value form of `deinit`); §22.4 (the `<array>` methods which consult these operators).

### 12.10.1 Arrays of Structs

An array of a struct (§8.2.7) declared with a capacity or a list owns an instance per slot, as a variable of the struct does.  The instances are made at compile time:  one set per struct, holding as many instances as the program's arrays of that struct declare slots, each handed to a slot the first time it is stored into or one of its members is reached, starting from the struct's defaults.  `a[i] = v` and `a += v` copy `v` into the slot's instance through the struct's copy operator.  `remove`, `removeValue`, `-=`, `clear`, and a local array's exit give instances back, so each call of a routine (a recursive one included) has instances of its own.  A program which holds more filled slots at once than it declared will run out:  the collector runs first, giving back the instances of arrays nothing refers to any more, and only then is it a run-time error.

An array of a struct with no capacity of its own (a `<linq>` result, a returned array, a local declared from another array) holds the instances of the array it came from:  changing an element through it changes the source, and removing one gives nothing back.  It has no instances to copy a new element into, so adding one (`+=`, `append`, `insert`, `push`, a store past its length) is a run-time error; declare the array with a capacity to grow it:

```bgl
#include <linq>
struct Coin { int value; }

// … in a routine:
array<Coin> purse = { Coin{ value = 5; }, Coin{ value = 20; }, Coin{ value = 10; } };
Coin spare;
array<Coin> big = purse.filter((Coin c) => c.value >= 10);       // shares purse's coins
big += spare;                                                     // run-time error
array<Coin> mine[8] = purse.filter((Coin c) => c.value >= 10);   // eight coins of its own, two copied in
mine += spare;                                                    // copied into the third
```

Assigning a result into an array with a capacity of its own (`kept = hand.filter(…);`) copies the elements into that array's instances.

Such a view of a routine's own array of structs lives no longer than that routine, since the instances go back when it returns.  Returning one, binding a global or member to one with `:=`, or keeping one in a global or member `var` is a compile-time error; copy it into an array with a capacity of its own first.  A view pinned for Inform 6 code (`bgl.gc.keep`) is the program's to keep from outliving its source.

- The initial elements of a file-scope or object member array are the slots' instances, as static data names an instance (§3.7).  An inline object `T{ … }` is an instance of its own; a named instance (`T named { … }`) is shared with the array; a global variable of the class (`T v = T{ … };` or `T v;`) gives its slot a copy of its initial value.
- A member array of a struct declared in a class can't have initial elements; that is a compile-time error.
- `pop()`, `popEnd()`, and `dequeue()` release the slot's instance, as `remove` does, and return it.  It stays readable until the instance is handed out again, so copy what you need before adding to an array of that struct.  An element stored by reference (an instance of an ordinary class) is the caller's once it is returned:  its `static deinit` doesn't run, as it does for `remove`.
- Reordering an array of structs into itself (`trail = trail.orderBy(…);`) moves the instances between slots:  a `ref` to an element follows that element to its new place.
- A view is valid only while its source is unchanged:  once the source removes or clears an element, the view may still show that instance, which another array can then be given.

## 12.11 `extend` for Arrays

**Syntax**

```syntax
extend ARRAY {
    inject ELEMENT [ after REF | before REF | first | last ] ;
    remove REF ;
    move   REF [ after REF | before REF | first | last ] ;
}
```

`REF` is an element name or a zero-based index written `[N]`, where `[` and `]` are literal.

`extend arrayName { … }` edits the initializer of a previously declared array at compile time, from a later file or a later point in the same file.  Run-time mutation uses the array's methods (§22.4).  The body holds *array extension statements*:  `inject`, `remove`, and `move`.

- `inject element [position]` splices an element in.  The element is a named object, a literal, or an inline object `Type{ … }` (§11.3); when the element type is an object-backed class the type can be omitted (`inject { … } last;`).  With no position clause the element is appended.
- `remove X` removes an element.
- `move X [position]` repositions an existing element (remove, then inject at the new position).

`after X` and `before X` place the element immediately next to an existing element; `first` and `last` name the two ends.  An element name in a reference is the identifier of the object an element refers to (`after [0]` and `remove [2]` use an index instead).

The statements apply in source order, so a later `move` or `remove` will see the effect of earlier ones, and `[N]` indices are relative to the array's state at that point.  An unknown name or an out-of-range `[N]` is a compile-time error.

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

**See also** §11.10 (`extend` for objects); §13.5 (`extend` for verb grammar); §8.7.1 (`extend class`).

## 12.12 Emitters on Arrays

An `extend class array` block adds emitters to every array (§8.1.1).  A body is raw I6, so it works with the array's own layout.  For a global or local array, `$self` is the array, element `i` is `$self-->(i+1)`, and `$prop` is `0` (§7.3).  A member array is its property's data (§12.7):  `$self` is the owning object, `$prop` the property, and element `i` is `$self.&$prop-->i`.  Alternatively, a read can be written `$i6Expr($self[i])` (§7.3.4), which emits the right form for either.
