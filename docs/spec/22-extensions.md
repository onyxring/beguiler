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
- [22.8 `<glulxImage>`](#228-glulximage)
<!-- /toc -->

## 22.1 Overview

An *extension* is a file in the `beguiLib` folder that a program enables with `#include <name>` (§14.1.1). Extensions build on the runtime core (§21) and are library-agnostic: each works with any IF library binding or with none. Nothing in an extension is available until it is included; `<array>` is the exception because the core includes it itself (§12.1).

| Extension | Include | Adds | Requires `bglInit()` | Also includes | Target |
|---|---|---|---|---|---|
| Tracked character buffers | `#include <buf>` | `array<char>` length tracking, `bgl.util.buf` | yes (length headers of sized arrays) | — | both |
| Strings | `#include <string>` | content comparison on `string`; the `stringObj` type | yes | `<buf>` | both |
| Arrays | loaded by the core | search, mutation, deque and sort methods on `array<T>`; copy-on-assign | yes (length headers of sized arrays) | — | both |
| LINQ chains | `#include <linq>` | fluent chain operations on `array<T>` | yes | `<array>` | both |
| Key input | `#include <ui>` | `bgl.ui.waitForKey()`, `hideCursor()`, `showCursor()` | no | — | both |
| Glulx windows | `#include <glulxWindow>` | window types, splitting, sizing, styles, colors | no | `<glulxImage>` when `generateBlorb` is true | Glulx |
| Glulx images | `#include <glulxImage>` | the `glulxImage` handle; `eImages` metadata | no | — | Glulx |

Including an extension more than once is harmless. The names `uint`, the `char` utilities, `bgl.util.math`, `bgl.util.random`, `bglAllocated` and `bgl.world` are part of the core and need no include (§21).

Every entry below has the same shape: purpose, include line, what it adds, the settings it reads, and notes.

## 22.2 `<buf>`

**Purpose.** Length-tracked character buffers. With `<buf>` included, a sized `array<char>` is a *tracked buf*: it records its capacity and its current length, and the buffer operations below apply to it.

**Include**

```bgl
#include <buf>
```

**Description**

A tracked buf's value behaves as a standard I6 hybrid buffer — the length word first, then the characters — so it can be passed directly to I6 library routines that expect one (`print_to_array`, `glk_put_buffer`, …). `buf[i]` reads and writes character `i` (§12.4). Length and capacity are read and written through the methods below. Writing `buf[i]` does not change the length.

An `array<char>` that is not tracked (an `extern` I6 array, or one created without the tracked layout) answers `size()` and `length()` from the buffer's length word (§12.4), and `isTracked()` returns false.

**Methods on `array<char>`**

| Method | Returns | Description |
|---|---|---|
| `buf.size()` | `int` | Capacity in characters; `-1` for an untracked buffer. |
| `buf.length()` | `int` | Current number of characters. |
| `buf.setLength(n)` | `void` | Set the current length, limited to `size()` on a tracked buffer. |
| `buf.isTracked()` | `bool` | True for a tracked buf. |

**Buffer operations, `bgl.util.buf`**

Every operation takes the buffer as its first argument. Operations that return `array<char>` return the buffer they modified.

| Operation | Returns | Description |
|---|---|---|
| `set(buf, value)` | `array<char>` | Replace the contents with a string literal or another buffer; sets the length. |
| `copy(to, from, n[, toPos[, fromPos]])` | `array<char>` | Copy `n` characters (`-1`: to the end of `from`) from `from[fromPos]` to `to[toPos]`; bounded by both buffers. |
| `append(to, from)` / `prepend(to, from)` | `array<char>` | Add `from`'s contents at the end / start of `to`. |
| `insert(to, from, pos[, count])` | `array<char>` | Insert `from` (or its first `count` characters) at `pos`. |
| `delete(buf, pos, count)` | `array<char>` | Remove `count` characters at `pos`, closing the gap. |
| `mid(to, from, fromPos, n)` / `left(to, from, n)` / `right(to, from, n)` | `array<char>` | Copy a substring of `from` into `to`; `n == -1` in `mid` means to the end. |
| `indexOf(buf, search[, start])` | `int` | Position of the first occurrence at or after `start`, or `-1`. |
| `indexOfFirstTrue(buf, pred[, start])` / `indexOfFirstFalse` | `int` | Position of the first character for which `pred(c)` is true / false, or `-1`. `pred` is `func<bool, char>`. |
| `replace(buf, search, repl)` / `replaceAll(buf, search, repl[, start])` | `array<char>` | Replace the first / every occurrence in place. |
| `equals(a, b[, caseInsensitive])` | `bool` | Content equality. |
| `compare(a, b[, caseInsensitive])` | `int` | `-1`, `0` or `1`, by character code; a shorter buffer that matches sorts first. |
| `startsWith(buf, prefix[, caseInsensitive])` / `endsWith(buf, suffix[, caseInsensitive])` | `bool` | Prefix / suffix test. |
| `toUpper(buf)` / `toLower(buf)` / `reverse(buf)` | `array<char>` | In-place transforms. |
| `trim(buf)` / `trimLeft(buf)` / `trimRight(buf)` | `array<char>` | Remove leading and/or trailing spaces in place. |
| `getChar(buf, pos)` / `setChar(buf, pos, c)` | `int` / `void` | Bounds-checked character access; `getChar` returns `-1` out of range. |
| `print(buf[, len])` | `array<char>` | Print the first `len` characters, or all when `len` is omitted or `-1`. |
| `capture(buf[, maxBytes])` | `void` | Redirect subsequent `print` output into `buf`. Captures nest to a depth of 16. |
| `release()` | `array<char>` | End the most recent capture, restore the previous output target and set the buffer's length to what was written. |
| `stackLen()` | `int` | The current capture depth. |
| `isTracked(buf)` / `size(buf)` / `length(buf)` / `setLength(buf, n)` | | The function forms of the methods above. |

**Example**

```bgl
#include <buf>
array<char> line[64];

void Main() {
    bglInit();
    bgl.util.buf.set(line, "hello");
    bgl.util.buf.append(line, " world");
    bgl.util.buf.toUpper(line);
    bgl.util.buf.print(line);        // → HELLO WORLD
    print(line.length());            // → 11
}
```

**Settings**

`bglStringDefaultSize` (default `500`) is the capacity used for capture into an untracked buffer and for the pool buffers of `<string>`. It is an I6 constant, set before the include:

```bgl
#i6 { Constant bglStringDefaultSize 800; }
#include <buf>
```

**Notes**

Requires `bglInit()`, which writes the length headers of sized tracked buffers (§21.2). `compare()` orders by character code, not by locale: on the Z-machine every uppercase letter sorts before every lowercase one; pass `caseInsensitive` for dictionary order.

> **[Glulx]** Capture uses a Glk memory stream. `<buf>` supplies the constants it needs (`filemode_Write`, `gg_arguments`) when no IF library defines them.

**See also** §12.4, §22.3.

## 22.3 `<string>`

**Purpose.** Content semantics for text: comparison and ordering by content on `string`, and a second type, `stringObj`, that owns a mutable buffer.

**Include**

```bgl
#include <string>        // includes <buf>
```

**Description**

Two types, for two different things:

| Type | The slot holds | Mutable | Lifecycle |
|---|---|---|---|
| `string` | a reference to static text (a literal) | no | none; nothing is owned |
| `stringObj` | a buffer of its own, taken from the string pool | yes | allocated on declaration, released at scope exit |

A string literal is a `string`. Use `string` for text that is only read and `stringObj` for text that is built or changed. Both compare, order, `switch` and print by content, and they mix freely in expressions. Every operation that *produces* text returns a `stringObj`, so a `stringObj` is what must receive it. Assigning a `string` or a literal to a `stringObj` copies the text into the object's own buffer; assigning a `stringObj` to a `string` is a compile-time error, because the `string` would alias a buffer it does not own. `stringObj a = b;` copies `b`'s value; `ref stringObj a := b;` binds a reference (§3.7).

`s == null` and `s != null` remain identity tests on both types (there is no content to compare against `null`).

**Operators (both types unless noted)**

| Operator | Description |
|---|---|
| `s = "text"` / `s = other` | Assign. On a `string` the slot now refers to the text; on a `stringObj` the text is copied into its buffer. `stringObj` also accepts an interpolated string, capturing its output. |
| `s + v` | Concatenation; `v` is a literal or a string of the same type. Returns a new `stringObj`; `s` is unchanged. |
| `s += v` | **`stringObj` only.** Append in place. |
| `s == v` `s != v` `s < v` `s <= v` `s > v` `s >= v` | Content comparison and lexicographic ordering by character code. |
| `switch (s) { case "a": … }` | Content comparison. |
| `s[i]` | The character at position `i`. |
| `s[i] = c` | **`stringObj` only.** Replace the character at position `i`. |
| `s?` | True when the slot is not `null` (§4.10). |

**Methods (both types)**

| Method | Returns | Description |
|---|---|---|
| `s.print()` | `void` | Print the text. |
| `s.append(v)` | `stringObj` | `s` followed by a string or a `char`. |
| `s.prepend(v)` | `stringObj` | `v` followed by `s`. |
| `s.toUpper()` / `s.toLower()` | `stringObj` | Case conversion. |
| `s.trim()` / `s.trimLeft()` / `s.trimRight()` | `stringObj` | Without leading and/or trailing spaces. |
| `s.reverse()` | `stringObj` | Characters in reverse order. |
| `s.mid(start, count)` / `s.left(count)` / `s.right(count)` | `stringObj` | Substrings. |
| `s.insert(pos, src)` | `stringObj` | With `src` inserted at `pos`. |
| `s.delete(pos, count)` | `stringObj` | With `count` characters removed at `pos`. |
| `s.replace(search, repl)` / `s.replaceAll(search, repl)` | `stringObj` | With the first / every occurrence replaced. |
| `s.format(pattern[, p1[, p2]])` | `stringObj` | `pattern` with `$0` replaced by `s` and `$1`, `$2` by the arguments. |
| `s.getLength()` | `int` | Number of characters. |
| `s.compareTo(other[, caseInsensitive])` | `int` | `-1`, `0` or `1`; the form a sort comparator needs. |
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
    if (name.startsWith("Cloak")) print(name.toUpper());   // → CLOAK OF DARKNESS!
}
```

**Settings**

The pool holds `bglStringPoolReserve` string objects (default `10`); every live `stringObj` occupies one, including each `stringObj` member of each object instance. Exhausting the pool is a runtime error. Each buffer holds `bglStringDefaultSize` characters (default `500`, §22.2). Both are I6 constants, set before the include:

```bgl
#i6 { Constant bglStringPoolReserve 64; }
#include <string>
```

`bglStringPoolReserve` is unrelated to `framePoolSize` (§17.4), which sizes the Z-machine local-variable overflow pool.

**Notes**

Requires `bglInit()`, which initializes the pool. `print(string)` is replaced by a printer that accepts a literal, a `stringObj`, a buffer or a routine (which it runs). For use of `stringObj` as an array element type — the slot allocates on first write and is released when the element is dropped — see §12.10.

**See also** §2.2, §12.10, §21.2, §22.2.

## 22.4 `<array>`

**Purpose.** Searching, mutation, deque and sort operations on `array<T>`, and value-semantic assignment.

**Include**

```bgl
#include <array>         // optional: the core includes it (§12.1)
```

**Description**

The runtime core loads `<array>` automatically, so an explicit `#include <array>` is never required; the built-in part of the surface is subscripting, `size()` and `length()` (§12.3). `<array>` adds the methods below and makes `dst = src` copy the elements of `src` into `dst` (clamped to `dst`'s capacity) and set `dst`'s length, rather than alias the array. Copy-on-assign is the capture mechanism for a returned local array and for a chain result (§22.5).

Methods that take an element (`indexOf`, `contains`, `append`, …) are type-checked against `T`: an argument of an incompatible type is a compile-time error. Where a method needs an operation of `T` — equality, ordering, assignment, release — it uses the one `T` publishes, or the plain word semantics when `T` publishes none; the contract is specified in §12.10.

**Methods**

| Method | Returns | Description |
|---|---|---|
| `length()` | `int` | The number of elements in use. It is set at allocation (the element count for a list initializer, `0` for a sized declaration) and changed only by the operations below. On an untracked `extern` array it returns `size()`. |
| `setLength(n)` | `void` | Set the length. A negative `n` is a runtime error. No effect on an untracked array. |
| `isTracked()` | `bool` | True for a Beguile-declared array with length tracking; false for an I6-native `extern` array. |
| `indexOf(item)` / `find(item)` | `int` | First index of `item` in `0..length()-1`, or `-1`. |
| `contains(item)` | `bool` | True when `item` is present. |
| `clear()` | `void` | Zero every slot up to `size()`, releasing owned elements, and set the length to `0`. |
| `swap(pos1, pos2)` | `void` | Exchange two elements. Indices must be in range. |
| `reverse()` | `void` | Reverse the elements `0..length()-1` in place. |
| `append(item)` | `bool` | Add at position `length()`. False when the array is full. |
| `prepend(item)` | `bool` | Insert at position `0`, shifting the rest right. False when full. |
| `insert(pos, item)` | `bool` | Insert at `pos` (`0..length()`), shifting the rest right. False when full or `pos` is out of range. |
| `remove(pos)` | `void` | Remove the element at `pos`, shifting the rest left. Out of range: no effect. |
| `removeValue(item)` | `void` | Remove every element equal to `item`. |
| `arr += item` / `arr += { a, b }` | `void` | Append one element, or each element of a list. |
| `arr -= item` / `arr -= { a, b }` | `void` | `removeValue` for one element, or for each element of a list. |
| `push(item)` | `void` | Insert at the front (position `0`). |
| `pop()` | `T` | Remove and return the front element; `0` when empty. |
| `peek()` | `T` | The front element without removing it; `0` when empty. |
| `enqueue(item)` | `void` | Add at the back. |
| `dequeue()` | `T` | Remove and return the front element; `0` when empty. |
| `peekEnd()` | `T` | The back element without removing it; `0` when empty. |
| `popEnd()` | `T` | Remove and return the back element; `0` when empty. |
| `sort()` | `void` | Sort `0..length()-1` ascending in place, by `T`'s ordering operator or, when it has none, by signed word value. |
| `sort(compare)` | `void` | Sort with a comparator `func<int, T, T>` returning `-1`, `0` or `1`. |

`push`, `peek` and `pop` operate at the front of the array; `enqueue`, `peekEnd` and `popEnd` at the back. The sort is stable.

**Example**

```bgl
array<int> scores[8];

void Main() {
    bglInit();
    scores += { 40, 10, 30 };
    scores.append(20);
    scores.sort();                       // 10 20 30 40
    print(scores.indexOf(30));           // → 2
    scores.sort((int a, int b) => b - a);   // descending
    int top = scores.pop();              // 40
}
```

**Notes**

Requires `bglInit()`, which writes the length headers of sized tracked arrays (§21.2). Writing `arr[i] = v` never changes the length.

**See also** §12.3, §12.10, §22.5.

## 22.5 `<linq>`

**Purpose.** Chainable, LINQ-style transformations on `array<T>`.

**Include**

```bgl
#include <linq>          // includes <array>
```

**Description**

Operations are *non-terminals*, which return a typed array and may be chained further, and *terminals*, which reduce a chain to one value. Calling a chain operation without `<linq>` is a compile-time error.

| Method | Returns | Description |
|---|---|---|
| `filter(pred)` | `array<T>` | The elements for which `pred(elem)` is true. `pred` is `func<bool, T>`. |
| `map(f)` | `array<var>` | `f(elem)` for each element. `f` is `func<var, T>`; the result is `array<var>` because the mapper's result type is not tracked. |
| `take(n)` / `skip(n)` | `array<T>` | The first `n` elements / all but the first `n`. `n` is limited to `0..length()`. |
| `takeWhile(pred)` / `skipWhile(pred)` | `array<T>` | Elements up to the first for which `pred` is false / from that element on. |
| `distinct()` | `array<T>` | The first occurrence of each value. |
| `orderBy()` | `array<T>` | A sorted copy, by signed word value; the source is unchanged. |
| `orderBy(compare)` | `array<T>` | A sorted copy using `func<int, T, T>`. |
| `first()` / `last()` | `T` | The first / last element; `0` when empty. |
| `count()` | `int` | The same as `length()`. |
| `any(pred)` | `bool` | True when some element satisfies `pred`; false on an empty array. |
| `all(pred)` | `bool` | True when every element satisfies `pred`; true on an empty array. |

**Chain results.** A non-terminal's result lives in a scratch buffer that the next chain reuses. Consume it in the same statement, reduce it with a terminal, or capture it by assigning it to a typed array, which copies it (§22.4). Do not return a chain result from a function whose source is a local array (§12.6).

**Nesting.** A chain may run inside another chain's predicate or mapper — `arr.filter((Room r) => r.exits.any(isOpen))` — to a depth of two (a chain inside a chain). Deeper nesting is a runtime error that ends the program.

**Example**

```bgl
#include <linq>
array<int> nums = { 3, -1, 4, -1, 5 };

void Main() {
    bglInit();
    array<int> positives = nums.filter((int x) => x > 0).distinct();   // captured: 3 4 5
    print(nums.map((var y) => y * 2).first());                          // → 6
}
```

**Settings**

`linqScratchSize` (§17.4, default `32`) is the capacity of each scratch buffer. A chain step whose result would exceed it is a runtime error that ends the program.

**Notes**

Requires `bglInit()`, which prepares the scratch buffers. `orderBy()` with no comparator orders by signed word value even when `T` publishes an ordering operator; pass `compare` for content-ordered element types such as `string`.

**See also** §4.14, §12.6, §17.4, §22.4.

## 22.6 `<ui>`

**Purpose.** Single-key input and cursor placement, on both targets, through `bgl.ui`.

**Include**

```bgl
#include <ui>
```

**Description**

| Member | Returns | Description |
|---|---|---|
| `bgl.ui.waitForKey([separator])` | `char` | Wait for a key press and return it. When `separator` (a `string`) is given, it is printed, followed by a blank line, before waiting. |
| `bgl.ui.hideCursor()` | `int` | Move the text cursor out of the prose ahead of a key read: to the last cell of the status window when `bgl.ui.statusBar.height` is positive. Returns the window the caller should request input on (Glulx), or `0` when the main window should be used. |
| `bgl.ui.showCursor()` | `void` | Undo `hideCursor()`. On the Z-machine, returns output to the main window; on Glulx there is nothing to undo. |

`waitForKey()` calls `hideCursor()` and `showCursor()` itself.

**Example**

```bgl
#include <ui>

void Main() {
    char k = bgl.ui.waitForKey("[Press any key]");
    if (k == 'q') return;
}
```

**Notes**

Does not require `bglInit()`.

> **[Glulx]** `waitForKey()` requests a character event on the status window when `statusBar.height` is positive and `statusBar.id` is a real window, otherwise on `bgl.ui.mainWin.id`; it discards other events until the key arrives. `mainWin.id` must therefore hold the main window's handle, which a binding provides (§23.3.8).

> **[Z-machine]** `hideCursor()` switches to the upper window and positions the cursor at its last row and column; `waitForKey()` reads with the character-input opcode and then switches back.

**See also** §21.10, §23.3.8.

## 22.7 `<glulxWindow>`

**Purpose.** Typed access to the Glk window tree: window types with compile-time subtype safety, splitting, sizing, image drawing, styles and colors.

**Include**

```bgl
#include <glulxWindow>   // includes <glulxImage> when generateBlorb is true
```

> **[Glulx]** This extension is Glulx-only. Including it in a Z-machine build is a compile-time error; guard the include and all window code with `#if TARGET_GLULX` in a source shared between targets.

**Description**

Glk arranges the screen as a binary tree of windows: a window is never resized directly; instead an existing window is *split* to create a child. The extension models each window as a reference-semantic object (§21.5.8) and gives a graphics-only or grid-only operation on the wrong kind of window a compile-time error.

### 22.7.1 Window Types

**Description**

| Type | Kind | Members beyond `window` |
|---|---|---|
| `window` | base | `id`, `width`, `height`, `close()`, the split family (§22.7.3), the move family (§22.7.4), `measureStyle()`, `styleHonored()` (§22.7.9) |
| `textBufferWindow` | scrolling prose | `drawImage(img, align, …)` (§22.7.5), `setStyle()`, `clearStyle()` (§22.7.7) |
| `textGridWindow` | fixed character grid | `moveCursor(col, line)` (§22.7.6), `setStyle()`, `clearStyle()` |
| `graphicsWindow` | pixels | `drawImage(img, x, y, …)`, `setBackgroundColor(color)` |

The types are also reachable as `bgl.glulx.window`, `bgl.glulx.textBufferWindow`, `bgl.glulx.textGridWindow` and `bgl.glulx.graphicsWindow`. Windows derive from `_bglObject`, not from `object`: they are not world-tree objects and have no `parent`, `children` or attributes.

Child windows are pooled: at most 8 text-buffer, 8 text-grid and 8 graphics windows may exist at once. A split beyond the pool fails as `new` does (§4.13).

### 22.7.2 Roots

The core objects `bgl.ui.mainWin` and `bgl.ui.statusBar` (§21.10) are the roots of the tree; the extension adds the window API to those same objects.

| Object | Window kind | Added by this extension |
|---|---|---|
| `bgl.ui.mainWin` | text buffer | `width`, `height`, the split family, `drawImage()`, `setStyle()`, `clearStyle()` |
| `bgl.ui.statusBar` | text grid | `width`, the split family, `setStyle()`, `clearStyle()` (`height` is the core's, or the binding's, §23.3.8) |
| `bgl.ui.screen` | not a window | `setStyle()`, `clearStyle()` for both text window types at once (§22.7.7) |

The roots are objects, not `window` instances: `close()`, the move family, `moveCursor()`, `measureStyle()` and `styleHonored()` are not available on them.

### 22.7.3 Splitting

**Syntax**

```syntax
⟨view⟩ = ⟨win⟩.split⟨direction⟩⟨kind⟩( ⟨size⟩ [ , ⟨scale⟩ [ , ⟨border⟩ ] ] ) ;
```

⟨direction⟩ is `Up`, `Down`, `Left` or `Right`; ⟨kind⟩ is `Grid`, `Graphics` or `Buffer`; every combination exists, spelled as one method name (`splitUpGrid`, `splitLeftGraphics`, …). ⟨size⟩ is a number of lines (grid, buffer) or pixels (graphics) when ⟨scale⟩ is `fixed` (the default), or a percentage when it is `proportional`. ⟨border⟩ is `border` or `noBorder` (the default). Any window, root or child, can be split.

**Description**

The kind fixes the returned type; the direction fixes its *orientation view*:

| Direction | Returned type |
|---|---|
| `Up`, `Down` | `textGridWindowHorz`, `graphicsWindowHorz` or `textBufferWindowHorz` |
| `Left`, `Right` | `textGridWindowVert`, `graphicsWindowVert` or `textBufferWindowVert` |

An orientation view is an `alias class` of the content type (§8.2.4) that hides the assignment to the axis a window of that orientation cannot resize (§22.7.4). Holding the result with `auto` keeps the view; holding it as the plain content type (`textGridWindow hud = …`) or casting to it drops to the permissive surface.

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
⟨win⟩.width
⟨win⟩.height
⟨win⟩.width = ⟨n⟩ ;
⟨win⟩.height = ⟨n⟩ ;
⟨win⟩.move⟨direction⟩( ⟨size⟩ [ , ⟨scale⟩ [ , ⟨border⟩ ] ] ) ;
```

⟨direction⟩ is `Up`, `Down`, `Left` or `Right`, spelled as one method name (`moveUp`, …).

**Description**

`width` and `height` are readable on every window and root; reading queries the live window. Writing re-arranges the window within its parent pair. Only the axis a window was split *along* can be written — `height` for an `Up`/`Down` split, `width` for a `Left`/`Right` split — because the other dimension is dictated by the sibling window. This is enforced at two levels:

- **At compile time**, when the window is held as its orientation view: a `…Horz` view hides `width.operator =`, a `…Vert` view hides `height.operator =`, and writing the hidden axis is a compile-time error.
- **At run time**, on the permissive surface (a content-typed or `window`-typed value, or a cast): writing the fixed axis has no effect, and reports it through `log()` (§21.4).

`moveUp`, `moveDown`, `moveLeft` and `moveRight` re-arrange an *existing* child window within its parent pair, changing its placement and size and, with it, which axis is subsequently writable. They are not available on the roots.

**Example**

```bgl
auto hud = bgl.ui.mainWin.splitUpGrid(3);
hud.height = 5;                      // the split axis
// hud.width = 40;                   // compile-time error: hidden on textGridWindowHorz
(textGridWindow)hud.width = 40;      // permissive surface: no effect at run time
bgl.ui.statusBar.height = 2;         // roots are writable on both axes
```

### 22.7.5 Images

**Syntax**

```syntax
⟨graphicsWin⟩.drawImage( ⟨img⟩ , ⟨x⟩ , ⟨y⟩ [ , ⟨width⟩ [ , ⟨height⟩ ] ] ) ;
⟨textBufferWin⟩.drawImage( ⟨img⟩ [ , ⟨align⟩ [ , ⟨width⟩ [ , ⟨height⟩ ] ] ] ) ;
⟨graphicsWin⟩.setBackgroundColor( ⟨color⟩ ) ;
```

**Description**

Image drawing needs blorb assets, so these methods exist only when `generateBlorb` is true (§17.6). ⟨img⟩ is a `var`: an `eImages` value, a `glulxImage` (§22.8) or a raw resource id. A graphics window draws at pixel position `(x, y)`; a text-buffer window (including `bgl.ui.mainWin`) draws inline with an `eGlulxImageAlign` (default `inlineCenter`). When both `width` and `height` are `0` the image is drawn at its natural size; when one is given the other is computed to preserve the aspect ratio; when both are given they are used as is.

`setBackgroundColor(color)` sets a graphics window's background to an `$RRGGBB` value and clears the window to it.

**Example**

```bgl
pic.drawImage(eImages.coverArt, 0, 0);            // natural size
pic.drawImage(eImages.coverArt, 0, 0, 100);       // width 100, height to match
bgl.ui.mainWin.drawImage(eImages.icon, eGlulxImageAlign.marginLeft, 48, 48);
```

### 22.7.6 Cursor and Lifecycle

**Description**

A text-grid window positions its cursor explicitly; any child window can be closed, which also closes every window split from it.

**Methods**

| Member | On | Description |
|---|---|---|
| `win.moveCursor(col, line)` | `textGridWindow` | Place the cursor at column `col`, line `line`. |
| `win.close()` | any child window | Close the window and its subtree. Closing an already-closed window has no effect. |

**Example**

```bgl
auto hud = bgl.ui.mainWin.splitUpGrid(3);   // textGridWindowHorz
hud.moveCursor(0, 0);                       // the top-left cell
hud.close();                                // hud and any window split from it are gone
```

### 22.7.7 Styles

**Syntax**

```syntax
⟨target⟩.setStyle( ⟨styleType⟩ , style { ⟨member⟩ = ⟨value⟩ ; … } ) ;
⟨target⟩.clearStyle( ⟨styleType⟩ ) ;
```

**Description**

Glk styles are hints set per window kind and style type; a hint affects windows of that kind created *after* it is set. A style applied after a split does not affect windows already created. ⟨target⟩ is `bgl.ui.screen` (both text kinds at once), `bgl.ui.mainWin` or a `textBufferWindow` (the text-buffer kind), or `bgl.ui.statusBar` or a `textGridWindow` (the text-grid kind). ⟨styleType⟩ is an `eGlulxStyleType` (§22.7.10).

`style` is a value class whose members default to "leave the interpreter's setting". Build one with the named inline-object form (§11.3.1) giving only the members to change; `clearStyle()` resets every hint of that style type to the interpreter's default.

| `style` member | Type | Meaning |
|---|---|---|
| `justify` | `int` | An `eGlulxJustify` value, cast to `int`. |
| `indentation` | `int` | Left-margin indent. |
| `paragraphIndentation` | `int` | Additional first-line indent. |
| `sizeAdjustment` | `int` | Text size relative to the base size (`…, -1, 0, 1, …`). |
| `fontWeight` | `int` | `-1` lighter, `0` normal, `1` bold. |
| `italics` | `bool` | Italic or oblique text. |
| `fixedWidth` | `bool` | Fixed-pitch font. |
| `foreColor` | `int` | Text color, `$RRGGBB`. |
| `backColor` | `int` | Background color, `$RRGGBB`. |
| `reverse` | `bool` | Swap foreground and background. |

**Example**

```bgl
bgl.ui.screen.setStyle(eGlulxStyleType.normal, style { backColor = $111111; foreColor = $cccccc; });
bgl.ui.screen.setStyle(eGlulxStyleType.header, style { fontWeight = 1; justify = (int)eGlulxJustify.centered; });
hud.setStyle(eGlulxStyleType.alert, style { reverse = true; foreColor = $ff0000; });
hud.clearStyle(eGlulxStyleType.alert);
```

### 22.7.8 Colors

**Description**

`bgl.glulx.color` provides `$RRGGBB` values: `color.rgb(r, g, b)` composes one from `0..255` channels, and the members `black`, `white`, `red`, `green`, `blue`, `yellow`, `cyan`, `magenta`, `gray`, `lightGray` and `darkGray` are ready-made values. All are plain `int`s and are accepted wherever a color is.

**Example**

```bgl
#using bgl.glulx;
pic.setBackgroundColor(color.rgb(20, 30, 40));
bgl.ui.screen.setStyle(eGlulxStyleType.normal, style { foreColor = color.white; });
```

### 22.7.9 Style Validation

**Description**

Interpreters may ignore style hints. On a child window, `measureStyle(styleType, hint)` returns the value the interpreter actually uses for that style and hint, or `styleUnset` when it reports none; `styleHonored(styleType, hint)` is true when the interpreter reports the hint at all. `hint` is an `eGlulxStyleHint`, whose members are named after the `style` members. Both query the live window, so they are called after the window exists.

**Example**

```bgl
if (!hud.styleHonored(eGlulxStyleType.alert, eGlulxStyleHint.reverse)) { /* fall back */ }
int fg = hud.measureStyle(eGlulxStyleType.normal, eGlulxStyleHint.foreColor);
```

### 22.7.10 Enums

**Description**

Provided by the Glulx core or by this extension. Each core enum is also reachable through a short alias under `bgl.glulx` (§21.3); the alias names the type, and members may be written through either name (`bgl.glulx.eWinType.textGrid` or `eGlulxWindowType.textGrid`).

| Enum or bnum | Values | Provided by | `bgl.glulx` alias |
|---|---|---|---|
| `eGlulxWindowType` | `textBuffer`, `textGrid`, `graphics` | core | `eWinType` |
| `bGlulxWindowPlacement` | `left`, `right`, `above`, `below` | core | `bWinPlacement` |
| `bGlulxWindowScale` | `fixed`, `proportional` | core | `bWinScale` |
| `bGlulxWindowBorder` | `border`, `noBorder` | core | `bWinBorder` |
| `eGlulxImageAlign` | `inlineUp`, `inlineDown`, `inlineCenter`, `marginLeft`, `marginRight` | core | `eImgAlign` |
| `eGlulxImageDimension` | `original`, `scaled` | core | `eImgDimension` |
| `eGlulxStyleType` | `normal`, `emphasized`, `fixed`, `header`, `subheader`, `alert`, `note`, `blockQuote`, `input`, `user1`, `user2` | core | `eStyleType` |
| `eGlulxJustify` | `left`, `full`, `centered`, `right` | `<glulxWindow>` | — |
| `eGlulxStyleHint` | `indentation`, `paragraphIndentation`, `justify`, `sizeAdjustment`, `fontWeight`, `italics`, `fixedWidth`, `foreColor`, `backColor`, `reverse` | `<glulxWindow>` | — |

`styleUnset` is the `const int` returned by `measureStyle()` for an unreported hint.

**See also** §8.2.4, §8.7.4, §11.3.1, §17.6, §21.10, §22.8, §23.3.8.

## 22.8 `<glulxImage>`

**Purpose.** A typed handle to a Glulx image resource that answers "how big is this picture?".

**Include**

```bgl
#include <glulxImage>
```

> **[Glulx]** Glulx-only; including it in a Z-machine build is a compile-time error.

**Description**

`glulxImage` is a veneer class over the resource id (§8.2.5): it adds a type and methods but no storage, so a `glulxImage` is accepted wherever an image id is (`drawImage`, `bgl.printRules.img`). It is assigned from an `eImages` value or a raw `int` id, and converts back to `int` with `(int)img`.

| Member | Returns | Description |
|---|---|---|
| `img.width()` / `img.height()` | `int` | Natural pixel dimensions. |
| `img.size()` | `bglSize` | Both dimensions (§21.12). |

The same three members are added to the `eImages` enum, so `eImages.logo.width()` works without a handle.

**Example**

```bgl
#include <glulxImage>

void Main() {
    glulxImage cover = eImages.coverArt;
    int w = cover.width();
    pic.drawImage(cover, 0, 0, cover.width() / 2);
}
```

**Notes**

Does not require `bglInit()`. Does not require `generateBlorb`, but without packaged assets there are no images to measure.

**See also** §17.6, §21.12, §22.7.
