# Examples

| Example | Requires | Builds standalone? |
|---|---|---|
| [`hello.bgl`](hello.bgl) | nothing | Yes: transpiles and compiles with no IF library |
| [`strings.bgl`](strings.bgl) | nothing | Yes: transpile-only (`informName = "none"`) |
| [`linq.bgl`](linq.bgl) | nothing | Yes: transpile-only |
| [`operators.bgl`](operators.bgl) | nothing | Yes: transpile-only |
| [`floats.bgl`](floats.bgl) | nothing | Yes: transpile-only, Glulx target |
| [`cloakOfDarknessPuny.bgl`](cloakOfDarknessPuny.bgl) | [PunyInform](https://github.com/johanberntsson/PunyInform) | No: you must have PunyInform installed |

Each library-free example transpiles with beguiler alone:

```
beguiler examples/strings.bgl
```

## hello.bgl

The smallest possible Beguile program. It emits a bare `Main` routine, so it needs no IF library and no Inform 6 standard library. This is the program the cross-platform CI smoke test transpiles on every supported platform.

## Feature tours

Four tiny, library-free programs, each showing one modern feature. They set `informName = "none"`, so they transpile with beguiler alone; point `informName` at Inform 6 and add an IF library to build a runnable story.

- **[`strings.bgl`](strings.bgl):** mutable strings via `#include <string>`: concatenation, case conversion, and search.
- **[`linq.bgl`](linq.bgl):** LINQ-style array queries via `#include <linq>`: `filter`, `orderBy`, and a `count` terminal.
- **[`operators.bgl`](operators.bgl):** operator overloading: a class defines `operator >` so its instances compare with `>`.
- **[`floats.bgl`](floats.bgl):** floating-point arithmetic (Glulx target only; the Z-machine has no floats).

## cloakOfDarknessPuny.bgl

A port of Roger Firth's *Cloak of Darkness*, the canonical IF benchmark, written against **PunyInform**. This is the version used in the documentation. It demonstrates a complete, playable game, but it is **not** self-contained: you need PunyInform (and its include paths set in the `#beguilerSettings` block) to build a story file from it.
