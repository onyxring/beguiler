# Remaining Work

A living checklist of outstanding language/compiler and BLR work, grouped into two
buckets. Items are roughly ordered by priority within each bucket. Update as work lands.

_Last reviewed: 2026-08-08._

---

## 1. Compiler / language

- [ ] **Verb multi-bucket `Extend` ordering.** Uncommitted i6Emitter change: emit one
      `Extend` directive per priority bucket (descending order for `first`, ascending for
      default). Design agreed; needs finishing + tests.
- [ ] **`abstract` member feature.** Deferred behind Jim's exploration (from the verb
      handler/perform redesign).
- [ ] **Compiler/BLR coupling audit.** 14 items where the C++ compiler hardcodes BLR type/class
      names; some defensible, some refactor candidates. **Citations refreshed 2026-08-08** (see the
      audit memo). **#7 done** — the dead `bglWorld.ofClass` guard was removed (BLR long ago renamed
      it to `bgl.world.instances`) and the stale spec §16.4 rewritten to the real `getAll`/`inParent`/
      `instances` surface. Next candidates: #3 (attributeList → I6 `has` via a BLR class hook; the
      `={a,b,!c}` model already simplified the pipeline), then #1 (finish object fallbacks), #5
      (parent positional). Full walkthrough still pending.

### Paused (do not push — wait for Jim to re-engage)
- [ ] **Class-typed locals backing decoupling from `object`.** Remove the `inheritsFromObject`
      gate from backing synthesis (audit items #1/#2). Jim paused this pending his own design
      re-engagement; the last proposal on the table was a `default` qualifier on object's op=.

---

## 2. BLR (Beguile Language Runtime)

  > **Ephemeral-array model (design record, 2026-08-08).** Local arrays returned from a routine
  > are *ephemeral*, like ephemeral strings — not a compiler bug. A local array's framePool slice
  > is freed at routine exit; the free is *logical* (the slot isn't zeroed), so a returned pointer,
  > or a local array read inside a `return` expression, refers to already-freed storage. It usually
  > *appears* to work because the data lingers — failures are intermittent, surfacing only when an
  > intervening allocation reuses the slot. **Rule for authors: assign a returned/ephemeral array
  > to a typed local to keep it — that copies it** (value-semantic copy-on-assign, now implemented).
- [ ] **glulxWindow finish/remodel.** Now compiles clean on Glulx after `ref` members landed
      (`ref _glulxWindow parentWin`). Remaining: verify runtime behavior in a real Glk window,
      and confirm the `operator=` / `tmpWin` copy pattern is still the model we want.
- [ ] **orString port phases 3b–6.** Phases 0/1/2/3a landed (`_bglString` class shell with
      `#i6` stub bodies). Remaining: 3b+ convert each method body to Beguile-native; 4–6 =
      storage decls, `_bglStringPool`, init function.
- [ ] **`_myExtensions` off orLibrary.** Migrate the personal extensions to depend on the
      ported Beguile-native stdlib rather than raw orLibrary I6.
- [ ] **Base-class split: world-tree methods vs. utility objects.** `object`'s world-tree
      surface (remove/give/has/…) leaks into utility classes like `_bglArray`. One-line
      `replace` workaround exists; revisit on next collision or a hierarchy refactor.

---

## Recently completed (for context — remove once stale)

- **Doc-comment promotion + stale `<glulx>` scrub (2026-08-08).** Two BLR doc hygiene items.
  (1) **Doc-comment promotion:** `punyInform.bgl`'s `grammarToken` enum values (trailing `//`) and
  all `_bglFloat` methods in `float.bgl` (block-above-group `//`) promoted to `///` so LSP
  completion/hover carry documentation. (The stdlib binding's `grammarToken` was already `///`.)
  Verified the capture chain end-to-end: lexer accumulates `///` into the next token's `docComment`
  (fileLexer.cpp:751-830, blank-line orphan guard), parser transfers it to enum-value
  (bglParserDecls.cpp:79) and function/method (613-614) decls — the same proven path the stdlib
  binding uses. Final hover/completion render is best re-checked with Jill's JSON-RPC harness.
  (2) **Stale `<glulx>` cleanup:** the `<glulx>`/`<zcode>` includes never existed as files — the
  platform core (`_glulxCore`/`_zcodeCore`) is auto-selected by target in `__beguileCore.bgl`
  (`#if TARGET_GLULX`). Scrubbed the phantom-include claims: rewrote the `_glulxCore.bgl` and
  `_zcodeCore.bgl` headers (the latter was a copy-pasted "glulx.bgl" header), fixed the
  contradictory "opt-in `#include <zcode>/<glulx>`" comment in `__beguileCore.bgl`, and deleted the
  dead commented-out platform-include block in `buf.bgl` (author's own "TODO delete"). Only
  `<glulxWindow>` is a real opt-in Glulx extension. All changes are comments/dead-code — emission
  byte-identical (exact-match baselines hold); suite 159/0/0; mack 25/25; Glulx core parses clean.
- **Verb synonyms + DM4 §30 gaps closed (2026-08-08).** New `extend V { synonyms = {.w1, .w2}; }`
  form emits I6's compact `Verb 'w1' 'w2' = 'anchor';` — the listed words become **true aliases** of
  the anchor's grammar table (a later `Extend` on the anchor flows to them automatically), distinct
  from copying grammar lines. Works on extern (stdlib) and native verbs; anchor's primary word
  resolved at emit; single-char `//` + plural `//p` flags handled; empty list errors. New AST node
  `verbSynonymDecl` (typeDef.h), parsed in the extend-body handler (bglParserClassObjectDecl.cpp),
  emitted as a globals-level node so it lands after the anchor's own `Verb` directive
  (i6Emitter.cpp `emitVerbSynonym`). This closes the DM4 §30 item: the `grammar =` "whole-verb vs
  per-trigger-word" asymmetry was resolved by **documentation, not the emitter refactor** (option 3
  rejected — it would cost Beguile's deliberate per-trigger-word distinct-pattern expressiveness);
  spec now frames `grammar =` as per-trigger-word replace + add-as-new and points at `synonyms =`
  for shared identity. `Extend only` stays deferred to `#i6 { }` (documented, rare). Tests
  `test_verb_synonyms` + `_test_verb_synonyms_empty` + `_test_verb_synonyms_non_verb`; suite
  159/0/0; mack 25/25. Spec §14.4 "Verb Synonyms". **Follow-up resolved 2026-08-08:** the "spurious
  not-a-property" diagnostic Jill was sent to investigate turned out to be compiler-side, not
  extension-side (all `.bgl` diagnostics come from the parser's `lspErrors`). Two causes: (a) the
  original squiggle on genuine verbs was a stale LSP binary pre-rebuild; (b) a real gap — `synonyms`
  recognition was gated on `vod != nullptr`, so on a **non-verb** target it fell through to the
  generic "not a property" error. Fixed by decoupling recognition from verb resolution: `synonyms`
  is now recognized in any extend body, builds the `verbSynonymDecl` only when the target is a verb,
  and otherwise gives a targeted "only valid when extending a verb" error (guarded by
  `_test_verb_synonyms_non_verb`). Optional beguilex nicety still open: contextual keyword
  highlighting for `synonyms`/`grammar`/`priority` inside extend bodies (Jill offered) — same spirit
  as `#i6replace` highlighting.
- **Object-member-without-cast diagnostic — closes the parentProp item (2026-08-08).** Strict-mode
  field access on an `object`-typed receiver whose member isn't on `object` (e.g. `r1.parent.cap`)
  now errors *at the member* with a cast hint instead of silently dropping `.cap` and surfacing the
  misleading "Cannot assign value of type 'object' …" downstream. General (keyed on receiver-is-
  `object`, not parentProp-specific); loose `#bgl` islands unaffected; the method form already
  errored accurately. `bglParserExpr.cpp` field-chain fallthrough. Test
  `_test_object_member_no_cast.bgl`; suite 156/0/0; mack 25/25. Spec §11.5.1. With this + instance
  casts, the parentProp/dynamic-parent item is CLOSED — casts cover every access, and forgetting one
  is now a clear error. Optional future *enhancements* were explored but are **not planned** (casts
  already cover the need): (a) `as<T>` safe checked downcast (`x.parent.as<Room>` → `Room`/`nothing`,
  the safe path for *mobile* objects); (b) `parentProp<T>` generic member + `replace` — narrow the
  parent's read/write type per class (`replace parentProp<Room> parent;`), enforced for fixtures that
  only ever live in one type; (c) `parentProp<auto>` — per-instance read-type inferred from the
  declared parent, reads-only. Revive from git history if a concrete need appears.
- **Instance casts — `(instanceName)expr` (2026-08-08).** A cast target can now be a specific object
  instance, not just a class: `((library)x).shelves` reaches the instance's *own* members
  (instance-only fields), plus inherited class members, type-checked against the instance. Cast
  parser detects `(` ident `)` where the identifier names an objectDef and an operand follows
  (grouping `(obj) == y` / `(obj);` unaffected); the objectDef type is preserved (not collapsed to
  its class); the field chain resolves objectDef members. Unchecked downcast (asserts runtime type),
  but the member name is checked — the type-safe alternative to `(var)`. Tests: `test_instance_cast`
  + `_test_instance_cast_bad_member`; suite 155/0/0; mack 25/25. Spec §11.5.1.
- **Opt-in auto-abbreviations — `#beguilerSettings.economy` (2026-08-08).** When true, the compiler
  computes optimal I6 text abbreviations (`inform6 -u` pre-pass), injects them, and compiles with
  `-e`. **≈13.8 KB saved on mack** (258,560 → 244,736 B). Non-intrusive: skipped if the source
  already has its own `Abbreviate` directives (author's set respected); off by default; no-op when
  `informName="none"`. Wired: typeDef.h + `_beguilerSettings.bgl` schema + parse + beguiler.cpp
  two-pass. Spec settings table updated. (mack left economy OFF — author's per-project choice.)
  This is the real Z5-headroom lever; the LINQ split below is complementary hygiene.
- **LINQ split into `<linq>` (2026-08-08).** LINQ (chain surface + `_bglArray` non-terminals/terminals
  + depth-stack scratch) moved from `<array>` to a new `<linq>` (`beguiler/beguiLib/linq.bgl`, which
  `#include <array>`). `<array>` keeps base ops + copy-on-assign. Flags de-conflated (`<array>`→
  arrayInUse, `<linq>`→linqInUse); `bglWorld` pulls only `<array>`; "No method 'filter'…" now hints
  `#include <linq>`. Array/bglWorld programs stop paying for LINQ — **mack shed ~2.9 KB** (Z5 free
  884 → 3796 B) with zero chain use. Suite 153/0/0; mack 25/25. Spec §17.2.6/§17.2.6b updated.
- **Nested LINQ chains + array copy-on-assign (2026-08-08).** LINQ scratch is now a depth-indexed
  stack of buffer pairs (`array.bgl`): a chain inside another chain's lambda runs at a deeper depth
  and can't corrupt the outer (default 1 level of nesting, `_BGL_LINQ_MAXDEPTH=2`; deeper → runtime
  guard). Retires the LINQ-nesting-enforcement item — nesting *works* instead of being rejected.
  `array<T>` gained value-semantic **copy-on-assign** (`_bglArray.copy` + `operator=` emitter, reached
  via getDispatchClass at both assignment paths + a template-aware operator= match) — the capture
  mechanism for the ephemeral-array model. Scoped out: `array<char>` (byteArray) value-copy is a clean
  compile error (word-copy would corrupt bytes; byteArray length/`$prop` infra is a separate gap).
  Tests: `test_linq_nested_chain`, `test_array_copy_on_assign`, `_test_array_char_copy_unsupported`;
  suite 153/0/0; mack 25/25 (Z5 story now 261632/262144 B — tight). Spec §17.2.6 + §4.7.2 updated.
- **`#i6replace` beguilex highlighting (Jill, 2026-08-08).** TextMate grammar entry added in
  `beguilex/syntaxes/beguile.tmLanguage.json` (keyword + routine/saved-name operands colored;
  `;` and trailing `//` left to normal grammar). No LSP change needed — the raw-I6 skip already
  fires only on `#i6` + whitespace/`{`, so `#i6replace` scans as ordinary Beguile. Uncommitted in
  the beguilex repo (no-Claude-trailer convention applies).
- **mack implicit-take plural — NOT a compiler/port bug (closed).** `put rocks in barrel`
  (rocks on ground) gives "You are not holding one" instead of implicit-taking. Verified the
  pre-port original I6 mack behaves *identically* — it's standard I6 parser behavior (plural /
  `all` for `put`/`insert`/`drop` only scopes carried items; only the singular path reaches
  `ImplicitTake`). Not the emitted code. Tracked as a mack game-level item in
  `WIP/begMack/TODO.md`; removed from this compiler list. (2026-08-08)
- **`#if` define-timing consistency** — the main pass no longer inherits the pre-scan's
  end-of-file define table; it re-derives source `#define`s linearly, so `#if` is
  position-dependent and identical across both passes (a late `#define` no longer
  retroactively enables an earlier `#if`). `#undef`/redefine mid-file preserved.
  Guarded by `test_if_define_timing.bgl`. (2026-08-08)
- **`#i6replace` directive** — hoisted I6 `Replace`; implemented in mack; spec §15.5.1. (2026-08-08)
- **`ref` members** — pointer-alias member fields, no backing synthesis; fixes glulxWindow's
  self-referential `parentWin`. Spec §10.2.2. (2026-08-08)
- **Chaining sweep** — fixed field-read through class-typed members (`self.x.y` no longer
  emits `self.x.self.y`; external `o.a.b` no longer drops the tail) and member/method names
  colliding case-insensitively with type names (`t.color`, `wn.draw()`, `t.object`). Spec §2.4. (2026-08-08)
- **parentProp read/compare/chaining** generalized with a BLR-derived invariant guard;
  `rawArray<T>` untracked literals; `//p` plural dict flag; `#i6` placeholder claim by source
  location; parser hardening (object-returning fn before class; apostrophe in `!` comment
  inside `#i6` in a method body).
