# Changelog

## [Unreleased]

Releases:
- Each release archive now carries the matching Beguilex extension in a
  `beguilex` folder: the `.vsix`, built from the Beguilex repository at the same
  tag, with its README, CHANGELOG and license. The `.vsix` is also attached to the
  release on its own.

Library (beguiLib):
- A new official extension, `<extensions/stdlib/hooks>`: hooks inside the
  standard library's routines (the play loop, reading and parsing the command,
  LOOK, EXAMINE, GO, lists and more), merged into the library with `#mergeI6`
  rather than replacing its routines. An object in `LibraryExtensions` answers
  a hook by defining its `ext_` method. Each group of hooks is its own extension
  (`hooks/informLibrary`, `keyboard`, `parser`, `standardLibrary`, `verbs`,
  `writeAfterEntry`, `writeList`), so a program pays only for the groups it
  includes. Ported from orLibrary's hooks, which now use them.
- New official extensions `<extensions/stdlib/trackSeen>` (the `beenSeen`
  attribute), `<extensions/stdlib/distinctRead>` (READ as its own action, with
  `readDescription`), `<extensions/stdlib/review>` (REVIEW and REVIEW <object>,
  taking no turn) and `<extensions/stdlib/firstImpressions>` (`preImpression`
  and `postImpression`, shown the first time only). Ported from orLibrary's
  _orTrackSeen, orDistinctRead, orReview and orFirstImpressions.
- New official extensions `<extensions/stdlib/adjective>` (an object's
  `adjective` words describe it without naming it, and the parser prefers an
  object named by a noun) and `<extensions/stdlib/parserUtil>` (the
  ChooseObjects codes and answers, and helpers that change the parser's match
  list). Ported from orLibrary's orAdjective and orUtilParser.
- A new official extension, `<extensions/stdlib/stateDescriptors>`: the notes
  after an object's name in room descriptions and inventories ("(closed and
  providing light)") come from handlers a game can add, remove or replace.
  Ported from orLibrary's orStateDescriptors.
- A new library module, `<languages/english>`: pronouns and verbs that agree
  with an object, for interpolated text. `cThe(player, "give", "gives",
  "gave")` prints "You give" or "Bob gave"; `they`, `them`, `their`, `theirs`
  and `themselves` give each case for any person, number and gender;
  `cThe(o, are)`, `cant`, `have` and the like cover the common verbs. The
  `narrativeTense` and `narrativeVoice` settings fix the tense and voice, and a
  fixed tense leaves the other tense's text out of the story file.
- `<languages/english/messages>`: the standard library's messages, written with
  `<languages/english>` in place of English.h's LanguageLM, which a merged patch
  leaves out. A game that includes it prints the same text, following the
  `narrativeTense` and `narrativeVoice` settings. The story file is smaller
  than with English.h's messages: a minimal game by 1,480 bytes with both
  settings fixed, 104 with neither; Mack by 1,160.
- A new official extension, `<extensions/general/transition>`:
  `bgl.output.transition()` divides story parts with centered text, a prompt, a
  key press and a cleared window. Ported from orLibrary's orTransition.
- A new official extension, `<extensions/stdlib/backdrop>`: scenery a room
  mentions, answered without an object for every word. A `backdrops` member is
  an array of rows, each holding the words, a description and a reply, and a
  `backdrop` object can float the same rows between rooms. Ported from
  orLibrary's orBackdrop.
- `_bglArray` follows a promoted member array when given its owner and property,
  so callers using that form can pass one over 31 elements.
- A new official extension, `<extensions/stdlib/betterChoice>`: the parser passes
  over objects a command can't sensibly mean, and an object can answer for itself
  with `chooseobject`. Ported from orLibrary's orBetterChoice.
- A new official extension, `<extensions/stdlib/menu>`: a tree of menus for help,
  hints and about screens. Ported from orLibrary's orMenu.
- A new official extension, `<extensions/stdlib/statusLine>`: a status line whose
  left, middle and right sections a game can replace. Ported from orLibrary's
  orStatusLine.
- With `<buf>`, `print()` prints a character buffer.
- `pop()`, `popEnd()` and `dequeue()` on an array of a struct or owning type now
  release the slot, as `remove()` does. The returned value stays readable until
  the slot is handed out again.
- Generated names for anonymous arrays, inline objects and array headers are now
  short (`bglrab`, `bglhab`). Inform 6 stores array names in the story, so games
  with many anonymous arrays get smaller.
- `<string>` is now the heap-based runtime that was `<stringObj>`: each
  `stringObj` buffer grows as text is written, with no 494-character limit. The
  pool-based runtime is kept as `<archive/string>`; a program includes one or
  the other.
- `center()` and `inset()` moved from `bgl.ui` to `bgl.output`.
- The library bindings replace `stringOrRoutine` with `iString`
  (`string | interpolatedString`), a string plain or interpolated, for
  properties such as `description` and `short_name`; `iStringOrRoutine`, which
  also holds a routine, for a lambda or a routine assigned while the game runs;
  and `iStringOrObject`, for direction properties that hold a room or a message.
- The library bindings make an object's name and parent positional, as in an
  Inform 6 object header: `object hook { "small brass hook", cloakroom; }`.

Language:
- `#mergeI6 "name"` applies a patch, a unified diff from `diff -u` or `git
  diff`, to the Inform 6 files it names, wherever it appears in the program.
  Patches stack, so several extensions can each change a library routine
  without replacing it; a patch that no longer matches is a compile error. The
  patched copies go in the output folder's `i6patched` folder, ahead of every
  other include directory.
- `beguiler --make-patch` and `--apply-patch` write and read those patches, for
  the editor's Create I6 Patch and Edit I6 Patch commands.
- An interpolated string used as a value becomes a routine that prints it,
  unless it is assigned to a `stringObj`: a `var`, an `auto`, a `var` argument
  and an element of an initializer list take one (`array<var> a = {$"…"};`).
- `interpolatedString`: an interpolated string assigned to one, to a
  `func<void>`, or to a library property that takes a string or a routine
  (`short_name = $"{watts}W lamp";`) becomes a routine that prints it, its
  expressions evaluated each time it prints. Assigned to a `stringObj`, it is
  the text at that moment. `print()` and `printLine()` print one.
- Positional slots an `extern` class lists come after the program's own, as
  the root's name always has.
- `#include "…"` never finds a file in the Beguile library, which is included
  only with `#include <…>`; quoting one is an error naming the bracket form.
- `beguiLibPath` in the entry file's settings now names the library for the
  whole program (the core included); `-lib=` still wins, and a relative path is
  relative to the entry file. Another file naming a different library is an error.
- The library compiles without warnings, and its warnings are no longer hidden.
  A local is warned about hiding a global only when the global is in its own
  file or one it includes.
- A parameter named like a member of its class or object is no longer warned
  about (`void resize(int width) { self.width = width; }`).
- `heapSize` and `worldBufSize` are read by the library like any other setting;
  `worldBufSize = 0` is now an error, as `heapSize` below 64 was.
- Smaller story files, with what prints and what returns unchanged:
  - A call or interpolated `{…}` whose argument is a `?:` takes each branch's
    value itself (`if (c) print("A"); else print("b");`), nested `?:` too,
    so no temporary is stored and a literal prints inline.
  - An emitter's `print (string) $s` prints a literal argument inline.
  - Adjacent literal prints compile as one, and one that ends a line before
    `return true` compiles as Inform 6's print_ret.
  - A routine that ends by returning a constant returns it at the end of each
    branch of a trailing `switch` or `if` too, where that return is a single
    instruction.

Editor:
- After `return` in a routine that returns an enum, `bnum`, `bool`, or a union
  holding one, completion offers the possible values first (for a base `bnum`,
  those of the `bnum`s built on it), then values of that type in scope. After
  `return left | ` it offers the `bnum` values again.
- A call argument typed as a base `bnum` offers the values of the `bnum`s built
  on it, also after `|`.
- An argument or return value of a routine type (`func<…>`) offers the routines
  whose signature fits, by name.

Fixes:
- Lines an extern verb adds with `grammar +=` for a word evicted from another
  library verb and reclaimed by a native verb were discarded by the eviction.
- A method call on a cast of an array element (`((object)list[i]).has(a)`)
  was emitted as an Inform 6 method call instead of through its emitter.
- `return #beguilerSettings.x;` was rejected when the setting came straight
  after `return`.

## [0.1.0-preview.5] - 2026-10-20

Language:
- Type kinds: primitive, reference and value types, with two roots (`_bglObject`
  for object-backed classes, `_bglPrimitive` for word values); `object` is no
  longer special to the compiler.
- `struct`: value types with an automatic copy operator. A plain class-typed
  declaration owns an instance; `ref` refers to one, bound with `:=`; `:==` is
  identity comparison; copy operators on reference classes.
- Arrays own their storage: `=` copies, `ref`/`:=` shares. Local arrays and
  `<linq>` results live on a heap that is collected at the prompt (`bgl.gc`).
- `superposed` methods in object bodies are kept only when called; `<array>`
  ships only the methods a program uses.
- `property x;` declares an individual property, with no Z-machine limit.
- A class-typed object member takes its instance from a body:
  `Pet dog { "rex", 3; }`.
- `extend class` on an extern class types the properties Inform 6 provides;
  `extern` on an extend is an error.
- Also: named arguments in any order, `<=>` derives the ordering operators,
  Z-machine `try`/`catch`, `asI6` / `asBgl` name clauses, emitter chaining with
  `$i6Name` / `$i6Expr`, enum and bnum emitter methods, Glulx hyperlinks,
  `literal` values, bare `=>` lambdas, a ternary over unrelated types resolves to
  their union, and order-independent forward references throughout.
- `alias name = obj.name;` at file scope makes an object's emitters callable as
  globals. `print`, `printLine` and `printName` are global this way; `log()` is
  now `bgl.output.log()`, so authors can name objects `log`.
- `replace` keeps a member's kind: an accessor can be replaced only by an
  accessor, and a value only by a value.
- The library compiles under the same rules as any program; the compiler no
  longer asks where code comes from.
- One rule decides Inform 6 name clashes: a routine, global, array or object
  named like a member property is emitted as `_g_<name>`; a member yields
  (`_m_<name>`) only to externs, `asI6` names and other fixed names. Raw Inform 6
  names a renamed global with `$i6Name(x)`; its bare name is an error.
- A later `extern` of a routine from another file replaces the earlier
  signature, whoever declared it.
- An extension can declare int settings of its own (`extend class
  beguilerSettingsType { int x; }`) and read them as `#beguilerSettings.x`.
- A member initializer can read an extern only if it is `const`: declare an
  Inform 6 object typed by its class as `extern const Tray trayA;`.

Library (beguiLib):
- New layout: core, language extensions at the root (`#include <array>`),
  `bindings/`, `extensions/{general,stdlib,puny}/` and `libraries/`. Core no
  longer refers to any library.
- `<glulxWindow>` and `<glulxImage>` for Glulx windows and images.
- The first official extension, `<extensions/stdlib/remoteLook>`: look in a
  direction and see into the next room.
- Doc comments across the author-facing surface; article print rules moved to
  `bgl.printRules`.
- `<extensions/general/center>`: `bgl.ui.center()` and `bgl.ui.inset()` print
  text centered or between margins, word-wrapped, in any window.
- `bgl.output` holds every output routine: `print()`, `printLine()`,
  `printName()`, `spaces()` and `log()`. Types add their `print()` overloads
  there with `extend _bglOutput`.
- `bgl.ui.mainWin.width`, `bgl.ui.statusBar.width` and
  `bgl.ui.statusBar.moveCursor()` work on both targets without `<glulxWindow>`.
- `<buf>` ships only the methods a program uses.
- `<stringObj>`, an alternative to `<string>` (include one or the other): each
  `stringObj` buffer is a block in a string heap and grows as text is written
  into it, so a string is no longer limited to 494 characters. Its sizes are the
  `stringHeapSize` and `stringCaptureSize` settings, and in a debug build the
  `heap` verb reports the most the run needed, for sizing it for release.

Editor and debugger (with beguilex):
- The debugger reads arrays in their current layout, shows signed numbers,
  chars, floats, bit flags and null objects, and evaluates `obj.member`,
  `arr[i]` and `arr.length` in Watch and hover; conditional breakpoints, hit
  counts and logpoints; values can be edited.
- The language server reports warnings, renames and finds references
  case-insensitively, recovers from errors in class bodies, reads include paths
  from `#beguilerSettings`, and coalesces fast typing into one reparse.

Documentation:
- The language specification is restructured under `docs/spec/`, with
  Appendix K for gotchas; `languageSpec.md` is generated from it.

Quality:
- Hardening rounds (field trials, spec doc tests, a feature-interaction matrix,
  LSP probes, heap and measurement rounds) found and fixed several hundred
  defects; the regression suite is 998 tests.

Fixes:
- On the Z-machine, a capture or a literal longer than its string's buffer
  was written past the buffer (with `<string>`, over the next string) and then
  silently cut off. The program now stops with an error naming the setting to
  raise, on both targets. With `<stringObj>`, a long literal is kept whole when
  the heap has room.
- On the Z-machine, `bgl.ui.statusBar.id` is now an accessor that returns `0`;
  it was a plain `null` value, and code that read it as an accessor failed at
  run time.
- A method declared below its caller and returning `array<T>` keeps its
  element type in the call.
- A routine no longer sweeps fresh arrays after every statement when its calls
  return file-scope arrays, or when a local takes a call's whole result.
- An emitter body naming an Inform 6 compiler constant such as `WORDSIZE` is no
  longer rejected when a Beguile emitter shares its name.

## [0.1.0-preview.4] - 2026-09-09

Language:
- Typed properties: every property declaration now carries a type, not only
  `extern` ones. Additive and raw properties use `rawArray<T>`, and raw member
  array layout is keyed on additivity, with a correct extent.
- Member arrays: `array<T>` and `rawArray<T>` work as object and class members,
  each with a real length slot and addressed as (owner, property); an oversized
  member array is promoted to a synthesized global; `ref array<T>` members and a
  member's `as <i6name>` alias are honoured at every access site.
- References: `:=` is the reference binding (rebinding) operator; `ref` leaves a
  member empty until bound; `=` assigns through a reference, dispatching
  `operator=` into the referent. `=` on a `ref` slot is now a hard error.
- Strings: `string` is now a lightweight text pointer; owned string storage is
  `stringObj`, and owned-slot writes are enforced.
- Operators: `Type::operator <op>` is a user-facing operator reference;
  `operator <=>` may be an instance operator; overloaded operator names are
  discriminated on the instance side; and a literal argument matches an operator
  declared for its base type.
- Computed access: `obj.p` where `p` holds a property, including the statement
  form of a computed message send.
- Owning arrays: automatic allocation and destruction of element storage for
  arrays whose elements own storage; `indexOf` scoped to `length()`.
- `superposed class`: the I6 `Class` directive is withheld and emitted only when
  the name is referenced (a static instance, subclass, `new`, or `ofclass`),
  base classes first; a class nothing references costs zero bytes.
- `bgl.ui.statusBar.height` get/set property, auto-included but zero-cost when
  unused; the standard-library binding defers it to the library's status window.

Build / portability:
- The `<format>` dependency is gone: the toolchain floor is now C++17 (was C++20),
  and `std::format` is used automatically when the compiler provides it, so the
  build works at any standard from C++17 up. One build command line per platform,
  with no per-OS defines, forced includes, or library installs.
- The build tracks header dependencies for incremental `make`.

Fixes:
- `stringObj.trimRight` no longer trims from the left.
- Subscript writes work again after the operator-reference unification.

Editor / LSP:
- Starting a Run now stops any already-running Beguile debug session instead of
  stacking a second one on top.

## [0.1.0-preview.3] - 2026-08-23

Language:
- Inline object declarations — `Type{ … }` anonymous objects in expression and
  statement position: an `inline` member modifier for positional slots; comma
  separates positional values, the first `;` transitions to named `field = value;`
  members; type inference from the target (array elements, variable initializers,
  and `inject`); nested aggregate field values; and a standalone no-name form.
- Declarative array editing — `extend <array> { inject / remove / move … }`,
  applied at compile time with no runtime cost.
- `func<>` usable as a type argument (array element / member type).
- Array `+=` / `-=` operators.

Fixes:
- Subscript-then-member access on object arrays (`arr[i].member`) no longer reads
  the wrong slot (Inform 6 binds `.` tighter than `-->`; now parenthesized).
- Unicode / smart-quote handling in string literals.
- Current-file-relative (`>`) includes when the transpiled `.inf` moves to `output/`.

Editor / LSP:
- Colors the new `inline` keyword; context-aware completion for `extend` bodies
  (verb grammar, array `inject`/`remove`/`move`) and class/object member modifiers.
- Semantic tokens: multi-line interpolated strings, `#using`-imported members, and
  UTF-16 column alignment on lines with non-ASCII (smart-quote) text.

## [0.1.0-preview] - 2026-08-16

Initial public preview release.

Beguile is a modern language for interactive fiction that transpiles to
Inform 6 and targets the Z-Machine (v3/v5/v8) and Glulx. This first preview
includes the Beguiler compiler, both library bindings (PunyInform and the
Inform 6 Standard Library), and the Beguilex VS Code extension with syntax
highlighting, language support, and step debugging.

This is an experimental preview. The language and compiler are evolving
actively; expect breaking changes between preview versions.
