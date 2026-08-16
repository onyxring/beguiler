# Scope / Reachability queries for `bgl.world` — design draft

**Status:** to be designed (not implemented). Captures the direction from the
BLR-review discussion so it can be picked up later. Decisions marked **[open]**.

---

## 1. Motivation

`bgl.world` today enumerates the object *tree* — `getAll(pred)`, `inParent(p, pred)`,
`instances(cls, pred)`, each returning a tracked `array<object>` that composes with
`<array>`/`<linq>`. What it can't answer is the most IF-specific question: **what is
in scope for an actor?** — the set of objects the actor can perceive or reach right
now (contents of the location, open/transparent containers, held items, subject to
light). That's the query authors reach for constantly (disambiguation, "you can't see
any such thing", reactions, sensory verbs).

## 2. Why we can't just wrap I6's scope

I6's scope (`LoopOverScope`, `TestScope`, `ScopeWithin`, `PlaceInScope`, the `scope=`
grammar token, the `InScope()` hook) is a **Parser construct**, and it is
library-dependent *all the way down* — not only the routines, but the very attributes
that define scope (`open`, `transparent`, `container`, `supporter`, `light`,
`concealed`, `enterable`) are declared in the **bindings**, not core. Beguile's
standing rule is that the BLR must be **independent of the I6 libraries** (games will
eventually be built without them). So a library-free `bgl.world` genuinely cannot wrap
any of I6's scope machinery. Scope has to be rebuilt from primitives Beguile owns — the
object tree (`parent`/`child`/`sibling`) — plus a policy the game supplies.

## 3. Core principle: **Beguile owns the traversal; the dev owns the policy**

A scope test — "can the actor perceive/reach this object?" — decomposes into two parts
with different ownership:

- **Structural reachability** — is the candidate inside the actor's location subtree
  (does its parent-chain reach the actor or their room)? Pure tree walking →
  **library-independent, Beguile provides it.**
- **Perceptual policy** — does a *closed opaque* box hide its contents? does darkness
  hide the room? This needs `open`/`transparent`/`light`, which are **library/game
  defined** → **the dev owns it.**

So core `bgl.world` should ship the **traversal primitives**, and "scope" is those
primitives + a **dev-supplied policy predicate** (the "scope filter"). A useful
*default* policy (open/transparent/light) can't live in core — it assumes attributes
core doesn't know — so it belongs in an **opt-in** piece (a `<scope>` extension, or the
stdlib/orLibrary binding). Stdlib users get batteries-included behavior they can
override; library-free games supply their own.

## 4. Two candidate shapes — **[open, the main decision]**

**(a) Flat filter over all objects** (the "filter passed to a LINQ routine" framing):
```
bgl.world.getAll(scopePred)          // or  bgl.world.getAll().filter(scopePred)
```
- `scopePred(candidate)` tests each object independently; it must itself walk up the
  parent-chain and check each container's openness.
- Pro: simplest, LINQ-native, *already works today* via `getAll(pred)`.
- Con: scope is fundamentally a **recursive reveal** — a closed box hides its contents.
  A flat predicate re-runs the reveal walk per object, and a naive one leaks (a plain
  "is it in my room's subtree?" test lets you see inside closed boxes). Correctness is
  on the dev.

**(b) Traversal parameterized by a "descend?" filter** (reveal model):
```
bgl.world.inScope(actor, canSeeInto)  // walk out from actor's location, descend into a
                                       // container only where canSeeInto(container) holds
                                       // → array<object>, still LINQ-composable
```
- Reveal happens **once**, correctly; only reachable objects are visited (more
  efficient). The "filter" is a *can-I-see-through-this?* predicate, not a per-object
  membership test.
- Con: a bespoke traversal (slightly more machinery) rather than a plain LINQ filter.

**Lean:** (b) for the scope query specifically (correct reveal, library-free), while
keeping the flat `getAll(pred)` for simple flat-membership queries. Both return
`array<object>` so downstream `<linq>` chaining is identical.

## 5. Proposed surface (sketch)

```
// core bgl.world — library-independent traversal primitives
bgl.world.isWithin(candidate, ancestor)            // parent-chain containment test → bool
bgl.world.contentsOf(container, deep, descend?)    // direct or recursive contents,
                                                   // recursion gated by the descend? filter
bgl.world.inScope(actor, canSeeInto)               // reveal-walk from actor's location →
                                                   // array<object>  (canSeeInto = policy)

// opt-in default policy (extension/binding — assumes the attribute model, NOT core)
bool defaultCanSeeInto(object c) { return c.has(open) || c.has(transparent); }  // + light…
```

## 6. Redefinability — **[open]**

The dev "redefines what scope is" by supplying the policy predicate. Options:
- **Per-call filter** — `inScope(actor, myPolicy)`, default policy as the argument
  default. Cleanest, most Beguile-idiomatic (mirrors `getAll(pred)`), no global state.
- **Settable game-wide default** — `bgl.world.scopePolicy = myFn`, or a `replace`-able
  policy function, for "one definition of scope for this game."

**Lean:** start with the explicit per-call filter; add the global default only if the
ergonomics demand it.

## 7. Composition

Everything returns `array<object>`, so it chains with the existing engine:
`bgl.world.inScope(player, canSeeInto).filter(o => o.has(edible))`, `for(o in
bgl.world.inScope(player))`, etc. The rotating scratch-buffer pool and `superposed`
gating carry over from the current `bgl.world` design.

## 8. Caveats / open threads

- **Attributes are library-defined** → any non-trivial *default* policy is opt-in, not
  core. This is the hard constraint that shapes the whole feature.
- **Actor context** — scope is per-actor; the query takes the actor (param or captured).
- **Light / darkness** — "can't see in the dark" is a policy factor the filter owns.
- **See vs. reach** — visual scope (see into a transparent closed box) and physical
  reach (can't reach in) differ. A single filter conflates them; a mature design might
  expose two (visible vs. reachable). Probably out of scope for v1 — note and defer.
- **Buffer pool** — large worlds / deep nesting can exceed the 128-object scratch
  buffers silently (same limitation as today's `bgl.world`); snapshot into an owned
  `array<object>` for deep work.

## 9. Decisions to make before implementing

1. Flat filter vs. recursive-reveal traversal as the primary scope surface (§4).
2. Ship a default policy? (must be opt-in extension/binding, not core — §3).
3. Redefinability mechanism: per-call only, or also a settable game-wide default (§6).
4. See-vs-reach: one filter now, or design for two from the start (§8).
