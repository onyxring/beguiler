# Appendix A Reserved Words

This is the alphabetical table of Beguile's reserved words, with the section that defines each; §1.5
groups the same words by role and explains why they should not be used as names. Beguile is
case-insensitive, so this applies in any letter case. Words marked *soft* have meaning only in the
position shown and are otherwise ordinary identifiers.

Kinds: **qualifier** (declaration qualifier), **type** (type name or type-forming word),
**control** (statement or control-flow word), **operator** (operator word), **value** (a value or
receiver name), **I6** (I6-significant: the word also appears verbatim in the generated program, so
reusing it as a name can produce Inform 6 that the Inform 6 compiler rejects).

| Word | Kind | Section |
|---|---|---|
| `additive` | qualifier, I6 | §11.7.2 |
| `alias` | qualifier (a second Beguile name for a Beguile declaration) | §8.2.4, §10.2 |
| `array` | type, I6 | §12.2 |
| `asBgl` | qualifier (names an `extern` I6 symbol for Beguile source) | §3.11 |
| `asI6` | qualifier (names the I6 symbol a Beguile declaration emits as) | §3.11 |
| `attribute` | type, I6 | §11.6 |
| `auto` | type | §3.6, §9.9.2 |
| `bnum` | type | §2.7.2 |
| `bool` | type | §2.2 |
| `break` | control | §5.13 |
| `byVal` | qualifier | §8.2.7 |
| `case` | control | §5.12 |
| `catch` | control | §5.16 |
| `char` | type | §2.2 |
| `charLiteral` | type | §2.4 |
| `class` | qualifier, I6 | §8.1 |
| `const` | qualifier | §3.4 |
| `continue` | control | §5.13 |
| `default` | qualifier; control (`switch`) | §5.12, §8.7.3, §15.4.1 |
| `delete` | control | §5.15 |
| `dictionaryWord` | type | §13.1, §21.5.3 |
| `dictionaryWordLiteral` | type | §2.4 |
| `do` | control | §5.11 |
| `else` | control | §5.8 |
| `emitter` | qualifier | §7.2 |
| `enum` | type | §2.7.1 |
| `explicit` | qualifier | §9.4 |
| `extend` | qualifier | §8.7.1, §11.10, §12.11, §13.5 |
| `extern` | qualifier | §15.4 |
| `false` | value, I6 (member of `eBool`) | §2.2 |
| `float` | type | §2.3 |
| `for` | control | §5.9 |
| `func` | type | §2.9 |
| `hide` | qualifier (soft: at the head of a member declaration) | §8.7.4 |
| `if` | control | §5.8 |
| `in` | control (`for … in`) | §5.9.1 |
| `inject` | control (soft: inside `extend` of an array) | §12.11 |
| `inline` | qualifier | §8.3.5, §11.3.1 |
| `int` | type | §2.2 |
| `interpolatedStringLiteral` | type | §2.4 |
| `intLiteral` | type | §2.4 |
| `move` | control (soft: inside `extend` of an array) | §12.11 |
| `negativeIntLiteral` | type | §2.4 |
| `new` | operator | §4.13 |
| `nothing` | value, I6 | §2.5 |
| `null` | value | §2.5 |
| `object` | type, I6 | §11.2 |
| `operator` | qualifier (operator member) | §9 |
| `outer` | value (soft: inside a property accessor body) | §9.9.3 |
| `property` | type, I6 | §11.7.1 |
| `rawArray` | type | §12.8 |
| `ref` | qualifier | §3.7 |
| `remove` | control (soft: inside `extend` of an array) | §12.11 |
| `replace` | qualifier, I6 | §6.5, §8.7.2 |
| `replaced` | operator (call to the replaced routine) | §6.5 |
| `return` | control | §5.14 |
| `rfalse` | control | §5.14 |
| `rtrue` | control | §5.14 |
| `self` | value, I6 | §6.6 |
| `static` | qualifier | §8.3.3 |
| `string` | type, I6 | §2.2 |
| `stringLiteral` | type | §2.4 |
| `superposed` | qualifier | §3.12 |
| `switch` | control | §5.12 |
| `synonyms` | qualifier (soft: inside a verb body or `extend` of a verb) | §13.5.4 |
| `throw` | control | §5.16 |
| `to` | control (`for` / `case` ranges) | §5.9, §5.12 |
| `true` | value, I6 (member of `eBool`) | §2.2 |
| `try` | control | §5.16 |
| `typesealed` | qualifier | §8.2.8 |
| `uint` | type | §2.2, §21.6.1 |
| `union` | type (soft) | §2.8.2 |
| `until` | control | §5.11 |
| `var` | type | §2.6 |
| `verb` | type, I6 | §13.2 |
| `void` | type | §2.2, §6.2 |
| `while` | control | §5.10 |

The following identifiers are not reserved, although they carry meaning: `grammar`, `meta` and
`priority` are members of the `verb` class, and `handler` and `perform` are its methods (§13.2);
`typeof` is a function of the runtime core (§2.8.1); `create` and `destroy` are the lifecycle methods of a pooled class (§8.2.6)
and `init` and `deinit` the lifecycle emitters of any class (§8.5); `first`, `last`, `after` and
`before` are the positions of an array `inject` (§12.11); `reverse` and `withI6Synonyms` are the
pseudo-tokens that may end a grammar line (§13.4.4). The keyword `for` is reused, outside a loop, in
`alias class Foo for Bar` (§8.2.4).

Directive names (`#include`, `#if`, …) are listed in Appendix B. Inform 6's own reserved words,
which constrain any Beguile name that reaches the generated program, are listed in §15.9.
