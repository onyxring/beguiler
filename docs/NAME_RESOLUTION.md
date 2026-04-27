# Beguile Compiler — Name Resolution

This is the deep dive on how Beguile turns identifiers in source code into the right declaration. Read [`ARCHITECTURE.md`](ARCHITECTURE.md) first — this document assumes you know what `languageService.globals`, `currentClass` / `currentObject` / `currentFunc`, and the compile-context stack are.

The rules described here have evolved across many small changes. The goal of this document is to make those rules legible *as a system* — so you can predict what the compiler will do without spelunking through `bglParser.cpp`.

---

## The shape of the problem

Beguile source contains identifiers. Most of them are unambiguous. Some aren't:

```beguile
class point { int x; int y; }
point p;
p.x = 5;             // 'p' could be a local, param, member, global, enum value, etc.
                     // We need to pick the right one — and produce the right I6 emission.
```

Two things have to happen for every identifier reference:

1. **Type resolution** — what is `p`'s type? (Drives method dispatch, type checking, conversion.)
2. **Qualification** — what I6 text should we emit in place of `p`? (`p`? `self.p`? `_bgl_classname_p`?)

These are answered by two functions that walk the same tier hierarchy in (mostly) the same order:

| Function | Returns | Lives at |
|----------|---------|----------|
| [`resolveIdentifierType`](../bglParser.cpp#L2331) | The Beguile type name (e.g. `"point"`, `"int"`, `"_glulx"`) | `bglParser.cpp:2331` |
| [`qualifyIdentifier`](../bglParser.cpp#L2846) | The I6 emission text (e.g. `"p"`, `"self.p"`, `"_bgl_widget_count"`) | `bglParser.cpp:2846` |

Each is also given a `memberHint` that biases the result toward a candidate whose type actually exposes a named member — see *Member-aware tie-breaking* below.

A third helper, [`resolvePathType`](../bglParser.cpp#L6395) at `bglParser.cpp:6395`, handles dotted paths (`obj.prop`) by qualifying the head and then walking instance/class members.

---

## Two scopes, two rules

The tiers split into two groups with **different resolution rules**:

- **Lexical scope** (Tiers 1–3): the names visible in the current code — params, locals in nested blocks, members of the enclosing object/class. Inner shadows outer by design. **Rule: first match wins.**
- **File scope** (Tiers 4+): enum values, globals, verbs, emitter objects, `#define` symbols, `#using`-imported members. These all live in one conceptual namespace; they don't shadow each other. **Rule: collect all matches; if more than one survives the tie-breakers, error and force explicit qualification.**

This split matters because lexical scoping is *about* shadowing — `int x = 5;` in a method body should win over a global `x` without any compiler complaint. But two enum values both named `Red`, or an enum value named `Glulx` colliding with a `#using bgl`-imported member, are accidents, not intentional shadowing. Strict ambiguity at file scope catches them.

## Tier order

| Tier | Scope | Matches | Used at |
|------|-------|---------|---------|
| 0    | Special | `"null"`, `"self"` | Top of both functions |
| 1a   | Lexical | Function parameters of the current function | `func->params` |
| 1b   | Lexical | Locals declared in the current statement block | `body->statements` |
| 1c   | Lexical | Locals in ancestor blocks (walks `activeBlockStack`) | `if` / `for` / `while` nesting where the local is declared in an enclosing block |
| 2    | Lexical | Members of `currentObject` (when inside an `object` body) | Direct members, no hierarchy walk |
| 3    | Lexical | Members of `currentClass` and its base hierarchy | Direct first, then `baseClasses` recursively |
| 4    | File    | Enum value names (every enum containing the name produces one candidate) | Walks `languageService.objectTypes` for matching `enumDef`s |
| 5    | File    | Globals from `languageService.globals` (variables, functions, objectDefs) | First match by `g->name == name` |
| 6    | File    | Verb names (action constants like `##Take`) | `languageService.verbs` |
| 7    | File    | Global emitter objects (e.g. `bglOpCodes`) | `cd->isGlobalEmitterObject` or `cd->isEmitterClass` |
| 8    | File    | `#define` symbols | `definedSymbols` map |
| 9    | File    | `#using`-imported scopes | `usingImports` (classes) and `usingObjectImports` (objectDefs) |
| 10   | Lexical | Lambda closure capture (when inside a lambda) | Walks `lambdaOuterFunc` and `lambdaOuterFuncStack` |
| 11   | Special | Loose mode passthrough (inside `#bgl{}` only) | Returns `name` / `"var"` so unknown identifiers from surrounding I6 pass through |

`resolveIdentifierType` and `qualifyIdentifier` walk the same tiers in the same order. They produce different return *forms* (Beguile type name vs I6 emission text) but always agree about which tier wins. If they ever disagreed, the emitted code and type would describe different declarations — a class of bug worth recognising on sight.

---

## File-scope ambiguity and how it's resolved

When a name matches at Tier 4+, the resolver collects **all** candidates from those tiers (not just the first). After collection it deduplicates entries that resolve to the same I6 emission and type — the same declaration seen via multiple lookup paths is one candidate, not two.

Then it tries to narrow the set down to a single winner by applying two tie-breakers in order:

1. **`memberHint`** — passed by callers that know a `.member` access is about to follow. A candidate satisfies the hint if its type exposes that member (`typeHasMember(type, memberHint)` returns true).
2. **`currentExpectedType`** — set by callers that know the type they need (variable initializer RHS, operator RHS, default parameter value). A candidate satisfies the expected type if `isTypeCompatible(candidateType, expectedType)` is true.

If exactly one candidate survives a tie-breaker, that's the winner. If none survive (or none reduce to one), the resolver errors with:

```
'X' is ambiguous: matches enum value of 'eColor' and enum value of 'eFlavor'.
Qualify the use explicitly to disambiguate.
```

The error lists every candidate's origin so the user can see what they have and pick the right one.

### `memberHint` — when the receiver must have a member

[`typeHasMember(type, name)`](../bglParser.cpp#L2318) returns true if `type` is a `classDef` (walks base hierarchy) or `objectDef` with a member of that name. Returns false for enums and primitives — they have no addressable members.

Wired at three call sites where a member access is imminent:
- Bare-identifier branch in expression parsing — peeks past `.` to extract the next identifier.
- Expression-context method-call site — passes the method name.
- Statement-context method-call site — passes the method name.

### `currentExpectedType` — when the type is known from context

The parser maintains a single `currentExpectedType` slot. Call sites that know what type the upcoming expression should produce save the previous value, set their type, parse the expression, and restore. `parseExpression` itself uses an RAII guard so internal modifications don't leak past the call.

Wired at:
- **Variable initializer RHS** — declared type becomes the expected type.
- **Assignment statement RHS** — LHS variable's type becomes the expected.
- **Binary operator RHS** — LHS class/enum type becomes the expected (most operators take same-type RHS).
- **Default parameter values** — parameter's declared type.
- `parseCallArgList` **clears** the expected type for each argument context — function-argument expected types depend on the callee's parameter types, which are determined by overload resolution that hasn't run yet.

Concrete payoffs:
- `bGlulxWindowScale scale = fixed;` resolves `fixed` to `bGlulxWindowScale.fixed` even though `fixed` also names a value in `eGlulxStyleType`.
- `if (col == red)` (where `col: eColor`) resolves `red` to `eColor.Red` — `currentExpectedType="ecolor"` filters out any unrelated `red` enum.
- `void splitWindow(bGlulxWindowScale scale=fixed, …)` — the default value resolves correctly because parameter type sets the expected.

---

## Type identity for unclassed objectDefs

Beguile's design philosophy treats every `objectDef` as a thing with members, regardless of whether it has a declared class. The resolver implements this rule:

> An unclassed `objectDef` IS its own type.

Concretely, in `resolveIdentifierType` at line 2407:

```cpp
else if(auto* od = dynamic_cast<objectDef*>(g))
    ct = od->objectClass ? od->objectClass->name : od->name;
```

For `object _glulx { … }`, `resolveIdentifierType("_glulx")` returns `"_glulx"` — not `"object"`. This propagates everywhere: `auto x = _glulx;` infers `x`'s type as `"_glulx"`, not `"object"`; `bgl.glulx` (where the member is `auto glulx = _glulx;`) types as `"_glulx"`; method dispatch on those values finds `_glulx`'s methods.

Same rule for `self` inside an unclassed object body (line 2350):

```cpp
return currentObject->objectClass ? currentObject->objectClass->name : currentObject->name;
```

**Compatibility with the bare `object` class** is handled by [`isTypeCompatible`](../bglParser.cpp#L3193) — any `objectDef`-typed value is implicitly assignable to a parameter of type `object`. So `void doSomething(object o); doSomething(_glulx);` still type-checks. The `object` type behaves as a universal supertype for anything declared with the `object` keyword.

---

## How method dispatch finds the routine

Method calls flow through [`bindMethodCall`](../bglParser.cpp#L3394) → [`resolveMethodWithConversion`](../bglParser.cpp#L2287) → [`resolveMethod`](../bglParser.cpp#L2200).

`resolveMethod` runs in two steps:

**Step 1 — class hierarchy search**: if `typeName` resolves to a `classDef`, walk its members and base classes for a method matching by name + arity + arg types. This is where ordinary class methods are found.

**Step 2 — objectDef member search** (fallback when Step 1 misses): three lookup strategies tried in order:

- **(a)** `typeName` itself resolves to an `objectDef` — search its members directly. This is how the type-identity rule above pays off: when `glulx` types as `_glulx`, dispatch finds `_glulx`'s methods.
- **(c)** `self` or `typeName == "object"` inside an object body — search `currentObject`'s members.
- **(b)** `objPath` names a global `objectDef` — search its members. Covers receivers like `_glulx.method()`.

The fallback semantics (`varFallback`) capture the case where the only matching method takes a `var`-typed parameter — used as a last resort if nothing else matches.

**The dispatch sites** all accept either a `classDef` or `objectDef` receiver:

| Site | Location |
|------|----------|
| Expression-context method call | [`bglParser.cpp:4895`](../bglParser.cpp#L4895) |
| Chained method call on resolved expression | [`bglParser.cpp:5310`](../bglParser.cpp#L5310) |
| Statement-context method call | [`bglParser.cpp:6196`](../bglParser.cpp#L6196) |

If the type doesn't resolve to either, the dispatcher errors with `"Type '{X}' has no methods"`.

---

## The namespace-redirect mechanism

For `bgl.glulx.createMainWindow()` to compile, the parser has to walk the path: `bgl` is a global `objectDef`, `glulx` is its member with initializer `_glulx`, and `_glulx` is itself a global `objectDef` carrying the method.

This is handled by the **auto-member-on-object-instance** branch at [`bglParser.cpp:5044`](../bglParser.cpp#L5044) (search the file for the comment "Auto member on object instance pointing to another object: redirect cur"). When the parser sees `name.member.…` *and the third token is `.`* (i.e. we're in the middle of a chain, not at the final method call), it:

1. Looks up `name` as a global objectDef.
2. Finds `member` in that objectDef's members.
3. Reads the member's `declaredExpressionValue` to get the initializer text (e.g. `"_glulx"`).
4. Looks up the initializer name in the globals as another objectDef.
5. **Replaces `cur.value` with that target object's name** and re-enters the loop.

So `bgl.glulx.createMainWindow()` becomes the equivalent of `_glulx.createMainWindow()` after one redirect — and the dispatch finds the method on `_glulx` without ever needing to know about `bgl`.

The redirect is **only triggered for intermediate chain steps** (when the next token is `.`). For the final step (when the next token is `(`), we're at a direct method call, not a chain, so the redirect doesn't fire — we rely instead on the type-identity rule for `_glulx` (and the qualified emission via `qualifyIdentifier`).

---

## How `#using` plugs in

`#using bgl;` adds the `bgl` objectDef to `usingObjectImports`. `#using SomeClass;` adds the class to `usingImports`.

When the resolver reaches Tier 9, it walks each imported scope and looks for `name` among that scope's members. For `glulx` after `#using bgl`:

- `qualifyIdentifier("glulx", …)` returns `"bgl.glulx"` (the full path so I6 can resolve it through the property chain at runtime).
- `resolveIdentifierType("glulx", …)` returns `vd->type.name` for the `glulx` member — which for `auto glulx = _glulx;` is `"_glulx"` (per the type-identity rule above).

The combination — qualified path emission + correct type — means `glulx.createMainWindow()` after `#using bgl` emits as `bgl.glulx.createmainwindow()` and finds the method on `_glulx`. Both halves of the resolver have to agree about which tier wins; if they disagreed, we'd emit one thing and type-check against another.

---

## Worked example: `glulx.createMainWindow()` after `#using bgl`

Setting:

```beguile
#using bgl;
…
mainWin = glulx.createMainWindow();
```

Where `bgl` is the global objectDef, `glulx` is its member declared as `auto glulx = _glulx;`, and `_glulx` is another global objectDef with method `createMainWindow()`. The compiler's built-in `eTarget` enum also has a value `Glulx`, which case-folds to the same name.

Step by step:

1. **Parser** reaches `glulx`. Token type: identifier. Next token: `.`. Then `createMainWindow`. Then `(`.
2. The dot-access branch is entered. It reads `member = createMainWindow`, `afterMember = (`. Method-call branch.
3. **`resolveIdentifierType("glulx", func, body, memberHint="createmainwindow")`** is called:
   - Tiers 1–3 (lexical): no match.
   - Tiers 4+ (file scope): collect candidates.
     - Tier 4 — enum value `Glulx` of `eTarget` → candidate `(type=etarget, origin="enum value of 'etarget'")`.
     - Tier 9 — `bgl.glulx` is a `#using`-imported member with type `_glulx` (per the type-identity rule below) → candidate `(type=_glulx, origin="#using-imported member 'bgl.glulx'")`.
   - 2 candidates ⇒ try `memberHint="createmainwindow"`:
     - `typeHasMember("etarget", …)` → false.
     - `typeHasMember("_glulx", "createmainwindow")` → true.
   - Single satisfying candidate ⇒ return `"_glulx"`.
4. **`qualifyIdentifier("glulx", …, memberHint="createmainwindow")`** is called for the I6 emission. Same tier walk, same disambiguation. Tier 9 wins, returns `"bgl.glulx"`.
5. The dispatch site has `objType = "_glulx"` and `objName = "bgl.glulx"`. The receiver-type check passes (`_glulx` is an `objectDef`).
6. **`bindMethodCall("_glulx", "bgl.glulx", "createmainwindow", …)`**:
   - `resolveMethod` Step 1 (class hierarchy): `getType("_glulx")` is an `objectDef`, not a `classDef`. No match.
   - Step 2 (a): `getType("_glulx")` is an `objectDef` → `targetObj = _glulx`. Walk its members; find `createMainWindow`. Bind.
7. **Emission**: the method is non-emitter, so we emit `bgl.glulx.createmainwindow()`. At runtime, I6 resolves `bgl.glulx` through the property chain to the `_glulx` object, then dispatches `createmainwindow` on it.

The whole pipeline works because every step honours the same set of rules: file-scope candidate collection, `memberHint` tie-breaking, the type-identity rule for unclassed objectDefs, and the qualified emission for `#using`-imported members.

---

## Common failure modes

| Symptom | Likely cause | Look at |
|---------|--------------|---------|
| `"'X' is ambiguous: matches A and B. Qualify the use explicitly to disambiguate."` | A real cross-tier name collision that no tie-breaker could resolve. Either `memberHint` and `currentExpectedType` weren't set at the call site (so the resolver had no signal), or the candidates genuinely all satisfy them. | Either qualify the use (e.g. `eFoo.x` instead of bare `x`), or — if the call site SHOULD have provided a hint — wire `currentExpectedType` for that context. |
| `"No method 'X' on type 'object'"` | An unclassed objectDef received a method call but the type came back as bare `"object"` instead of the objectDef's own name. | Check `resolveIdentifierType` — is there a path returning `"object"` for an objectDef without applying the type-identity rule? |
| `"Undeclared identifier 'X'"` when X exists at global scope | The bare-identifier branch returned empty from `qualifyIdentifier`. The candidate collection may have produced no results at all — check if the binding (e.g. `extern verb foo;`) actually registers the name where the resolver looks. | Trace which tier should have matched; verify the registration path. |
| Type checking accepts the wrong assignment | `isTypeCompatible` may be missing a case for the type pair involved. | [`isTypeCompatible`](../bglParser.cpp#L3165) at `bglParser.cpp:3165`. |
| Expected-type-driven resolution didn't fire when you thought it would | The call site doesn't set `currentExpectedType`. The wired sites are: variable initializer RHS, assignment RHS, binary-operator RHS, default parameter values. Other sites pass through whatever was set by the enclosing context, or empty. | Check the four call sites listed in *Expected-type-driven resolution* above; if you need it elsewhere, add the save/set/restore pattern there. |

---

## What this document deliberately doesn't cover

- **Operator dispatch**: similar rules but lives in `applyBinaryOperator` and friends. Operators get their names mangled (`+` → `_opadd`, `=` → `_opeq`) for routine emission — see `mangleOperatorName` at `bglParser.cpp:3577`. Worth its own section if/when you tackle it again.
- **Generic / `func<>` / `array<>` resolution**: the type-name carries embedded angle-bracket syntax. Most resolution code handles it as a string; a few sites do special parsing. Out of scope here.
- **Pre-pass stubs**: identifiers can resolve to a forward-declaration stub during parsing; the stub gets replaced by the real definition later. The resolver mostly doesn't care, but stubs can produce `nameFound` matches without `arityMatch` matches in `resolveMethod`. See the `isPrePassStub` branches.

