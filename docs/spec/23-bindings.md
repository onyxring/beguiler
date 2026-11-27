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
  - [23.3.11 Standard Library Properties](#23311-standard-library-properties)
  - [23.3.12 Collection at the Prompt](#23312-collection-at-the-prompt)
  - [23.3.13 Parser State](#23313-parser-state)
- [23.4 Differences Between the Bindings](#234-differences-between-the-bindings)
- [23.5 Writing a Binding](#235-writing-a-binding)
<!-- /toc -->

## 23.1 What a Binding Is

A *binding* is a Beguile source file in `beguiLib/bindings/` which exposes one external Inform 6 library to Beguile's type system.  It declares the library's attributes, globals, constants, routines, and actions as `extern` (§16.4), so your code can name them with type checking, and it declares the library's additive properties.  It also integrates the library with the runtime core:  it declares the story-identity constants the library expects, runs `bglInit()` before the library's own `main`, and routes the core's `bgl.ui` window objects through the library's window handling.

A binding defines no behavior of its own; the library's I6 source is still included with `#includeI6` (§16.5).  Extensions written for one library, such as the standard library hooks, are covered in §22.9.

Bindings are optional.  Different libraries need different bindings and are not used together.

**Syntax**

```bgl
#include <bindings/i6StandardLibrary>
#include <i6StandardLibrary>            // the folder prefix is optional (§14.1.1)
```

## 23.2 Available Bindings

The following bindings are available:

| File | Library | I6 includes it corresponds to | Target notes |
|---|---|---|---|
| `bindings/i6StandardLibrary.bgl` | The Inform 6 standard library, version 6.12 | `parser`, `verblib`, `grammar` | Z-machine and Glulx.  Declares the Glulx window globals (`gg_mainwin`, `gg_statuswin`, …). |
| `bindings/punyInform.bgl` | PunyInform | `globals`, `puny` | Z-machine.  PunyInform uses no classes; objects are distinguished by attributes alone. |

Each binding is `#once`-guarded.

The standard-library binding requires release 6.12 of that library.  Against an earlier release, such as 6/10, it will stop the build with an error naming the release it needs.

**Include order.**  The library's own includes must sit where Inform 6 expects them (§18.6).  For the standard library:

```bgl
#include <bindings/i6StandardLibrary>
#includeI6 "parser"
#includeI6 "verblib"
// classes, objects, Initialise, and any entry point the program defines (NewRoom, PrintTaskName, …)
#includeI6 "grammar"
```

An entry point your program defines must come before `#includeI6 "grammar"`; a later definition is a duplicate routine.  Verbs and `extend` blocks can go anywhere.

PunyInform's own includes are the reverse:  the binding follows them, and the entry points go before them.

<!-- doctest: skip -->
```bgl
// entry points the program defines (NewRoom, DeathMessage, …)
#includeI6 "globals"
#includeI6 "puny"
#include <bindings/punyInform>
```

An entry point of PunyInform (`NewRoom`, `DeathMessage`, `LibraryMessages`, `BeforeParsing`, `ParseNoun`, `ChooseObjects`, `InScope`, `UnknownVerb`, `PrintVerb`, …) must be defined before `#includeI6 "puny"`.  A definition after the include is never called, and the only sign is the I6 warning `Routine "NewRoom" declared but not used`.  The `punyInform` binding declares none of the entry points.

## 23.3 What a Binding Provides

### 23.3.1 Entry Point and `bglInit()`

Both libraries define `Main` themselves and call the program's `Initialise` routine (§6.7), so a program using either binding defines `Initialise`, not `Main`.

Each binding wraps the library's `main` so that `bglInit()` (§21.2) runs before it; you don't call `bglInit()` yourself.

The wrap is performed only when the `autoInitialize` setting (§15.4) is true, its default.  Set `autoInitialize = false` when another I6 extension already replaces `main`; your program must then call `bglInit()` itself.

### 23.3.2 `story` and `headline`

Both libraries read the I6 constants `Story` and `Headline` for the banner.  Each binding declares them from `#beguilerSettings` (§15.7), ahead of the program's `#includeI6` of the library:

| Constant | Taken from |
|---|---|
| `story` | `#beguilerSettings.title` |
| `headline` | `#beguilerSettings.headline` |

A program using a binding must not declare them again.  The `i6StandardLibrary` binding also declares `story` and `headline` as `extern string`, so Beguile code can read them.

### 23.3.3 Capability Flags

Each binding advertises itself with an order-independent symbol (`#declare`, §14.2.3) which any file can test:

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

A binding declares the following as `extern`:

- the library's **attributes** (`light`, `container`, `scenery`, `static`, …; §21.5.1);
- its **globals**:  game state (`location`, `player`, `actor`, `score`, `turns`, …) and parser results (`noun`, `second`, `action`, `verb_word`, …).  `action` is declared with type `verb` (§13.3);
- its **constants**:  parser error codes, scope reasons, and color and window constants;
- its **objects**:  `thedark`, `selfobj`, and (standard library only) the compass direction objects `n_obj` … `d_obj`;
- its **routines** (`PlayerTo`, `TestScope`, `StartTimer`, `StatusLineHeight`, …), and, for the standard library, the optional entry points the library calls at defined moments (`AfterLife`, `NewRoom`, `TimePasses`, `InScope`, …) as `extern default` functions (§16.4.1).  PunyInform's entry points are not declared (§23.2).

Beyond the routines it documents, each binding declares every other routine the library defines (in `parser.h`, `verblib.h`, and `english.h`; in PunyInform's core files) as `extern var NAME(var a = 0, …)`, taking the routine's first locals as optional parameters; an argument the call leaves out is `0`.  A member named like any library routine is renamed automatically (§16.9).  A member reached only through `#using` loses to a library routine of the same name, with a warning (§10.4).  A program's own `extern` declaration of a library routine replaces the binding's.

The library's object properties are covered in §23.3.11.

### 23.3.5 Additive Properties

Additive properties belong to the library that defines them, so the binding declares them (§11.7.2).  The `i6StandardLibrary` binding declares `before`, `after`, `life`, `orders`, `describe`, `time_out`, and `each_turn`; the `punyInform` binding declares `before`, `after`, `life`, `describe`, `time_out`, and `each_turn` (`orders` is an ordinary property in PunyInform).  `name` is additive in the Inform 6 compiler itself and needs no binding (§11.7.2).

### 23.3.6 Verbs and Grammar Tokens

A binding declares the library's actions as `extern verb`s with their claimed dictionary words (§13.2.3):

```bgl
extern verb Take { .take|.carry|.hold|.get|.pick|.peel }
extern verb Look { .look|.l }
extern verb Receive { }                 // a fake action: no words or grammar of its own
```

It also declares the `grammarToken` enum (§13.4.1) with the parser's token names (`NOUN`, `HELD`, `CREATURE`, `TOPIC`, `MULTI`, `MULTIHELD`, `MULTIEXCEPT`, `MULTIINSIDE`, `SPECIAL`, `ANYNUMBER`, `NUMBER`, `SCOPE`, `REVERSE`) for use in grammar patterns.

### 23.3.7 Shared Types

Each binding declares `iString`, `iStringOrObject`, and `iStringOrRoutine` (§21.5.10), with `print()` overloads for `iString` and `iStringOrRoutine`.

### 23.3.8 Status Bar and Main Window

Each binding replaces the core's `bgl.ui.statusBar.height` and `bgl.ui.mainWin.id` / `bgl.ui.statusBar.id` (§21.10) with accessors which read and write the library's own status-line state:

| Member | `i6StandardLibrary` | `punyInform` |
|---|---|---|
| `bgl.ui.statusBar.height` read | the library's tracked current height | the library's tracked current height |
| `bgl.ui.statusBar.height` write | the library's status-line-height routine | sets the library's persistent height and splits immediately |
| `bgl.ui.statusBar.id` | the library's status window (Glulx); `0`, read-only (Z-machine) | `0`, read-only |
| `bgl.ui.mainWin.id` | the library's main window | the core's (`0`) |

### 23.3.9 Banner Texts

The `i6StandardLibrary` binding binds the library's banner texts (`INFORMV__TX`, `LIBRARYV__TX`, `LibRelease`) and merges its own banner into the library's `Banner` routine (§14.1.6), one which also names the Beguile version.  A game can change the banner by replacing `Banner` (§6.5).  The `punyInform` binding does the same for PunyInform.

### 23.3.10 Library Options and Debug Builds

Parts of a library exist only when an I6 option constant is set, or only in a debug build.  A binding declares such parts under the symbol which enables them, so a program naming one in a build that lacks it will fail with a Beguile error instead of an Inform 6 one:

| Parts | Declared under |
|---|---|
| Debugging verbs (`tree`, `purloin`, `actions`, …) and debug-only globals and constants | `#if DEBUG` (§20.3) |
| Standard library:  `objects` and `places` | `#if !NO_PLACES` |
| PunyInform routines:  `NumberWord`, `LanguageNumber`, `Achieved` | `#if OPTIONAL_ALLOW_WRITTEN_NUMBERS`, `#if OPTIONAL_LANGUAGE_NUMBER`, `#if OPTIONAL_FULL_SCORE && TASKS_PROVIDED` |
| PunyInform:  `recording`, `replay`, `objects`, and `places` | `#if OPTIONAL_EXTENDED_METAVERBS`; `objects` and `places` also `#if !NO_PLACES` |

You set a library option with `#defineI6` (§14.2.1), before the binding's `#include`:

<!-- doctest: skip -->
```bgl
#defineI6 OPTIONAL_EXTENDED_METAVERBS
#includeI6 @"globals"
#includeI6 "puny"
#include <bindings/punyInform>
```

### 23.3.11 Standard Library Properties

Neither binding declares the library's object properties (`description`, `capacity`, `door_to`, `before`, …) as members of `object`, except `short_name`, which both declare there (§21.4); when an object has both a `short_name` and an `instanceName` (§11.5.4), the library prints `short_name`.  A standard-library property is declared on the class or object providing it, as an ordinary member (§8.3, §11.7.1); declaring one on `extend class object` only gives it a type (§8.7.1).  The member's name is the property's, and its type says which of the library's forms it takes.  Many properties accept either a value or a routine (among them those typed `iString`, `iStringOrObject`, and `iStringOrRoutine`, §21.5.10):  declare the member as a value to give a value, or as a method to give a routine.  A routine the library calls returns what the library expects of it, given below as the method's return type.

| Property | The library uses it to | Declare it as |
|---|---|---|
| `description` | describe the object (`examine`) or room (`look`) | `string description`, `void description()`, or `bool description()`:  true when it printed the whole description (otherwise the library adds its own text) |
| `initial` | describe the object in a room before it is first moved | `string initial` or `void initial()` |
| `when_open`, `when_closed`, `when_on`, `when_off` | describe an openable or switchable object in a room | `string` or a `void` method |
| `inside_description` | describe the inside of an enterable object | `string` or a `void` method |
| `short_name` | print the object's name | `string short_name`, an interpolated string (§2.9.1), or `bool short_name()`:  true when it printed the whole name |
| `article`, `short_name_indef` | print the indefinite article / indefinite name | `string` or a `void` method |
| `plural` | name several identical objects | `string plural` or a `void` method |
| `parse_name` | match the player's words against the object | `int parse_name()`:  the number of words matched, 0 for none, -1 to use `name` |
| `n_to` … `d_to`, `in_to`, `out_to` | the room or door in that direction | `object n_to`, or `var n_to()` returning a room or door to go there, `false` to refuse with the room's `cant_go` (or the library's own message), or `true` after printing its own refusal |
| `cant_go` | refuse movement in a direction with no exit | `string cant_go` or `void cant_go()` |
| `door_to` | the room on the other side of a door | `object door_to` or `object door_to()` |
| `door_dir` | the direction a door leads | `property door_dir` (`door_dir = n_to;`) or `property door_dir()` |
| `with_key` | the key which locks and unlocks the object | `object with_key` |
| `found_in` | the rooms a floating object (a door, scenery) is in | `rawArray<object> found_in`, or `bool found_in()`:  true when it is in `location` |
| `capacity` | how many objects a container or supporter holds | `int capacity` |
| `number`, `time_left` | a general-purpose number; turns left on the object's timer | `int` |
| `daemon` | run every turn after `StartDaemon(obj)` | `void daemon()` |
| `react_before`, `react_after` | intercept any action while the object is in scope | `bool react_before()`:  true to stop the action |
| `invent` | change how the object is listed in an inventory | `bool invent()`:  true when it printed the whole entry |
| `add_to_scope` | bring other objects into scope with this one | `rawArray<object> add_to_scope`, or `void add_to_scope()` calling `AddToScope(obj)` |
| `list_together` | group similar objects in a list | `int list_together` (a group number) or `string list_together`; the library groups objects whose values are the same word, so a string is declared once, on their class, rather than repeated on each object |
| `articles` | articles for a name in a language without inflection | `rawArray<string> articles` |

The binding gives `found_in`, `add_to_scope`, and `articles` their array types, so an object can fill one without naming a type (`found_in = {study, attic};`).

> ***Note:** `null` is the same word as `false`, so a direction routine which returns `null` refuses with `cant_go` too.  A direction property given an interpolated string refuses the move, since the string returns `true` once printed.*

The additive properties (§23.3.5) are written as methods:  `bool before()` returns true to stop the action; `bool describe()`, called when the library lists the object in a room, returns true when it printed the object's paragraph itself, and the library then doesn't list it; the others follow the above table.

**Inventory listings.**  `invent` is called twice for each entry, and `inventory_stage` (1 or 2) says which:  1 before the name is printed, 2 after it, for text following the name.

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
    found_in = {study, attic};
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

### 23.3.12 Collection at the Prompt

The core keeps one routine for the library to run before it reads the player's command:  the global `_bglPromptHook` (0 until the program first allocates from the heap).  Each binding runs it there, which is where heap arrays are collected (§12.6):

| Binding | Where |
|---|---|
| `i6StandardLibrary` | A `LibraryExtensions` object's `ext_afterprompt` (6.11 and later), so a program's own `AfterPrompt` still runs. |
| `punyInform` | A replacement of `_ReadPlayerInput` which runs the hook, then the library's routine; a read with no prompt (`YesOrNo()`, menus) skips it. |

### 23.3.13 Parser State

A program reads the parser's state through the globals a binding declares (§23.3.4).  A `NUMBER` token's value is in `noun` or `second` (§13.4.2).

**The player's words.**  Inside `parse_name` (and other parsing routines) `NextWord()` returns the next word as a `dictionaryWord`, or `null` for a word not in the dictionary, and advances `wn`; `NextWordStopped()` returns -1 once the words run out.

**Telling objects apart.**  When the parser asks whether two objects sharing a `parse_name` can be told apart, it sets `parser_action` to `TheSame` and the objects in `parser_one` and `parser_two`; the routine answers -1 when they are the same and -2 when they differ (0 leaves it to their `name` words).  A `parse_name` which matched a plural word sets `parser_action = PluralFound;`.  PunyInform has only `parser_action`, for `PluralFound`.

**The topic of Ask, Tell, and Answer.**  For Ask and Tell the parser puts the topic's first word (after a leading "the") in `second`; for Answer (`answer TOPIC to CHARACTER`) it is in `noun`, and `second` is the character.  Both are declared `object`, so a cast reinterprets the word:  `(dictionaryWord)second == .weather` (§4.11).  The whole topic is the words `consult_from` to `consult_from + consult_words - 1`, which you can read with `wn = consult_from;` and `NextWord()`.

## 23.4 Differences Between the Bindings

The two bindings differ in the following ways:

| | `i6StandardLibrary` | `punyInform` |
|---|---|---|
| Directions | Direction *objects* `n_obj` … `d_obj`; a Go action has `noun == n_obj`. | No direction objects.  `selected_direction` (a `property`) holds the direction property (`n_to`, `s_to`, … `in_to`, `out_to`) and `noun` is the shared `Directions` placeholder; `selected_direction_index` is `1..12`.  The `FAKE_*_OBJ` constants are the parser's internal sentinels. |
| Reacting objects | Any object can define `before`/`after`. | An object with `each_turn`, `react_before`, `react_after`, or `add_to_scope` needs the `reactive` attribute, which the library sets for it at startup unless `OPTIONAL_MANUAL_REACTIVE` is defined; with that option the program gives `reactive` itself. |
| Extra attributes | `door`, `absent`, `pluralname`, `male`, `female`, `neuter`, `switchable`, `on`, `scored`, `workflag`, … | The same, plus `reactive`. |
| Pronouns | `itobj`, `himobj`, `herobj` | Also `themobj`. |
| Status line | `StatusLineHeight()`, `gg_statuswin_cursize` | `_StatusLineHeight()`, `statusline_height`, `statusline_current_height` |
| Colors | `CLR_*` constants | `CLR_*` plus the Ozmoo extended colors, and the `clr_on`/`clr_fg`/`clr_bg`/`clr_fgstatus` globals |
| Verb sets | The standard library's actions, meta actions, and debug actions. | PunyInform's; some claimed-word sets differ (`Drop` claims `throw`; `Shout`/`ShoutAt`; `Again`/`Oops`; `LookModeNormal`/`Short`/`Long` in place of `LMode1..3`). |

## 23.5 Writing a Binding

A binding for another library follows these rules:

1. **Guard the file with `#once`** (§14.1.7), and declare the shared types (§23.3.7).
2. **Advertise the library with `#declare`** (§14.2.3):  a bare capability symbol which other files can test regardless of include order.
3. **Declare names, not behavior.**  Use `extern attribute`, `extern object`, `extern int` / `bool` / `string`, `extern const`, `extern property`, `extern additive property`, `extern verb`, and `extern` routines (§16.4).  Give `action` the type `verb`.  Declare each additive property the library defines (§11.7.2).  Don't declare the library's object properties on `object`.
4. **Declare every action as an `extern verb` with its claimed words** (§13.2.3), including fake actions with an empty body, `extern verb Name { }`, which claims no words, so that grammar added by a program extends the library's verbs instead of colliding with them.
5. **Declare the `grammarToken` enum** with the library's parser token names (§13.4.1).
6. **Declare library-required constants in `#emitfirst`** (§14.4.2) using `##beguilerSettings.KEY` substitution (§14.4.5), so that they precede the program's `#includeI6` of the library (§23.3.2).
7. **Run `bglInit()`.**  Wrap the library's `main` with `#emitfirst { replace main _oldmain; }` and `#emitlast { [main; bglInit(); _oldmain(); ]; }`, gated on `#if bglAutoInitialize` so that `autoInitialize = false` disables it (§23.3.1).
8. **Route the status bar** by replacing `bgl.ui.statusBar`'s `id` and `height` accessors, and `bgl.ui.mainWin.id`, with the library's own state (§23.3.8).
9. **Declare the library's optional entry points** as `extern default` functions, so that a program overrides one by defining it (§16.4.1).
10. **Declare optional parts under the symbol that enables them**:  `#if DEBUG` for what exists only in a debug build, and `#if OPTION` / `#if !OPTION` for what a library option constant adds or removes (§23.3.10).
11. **Run `_bglPromptHook` when the turn's prompt is shown** (§23.3.12), when it is not 0, at a point where no Beguile routine is running.

**See also** §11.7, §13.2.3, §14.4, §16.4, §15.7, §21.5.
