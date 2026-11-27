# Examples

| Example | Requires | Builds standalone? |
|---|---|---|
| [`hello.bgl`](hello.bgl) | nothing | Yes: transpiles with no IF library or Inform 6 |
| [`strings.bgl`](strings.bgl) | nothing | Yes: transpile-only (`informName = "none"`) |
| [`linq.bgl`](linq.bgl) | nothing | Yes: transpile-only |
| [`operators.bgl`](operators.bgl) | nothing | Yes: transpile-only |
| [`floats.bgl`](floats.bgl) | nothing | Yes: transpile-only, Glulx target |
| [`cloakOfDarknessPuny.bgl`](cloakOfDarknessPuny.bgl) | [PunyInform](https://github.com/johanberntsson/PunyInform) | No: you must have PunyInform installed |
| [`mack/_mack.bgl`](mack/_mack.bgl) | Inform 6 and its Standard Library | No: you must have both installed |

Each library-free example transpiles with beguiler alone:

```
beguiler examples/strings.bgl
```

## hello.bgl

The smallest possible Beguile program. It emits a bare `Main` routine, so it needs no IF library. This is the program the cross-platform CI smoke test transpiles on every supported platform.

## Feature tours

Four tiny, library-free programs, each showing one modern feature. They set `informName = "none"`, so they transpile with beguiler alone; point `informName` at Inform 6 and add an IF library to build a runnable story.

- **[`strings.bgl`](strings.bgl):** mutable strings via `#include <string>`: concatenation, case conversion, and search.
- **[`linq.bgl`](linq.bgl):** LINQ-style array queries via `#include <linq>`: `filter`, `orderBy`, and a `count` terminal.
- **[`operators.bgl`](operators.bgl):** operator overloading: a class defines `operator >` so its instances compare with `>`.
- **[`floats.bgl`](floats.bgl):** floating-point arithmetic (Glulx target only; the Z-machine has no floats).

## cloakOfDarknessPuny.bgl

A port of Roger Firth's *Cloak of Darkness*, the canonical IF benchmark, written against **PunyInform**. This is the version used in the documentation. It demonstrates a complete, playable game, but it is **not** self-contained: you need PunyInform to build a story file from it.

## mack

*Monkeys and Car Keys*, a full game: an entry in the 2025 IFComp, ported to Beguile from its Inform 6 original. It is here as a reference to compile and read, not as a tutorial. It is written against the **Inform 6 Standard Library** and uses a good number of the [extensions](../docs/extensions.md).

Build it from its own folder:

```
cd examples/mack
beguiler _mack.bgl
```

The `includePaths` line at the top of `_mack.bgl` is a placeholder: change it to where the Standard Library sits on your own machine. `myGameBaseline.bgl` and `gameStateTracker.bgl`, beside the game, are the author's own support files, and `assets/` holds the cover art.
