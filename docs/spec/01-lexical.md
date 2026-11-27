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
- [1.7 Operators](#17-operators)
- [1.8 Directives](#18-directives)
<!-- /toc -->

## 1.1 Source Text and Encoding

A source file is plain text, encoded as UTF-8 or Latin-1.  A byte-order mark at the start of a UTF-8 file is ignored.

Outside of comments, string literals, character literals, and dictionary words, source text is ASCII.  Inside those, you can directly type any character which Beguile can represent on the target (Appendix D); any other is a compile-time error.

## 1.2 Comments

**Syntax**

```syntax
// TEXT-TO-END-OF-LINE
/* TEXT */
/// DOCUMENTATION-TO-END-OF-LINE
/** DOCUMENTATION */
```

Beguile has two comment forms, the line comment `//` and the block comment `/* */`.  Comments are simply thrown away.

A comment which opens with a third character, `///` or `/**`, is a *doc comment*.  It has no effect on compilation either; however, the language server (§17.6) will show its text, formatted as Markdown, wherever you hover over the declared name.  In a `/** */` comment, a leading `*` on each line is not part of the text.

A doc comment documents a declaration from one of two positions:

- **Before it.**  Doc comments on the lines directly above a declaration document it, and consecutive doc comments join into one text.  A blank line between a doc comment and the next declaration detaches the comment.
- **After it, on the same line.**  A doc comment which follows code on its line documents the declaration that starts on that line, never the next one.  If a declaration is documented both ways, the text above it is shown first.

**Example**

```bgl
// to the end of the line

/* to the closing
   delimiter */

/// The room the player is in.
extern object location;
extern int    score;      /// Points earned so far.

/** Prints the banner.
 * Called once, at startup. */
void showBanner(){ print("Welcome."); }
```

## 1.3 Case-Insensitivity

Beguile is case-insensitive for every token except the contents of string literals, and normalizes the rest to lowercase; the following lines are entirely equivalent:

<!-- doctest: skip -->
```bgl
if(X == 1) print("yes");
IF(x == 1) Print("yes");
If(x == 1) PRINT("yes");
```

## 1.4 Identifiers

**Syntax**

```syntax
LETTER-OR-UNDERSCORE [ LETTER,-DIGIT-OR-UNDERSCORE … ]
```

An identifier is a sequence of letters, digits, and underscores which doesn't start with a digit.  You should not use a reserved word (§1.5) as a name.  A member can share its name with a type (§3.8.3).

**Example**

<!-- doctest: skip -->
```bgl
score   myVar   _internal   room1   velvetCloak
```

> ***Note:** Names beginning with `_bgl` or `bgl` are earmarked for the compiler and the runtime.  The compiler won't reject such a name in your code; however, one which matches one of the system's can conflict with it, and the behavior is then undefined.  Where the runtime exposes a prefixed name for you to use, it is documented with the feature (§16.8, §21.5.8).*

## 1.5 Reserved Words

The words below have meaning in Beguile and should not be used as names.  The compiler won't reject every use of one as a name; such a use is unsupported regardless.  Declaring a type with the name of a built-in type is a compile-time error, and a global named after a word shared with Inform 6 is rejected by the Inform 6 stage.  All of this applies in any letter case.  Appendix A lists the words alphabetically.

**Declaration words** begin or qualify a declaration.

`alias` `asBgl` `asI6` `class` `const` `default` `distributor` `emitter` `explicit` `extend` `extern`
`inline` `literal` `operator` `primitive` `ref` `replace` `required` `static` `struct` `superposed`
`typesealed`

`primitive` is a keyword only directly before `class`, `struct` only directly before a name, `required` only directly before a member's type, `literal` (§3.13) only directly before a type, and `distributor` (§6.8) only directly before a name and its `;`; elsewhere they are ordinary names (`int struct;`).

**Type-forming words** build a type rather than name one.

`auto` `bnum` `enum` `func` `selfType` `var` `void`

**Built-in type names** are the types every program has, with no `#include`.

`array` `attribute` `bool` `char` `dictionaryWord` `float` `int` `interpolatedString` `object`
`property` `rawArray` `string` `uint` `verb`

**Literal pseudo-types** are the types of literal values.

`charLiteral` `dictionaryWordLiteral` `intLiteral` `interpolatedStringLiteral` `negativeIntLiteral`
`nullLiteral` `stringLiteral`

**Statement words** begin or structure a statement.

`break` `case` `catch` `continue` `delete` `do` `else` `for` `if` `in` `return` `rfalse` `rtrue`
`switch` `throw` `to` `try` `until` `while`

**Expression words** introduce an expression.

`new` `replaced`

**Value words** name a fixed value or the current receiver.  None of them can be declared as a name.

`false` `null` `self` `true`

**Contextual words** have meaning in one position only and are ordinary identifiers elsewhere.

`hide` `inject` `move` `outer` `remove` `synonyms` `union`

**Words shared with Inform 6** also appear verbatim in the generated Inform 6, so a program which uses one as a name can produce output the Inform 6 compiler will reject.

`additive` `array` `attribute` `class` `false` `nothing` `object` `property` `replace` `self` `string`
`true` `verb`

Inform 6 reserves still more words which Beguile does not (§16.9).

## 1.6 Literals

### 1.6.1 Integer Literals

**Syntax**

```syntax
DIGITS
$HEX-DIGITS
$$BINARY-DIGITS
```

An integer literal is decimal, hexadecimal (prefix `$`, digits `0` to `9` and `A` to `F` in either case), or binary (prefix `$$`, digits `0` and `1`).  For a negative value, simply prefix a `-`.

Its pseudo-type is `intLiteral`, or `negativeIntLiteral` when negated (§2.4).

**Example**

<!-- doctest: skip -->
```bgl
42        $FF         $$11111111     // -> 42, 255, 255
-1234     $0A         $$11010        // -> -1234, 10, 26
```

> ***Note:** Beguile writes hexadecimal with `$` (`$FF`), as Inform 6 does; a `0x` literal (`0xFF`) is a compile-time error.*

### 1.6.2 Float Literals

**Syntax**

```syntax
DIGITS.DIGITS
.DIGITS
```

A float literal is a decimal number containing a `.` with at least one digit after it.  Again, prefix a `-` for a negative value.  `1.` is *not* a float literal and is a compile-time error; write `1.0`.

**Example**

<!-- doctest: skip -->
```bgl
1.0     .3     -1.2
```

> ***Note:** A `.` followed by anything other than a digit is a member access, so `42.someMethod()` calls a method on the integer `42` (§2.4) rather than starting a float.*

> ***[Glulx]:** Float literals and the `float` type (§2.3) exist only when the target is Glulx.*

### 1.6.3 String Literals

**Syntax**

```syntax
"CHARACTERS"
```

A string literal is text enclosed in double quotes.  It recognizes quite a few backslash escapes; here are the ones you'll need in everyday text:

| Escape | Character |
|---|---|
| `\n` | newline |
| `\"` | `"` |
| `\\` | `\` |
| `\^` | `^` |
| `\~` | `~` |
| `\@` | `@` |
| `\{` `\}` | `{` `}` |

A backslash before any other character is a compile-time error, unless Appendix D lists the escape.

Beguile keeps two of Inform 6's string conventions:  an unescaped `^` is a newline and an unescaped `~` is a double quote.  Escape them to print them literally.  Every other character prints as itself, `@` included (accented letters are written with the `\` escapes of Appendix D, not I6's `@` codes).  Appendix D covers the remaining escapes and characters.

A string literal has the pseudo-type `stringLiteral` (§2.4).

**Example**

<!-- doctest: skip -->
```bgl
"Hello, world!"
"She said, \"well done.\""
"Line one^Line two"
```

### 1.6.4 Raw String Literals

**Syntax**

```syntax
@"CHARACTERS"
```

A raw string literal turns off all escape processing except `\"`.  Every other character is taken literally, including `\`, `^`, and `~`.  It can appear anywhere a string literal can, including `#beguilerSettings` values (§15.1), and has the same pseudo-type, `stringLiteral`.

**Example**

```bgl
string path = @"C:\Users\jim\IF-Games\medusa.bgl";
```

### 1.6.5 Interpolated String Literals

**Syntax**

```syntax
$"CHARACTERS { EXPRESSION } CHARACTERS …"
```

An interpolated string literal is prefixed with `$` and can contain Beguile expressions inside `{ }` spans.  Its escapes are those of a string literal; `\{` is a literal `{` rather than the start of a span.

A span holds any expression, including one with braces of its own (e.g., a lambda with a block body).  A string literal inside a span is written with plain quotes.  Escapes belong to the characters of a string, so `\"` inside a span is a compile-time error.

What happens to an interpolated string depends on where you use it:  an emitter which declares an `interpolatedStringLiteral` parameter expands it in place (§2.4.2), a `stringObj` captures its output (§22.3), and a `func<void>`, a `var`, or an `interpolatedString` takes it as a routine which prints it (§2.9.1).

**Example**

```bgl
object lamp { string title = "brass lamp"; int weight = 2; }

// … in a routine:
print($"The {lamp.title} weighs {lamp.weight} stone.");   // -> The brass lamp weighs 2 stone.
print($"Press \{enter} to continue.");                    // -> Press {enter} to continue.
print($"Lit: {lamp.weight > 1 ? "yes" : "no"}.");         // -> Lit: yes.
```

### 1.6.6 Character Literals

**Syntax**

```syntax
'CHARACTER'
```

A character literal represents exactly one character and is written in single quotes.  It accepts the same escapes as a string literal and the same directly typed Unicode characters (Appendix D), so the literal itself can be several characters long; however, one which represents more than one character is a compile-time error.

`\'` followed by a vowel in the acute-accent set is the acute accent (`'\'e'` is `é`); a `\'` which isn't followed by such a vowel is an escaped single quote.  A character literal has the pseudo-type `charLiteral` (§2.4).

**Example**

<!-- doctest: skip -->
```bgl
'a'   '\n'   '\\'   'ä'   '\:a'
```

> ***Note:** Inform 6 writes a dictionary word in single quotes (`'sword'`).  Beguile does **not**:  single quotes are the character literal, and a dictionary word is written with a leading `.` (§1.6.7).*

### 1.6.7 Dictionary Word Literals

**Syntax**

```syntax
.WORD
..WORD
```

A dictionary word literal names an entry in the I6 dictionary (i.e., the words which the parser matches the player's input against).  The `.` form is singular; the `..` form is plural.  The apostrophe is an acceptable character.  A `-` between two characters is part of the word, not the subtraction operator.

Its pseudo-type is `dictionaryWordLiteral`, which is compatible with `dictionaryWord` (§2.4, §21.5.3).  §13.1 covers what singular and plural words mean.

**Example**

<!-- doctest: skip -->
```bgl
.cloak   ..cloaks   .medium-sized   .monkey's
```

A word the dot form can't spell (e.g., one starting with a digit) is written as a string literal cast to `dictionaryWord`.  The cast is made at compile time and applies only to a string literal holding one word.

```bgl
object key101 { name = {.key, (dictionaryWord)"101"}; }
```

> ***Note:** The above cast is the only way to write that word:  `.101` is the `float` literal 0.101, **not** a dictionary word.*

## 1.7 Operators

The following multi-character sequences are parsed as single operators.  A longer token always wins:  `<=>` is the three-way comparison, never `<=` followed by `>`.

```text
<<=  >>=  <=>  :==
-=  +=  *=  /=  %=  &=  |=  ^=  :=  ?=  ==  !=  <=  >=  =~
&&  ||  ++  --  <<  >>  =>  ?.  ??
```

The single-character operators are:

```text
=  +  -  *  /  %  <  >  !  &  |  ^  ?  :  .  ,  ;  #  (  )  {  }  [  ]
```

`::` is the global-scope qualifier (§3.9) and, after a type name, introduces an operator reference (`string::operator <=>`, §4.15).  When immediately followed by an identifier, it forms a single token with it:  `::name`.

Appendix C.3 indexes the role of each operator and the section which covers it; §9.1 lists the overloadable operators, and §4.3 tabulates precedence.

## 1.8 Directives

**Syntax**

```syntax
#IDENTIFIER
```

A `#` immediately followed by an identifier, with no whitespace between, is a directive token:  `#include`, `#define`, `#if`, `#i6`.  §14 covers directives, and Appendix B indexes them.
