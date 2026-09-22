# 1 Lexical Structure

<!-- toc -->
- [1.1 Source Text and Encoding](#11-source-text-and-encoding)
- [1.2 Comments](#12-comments)
- [1.3 Case-Insensitivity](#13-case-insensitivity)
- [1.4 Identifiers](#14-identifiers)
- [1.5 Keywords](#15-keywords)
- [1.6 Literals](#16-literals)
  - [1.6.1 Integer Literals](#161-integer-literals)
  - [1.6.2 Float Literals](#162-float-literals)
  - [1.6.3 String Literals](#163-string-literals)
  - [1.6.4 Raw String Literals](#164-raw-string-literals)
  - [1.6.5 Interpolated String Literals](#165-interpolated-string-literals)
  - [1.6.6 Character Literals](#166-character-literals)
  - [1.6.7 Dictionary Word Literals](#167-dictionary-word-literals)
- [1.7 Operators and Punctuation](#17-operators-and-punctuation)
- [1.8 Directive Tokens](#18-directive-tokens)
<!-- /toc -->


## 1.1 Source Text and Encoding

A source file is plain text encoded as UTF-8 or Latin-1. Outside comments, string literals, character
literals and dictionary words, source text is ASCII. Inside those, characters beyond ASCII may be typed
directly, provided each is one Beguile can represent on the target (Appendix D); any other character is
a compile-time error.

## 1.2 Comments

**Syntax**

```syntax
// ⟨text to end of line⟩
/* ⟨text⟩ */
```

**Description**

Beguile has the two C comment forms. Comments are discarded and have no effect on compilation.

**Example**

```bgl
// to the end of the line

/* to the closing
   delimiter */
```

## 1.3 Case-Insensitivity

Beguile is case-insensitive for every token except the contents of string literals. Keywords, type
names, identifiers and operator names are normalized to lowercase, so the following are equivalent:

```bgl
if(X == 1) print("yes");
IF(x == 1) Print("yes");
If(x == 1) PRINT("yes");
```

String literal contents are preserved exactly as written.

## 1.4 Identifiers

**Syntax**

```syntax
⟨letter or underscore⟩ [ ⟨letter, digit or underscore⟩ … ]
```

**Description**

An identifier is a sequence of letters, digits and underscores whose first character is not a digit.
A keyword (§1.5) may not be used as a variable, function, type or object name. A member may share its
name with a type; the rule is in §3.8.3.

**Reserved prefixes.** Identifiers beginning with `_bgl` or `bgl` are reserved for language-generated
symbols and runtime infrastructure (`bglInit`, `bglWorld`, loop counters, scratch temporaries). The
compiler does not reject such names in user code, but if one collides with a generated symbol the
behavior is undefined. Documented hooks and base types under the prefix (`_bglObject`,
`_bglGlobalDeclaration`, §21.5.8, §15.8) are the exception; user code may name them.

**Example**

```bgl
score   myVar   _internal   room1   velvetCloak
```

## 1.5 Keywords

The following words are reserved and may not be used as variable, function, type or object names.
Beguile is case-insensitive, so the reservation applies in any letter case. Appendix A is the single
alphabetical table of these words, with the kind and owning section of each; this section groups the
same words by role.

**Declaration and qualifier keywords.** Consumed entirely by the compiler; none appears in the
generated output.

`additive` `alias` `as` `byVal` `const` `default` `emitter` `explicit` `extend` `extern` `inline`
`operator` `ref` `replace` `static` `superposed` `typesealed`

**Type keywords and built-in type names.** Words that form or name a type. `auto`, `bnum`, `enum`,
`func`, `var` and `void` are recognized by the compiler; the rest are declared by the runtime core but
reserved at file scope for the same reason a user-declared type name is (§3.1).

`array` `attribute` `auto` `bnum` `bool` `char` `class` `dictionaryWord` `enum` `float` `func` `int`
`object` `property` `rawArray` `string` `uint` `var` `verb` `void`

The literal pseudo-types (§2.4) are likewise reserved.

**Control-flow keywords.** Each compiles to an I6 statement of the same or equivalent name.

`break` `case` `catch` `continue` `delete` `do` `else` `for` `if` `in` `return` `rfalse` `rtrue`
`switch` `throw` `to` `try` `until` `while`

**Operator keywords.**

`new` `replaced`

**Value keywords.**

`false` `grammar` `nothing` `null` `self` `true`

`grammar` names a grammar declaration or member (§13.4) rather than a value, but is reserved in the
same way.

**Contextual keywords.** These are recognized only in one syntactic position and are ordinary
identifiers elsewhere: `hide` (§8.7.4), `inject`, `move` and `remove` (§12.11), `outer` (§9.9.3),
`synonyms` (§13.5.4) and `union` (§2.8.2).

**I6-significant words.** Of the words above, `array`, `attribute`, `class`, `false`, `grammar`,
`nothing`, `object`, `property`, `replace`, `self`, `string`, `true` and `verb` also appear verbatim in
the generated I6 as keywords or well-known identifiers.

`meta`, `priority`, `handler` and `perform` are not keywords; they are members of the `verb` class
(§13.2). `typeof` is not a keyword; it is a function of the runtime core (§2.8.1). The words that
Inform 6 reserves and Beguile does not, and the `as` clause that avoids them in generated names, are
covered in §15.9.

## 1.6 Literals

### 1.6.1 Integer Literals

**Syntax**

```syntax
⟨digits⟩
$⟨hex digits⟩
$$⟨binary digits⟩
```

**Description**

An integer literal is decimal, hexadecimal (prefix `$`, digits `0`–`9` `A`–`F` in either case) or
binary (prefix `$$`, digits `0` and `1`). A negative value is formed by prefixing `-`. C-style `0x`
notation is a compile-time error.

An integer literal has the pseudo-type `intLiteral`; a negated one has `negativeIntLiteral` (§2.4).

**Example**

```bgl
42        $FF         $$11111111     // → 42, 255, 255
-1234     $0A         $$11010        // → -1234, 10, 26
```

### 1.6.2 Float Literals

**Syntax**

```syntax
⟨digits⟩.⟨digits⟩
.⟨digits⟩
```

**Description**

A float literal is a decimal number containing a `.` with at least one digit after it. A `.` is part
of the literal only when a digit follows it, so `1.method()` is an integer followed by a member access.
A negative value is formed by prefixing `-`.

**Example**

```bgl
1.0     .3     -1.2
```

**Notes**

> **[Glulx]** Float literals and the `float` type (§2.3) exist only when the target is Glulx.

### 1.6.3 String Literals

**Syntax**

```syntax
"⟨characters⟩"
```

**Description**

A string literal is text enclosed in double quotes. Backslash escapes are recognized; the ones needed
in everyday text are:

| Escape | Character |
|---|---|
| `\n` | newline |
| `\"` | `"` |
| `\\` | `\` |
| `\^` | `^` |
| `\~` | `~` |
| `\@` | `@` |

Beguile preserves Inform 6's conventions for extended characters in strings: an unescaped `^` is a
newline and an unescaped `~` is a double quote, exactly as in I6, and both must be escaped to print
literally. Numeric escapes, diacritical shorthands, directly typed Unicode characters and the folding
of typographic quotes are specified in Appendix D.

A string literal has the pseudo-type `stringLiteral` (§2.4).

**Example**

```bgl
"Hello, world!"
"She said, \"well done.\""
"Line one^Line two"
```

### 1.6.4 Raw String Literals

**Syntax**

```syntax
@"⟨characters⟩"
```

**Description**

A raw string literal disables all escape processing except `\"`. Every other character between the
delimiters is taken literally, including `\`, `^` and `~`; a raw string therefore contains no I6
newline or quote. A raw string may appear anywhere a string literal may, including
`#beguilerSettings` values (§17.1), and has the same pseudo-type, `stringLiteral`.

**Example**

```bgl
string path = @"C:\Users\jim\IF-Games\medusa.bgl";
```

### 1.6.5 Interpolated String Literals

**Syntax**

```syntax
$"⟨characters⟩ { ⟨expression⟩ } ⟨characters⟩ …"
```

**Description**

An interpolated string literal is prefixed with `$` and may contain Beguile expressions inside `{ }`
spans. Escapes are those of a string literal, plus `\{` for a literal `{`. A `{ }` pair nested inside
an expression span is a compile-time error.

An interpolated string has the pseudo-type `interpolatedStringLiteral` (§2.4.2), which may be passed
only to an emitter declaring a parameter of that type; passing it to a non-emitter function is a
compile-time error. `print()` and `log()` accept it in the core runtime (§21.4); assignment to a
`string` requires the `<string>` extension (§22.3).

**Example**

```bgl
print($"The {obj.name} weighs {obj.weight} stone.");
print($"Press \{enter} to continue.");     // → Press {enter} to continue.
```

### 1.6.6 Character Literals

**Syntax**

```syntax
'⟨character⟩'
```

**Description**

A character literal is exactly one character in single quotes. It accepts the same escapes as a string
literal and the same directly typed Unicode characters (Appendix D). A single-quoted literal holding
more than one character is a compile-time error; Beguile does not use I6's `'word'` spelling for
dictionary words (§1.6.7).

`\'` followed by a vowel in the acute-accent set is the acute accent (`'\'e'` is `é`); a `\'` not
followed by such a vowel is an escaped single quote. A character literal has the pseudo-type
`charLiteral` (§2.4).

**Example**

```bgl
'a'   '\n'   '\\'   'ä'   '\:a'
```

### 1.6.7 Dictionary Word Literals

**Syntax**

```syntax
.⟨word⟩
..⟨word⟩
```

**Description**

A dictionary word literal names an entry in the I6 dictionary, the tokens the parser matches player
input against. The `.` form is singular; the `..` form is plural. A `-` between two word characters is
part of the word, not the subtraction operator, and an apostrophe is likewise part of the word.

A dictionary word literal has the pseudo-type `dictionaryWordLiteral` and is compatible with
`dictionaryWord` (§2.4, §21.5.3). The meaning of singular and plural words is defined in §13.1.

**Example**

```bgl
.cloak   ..cloaks   .medium-sized   .monkey's
```

## 1.7 Operators and Punctuation

The following multi-character sequences are single tokens. A longer token always wins: `<=>` is the
three-way comparison, never `<=` followed by `>`.

```text
<<=  >>=  <=>
-=  +=  *=  /=  %=  &=  |=  ^=  :=  ?=  ==  !=  <=  >=  =~
&&  ||  ++  --  <<  >>  =>  ?.  ??
```

The single-character operators and punctuation are:

```text
=  +  -  *  /  %  <  >  !  &  |  ^  ?  :  .  ,  ;  #  (  )  {  }  [  ]
```

`::` immediately followed by an identifier forms one token with it, the global-scope qualifier
`::name` (§3.9). No other construct uses `::`.

Inside a class body, `[]` and `[]=` are the names of the subscript operators in an `operator`
declaration (§9.3); elsewhere `[` and `]` are separate tokens.

The role of each operator and the section that specifies it are indexed in Appendix C.3; the
overloadable operators are listed in §9.1 and precedence is tabulated in §4.3.

## 1.8 Directive Tokens

**Syntax**

```syntax
#⟨identifier⟩
```

**Description**

A `#` immediately followed by an identifier, with no intervening whitespace, is a directive token:
`#include`, `#define`, `#if`, `#i6`. Directives are specified in §14 and indexed in Appendix B.
