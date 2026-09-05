> Note: I used this document to work through verbs and grammar, how they are handled in I6 and the modification of how I want them to work in Beguile.  It's included here simply for posterity.
# Verbs and Grammar: a mental model 
---
## The I6 and VM Grammar Models
This section describes a mental model for how the underlying VM manages grammar, and describes how I6 accommodates it.

The VM has a grammar table of commands which are matched against player input and associated with a handler routine.  

I6 populates the grammar table from `Verb` declarations... 

```
Verb 'type' 'dial' * number 'in'/'into' noun -> TypeNum;
```

Here's a mental model (conceptual, not literal storage) of what the above line expands to in the grammar table:

| trigger word | token 1 | token 2 | token 3 | action handler |
| ------------ | ------- | ------- | ------- | -------------- |
| type         | NUMBER  | into    | NOUN    | TypeNum        |
| type         | NUMBER  | in      | NOUN    | TypeNum        |
| dial         | NUMBER  | into    | NOUN    | TypeNum        |
| dial         | NUMBER  | in      | NOUN    | TypeNum        |
Somethings to remember about this:

- The words preceding the `*`, 'type' and 'dial', are *trigger* words.  *(This distinction becomes relevant in a bit.)* 
- Each trigger word has its own set of entries in the grammar table.
- There can be more than one possible dictionary word defined for a single grammar token, separated by `/`.   These are also expanded in the conceptual grammar table: 'in' and 'into' create separate entries.

Now when the player types...
```
type 5 into keypad
dial 5 in keypad
```
 ... each matches an entry in the grammar table (here, the first and last entries).
## A note on Verb and trigger words 

Conceptually, `Verb` is primarily a way to group trigger words together (I refer to a `verb` sometimes as a trigger-word-grouping), and I6 enforces this:   **A dictionary word can be the TRIGGER of at most ONE verb.** Two `Verb` directives claiming the same trigger word is a hard compile error:  

```
Verb 'type' 'dial' * number 'in'/'into' noun -> TypeNum;
Verb 'dial' * 'reset' noun -> ResetPad; //ERROR!  dial verb already defined
```

In practice, this means you CANNOT add grammar to an existing trigger word by declaring another `Verb` directive for it. Instead, you `Extend` the verb construct (i.e., the "grouping" of one or more trigger words) which owns it...

## Extending / altering base grammar in I6

Most games sit on a reusable library that already supplies common grammar, so a dev needs to add or alter grammar without editing the library source. I6 supports this with `Extend`.  For our purposes, we'll assume the previous type/dial grammar is part of a library which we want to subsequently `Extend` to include more grammar...

```i6
Extend 'dial' * 'reset' noun -> ResetPad;
```

After this, the grammar table now looks like...

| trigger word | token 1     | token 2    | token 3 | action handler |
| ------------ | ----------- | ---------- | ------- | -------------- |
| type         | NUMBER      | into       | NOUN    | TypeNum        |
| type         | NUMBER      | in         | NOUN    | TypeNum        |
| dial         | NUMBER      | into       | NOUN    | TypeNum        |
| dial         | NUMBER      | in         | NOUN    | TypeNum        |
| ***type***   | ***reset*** | ***NOUN*** |         | ***ResetPad*** |
| ***dial***   | ***reset*** | ***NOUN*** |         | ***ResetPad*** |
Some points worth calling out:

- Notice that the `extend` line only referenced ***one*** of the two trigger words defined in the original `Verb` declaration (trigger word grouping), and yet *all* trigger words were duplicated in the grammar table.  I6 does this to make it easy to expand synonyms.  It remembers which trigger words are declared together and extends them together.
- Notice that the new grammar is ***appended*** to the pre-existing grammar in the table.  This is an important distinction, since grammar resolution starts at the top and evaluates each grammar line in order.  All things being equal, the first grammar line matched is the one selected.  

### Extending some trigger words but not others
To create entries in the grammar table without pulling in all other trigger words, I6 supports the `only` qualifier...

```
Extend only 'dial' * 'reset' noun -> ResetPad;
```

This results in a the following grammar...

| trigger word | token 1     | token 2    | token 3 | action handler |
| ------------ | ----------- | ---------- | ------- | -------------- |
| type         | NUMBER      | into       | NOUN    | TypeNum        |
| type         | NUMBER      | in         | NOUN    | TypeNum        |
| dial         | NUMBER      | into       | NOUN    | TypeNum        |
| dial         | NUMBER      | in         | NOUN    | TypeNum        |
| ***dial***   | ***reset*** | ***NOUN*** |         | ***ResetPad*** |

Notice that 'type' is ***not*** extended.  

### Splitting Verbs: a Consequence of `extend only` 
A point worth noting here is that, following the `extend only` line, 'dial' and 'type' are ***no longer*** grouped together in the mind of I6.  In fact, I6 creates a copy of the defined grammar and duplicates it in a new `Verb` (trigger-word-grouping), ***removing*** 'dial' as a trigger word from the first (leaving only 'type') and making 'dial' the only trigger word in the new `Verb` group.   Subsequent extensions to 'dial' will ***not*** also expand 'type' and vice versa.  

What this means is that our example so far:
```i6
Verb 'type' 'dial' * number 'in'/'into' noun -> TypeNum;
Extend only 'dial' * 'reset' noun -> ResetPad;
```

is semantically equivalent to if we had typed...

```i6
Verb 'type' * number 'in'/'into' noun -> TypeNum;
Verb 'dial' * number 'in'/'into' noun -> TypeNum
			* 'reset' noun -> ResetPad;
```

### *Caution*: Multi-word `extend only` will lose grammar

`Extend only` can list more than one trigger word. When the listed words come from the SAME verb grouping, they just split off together. But when they come from *different* verb groupings, i6 has to pick one grammar for the new merged verb: it keeps only the grammar of the ***last*** word in the list, and the earlier words lose their own grammar and inherit the last one's. (All listed words are still removed from their original groupings either way.)

Consider two independent verbs:

```i6
Verb 'type' 'dial' * number 'into' noun -> TypeNum;
Verb 'put' 'input' * 'code' 'into' noun        -> DialCode;
```

Which produces the following grammar table (two separate trigger-word-groupings):

| trigger word | Vrb | token 1 | token 2 | token 3 | action handler |
| ------------ | --- | ------- | ------- | ------- | -------------- |
| type         |  1  | NUMBER  | into    | NOUN    | TypeNum        |
| dial         |  1  | NUMBER  | into    | NOUN    | TypeNum        |
| put          |  2  | code    | into    | NOUN    | DialCode       |
| input        |  2  | code    | into    | NOUN    | DialCode       |

If we `extend only` using a trigger word from each...

```i6
Extend only 'input' 'dial' * 'reset' noun -> ResetPad;
```

We get the following...

| trigger word | Vrb     | token 1      | token 2    | token 3    | action handler | Note |     |
| ------------ | ------- | ------------ | ---------- | ---------- | -------------- | ---- | --- |
| type         | 1       | NUMBER       | into       | NOUN       | TypeNum        |      |     |
| put          | 2       | code         | into       | NOUN       | DialCode       |      |     |
| dial         | 3       | reset        | NOUN       |            | ResetPad       |      |     |
| input        | 3       | reset        | NOUN       |            | ResetPad       |      |     |
| dial         | 3       | NUMBER       | into       | NOUN       | TypeNum        |      |     |
| ***input***  | ***3*** | ***NUMBER*** | ***into*** | ***NOUN*** | ***TypeNum***  |      |     |
As expected, `dial` and `input` have been grouped into a new verb grouping which has their new grammar, and `dial`'s previous grammar; however, `input` has lost its original grammar and inherited `dial`'s. 

This silent loss of all-but-the-last trigger word's grammar is almost certainly an I6 bug, but it needs to be considered by the Beguile generation sequence.
### `replace`: wipe the split word's inherited grammar

`replace` (with `only`) discards the grammar the split word would otherwise inherit, so only the new lines remain:

```i6
Verb 'type' 'dial' * number 'into' noun -> TypeNum;
Extend only 'dial' replace * 'reset' noun -> ResetPad;
```

This results in a the following grammar...

| trigger word | token 1     | token 2    | token 3 | action handler |
| ------------ | ----------- | ---------- | ------- | -------------- |
| type         | NUMBER      | into       | NOUN    | TypeNum        |
| type         | NUMBER      | in         | NOUN    | TypeNum        |
| ***dial***   | ***reset*** | ***NOUN*** |         | ***ResetPad*** |

With this `replace` qualifier, the following player command will no longer be understood...

```
dial 5 into keypad
```

### Sequencing: `first` and `last`

Grammar is matched top-to-bottom, first good match wins.  I6 introduces the `first` and `last` qualifiers to help manage this:

- default (no qualifier): new lines are APPENDED to the bottom of the verb's table.
- `first`: new lines are moved to the TOP, so a narrow new rule is considered before the library's more general ones.
- `last`: explicitly forces to the bottom; rarely needed, since default already appends, but useful for supplementary grammar that MUST stay last.

---
# The Beguile grammar model
Armed with a mental model of I6 and the VM's grammar table, we can now begin the real work of defining how Beguile represents grammar and how it translates into I6.
## Verb Objects
Beguile conceptualizes verbs as **verb objects**: a bundle of {grammar lines} + {one verb handler}. Instead of poking dictionary words and worrying about which one is the "key," you work with the object itself:

```bgl
verb TypeNum {
    grammar = { 
	     {.type|.dial, NUMBER, .into|.in, NOUN}
	};
    void handler(){ print("You can't put anything in it."); }
}
```

This model turns the I6 model on its head: it is the handler which is unique; there is no concept in Beguile of a trigger word.

To translate a Beguile verb into I6, Beguiler emits **one I6 `Verb` declaration per trigger word** — each dictionary word owns its own I6 verb. A `.a|.b` alternation is expanded to one directive per word. (This keeps the I6 side simple: every directive is a single word plus its grammar, which is also what lets `grammar -=` remove one word's line without disturbing the others.)

It additionally generates the expected verbSub.

Here's what the above emits as...

```
Verb 'type' * NUMBER 'into'/'in' NOUN -> TypeNum;
Verb 'dial' * NUMBER 'into'/'in' NOUN -> TypeNum;
```

## Extending / altering base grammar in Beguile
You alter a previously-defined verb by `extend`-ing it. In an `extend` body the grammar operator is **explicit**, so an edit can never silently do the wrong thing:

- `grammar += { ... }` — **append** grammar lines.
- `grammar -= { ... }` — **remove** matching grammar lines (see below).
- `replace grammar = { ... }` — **replace** the verb's whole grammar (see below).

A bare `grammar = { ... }` is only valid in the original `verb` declaration; inside an `extend` it is an error that points you to the three forms above.  

Appending:

```
extend TypeNum {
    grammar += { 
	     {.input, NUMBER, .into|.in, NOUN}     
	};
}
```

With the above in place, the output gains a directive for the new word... 

```
Verb 'type' * NUMBER 'into'/'in' NOUN -> TypeNum;
Verb 'dial' * NUMBER 'into'/'in' NOUN -> TypeNum;
Verb 'input' * NUMBER 'into'/'in' NOUN -> TypeNum;
```

One `Verb` per trigger word — `type`, `dial`, and `input` each get their own, all routed to the same action. This is more directives than I6's grouped `Verb 'type' 'dial' 'input'` form, but it is runtime-identical (each word triggers the same pattern → `TypeNum`); Beguile trades a little I6-source verbosity for a uniform model where each word's grammar can be edited independently.
## Removing grammar in Beguile
`grammar -= { ... }` removes grammar. How much it removes is set by **how much of the line you name** — the grain scales with the spec:

- **Line-level** `-= { {.w, pat…} }` (with a pattern) removes the one **exactly-matching** line.
- **Word-level** `-= { {.w} }` (bare word, no pattern) removes **all** of that word's grammar.

```bgl
extend TypeNum {
    grammar -= { {.dial, .into, noun} };   // line-level: drop that one dial line
}
extend TypeNum {
    grammar -= { {.dial} };                // word-level: drop EVERY dial line
}
```

Fully qualifying the pattern means "this rule"; specifying  just the first word means "all rules beginning with it." (A single bare word is the only "match-many" form; a partial-pattern *prefix* matching is deliberately not supported, because `-= {.give, noun}` would then silently take a more-specific `{.give, noun, .to, noun}` line you never named.)

Removal is **source-order**: a `-=` only sees the lines declared *before* it (the base plus earlier extends), so a later `+=` of the same line is unaffected. A `-=` that matches nothing **warns** (almost always a typo or a mis-ordering against the matching `+=`).

### On `extern` verbs: the two-layer model
Beguile has only **partial sight** of a library verb, and the two grains behave differently because of it. Beguile knows three things about an extern verb: the **trigger words it claims** (from the binding), the **grammar Beguile itself added** to it with `+=` (word *and* pattern), and nothing about the library's **original** grammar — those patterns are **opaque**.

- **Fully Qualified Pattern `-=`** can only match Beguile's own additions. The library's original patterns are invisible, so an attempt to specify an exact pattern can't match.  Beguile warns you of this, points you at the single word form.
- **Single Word Pattern `-=` EVICTS the entiere word.** It needs no patterns, so it *can* reach the opaque layer: it lowers to I6 `Extend only 'w' replace`, peeling the whole word off the library verb. This is how you reclaim a library word.

Because Beguile can tell a *claimed* word from an unknown one, the "nothing matched" warning is precise rather than flat:

| you write on an extern verb                                 | outcome                                                                                     |
| ----------------------------------------------------------- | ------------------------------------------------------------------------------------------- |
| `-= { {.w, pat} }` matches a Beguile-added line             | remove that line                                                                            |
| `-= { {.w, pat} }`, no match, but `w` **is** a claimed word | warn: the library's grammar for `w` is opaque — *use `-= { {.w} }` to evict the whole word* |
| `-= { {.w, pat} }`, no match, `w` not claimed               | warn: typo — `w` isn't a word this verb has                                                 |
| `-= { {.w} }`, `w` is claimed (or Beguile added it)         | **evict** — `Extend only 'w' replace`                                                       |
| `-= { {.w} }`, `w` neither claimed nor added                | warn: nothing to evict                                                                      |

## Replacing grammar in Beguile
To replace a verb's existing grammar wholesale rather than edit it, put the `replace` qualifier on the `grammar` assignment inside an `extend`. (`grammar += { ... }` appends and `grammar -= { ... }` removes, as above; `replace grammar = { ... }` wipes the whole verb first.)

```bgl
extend TypeNum {
    replace grammar = { 
         {.dial, .to, NOUN}     
    };
}
```

This means all grammar previously associated with `TypeNum` is replaced, including `type`, `input`, and the previous `dial` grammar. The replace lines become the verb's complete grammar, so only the trigger words they name survive — `type` and `input` are dropped:

```
Verb 'dial' * 'to' NOUN -> TypeNum;
```

## Synonyms in Beguile

Everything so far treats each trigger word as its own thing: Beguile emits one `Verb 'w'` per word, so two words that happen to share a pattern are still ***independent*** I6 verbs. Adding `.steal` and `.grab` as extra grammar lines *copies* the pattern at that moment — a later change to the original verb does NOT reach them.

Sometimes you want the opposite: new words that are ***true aliases*** of an existing verb, sharing its grammar identity so any later extension flows to them automatically. That is I6's synonym form...

```i6
Verb 'steal' 'grab' 'pilfer' = 'take';
```

...and Beguile spells it with a `synonyms` assignment inside an `extend`:

```bgl
extend Take {
    synonyms = {.steal, .grab, .pilfer};   
}
```

The verb being extended is the **anchor** (`Take`); its primary trigger word is resolved at emit time. The listed words alias the anchor's grammar table rather than copying it, so a later `extend Take { grammar += { ... } }` is reachable through every synonym too. This is the one place two dictionary words deliberately share a single I6 grammar identity, in contrast to Beguile's default one-`Verb`-per-word emission.

`synonyms` works on both `extern`/library verbs (the common case — aliasing new words onto a stdlib verb) and native verbs; for a native verb the synonym directive is emitted *after* the verb's own `Verb`, so the anchor's grammar already exists. An empty `synonyms = { }` is an error. (Full reference: languageSpec §"Verb Synonyms".)

## Priority in Beguile: the `first`/`last` variant

I6 orders grammar positionally with `first`/`last`; Beguile replaces that with a numeric `priority` on the verb (a property of the `verb` class, default `10`). A **lower number is matched earlier** — priority `5` is tried before priority `10` — the same "first good match wins" idea, expressed as a number instead of a position.

You set it in three places:

- **On the verb's own block** — `priority = N;` sets the verb's *anchor*, the pivot everything else sorts against (default `10`):

    ```bgl
    verb TypeNum {
        priority = 5;                                  // sorts ahead of default-priority grammar
        grammar = { {.type|.dial, NUMBER, .into|.in, NOUN} };
        void handler(){ ... }
    }
    ```

- **Inside an `extend`** — a bare `priority = N;` is a *block-local directive*: it stamps onto every line added by that block's `grammar += { ... }` and is NOT a persistent property, so different `extend`s can carry different priorities on the same verb without colliding:

    ```bgl
    extend Look {
        priority = 5;                                  // these lines beat the library's default grammar
        grammar += { {.peer, .at, noun} };
    }
    ```

- **Per grammar rule** — a `grammarRule` object takes an optional third positional element: `grammarRule r = {Drop, {.toss, held}, 5};`. (Inferred-verb rules inherit the enclosing verb/`extend` priority instead.)

Priority is the one place the native-vs-extern split shows through in emission:

- For a verb Beguile **owns**, all of a word's lines fold into that word's single `Verb`, emitted in priority order (lower numbers nearer the top, so they are matched first). Ordering *is* the priority; no I6 `first`/`last` appears.
- For an **extern** verb, Beguile reaches in with `Extend`, and priority chooses the qualifier: a line whose priority is *below* the verb's anchor emits `Extend 'w' first …`, and a line at or above the anchor emits a plain `Extend 'w' …` (I6 appends it last). This is the only place I6's `first`/`last` actually surfaces.

`priority` is recognized only on verbs; the compiler keeps it out of the emitted I6 `with` clause.

---
This is the general rule: **a verb created purely in Beguile always lowers to pure `Verb` declarations — Beguile never uses I6 `Extend` against grammar it owns.** I6 `Extend` is reserved for verbs Beguile does NOT own (`extern`/library verbs); see "Beguile with I6 libraries" below.


Splitting a verb is NOT a Beguile concept: you own your verbs and manipulate them by object identity, so there is never a reason to split. It exists only as an I6-interop operation (`Extend only`) for reaching into foreign library grammar — see "Beguile with I6 libraries" below.

### Handlers in an `extend`

When adding grammar, you don't need to redefine `handler` in an `extend`; it is already defined on the base `verb`. To *replace* the behavior, use the normal method-`replace` qualifier inside the extend: `replace void handler(){ ... }`. (Omitting `replace` is an error that points you to it.) Overriding the handler of an `extern`/library verb is not yet supported.

## The I6 ↔ Beguile mapping at a glance

| I6                                                      | Beguile                                   | note                                                |
| ------------------------------------------------------- | ----------------------------------------- | --------------------------------------------------- |
| `Verb 'w1' 'w2' * ... -> Act` + `ActSub`                | `verb Act { grammar = {…}; handler(){} }` | action + routine fused                              |
| any word names the verb                                 | you name the OBJECT                       | no "key word" to reason about                       |
| `Extend 'w'` (append)                                   | `extend Act { grammar += {…}; }`          | append; resolved into the verb's `Verb`             |
| (no I6 primitive)                                       | `extend Act { grammar -= {…}; }`          | remove matching lines (per word); warns if none; on extern, removes only Beguile's own additions |
| `first` / `last`                                        | per-verb / per-rule `priority`            | lower number = matched earlier (see "Priority in Beguile") |
| `Extend 'w' replace`                                    | `extend Act { replace grammar = {…}; }`   | wipe whole verb; resolved into the verb's `Verb` (Extend only if extern) |
| `Verb 'w1' 'w2' = 'v'` (synonym form)                   | `extend Act { synonyms = {.w1, .w2}; }`   | true aliasing                                       |
| `Extend only 'w' replace` (take a word off a FOREIGN verb) | `extend Lib { grammar -= { {.w} }; }`  | word-level `-=` evicts; reclaim by declaring `w`, or leave it to disable the word |

## How Beguile lowers a verb object to I6

Trigger words are I6's foundational model: an I6 verb IS a grouping of trigger words sharing one grammar table. A Beguile verb is the opposite in spirit — an OBJECT (grammar lines plus a handler), referenced by identity, with NO explicit notion of a trigger-word-grouping. So when Beguile lowers a verb object to I6, it re-derives the groupings its own syntax never stated:

> each grammar line carries one trigger word (an alternation like `.a|.b` is expanded to one line per word), and Beguile emits one I6 `Verb` per trigger word.

So **one Beguile `verb` can become several I6 `Verb` declarations** — a `.type` line and a `.say` line lower to `Verb 'type'` and `Verb 'say'`, both routed to the same action.

Crucially, for grammar Beguile OWNS, that lowering is **pure `Verb` — no I6 `Extend`.** Beguile resolves the whole verb (base grammar + every `extend`/`replace`/priority contribution) at compile time and emits the final grammar. So:

```bgl
verb TypeNum { grammar = { {.type|.dial, number, .into|.in, noun} }; ... }
extend TypeNum { grammar += { {.dial, .reset, noun} }; }       // append
```

The append folds into the base at compile time, and Beguile emits pure `Verb`s — one per word. Note the `+=` named only `.dial`, so `reset` lands on `dial` alone:

```
Verb 'type' * NUMBER 'into'/'in' NOUN -> TypeNum;
Verb 'dial'
    * NUMBER 'into'/'in' NOUN -> TypeNum
    * 'reset' NOUN -> TypeNum;
```

No `Extend` anywhere: Beguile owns `type` and `dial`, so it just emits their resolved final grammar. (Plain `grammar = {…}` in an `extend` is APPEND; `replace grammar = {…}` wipes the whole verb; priority reorders — all of it folds into the `Verb` above.)
### How `-> Action` fuses the action and its Sub routine

One I6 detail the object hides: `-> Action` names an **action**, not a routine directly. Each
action has a partner **Sub routine** (`TypeNumSub`) that the library calls when the action fires.
Beguile FUSES the two: `TypeNum` is both the action you compare against (`action == TypeNum`,
`case TypeNum`) and the owner of the handler body.

# Beguile with I6 libraries

In a pure-Beguile program you own every verb, so you never need to reach down to word level: you declare the object with the words and grammar you want, extend it, delete it, reorder it, all by object identity.

You can reach into a library verb and **add** grammar to it: `extend Attack { grammar += { {.bludgeon, noun} } }` gives the library's `Attack` verb a new trigger word, routed to its existing action (`AttackSub`). Beguile tracks what it adds, so you can **remove your own additions** later with a line-level `grammar -=`.

You can also **take a word away** from a library verb — reclaim it, or disable it — with a **word-level** `grammar -=`. This is the resolution of what used to be the lone gap (I6's `Extend only`): rather than a word-addressed split operation, it's simply the coarse grain of `-=`, addressed at the verb object you name in the `extend`:

```bgl
// The keypad: 'enter' and 'put' belong to library verbs; take them for the keypad.
extend Enter { grammar -= { {.enter} }; }    // evict 'enter' from the library's Enter verb
extend PutOn { grammar -= { {.put}   }; }    // evict 'put' from the library's PutOn verb

verb Keypad {                                 // ...then just declare them, ordinarily
    grammar = {
        {.type,  number, .into, noun},
        {.enter, number, .into, noun},
        {.put,   number, .into, noun},
    };
    void handler(){ ... }
}
```

Under the hood each eviction lowers to I6 `Extend only 'w' replace` (the one directive that reaches a library verb's opaque grammar, since it needs no patterns), and Keypad's reclaimed lines fold into it. A word nobody reclaims is simply **disabled**: `extend Disturb { grammar -= { {.xyzzy} }; }` on its own strips `xyzzy` from the parser. See "Removing grammar in Beguile" above for the full two-layer model and its diagnostics.

Why this shape rather than a dedicated `splitVerb` / `release` keyword: word-eviction has no purpose in a *pure*-Beguile program (there, competing verbs simply coexist and are resolved by pattern/priority/handler, and any grammar you want gone you `-=` by line, since native grammar is transparent). Its one irreducible job is reaching the *opaque* grammar of an extern verb — so it belongs as the coarse grain of the removal operator we already have, addressed at a named verb object, not as a new word-addressed primitive that would reintroduce the micro-grammar the object model discards.

## Adding grammar to a library word: word-precise by default

Eviction (above) *removes* a library word. The other direction — *adding* a meaning to one — is where I6's synonym grouping would otherwise leak, and Beguile closes that leak by default.

I6 library verbs are often **grouped**: `Verb 'enter' 'cross' 'sit' 'lie' * …` claims four synonym words that share one grammar table. In I6, `Extend 'enter' …` would spread your new line across *all four* — so "cross 5 into keypad" would parse, which you never asked for. Beguile is word-centric (you named `enter`, not `cross`), so when a grammar line's trigger word belongs to a grouped library verb it lowers to **`Extend only 'w'` by default** — splitting just `enter` off, inheriting the library grammar, adding your line, leaving `cross`/`sit`/`lie` alone:

```bgl
verb Keypad {
    grammar = { {.enter, number, .into, noun} };   // no keyword needed
    void handler(){ ... }
}
// emits:  extend only 'enter' * number 'into' noun -> Keypad;
```

`enter` now also dials the keypad, still navigates rooms, and its synonyms are untouched — the object model's "name a word, affect that word" rule, honored at the library boundary. Solo library words (like `put`, a single-trigger I6 verb) have no synonyms to over-reach, so they stay plain `Extend`; brand-new words are a fresh `Verb`.

This is the counterpart to eviction: both peel `enter` off its group via `Extend only`, but eviction adds `replace` (wiping the library grammar — the word is *yours*), while the default keeps it (the word gains a meaning, *shares* the library's). Split-and-keep is the common case, so it's the keywordless default.

When you genuinely want I6's whole-group spread — a pattern that should reach every synonym of a verb — opt in per line with **`withI6Synonyms`** (a trailing modifier, after `reverse`):

```bgl
extend Take { grammar += { {.take, .all, .from, noun, withI6Synonyms} }; }
// emits:  extend 'take' * 'all' 'from' noun -> Take;   (take/get/carry all reach it)
```

The visibility lands where it belongs: the *precise* behavior (what you almost always want) is the silent default, and the *surprising* act — reaching into I6's grouping to spread across synonyms — is the thing you spell out. And because grouping is a purely I6 concept (Beguile never groups its own verbs), `withI6Synonyms` is named for it and never applies to anything native.

### Extern/library verbs

I6 `Extend` shows up in exactly one situation: a grammar line's trigger word belongs to an **extern/library** verb Beguile did not create:  

`extend Take { replace grammar = { {.take, ...} } }` // `take` belongs to a library 
