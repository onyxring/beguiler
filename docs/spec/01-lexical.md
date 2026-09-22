# 1 Lexical Structure

<!-- toc -->
- [1.1 Source Text and Encoding](#11-source-text-and-encoding)
- [1.2 Comments](#12-comments)
- [1.3 Case-Insensitivity](#13-case-insensitivity)
- [1.4 Identifiers](#14-identifiers)
- [1.5 Reserved Words](#15-reserved-words)
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

Beguile has two comment forms. Comments are discarded and have no effect on compilation.

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

Casing within string literals is preserved as written.

## 1.4 Identifiers

**Syntax**

```syntax
⟨letter or underscore⟩ [ ⟨letter, digit or underscore⟩ … ]
```

**Description**

An identifier is a sequence of letters, digits and underscores whose first character is not a digit.
A reserved word (§1.5) should not be used as a name. A member may share its name with a type
(§3.8.3).

**Example**

```bgl
score   myVar   _internal   room1   velvetCloak
```

> **Reserved prefixes.** Names beginning with `_bgl` or `bgl` are earmarked for the language and its
> runtime: the compiler generates symbols with these prefixes (loop counters, scratch temporaries,
> `bglInit`), and the runtime library declares its own (`bgl`, `_bglObject`,
> `_bglGlobalDeclaration`). The compiler does not reject such a name in user code, but a name of your
> own that coincides with one of the system's can conflict with it, and the behavior is then
> undefined. Where the runtime exposes a prefixed name for authors to use, it is documented with the
> feature (§15.8, §21.5.8).

## 1.5 Reserved Words

The words below have meaning in Beguile and should not be used as names. The compiler recognizes most
of them only in the position where they carry that meaning, so it does not reject every use of one as a
name; the use is nevertheless unsupported. Declaring a type with the name of a built-in type is a
compile-time error, and a global named after a word shared with Inform 6 is rejected by the Inform 6
stage. Beguile is case-insensitive, so all of this applies in any letter case. Appendix A lists the same
words alphabetically, with the section that defines each.

**Declaration words** begin or qualify a declaration.

`alias` `as` `byVal` `class` `const` `default` `emitter` `explicit` `extend` `extern` `inline`
`operator` `ref` `replace` `static` `superposed` `typesealed`

**Type-forming words** build a type rather than name one.

`auto` `bnum` `enum` `func` `var` `void`

**Built-in type names** are the types the runtime core declares.

`array` `attribute` `bool` `char` `dictionaryWord` `float` `int` `object` `property` `rawArray`
`string` `uint` `verb`

**Literal pseudo-types** are the types of literal values.

`charLiteral` `dictionaryWordLiteral` `intLiteral` `interpolatedStringLiteral` `negativeIntLiteral`
`stringLiteral`

**Statement words** begin or structure a statement.

`break` `case` `catch` `continue` `delete` `do` `else` `for` `if` `in` `return` `rfalse` `rtrue`
`switch` `throw` `to` `try` `until` `while`

**Expression words** introduce an expression.

`new` `replaced`

**Value words** name a fixed value or the current receiver.

`false` `nothing` `null` `self` `true`

**Contextual words** have meaning in one position only and are ordinary identifiers elsewhere.

`hide` `inject` `move` `outer` `remove` `synonyms` `union`

**Words shared with Inform 6** also appear verbatim in the generated Inform 6, as keywords or
well-known identifiers, so a program that uses one as a name can produce Inform 6 that the Inform 6
compiler rejects.

`additive` `array` `attribute` `class` `false` `nothing` `object` `property` `replace` `self` `string`
`true` `verb`

Inform 6 reserves further words that Beguile does not; §15.9 describes them and the `as` clause that
keeps them out of generated names.

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
binary (prefix `$$`, digits `0` and `1`). A negative value is formed by prefixing `-`. `0x`
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

A float literal is a decimal number containing a `.` with at least one digit after it. A negative
value is formed by prefixing `-`. `1.` is not a float literal and is a compile-time error; write `1.0`.

**Example**

```bgl
1.0     .3     -1.2
```

**Notes**

> **Member access on a literal.** A `.` followed by anything other than a digit is a member access, so
> `42.someMethod()` calls a method on the integer `42` (§2.4) rather than beginning a float.

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
compile-time error. `print()` and `log()` accept it in the core runtime (§21.4).

**Example**

```bgl
object lamp { string title = "brass lamp"; int weight = 2; }

print($"The {lamp.title} weighs {lamp.weight} stone.");   // → The brass lamp weighs 2 stone.
print($"Press \{enter} to continue.");                    // → Press {enter} to continue.
```

### 1.6.6 Character Literals

**Syntax**

```syntax
'⟨character⟩'
```

**Description**

A character literal represents exactly one character and is written in single quotes. It accepts the
same escapes as a string literal and the same directly typed Unicode characters (Appendix D), so the
literal itself may be several characters long. A literal representing more than one character is a
compile-time error.

`\'` followed by a vowel in the acute-accent set is the acute accent (`'\'e'` is `é`); a `\'` not
followed by such a vowel is an escaped single quote. A character literal has the pseudo-type
`charLiteral` (§2.4).

**Example**

```bgl
'a'   '\n'   '\\'   'ä'   '\:a'
```

> **Not an I6 dictionary word.** Inform 6 writes a dictionary word in single quotes (`'sword'`).
> Beguile does not: single quotes are the character literal, and a dictionary word is written with a
> leading `.` (§1.6.7).

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

Where a subscript operator is declared on a class or object, or referred to by name, `[]` and `[]=`
are single tokens naming it (§9.3, §4.15). Everywhere else — an array declaration, a subscript
expression — `[` and `]` are separate tokens.

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
