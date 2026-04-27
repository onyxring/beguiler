# Beguile Compiler — Gotchas

Non-obvious rules, hidden invariants, and historical hazards. Read [`ARCHITECTURE.md`](ARCHITECTURE.md) and [`NAME_RESOLUTION.md`](NAME_RESOLUTION.md) first; this doc lives below them.

Each entry is a thing that has bitten us — or that *will* bite the next person to touch this code. Add an entry every time something surprises you.

---

## Lexer / source handling

### Case-insensitive lexer

[`fileLexer.cpp:625`](../fileLexer.cpp#L625) lowercases every token's `value` before the parser sees it. The pre-lowercase form lives in `originalValue`.

**The trap**: searching the codebase for `MyName` won't find a Beguile-side reference to it — the parser sees `myname`. When comparing tokens against fixed strings, always use the lowercase form. When emitting back to the user (errors, IDE diagnostics, verb names that I6 cares about), use `originalValue` or the `displayName` field on AST nodes.

### `!` comments inside `#i6{}` blocks

[`fileLexer.cpp:417` `getRawTextThroughClosingBrace`](../fileLexer.cpp#L417) reads raw I6 between braces. It must skip:
- `//` line comments
- `"..."` strings
- `'...'` character/dictionary literals
- **`!` line comments** (the I6 native comment)

If you forget the `!` skip, an English contraction in any I6 comment (`don't`, `aren't`, `player's`) opens a runaway char-literal scan that swallows braces in unrelated code far below — surfaced as a phantom "missing closing brace at EOF" error pointing at the wrong file. The full I6 standard library has dozens of such comments.

### Stale `currentStatementSrc`

`bglParser::currentStatementSrc` is set at the start of every `processStatement` call so error messages can point at the correct line. It's cleared on `closeCompileContext(codeBlock)` in [`bglParser.cpp:closeCompileContext`](../bglParser.cpp).

**The trap**: forgetting to clear means a global-scope error fired *after* a function body parses reports the line of the last in-body statement, not the actual offending construct. Negative test [`_test_source_location_after_function.bgl`](../tests/_test_source_location_after_function.bgl) locks this in.

### `#i6{}` ↔ `#bgl{}` interleaving

Inside a `#i6{}` raw block, `#bgl{ ... }` switches back to Beguile parsing for the contents, then back to raw I6 on the closing `}`. Brace counting must work in both directions. Handled by [`fileLexer.cpp:480` `getRawTextUntilCloseOrBgl`](../fileLexer.cpp#L480). Any rewrite of either reader must preserve the depth-tracking handshake.

---

## Type system / object model

### Implicit `object` parent class

When the parser sees `object foo {…}`, it sets `foo.objectClass` to the *built-in* `object` class — **not** to `nullptr`. So checks like:

```cpp
od->objectClass ? od->objectClass->name : od->name
```

…always return `"object"` for any plain object declaration. The intent of "use the explicit class if there is one, else the object's own name" requires the extra guard:

```cpp
bool explicitNonObjectClass = od->objectClass && od->objectClass->name != "object";
explicitNonObjectClass ? od->objectClass->name : od->name
```

This is wired correctly in `resolveIdentifierType` and `qualifyIdentifier` today. If you add a third place that asks "what's this objectDef's type?", apply the same guard or you'll silently get `"object"` everywhere.

### Pre-pass stubs

The parser runs a pre-scan ([`bglPreScanner.cpp`](../bglPreScanner.cpp)) that registers stub declarations so forward references work. Stubs have `isPrePassStub = true`. The full parse pass is supposed to *replace* stubs with real definitions, but in some paths a stub and a real definition coexist for the same name (especially across different categories — e.g. a `verbObjectDef` and a stub `variableDeclaration` both named the same thing).

When walking `languageService.globals` looking for matches, dedup carefully: in the strict-ambiguity resolver, candidates that produce the same I6 emission and same type are deduplicated at [`bglParser.cpp` qualifyIdentifier dedup block](../bglParser.cpp). If you skip the dedup, you'll get spurious ambiguity errors like *"'X' is ambiguous: matches global variable 'X' and verb 'X'"* — where both candidates are actually the same registration seen via two paths.

### `auto` infers from RHS `resolvedType`

[`bglParser.cpp:processVariableDeclaration`](../bglParser.cpp) sets `varDecl.type` from `rhs->resolvedType` when `auto` is declared. If the expression node doesn't set its `resolvedType`, `auto` either infers wrong or errors with *"Cannot infer type from initializer expression"*.

**Rule of thumb**: every place that constructs an `expression*` and assigns to a token list must also set `resolvedType` if the value is known. The list of literals/operations that already do is large; new emission paths often miss it.

---

## Operator dispatch

### Operator routine names need mangling

I6 doesn't accept `=`, `+`, `?` as routine names. When a class declares operator methods, the I6 emission uses a mangled name in `i6name`: `_opeq`, `_opadd`, `_opqry`, etc. Mangler at [`bglParser.cpp:mangleOperatorName`](../bglParser.cpp).

This is set automatically at `processRoutineDeclaration` time *only if* the name starts with a non-identifier char. If you add a new operator-emission path that doesn't go through `processRoutineDeclaration`, you must either reuse `mangleOperatorName` or your emitted I6 will have invalid identifiers. **Symptom**: I6 compilation errors like *"unknown identifier '='"* with very little context.

### `var` is a symmetric escape hatch

In binary-operator resolution, `var` matches any type — *both* directions:
- An operator's parameter typed `var` matches any RHS type.
- An RHS resolved as `var` matches any operator parameter type.

If you add a new operator-resolution path and only check one direction, you reintroduce the bug where `int - var` (e.g. an undeclared identifier on the RHS) errors with *"No operator '-' on type 'int' accepting 'var'"*. See [bglParser.cpp:applyBinaryOperator](../bglParser.cpp).

### Non-emitter `operator =` dispatches via `_opeq`

A non-emitter `operator =` defined on a class is rewritten at the assignment site as `lhs._opeq(rhs);`. The synthesized emitter body uses **`$target`** (full LHS path) — *not* `$self` (owner). This means `obj.field = value` correctly emits `obj.field._opeq(value)`, dispatching on the property, not the owner.

If you "clean up" the assignment-statement emitter and substitute `$self` instead, dotted assignments silently dispatch on the wrong receiver. Test [`test_operator_equals_dispatch.bgl`](../tests/test_operator_equals_dispatch.bgl) locks the three LHS shapes.

---

## Z-machine specifics

### Local-variable spill on Z3/Z5/Z8

Z-machine routines have a hard limit of 15 locals. Beguile spills overflow into a frame pool array (`_bglFrm`) and excess parameters into globals (`_bglXP0`, `_bglXP1`…). Spill state is held in `i6Emitter::currentSpillAliases`, `currentSpillCount`, `framePoolSize`.

The relevant predicate is `funcNeedsSpill(fd)`. When true, every local-name emission must go through `spillName(name)` instead of `name`. Forgetting this in new emitter code → I6 references to undefined locals — which the I6 compiler may or may not catch depending on the surrounding context. **Symptom**: working program on Glulx (no limit), broken on Z5 with cryptic I6 errors or runtime garbage.

### `Object` declaration syntax differs by target

I6 `Object` declarations work on both Z and Glulx, but property defaults, attribute counts, and array layouts differ. Code that emits literal I6 must check `currentTarget` for target-conditional output. Most paths handle this via the existing emit helpers; new emitter code should look for an existing helper before adding a new target switch.

---

## Source-mapping / debugging

### Source map drives I6 diagnostic remapping

`i6Emitter::sourceMap` is `vector<tuple<int, string, int>>` — `(i6Line, bglFile, bglLine)`. When the I6 compiler reports an error, the driver in `beguiler.cpp` remaps that line back to the original `.bgl` source position.

If you add new emission code that writes multi-line text without pushing entries to `sourceMap`, I6 errors inside that range point at the wrong line (or the wrong file entirely). Use [`emitRawTextWithSourceMap`](../i6Emitter.cpp) for any chunk longer than a single line — it handles the bookkeeping.

---

## Name resolution

### Lexical scope absolutely shadows file scope

The new strict-ambiguity rule applies *only* at file scope (Tiers 4+). Lexical scope (params, locals, current object/class members) still uses first-match-wins — that's how scoping should work. A local `int x` legitimately shadows a global `x`, with no error.

If you "improve" the resolver to detect *all* shadowing as ambiguity, you'll break ordinary block scoping and fail tests like [`adversarial/adv_name_collision.bgl`](../tests/adversarial/adv_name_collision.bgl). The split is intentional — keep it.

### `currentExpectedType` must be saved/restored

Set by call sites that know the type they need (variable initializer RHS, assignment RHS, operator RHS, default parameter values). Read by the resolver as a tie-breaker.

`parseExpression` uses an RAII guard that restores the saved value on any return path. Sub-contexts that *break* the type context (function call args via `parseCallArgList`) save/clear/restore explicitly.

If you add a new sub-expression context (a new statement form with embedded expressions, a new operator that takes a different-typed RHS), you must decide:
- Inherit the outer context? Probably wrong — you'll silently bind wrong identifiers in unrelated subexpressions.
- Clear it? Use the save/clear/restore pattern from `parseCallArgList`.

### `qualifyIdentifier` and `resolveIdentifierType` must agree on tier

These two functions walk the same tiers in the same order to ensure the type and the I6 emission describe the same declaration. If they ever pick different tiers for the same name — say, you optimize one but not the other — the emission and the type type-check against different things, producing very confusing errors downstream.

When changing tier ordering or candidate-collection logic, update *both* functions in lockstep.

---

## Build / test infrastructure

### `_test_*.bgl` files are negative tests

The `_` prefix excludes them from the positive-baseline pass and includes them in the negative-test pass. Each must declare one or more `// EXPECT_ERROR: <substring>` headers — every substring must appear in stderr for the test to pass.

If you accidentally name a positive test with a leading underscore, it'll be treated as a negative test, fail to compile (or fail the EXPECT_ERROR check), and report a confusing failure. See top of [`run_tests.sh`](../tests/run_tests.sh).

### Recapturing baselines

`./run_tests.sh --capture` re-captures the positive baselines. **Always investigate diffs before recapturing** — most "innocuous" emission diffs are intentional behavior changes that you should be able to explain, but a few are real regressions hiding behind plausible-looking output. The `currentExpectedType` work, the auto-backing rule, and the value-emitter inlining all produced legitimate baseline shifts that needed re-capture.

---

## When in doubt

- Look at the test that exercises the area you're touching. If there isn't one, add it before making the change. The test makes the regression visible the next time someone "innocently" rewrites your code.
- The strict-ambiguity rule and the type-identity rule are *load-bearing*. If a change you're making would break either, you're probably solving a different problem.
- Ask: does this change interact with the lexer's case-folding? If yes, double-check `originalValue` vs `value` usage at every touch point.
