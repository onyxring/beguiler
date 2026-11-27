# 15 Settings (`#beguilerSettings`)

<!-- toc -->
- [15.1 The Settings Block](#151-the-settings-block)
- [15.2 Toolchain Paths](#152-toolchain-paths)
- [15.3 Compilation Settings](#153-compilation-settings)
- [15.4 Runtime Settings](#154-runtime-settings)
- [15.5 Game Metadata](#155-game-metadata)
- [15.6 Blorb Packaging](#156-blorb-packaging)
  - [15.6.1 Asset Discovery and `_blorbAssets.bgl`](#1561-asset-discovery-and-blorbassetsbgl)
  - [15.6.2 IFID Generation and Persistence](#1562-ifid-generation-and-persistence)
- [15.7 Reading Settings From Source](#157-reading-settings-from-source)
- [15.8 Bindings and the Library Banner Constants](#158-bindings-and-the-library-banner-constants)
- [15.9 Declaring Settings](#159-declaring-settings)
<!-- /toc -->

## 15.1 The Settings Block

**Syntax**

```syntax
#beguilerSettings {
    [ TYPE ] PROPERTY = VALUE ;
    …
}
```

`VALUE` is an integer literal, a string literal (`"…"` or raw `@"…"`), `true`/`false`, or an enum member written bare (`Z5`) or qualified by its enum (`eTarget.Z5`).

A `#beguilerSettings` block configures the compiler and the Inform 6 invocation which follows.  It can appear only at global scope.  You can write any number of blocks, in the entry file or in any included file, and a block can be empty.

Each entry names one declared property:  one of the compiler's own, in §15.2 to §15.6 (Appendix E lists them all), or one an extension declares (§15.9).  The optional leading `TYPE` is documentation only; it must be a type name or a data-type keyword, and is otherwise ignored.  Naming a property which doesn't exist, giving a value of the wrong type, or qualifying an enum value with the wrong enum name is a compile-time error.

**Precedence.**  Properties follow **first-writer-wins**:  the first block, in parse order, which sets a property fixes it, and later assignments to the same property are ignored.  A value given on the command line counts as written before any block (§17.4).  There are two exceptions:

- `includePaths` is **additive**:  every occurrence appends to the search path (duplicates are dropped).
- `release` and `seriesNumber` treat `0` as "not set", so a block which assigns `0` does not fix them.

**Entry-file properties.**  The following properties are read from the entry source file before parsing begins, by a textual scan of its `#beguilerSettings` blocks:

- `target`
- `includePaths`
- `beguiLibPath`
- `generateBlorb`
- `blorbAssetPath`
- `autoInitialize`
- every `int` property set to an integer literal, including those the library and extensions declare (`heapSize`, `worldBufSize`, `<string>`'s `stringHeapSize`)

These properties must be written in the entry file, as literal values, and not in an included file.

**Example**

```bgl
#beguilerSettings {
    target       = Z5;            // bare enum value
    outputPath   = "build";
    release      = 2;
    serial       = "260921";
    title        = "Cloak of Darkness";
    author       = "Roger Firth";
}
```

**See also** §17.4, §18.4, Appendix E.

## 15.2 Toolchain Paths

These properties locate external tools.

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `informPath` | string | none | Full path to the Inform 6 compiler binary.  Wins over `informName`. |
| `informName` | string | `"inform"` | File name of the Inform 6 binary, looked up next to the `beguiler` binary.  `"none"` skips the Inform 6 hand-off. |
| `beguiLibPath` | string | `"beguiLib"` | Directory of the Beguile system library, replacing the binary-adjacent default for the whole program; `-lib=` wins.  A relative path is relative to the entry file's folder.  Set in the entry file:  a block in another file naming a different library is a compile-time error. |
| `includePaths` | string | none | One or more directories, comma-separated, added to the search path for both `#include "…"` and `#includeI6` (§18.4).  Additive across blocks and with `-includepaths=`. |

A regular `"…"` `includePaths` entry is normalized:  separators are rewritten (§18.5), a relative path is resolved against the directory of the file containing the block, and the result is canonicalized.  Each such entry must name an existing directory, or the block is a compile-time error.  A raw `@"…"` entry is taken verbatim, with no rewriting, resolution, or check.

## 15.3 Compilation Settings

The following properties control the compilation itself:

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `target` | `eTarget` | `Glulx` | `Glulx`, `Z5`, or `Z8`.  Exposed to `#if` as the valueless `TARGET_GLULX` / `TARGET_ZCODE` flags (§14.2.4, Appendix F). |
| `outputPath` | string | `"output"` | Directory for the story file and every intermediate file; relative to the source file's directory (§20.1). |
| `errorFormat` | `eErrorFormat` | `E1` | Inform 6 diagnostic style.  Only `E1` and `E2` are accepted in a block. |
| `release` | int | `0` | Story release number, emitted as an I6 `Release` directive when non-zero. |
| `serial` | string | `""` | Story serial number, emitted as an I6 `Serial` directive when set.  Must be exactly six digits. |
| `omitUnusedRoutines` | bool | `true` | Inform 6 drops routines nothing references (§18.11). |
| `economy` | bool | `false` | Opt-in automatic text abbreviation (§18.11). |
| `filterVerbSplitWarnings` | bool | `true` | Hides Inform 6's "Verb declaration no longer has any verbs associated" warning for a library verb whose only word a Beguile line split off (§13.4.4); `false` shows it. |
| `rewritePaths` | bool | `true` | Rewrite `/` and `\` in every path (settings paths, `#include`, `#includeI6`) to the platform separator (§18.5). |

## 15.4 Runtime Settings

These properties size runtime structures in the generated code.  `framePoolSize` and `worldBufSize` must be at least 1, and `heapSize` must be 0 (the target's default) or at least 64.

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `framePoolSize` | int | sized from the program, at least `64` | Word slots in the frame pool, which backs local `array<char>` buffers on both targets and, on the Z-machine, local-variable overflow (§18.10). |
| `heapSize` | int | `1536` on the Z-machine, `16384` on Glulx | Words in the heap that local arrays and `<linq>` results are allocated from (§12.6).  Must be set in the entry file (§15.1). |
| `worldBufSize` | int | `128` | Objects per scratch buffer for `bgl.world` queries (§21.9); a walk which would exceed it stops there.  Must be set in the entry file (§15.1). |
| `autoInitialize` | bool | `true` | When `true`, the library binding wraps the program's entry point so that `bglInit()` runs first (§21.2, §23.3.1).  Set `false` when another library already replaces `Main`; the program then calls `bglInit()` itself.  A program built on no binding at all always calls it itself, regardless of this setting. |

`<string>` declares two more runtime settings, `stringHeapSize` and `stringCaptureSize` (§22.3).  The string pool used by `<string>` is not a setting:  its size is the I6 constant `bglStringPoolReserve`, declared in an `#i6` island before the extension is included (§22.3).

## 15.5 Game Metadata

These properties describe the work.  They feed the iFiction record which is written into a blorb (§15.6) and can be read from source (§15.7); however, apart from `release`, `serial`, and `ifid`, the compiler does **not** emit them into the story file.

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `title` | string | `""` | Title (iFiction `<title>`).  `"Untitled"` in the blorb record if unset. |
| `author` | string | `""` | Author (iFiction `<author>`).  `"Anonymous"` in the blorb record if unset. |
| `headline` | string | `""` | Subtitle or tagline (iFiction `<headline>`). |
| `genre` | string | `""` | Genre, e.g., `"Mystery"` (iFiction `<genre>`). |
| `description` | string | `""` | Blurb (iFiction `<description>`). |
| `language` | string | `""` | ISO-639 code, e.g., `"en"` (iFiction `<language>`). |
| `series` | string | `""` | Series name (iFiction `<series>`). |
| `seriesNumber` | int | `0` | Position in the series (iFiction `<seriesnumber>`); written only when greater than 0. |
| `firstPublished` | string | `""` | `"YYYY"` or `"YYYY-MM-DD"` (iFiction `<firstpublished>`). |
| `forgiveness` | string | `""` | `"Merciful"`, `"Polite"`, `"Tough"`, `"Nasty"`, or `"Cruel"` (iFiction `<forgiveness>`). |
| `ifid` | string | `""` | IFID in UUID form.  Auto-generated when blorb packaging is on (§15.6.2). |

**What reaches the story file.**  When `ifid` is set (explicitly or by generation), the compiler embeds it in the story file in the Treaty of Babel form which cataloging tools search for:

```i6
Array UUID_ARRAY string "UUID://A0B1C2D3-E4F5-6789-ABCD-EF0123456789//";
```

Everything else is available only through `#beguilerSettings.prop` (§15.7).

## 15.6 Blorb Packaging

Two properties control blorb packaging:

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `generateBlorb` | bool | `false` | Master switch for the asset scan and the blorb build. |
| `blorbAssetPath` | string | `"assets"` | Directory scanned for assets.  Relative to the source file's directory. |

When `generateBlorb` is `true`, two extra stages will run (§18.2):

1. **Before parsing**, the asset directory is scanned and `_blorbAssets.bgl` is written next to the source file.
2. **After Inform 6 succeeds**, the story file and every discovered asset are assembled into a blorb next to the story file:  `STEM.gblorb` for Glulx, `STEM.zblorb` for the Z-machine (§20.2).  The blorb carries an iFiction record built from §15.5.

A missing asset directory is reported, and packaging proceeds with no assets.

### 15.6.1 Asset Discovery and `_blorbAssets.bgl`

The asset directory is scanned **non-recursively**, and files whose name begins with `.` are ignored.  Each remaining file becomes one resource, classified by its extension (case-insensitively):

| Extension | Resource | Enum |
| --- | --- | --- |
| `png`, `jpg`, `jpeg` | picture | `eImages` |
| `aiff`, `aif` | sound | `eSounds` |
| anything else | generic data | `eUnknownAsset` |

Unrecognized files are packaged rather than dropped, and a console note lists them.

The enum member name is the file's stem in camelCase (`-`, `_`, and space are word breaks) followed by the capitalized extension:  `priest.png` gives `priestPng`, and `title-theme.aiff` gives `titleThemeAiff`.  Resource numbers are assigned from 1, pictures first, then sounds, then generic data, alphabetically within each group; the number is the member's value.

The enums `eImages`, `eSounds`, and `eUnknownAsset` are declared empty, along with the union `eAssets = eImages | eSounds | eUnknownAsset` (§21.12).  The generated `_blorbAssets.bgl` extends each of them:

```bgl
// Auto-generated by beguiler blorbifier: do not edit
extend enum eImages {
    priestPng = 1
}
extend enum eSounds {
    titleThemeAiff = 2
}
extend enum eUnknownAsset {
}
```

The file is regenerated on every build with packaging on.  Include it once from your source (`#include "_blorbAssets.bgl"`) so the members are in scope.

`generateBlorb` is also visible to `#if` as a symbol (§14.2.4).

### 15.6.2 IFID Generation and Persistence

When packaging is on and no `ifid` is set, the compiler supplies one:

1. **Deterministic.**  The IFID is a name-based UUID derived from the source file name, `author`, and `title`; the same three inputs will always yield the same IFID.
2. **Persisted.**  It is written to the head of `_blorbAssets.bgl` as `#beguilerSettings { ifid = "…"; }`, from where later builds read it back like any other setting.
3. **Explicit wins.**  An `ifid` in the program's own source takes precedence under first-writer-wins; the persisted value is a fallback.

## 15.7 Reading Settings From Source

**Syntax**

```syntax
#beguilerSettings.PROPERTY
```

In an expression, `#beguilerSettings.property` is replaced at parse time by a literal holding the property's value:  a string literal for string and enum properties (an enum value is its name, e.g., `"z5"`), an integer literal for integer properties, and `true`/`false` for boolean ones.  **Every declared property can be read.**  A name which is not a declared property is a compile-time error.

| Result | Properties |
| --- | --- |
| string | `title`, `author`, `headline`, `genre`, `description`, `language`, `series`, `firstPublished`, `forgiveness`, `ifid`, `target`, `outputPath`, `blorbAssetPath`, `informName`, `informPath`, `beguiLibPath`, `errorFormat`, `serial`, `includePaths` |
| int | `release`, `seriesNumber`, `framePoolSize`, `heapSize`, `worldBufSize` |
| bool | `generateBlorb`, `autoInitialize`, `economy`, `filterVerbSplitWarnings`, `omitUnusedRoutines`, `rewritePaths` |

`includePaths` is a list, which has no literal form, so it reads back as the `;`-joined search path.

A setting an extension declares (§15.9) is read the same way.

The value is whatever has been fixed when the reference is parsed.  A string property not yet set reads as `""` and an integer property as `0`, except for the runtime sizes (§15.4), which read as their defaults.  As such, the block which sets a property must precede, in parse order, any reference to it.

Inside the raw-I6 body of `#emitfirst`, `#emitlast`, `#storedEmitFirst`, and `#storedEmitLast`, the same properties are available as `##beguilerSettings.property` (§14.4.5).

**In a `#if` condition.**  The same reference reads the same value in a compile-time condition (§14.2.5).

**Example**

```bgl
#beguilerSettings { title = "Cloak of Darkness"; release = 3; }

const string story      = #beguilerSettings.title;     // -> "Cloak of Darkness"
const int    gameRelease = #beguilerSettings.release;  // -> 3

#if #beguilerSettings.target == "z8"
    // this build has the larger Z-machine address space
#endif
```

**See also** §14.2.5, §14.4.2, §14.4.5, §15.5.

## 15.8 Bindings and the Library Banner Constants

The IF library bindings declare the library's `story` and `headline` constants from the `title` and `headline` settings; see §23.3.2.

## 15.9 Declaring Settings

An extension can declare int settings of its own by extending the settings type, and reads them the same way (§15.7); unset, one reads as `0`.  A setting's entry can come before the include which declares it:

```bgl
extend class beguilerSettingsType {
    int stringHeapSize;   /// Bytes in the string heap (unset: 5120).
}
#if #beguilerSettings.stringHeapSize > 0
    const int bglStringHeapSize = #beguilerSettings.stringHeapSize;
#else
    const int bglStringHeapSize = 5120;
#endif
```
