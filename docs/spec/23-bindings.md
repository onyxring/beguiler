# 23 IF Library Bindings

<!-- toc -->
- [23.1 What a Binding Is](#231-what-a-binding-is)
- [23.2 Available Bindings](#232-available-bindings)
- [23.3 What a Binding Provides](#233-what-a-binding-provides)
  - [23.3.1 Entry Point and `bglInit()`](#2331-entry-point-and-bglinit)
  - [23.3.2 `story` and `headline`](#2332-story-and-headline)
  - [23.3.3 Capability Flags](#2333-capability-flags)
  - [23.3.4 Attributes, Globals, Constants and Routines](#2334-attributes-globals-constants-and-routines)
  - [23.3.5 Additive Properties](#2335-additive-properties)
  - [23.3.6 Verbs and Grammar Tokens](#2336-verbs-and-grammar-tokens)
  - [23.3.7 Shared Types](#2337-shared-types)
  - [23.3.8 Status Bar and Main Window](#2338-status-bar-and-main-window)
  - [23.3.9 Banner Texts](#2339-banner-texts)
  - [23.3.10 Library Options and Debug Builds](#23310-library-options-and-debug-builds)
- [23.4 Differences Between the Bindings](#234-differences-between-the-bindings)
- [23.5 Writing a Binding](#235-writing-a-binding)
<!-- /toc -->

## 23.1 What a Binding Is

A *binding* is a Beguile source file in `beguiLib/bindings/` that exposes one external Inform 6 library to Beguile's type system. It declares the library's attributes, globals, constants, routines and actions with `extern` declarations (§15.4), so that Beguile code can name them with type checking; it declares the library's additive properties; and it integrates the library with the runtime core: it declares the story-identity constants the library expects, runs `bglInit()` before the library's own `main`, and routes the core's `bgl.ui` window objects through the library's window handling.

A binding defines no behavior of its own. The library's I6 source is still included with `#includeI6` (§15.5); the binding only makes its names visible.

Bindings are optional. A program that manages its own `extern` declarations, or that uses an IF library without a binding, does not include one. Different libraries need different bindings and are not used together.

**Syntax**

```bgl
#include <bindings/i6StandardLibrary>
#include <i6StandardLibrary>            // the folder prefix is optional (§14.1.1)
```

## 23.2 Available Bindings

| File | Library | I6 includes it corresponds to | Target notes |
|---|---|---|---|
| `bindings/i6StandardLibrary.bgl` | The Inform 6 standard library | `parser`, `verblib`, `grammar` | Z-machine and Glulx. Declares the Glulx window globals (`gg_mainwin`, `gg_statuswin`, …). |
| `bindings/punyInform.bgl` | PunyInform | `globals`, `puny` | Z-machine. PunyInform uses no classes; objects are distinguished by attributes alone. |

Each binding is `#once`-guarded.

**Include order.** Declarations are emitted in source order (§18.6), so the library's own includes sit
where Inform 6 expects them. For the standard library:

```bgl
#include <bindings/i6StandardLibrary>
#includeI6 "parser"
#includeI6 "verblib"
// classes, objects, Initialise, and any entry point the program defines (NewRoom, PrintTaskName, …)
#includeI6 "grammar"
```

An entry point the program defines must come before `#includeI6 "grammar"`: `grammar` declares a stub
for every entry point not yet defined, and a definition after it is then a duplicate routine. Verbs
and `extend` blocks may go anywhere: the grammar they emit follows the library's.

PunyInform's own includes are the reverse: the binding follows them, and the entry points go before them.

<!-- doctest: skip -->
```bgl
// entry points the program defines (NewRoom, DeathMessage, …)
#includeI6 "globals"
#includeI6 "puny"
#include <bindings/punyInform>
```

An entry point of PunyInform (`NewRoom`, `DeathMessage`, `LibraryMessages`, `BeforeParsing`, `ParseNoun`,
`ChooseObjects`, `InScope`, `UnknownVerb`, `PrintVerb`, …) must be defined before `#includeI6 "puny"`.
PunyInform detects an entry point with `#Ifdef` while it is being read, and compiles the call to one only
if the name is already defined; a definition after the include is a routine that nothing calls, and the
only sign is the I6 warning `Routine "NewRoom" declared but not used`. The `punyInform` binding therefore
declares none of the entry points: PunyInform has no stubs for them, so a declaration would give no
default to override, and it would not check a definition's signature either.

## 23.3 What a Binding Provides

### 23.3.1 Entry Point and `bglInit()`

Both libraries define `Main` themselves and call the program's `Initialise` routine (§6.7). A program using either binding therefore defines `Initialise`, not `Main`.

Each binding wraps the library's `main` so that `bglInit()` (§21.2) runs before it. The program does not call `bglInit()` itself.

Each binding performs the wrap only when the `autoInitialize` setting (§17.4) is true, which is its default. Set `autoInitialize = false` when another I6 extension already replaces `main` (Inform 6 forbids two replacements); the program is then responsible for calling `bglInit()`.

### 23.3.2 `story` and `headline`

Both libraries read the I6 constants `Story` and `Headline` while their own source is being included, for the banner. Each binding declares them from `#beguilerSettings` (§17.7) in an `#emitfirst` block, so that they precede the program's `#includeI6` of the library:

| Constant | Taken from |
|---|---|
| `story` | `#beguilerSettings.title` |
| `headline` | `#beguilerSettings.headline` |

A program using a binding must not declare them again. The `release` and `serial` settings are emitted by the compiler as the I6 `Release` and `Serial` directives, independently of any binding (§17.3). The `i6StandardLibrary` binding also declares `story` and `headline` as `extern string`, so Beguile code can read them.

### 23.3.3 Capability Flags

Each binding advertises itself with an order-independent symbol (`#declare`, §14.2.3), which any file — including a core file parsed before the binding — can test:

| Binding | Symbol |
|---|---|
| `i6StandardLibrary` | `I6_STANDARD_LIBRARY` |
| `punyInform` | `PUNYINFORM` |

```bgl
#if I6_STANDARD_LIBRARY
    // code that relies on the standard library
#endif
```

### 23.3.4 Attributes, Globals, Constants and Routines

A binding declares, as `extern`:

- the library's **attributes** (`light`, `container`, `scenery`, `static`, …; §21.5.1);
- its **globals**: game state (`location`, `player`, `actor`, `score`, `turns`, …) and parser results (`noun`, `second`, `action`, `verb_word`, …). `action` is declared with type `verb`, so it compares directly with verb names (§13.3);
- its **constants**: parser error codes, scope reasons, color and window constants;
- its **objects**: `thedark`, `selfobj`, and (standard library only) the compass direction objects `n_obj` … `d_obj`;
- its **routines** (`PlayerTo`, `TestScope`, `StartTimer`, `StatusLineHeight`, …), and, for the standard library, the optional entry points the library calls at defined moments (`AfterLife`, `NewRoom`, `TimePasses`, `InScope`, …) as `extern default` functions: a program overrides one by defining a function of that name (§15.4.1). PunyInform's entry points are not declared (§23.2).

Neither binding declares the library's object *properties* (`description`, `capacity`, `door_to`, `before`, …) as members of `object`; a class or object declares the properties it provides (§11.7.1), with the types given in §23.3.11. The one exception is `short_name`, which both bindings declare on `object`, since each library prints it as the object's name (§21.4).

### 23.3.5 Additive Properties

Additive properties belong to the library that defines them, so the binding declares them (§11.7.2). The `i6StandardLibrary` binding declares `before`, `after`, `life`, `orders`, `describe`, `time_out` and `each_turn`; the `punyInform` binding declares `before`, `after`, `life`, `describe`, `time_out` and `each_turn` (`orders` is an ordinary property in PunyInform). `name` is additive in the Inform 6 compiler itself and is declared by the core (§21.5.2), not by a binding.

### 23.3.6 Verbs and Grammar Tokens

A binding declares the library's actions as `extern verb`s with their claimed dictionary words, so that a program can compare `action` with them, launch them with `perform()`, and extend their grammar without colliding with the library's own `Verb` directives (§13.2.3):

```bgl
extern verb Take { .take|.carry|.hold|.get|.pick|.peel }
extern verb Look { .look|.l }
extern verb Receive { }                 // a fake action: no words or grammar of its own
```

It also declares the `grammarToken` enum (§21.5.5) with the parser's token names — `NOUN`, `HELD`, `CREATURE`, `TOPIC`, `MULTI`, `MULTIHELD`, `MULTIEXCEPT`, `MULTIINSIDE`, `SPECIAL`, `ANYNUMBER`, `NUMBER`, `SCOPE`, `REVERSE` — for use in grammar patterns.

### 23.3.7 Shared Types

Each binding declares `stringOrRoutine` (§21.5.10) with its `print()` overload. Its behavior does not depend on either library; a program includes only one binding, so the two declarations never meet.

### 23.3.8 Status Bar and Main Window

The core's `bgl.ui.statusBar.height` and `bgl.ui.mainWin.id` / `bgl.ui.statusBar.id` (§21.10) are replaced by each binding with accessors that read and write the library's own status-line state, so that `bgl.ui.statusBar.height = 2` and the library's own status-line redraw agree:

| Member | `i6StandardLibrary` | `punyInform` |
|---|---|---|
| `bgl.ui.statusBar.height` read | the library's tracked current height | the library's tracked current height |
| `bgl.ui.statusBar.height` write | the library's status-line-height routine | sets the library's persistent height and splits immediately |
| `bgl.ui.statusBar.id` | the library's status window (Glulx); `null` (Z-machine) | `null` |
| `bgl.ui.mainWin.id` | the library's main window | the core's (`0`) |

### 23.3.9 Banner Texts

The `i6StandardLibrary` binding binds the library's banner texts (`INFORMV__TX`, `LIBRARYV__TX`, `LibRelease`) and replaces the library's banner with one that also names the Beguile version. It reads the release number and serial code through `bgl.header` (§21.14).

### 23.3.10 Library Options and Debug Builds

Parts of a library exist only when an I6 option constant is set, or only in a debug build. A binding
declares such parts under the symbol that enables them, so a program that names one in a build that
lacks it fails with a Beguile error instead of an Inform 6 one:

| Parts | Declared under |
|---|---|
| Debugging verbs (`tree`, `purloin`, `actions`, …) and debug-only globals and constants | `#if DEBUG` (§20.3) |
| Standard library: `objects` and `places` | `#if !NO_PLACES` |
| PunyInform routines: `NumberWord`, `LanguageNumber`, `Achieved` | `#if OPTIONAL_ALLOW_WRITTEN_NUMBERS`, `#if OPTIONAL_LANGUAGE_NUMBER`, `#if OPTIONAL_FULL_SCORE && TASKS_PROVIDED` |
| PunyInform: `recording`, `replay`, `objects` and `places` | `#if OPTIONAL_EXTENDED_METAVERBS`; `objects` and `places` also `#if !NO_PLACES` |

A program sets a library option with `#defineI6` (§14.2.1), before the binding's `#include`:

<!-- doctest: skip -->
```bgl
#defineI6 OPTIONAL_EXTENDED_METAVERBS
#includeI6 @"globals"
#includeI6 "puny"
#include <bindings/punyInform>
```

### 23.3.11 Standard Library Properties

**Description**

A standard-library property is declared on the class or object that provides it, as an ordinary member
(§8.3, §11.7.1). The library reads each one by name, so the member's name is the property's; its type
says which of the library's forms it takes. Many properties accept either a value or a routine: declare
the member as a value to give a value, or as a method to give a routine. A routine the library calls
returns what the library expects of it, which the table gives as the method's return type.

| Property | The library uses it to | Declare it as |
|---|---|---|
| `description` | describe the object (`examine`) or room (`look`) | `string description`, `void description()`, or `bool description()`: true when it printed the whole description (otherwise the library adds its own text, such as a switchable object's "currently switched on") |
| `initial` | describe the object in a room before it is first moved | `string initial` or `void initial()` |
| `when_open`, `when_closed`, `when_on`, `when_off` | describe an openable or switchable object in a room | `string` or a `void` method |
| `inside_description` | describe the inside of an enterable object | `string` or a `void` method |
| `short_name` | print the object's name (declared on `object` by the binding) | `string short_name`, or `bool short_name()`: true when it printed the whole name |
| `article`, `short_name_indef` | print the indefinite article / indefinite name | `string` or a `void` method |
| `plural` | name several identical objects | `string plural` (declared by the binding) |
| `parse_name` | match the player's words against the object | `int parse_name()`: the number of words matched, 0 for none, -1 to use `name` |
| `n_to` … `d_to`, `in_to`, `out_to` | the room or door in that direction | `object n_to`, or `var n_to()` returning a room or door, `false` for "can't go", or `true` after printing its own refusal |
| `cant_go` | refuse movement in a direction with no exit | `string cant_go` or `void cant_go()` |
| `door_to` | the room on the other side of a door | `object door_to` or `object door_to()` |
| `door_dir` | the direction a door leads | `property door_dir` (`door_dir = n_to;`) or `property door_dir()` |
| `with_key` | the key that locks and unlocks the object | `object with_key` |
| `found_in` | the rooms a floating object (a door, scenery) is in | `rawArray<object> found_in`, or `bool found_in()`: true when it is in `location` |
| `capacity` | how many objects a container or supporter holds | `int capacity` |
| `number`, `time_left` | a general-purpose number; turns left on the object's timer | `int` |
| `daemon` | run every turn after `StartDaemon(obj)` | `void daemon()` |
| `react_before`, `react_after` | intercept any action while the object is in scope | `bool react_before()`: true to stop the action |
| `invent` | change how the object is listed in an inventory | `bool invent()`: true when it printed the whole entry |
| `add_to_scope` | bring other objects into scope with this one | `rawArray<object> add_to_scope`, or `void add_to_scope()` calling `AddToScope(obj)` |
| `list_together` | group similar objects in a list | `int list_together` (a group number) or `string list_together`; the library groups objects whose values are the same word, so a string is declared once, on their class, rather than repeated on each object |
| `articles` | articles for a name in a language without inflection | `rawArray<string> articles` |

The additive properties `before`, `after`, `life`, `orders`, `describe`, `time_out` and `each_turn` are
declared by the binding (§23.3.5) and written as methods: `bool before()` returns true to stop the
action, and the others follow the same table.

**The player's words.** Inside `parse_name` (and other parsing routines) `NextWord()` returns the next
word as a `dictionaryWord`, or `null` for a word not in the dictionary, and advances `wn`;
`NextWordStopped()` returns -1 once the words run out. Both are declared by the binding.

**The topic of Ask, Tell and Answer.** For Ask and Tell the parser puts the topic's first word (after a
leading "the") in `second`; for Answer (`answer ⟨topic⟩ to ⟨character⟩`) it is in `noun`, and `second`
is the character. Both are declared `object` by the binding, so a cast reinterprets the word:
`(dictionaryWord)second == .weather` (§4.11). The whole topic is the words `consult_from` to
`consult_from + consult_words - 1`, read with `wn = consult_from;` and `NextWord()`.

**Numbers.** A `NUMBER` grammar token (§13.4.2) puts the number in `noun` or `second`, by its position
among the line's value tokens, and in `parsed_number`. `noun` and `second` are declared `object`, so a
cast reads the number: `(int)noun`.

**Inventory listings.** `invent` is called twice for each entry; `inventory_stage` (1 or 2) says which:
1 before the name is printed, 2 after it, for text that follows the name.

**Example**

<!-- doctest: compile -->
```bgl
class Room : object { attributes = {light}; string description; }
Room hall {
    "Hall"; description = "A plain hall.";
    object n_to = study;
    var e_to(){ print("A wall.^"); return true; }
    string cant_go = "Not that way.";
}
Room study { "Study"; description = "A study."; object s_to = hall; object u_to = hatch; }
Room attic { "Attic"; description = "An attic."; object d_to = hatch; }
object hatch {
    "hatch"; attributes = {door, openable, static, scenery};
    rawArray<object> found_in = {study, attic};
    object door_to(){ if (location == study) return attic; return study; }
    property door_dir = u_to;
}
object ball {
    "ball"; parent = hall;
    bool painted = false;
    bool short_name(){ if (painted) { print("red ball"); return true; } return false; }
    int parse_name(){
        int n = 0;
        while (true){ dictionaryWord w = NextWord(); if (w == .ball || w == .red) n++; else return n; }
    }
    string initial = "A ball lies here.";
    bool invent(){ if (inventory_stage == 2) print(" (bouncy)"); return false; }
}
object guide {
    "guide"; rawArray<dictionaryWord> name = {.guide}; parent = hall; attributes = {animate, proper};
    bool life(){
        if (action == Ask && (dictionaryWord)second == .weather){ print("Rain, later.^"); return true; }
        return false;
    }
}
bool Initialise(){ location = hall; rfalse; }
```

## 23.4 Differences Between the Bindings

| | `i6StandardLibrary` | `punyInform` |
|---|---|---|
| Directions | Direction *objects* `n_obj` … `d_obj`; a Go action has `noun == n_obj`. | No direction objects. `selected_direction` (a `property`) holds the direction property (`n_to`, `s_to`, … `in_to`, `out_to`) and `noun` is the shared `Directions` placeholder; `selected_direction_index` is `1..12`. The `FAKE_*_OBJ` constants are the parser's internal sentinels. |
| Reacting objects | Any object may define `before`/`after`. | An object with `each_turn`, `react_before`, `react_after` or `add_to_scope` needs the `reactive` attribute, which the library sets for it at startup unless `OPTIONAL_MANUAL_REACTIVE` is defined; with that option the program gives `reactive` itself. |
| Extra attributes | `door`, `absent`, `pluralname`, `male`, `female`, `neuter`, `switchable`, `on`, `scored`, `workflag`, … | The same, plus `reactive`. |
| Pronouns | `itobj`, `himobj`, `herobj` | Also `themobj`. |
| Status line | `StatusLineHeight()`, `gg_statuswin_cursize` | `_StatusLineHeight()`, `statusline_height`, `statusline_current_height` |
| Colors | `CLR_*` constants | `CLR_*` plus the Ozmoo extended colors, and the `clr_on`/`clr_fg`/`clr_bg`/`clr_fgstatus` globals |
| Verb sets | The standard library's actions, meta actions and debug actions. | PunyInform's; some claimed-word sets differ (`Drop` claims `throw`; `Shout`/`ShoutAt`; `Again`/`Oops`; `LookModeNormal`/`Short`/`Long` in place of `LMode1..3`). |

## 23.5 Writing a Binding

A binding for another library follows these rules:

1. **Guard the file with `#once`** (§14.1.6), and declare the shared types (§23.3.7).
2. **Advertise the library with `#declare`** (§14.2.3): a bare capability symbol that other files can test regardless of include order.
3. **Declare names, not behavior.** Use `extern attribute`, `extern object`, `extern int` / `bool` / `string`, `extern const`, `extern property`, `extern additive property`, `extern verb` and `extern` routines (§15.4). Give `action` the type `verb`. Declare each additive property the library defines (§11.7.2). Do not declare the library's object properties on `object`.
4. **Declare every action as an `extern verb` with its claimed words** (§13.2.3), including fake actions with an empty body, `extern verb Name { }`, which claims no words, so that grammar added by a program extends the library's verbs instead of colliding with them. A program's own verb can't take a fake action's name.
5. **Declare the `grammarToken` enum** with the library's parser token names (§21.5.5).
6. **Declare library-required constants in `#emitfirst`** (§14.4.2) using `##beguilerSettings.<key>` substitution (§14.4.5), so that they precede the program's `#includeI6` of the library (§23.3.2).
7. **Run `bglInit()`.** Wrap the library's `main` with `#emitfirst { replace main _oldmain; }` and `#emitlast { [main; bglInit(); _oldmain(); ]; }`, gated on `#if bglAutoInitialize` so that `autoInitialize = false` disables it (§23.3.1).
8. **Route the status bar** by replacing `bgl.ui.statusBar`'s `id` and `height` accessors, and `bgl.ui.mainWin.id`, with the library's own state (§23.3.8), so that the core, the `<ui>` extension and `<glulxWindow>` operate on the library's windows.
9. **Declare the library's optional entry points** as `extern default` functions, so that a program overrides one by defining it (§15.4.1).
10. **Declare optional parts under the symbol that enables them**: `#if DEBUG` for what exists only in a debug build, and `#if ⟨OPTION⟩` / `#if !⟨OPTION⟩` for what a library option constant adds or removes (§23.3.10).

**See also** §11.7, §13.2.3, §14.4, §15.4, §17.7, §21.5.
