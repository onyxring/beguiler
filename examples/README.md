# Examples

| Example | Library required | Builds standalone? |
|---|---|---|
| [`hello.bgl`](hello.bgl) | none | **Yes** — transpiles and compiles with no IF library |
| [`cloakOfDarknessPuny.bgl`](cloakOfDarknessPuny.bgl) | [PunyInform](https://github.com/johanberntsson/PunyInform) | No — you must have PunyInform installed |

## hello.bgl

The smallest possible Beguile program. It emits a bare `Main` routine, so it
needs no IF library and no Inform 6 standard library. This is the program the
cross-platform CI smoke test transpiles on every supported platform:

```
beguiler examples/hello.bgl
```

## cloakOfDarknessPuny.bgl

A port of Roger Firth's *Cloak of Darkness* — the canonical IF benchmark —
written against **PunyInform**. This is the version used in the documentation.
It demonstrates a complete, playable game, but it is **not** self-contained:
you need PunyInform (and its include paths set in the `#beguilerSettings`
block) to build a story file from it.
