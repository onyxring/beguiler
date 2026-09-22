# Appendix D String Escapes and Character Tables

<!-- toc -->
- [D.1 Basic Escapes](#d1-basic-escapes)
- [D.2 Numeric Escapes](#d2-numeric-escapes)
- [D.3 Diacritical Shorthands](#d3-diacritical-shorthands)
- [D.4 Context-Sensitive `\^` and `\~`](#d4-context-sensitive-and-)
- [D.5 Directly Typed Characters](#d5-directly-typed-characters)
- [D.6 Typographic Quotes and the Backtick](#d6-typographic-quotes-and-the-backtick)
- [D.7 Character Literals](#d7-character-literals)
<!-- /toc -->


This appendix completes §1.6.3 and §1.6.6. Every escape below is
valid in a string literal, an interpolated string literal and a character literal; none is processed
in a raw string literal, which recognizes only `\"`.

## D.1 Basic Escapes

| Escape | Character |
|---|---|
| `\n` | newline |
| `\"` | `"` |
| `\\` | `\` |
| `\^` | `^` (see §D.4) |
| `\~` | `~` (see §D.4) |
| `\@` | `@` |
| `\{` | `{` (interpolated strings only, §1.6.5) |

An unescaped `^` in a string is a newline and an unescaped `~` is a double quote, as in I6.

```bgl
"She said, \"well done.\""
"Price: 5\~ off!"
"Press \^ to continue."
```

## D.2 Numeric Escapes

| Escape | Meaning |
|---|---|
| `\`*NNN* | the character with decimal code *NNN* |
| `\$`*XX* | the character with hexadecimal code *XX* |

Both forms consume every consecutive digit (or hexadecimal digit) after the prefix. The code is a
Unicode code point and is rendered on both targets.

```bgl
"na\239ve"     // → naïve
"na\$EFve"     // → naïve
```

## D.3 Diacritical Shorthands

An accent escape is `\` followed by an accent mark and the letter it applies to:

| Escape | Letters | Example | Result |
|---|---|---|---|
| `\'`*X* | a e i o u y A E I O U Y | `"caf\'e"` | café |
| `` \` ``*X* | a e i o u y A E I O U Y | `` "cr\`eme" `` | crème |
| `\^`*X* | a e i o u y A E I O U Y | `"g\^ateau"` | gâteau |
| `\:`*X* | a e i o u y A E I O U Y | `"na\:ive"` | naïve |
| `\~`*X* | a n o A N O | `"se\~nor"` | señor |

A named escape is `\` followed by a two-character name:

| Escape | Result | Escape | Result |
|---|---|---|---|
| `\/o` `\/O` | ø Ø | `\th` `\TH` | þ Þ |
| `\cc` `\cC` | ç Ç | `\et` `\ET` | ð Ð |
| `\oa` `\oA` | å Å | `\ae` `\AE` | æ Æ |
| `\oe` `\OE` | œ Œ | `\ss` | ß |
| `\LL` | £ | `\!!` | ¡ |
| `\??` | ¿ | `\<<` `\>>` | « » |

## D.4 Context-Sensitive `\^` and `\~`

`\^` and `\~` are both a basic escape (§D.1) and an accent mark (§D.3). When the escape is followed
by a letter in its accent set it produces the accented letter; otherwise it produces a literal `^` or
`~`. To force the literal character before a letter that would otherwise take the accent, double it:
`\^^` or `\~~`.

```bgl
"\^a"      // → â
"\^^a"     // → ^a
"\~n"      // → ñ
"\~~n"     // → ~n
"\^z"      // → ^z   (z is not in the accent set)
```

## D.5 Directly Typed Characters

Accented characters may be typed directly into a string literal, an interpolated string literal, a
character literal or a dictionary word. The source file may be UTF-8 or Latin-1. Each character must
be one of the ZSCII extended characters (codes 155 to 224):

| Group | Characters |
|---|---|
| diaeresis | ä ö ü Ä Ö Ü ë ï ÿ Ë Ï |
| acute | á é í ó ú ý Á É Í Ó Ú Ý |
| grave | à è ì ò ù À È Ì Ò Ù |
| circumflex | â ê î ô û Â Ê Î Ô Û |
| ring and slash | å Å ø Ø |
| tilde | ã ñ õ Ã Ñ Õ |
| ligatures and letters | æ Æ ç Ç þ Þ ð Ð œ Œ ß |
| punctuation | £ ¡ ¿ |

Any other non-ASCII character is a compile-time error, except the typographic quotes of §D.6.

```bgl
"café"
'ñ'
.café
```

## D.6 Typographic Quotes and the Backtick

The curly double quotes `“` `”`, the curly single quotes `‘` `’` and the backtick `` ` `` are accepted
in string literals and rendered according to the target.

> **[Glulx]** Each curly quote is rendered as its own glyph; the backtick is rendered as a backtick.

> **[Z-machine]** The Z-machine has no typographic glyphs: curly double quotes fold to a straight
> double quote, and curly single quotes and the backtick fold to a straight apostrophe `'`.

The target is the resolved build target (§15); Glulx is the default. Only directly typed characters
are folded: a numeric escape such as `\$201C` always denotes that code point.

## D.7 Character Literals

A character literal (§1.6.6) holds exactly one character, written directly or with any escape above. Its
value is the character's ZSCII code, so a character literal may be compared numerically: `c >= 'ä'`
compares against the code of ä.

`\'` followed by a letter in the acute-accent set (§D.3) is the acute accent: `'\'e'` is é. A `\'`
not followed by such a letter is an escaped single quote.
