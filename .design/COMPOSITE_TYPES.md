# Composite Types — Design Sketch

**Status**: design proposal, not implemented.

A composite type is a named alternation of existing types. A value of a composite type is a value of any one of its member types. Function parameters and return types accept the composite, with the type-checker treating each member as compatible.

This is a proposal for a future Beguile language feature. It captures decisions in enough detail that an implementation pass has a starting point without re-litigating the design.

## Motivation

Several places in Beguile express "one of a fixed set of types" today via either:

- a hardcoded list in the parser (e.g. the grammar-line accept list — `grammarToken`, `attribute`, `function`),
- a `var` parameter that loses all type information,
- an overload set, when the called code can branch on type.

A composite type lets the library declare the accepted set once and have the type-checker enforce it everywhere — shifting parser-internal knowledge into the type system, where it can be reused.

## Syntax

```bgl
composite grammarElement = dictionaryWord | grammarToken | attribute | function;
```

- `composite` is a new file-scope keyword.
- The right-hand side is a `|`-separated list of existing type names.
- Each member must already be a registered type (class, enum, or another composite).
- Composites may include other composites; the result is flattened (transparent union).

Use sites:

```bgl
void emit(grammarElement el) { … }

emit(.water);     // dictionaryWord — accepted
emit(held);       // grammarToken value — accepted
emit(MyAction);   // function — accepted
emit(myObject);   // ERROR: 'object' is not a member of grammarElement
```

Return type:

```bgl
grammarElement parseNext() { … }
```

## Semantics

### Type compatibility

A composite parameter type `C = T1 | T2 | … | Tn` accepts an argument of type `A` when:
- `A == Ti` for some `i`, OR
- `A` is compatible (per existing `isTypeCompatible` rules) with some `Ti` (subtype, conversion, var, etc.).

A composite argument type `A1 | A2 | …` is compatible with a non-composite parameter type `P` when **every** `Ai` is compatible with `P`. (The contravariant rule is intentional — passing "any of these" to a function that accepts only one specific type is unsafe unless every alternative is acceptable.)

A composite-to-composite assignment: `B = A` where both are composite is compatible when every member of `A` is a member of (or compatible with a member of) `B` — i.e., `A`'s members are a subset of `B`'s.

### Member access and method dispatch

Calling a method on a composite-typed value: the call is rejected unless **all members** of the composite provide that method with compatible signatures. This matches TypeScript's rule for union types and avoids surprising runtime mismatches.

```bgl
composite stringy = string | dictionaryWord;
stringy s = …;
s.print();   // OK if both string and dictionaryWord have print()
s.length();  // ERROR if only string has length()
```

For the pragmatic IF authoring case, this rule will mostly steer users toward narrowing first (see below) rather than calling polymorphically.

### Type narrowing

To act on a composite value as a specific member type, the user narrows it. Two forms, mirroring TypeScript and Rust:

```bgl
if (s is string) {
    s.length();    // 's' is treated as 'string' inside this branch
}
```

```bgl
string str = (string)s;   // explicit cast; runtime check that s is actually a string
```

The `is` form is a flow-sensitive narrowing: inside the `then` branch, the static type of `s` is the matched member. Outside, the static type is the original composite. Implementation note: the existing static-analysis pass that resolves identifier types per-block can be extended with a narrowing record indexed by `(name, block)`.

The cast form (`(T)expr`) already exists for explicit conversions. For composite values, it asserts that the value's runtime type is `T` — semantically a checked cast. I6 emission depends on whether composites need a runtime tag (see I6 emission below).

### Variance

Composites are invariant in their members:
- `string | int` is not a subtype of `string | int | object`.

This is the safest default. A subtyping rule could be added later if a use case justifies it.

### Overload resolution

If a function has overloads `f(int)` and `f(string)`, and the caller has an `int | string` value, overload resolution **fails** with an ambiguity error. The user must narrow first, or the function must accept the composite directly.

```bgl
void f(int n) { … }
void f(string s) { … }
composite x = int | string;
x val = …;
f(val);        // ERROR: ambiguous; narrow with 'is' or accept the composite
```

## I6 emission

Composites are a Beguile-side type-system construct. I6 has no first-class union type; everything sits in untyped 16-bit (Z) or 32-bit (Glulx) cells. There are two approaches to runtime representation, depending on whether narrowing has runtime semantics:

### Option 1 — type-erased (simplest)

The composite is just `var` at runtime. Type narrowing is purely a static-analysis affordance; the `is` form has no runtime check. This is unsafe if an actual mismatched value flows in, but Beguile's type system is opt-in elsewhere too.

Pros: zero runtime cost; trivial to implement.
Cons: no runtime safety; `(T)expr` casts are static-only.

### Option 2 — tagged

A composite value carries a small runtime tag indicating which member type it holds. Tags are issued at composite-aware boundaries (function arguments where a composite is expected, return statements from composite-returning functions, etc.).

Pros: `is` and `(T)expr` have real runtime semantics; can detect broken interop.
Cons: every composite-typed slot pays a tag. Memory and copy costs add up. Tags must be kept in sync with the member set across recompilations.

**Recommendation**: start with Option 1. Add tagging later only if a real use case demands runtime safety.

## What this does NOT solve

- **Identifier-resolution ambiguity** (e.g., the `noun` enum value vs. the `extern object noun` global): composite types operate at the type-checker level. By the time the type-checker runs, the resolver has already chosen which `noun` is being referenced. The current "globals beat enum values" tie-break rule (see §13 of the language spec) is the correct level for that fix.
- **Runtime type queries on values whose declared type is concrete**: `obj.is(SomeClass)` at runtime exists for objects already (§8.6b), and is unrelated to composite types.

## Relationship to existing language features

| Feature | Relationship |
|---|---|
| `var` | A typed alternative — composites narrow `var`'s "anything" to "one of these specific things." |
| Enums | Enums are sums of *values*; composites are sums of *types*. Complementary, not redundant. |
| Class hierarchies | Composites are an alternative to inheritance for "this OR that without a shared base." A composite of three classes doesn't require declaring a synthetic base. |
| Overloads | Composites and overloads address overlapping needs; overloads have richer dispatch (per-call branching). Composites are simpler to declare and travel naturally as values. |

## Refactoring opportunities, post-feature

Once composites land, the grammar-line parser at [bglParser.cpp:10286](../bglParser.cpp#L10286) — currently:

```cpp
if(resolvedType != "grammartoken" && resolvedType != "attribute" && resolvedType != "function")
    parsingError(…);
```

becomes:

```cpp
if(!isTypeCompatible(resolvedType, "grammarelement"))
    parsingError(…);
```

— with `grammarElement` now declared once in the library binding as `composite grammarElement = grammarToken | attribute | function`. The accepted set moves from compiler code into library text, where users can audit and (in principle) extend it.

Other candidate sites:
- `array<T>` element-type permissibility (currently a hardcoded set of intrinsic `T`s plus an "any class" rule).
- `switch` case label types (currently special-cased per condition type).

## Implementation cost

Rough sketch (not committed):

- New `compositeDef : public typeDef` holding `vector<typeDef*> members` plus a flattening pass at registration time.
- Parser: one rule for the `composite Name = T1 | T2 | …;` declaration form.
- `isTypeCompatible`: two new clauses (composite paramType, composite argType).
- `(T)expr` cast and `is` narrowing: AST nodes, scope-aware narrowing, type-checker integration.
- Method dispatch on composite receivers: walk all members, require unanimous agreement on a method with compatible signature.
- Tests: positive (each shape — declaration, parameter, return, narrow), negative (member access without narrow, ambiguous overload, non-member type assigned).

Estimated 200–400 lines of compiler code plus tests, depending on how much narrowing support is included in the first pass.

## Suggested first cut

If picked up, the smallest valuable scope is:

1. Composite declaration syntax + flattening.
2. Parameter type-checking only (no return values, no narrowing).
3. Refactor the grammar-line parser to use `composite grammarElement = …`.

That alone validates the feature, replaces a hardcoded list with library-declared text, and leaves narrowing/return/dispatch for follow-up passes once usage patterns are clearer.
