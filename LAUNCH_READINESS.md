# Beguile Launch Readiness

Working checklist for closing the gap between the current state and what the public
announcement promises. Goal: let people **evaluate** Beguile, **use it** in most cases,
and **report bugs**. Ordered by how early a new user hits each gap.

> Internal working notes (candid). Consider `.gitignore`-ing this file if you don't want
> the pre-launch assessment public.

## Launch-blockers - do before posting the announcement

- [ ] **Publish the GitHub release.** `v0.1.0-preview.2` is currently a **draft**
      (`isDraft: true`), so the announcement's Download links and the README's
      "binaries attached to each Release" 404 for the public. Binaries themselves
      are built (linux-x64, macos-arm64, macos-x86_64, windows-x64).
- [x] **Write `docs/quickStart.md`.** Was a 26-byte `TBD` stub; the announcement links
      it as the getting-started guide. (First pass written: download + configure a
      working environment.)
- [x] **Z-machine step-debugging works.** (Jim, 2026-08-15: no longer true that it's
      Glulx-only — step-debugging now works on **both** Z-machine and Glulx.) The old
      Beguilex `TODOs.md` note is stale; the flagship Z5 example debugs fine, so the
      "step-through debugging" headline holds for the common case. No announcement caveat
      needed. (Beguilex `TODOs.md` should be updated to drop the Z-machine-debug limitation.)
- [ ] **Ship the Beguilex `.vsix` on its release** (Beguilex repo) so install is turnkey
      (`code --install-extension ...vsix`), not build-from-source.

## High-impact - first patch week

- [x] **Silence internal warnings in the flagship example.** Done. Beguiler now filters
      BLR-internal warnings from both channels: I6 warnings that back-map into the runtime
      library (or name a `_bgl` symbol) are suppressed, and Beguile's own warnings that
      originate in the BLR are suppressed at `parsingWarning`. The I6 warning-count summary
      is reconciled to the shown count. Cloak now shows only the 3 legitimate user `parent`
      warnings; `hello.bgl` is fully clean. (Follow-up: a `-Wlib`/verbose flag could
      re-expose BLR warnings for library development.)
- [~] **Add an issue template.** **beguiler: DONE** — `.github/ISSUE_TEMPLATE/bug_report.md`
      + `feedback.md` exist (bug report asks for `.bgl`, target, version, generated
      `.transpiled.inf`). **beguilex: still open** — no `.github/ISSUE_TEMPLATE/` in the
      extension repo yet; add the same bug-report template there.
- [ ] **Add 3-4 tiny feature-showcase examples** (mutable string, LINQ chain, operator
      overload, a float) so evaluators can copy-paste and believe the feature list.
      (Jim, 2026-08-15: good idea — likely folded into the quickStart guide.)

## Medium - soon after

- [x] **orLibrary usage doc.** Done. Added a "Using the orLibrary" section to
      `docs/quickStart.md`: orLibrary is an optional add-on that ships its own Beguile
      bindings (in the orLibrary project's `src/bglBindings/`), used by adding
      `orLibraryI6/src` to `includePaths` and `#include "bglBindings/orX"` (quoted form,
      confirmed working; `<orX>` does not resolve since angle-brackets search only
      Beguiler's bundled bindings).
- [ ] **Float caveat.** Floats are Glulx-only (Z-machine has no float support); the
      announcement lists them unqualified. One-line caveat.
- [x] **Verify Z3 and Z8 targets.** Done. **Z8 builds clean** (Cloak -> `.z8`, no warnings).
      **Z3 does NOT work at all** and cannot without a core rewrite. Even a minimal
      `void Main(){ print("hi^"); }` fails, with three distinct v3-fundamental errors:
        - 68x *"A function may be called with at most 3 arguments"* - v3 call opcodes cap
          at 3 args; core routines like `_bglArray.set(obj,prop,n,v)` take 4.
        - *"Object message calls are not supported in v3"* (`CA__Pr` veneer) - **the killer:**
          v3 (1979) has no support for `obj.method(args)` at all; that veneer is v4+.
          Beguile's entire object model is method-dispatch, so this is inescapable.
        - *"Class messages are not supported in v3"* (`Cl__Ms`).
      (`_bglArray` is also emitted unconditionally rather than superposed, so even a
      no-array program pulls in the 4-arg routines.) I6 *does* support compiling for v3,
      but only for code that stays inside v3's limits (as PunyInform is hand-designed to);
      Beguile's generated code uses modern I6 features v3 lacks. **RESOLVED: Z3 dropped.**
      Removed from the announcement (by Jim), README, spec, the `eTarget` enum, and the CLI
      (`-z3`); `target = Z3` is now rejected cleanly ("Invalid target 'z3'. Must be a value
      of eTarget (Glulx, Z5, or Z8)."). Supported targets are now Glulx / Z5 / Z8. (Note:
      the `-z6` CLI leftover has also been removed.)
- [x] **`mack` (Monkeys and Car Keys) polish.** Investigated. The current build shows
      **6** warnings (down from 12), and none are defects in mack's Beguile source or in
      Beguile codegen. Classified by origin:
        - **3 from the orLibrary** (`orUtilUi.h` unused local `w`, `orMenu.h` unused local
          `t`, `orMenu.h` unused constant `orMenuTopOnly`) - third-party I6 the game only
          includes; not ours to fix.
        - **1 from `_myExtensions/myGameBaseline.h`** ("Class `person` declared but not
          used") - Jim's personal reusable base-class extension; the class is a reusable
          base this particular game does not instantiate.
        - **1 "Verb declaration no longer has any verbs / Extend only"** attributed to
          `grammar.h(381)` (the stdlib `Verb 'push' ...`): triggered by
          `_myExtensions/myExtendedGrammar.h:131-132`, which defines `Verb 'shove' = 'push'`
          / `Verb 'nudge' = 'push'` synonyms. Also a personal-extension quirk, not mack or
          Beguile.
        - **1 "statement can never be reached"** in the transpiled output: mack's own
          intentional dev toggle. `Initialise()` has `#if orBetaDev { rtrue; }` (skip the
          intro during development), and `_mack.bgl:31` sets `#define orBetaDev true`, so
          the build is in beta-dev mode. For a public build, **remove** the
          `#define orBetaDev true` line (definedness semantics: setting it `false` will not
          disable it, since `#if` tests whether the symbol is defined at all). That restores
          the intro and clears this warning; the walkthrough `wt` verb also drops out.
      Net: mack's Beguile source is clean. The residual warnings come from included I6
      (orLibrary + Jim's personal extensions) and one deliberate dev flag. If mack is cited
      publicly, either ship it with `orBetaDev` undefined (clears the one own-source warning)
      and label the rest as third-party-include noise, or extend the warning filter to cover
      user include paths (rejected here: it would also hide warnings in the author's own
      code, which they should see).

## New-user journey (what this list protects)

1. Sees announcement -> clicks Download -> **draft release = 404** (blocker 1)
2. Clicks quickstart -> **was TBD** (blocker 2, done)
3. Installs extension -> **build-from-source friction** (blocker 4)
4. Builds a Z5 game -> works
5. Tries to debug -> **Z-machine debugging broken** (blocker 3)
6. First impression -> **flagship example has 7 warnings** (high-impact 1)
7. Reports a bug -> Issues works, but **no template** (high-impact 2)

## Verified working (the foundation is real)

- Language + features (mutable strings, operator overloads, lambdas, LINQ, floats,
  arrays/buffers) present and covered by the test suite (163/0/0).
- Both modes (precompiler, stand-alone) work.
- Both bindings ship (`punyInform`, `i6StandardLibrary`); Z5/Glulx well-exercised.
- Both claimed ports exist and build: Cloak of Darkness (`examples/`) and Monkeys and
  Car Keys (the `mack` port in `WIP/`).
- Release binaries are actually built for all four platforms.
