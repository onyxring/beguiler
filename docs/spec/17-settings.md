# 17 Settings (`#beguilerSettings`)

<!-- toc -->
- [17.1 The Settings Block](#171-the-settings-block)
- [17.2 Toolchain Paths](#172-toolchain-paths)
- [17.3 Compilation Settings](#173-compilation-settings)
- [17.4 Runtime Settings](#174-runtime-settings)
- [17.5 Game Metadata](#175-game-metadata)
- [17.6 Blorb Packaging](#176-blorb-packaging)
  - [17.6.1 Asset Discovery and `_blorbAssets.bgl`](#1761-asset-discovery-and-blorbassetsbgl)
  - [17.6.2 IFID Generation and Persistence](#1762-ifid-generation-and-persistence)
- [17.7 Reading Settings From Source](#177-reading-settings-from-source)
- [17.8 Bindings and the Library Banner Constants](#178-bindings-and-the-library-banner-constants)
<!-- /toc -->

## 17.1 The Settings Block

**Syntax**

```syntax
#beguilerSettings {
    [ ⟨type⟩ ] ⟨property⟩ = ⟨value⟩ ;
    …
}
```

⟨value⟩ is an integer literal, a string literal (`"…"` or raw `@"…"`), `true`/`false`, or an enum
member written bare (`Z5`) or qualified by its enum (`eTarget.Z5`).

**Description**

A `#beguilerSettings` block configures the compiler and the Inform 6 invocation that follows. It is
permitted only at global scope (§14.7.1 lists the directive among the others). Any number of blocks
may appear, in the entry file or in any included file, and a block may be empty.

Each entry names one property from the fixed set in §17.2–§17.6 (Appendix E lists them all). The
optional leading ⟨type⟩ is documentation only; it must be a type name or a data-type keyword and is
otherwise ignored. Property names are case-insensitive, like every identifier. Naming a property that
does not exist, giving a value of the wrong type, or qualifying an enum value with the wrong enum
name, is a compile-time error.

**Precedence.** Properties follow **first-writer-wins**: the first block, in parse order, that sets a
property fixes it, and later assignments to the same property are ignored. A value given on the
command line counts as written before any block (§16.4). Two exceptions:

- `includePaths` is **additive**: every occurrence appends to the search path (duplicates are
  dropped).
- `release` and `seriesNumber` treat `0` as "not set", so a block that assigns `0` does not fix them.

**Entry-file properties.** Five properties are read from the entry source file before parsing begins,
by a textual scan of its `#beguilerSettings` blocks:

- `target`
- `includePaths`
- `generateBlorb`
- `blorbAssetPath`
- `autoInitialize`
- `worldBufSize`

The asset scan, the compile-time target symbols used by `#if` during the pre-scan, and the include
search path used to resolve forward references all come from this early read. These properties must
therefore be written in the entry file, as literal values, not in an included file.

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

**See also** §16.4, §18.4, Appendix E.

## 17.2 Toolchain Paths

These properties locate external tools. They are never written to the generated output.

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `informPath` | string | none | Full path to the Inform 6 compiler binary. Wins over `informName`. |
| `informName` | string | `"inform"` | File name of the Inform 6 binary, looked up next to the `beguiler` binary. `"none"` skips the Inform 6 hand-off. |
| `beguiLibPath` | string | `"beguiLib"` | Directory of the Beguile system library, replacing the binary-adjacent default for includes processed after the block. |
| `includePaths` | string | none | One or more directories, comma-separated, added to the search path for both `#include "…"` and `#includeI6` (§18.4). Additive across blocks and with `-includepaths=`. |

A regular `"…"` `includePaths` entry is normalized: separators are rewritten (§18.5), a relative path
is resolved against the directory of the file containing the block, and the result is canonicalized.
Each such entry must name an existing directory or the block is a compile-time error.
A raw `@"…"` entry is taken verbatim, with no rewriting, resolution or check. Every entry is also
emitted to Inform 6 as a `!% ++include_path=` line, one per directory, so that Inform 6 resolves its
own `Include` directives against the same set.

## 17.3 Compilation Settings

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `target` | `eTarget` | `Glulx` | `Glulx`, `Z5` or `Z8`. Emitted as `!% -G`, `!% -v5` or `!% -v8`, and exposed to `#if` as the valueless `TARGET_GLULX` / `TARGET_ZCODE` flags (Appendix F). The flags say which machine; for the Z-machine version, read the setting — `#if #beguilerSettings.target == "z8"` (§17.7). |
| `outputPath` | string | `"output"` | Directory for the story file and every intermediate file; relative to the source file's directory (§20.1). |
| `errorFormat` | `eErrorFormat` | `E1` | Inform 6 diagnostic style, emitted as `!% -E1` or `!% -E2`. Only `E1` and `E2` are accepted in a block. |
| `release` | int | `0` | Story release number, emitted as an I6 `Release` directive when non-zero. |
| `serial` | string | `""` | Story serial number, emitted as an I6 `Serial` directive when set. Must be exactly six digits. |
| `omitUnusedRoutines` | bool | `true` | Emits `!% $OMIT_UNUSED_ROUTINES=1`, so Inform 6 drops routines nothing references (§18.11). |
| `economy` | bool | `false` | Opt-in automatic text abbreviation (§18.11). |

## 17.4 Runtime Settings

These properties size runtime structures in the generated code. Each of the three sizes must be at
least 1.

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `framePoolSize` | int | `64` | Word slots in the frame pool that backs Z-machine local-variable overflow and local arrays (§18.10). |
| `linqScratchSize` | int | `32` | Elements per scratch buffer for `<linq>` query chains (§22.5). Emitted only when `<linq>` is included. |
| `worldBufSize` | int | `128` | Objects per scratch buffer for `bgl.world` queries (§21.9); a walk that would exceed it stops there. Sizes a runtime-library declaration, so it must be set in the entry file (§17.1). |
| `forInScratchSize` | int | `31` | Maximum elements in a literal-list `for (x in {…})` (§5.9.1). Emitted only when that form is used. |
| `rewritePaths` | bool | `true` | Rewrite `/` and `\` in every path (settings paths, `#include`, `#includeI6`) to the platform separator (§18.5). |
| `autoInitialize` | bool | `true` | When `true`, the library binding wraps the program's entry point so that `bglInit()` runs first (§21.2, §23.3.1). Set `false` when another library already replaces `Main`; the program then calls `bglInit()` itself. A program built on no binding at all always calls it itself, whatever this is set to — there is nothing to do the wrapping. |

The string pool used by `<string>` is not a setting: its size is the I6 constant
`bglStringPoolReserve`, declared in an `#i6` island before the extension is included (§22.3).

## 17.5 Game Metadata

These properties describe the work. They feed the iFiction record written into a blorb (§17.6) and
are readable from source (§17.7); apart from `release`, `serial` and `ifid` they are **not** emitted
into the story file by the compiler.

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `title` | string | `""` | Title (iFiction `<title>`). `"Untitled"` in the blorb record if unset. |
| `author` | string | `""` | Author (iFiction `<author>`). `"Anonymous"` in the blorb record if unset. |
| `headline` | string | `""` | Subtitle or tagline (iFiction `<headline>`). |
| `genre` | string | `""` | Genre, e.g. `"Mystery"` (iFiction `<genre>`). |
| `description` | string | `""` | Blurb (iFiction `<description>`). |
| `language` | string | `""` | ISO-639 code, e.g. `"en"` (iFiction `<language>`). |
| `series` | string | `""` | Series name (iFiction `<series>`). |
| `seriesNumber` | int | `0` | Position in the series (iFiction `<seriesnumber>`); written only when greater than 0. |
| `firstPublished` | string | `""` | `"YYYY"` or `"YYYY-MM-DD"` (iFiction `<firstpublished>`). |
| `forgiveness` | string | `""` | `"Merciful"`, `"Polite"`, `"Tough"`, `"Nasty"` or `"Cruel"` (iFiction `<forgiveness>`). |
| `ifid` | string | `""` | IFID in UUID form. Auto-generated when blorb packaging is on (§17.6.2). |

**What reaches the story file.** `release` and `serial` become I6 `Release` and `Serial` directives.
When `ifid` is set (explicitly or by generation) the compiler embeds it in the story file in the
Treaty of Babel form that cataloging tools search for:

```i6
Array UUID_ARRAY string "UUID://A0B1C2D3-E4F5-6789-ABCD-EF0123456789//";
```

Everything else is available only through `#beguilerSettings.prop` (§17.7). The IF library bindings
use that mechanism to declare the library's `story` and `headline` constants from `title` and
`headline` (§23.3.2).

## 17.6 Blorb Packaging

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `generateBlorb` | bool | `false` | Master switch for the asset scan and the blorb build. |
| `blorbAssetPath` | string | `"assets"` | Directory scanned for assets. Relative to the source file's directory. |

When `generateBlorb` is `true`, two extra stages run (§18.2):

1. **Before parsing**, the asset directory is scanned and `_blorbAssets.bgl` is written next to the
   source file.
2. **After Inform 6 succeeds**, the story file and every discovered asset are assembled into a blorb
   next to the story file: `<stem>.gblorb` for Glulx, `<stem>.zblorb` for the Z-machine
   (§20.2). The blorb carries an iFiction record built from §17.5.

A missing asset directory is reported and packaging proceeds with no assets.

### 17.6.1 Asset Discovery and `_blorbAssets.bgl`

The asset directory is scanned **non-recursively**; files whose name begins with `.` are ignored.
Each remaining file becomes one resource, classified by its extension (case-insensitively):

| Extension | Resource | Enum |
| --- | --- | --- |
| `png`, `jpg`, `jpeg` | picture | `eImages` |
| `aiff`, `aif` | sound | `eSounds` |
| anything else | generic data | `eUnknownAsset` |

Unrecognized files are packaged rather than dropped, and a console note lists them.

The enum member name is the file's stem in camelCase (`-`, `_` and space are word breaks) followed by
the capitalized extension: `priest.png` → `priestPng`, `title-theme.aiff` → `titleThemeAiff`.
Resource numbers are assigned from 1, pictures first, then sounds, then generic data, alphabetically
within each group; the number is the member's value.

The core library declares three empty enums, `eImages`, `eSounds` and `eUnknownAsset`, and the
union `eAssets = eImages | eSounds | eUnknownAsset` (§21.12). The generated `_blorbAssets.bgl` extends
each of them:

```bgl
// Auto-generated by beguiler blorbifier — do not edit
extend enum eImages {
    priestPng = 1
}
extend enum eSounds {
    titleThemeAiff = 2
}
extend enum eUnknownAsset {
}
```

The file is regenerated on every build with packaging on. Include it once from the source
(`#include "_blorbAssets.bgl"`) so the members are in scope; the union lets one routine accept any
asset while `eImages`-typed operations reject a sound id.

`generateBlorb` is also visible to `#if` as the symbol `generateBlorb`, carrying the value `true` or
`false`; test it by value, `#if (generateBlorb == true)`, because the symbol is always declared
(Appendix F).

### 17.6.2 IFID Generation and Persistence

When packaging is on and no `ifid` is set, the compiler supplies one:

1. **Deterministic.** The IFID is a name-based UUID derived from the source file name, `author`
   and `title`; the same three inputs always yield the same IFID.
2. **Persisted.** It is written to the head of `_blorbAssets.bgl` as
   `#beguilerSettings { ifid = "…"; }`, from where later builds read it back like any other
   setting.
3. **Explicit wins.** An `ifid` in the program's own source takes precedence under
   first-writer-wins; the persisted value is a fallback.
4. **Stable.** An IFID must never change once a work is published; rules 1–3 keep it constant across
   rebuilds even if the generated file is deleted.

## 17.7 Reading Settings From Source

**Syntax**

```syntax
#beguilerSettings.⟨property⟩
```

**Description**

In an expression, `#beguilerSettings.property` is replaced at parse time by a literal holding the
property's value: a string literal for string and enum properties (an enum value is its name, e.g.
`"z5"`), an integer literal for integer properties, and `true`/`false` for boolean ones. **Every
declared property may be read** — a property the author can write is one they can read back. A name
that is not a declared property is a compile-time error.

| Result | Properties |
| --- | --- |
| string | `title`, `author`, `headline`, `genre`, `description`, `language`, `series`, `firstPublished`, `forgiveness`, `ifid`, `target`, `outputPath`, `blorbAssetPath`, `informName`, `informPath`, `beguiLibPath`, `errorFormat`, `serial`, `includePaths` |
| int | `release`, `seriesNumber`, `framePoolSize`, `linqScratchSize`, `worldBufSize`, `forInScratchSize` |
| bool | `generateBlorb`, `autoInitialize`, `economy`, `omitUnusedRoutines`, `rewritePaths` |

`includePaths` is a list, which has no literal form, so it reads back as the `;`-joined search path.

The value is whatever has been fixed when the reference is parsed. A string property not yet set
reads as `""` and an integer property as `0`, except the three runtime sizes, which read as their
defaults; so the block that sets a property must precede, in parse order, any reference to it.

Inside the raw-I6 body of `#emitfirst`, `#emitlast`, `#storedEmitFirst` and `#storedEmitLast`, the
same properties are available as `##beguilerSettings.property` (§14.4.5).

**In a `#if` condition.** The same reference reads the same value in a compile-time condition
(§14.2.5), which is how a program asks a question the target flags cannot answer — `TARGET_ZCODE`
and `TARGET_GLULX` say which machine, and nothing more. A string property compares against a string
literal, ignoring case; an int property compares numerically; a bool property tests on its own. The
property must be written with no space around the `.`.

**Example**

```bgl
#beguilerSettings { title = "Cloak of Darkness"; release = 3; }

const string story      = #beguilerSettings.title;     // → "Cloak of Darkness"
const int    gameRelease = #beguilerSettings.release;  // → 3

#if #beguilerSettings.target == "z8"
    // this build has the larger Z-machine address space
#endif
```

**See also** §14.2.5, §14.4.2, §14.4.5, §17.5.

## 17.8 Bindings and the Library Banner Constants

The IF library bindings declare the library's `story` and `headline` constants from the `title` and `headline` settings; see §23.3.2.
