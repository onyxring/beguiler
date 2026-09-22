# Appendix C Operators

<!-- toc -->
- [C.1 Precedence](#c1-precedence)
- [C.2 Overloadable Operators](#c2-overloadable-operators)
- [C.3 Operator-to-Section Index](#c3-operator-to-section-index)
<!-- /toc -->

## C.1 Precedence

The table is the one in §4.3; higher precedence binds more tightly.

| Prec | Operators | Kind | Assoc | Meaning |
|:---:|---|---|---|---|
| 14 | `.` `?.` `[]` `()` `v?` `++` `--` | postfix | left | Member access, optional access, subscript, call, postfix query, postfix increment/decrement |
| 13 | `!` `-` `&` `(Type)` `++` `--` | prefix | right | Logical not, negation, address-of, cast, prefix increment/decrement |
| 11 | `*` `/` `%` | infix | left | Multiplicative |
| 10 | `+` `-` | infix | left | Additive |
| 9 | `<<` `>>` `<=>` | infix | left | Shift, three-way comparison |
| 8 | `<` `<=` `>` `>=` | infix | left | Relational |
| 7 | `==` `!=` `?=` `=~` | infix | left | Equality |
| 6 | `&` | infix | left | Bitwise and |
| 5 | `^` | infix | left | Bitwise exclusive or |
| 4 | `\|` | infix | left | Bitwise or |
| 3 | `&&` | infix | left | Logical and |
| 2 | `\|\|` | infix | left | Logical or |
| 1 | `? :` | ternary | — | Conditional (one per statement, §4.9) |
| 1 | `??` | infix | — | Null coalescing (§4.10) |
| 0 | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` `:=` | infix | right | Assignment (§5.5), compound assignment (§5.6), reference binding (§3.7) |


## C.2 Overloadable Operators

The complete set of operators a class may overload, with the declaration shape of each, is the table
in §9.1; the operators that must be declared as emitters are listed in §9.8. `?.`, `??`, `=>` and
`:=` are operator tokens that are not overloadable.

## C.3 Operator-to-Section Index

| Operator(s) | Role | See |
|---|---|---|
| `+` `-` `*` `/` `%` | Arithmetic | §4.5 |
| `+=` `-=` `*=` `/=` `%=` | Compound arithmetic assignment | §5.6, §9.7 |
| `==` `!=` `<` `>` `<=` `>=` | Comparison (result `eBool`) | §4.5 |
| `?=` | Type-defined comparison; no built-in meaning | §4.5, §9.5 |
| `<=>` | Three-way comparison (result `int`: negative / 0 / positive) | §4.5, §9.6 |
| `=~` | Content / case-insensitive equality (core `char`; `<string>`) | §4.5, §21.7, §22.3 |
| `&&` `\|\|` `!` | Logical (result `eBool`) | §4.5, §9.5 |
| `&` `\|` `^` `<<` `>>` | Bitwise and shift | §4.5, §9.1 |
| `&=` `\|=` `^=` `<<=` `>>=` | Compound bitwise assignment | §5.6 |
| `++` `--` | Increment / decrement | §5.7, §9.7 |
| `=` | Assignment | §5.5, §2.11 |
| `:=` | Reference binding (rebinding) | §3.7 |
| `? :` | Ternary conditional | §4.9 |
| `?` (postfix) | Query / null test (result `eBool`) | §4.10, §9.5 |
| `?.` | Optional chaining | §4.10 |
| `??` | Null coalescing | §4.10 |
| `(Type)x` | Type cast and conversion | §4.11, §9.4 |
| `&x` (prefix) | Address-of: raw machine address as `int` (`(int)x`) | §4.12 |
| `=>` | Lambda literal | §4.14 |
| `[]` `[]=` | Subscript read / write | §9.3, §12.3 |
| `::name` | Global-scope qualifier | §3.9 |
| `Type::operator op` | Operator reference | §4.15 |
| `$opref(op)` | Operator reference inside an emitter body | §7.3.1 |
