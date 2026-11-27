# Appendix F Pre-defined Symbols

The compiler defines the following symbols before any source file is processed.  You can test and compare them in `#if` expressions exactly like symbols defined with `#define` (§14.2).  Symbol names are case-insensitive, like all Beguile identifiers.

| Symbol | Example value | Meaning |
|--------|---------------|---------|
| `beguiler` | `1023` | Compiler version encoded as `major*1000 + minor*10 + patch`:  1.2.3 is `1023`, 1.1.0 is `1010`.  The encoding fits a 16-bit signed Z-machine word for major versions up to 32. |
| `beguilerMajor` | `1` | Major version component. |
| `beguilerMinor` | `2` | Minor version component. |
| `beguilerPatch` | `3` | Patch version component. |
| `TARGET_GLULX` | (defined, no value) | Defined when the `target` setting is `Glulx`. |
| `TARGET_ZCODE` | (defined, no value) | Defined when the `target` setting is a Z-machine version. |
| `DEBUG` | (defined, no value) | Defined in a debug build (`--debug`, §20.3).  A program can also turn it on itself with `#redef DEBUG` (§14.2.2). |
| `bglAutoInitialize` | `1` | Defined when the `autoInitialize` setting is `true` (§15.4); the bindings test it before running `bglInit()` for you (§23.5). |
| `generateBlorb` | `true` | The `generateBlorb` setting (§15.6).  Always defined; test it by value, `#if (generateBlorb == true)`. |

The version symbols are read-only and derived from the compiler's own version.  The target symbols are set from the `target` setting (§15.3).  Exactly one of `TARGET_GLULX` and `TARGET_ZCODE` is defined.

**The target symbols carry no value.**  Neither can be used in a Beguile expression; `#if TARGET_ZCODE` tests definedness like any other bare symbol (§14.2.5).  To tell Z5 from Z8, test the `target` *setting*, which `#if` reads directly:

<!-- doctest: skip -->
```bgl
#if #beguilerSettings.target == "z8"
```

**Resolution rule.**  Every symbol which carries a value, pre-defined or `#define`d, is resolved as an inline compile-time literal wherever it appears in a Beguile expression:  the name is replaced by its value.  The exception is a symbol defined with `#defineI6`, which is an I6 `Constant` and is emitted by name (§14.2.1).  No other symbol becomes an I6 `Constant`, although the program can assign one to a `const` variable of its own.

```bgl
if(beguiler >= 1010) { … }          // resolved at compile time to if(1010 >= 1010)
const int myVer = beguilerMajor;    // this const is the program's own declaration
```

**Example**

```bgl
#if beguiler >= 1010
    // requires Beguile 1.1.0 or later
#endif

#if #beguilerSettings.target == "z5"
    // Z5 only (excludes Z8): the version lives in the setting, not the symbol
#endif

#if TARGET_GLULX
    // Glulx-specific code
#endif
```

**See also** §14.2.4, §14.2.5, §15.3, Appendix E.
