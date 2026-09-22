# Appendix G Emitter Substitution Tokens

## G.1 Token Reference

Every `$` token is replaced when an emitter body is substituted at a use site; every `##` form is
processed at the same time. Anything else in an emitter body, including single-hash `#` directives
and unrecognized `##name` text, passes through to the output unchanged. Full rules: §7.3 and §7.4.

| Token | Meaning | See |
|---|---|---|
| `$self` | In an operator or assignment emitter, the receiver with its trailing `.member` removed when the receiver is a member access (`obj` for `obj.score + 1`); otherwise, and in a method emitter, the receiver itself (`container.children` for `container.children.length()`). Not meaningful in a global emitter. | §7.3 |
| `$val` | The full receiver expression as written: `obj.score` for `obj.score + 1`; otherwise the same as `$self`. | §7.3 |
| `$host` | The object a proxy member is accessed on, the owner of the proxy: the receiver with its trailing `.member` removed, in every kind of emitter (`container` for `container.children.length()`); equals `$self` when the receiver is not a member access. | §7.3, §7.3.2 |
| `$name` | The argument supplied for the parameter declared as `name`. | §7.3 |
| `$target` | The assignment target as a full lvalue path, or a compiler-supplied temporary in statement position. Its presence makes the body responsible for the store. | §7.3.2 |
| `$prop` | In an `array<T>` emitter, the property name of an object-member array; `0` for a global array. | §7.3, §10.7 |
| `$opref(op[, T])` | A callable reference to the receiver type's `operator op` (routine name for `static`, property name for an instance operator, `0` for an emitter or none); `T` selects an overload. | §7.3.1 |
| `$oprefReq(op[, T])` | As `$opref`, with a compile-time warning when nothing is found. | §7.3.1 |
| `$selfsub` | In a `_bglGlobalDeclaration` body, the instance name with `sub` appended. | §13.8 |
| `##if expr` / `##else` / `##endif` | Conditional inclusion of body text, with the `#if` expression syntax. `##ifdef`/`##ifndef` are errors. | §7.4 |
| `##beguilerSettings.key` | The compile-time value of a `#beguilerSettings` property, in the raw-I6 bodies of `#emitfirst`, `#emitlast`, `#storedEmitFirst` and `#storedEmitLast` only. | §12.4.5, §15.7 |
| `##Name` (other) | Not a token: passes through verbatim as I6 text (an I6 action constant such as `##Take`; `##$v` yields the action name of `$v`). | §7.4 |
