# 22 Language Extensions

<!-- toc -->
- [22.1 Overview](#221-overview)
- [22.2 `<buf>`](#222-buf)
- [22.3 `<string>`](#223-string)
- [22.4 `<array>`](#224-array)
- [22.5 `<linq>`](#225-linq)
- [22.6 `<ui>`](#226-ui)
- [22.7 `<glulxWindow>`](#227-glulxwindow)
  - [22.7.1 Window Types](#2271-window-types)
  - [22.7.2 Roots](#2272-roots)
  - [22.7.3 Splitting](#2273-splitting)
  - [22.7.4 Sizing and Re-arrangement](#2274-sizing-and-re-arrangement)
  - [22.7.5 Images](#2275-images)
  - [22.7.6 Cursor and Lifecycle](#2276-cursor-and-lifecycle)
  - [22.7.7 Styles](#2277-styles)
  - [22.7.8 Colors](#2278-colors)
  - [22.7.9 Style Validation](#2279-style-validation)
  - [22.7.10 Enums](#22710-enums)
  - [22.7.11 Window Output](#22711-window-output)
  - [22.7.12 Capabilities](#22712-capabilities)
- [22.8 `<glulxImage>`](#228-glulximage)
- [22.9 Library-specific Extensions](#229-library-specific-extensions)
<!-- /toc -->

## 22.1 Overview

An *extension* is a file in the `beguiLib` folder which your program enables with `#include <name>` (§14.1.1).  Extensions build on the runtime core (§21) and are library-agnostic:  each works with any IF library binding, or with none.  The exceptions are the library-specific extensions (§22.9), which work only with the library they name.  Nothing in an extension is available until you include it, and a program which doesn't include one carries none of its code.

This chapter covers the following extensions:

| Extension | Include | Adds | Requires `bglInit()` | Also includes | Target |
|---|---|---|---|---|---|
| Tracked character buffers | `#include <buf>` | `array<char>` length tracking, `bgl.util.buf` | yes (length headers of sized arrays) | - | both |
| Strings | `#include <string>` | the `stringObj` type; text methods on `string` | yes | `<buf>` | both |
| Arrays | `#include <array>` | search, mutation, deque, and sort methods on `array<T>`; `copyFrom` | no | - | both |
| LINQ chains | `#include <linq>` | fluent chain operations on `array<T>` | no | `<array>` | both |
| Key input | `#include <ui>` | `bgl.ui.waitForKey()`, `hideCursor()`, `showCursor()` | no | - | both |
| Glulx windows | `#include <glulxWindow>` | window types, splitting, sizing, styles, colors | no | `<glulxImage>` when `generateBlorb` is true | Glulx |
| Glulx images | `#include <glulxImage>` | the `glulxImage` handle; `eImages` metadata | no | - | Glulx |

Including an extension more than once is harmless.  The names `uint`, the `char` utilities, `bgl.util.math`, `bgl.util.random`, `bglAllocated`, and `bgl.world` are part of the core and need no include (§21).

Every entry below has the same shape:  purpose, include line, what it adds, the settings it reads, and notes.

## 22.2 `<buf>`

**Purpose.**  Length-tracked character buffers.  With `<buf>` included, a sized `array<char>` is a *tracked buf*:  it records its capacity and current length, and the buffer operations below apply to it.

**Include**

```bgl
#include <buf>
```

A tracked buf is laid out as a standard I6 hybrid buffer (the length word first, then the characters), so you can pass it directly to an I6 routine which expects one.  `buf[i]` reads and writes character `i` (§12.4), and doesn't change the length.

An untracked `array<char>` (one created without the tracked layout) answers `size` and `length` from the buffer's length word (§12.4), and `isTracked()` returns false.  An array which Inform 6 declares, such as the standard library's input `buffer`, is declared `extern rawArray<char>` (§12.8), not `array<char>`.

A buf, and the `stringObj` built on one (§22.3), holds one byte per character, so it holds only the characters of Latin-1 (codes 0 to 255).  This is true on Glulx too, where string literals and `char` values can be any Unicode character (§D.5):  a character beyond Latin-1 captured or appended into a buf will be stored as `?`.  An `array<char>` initialized from such text is a compile-time error.

**Methods on `array<char>`**

| Method | Returns | Description |
|---|---|---|
| `buf.size` | `int` | Capacity in characters; an untracked buffer's is its length word, and `-1` for `null`. |
| `buf.length` | `int` | Current number of characters. |
| `buf.length = n` | | Set the current length, limited to `size` on a tracked buffer. |
| `buf.isTracked()` | `bool` | True for a tracked buf. |
| `buf.copyFrom(other)` | | Copy `other`'s characters and length, up to `size`; an untracked buffer takes no more than its current length. |

**Buffer operations, `bgl.util.buf`**

Operations which return `array<char>` return the buffer they modified.

| Operation | Returns | Description |
|---|---|---|
| `set(buf, value)` | `array<char>` | Replace the contents with a string literal or another buffer; sets the length. |
| `copy(to, from, n[, toPos[, fromPos]])` | `array<char>` | Copy `n` characters (`-1`: to the end of `from`) from `from[fromPos]` to `to[toPos]`; bounded by both buffers. |
| `append(to, from)` / `prepend(to, from)` | `array<char>` | Add `from`'s contents at the end / start of `to`; `from` is a buffer or a string (a literal included). |
| `insert(to, from, pos[, count])` | `array<char>` | Insert `from` (or its first `count` characters) at `pos`. |
| `delete(buf, pos, count)` | `array<char>` | Remove `count` characters at `pos`, closing the gap. |
| `mid(to, from, fromPos, n)` / `left(to, from, n)` / `right(to, from, n)` | `array<char>` | Copy a substring of `from` into `to`; `n == -1` in `mid` means to the end, and a position or count outside `from` is clamped to it. |
| `indexOf(buf, search[, start])` | `int` | Position of the first occurrence at or after `start`, or `-1`. |
| `indexOfFirstTrue(buf, pred[, start])` / `indexOfFirstFalse` | `int` | Position of the first character for which `pred(c)` is true / false, or `-1`.  `pred` is `func<bool, char>`. |
| `replace(buf, search, repl)` / `replaceAll(buf, search, repl[, start])` | `array<char>` | Replace the first / every occurrence in place. |
| `equals(a, b[, caseInsensitive])` | `bool` | Content equality. |
| `compare(a, b[, caseInsensitive])` | `int` | `-1`, `0`, or `1`, by character code; a shorter buffer which matches sorts first. |
| `startsWith(buf, prefix[, caseInsensitive])` / `endsWith(buf, suffix[, caseInsensitive])` | `bool` | Prefix / suffix test. |
| `toUpper(buf)` / `toLower(buf)` / `reverse(buf)` | `array<char>` | In-place transforms. |
| `trim(buf)` / `trimLeft(buf)` / `trimRight(buf)` | `array<char>` | Remove leading and/or trailing spaces in place. |
| `getChar(buf, pos)` / `setChar(buf, pos, c)` | `int` / `void` | Bounds-checked character access; `getChar` returns `-1` out of range. |
| `print(buf[, len])` | `array<char>` | Print the first `len` characters, or all when `len` is omitted or `-1`. |
| `capture(buf[, maxBytes])` | `void` | Redirect subsequent `print` output into `buf`.  Captures nest to a depth of 16. |
| `release()` | `array<char>` | End the most recent capture, restore the previous output target, and set the buffer's length to what was written. |
| `stackLen()` | `int` | The current capture depth. |
| `isTracked(buf)` / `size(buf)` / `length(buf)` / `setLength(buf, n)` | | The function forms of the methods above. |

**Example**

```bgl
#include <buf>
array<char> line[64];
array<char> word[16];

void Main() {
    bglInit();
    bgl.util.buf.set(line, "hello");
    bgl.util.buf.set(word, " world");
    bgl.util.buf.append(line, word);   // a buffer
    bgl.util.buf.append(line, "!");    // or a string
    bgl.util.buf.toUpper(line);
    bgl.util.buf.print(line);          // -> HELLO WORLD!
    print(line.length);                // -> 12
}
```

**Settings**

`bglStringDefaultSize` (default `500`) is the capacity used for capture into an untracked buffer; `<string>` doesn't use it.  It is an I6 constant; since `<buf>` is processed before your code, set it in an `#emitfirst` block (§14.4.2):

```bgl
#emitfirst { Constant bglStringDefaultSize 800; }
```

> ***Note:** `<buf>` requires `bglInit()` (§21.2).  A local sized buffer is tracked from its declaration until its routine returns.  The registry of tracked buffers has room for the `<string>` pool's buffers plus `bglTrackedBufferReserve` more (default `116`; an I6 constant set before the include, like `bglStringPoolReserve`).*

> ***[Glulx]:** `<buf>` defines `filemode_Write` and `gg_arguments` when no IF library does.*

**See also** §12.4, §22.3.

## 22.3 `<string>`

**Purpose.**  Text you can build and compare:  a second type, `stringObj`, which owns a mutable buffer and compares by content, and text methods and ordering on `string`.

**Include**

```bgl
#include <string>        // includes <buf>
```

The extension has two types, for two different things:

| Type | The slot holds | Mutable | Lifecycle |
|---|---|---|---|
| `string` | a reference to static text (a literal) | no | none; nothing is owned |
| `stringObj` | a buffer of its own, a block in the string heap | yes | allocated on declaration, released at scope exit |

A string literal is a `string`.  The two types mix freely in expressions, and every operation which *produces* text returns a `stringObj`.

Assigning a `stringObj` to a `string` is a compile-time error, and so is passing a `stringObj` where a `string` parameter is expected:  the parameter takes `stringObj`, or `string | stringObj` for either.

`stringObj a = b;` copies `b`'s value; `ref stringObj a := b;` binds a reference (§3.7).  A `stringObj` parameter is a copy too (§8.5); a `ref stringObj` parameter shares the caller's.  Results can be passed straight into a call or an operator, several at once (`join(s.left(3), s.right(4))`, `a.left(1) + b.right(1)`):  each is held until the call returns (§8.5.1).  A `stringObj` global is allocated at startup.

A `stringObj` which a string operation returns is *ephemeral*:  its block has already been released when the caller receives it (§8.5), and its text stays intact only until the next string is allocated.  Used at once, as an operand or an argument, it is held for that call as above.  To keep it any longer, assign it to a `stringObj`, which copies it.

`==` and `!=` between two `string`s (literals included) compare addresses, as they do without the extension (§2.2):  two equal literals are different strings.  `a.equals(b)` compares their text.  When either side is a `stringObj`, `==` and `!=` compare text.

`s == null` and `s != null` remain identity tests on both types (there is no content to compare against `null`).

**Operators (both types unless noted)**

| Operator | Description |
|---|---|
| `s = "text"` / `s = other` | Assign.  On a `string` the slot now refers to the text; on a `stringObj` the text is copied into its buffer.  `other` is a literal, a `string` or a `stringObj`; an `array<char>` is not (append it instead, `s = s.append(buf)`).  `stringObj` also accepts an interpolated string, capturing its output. |
| `s + v` | Concatenation; `s` and `v` are each a literal, a `string`, or a `stringObj`, in either order (a `stringObj` also takes a `char` on either side: `s + '!'`, `'#' + s`).  Returns a new `stringObj`; neither operand is changed. |
| `s += v` | **`stringObj` only.**  Append in place.  `v` can also be an interpolated string, whose output is appended (`s += $" {n} left";`). |
| `s == v` `s != v` | Content comparison when either side is a `stringObj`; between two `string`s, address comparison. |
| `s < v` `s <= v` `s > v` `s >= v` | Lexicographic ordering by character code. |
| `s =~ v` | Content comparison ignoring case. |
| `s <=> v` | Three-way comparison: negative, `0`, or positive as `s` sorts before, with, or after `v` (§4.6). |
| `switch (s) { case "a": … }` | Content comparison. |
| `s[i]` | The character at position `i`. |
| `s[i] = c` | **`stringObj` only.**  Replace the character at position `i`. |
| `s?` | True when the slot is not `null` (§4.10). |

**Methods (both types)**

A method's string argument can be a literal, a `string`, a `stringObj`, or an `array<char>` (`append`, `prepend`, `contains`, `indexOf`, `startsWith`, `endsWith`, `compareTo`).  `append`, `prepend`, `contains`, `indexOf`, `startsWith`, `endsWith`, `insert`, `replace`, `replaceAll` (its search text), `==`, and `!=` also take a `char`, which stands for the one-character string.

| Method | Returns | Description |
|---|---|---|
| `s.print()` | `void` | Print the text. |
| `s.append(v)` | `stringObj` | `s` followed by a string or a `char`. |
| `s.prepend(v)` | `stringObj` | A string or a `char` followed by `s`. |
| `s.toUpper()` / `s.toLower()` | `stringObj` | Case conversion. |
| `s.trim()` / `s.trimLeft()` / `s.trimRight()` | `stringObj` | Without leading and/or trailing spaces. |
| `s.reverse()` | `stringObj` | Characters in reverse order. |
| `s.mid(start, count)` / `s.left(count)` / `s.right(count)` | `stringObj` | Substrings.  A position or count outside the text is clamped to it: `left(-1)` is empty and `left(99)` is the whole text. |
| `s.insert(pos, src)` | `stringObj` | With `src` inserted at `pos` (a position past the end appends). |
| `s.delete(pos, count)` | `stringObj` | With `count` characters removed at `pos`. |
| `s.replace(search, repl)` / `s.replaceAll(search, repl)` | `stringObj` | With the first / every occurrence replaced. |
| `s.format(pattern[, p1[, p2]])` | `stringObj` | `pattern` with `$0` replaced by `s` and `$1`, `$2` by the arguments, each a `string` or `stringObj`. |
| `s.length` | `int` | Number of characters. |
| `s.compareTo(other[, caseInsensitive])` | `int` | `-1`, `0`, or `1`. |
| `s.equals(other)` | `bool` | True when both hold the same text.  Between two `string`s this is the content comparison `==` doesn't make. |
| `s.indexOf(search)` | `int` | Position of the first occurrence, or `-1`. |
| `s.startsWith(prefix)` / `s.endsWith(suffix)` / `s.contains(search)` | `bool` | Substring tests. |
| `s.isEmpty()` | `bool` | True when the length is `0`. |

**Methods (`stringObj` only)**

| Method | Returns | Description |
|---|---|---|
| `s.capture()` | `void` | Redirect subsequent `print` output into `s`. |
| `s.release()` | `void` | End the capture and restore the previous output target. |
| `s.captureOutput(obj, prop)` | `void` | Capture the printing of `obj.prop` (a string or a routine) into `s`. |

**Example**

```bgl
#include <string>

void Main() {
    bglInit();
    string    title = "Cloak";
    stringObj name;
    name = title;                        // copies the text into name's buffer
    name = name + " of Darkness";        // a new stringObj, assigned back
    name += "!";
    if (name.startsWith("Cloak")) print(name.toUpper());   // -> CLOAK OF DARKNESS!
}
```

**Storage**

A `stringObj` always holds a string, so `null` can't be stored in one or passed to a `stringObj` parameter; both are compile-time errors.  A `ref stringObj` can be bound to null (`ref stringObj r := null;`), null can be passed to a `ref stringObj` parameter, and a function returning `stringObj` can return null; for those, `??` gives a fallback:

<!-- doctest: skip -->
```bgl
stringObj s = null;                  // error
ref stringObj r := null;             // fine: r refers to no string
print(r ?? "none");                  // none
```

Each string's characters live in a block of the string heap, a single array of `stringHeapSize` bytes (default `5120`).  A new string starts with room for 16 characters; writing more grows its block by half again.  A block which can't grow in place is copied to a new one, so for that moment the heap holds both.  There is no per-string limit beyond the heap's room.

Text captured by assigning an interpolated string to a `stringObj`, or by `capture()`, is written into the string's block as it is printed.  On the Z-machine the block is first grown to `stringCaptureSize` characters (default `512`), and the room the text didn't use is given back when the capture ends; a block which is already larger keeps its size.  Captured text longer than the room is cut short to it, and the program runs on.  On Glulx the rest is simply not written.  On the Z-machine it has already been written past the block:  where the heap's free end lay there for the whole capture nothing is lost but that text, and anywhere else it has overwritten memory in use, so the program stops with a run-time error naming the setting.  A literal assigned to a `stringObj` can use all of the heap's free room, and text built with `+=` and the other operators has no such limit.

A `string` (a literal) longer than `stringCaptureSize` can be printed and assigned to a `stringObj`; however, other operations on it (`length`, comparisons, `+`) print it into a temporary buffer of that size.  On the Z-machine they will stop with the above run-time error, and on Glulx they will see only its first `stringCaptureSize` characters.  Simply copy it into a `stringObj` first.

Two temporary buffers of `stringCaptureSize` characters are taken from the heap when the program starts, so the heap needs a bit over twice that before any string is stored.

When the heap is full, it is compacted.  If there is still no room, the string takes what room is left and the rest of the text is dropped; the program runs on, and a string made while the heap is full will be empty.  A Z-machine capture is the exception:  it can't be held to less than `stringCaptureSize` characters, so with no room for one the program stops.  Each capture under way keeps `stringCaptureSize` characters in use.

In a debug build (§20.3), text that is cut short prints a run-time warning naming the setting to raise; a release build cuts it short silently.

**Settings**

Both sizes are settings which `<string>` declares (§15.4).  Left unset or at `0`, they take the above defaults:

```bgl
#beguilerSettings { stringHeapSize = 8192; stringCaptureSize = 1024; }
#include <string>
```

In a debug build of a program with a library binding, the `heap` verb also reports the string heap (§20.3).

The string objects themselves come from a pool of `bglStringPoolReserve` (default `10`); every live `stringObj` occupies one, including each `stringObj` member of each object instance.  Some operations also borrow working copies for as long as they run:  `toUpper()`/`toLower()` two, `format()` up to four.  Exhausting the pool stops the program with a runtime error (§19).  The reserve is an I6 constant, set before the include:

```bgl
#i6 { Constant bglStringPoolReserve 32; }
#include <string>
```

> ***Note:** `<string>` requires `bglInit()`.  With it included, `print()` accepts a literal, a `stringObj`, a buffer, or a routine (which it runs).  As an array element type, a `stringObj` slot allocates on first write and is released when the element is dropped (§12.10).*

**See also** §2.2, §12.10, §21.2, §22.2.

## 22.4 `<array>`

**Purpose.**  Searching, mutation, deque, and sort operations on `array<T>`.

**Include**

```bgl
#include <array>
```

Subscripting, `size`, `length`, and assignment need no include (§12.3, §12.5); the methods below do.

Methods which take an element (`indexOf`, `contains`, `append`, …) are type-checked against `T`:  an argument of an incompatible type is a compile-time error.  Where a method needs an operation of `T` (equality, ordering, assignment, or release), it uses the one `T` publishes, or plain word semantics when it publishes none (§12.10).

**Methods**

| Method | Returns | Description |
|---|---|---|
| `isTracked()` | `bool` | True for a Beguile-declared array with length tracking; false for an I6-native `extern` array. |
| `copyFrom(src)` | `void` | Copy `src`'s elements and length into this array's own storage, up to `size`; elements of an owning type are copied through their `operator =`. |
| `indexOf(item)` / `find(item)` | `int` | First index of `item` in `0..length-1`, or `-1`. |
| `contains(item)` | `bool` | True when `item` is present. |
| `clear()` | `void` | Zero every slot up to `size`, releasing owned elements, and set the length to `0`. |
| `swap(pos1, pos2)` | `void` | Exchange two elements.  Indices must be in range. |
| `reverse()` | `void` | Reverse the elements `0..length-1` in place. |
| `append(item)` | `bool` | Add at position `length`.  False when the array is full. |
| `prepend(item)` | `bool` | Insert at position `0`, shifting the rest right.  False when full. |
| `insert(pos, item)` | `bool` | Insert at `pos` (`0..length`), shifting the rest right.  False when full or `pos` is out of range. |
| `remove(pos)` | `void` | Remove the element at `pos`, shifting the rest left.  Out of range: no effect. |
| `removeValue(item)` | `void` | Remove every element equal to `item`. |
| `arr = { a, b }` | `void` | Replace the contents: `clear()`, then append each element of the list. |
| `arr += item` / `arr += { a, b }` | `void` | Append one element, or each element of a list. |
| `arr -= item` / `arr -= { a, b }` | `void` | `removeValue` for one element, or for each element of a list. |
| `push(item)` | `void` | Insert at the front (position `0`). |
| `pop()` | `T` | Remove and return the front element; `0` when empty. |
| `peek()` | `T` | The front element without removing it; `0` when empty. |
| `enqueue(item)` | `void` | Add at the back. |
| `dequeue()` | `T` | Remove and return the front element; `0` when empty. |
| `peekEnd()` | `T` | The back element without removing it; `0` when empty. |
| `popEnd()` | `T` | Remove and return the back element; `0` when empty. |
| `sort()` | `void` | Sort `0..length-1` ascending in place, by `T`'s ordering operator or, when it has none, by signed word value. |
| `sort(compare)` | `void` | Sort with a comparator `func<int, T, T>` returning `-1`, `0`, or `1`. |

The sort is stable.

**Example**

```bgl
#include <array>
array<int> scores[8];

void Main() {
    bglInit();
    scores += { 40, 10, 30 };
    scores.append(20);
    scores.sort();                       // 10 20 30 40
    print(scores.indexOf(30));           // -> 2
    scores.sort((int a, int b) => b - a);   // descending
    int top = scores.pop();              // 40
}
```

**See also** §12.3, §12.10, §22.5.

## 22.5 `<linq>`

**Purpose.**  Chainable, LINQ-style transformations on `array<T>`.  An `array<char>` doesn't take them (a compile-time error).

**Include**

```bgl
#include <linq>          // includes <array>
```

Operations are either *non-terminals*, which return a typed array and can be chained further, or *terminals*, which reduce a chain to one value.  Calling a chain operation without `<linq>` is a compile-time error.

| Method | Returns | Description |
|---|---|---|
| `filter(pred)` | `array<T>` | The elements for which `pred(elem)` is true.  `pred` is `func<bool, T>`. |
| `map(f)` | `array<var>` | `f(elem)` for each element.  `f` is `func<var, T>`.  A mapper returning a struct with fields is a compile-time error. |
| `take(n)` / `skip(n)` | `array<T>` | The first `n` elements / all but the first `n`.  `n` is limited to `0..length`. |
| `takeWhile(pred)` / `skipWhile(pred)` | `array<T>` | Elements up to the first for which `pred` is false / from that element on. |
| `distinct()` | `array<T>` | The first occurrence of each value. |
| `orderBy()` | `array<T>` | A sorted copy, by `T`'s ordering (its `<=>`), or by signed word value when `T` has none; the source is unchanged. |
| `orderBy(compare)` | `array<T>` | A sorted copy using `func<int, T, T>`. |
| `first()` / `last()` | `T` | The first / last element; `0` when empty, or a run-time error for a struct, which has no `0`. |
| `count()` | `int` | The same as `length`. |
| `any(pred)` | `bool` | True when some element satisfies `pred`; false on an empty array. |
| `all(pred)` | `bool` | True when every element satisfies `pred`; true on an empty array. |

**Chain results.**  Each non-terminal returns a new array from the heap (§12.6), with the capacity of the array it was given.  Intermediate results are freed as the chain runs; the final result is freed like any other array once nothing refers to it.

**Struct elements.**  How a result over an array of a struct shares the source's instances, and when they are copied, is covered in §12.10.1.

**Nesting.**  A chain can run inside another chain's predicate or mapper, to any depth:  `arr.filter((Room r) => r.exits.any(isOpen))`.

**Example**

```bgl
#include <linq>
array<int> nums = { 3, -1, 4, -1, 5 };

void Main() {
    bglInit();
    array<int> positives = nums.filter((int x) => x > 0).distinct();   // captured: 3 4 5
    print(nums.map((var y) => y * 2).first());                          // -> 6
}
```

**Settings**

Results come from the heap, sized by `heapSize` (§15.4).

> ***Note:** `orderBy()` with no comparator orders by `T`'s `operator <=>` when it publishes one (a `string` by its text), and otherwise by signed word value; pass `compare` for any other order.  `distinct()` matches elements through `T`'s `operator ==` when it has one.*

**See also** §4.14, §12.6, §15.4, §22.4.

## 22.6 `<ui>`

**Purpose.**  Single-key input and cursor placement, on both targets, through `bgl.ui`.

**Include**

```bgl
#include <ui>
```

The extension adds three members to `bgl.ui`:

| Member | Returns | Description |
|---|---|---|
| `bgl.ui.waitForKey([separator])` | `char` | Wait for a key press and return it.  When `separator` (a `string`) is given, it is printed, followed by a blank line, before waiting. |
| `bgl.ui.hideCursor()` | `int` | Move the text cursor out of the prose ahead of a key read: to the last cell of the status window when `bgl.ui.statusBar.height` is positive.  Returns the window the caller should request input on (Glulx), or `0` when the main window should be used. |
| `bgl.ui.showCursor()` | `void` | Undo `hideCursor()`.  On the Z-machine, returns output to the main window; on Glulx there is nothing to undo. |

`waitForKey()` calls `hideCursor()` and `showCursor()` itself.

**Example**

```bgl
#include <ui>

void Main() {
    char k = bgl.ui.waitForKey("[Press any key]");
    if (k == 'q') return;
}
```

> ***Note:** `<ui>` does not require `bglInit()`.*

> ***[Glulx]:** `waitForKey()` requests a character event on the status window when `statusBar.height` is positive and `statusBar.id` is a real window, and otherwise on `bgl.ui.mainWin.id`; it discards other events until the key arrives.  `mainWin.id` must therefore hold the main window's handle, which a binding provides (§23.3.8).*

**See also** §21.10, §23.3.8.

## 22.7 `<glulxWindow>`

**Purpose.**  Typed access to the Glk window tree:  window types with compile-time subtype safety, splitting, sizing, image drawing, styles, and colors.

**Include**

```bgl
#include <glulxWindow>   // includes <glulxImage> when generateBlorb is true
```

> ***[Glulx]:** This extension is Glulx-only.  Including it in a Z-machine build is a compile-time error; in a source shared between targets, guard the include and all window code with `#if TARGET_GLULX`.*

Glk arranges the screen as a binary tree of windows.  A window is never resized directly; instead, an existing window is *split* to create a child.  The extension models each window as a reference-semantic object (§21.5.8), and a graphics-only or grid-only operation on the wrong kind of window is a compile-time error.

### 22.7.1 Window Types

The extension provides a base `window` type and one type for each kind of window:

| Type | Kind | Members beyond `window` |
|---|---|---|
| `window` | base | `id`, `width`, `height`, `close()`, the split family (§22.7.3), the move family (§22.7.4), `childStyles` (§22.7.7), `measureStyle()`, `styleHonored()`, `checkStyle()`, `distinguishes()` (§22.7.9), `supports` (§22.7.12) |
| `textBufferWindow` | scrolling prose | `drawImage(img, align, …)` (§22.7.5), `setStyle()`, `clearStyle()` (§22.7.7), `print()`, `printLine()`, `clear()` (§22.7.11) |
| `textGridWindow` | fixed character grid | `moveCursor(col, line)` (§22.7.6), `setStyle()`, `clearStyle()`, `print()`, `printLine()`, `clear()` |
| `graphicsWindow` | pixels | `drawImage(img, x, y, …)`, `setBackgroundColor(color)` |

The types are also reachable as `bgl.glulx.window`, `bgl.glulx.textBufferWindow`, `bgl.glulx.textGridWindow`, and `bgl.glulx.graphicsWindow`.  Windows are not world-tree objects:  they have no `parent`, `children`, or attributes.

Child windows are pooled:  at most 8 text-buffer, 8 text-grid, and 8 graphics windows can exist at once (§22.7.3).

### 22.7.2 Roots

The objects `bgl.ui.mainWin` and `bgl.ui.statusBar` (§21.10) are the roots of the tree; the extension adds the window API to those same objects.

| Object | Window kind | Added by this extension |
|---|---|---|
| `bgl.ui.mainWin` | text buffer | `width`, `height`, the split family, `childStyles`, `drawImage()`, `setStyle()`, `clearStyle()`, `print()`, `printLine()`, `clear()`, `supports` |
| `bgl.ui.statusBar` | text grid | `width`, the split family, `childStyles`, `setStyle()`, `clearStyle()`, `print()`, `clear()`, `moveCursor()`, `supports` (`height` is already there, §21.10, §23.3.8) |
| `bgl.ui.screen` | not a window | `setStyle()`, `clearStyle()` for both text window types at once (§22.7.7) |

The roots are objects, not `window` instances:  `close()`, the move family, `measureStyle()`, `styleHonored()`, `checkStyle()`, and `distinguishes()` are not available on them.

### 22.7.3 Splitting

**Syntax**

```syntax
VIEW = WIN.splitDirectionKind( SIZE [ , SCALE [ , BORDER ] ] [ , styles: SHEET ] ) ;
```

`Direction` is `Up`, `Down`, `Left`, or `Right`; `Kind` is `Grid`, `Graphics`, or `Buffer`.  Every combination exists, spelled as one method name (`splitUpGrid`, `splitLeftGraphics`, …).  `SIZE` is a number of lines (grid, buffer) or pixels (graphics) when `SCALE` is `fixed` (the default), or a percentage when it is `proportional`.  `BORDER` is `border` or `noBorder` (the default).  `SHEET` is a `styleSheet` which gives the new window its own look (§22.7.7).  Any window, root or child, can be split.

A split beyond the pool (§22.7.1) fails as `new` does (§4.13), and so does a split the interpreter refuses (e.g., a graphics window where it has no graphics) or one from a window which isn't open (a status bar never given a height).  In each case the result is `null`.

The kind fixes the returned type; the direction fixes its *orientation view*:

| Direction | Returned type |
|---|---|
| `Up`, `Down` | `textGridWindowHorz`, `graphicsWindowHorz` or `textBufferWindowHorz` |
| `Left`, `Right` | `textGridWindowVert`, `graphicsWindowVert` or `textBufferWindowVert` |

An orientation view is an `alias class` of the content type (§8.2.4) which hides the assignment to the axis a window of that orientation can't resize (§22.7.4).  Holding the result with `auto` keeps the view; holding it as the plain content type (`textGridWindow hud = …`), or casting to it, drops to the permissive surface.

**Example**

```bgl
#include <glulxWindow>
#using bgl.glulx;

void Main() {
    auto hud  = bgl.ui.mainWin.splitUpGrid(3);                        // textGridWindowHorz
    auto pic  = bgl.ui.mainWin.splitLeftGraphics(40);                 // graphicsWindowVert
    auto band = bgl.ui.statusBar.splitRightGraphics(20, proportional); // 20% wide
}
```

### 22.7.4 Sizing and Re-arrangement

**Syntax**

```syntax
WIN.width
WIN.height
WIN.width = N ;
WIN.height = N ;
WIN.moveDirection( SIZE [ , SCALE [ , BORDER ] ] ) ;
```

`Direction` is `Up`, `Down`, `Left`, or `Right`, spelled as one method name (`moveUp`, …).

`width` and `height` can be read on every window and root; reading queries the live window.  Writing re-arranges the window within its parent pair.  Only the axis which a window was split *along* can be written (`height` for an `Up`/`Down` split, `width` for a `Left`/`Right` split).  This is enforced at two levels:

- **At compile time**, when the window is held as its orientation view:  a `…Horz` view hides `width.operator =`, a `…Vert` view hides `height.operator =`, and writing the hidden axis is a compile-time error.
- **At run time**, on the permissive surface (a content-typed or `window`-typed value, or a cast):  writing the fixed axis has no effect, and reports it through `bgl.output.log()` (§21.4).

`moveUp`, `moveDown`, `moveLeft`, and `moveRight` re-arrange an *existing* child window within its parent pair, changing its placement and size.  A window split `Up` or `Down` moves only up or down, and one split `Left` or `Right` only left or right.  A `…Horz` view hides `moveLeft` and `moveRight`, a `…Vert` view hides `moveUp` and `moveDown`, and on the permissive surface a move across the axis does nothing and reports a programming error.  They are not available on the roots.

**Example**

<!-- doctest: skip -->
```bgl
auto hud = bgl.ui.mainWin.splitUpGrid(3);
hud.height = 5;                      // the split axis
// hud.width = 40;                   // compile-time error: hidden on textGridWindowHorz
((textGridWindow)hud).width = 40;    // permissive surface: no effect at run time
bgl.ui.statusBar.height = 2;         // roots are writable on both axes
```

### 22.7.5 Images

**Syntax**

```syntax
GRAPHICS-WIN.drawImage( IMAGE [ , X [ , Y [ , WIDTH [ , HEIGHT ] ] ] ] ) ;
TEXT-BUFFER-WIN.drawImage( IMAGE [ , ALIGN [ , WIDTH [ , HEIGHT ] ] ] ) ;
GRAPHICS-WIN.setBackgroundColor( COLOR ) ;
```

These methods exist only when `generateBlorb` is true (§15.6).  `IMAGE` is an `eImages` value; a `glulxImage` (§22.8) converts to one, and a raw resource id is passed as `(eImages)id`.  Every other parameter is optional and can be passed by name, as with the `img` print rule (§21.11).  A graphics window draws at pixel position `(x, y)`, default `(0, 0)`; a text-buffer window (including `bgl.ui.mainWin`) draws inline with an `eGlulxImageAlign` (default `inlineCenter`).  When both `width` and `height` are `0`, the image is drawn at its natural size; when one is given, the other is computed to preserve the aspect ratio; when both are given, they are used as is.

`setBackgroundColor(color)` sets a graphics window's background to an `$RRGGBB` value and clears the window to it.

**Example**

<!-- doctest: skip -->
```bgl
pic.drawImage(eImages.coverArt);                  // natural size, at the top-left
pic.drawImage(eImages.coverArt, width: 100);      // width 100, height to match
bgl.ui.mainWin.drawImage(eImages.icon, eGlulxImageAlign.marginLeft, 48, 48);
```

### 22.7.6 Cursor and Lifecycle

Closing a window returns it to its class's pool (§8.2.6).  The windows split from it stay open:  the window beside it takes over its space.

After a restore, undo or restart, `bgl.ui.recoverWindows()` (§21.10) brings Glk's windows back in line with the program's.  A window the program has open and Glk still has is kept as it is.  A window Glk has which the program has closed, or never opened, is closed.  A window the program has open which Glk no longer has is opened again from the window it was split from, with its split, its size and that window's `childStyles` (not a sheet passed to the split); it comes back empty, since Glk can't restore what a window showed.  Windows split from a closed window are opened again from the window it was split from.

**Methods**

| Member | On | Description |
|---|---|---|
| `win.moveCursor(col, line)` | `textGridWindow`, `bgl.ui.statusBar` | Place the cursor at column `col`, line `line`, both counted from 0. |
| `win.close()` | any child window | Close the window and return it to the pool.  Closing it again has no effect until another window is split, which can reuse its slot. |

**Example**

<!-- doctest: skip -->
```bgl
auto hud = bgl.ui.mainWin.splitUpGrid(3);   // textGridWindowHorz
hud.moveCursor(0, 0);                       // the top-left cell
hud.close();                                // hud is gone; its slot is free for the next split
```

### 22.7.7 Styles

**Syntax**

```syntax
TARGET.setStyle( STYLE-TYPE , style { MEMBER = VALUE ; … } ) ;
TARGET.clearStyle( STYLE-TYPE ) ;
```

Glk styles are hints set per window kind and style type, and a hint affects only windows of that kind created *after* it is set, never those already open.  `TARGET` is `bgl.ui.screen` (both text kinds at once), `bgl.ui.mainWin` or a `textBufferWindow` (the text-buffer kind), or `bgl.ui.statusBar` or a `textGridWindow` (the text-grid kind).  `STYLE-TYPE` is an `eGlulxStyleType` (§22.7.10).

`style` is a struct whose members default to "leave the interpreter's setting".  Build one with the named inline-object form (§11.3.1), giving only the members to change.  `clearStyle()` resets every hint of that style type to the interpreter's default.

| `style` member | Type | Meaning |
|---|---|---|
| `justify` | `int` | An `eGlulxJustify` value, cast to `int`. |
| `indentation` | `int` | Left-margin indent. |
| `paragraphIndentation` | `int` | Additional first-line indent. |
| `sizeAdjustment` | `int` | Text size relative to the base size (`…, -1, 0, 1, …`). |
| `fontWeight` | `int` | `-1` lighter, `0` normal, `1` bold. |
| `italics` | `styleFlag` | Italic or oblique text. |
| `fixedWidth` | `styleFlag` | Fixed-pitch font. |
| `foreColor` | `int` | Text color, `$RRGGBB`. |
| `backColor` | `int` | Background color, `$RRGGBB`. |
| `reverse` | `styleFlag` | Swap foreground and background. |

The members `italics`, `fixedWidth`, and `reverse` are `styleFlag`s:  they take `true` or `false`, and stay unset (`styleUnset`, §22.7.9) until given one.

**Example**

<!-- doctest: skip -->
```bgl
bgl.ui.screen.setStyle(eGlulxStyleType.normal, style { backColor = $111111; foreColor = $cccccc; });
bgl.ui.screen.setStyle(eGlulxStyleType.header, style { fontWeight = 1; justify = (int)eGlulxJustify.centered; });
hud.setStyle(eGlulxStyleType.alert, style { reverse = true; foreColor = $ff0000; });
hud.clearStyle(eGlulxStyleType.alert);
```

**A look for one window**

`setStyle()` changes a style type for every later window of that kind.  To give one window its own look, describe it as a `styleSheet`, a struct with one `style` member per style type (`normal`, `emphasized`, `fixed`, `header`, `subheader`, `alert`, `note`, `blockQuote`, `input`, `user1`, `user2`), and pass it to the split as `styles:`.  No other window is affected.  A style left unset in the sheet leaves that style type as it was.

A window's `childStyles` is a sheet which every window split *directly* from it gets.  It refers to the sheet, so changing the sheet later changes what the next split gets; assigning `null` removes it.  A sheet passed to the split takes precedence over the parent's `childStyles`, style type by style type, and both take precedence over `setStyle()`.

Hints are read only when a window opens:  neither `styles:` nor `childStyles` restyles a window that is already open.

**Example**

<!-- doctest: glulx -->
```bgl
#include <glulxWindow>
#using bgl.glulx;

styleSheet panelLook = styleSheet{
    alert      = style{ fontWeight = 1; foreColor = $C80000; };
    emphasized = style{ italics = true; };
};
styleSheet logLook = styleSheet{ normal = style{ fixedWidth = true; }; };

void Main() {
    bgl.ui.mainWin.childStyles = panelLook;
    auto notes = bgl.ui.mainWin.splitRightBuffer(25);                  // panelLook
    auto log   = bgl.ui.mainWin.splitDownBuffer(5, styles: logLook);   // logLook, then panelLook for the rest
    notes.childStyles = bgl.ui.mainWin.childStyles;                    // notes' children look the same
    print(notes != null && log != null);   // -> true
}
```

### 22.7.8 Colors

`bgl.glulx.color` provides `$RRGGBB` values:  `color.rgb(r, g, b)` composes one from `0..255` channels, and the members `black`, `white`, `red`, `green`, `blue`, `yellow`, `cyan`, `magenta`, `gray`, `lightGray`, and `darkGray` are ready-made values.  All are plain `int`s, accepted wherever a color is.

**Example**

<!-- doctest: skip -->
```bgl
#using bgl.glulx;
pic.setBackgroundColor(color.rgb(20, 30, 40));
bgl.ui.screen.setStyle(eGlulxStyleType.normal, style { foreColor = color.white; });
```

### 22.7.9 Style Validation

Interpreters can ignore style hints, and many can't report what they use.  On a child window, `measureStyle(styleType, hint)` returns the value the interpreter actually uses for that style and hint, or `styleUnset`, a `const int`, when it reports none (`fixedWidth` reads `1` for fixed-pitch, as it is set); `styleHonored(styleType, hint)` is true when the interpreter reports the hint at all.  `hint` is an `eGlulxStyleHint`, whose members are named after the `style` members.

`checkStyle(styleType, s)` compares every hint `s` sets with what the interpreter reports and returns an `eStyleCheck`:  `applied` when every reported hint matches, `differs` when at least one reported hint does not, and `unknown` when the interpreter reports none of them.  Interpreters built on Quixe (Lectrote, Parchment) report no hints, so there the answer is `unknown`.  The enum also reads as a `bool` three ways:  `r.known` (it could report), `r.isApplied`, and `r.isDiffering`.

`distinguishes(a, b)` is true when the interpreter shows style types `a` and `b` differently in that window; it is false when they look the same or it can't tell.

All of these query the live window.

**Example**

<!-- doctest: skip -->
```bgl
if (!hud.styleHonored(eGlulxStyleType.alert, eGlulxStyleHint.reverse)) { /* fall back */ }
int fg = hud.measureStyle(eGlulxStyleType.normal, eGlulxStyleHint.foreColor);

style warn = style{ fontWeight = 1; foreColor = $C80000; };
eStyleCheck r = hud.checkStyle(eGlulxStyleType.alert, warn);
bool useMarkers = r.isDiffering || !hud.distinguishes(eGlulxStyleType.normal, eGlulxStyleType.alert);
```

### 22.7.10 Enums

The following enums come from the Glulx core or from this extension.  Each core enum is also reachable through a short alias under `bgl.glulx` (§21.3); the alias names the type, and members can be written through either name (`bgl.glulx.eWinType.textGrid` or `eGlulxWindowType.textGrid`).

| Enum or bnum | Values | Provided by | `bgl.glulx` alias |
|---|---|---|---|
| `eGlulxWindowType` | `textBuffer`, `textGrid`, `graphics` | core | `eWinType` |
| `bGlulxWindowPlacement` | `left`, `right`, `above`, `below` | core | `bWinPlacement` |
| `bGlulxWindowScale` | `fixed`, `proportional` | core | `bWinScale` |
| `bGlulxWindowBorder` | `border`, `noBorder` | core | `bWinBorder` |
| `eGlulxImageAlign` | `inlineUp`, `inlineDown`, `inlineCenter`, `marginLeft`, `marginRight` | core | `eImgAlign` |
| `eGlulxImageDimension` | `original`, `scaled` | core | `eImgDimension` |
| `eGlulxStyleType` | `normal`, `emphasized`, `fixed`, `header`, `subheader`, `alert`, `note`, `blockQuote`, `input`, `user1`, `user2` | core | `eStyleType` |
| `eGlulxJustify` | `left`, `full`, `centered`, `right` | `<glulxWindow>` | - |
| `eGlulxStyleHint` | `indentation`, `paragraphIndentation`, `justify`, `sizeAdjustment`, `fontWeight`, `italics`, `fixedWidth`, `foreColor`, `backColor`, `reverse` | `<glulxWindow>` | - |
| `eStyleCheck` | `unknown`, `applied`, `differs`; emitter values `known`, `isApplied`, `isDiffering` | `<glulxWindow>` | - |
| `eCharOutput` | `none`, `approximate`, `exact` | `<glulxWindow>` | - |

### 22.7.11 Window Output

**Syntax**

```syntax
WIN.print( VALUE ) ;
WIN.printLine( [ VALUE ] ) ;
WIN.clear() ;
```

`print` and `printLine` write to one window and then return output to wherever it was going.  They print a value exactly as `print()` and `printLine()` do (§21.4).  They are members of the text windows and of `bgl.ui.mainWin` (`bgl.ui.statusBar` has `print`, `clear`, and `moveCursor`); writing to a graphics window is a compile-time error.  `clear()` empties the window.

**Example**

<!-- doctest: glulx -->
```bgl
#include <glulxWindow>

void Main() {
    auto panel = bgl.ui.mainWin.splitRightBuffer(25);
    panel?.printLine("Rooms seen:");
    panel?.printLine($"  {1 + 1} so far");
    panel?.clear();
    print("main");   // -> main
}
```

### 22.7.12 Capabilities

`bgl.glulx.supports` answers what the interpreter can do.  Each member is a property, read without parentheses:  `version` (the Glk version, `0xMMmmrr`), and the `bool`s `timer`, `graphics`, `graphicsTransparency`, `sound`, `soundMusic`, `soundVolume`, `soundNotify`, `hyperlinks`, `unicode`, `dateTime`, `lineTerminators`, and `resourceStream`.  The ones which take an argument are routines:  `charOutput(c)` returns an `eCharOutput` (`none`, `approximate`, or `exact`), `charInput(c)` is true when the player can type `c`, and `images(t)`, `mouse(t)`, and `hyperlinkInput(t)` ask about windows of type `t` (an `eGlulxWindowType`).

Every window, and both roots, also has a `supports` for its own kind:  `win.supports.images`, `win.supports.mouse`, and `win.supports.hyperlinks`.

**Example**

<!-- doctest: glulx -->
```bgl
#include <glulxWindow>

void Main() {
    if (bgl.glulx.supports.timer) print("timer ");
    bool canShow = bgl.glulx.supports.charOutput('e') != eCharOutput.none;
    print(canShow);                                   // -> true
    print(bgl.ui.mainWin.supports.hyperlinks || true);   // -> true
}
```

**See also** §8.2.4, §8.7.4, §11.3.1, §15.6, §21.4, §21.10, §22.8, §23.3.8.

## 22.8 `<glulxImage>`

**Purpose.**  A typed handle to a Glulx image resource which answers "how big is this picture?".

**Include**

```bgl
#include <glulxImage>
```

> ***[Glulx]:** Glulx-only; including it in a Z-machine build is a compile-time error.*

`glulxImage` is a primitive class over the resource id (§8.2.5).  It converts implicitly to `eImages`, is assigned from an `eImages` value or a raw `int` id, and converts to `int` with `(int)img`.

| Member | Returns | Description |
|---|---|---|
| `img.width` / `img.height` | `int` | Natural pixel dimensions. |
| `img.size` | `bglSize` | Both dimensions (§21.12). |

The same three are added to the `eImages` enum (§7.8), so `eImages.logo.width` works without a handle.

**Example**

<!-- doctest: skip -->
```bgl
#include <glulxImage>

void Main() {
    glulxImage cover = eImages.coverArt;
    int w = cover.width;
    pic.drawImage(cover, 0, 0, cover.width / 2);
}
```

> ***Note:** `<glulxImage>` does not require `bglInit()`.  It does not require `generateBlorb` either; however, without packaged assets there are no images to measure.*

**See also** §15.6, §21.12, §22.7.

## 22.9 Library-specific Extensions

**Purpose.**  Lets an extension change what the standard library does inside its own routines without replacing those routines.  Each hook is a call that a patch (§14.1.6) merges into the library.  An object defining the hook's method answers it.

**Include**

```syntax
#include <extensions/stdlib/hooks>               // every group
#include <extensions/stdlib/hooks/GROUP>       // one group
```

The hooks come in seven groups, one per area of the library.  Each group is its own extension, with its own patch.  Each group includes `<bindings/i6StandardLibrary>`, and the groups can be included in any combination and order.

| Group | Routines it hooks | Hooks |
|---|---|---|
| `informLibrary` | `InformLibrary.play` (the play loop) | `ext_afterGamePrologueNotify`, `ext_playLoopRewriteParsedCommands`, `ext_playLoopAssignObjectVariables`, `ext_playLoopPerformSingleObjectAction`, `ext_playLoopPerformMultiObjectAction` |
| `keyboard` | `KeyboardPrimitive`, `Keyboard` | `ext_keyboardPrimitiveNotify`, `ext_keyboardPrimitive`, `ext_keyboardGetInput`, `ext_beforePrompt`, `ext_keyboardPrepInput`, `ext_keyboardHandleComment`, `ext_keyboardHandleUndo`, `ext_keyboardHandleOops` |
| `parser` | `Parser__parse`, `Adjudicate`, `R_Process` | `ext_forceLookAhead`, `ext_suppressParsingImplicitTake`, `ext_adjudicate`, `ext_redirectActionNotify` |
| `standardLibrary` | `Identical`, `NounWord`, `NextWord`, `AfterRoutines`, `DisplayStatus`, `ActionPrimitive` | `ext_identical`, `ext_nounWord`, `ext_nextWord`, `ext_beforeAction`, `ext_afterAction`, `ext_afterRoutines`, `ext_displayStatus` |
| `verbs` | `PlayerTo`, `GoSub`, `LookSub`, `ExamineSub` | `ext_lookSub`, `ext_examineSub`, `ext_preDescription`, `ext_postDescription`, `ext_goSub`, `ext_goSubNoDir`, `ext_playerToNotify` |
| `writeAfterEntry` | `WriteAfterEntry` | `ext_sdPartInvBit`, `ext_sdFullInvBit`, `ext_sdConcealBit`, `ext_sdAlwaysBit`, `ext_sdRecurseBit`, `ext_sdIsOrAre`, `ext_sdNewLineBit`, `ext_sdDoList` |
| `writeList` | `WriteListR` | `ext_forceSingular`, `ext_wlrPluralMany` |

Each hook is an `extern property`, documented where its group declares it.  The `verbs` group also declares `orSuppressNewlineAfterDescription`.  The `writeAfterEntry` group also declares `_WAE_recurse_flag`, `_WAE_parenth_flag`, `_WAE_child_count`, and `_WAE_eldest_child`, which describe the entry being written.

A **library extension** is an object whose parent is the library's `LibraryExtensions` object; it answers a hook by defining a method with the hook's name.  Extensions are asked in the order they are declared:

- A hook which returns a value is asked of each extension in turn until one answers.  False (or 0) lets the library carry on as it would without the hook.  `ext_identical` is the exception:  true leaves it to the library, and false says the two objects differ.
- A hook whose name ends in `Notify`, and `ext_afterAction`, `ext_preDescription`, and `ext_postDescription`, are only told.  Every extension is told, and what it returns is ignored.
- A `keyboard` hook can also return -1, which starts reading the command again.

**Example**

<!-- doctest: compile -->
```bgl
#include <extensions/stdlib/hooks/verbs>

object darkHooks {
    parent = LibraryExtensions;
    bool ext_lookSub(){ print("It's too dark to make anything out.^"); rtrue; }
}
```

> ***Note:** The hooks exist only for the standard library, and the patches are made against version 6.12.8 (§23.2).  A patch which no longer matches the library is a compile-time error (§14.1.6).*

> ***Note:** An included group costs space even when nothing answers it.  On the Z-machine each group adds between 88 and 344 bytes; all seven add about 1.7 KB.*

> ***Note:** A group defines a hook's property only when Inform 6 code has not already defined it, and an Inform 6 object in `LibraryExtensions` answers a hook just as a Beguile one does.*

**See also** §14.1.6, §16.4, §16.6.
