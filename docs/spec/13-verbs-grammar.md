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
  - [13.4.4 Line Modifiers: `REVERSE`, `withI6Synonyms`](#1344-line-modifiers-reverse-withi6synonyms)
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
.WORD
..WORD
dictionaryWord NAME = .WORD ;
```

`dictionaryWord` is the type of a dictionary entry:  a token the parser matches against the player's input.  A singular word is written with a leading `.` and a plural word with a leading `..`; both have the type `dictionaryWord`.  The token form, including internal hyphens and apostrophes, is covered in §1.6.7.

Dictionary words are compared with `==` and `!=`; the `dictionaryWord` runtime type and its printing behavior are covered in §21.5.3.

**Example**

```bgl
dictionaryWord w = .lamp;
if (w == .lamp) print(w);      // -> lamp
```

**See also** §1.6.7, §21.5.3.

## 13.2 Verb Declarations

**Syntax**

```syntax
verb NAME {
    grammar = { { PATTERN } , … } ;
    void handler() { … }
    [ meta = true ; ]
    [ priority = N ; ]
    [ MEMBER … ]
}
```

`verb` is an alias class for `object` (§21.5.4) whose members are `grammarRuleList grammar`, `bool meta`, `int priority`, `handler()`, and `perform()`.  A `verb` declaration creates a named instance of that class.  The body uses ordinary object member syntax (§11.4).

A class can derive from `verb` to share behavior among several verbs.  A verb is then declared through that class, `CLASS NAME { … }`, and is a verb in every respect.

```bgl
class chatVerb : verb {
    string reply = "Nothing to say.";
    void handler() { print(reply); }
}
chatVerb Hum     { grammar = {.hum}; reply = "You hum."; }
chatVerb Whistle { grammar = {.whistle}; }

class infoVerb : verb { meta = true; }
infoVerb Credits { grammar = {.credits}; void handler() { print("Written by …"); } }
```

A variable, parameter, or array element of type `verb` can hold any verb, native or `extern`.  One typed as a subclass of `verb` holds a verb declared through that subclass, and can reach its members.

```bgl
class ChatVerb : verb { string reply; void handler() { print(reply); } }
ChatVerb Hum     { grammar = {.hum};     reply = "You hum."; }
ChatVerb Whistle { grammar = {.whistle}; reply = "You whistle."; }
array<ChatVerb> chats = { Hum, Whistle };

ChatVerb v = chats[1];
v.perform();                 // runs the Whistle action
if (v == action) { … }
verb any = v;                // any verb fits a `verb`
print(v.reply);
```

A verb name resolves like any identifier:  a local variable of the same name will shadow the verb (§3.10).

A verb named like a library verb (`verb Empty` beside the binding's `extern verb Empty`) is a compile-time error, and you extend the library verb instead (§13.5).  A program's own verb can't take a fake action's name either (§13.2.3).  A verb can share its name with a class (`verb Seal` beside `struct Seal`).

In the generated Inform 6, the verb's action and its action routine keep the verb's name (`##Size`, `SizeSub`), while the object carrying `handler()` is named `_bglVerb_NAME`.  I6 code naming the verb object reaches it through `$i6Name` (§7.3.3).  A verb whose action routine would take the name of a routine the binding declares (`verb Multi` and the library's `MultiSub`) is a compile-time error.

**Example**

```bgl
verb Inspect {
    grammar = {
        {.inspect, NOUN},
        {.study, NOUN},
    };
    void handler() {
        print("You inspect it closely.");
    }
}
```

> ***Note:** When the first token inside `grammar = { … }` is a dictionary-word literal, the outer braces are read as those of a single grammar line, so the inner braces can be omitted:  `grammar = {.whistle, NOUN};` is equivalent to `grammar = { {.whistle, NOUN} };`.  `|`-alternation is allowed in this form (`grammar = {.hum|.murmur, NOUN};`).  The single-line form is recognized wherever a grammar line list is accepted.*

**See also** §11.2, §21.5.4.

### 13.2.1 `handler()`

**Syntax**

```syntax
void handler() { … }
```

`handler()` is the verb's action body:  it will run when the player enters a command matching the verb's grammar.  The `verb` class declares it `required default` (§8.3.6), so a verb declared in Beguile must provide one; omitting it is a compile-time error.  The `default` body, which runs the verb's I6 routine, is used only by extern verbs (§13.2.3).

**Example**

```bgl
verb Twirl {
    grammar = { {.twirl} };
    void handler() { print("You twirl on the spot."); }
}
```

### 13.2.2 `perform()`

**Syntax**

```syntax
VERB.perform() ;
VERB.perform( NOUN ) ;
VERB.perform( NOUN , SECOND ) ;
```

`perform()` runs the verb's action from code.  It uses the active IF library's action runner, which saves and restores the current actor, action, noun, and second, and runs the surrounding before/after rules.  (The Inform 6 standard library skips the before rules while a `meta` verb is running.)

`perform()` works on any verb, native or `extern`, whether or not it defines a `handler()`.  To run an action and then return true from the enclosing function, follow the call with an explicit `rtrue;`.

**Example**

<!-- doctest: skip -->
```bgl
Enter.perform(door);
Take.perform(coin, pouch);
Take.perform(coin); rtrue;
```

### 13.2.3 Extern Verbs and Claimed Words

**Syntax**

```syntax
extern verb NAME { }
extern verb NAME { .WORD [ | .WORD ] … }
extern verb NAME { grammar = { { .WORD [ | .WORD ] … } , … } ; }
```

The `|` in these forms is the literal alternation token (§13.4.3), not notation.

A verb whose behavior is defined by the I6 library is declared `extern verb` with a body:  its trigger words, or `{ }` for an action the player can't type.  An extern verb needs no `handler()`; it falls back to the `default` one from `verb`, which runs the I6 routine (`<name>Sub`).

Without a body, `extern verb NAME;` declares a *variable* of type `verb`, as any class-typed declaration without a body does (`extern object noun;`).  The library's `action` is declared this way.

An I6 verb can claim several trigger words (`inventory`, `inv`, `i`).  Declaring them in the extern verb's body makes the claims visible, so a grammar line elsewhere using one of those words will extend the library verb rather than define a new one.  The body uses the same `grammar` syntax as a native verb, with these rules:

- Several words are listed with `|`-alternation (§13.4.3).  Splitting them over several lines or listing them all in one yields the same claimed-word set.
- Pattern tokens after the trigger word are ignored, and writing one is a warning.
- The first word of the first grammar line is the verb's *primary trigger*, the word used as the target when the verb is extended (§13.5).  All listed words are equally claimed for collision detection.

Extern verbs can't be marked `meta` and can't carry a `priority`.

When an extern verb's body begins with a dictionary-word literal, the body is read as the trigger-word section of a single grammar line:  `extern verb Inv { .inventory|.inv|.i }` is equivalent to `extern verb Inv { grammar = { {.inventory|.inv|.i} }; }`.  This form accepts trigger words only; a body with several lines or other members uses the full form.

**Example**

```bgl
extern verb Take { .take|.get }
extern verb Inv  { .inventory|.inv|.i }
extern verb Quit { .q|.quit|.die }
```

**See also** §16.4.2, §23.3.6.

### 13.2.4 Meta Verbs

**Syntax**

```syntax
meta = true ;
```

Setting the `verb` member `meta` to `true` marks the verb as out-of-world:  it will run without advancing the turn or triggering daemons or timers.  `meta` is recognized only on `verb` instances and is not a run-time property.  Extern verbs can't be marked meta (§13.2.3).

Inform 6 records meta-ness on the dictionary word, not on the action.  A word the library's grammar already declares (`wipe`, `shut`) keeps the library's non-meta marking, so a meta verb claiming such a word will still take a turn when the player types it; the compiler warns.  Give a meta verb words of its own.

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
priority = N ;
extend VERB { priority = N ; grammar += { … } ; }
grammarRule NAME = { VERB , { PATTERN } , N } ;
```

Priority orders grammar lines which come from several sources and target the same trigger word; the parser tries a lower number earlier.  The `verb` member `priority` defaults to `10`; it is recognized only on `verb` instances and is not a run-time property.

A verb's *anchor* is the priority declared in its own `verb` body, or `10` when none is declared (an extern verb always has the default anchor).  A program verb whose own grammar uses a word a library verb already owns is placed against the library's lines by the `verb` member `libraryPriority`, which is `20` by default; with an anchor below it (the default), the program verb's lines are tried first.  Every other contribution to that verb's trigger words sorts relative to the anchor (see **Ordering** below).  The verb a `synonyms` list attaches to (§13.5.4) is not an anchor in this sense.

| Source | Syntax | Scope | Default |
|---|---|---|---|
| Verb body (the anchor) | `priority = N;` inside `verb V { … }` | The verb's own lines; stored on the verb | `10` |
| `extend` block | `priority = N;` inside `extend V { … }` | Every line added by that block's `grammar +=`; not stored on the verb | `10` |
| Grammar-object rule | Third positional element of a `grammarRule` initializer, `{V, {pattern}, N}` | That one rule, sorted against its target verb's anchor; a grammar object has no block-level default, so the value is repeated per rule | `10` |
| Inferred-verb line | none | A line written `{pattern}` takes the owning verb's anchor, or the enclosing `extend` block's priority | - |

Combining `priority = N;` with `replace grammar = { … }` in the same `extend` block is a compile-time error (§13.5.3).  An extern verb can't carry a `priority` in its declaration (§13.2.3).

**Ordering.**  For each trigger word of a verb, lines with `priority < anchor` are tried before the verb's own lines, the verb's own lines come next, and lines with `priority >= anchor` from other sources come last.  Per-rule and block-local priorities take part in the same ordering.

**Example**

```bgl
verb Pocket {
    priority = 5;
    grammar = { {.pocket, NOUN}, {.stash, NOUN} };
    void handler() { … }
}

extend Look {
    priority = 5;                              // before Look's own lines
    grammar += { {.peek, NOUN} };
}

extend Look {
    priority = 12;                             // after Look's own lines
    grammar += { {.look, .carefully, NOUN} };
}

grammar additions {
    grammarRule r1 = {Pocket, {.nab, NOUN}};           // 10
    grammarRule r2 = {Drop, {.toss, HELD}, 5};         // 5
}
```

**See also** §13.4.5, §13.5.

## 13.3 Action Comparisons

**Syntax**

```syntax
action == VERB
action != VERB
switch (action) { case VERB : … }
```

The library variable `action` has the type `verb`.  You can compare it against a verb name with `==` and `!=`, and a `switch` on `action` takes verb names as its case values (§5.12).

**Example**

```bgl
if (action == Take) { … }

switch (action) {
    case Take: print("Taken.");
    case Drop: print("Dropped.");
}
```

A verb which shares its name with a `#define` symbol is still a verb where one is expected (§3.8.3).

**See also** §5.12, §23.3.4.

## 13.4 Grammar

Grammar lines define what the player can type and which verb it will trigger.  A line is declared on a verb (§13.4.5) or in a grammar object, to the same effect.

### 13.4.1 Grammar Types

The following types are used to declare grammar:

| Type | Purpose |
|---|---|
| `grammarToken` | An extern enum declared by the IF library binding (§23.3.6); its values are the parser tokens `NOUN`, `HELD`, `CREATURE`, ….  Both the bare value (`HELD`) and the qualified form (`grammarToken.HELD`) are valid in pattern position.  `NOUN(Routine)` and `SCOPE(Routine)` are its parameterized forms. |
| `patternElement` | One element of a pattern:  a dictionary word, a grammar token, an attribute, or a general parsing routine (§13.4.2).  A pattern is an `array<patternElement>`, written `{.examine, NOUN}`.  A program writes patterns in a verb's `grammar`; declaring a variable or member of this type is a compile-time error. |
| `grammarRule` | One verb-targeted pattern, with an optional priority:  `{Examine, {.examine, NOUN}}` or `{Examine, {.examine, NOUN}, 5}`. |
| `grammarRuleList` | A list of grammar rules; the type of the `grammar` member on `verb` and of a grammar object. |

`patternElement` is the base type of one element of a grammar pattern.  These types are the receivers of the grammar operators (`=`, `+=`, `-=`, `replace … =`).

A `grammarRule` has two initializer forms:

- **Explicit verb**:  `{Verb, {pattern}[, priority]}`.  Valid in any context.
- **Inferred verb**:  `{pattern}`.  The verb is the owning object, which should be a `verb` or a subclass of `verb`; otherwise a warning is issued.  The priority is the owning verb's anchor or the enclosing `extend` block's (§13.2.5).

A `grammarRule` member takes exactly one `{verb, {pattern}}` pair, while an `array<grammarRule>` member takes a list of them.  Any other shape is a compile-time error.


### 13.4.2 Pattern Tokens

Each element of a pattern is one of the following:

| Token | Matches |
|---|---|
| `.word` | the player typing that word |
| `..words` | the plural form of the word |
| `.w1 \| .w2` | any one of the listed words; may be parenthesized |
| `NOUN` | any in-scope object |
| `HELD` | a held object |
| `CREATURE` | a creature or actor |
| `TOPIC` | a topic phrase |
| `MULTI` | one or more in-scope objects |
| `MULTIHELD` | one or more held objects |
| `MULTIEXCEPT` | one or more in-scope objects, excluding one already matched (used after a preposition:  `MULTIEXCEPT, .in, NOUN`) |
| `MULTIINSIDE` | one or more objects inside a specific container (used after a preposition:  `MULTIINSIDE, .from, NOUN`) |
| `NUMBER` | a number typed by the player, range-checked |
| `ANYNUMBER` | any number, no range check |
| `SPECIAL` | a number or a dictionary word |
| *attributeName* | an object that has that attribute (`container`, `animate`) |
| *RoutineName* | a general parsing routine:  a global function the parser calls to consume input words (`NextWord()`, `wn`); it returns -1 for no match, 0 when it matched words but no value, 1 when it put a number in `parsed_number`, or the object it matched |
| `NOUN(Routine)` | a noun filter:  the parser matches nouns normally, puts each candidate object in `noun`, and calls the global `bool` routine, which accepts or rejects it |
| `SCOPE(Routine)` | a scope setter:  the global `bool` routine decides which objects are in scope for this line, using the library's scope routines (`PlaceInScope()`, `ScopeWithin()`) |

A `NUMBER`, `ANYNUMBER`, or `SPECIAL` token fills a noun slot as an object token does:  the number becomes `noun` (or `second`, for the line's second value), which a handler can read with a cast, `(int)second`.  The library's `parsed_number` holds the last number matched.  A `TOPIC` token takes a slot too:  in `{.type, TOPIC, .on, NOUN}` the object is `second`, and `noun` is 0.

The parser calls each routine token with no arguments, so a routine named in a pattern which declares parameters is a compile-time error.

Both bindings declare `scope_stage`, the pass (1, 2, or 3) the parser is making when it calls a scope routine.  Objects are placed in scope on pass 2, as in `if (scope_stage == 2) { PlaceInScope(obj); }`.

A bare identifier in a pattern must be declared as a `grammarToken`, an `attribute`, or a global function; any other declaration, or an undeclared name, is a compile-time error.  In pattern position a bare `NOUN` resolves to the grammar token even when an `extern object noun` exists at file scope; however, outside a pattern the global wins, and `grammarToken.NOUN` selects the token explicitly.

By convention, grammar tokens are written in uppercase (`NOUN`, `HELD`, `REVERSE`).  Beguile is case-insensitive (§1.3), so `noun` in a pattern is the same token.

**Example**

```bgl
bool isEdible() { return noun.has(edible); }

verb Taste {
    grammar = { {.taste, NOUN(isEdible)} };
    void handler() { … }
}

int parseColor() { … }

verb Paint {
    grammar = { {.paint, NOUN, parseColor} };      // "paint OBJECT COLOR"
    void handler() { … }
}

verb Chat {
    grammar = { {.chat, animate, .about, TOPIC} };
    void handler() { … }
}
```

**See also** §11.6, §23.3.6.

### 13.4.3 Alternation and Multi-trigger Lines

Dictionary words separated by `|` will match any one of them, and the group can be parenthesized.  Alternation is allowed in any position, including the first.

In the first position, alternation declares one pattern which fires on any of the listed trigger words.  The line behaves as one line per word, each treated independently (§13.5.2).  A new word defines that word's grammar, while a word already claimed by another verb (a library verb through its claimed words, or an earlier Beguile verb) adds the line to that word's grammar.  The same applies to multi-trigger lines inside `extend V { grammar += { … } }`.

**Example**

```bgl
verb Stow {
    grammar = {
        {.stow, HELD, .on | .onto | .upon, NOUN},
        {.stow, HELD, (.in | .into | .inside), NOUN},
    };
    void handler() { … }
}

verb TypeNum {                                     // three trigger words: one line per word
    grammar = { {.type | .enter | .put, NUMBER, .into | .in | .on | .onto, NOUN} };
    void handler() { print("You can't type anything there."); }
}
```

### 13.4.4 Line Modifiers: `REVERSE`, `withI6Synonyms`

**Syntax**

```syntax
{ PATTERN [ , REVERSE ] [ , withI6Synonyms ] }
```

Two pseudo-tokens can end a grammar line, in the above order; neither is matched against input.  The dotted forms `.reverse` and `.withI6Synonyms` are ordinary dictionary words and are unaffected.

- `REVERSE` swaps `noun` and `second` when the action receives its parsed arguments:  `{.give, CREATURE, HELD, REVERSE}`.
- `withI6Synonyms` widens the line's effect on a library verb.  A library verb can group several synonym words under one grammar table; when a line's trigger word belongs to such a group, the line affects only that word and its synonyms are untouched.  With `withI6Synonyms`, the line applies to every word in the group.  On a native word, or a library word with no synonyms, the modifier has no effect.

A line on a library word splits that word off its I6 `Verb`; when it was the only word of its `Verb`, Inform 6 warns that the declaration is left with no words; the compiler hides this warning unless `filterVerbSplitWarnings` is `false` (§15.3).  For background (not part of this specification), see [Verbs and Grammar](../Verbs-Grammar.md).

**Example**

```bgl
verb Keypad {
    grammar = { {.enter, NUMBER, .into, NOUN} };    // "enter 5 into keypad"; "cross" unaffected
    void handler() { … }
}

extend Take { grammar += { {.take, .all, .from, NOUN, withI6Synonyms} }; }   // take/get/carry/…
```

### 13.4.5 Grammar on Verbs and Grammar Objects

**Syntax**

```syntax
verb NAME { grammar = { { PATTERN } , … } ; … }

grammar NAME {
    grammarRule RULE = { VERB , { PATTERN } [ , PRIORITY ] } ;
    array<grammarRule> RULES = { { VERB , { PATTERN } [ , PRIORITY ] } , … } ;
}
```

The `grammar` member of a verb is a `grammarRuleList` whose lines use the inferred-verb form; the verb is the owning object.

A `grammar` declaration declares an object of class `grammarRuleList`.  A grammar object is cross-cutting:  one object can carry rules which target many verbs, each pairing a pattern with an explicit verb.  Member types can be inferred, and an `array<grammarRule>` member holds several rules.  The per-rule priority is the optional third element (§13.2.5).

**Example**

```bgl
grammar customPatterns {
    rule1 = {PutOn, {.hang, HELD, .on, NOUN}};
    array<grammarRule> rules = {
        {PutOn,  {.put, HELD, .on, NOUN}},
        {Insert, {.put, HELD, .in, NOUN}, 5},
    };
}
```

**See also** §13.2.5.

## 13.5 Extending Grammar

**Syntax**

```syntax
extend VERB {
    [ priority = N ; ]
    grammar += { { PATTERN } , … } ;
    grammar -= { { .WORD , PATTERN } , … } ;
    grammar -= { { .WORD } , … } ;
    replace grammar = { { PATTERN } , … } ;
    synonyms = { .WORD , … } ;
}
```

The two `grammar -=` forms are the line-level and word-level shapes of §13.5.2; entries of both shapes can be mixed in one list.

Grammar is added to or removed from an existing verb, including an `extern verb`, inside an `extend V { … }` body (§11.10).  An operator is required:  a bare `grammar = { … }` is valid only in the original `verb` or `extern verb` declaration, and is a compile-time error inside an `extend`.  A bare `priority = N;` in the block applies to the lines it appends (§13.2.5).

### 13.5.1 `grammar +=`

`grammar += { … }` appends lines to the verb's grammar.  It accepts multi-trigger lines (§13.4.3) and line modifiers (§13.4.4).

**Example**

```bgl
extern verb PutOn { .put }
extend PutOn {
    grammar += { {.hang, HELD, .on, NOUN} };
}
```

### 13.5.2 `grammar -=`

`grammar -= { … }` removes grammar; how much depends on how much of the line you name:

- **Line-level**:  `{.w, pattern…}` removes the one line which matches exactly:  same trigger word, same tokens, same `REVERSE` flag.
- **Word-level**:  `{.w}` removes all of that word's grammar.

A partial (prefix) pattern matches nothing:  `-= { {.give, NOUN} }` removes only the line whose pattern is exactly `NOUN`, never `{.give, NOUN, .to, NOUN}`.  An alternation in a `-=` entry (`.a|.b|.c`) is one removal per word, each matched and warned separately.  Removal is source-ordered:  a `-=` sees only the lines declared before it, so a later `+=` of the same line is unaffected.  A `-=` which matches nothing is a warning.

**Example**

```bgl
verb TypeNum { grammar = { {.type|.dial, .into, NOUN}, {.dial, .to, NOUN} }; void handler() { … } }
extend TypeNum { grammar -= { {.dial, .into, NOUN} }; }   // that one dial line
extend TypeNum { grammar -= { {.dial} }; }                // every dial line
```

**Extern verbs.**  For a library verb, Beguile knows the words it claims and the lines Beguile itself added with `+=`; however, the library's own patterns are opaque.  A line-level `-=` therefore matches only Beguile-added lines, and naming a claimed word with a pattern which matches none of them is a warning.  A word-level `-=` on a claimed word *evicts* the word from the library verb:  if a native verb also declares that word, its lines take the word over; otherwise the word is disabled.  The word must be genuinely claimed by the library verb.

Eviction discards the library's grammar for the word.  The lines a native verb declares for it become its complete grammar, and lines another extern verb adds for it with `grammar +=` are kept.  For background (not part of this specification), see [Verbs and Grammar](../Verbs-Grammar.md).

```bgl
extend Enter  { grammar -= { {.enter} }; }      // evict 'enter' from the library verb …
verb  Keypad  { grammar = { {.enter, NUMBER, .into, NOUN} }; void handler() { … } }   // … and reclaim it
extend Disturb { grammar -= { {.xyzzy} }; }     // no reclaimer: 'xyzzy' is disabled
```

**See also** §13.2.3, §13.4.3.

### 13.5.3 `replace grammar =`

`replace grammar = { … }` discards all of the verb's grammar (every trigger word and every prior `+=` and `-=`, regardless of their source order), and the listed lines become its complete grammar.  Only this verb is affected; a word which another verb also uses keeps its grammar there.  On an extern verb, the replacement overrides the library's grammar for the named words.  Combining `replace grammar =` with `priority = N;` in the same block is a compile-time error.

**Example**

```bgl
verb TypeNum { grammar = { {.type|.dial, .into, NOUN} }; void handler() { … } }
extend TypeNum {
    replace grammar = { {.dial, .to, NOUN} };     // 'type' is dropped
}

extend Take {
    replace grammar = { {.take, .firmly, NOUN} };  // overrides the library's 'take'
}
```

**See also** §13.2.5.

### 13.5.4 Synonyms

`synonyms = { .word, … }` inside an `extend` makes the listed words true aliases of the extended verb:  they share the verb's grammar rather than copying it, so a later `extend` of the verb will reach every synonym.  By contrast, adding the words as separate trigger lines with `grammar +=` copies the pattern set at that point, and later extensions won't reach them.

The alias is attached to the verb's primary trigger word (i.e., its first claimed word, or its name if it declares none).  `synonyms` works on both extern and native verbs.  The one-letter and plural word conventions apply (§1.6.7).  An empty `synonyms = { }` is a compile-time error.

There is no Beguile form for I6's `Extend 'w' only` directive; if you need it, write it in an `#i6` island (§16.2).

**Example**

```bgl
extend Take { synonyms = {.steal, .grab, .pilfer}; }
extend Take { grammar += { {.take, .quietly, NOUN} }; }   // also "steal quietly", "grab quietly"
```

**See also** §13.2.3, §16.2.
