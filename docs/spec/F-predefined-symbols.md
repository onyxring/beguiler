# Appendix F Pre-defined Symbols

The compiler defines the following symbols before any source file is processed. They are tested and
compared in `#if` expressions exactly like symbols defined with `#define` (§14.2). Symbol names are
case-insensitive, like all Beguile identifiers.

| Symbol | Example value | Meaning |
|--------|---------------|---------|
| `beguiler` | `1023` | Compiler version encoded as `major*1000 + minor*10 + patch`: 1.2.3 is `1023`, 1.1.0 is `1010`. The encoding fits a 16-bit signed Z-machine word for major versions up to 32. |
| `beguilerMajor` | `1` | Major version component. |
| `beguilerMinor` | `2` | Minor version component. |
| `beguilerPatch` | `3` | Patch version component. |
| `TARGET_GLULX` | (defined, no value) | Defined when the `target` setting is `Glulx`. Having no value, it can be tested with `#if` but not used in an expression. |
| `TARGET_ZCODE` | `5` or `8` | Defined when the target is the Z-machine; the value is the Z-machine version, so it can be compared. |

The version symbols are read-only and derived from the compiler's own version. The target symbols are
set from the `target` setting (§17.3) before any source is read, so they are available to every `#if`
in the program. Exactly one of `TARGET_GLULX` and `TARGET_ZCODE` is defined.

**Resolution rule.** Every symbol, pre-defined or `#define`d, that carries a value is resolved as an
inline compile-time literal wherever it appears in a Beguile expression: the name is replaced by its
value. A symbol never becomes an I6 `Constant` unless the program assigns it to a `const` variable.

```bgl
if(beguiler >= 1010) { … }          // resolved at compile time to if(1010 >= 1010)
const int myVer = beguilerMajor;    // this const is the program's own declaration
```

**Example**

```bgl
#if beguiler >= 1010
    // requires Beguile 1.1.0 or later
#endif

#if TARGET_ZCODE <= 5
    // Z5 only (excludes Z8)
#endif

#if TARGET_GLULX
    // Glulx-specific code
#endif
```

**See also** §14.2.4, §14.2.5, §17.3, Appendix E.
