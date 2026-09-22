# 13 Dictionary Words, Verbs and Grammar

<!-- toc -->
- [13.1 Dictionary Words](#131-dictionary-words)
- [13.2 Verb Declarations](#132-verb-declarations)
  - [13.2.1 `handler()`](#1321-handler)
  - [13.2.2 `perform()`](#1322-perform)
  - [13.2.3 Extern Verbs and Claimed Words](#1323-extern-verbs-and-claimed-words)
  - [13.2.4 Meta Verbs](#1324-meta-verbs)
  - [13.2.5 Verb Priority](#1325-verb-priority)
- [13.3 Action Comparisons](#133-action-comparisons)
- [13.4 Grammar](#134-grammar)
  - [13.4.1 Grammar Types](#1341-grammar-types)
  - [13.4.2 Pattern Tokens](#1342-pattern-tokens)
  - [13.4.3 Alternation and Multi-trigger Lines](#1343-alternation-and-multi-trigger-lines)
  - [13.4.4 Line Modifiers: `reverse`, `withI6Synonyms`](#1344-line-modifiers-reverse-withi6synonyms)
  - [13.4.5 Grammar on Verbs and Grammar Objects](#1345-grammar-on-verbs-and-grammar-objects)
- [13.5 Extending Grammar](#135-extending-grammar)
  - [13.5.1 `grammar +=`](#1351-grammar-)
  - [13.5.2 `grammar -=`](#1352-grammar--)
  - [13.5.3 `replace grammar =`](#1353-replace-grammar-)
  - [13.5.4 Synonyms](#1354-synonyms)
<!-- /toc -->

## 13.1 Dictionary Words

**Syntax**

```syntax
.⟨word⟩
..⟨word⟩
dictionaryWord ⟨name⟩ = .⟨word⟩ ;
```

**Description**

`dictionaryWord` is the type of a dictionary entry, a token the parser matches against player input.
A singular word is written with a leading `.` and a plural word with a leading `..`; both forms have
type `dictionaryWord`. The token form, including internal hyphens and apostrophes, is specified in
§1.6.7.

Dictionary words are compared with `==` and `!=`, and are the trigger and literal tokens of grammar
patterns (§13.4.2). `print()` on a dictionary word prints the word's text; the `dictionaryWord`
runtime type and its printing behavior are specified in §21.5.3.

**Example**

```bgl
dictionaryWord w = .lamp;
if (w == .lamp) print(w);      // → lamp
```

**See also** §1.6.7, §21.5.3.

## 13.2 Verb Declarations

**Syntax**

```syntax
verb ⟨name⟩ {
    grammar = { { ⟨pattern⟩ } , … } ;
    void handler() { … }
    [ meta = true ; ]
    [ priority = ⟨n⟩ ; ]
    [ ⟨member⟩ … ]
}
```

**Description**

`verb` is an alias class for `object` (`alias class verb for object { … }`, declared by the core BLR,
§21.5.4) whose members are `grammarRuleList grammar`, `bool meta`, `int priority`, `handler()` and
`perform()`. A `verb` declaration creates a named object that is an instance of that
class. The body uses ordinary object member syntax (§11.4): the `grammar` member's type is inferred
from the class, so `grammar = { … }` needs no type, and further members and methods may be declared
as on any object.

A verb name follows the same resolution rules as any identifier: a local variable with the same name
shadows the verb (§3.10).

**Example**

```bgl
verb Examine {
    grammar = {
        {.examine, noun},
        {.x, noun},
    };
    void handler() {
        print("You examine it closely.");
    }
}
```

**Notes**

> **Shorthand.** When the first token inside `grammar = { … }` is a dictionary-word literal, the
> outer braces are read as the braces of a single grammar line, so the inner braces may be omitted:
> `grammar = {.whistle, noun};` is the pretty lie for the canonical
> `grammar = { {.whistle, noun} };`. `|`-alternation is allowed in the shorthand
> (`grammar = {.hum|.murmur, noun};`). It applies only when one line is being declared; several lines
> use the canonical form with one pair of braces per line. The shorthand is recognized wherever a
> grammar line list is accepted: verb declarations, `extend` blocks (`grammar += { … }`,
> `grammar -= { … }`, `replace grammar = { … }`), grammar objects, and extern verb bodies.

**See also** §11.2, §21.5.4.

### 13.2.1 `handler()`

**Syntax**

```syntax
void handler() { … }
```

**Description**

`handler()` is the verb's action body: it runs when the player enters a command matching the verb's
grammar. The `verb` class declares it as a `default` emitter method (§8.7.3), so a verb object overrides it
with an ordinary `void handler()` and needs no `replace`. A non-extern verb must define `handler()`; omitting it is a
compile-time error. Extern
verbs (§13.2.3) are exempt.

**Example**

```bgl
verb Jump {
    grammar = { {.jump} };
    void handler() { print("You jump on the spot."); }
}
```

### 13.2.2 `perform()`

**Syntax**

```syntax
⟨verb⟩.perform() ;
⟨verb⟩.perform( ⟨noun⟩ ) ;
⟨verb⟩.perform( ⟨noun⟩ , ⟨second⟩ ) ;
```

**Description**

`perform()` runs the verb's action from code. The active IF library's action runner is used: it saves
and restores the current actor, action, noun and second, and runs the surrounding before/after rules.
`perform()` works on any verb, native or `extern`, whether or not it defines a `handler()`. To run an
action and then return true from the enclosing function, follow the call with an explicit `rtrue;`.

**Example**

```bgl
Enter.perform(door);
Take.perform(coin, pouch);
Take.perform(coin); rtrue;
```

### 13.2.3 Extern Verbs and Claimed Words

**Syntax**

```syntax
extern verb ⟨name⟩ ;
extern verb ⟨name⟩ { .⟨word⟩ [ | .⟨word⟩ ] … }
extern verb ⟨name⟩ { grammar = { { .⟨word⟩ [ | .⟨word⟩ ] … } , … } ; }
```

The `|` in these forms is the literal alternation token (§13.4.3), not notation.

**Description**

A verb whose behavior is defined by the I6 library is declared `extern verb`. The declaration
registers the name for `switch (action)` comparisons, grammar lines, `perform()` and method calls. An
extern verb needs no `handler()`.

An I6 verb may claim several trigger words (`inventory`, `inv`, `i`). Declaring them in the extern
verb's body makes the claims visible, so that a grammar line elsewhere that uses one of those words
extends the library verb rather than defining a new one. The body uses the same `grammar` syntax as a
native verb, and these rules apply:

- Several words are listed with `|`-alternation (§13.4.3). Splitting the words over several lines or
  listing them all in one line yields the same claimed-word set.
- Pattern tokens after the trigger word are ignored, and writing one is a warning. No grammar is
  defined for an extern verb from Beguile; only the words in first position contribute.
- A bare `extern verb Name;` claims a single word, the lowercased verb name. The body form is needed
  only when the verb claims additional words or its primary word differs from its name.
- The first word of the first grammar line is the verb's *primary trigger*, the word used as the
  target when the verb is extended (§13.5). All listed words are equally claimed for collision
  detection.

Extern verbs cannot be marked `meta` and cannot carry a `priority`.

> **Shorthand.** When an extern verb's body begins with a dictionary-word literal, the body is read
> as the trigger-word section of a single grammar line: `extern verb Inv { .inventory|.inv|.i }` is
> the pretty lie for `extern verb Inv { grammar = { {.inventory|.inv|.i} }; }`. The shorthand accepts
> trigger words only; a body with several lines or other members uses the canonical form.

**Example**

```bgl
extern verb Take;
extern verb Inv  { .inventory|.inv|.i }
extern verb Quit { .q|.quit|.die }
```

**See also** §15.4.2, §23.3.6.

### 13.2.4 Meta Verbs

**Syntax**

```syntax
meta = true ;
```

**Description**

Setting the `verb` member `meta` to `true` marks the verb as out-of-world: it runs without advancing
the turn and without triggering daemons or timers. `meta` is recognized only on `verb` instances and
is not a run-time property. Extern verbs cannot be marked meta; the I6 declaration already carries
the marking or does not.

**Example**

```bgl
verb Inventory {
    meta = true;
    grammar = { {.inventory}, {.i} };
    void handler() { … }
}
```

### 13.2.5 Verb Priority

**Syntax**

```syntax
priority = ⟨n⟩ ;
extend ⟨verb⟩ { priority = ⟨n⟩ ; grammar += { … } ; }
grammarRule ⟨name⟩ = { ⟨verb⟩ , { ⟨pattern⟩ } , ⟨n⟩ } ;
```

**Description**

Priority orders grammar lines that come from several sources and target the same trigger word. A
lower number is tried earlier by the parser. The `verb` member `priority` defaults to `10`; it is
recognized only on `verb` instances and is not a run-time property.

A verb's *anchor* is the priority declared in its own `verb` body, or `10` when none is declared
(an extern verb always has the default anchor). Every other contribution to that verb's trigger
words sorts relative to the anchor: a contribution with a lower priority is tried before the verb's
own lines, a higher one after. The verb that a `synonyms` list attaches to (§13.5.4) is not an anchor
in this sense.

| Source | Syntax | Scope | Default |
|---|---|---|---|
| Verb body (the anchor) | `priority = N;` inside `verb V { … }` | The verb's own lines; stored on the verb | `10` |
| `extend` block | `priority = N;` inside `extend V { … }` | Every line added by that block's `grammar +=`; not stored on the verb, so several `extend` blocks at different priorities coexist on one verb | `10` |
| Grammar-object rule | Third positional element of a `grammarRule` initializer, `{V, {pattern}, N}` | That one rule, sorted against its target verb's anchor; a grammar object has no block-level default, so the value is repeated per rule | `10` |
| Inferred-verb line | none | A line written `{pattern}` takes the owning verb's anchor, or the enclosing `extend` block's priority | — |

Combining `priority = N;` with `replace grammar = { … }` in the same `extend` block is a compile-time
error (§13.5.3). An extern verb cannot carry a `priority` in its declaration (§13.2.3).

**Ordering.** For each trigger word of a verb, lines with `priority < anchor` are tried before the
verb's own lines; the verb's own lines come next; lines with `priority ≥ anchor` from other sources
come last. Per-rule and block-local priorities participate in the same ordering.

**Example**

```bgl
verb Take {
    priority = 5;
    grammar = { {.take, noun}, {.grab, noun} };
    void handler() { … }
}

extend Look {
    priority = 5;                              // before Look's own lines
    grammar += { {.peek, noun} };
}

extend Look {
    priority = 12;                             // after Look's own lines
    grammar += { {.look, .carefully, noun} };
}

grammar additions {
    grammarRule r1 = {Take, {.nab, noun}};             // 10
    grammarRule r2 = {Drop, {.toss, held}, 5};         // 5
}
```

**See also** §13.4.5, §13.5.

## 13.3 Action Comparisons

**Syntax**

```syntax
action == ⟨verb⟩
action != ⟨verb⟩
switch (action) { case ⟨verb⟩ : … }
```

**Description**

The library variable `action` has type `verb`. It is compared against a verb name with `==` and `!=`,
and a `switch` on `action` takes verb names as case values (§5.12).

**Example**

```bgl
if (action == Take) { … }

switch (action) {
    case Take: print("Taken.");
    case Drop: print("Dropped.");
}
```

**See also** §5.12, §23.3.4.

## 13.4 Grammar

Grammar lines define what the player may type and which verb they trigger. A line is declared either
on a verb (§13.4.5) or in a grammar object, and both forms have the same effect.

### 13.4.1 Grammar Types

**Description**

| Type | Purpose |
|---|---|
| `grammarToken` | An extern enum declared by the IF library binding (§23.3.6); its values are the parser tokens `noun`, `held`, `creature`, …. Both the bare value (`held`) and the qualified form (`grammarToken.held`) are valid in pattern position. `noun(Routine)` and `scope(Routine)` are its parameterized forms. |
| `patternElement` | One element of a pattern: a dictionary word or a grammar token. A pattern is an `array<patternElement>`, written `{.examine, noun}`. |
| `grammarRule` | One verb-targeted pattern, with an optional priority: `{Examine, {.examine, noun}}` or `{Examine, {.examine, noun}, 5}`. |
| `grammarRuleList` | A list of grammar rules; the type of the `grammar` member on `verb` and of a grammar object. |

A `grammarRule` has two initializer forms:

- **Explicit verb**: `{Verb, {pattern}[, priority]}`. Valid in any context.
- **Inferred verb**: `{pattern}`. The verb is the owning object, which should be a `verb` or a
  subclass of `verb`; otherwise a warning is issued. The priority is the owning verb's anchor or the
  enclosing `extend` block's (§13.2.5).

A `grammarRule` member takes exactly one `{verb, {pattern}}` pair; an `array<grammarRule>` member
takes a list of them. Any other shape is a compile-time error.

**See also** §21.5.5.

### 13.4.2 Pattern Tokens

**Description**

Each element of a pattern is one of the following.

| Token | Matches |
|---|---|
| `.word` | the player typing that word |
| `..words` | the plural form of the word |
| `.w1 \| .w2` | any one of the listed words; may be parenthesized |
| `noun` | any in-scope object |
| `held` | a held object |
| `creature` | a creature or actor |
| `topic` | a topic phrase |
| `multi` | one or more in-scope objects |
| `multiheld` | one or more held objects |
| `multiexcept` | one or more in-scope objects, excluding one already matched (used after a preposition: `multiexcept, .in, noun`) |
| `multiinside` | one or more objects inside a specific container (used after a preposition: `multiinside, .from, noun`) |
| `number` | a number typed by the player, range-checked |
| `anynumber` | any number, no range check |
| `special` | a number or a dictionary word |
| *attributeName* | an object that has that attribute (`container`, `animate`) |
| *RoutineName* | a general parsing routine: a global `bool` function the parser calls to consume input words and report a match |
| `noun(Routine)` | a noun filter: the parser matches nouns normally, then calls the global `bool` routine with each candidate object, which accepts or rejects it |
| `scope(Routine)` | a scope setter: the global `bool` routine decides which objects are in scope for this line, using the library's scope routines (`PlaceInScope()`, `ScopeWithin()`) |

A bare identifier in a pattern must be declared as a `grammarToken`, an `attribute`, or a global
function; any other declaration, or an undeclared name, is a compile-time error. In pattern position
a bare `noun` resolves to the grammar token even when an `extern object noun` exists at file scope;
outside a pattern the global wins, and `grammarToken.noun` selects the token explicitly.

**Example**

```bgl
bool isEdible(object obj) { return obj.has(edible); }

verb Taste {
    grammar = { {.taste, noun(isEdible)} };
    void handler() { … }
}

bool parseColor(int context) { … }

verb Paint {
    grammar = { {.paint, noun, parseColor} };      // "paint <object> <color>"
    void handler() { … }
}

verb Chat {
    grammar = { {.chat, animate, .about, topic} };
    void handler() { … }
}
```

**See also** §11.6, §23.3.6.

### 13.4.3 Alternation and Multi-trigger Lines

**Description**

Dictionary words separated by `|` match any one of them, and the group may be parenthesized.
Alternation is allowed in any position, including the first.

When alternation appears in the first position, the line declares one pattern that fires on any of
the listed trigger words. The line behaves as one line per word: each trigger word is treated
independently, so `grammar -=` (§13.5.2) can remove a single word's line, and each word is routed on
its own. A new word defines that word's grammar; a word already claimed by another verb (a library
verb through its claimed words, or an earlier Beguile verb) adds the line to that word's grammar. The
same applies to multi-trigger lines inside `extend V { grammar += { … } }`.

**Example**

```bgl
verb Stow {
    grammar = {
        {.stow, held, .on | .onto | .upon, noun},
        {.stow, held, (.in | .into | .inside), noun},
    };
    void handler() { … }
}

verb TypeNum {                                     // three trigger words: one line per word
    grammar = { {.type | .enter | .put, number, .into | .in | .on | .onto, noun} };
    void handler() { print("You can't type anything there."); }
}
```

### 13.4.4 Line Modifiers: `reverse`, `withI6Synonyms`

**Syntax**

```syntax
{ ⟨pattern⟩ [ , reverse ] [ , withI6Synonyms ] }
```

**Description**

Two pseudo-tokens may end a grammar line, in this order. Neither is matched against input.
`withI6Synonyms`, when present, must be last. The dotted forms `.reverse` and `.withI6Synonyms` are
ordinary dictionary words and are unaffected.

- `reverse` swaps `noun` and `second` when the action receives its parsed arguments:
  `{.give, creature, held, reverse}`.
- `withI6Synonyms` widens the line's effect on a library verb. A library verb may group several
  synonym words under one grammar table; when a line's trigger word belongs to such a group, the line
  affects only that word and its synonyms are untouched. With `withI6Synonyms` the line applies to
  every word in the group. On a native word, or a library word with no synonyms, the modifier has no
  effect.

In I6 terms: a line whose trigger word belongs to a library verb's synonym group is emitted as
`Extend only 'w'`, which splits `w` off the group, keeps the library grammar it inherits and adds the
line to `w` alone; with `withI6Synonyms` the line is emitted as `Extend 'w'`, which I6 applies to
every word of the group. A library word with no synonyms is emitted as a plain `Extend`, and a new
word as a fresh `Verb`. Background, non-normative: [Verbs and Grammar](../Verbs-Grammar.md).

**Example**

```bgl
verb Keypad {
    grammar = { {.enter, number, .into, noun} };    // "enter 5 into keypad"; "cross" unaffected
    void handler() { … }
}

extend Take { grammar += { {.take, .all, .from, noun, withI6Synonyms} }; }   // take/get/carry/…
```

### 13.4.5 Grammar on Verbs and Grammar Objects

**Syntax**

```syntax
verb ⟨name⟩ { grammar = { { ⟨pattern⟩ } , … } ; … }

grammar ⟨name⟩ {
    grammarRule ⟨rule⟩ = { ⟨verb⟩ , { ⟨pattern⟩ } [ , ⟨priority⟩ ] } ;
    array<grammarRule> ⟨rules⟩ = { { ⟨verb⟩ , { ⟨pattern⟩ } [ , ⟨priority⟩ ] } , … } ;
}
```

**Description**

The `grammar` member of a verb is a `grammarRuleList` whose lines use the inferred-verb form; the verb
is the owning object.

A `grammar` declaration declares an object of class `grammarRuleList`. A grammar object is
cross-cutting: one object may carry rules targeting many verbs, each rule pairing a pattern with an
explicit verb. Member types may be inferred, and an `array<grammarRule>` member holds several rules.
Per-rule priority is the optional third element (§13.2.5).

**Example**

```bgl
grammar customPatterns {
    rule1 = {PutOn, {.hang, held, .on, noun}};
    array<grammarRule> rules = {
        {PutOn,  {.put, held, .on, noun}},
        {Insert, {.put, held, .in, noun}, 5},
    };
}
```

**See also** §13.2.5, §21.5.5.

## 13.5 Extending Grammar

**Syntax**

```syntax
extend ⟨verb⟩ {
    [ priority = ⟨n⟩ ; ]
    grammar += { { ⟨pattern⟩ } , … } ;
    grammar -= { { .⟨word⟩ , ⟨pattern⟩ } , … } ;
    grammar -= { { .⟨word⟩ } , … } ;
    replace grammar = { { ⟨pattern⟩ } , … } ;
    synonyms = { .⟨word⟩ , … } ;
}
```

The two `grammar -=` forms are the line-level and word-level shapes of §13.5.2; entries of both shapes
may be mixed in one list.

**Description**

Grammar is added to or removed from an existing verb, including an `extern verb`, inside an
`extend V { … }` body (§11.10). An operator is required: bare `grammar = { … }` is valid only in the
original `verb` or `extern verb` declaration and is a compile-time error inside an `extend`. A bare
`priority = N;` in the block applies to the lines it appends (§13.2.5).

### 13.5.1 `grammar +=`

**Description**

`grammar += { … }` appends lines to the verb's grammar. Multi-trigger lines (§13.4.3) and line
modifiers (§13.4.4) are accepted.

**Example**

```bgl
extern verb PutOn;
extend PutOn {
    grammar += { {.hang, held, .on, noun} };
}
```

### 13.5.2 `grammar -=`

**Description**

`grammar -= { … }` removes grammar. The grain of the removal is set by how much of the line is named:

- **Line-level** — `{.w, pattern…}` removes the one line that matches exactly: same trigger word,
  same tokens, same `reverse` flag.
- **Word-level** — `{.w}` removes all of that word's grammar.

A `-=` entry must match a line exactly; a partial (prefix) pattern matches nothing:
`-= { {.give, noun} }` removes only the line whose pattern is exactly `noun`, never
`{.give, noun, .to, noun}`. An alternation in a `-=` entry (`.a|.b|.c`) is one removal per word, each
matched and warned separately. Removal is source-ordered: a `-=` sees only lines declared before it,
so a later `+=` of the same line is unaffected. A `-=` that matches nothing is a warning.

**Example**

```bgl
verb TypeNum { grammar = { {.type|.dial, .into, noun}, {.dial, .to, noun} }; void handler() { … } }
extend TypeNum { grammar -= { {.dial, .into, noun} }; }   // that one dial line
extend TypeNum { grammar -= { {.dial} }; }                // every dial line
```

**Extern verbs.** For a library verb Beguile knows the words it claims and the lines Beguile itself
added with `+=`, but the library's own patterns are opaque. A line-level `-=` therefore matches only
Beguile-added lines; naming a claimed word with a pattern that matches none of them is a warning. A
word-level `-=` on a claimed word *evicts* the word from the library verb: if a native verb also
declares that word, its lines take the word over; otherwise the word is disabled. The word must be
genuinely claimed by the library verb.

In I6 terms, eviction is emitted as `Extend only 'w' replace`: the one directive that reaches a
library verb's opaque grammar, since it names no patterns. It detaches `w` from the library verb and
discards the library's grammar for it; the lines a native verb declares for `w` are folded into that
directive and become the word's complete grammar, and with no such lines the word matches nothing.
Background, non-normative: [Verbs and Grammar](../Verbs-Grammar.md).

```bgl
extend Enter  { grammar -= { {.enter} }; }      // evict 'enter' from the library verb …
verb  Keypad  { grammar = { {.enter, number, .into, noun} }; void handler() { … } }   // … and reclaim it
extend Disturb { grammar -= { {.xyzzy} }; }     // no reclaimer: 'xyzzy' is disabled
```

**See also** §13.2.3, §13.4.3.

### 13.5.3 `replace grammar =`

**Description**

`replace grammar = { … }` discards all of the verb's grammar, every trigger word and every prior `+=`
and `-=`, whatever their source order, and the listed lines become its complete grammar. Trigger
words not named in the replacement are dropped. Only this verb is affected; a word that another verb
also uses keeps its grammar there. On an extern verb the replacement overrides the library's grammar
for the named words. Combining `replace grammar =` with `priority = N;` in the same block is a
compile-time error.

**Example**

```bgl
verb TypeNum { grammar = { {.type|.dial, .into, noun} }; void handler() { … } }
extend TypeNum {
    replace grammar = { {.dial, .to, noun} };     // 'type' is dropped
}

extend Take {
    replace grammar = { {.take, .firmly, noun} };  // overrides the library's 'take'
}
```

**See also** §13.2.5.

### 13.5.4 Synonyms

**Description**

`synonyms = { .word, … }` inside an `extend` makes the listed words true aliases of the extended verb:
they share the verb's grammar rather than copying it, so a later `extend` of the verb reaches every
synonym. By contrast, adding the words as separate trigger lines with `grammar +=` copies the pattern
set at that point, and later extensions do not reach them.

The alias is attached to the verb's primary trigger word (its first claimed word, or its name if it
declares none). `synonyms` works on both extern and native verbs. One-letter and plural word
conventions apply (§1.6.7). An empty `synonyms = { }` is a compile-time error.

There is no Beguile form for I6's `Extend 'w' only` directive; if needed, write it in an `#i6` island
(§15.2).

**Example**

```bgl
extend Take { synonyms = {.steal, .grab, .pilfer}; }
extend Take { grammar += { {.take, .quietly, noun} }; }   // also "steal quietly", "grab quietly"
```

**See also** §13.2.3, §15.2.
