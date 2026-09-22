# 21 IF Library Bindings

<!-- toc -->
- [21.1 What a Binding Is](#211-what-a-binding-is)
- [21.2 Available Bindings](#212-available-bindings)
- [21.3 What a Binding Provides](#213-what-a-binding-provides)
  - [21.3.1 Entry Point and `bglInit()`](#2131-entry-point-and-bglinit)
  - [21.3.2 `story` and `headline`](#2132-story-and-headline)
  - [21.3.3 Capability Flags](#2133-capability-flags)
  - [21.3.4 Attributes, Globals, Constants and Routines](#2134-attributes-globals-constants-and-routines)
  - [21.3.5 Additive Properties](#2135-additive-properties)
  - [21.3.6 Verbs and Grammar Tokens](#2136-verbs-and-grammar-tokens)
  - [21.3.7 Shared Types](#2137-shared-types)
  - [21.3.8 Status Bar and Main Window](#2138-status-bar-and-main-window)
  - [21.3.9 `bgl.story`](#2139-bglstory)
- [21.4 Differences Between the Bindings](#214-differences-between-the-bindings)
- [21.5 Writing a Binding](#215-writing-a-binding)
<!-- /toc -->

## 21.1 What a Binding Is

A *binding* is a Beguile source file in `beguiLib/bindings/` that exposes one external Inform 6 library to Beguile's type system. It declares the library's attributes, globals, constants, routines and actions with `extern` declarations (§13.4), so that Beguile code can name them with type checking; it declares the library's additive properties; and it integrates the library with the runtime core: it declares the story-identity constants the library expects, runs `bglInit()` before the library's own `main`, and routes the core's `bgl.ui` window objects through the library's window handling.

A binding defines no behavior of its own. The library's I6 source is still included with `#includeI6` (§13.5); the binding only makes its names visible.

Bindings are optional. A program that manages its own `extern` declarations, or that uses an IF library without a binding, does not include one. Different libraries need different bindings and are not used together.

**Syntax**

```bgl
#include <bindings/i6StandardLibrary>
#include <i6StandardLibrary>            // the folder prefix is optional (§12.1.1)
```

## 21.2 Available Bindings

| File | Library | I6 includes it corresponds to | Target notes |
|---|---|---|---|
| `bindings/i6StandardLibrary.bgl` | The Inform 6 standard library | `parser`, `verblib`, `grammar` | Z-machine and Glulx. Declares the Glulx window globals (`gg_mainwin`, `gg_statuswin`, …). |
| `bindings/punyInform.bgl` | PunyInform | `globals`, `puny` | Z-machine. PunyInform uses no classes; objects are distinguished by attributes alone. |
| `bindings/_commonBindings.bgl` | — | — | Shared declarations both bindings include (§21.3.7). Not included directly. |

Each binding is `#once`-guarded.

## 21.3 What a Binding Provides

### 21.3.1 Entry Point and `bglInit()`

Both libraries define `Main` themselves and call the program's `Initialise` routine (§6.7). A program using either binding therefore defines `Initialise`, not `Main`.

Each binding wraps the library's `main` so that `bglInit()` (§19.2) runs before it. The program does not call `bglInit()` itself.

The `i6StandardLibrary` binding performs the wrap only when the `autoInitialize` setting (§15.4) is true, which is its default. Set `autoInitialize = false` when another I6 extension already replaces `main` (Inform 6 forbids two replacements); the program is then responsible for calling `bglInit()`.

### 21.3.2 `story` and `headline`

Both libraries read the I6 constants `Story` and `Headline` while their own source is being included, for the banner. Each binding declares them from `#beguilerSettings` (§15.7) in an `#emitfirst` block, so that they precede the program's `#includeI6` of the library:

| Constant | Taken from |
|---|---|
| `story` | `#beguilerSettings.title` |
| `headline` | `#beguilerSettings.headline` |

A program using a binding must not declare them again. The `release` and `serial` settings are emitted by the compiler as the I6 `Release` and `Serial` directives, independently of any binding (§15.3). The `i6StandardLibrary` binding also declares `story` and `headline` as `extern string`, so Beguile code can read them.

### 21.3.3 Capability Flags

Each binding advertises itself with an order-independent symbol (`#declare`, §12.2.3), which any file — including a core file parsed before the binding — can test:

| Binding | Symbol |
|---|---|
| `i6StandardLibrary` | `I6_STANDARD_LIBRARY` |
| `punyInform` | `PUNYINFORM` |

```bgl
#if I6_STANDARD_LIBRARY
    // code that relies on the standard library
#endif
```

### 21.3.4 Attributes, Globals, Constants and Routines

A binding declares, as `extern`:

- the library's **attributes** (`light`, `container`, `scenery`, `static`, …; §19.5.1);
- its **globals**: game state (`location`, `player`, `actor`, `score`, `turns`, …) and parser results (`noun`, `second`, `action`, `verb_word`, …). `action` is declared with type `verb`, so it compares directly with verb names (§11.3);
- its **constants**: parser error codes, scope reasons, color and window constants;
- its **objects**: `thedark`, `selfobj`, and (standard library only) the compass direction objects `n_obj` … `d_obj`;
- its **routines** (`PlayerTo`, `TestScope`, `StartTimer`, `StatusLineHeight`, …), and, for the standard library, the optional entry points the library calls at defined moments (`AfterLife`, `NewRoom`, `TimePasses`, `InScope`, …) as `extern default` functions: a program overrides one by defining a function of that name (§13.4.1).

Neither binding declares the library's object *properties* (`description`, `capacity`, `door_to`, `before`, …) as members of `object`; a class or object declares the properties it provides (§9.7.1). The one exception is `short_name`, which the core declares on `object` (§19.4).

### 21.3.5 Additive Properties

Additive properties belong to the library that defines them, so the binding declares them (§9.7.2). The `i6StandardLibrary` binding declares `before`, `after`, `life`, `orders`, `describe`, `time_out` and `each_turn`. `name` is additive in the Inform 6 compiler itself and is declared by the core (§19.5.2), not by a binding.

### 21.3.6 Verbs and Grammar Tokens

A binding declares the library's actions as `extern verb`s with their claimed dictionary words, so that a program can compare `action` with them, launch them with `perform()`, and extend their grammar without colliding with the library's own `Verb` directives (§11.2.3):

```bgl
extern verb Take { .take|.carry|.hold|.get|.pick|.peel }
extern verb Look { .look|.l }
extern verb Receive;                    // a fake action: no grammar of its own
```

It also declares the `grammarToken` enum (§19.5.5) with the parser's token names — `noun`, `held`, `creature`, `topic`, `multi`, `multiheld`, `multiexcept`, `multiinside`, `special`, `anynumber`, `number`, `scope`, `reverse` — for use in grammar patterns.

### 21.3.7 Shared Types

`bindings/_commonBindings.bgl`, included by both bindings, declares `stringOrRoutine` (§19.5.10) with its `print()` overload. Its behavior does not depend on either library.

### 21.3.8 Status Bar and Main Window

The core's `bgl.ui.statusBar.height` and `bgl.ui.mainWin.id` / `bgl.ui.statusBar.id` (§19.10) are replaced by each binding with accessors that read and write the library's own status-line state, so that `bgl.ui.statusBar.height = 2` and the library's own status-line redraw agree:

| Member | `i6StandardLibrary` | `punyInform` |
|---|---|---|
| `bgl.ui.statusBar.height` read | the library's tracked current height | the library's tracked current height |
| `bgl.ui.statusBar.height` write | the library's status-line-height routine | sets the library's persistent height and splits immediately |
| `bgl.ui.statusBar.id` | the library's status window (Glulx); `null` (Z-machine) | `null` |
| `bgl.ui.mainWin.id` | the library's main window | the core's (`0`) |

### 21.3.9 `bgl.story`

The `i6StandardLibrary` binding adds `bgl.story`, the story file's identity values read from the story header:

| Member | Returns | Description |
|---|---|---|
| `bgl.story.release` | `int` | The release number. |
| `bgl.story.serialChar(i)` | `char` | Character `i` (`0..5`) of the six-character serial. |
| `bgl.story.printSerial()` | `void` | Print the serial. |

It also binds the library's banner texts (`INFORMV__TX`, `LIBRARYV__TX`, `LibRelease`) and provides `informVersion`, a value-less emitter that prints the Inform 6 compiler version, usable inside an interpolated string like a print rule (§19.11).

> **[Z-machine/Glulx difference]** The header addresses differ per target; `bgl.story` hides the difference.

## 21.4 Differences Between the Bindings

| | `i6StandardLibrary` | `punyInform` |
|---|---|---|
| Directions | Direction *objects* `n_obj` … `d_obj`; a Go action has `noun == n_obj`. | No direction objects. `selected_direction` (a `property`) holds the direction property (`n_to`, `s_to`, … `in_to`, `out_to`) and `noun` is the shared `Directions` placeholder; `selected_direction_index` is `1..12`. The `FAKE_*_OBJ` constants are the parser's internal sentinels. |
| Reacting objects | Any object may define `before`/`after`. | An object with `before`, `after`, `each_turn`, `react_before` or `react_after` must have the `reactive` attribute. |
| Extra attributes | `door`, `absent`, `pluralname`, `male`, `female`, `neuter`, … | Also `switchable`, `on`, `workflag`, `reactive`, `scored`. |
| Pronouns | `itobj`, `himobj`, `herobj` | Also `themobj`. |
| Status line | `StatusLineHeight()`, `gg_statuswin_cursize` | `_StatusLineHeight()`, `statusline_height`, `statusline_current_height` |
| Colors | `CLR_*` constants | `CLR_*` plus the Ozmoo extended colors, and the `clr_on`/`clr_fg`/`clr_bg`/`clr_fgstatus` globals |
| Verb sets | The standard library's actions, meta actions and debug actions. | PunyInform's; some claimed-word sets differ (`Drop` claims `throw`; `Shout`/`ShoutAt`; `Again`/`Oops`; `LookModeNormal`/`Short`/`Long` in place of `LMode1..3`). |
| `autoInitialize` | Honored. | Not consulted; `main` is always wrapped. |

## 21.5 Writing a Binding

A binding for another library follows these rules:

1. **Guard the file with `#once`** (§12.1.6), and include `<bindings/_commonBindings>` for the shared types.
2. **Advertise the library with `#declare`** (§12.2.3): a bare capability symbol that other files can test regardless of include order.
3. **Declare names, not behavior.** Use `extern attribute`, `extern object`, `extern int` / `bool` / `string`, `extern const`, `extern property`, `extern additive property`, `extern verb` and `extern` routines (§13.4). Give `action` the type `verb`. Declare each additive property the library defines (§9.7.2). Do not declare the library's object properties on `object`.
4. **Declare every action as an `extern verb` with its claimed words** (§11.2.3), including fake actions as bodiless `extern verb Name;`, so that grammar added by a program extends the library's verbs instead of colliding with them.
5. **Declare the `grammarToken` enum** with the library's parser token names (§19.5.5).
6. **Declare library-required constants in `#emitfirst`** (§12.4.2) using `##beguilerSettings.<key>` substitution (§12.4.5), so that they precede the program's `#includeI6` of the library (§21.3.2).
7. **Run `bglInit()`.** Wrap the library's `main` with `#emitfirst { replace main _oldmain; }` and `#emitlast { [main; bglInit(); _oldmain(); ]; }`, gated on `#if bglAutoInitialize` so that `autoInitialize = false` disables it (§21.3.1).
8. **Route the status bar** by replacing `bgl.ui.statusBar`'s `id` and `height` accessors, and `bgl.ui.mainWin.id`, with the library's own state (§21.3.8), so that the core, the `<ui>` extension and `<glulxWindow>` operate on the library's windows.
9. **Declare the library's optional entry points** as `extern default` functions, so that a program overrides one by defining it (§13.4.1).

**See also** §9.7, §11.2.3, §12.4, §13.4, §15.7, §19.5.
