# bglStdLib - the Beguile Standard Library (BSL)

A foundational, batteries-included library for parser interactive fiction,
written in Beguile. This is the modern, rich tier of the Beguile ecosystem;
the constrained/retro tiers are served by other binding sets (see below).

**Status:** design + initial scaffolding. Driven by `WIP/bslCoD/cloakBSL.bgl`
(test-driven library design: write the game we want, then build the library to
satisfy it).

---

## Positioning - the three-tier story

An author picks a tier by choosing a binding/library set; no single library has
to compromise for the others:

- **Beguile + puny bindings** -> tiny z3/z5, retro / 8-bit niche.
- **Beguile + i6 stdlib bindings** -> classic z5/z8/Glulx, Inform-compatible.
- **Beguile + bglStdLib (this)** -> **Glulx only**, modern, graphics-first.

## Design pillars

1. **Glulx only.** 32-bit VM: ample memory + heap, full-width signed ints and
   floats, Unicode. This removes the Z-machine memory ceiling that would
   otherwise constrain runtime-dynamic rulebooks, and sidesteps the arithmetic
   limits that dogged 16-bit systems.
2. **Graphics / sound / windows / hyperlinks are first-class**, not bolted on.
   The output layer is written against a Glk-aware display abstraction from day
   one (no text-first retrofit). A differentiator no incumbent parser library
   occupies cleanly.
3. **No special compiler treatment.** The entire library is ordinary Beguile on
   generic beguiler - classes, arrays, `func<>` values, enums, `extern`
   bindings. When we hit something the compiler cannot express cleanly, we add
   the *generic* language capability (benefiting everyone), never a
   library-aware special case. This is also the strongest possible validation
   of the language.
4. **Everything legible and moddable.** The pipeline is data (arrays of rules),
   not hardcoded control flow. The LSP can show a rulebook's resolved order and
   the debugger can step it - the "why did that rule fire?" answer I7 never had.

## The rule model (locked 2026-08-22)

The action pipeline - and the game loop itself - are **rulebooks**: arrays of
rules run by a generic runner (the `orRoutineList` accumulate/until/while
strategies, typed to `eVerdict`). Rules can run other rulebooks, so the engine
is rulebooks all the way down; the action pipeline is a sub-book invoked by one
rule in the loop.

- **Outcome:** every rule body returns
  ```
  enum eVerdict { proceed = 0, stop = 1, block = 2 }
  ```
  - `proceed` - no decision; fall through to the next rule. **Falling off the
    end of a body = proceed** (I6 default 0), so no-op rules need no `return`.
  - `stop` - handled; halt this book; the action succeeds.
  - `block` - veto; halt; the action fails (the rule already printed why).
  - The `= 0/1/2` numbering makes the legacy `bool before()`/`after()` bridge
    automatic: `rfalse`(0) -> proceed, `rtrue`(1) -> stop, with no glue.
    `block`(2) is the new capability the old bool could not express.

- **A rule entry** is a `{ verb, matcher, body }` tuple, by direct analogy to
  `grammarRule { verb, pattern }`:
  ```
  before        += { {drop, shirt, ()=>{ print("floop."); }} };   // global book
  shirt.before  += { {drop,        ()=>{ print("floop."); }} };   // object-local (receiver = matcher)
  garment.before+= { {drop,        ()=>{ ... }} };                // class-level (all garments, inherited)
  ```
  - `matcher` is an `object` (identity match) or a `class` (`.is()` test, §7.8).
  - Body is a zero-arg `func<eVerdict>` that reads the action globals
    (`action`, `noun`, ...). Zero-arg + globals keeps it **capture-free**, the
    only safe way to store lambdas long-term (§10.8.4).
  - Legacy one-big-`switch(action)` `before()` methods fold in as a single
    catch-all rule; both spellings feed the same runner.

- **Three attach points** replace I7's hidden specificity engine with Beguile's
  real inheritance + visible array order: instance / class / global book. Class
  and instance rules both fire (append, not override); order is array position.

## The pipeline stages (per action)

`rewrite -> verify -> before -> preconditions(implicit actions) -> check ->
carryOut -> after -> report -> react`, then end-of-turn (`everyTurn` + daemons).

- **verify** is the one stage that differs: pure, called many times during
  disambiguation, returns a *ranking* (not `eVerdict`) so the parser can pick
  the logical object, defaults, and gate implicit-action targets (TADS's jewel).
- **before/instead unified:** no separate "instead" book; a `before` rule that
  returns `stop`/`block` *is* an instead. One book, stopping is a return value.

## beguiler: func<> gaps — fixed and remaining

Building the engine surfaced real language gaps around `func<...>` (the intended
"friction -> fix generically" signal). Status as of 2026-08-22:

**FIXED (compiler changes landed + full suite green, 177/0):**
- `func<...>` as a **generic type argument** — `array<func<eVerdict>>` now parses
  (routed through `parseFuncType` in the array-decl, parameter, and prescan
  paths).
- **`>>` in nested generic closes** — `array<func<T>>` / `func<func<T>>` (the
  latter was silently broken before): the lexer now splits a trailing `>>` into
  two `>` (`fileLexer::pushBackCloseAngle`), only when the type parser asks, so
  `>>` stays a shift operator everywhere else.
- **for-in over `array<func<...>>`** — the loop-var type parse now handles
  `func<...>` and preserves the parameterized type so the var is callable.

Rulebooks are therefore now the clean, typed `array<func<eVerdict>>` (no more
`array<var>` workaround).

**ALSO FIXED 2026-08-23 (suite still 177/0):**
- **Direct-calling a for-in func loop var** — `for(func<E> r in bk){ r(); }`
  now works. Root cause was the for-in loop var being added as a `forCtx` param
  typed via `getType()`, which returns the base `func` and drops the
  parameterization; now the full `func<...>` name is preserved (three for-in
  paths + the object/class member paths). `runRulebook` now uses the direct
  `r()` call (no more assign-to-local).
- **`func<...>` as a class/object member — now FULLY functional.** Declare,
  assign, read, and call directly (`obj.handler()` → indirect call through the
  property), plus `array<func<...>>` members and iterating them. Fixes spanned
  the parse (`processTypedMember` + class-body parser + `processArrayMember`),
  parameterized-name preservation in `processMemberVariable` and the class
  variable-member path, and a new func-property-call branch in `bindMethodCall`
  (synthesizes a functionDef so the caller emits `obj.member(args)`).

**STILL OPEN:**
1. **`{verb, matcher, body}` literal construction** — needs generic positional
   aggregate init for a user class (grammarRule's tuple-init is
   compiler-special-cased; BSL must not rely on that).

**Noted (pre-existing, NOT func-specific):** runtime `+=` on a *member* array
(`obj.arr += {x}`) emits invalid I6 for **all** element types (int included) —
a general member-array-mutation gap, orthogonal to func members.

> WARNING: the compiler-engineer *agent* that was first asked to do the func<>
> fix FABRICATED its entire report (claimed edits/tests it never ran —
> `tool_uses: 0`). The fixes above were done and verified by hand. Treat future
> agent claims as unverified until confirmed by a real build + suite run.

## Open items

- Body-return: settled on `eVerdict` (not `bool`).
- World model (`thing`/`room`/attributes/scope), standard verbs, the runner's
  other per-book combine strategies (run-all, verify-accumulate), the rule
  entry (`{verb, matcher, body}`), and the Glk input/output layer are still to
  design. Real Glulx input needs a Glk event loop (no runtime shortcut:
  `bgl.ui.waitForKey()` is Z-machine only).

## Layout

Lives at `beguiler/beguiLib/bglStdLib/`, i.e. a subfolder of the Beguile
Language Runtime tree (`beguiLib/`). Because `#include <name>` searches that
tree recursively, the library is pulled with `#include <bglStdLib>` (the
umbrella) - no `includePaths` entry needed.

Files live flat directly under `bglStdLib/` (no subfolders). Each has `#once`;
the umbrella orders them (siblings do not cross-include, matching core's
`__beguileCore.bgl`).

- `bglStdLib.bgl` - umbrella include (pulls the files in dependency order).
- `_verdict.bgl`  - the `eVerdict` outcome type every rule body returns.
- `_rulebook.bgl` - `runRulebook`, the generic stop-on-first-decision runner.
- `_gameLoop.bgl` - `rootRulebook` / `gameTurnRulebook` + the `main()` entry.
- (more to come: world model, pipeline stages, verbs, output layer.)
