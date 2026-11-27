# 17 Invoking the Compiler

<!-- toc -->
- [17.1 Synopsis](#171-synopsis)
- [17.2 Positional Arguments](#172-positional-arguments)
- [17.3 Options](#173-options)
  - [17.3.1 Pass-through switches](#1731-pass-through-switches)
- [17.4 Precedence Between the Command Line and `#beguilerSettings`](#174-precedence-between-the-command-line-and-beguilersettings)
- [17.5 Locating the Toolchain](#175-locating-the-toolchain)
- [17.6 Language-Server Mode](#176-language-server-mode)
- [17.7 Exit Status and Console Output](#177-exit-status-and-console-output)
- [17.8 Entry Mode](#178-entry-mode)
- [17.9 Examples](#179-examples)
- [17.10 Patch Tools](#1710-patch-tools)
<!-- /toc -->

## 17.1 Synopsis

```text
beguiler [options] SOURCE [OUTPUT]
beguiler --lsp [-lib=DIR] [-includepaths=DIR[,DIR…]]
beguiler --make-patch ORIGINAL EDITED [ORIGINAL EDITED …] -o PATCH
beguiler --apply-patch PATCH ORIGINAL -o COPY
```

`beguiler` is a single executable which transpiles a Beguile program to Inform 6, hands the result to the Inform 6 compiler, and (optionally) packages the story file into a blorb.  Run with no arguments, it will print a usage summary and exit with status 1.

## 17.2 Positional Arguments

**Source.**  The first argument which doesn't begin with `-` is the source file.  Its extension selects the entry mode (§17.8).

**Output.**  An optional second bare argument is the story file to write, used exactly as written.  When it is omitted, the story file is placed in the output directory (§20.1) as `STEM.EXT`, where `STEM` is the source file name without its extension and `EXT` is chosen by the target:  `ulx` for Glulx, `z5` or `z8` for the Z-machine.

A third bare argument is an error.

## 17.3 Options

Options are recognized only exactly as listed below.  Any other argument beginning with `-` is not an error; it is simply passed through to the Inform 6 command line (§17.3.1).

| Option | Effect | `#beguilerSettings` equivalent |
| --- | --- | --- |
| `-o DIR` | Output directory for the story file and all intermediate files.  `-o` without a following argument is an error. | `outputPath` |
| `-G`, `-g` | Target Glulx. | `target = Glulx` |
| `-z5`, `-Z5` | Target the Z-machine, version 5. | `target = Z5` |
| `-z8`, `-Z8` | Target the Z-machine, version 8. | `target = Z8` |
| `-EN` | Inform 6 error-message format; `N` is a single digit.  `E1` is the Microsoft style, `E2` the Macintosh style. | `errorFormat` |
| `-inform=<name>` | The Inform 6 compiler to run.  An absolute path is used verbatim; anything else is resolved relative to the directory containing the `beguiler` binary.  The name `none` skips the Inform 6 hand-off:  the transpiled `.inf` is written and compilation stops there. | `informName` / `informPath` |
| `-includepaths=DIR[,DIR…]` | Adds one or more directories to the include search path (§18.4).  Comma-separated; whitespace around each entry is trimmed; duplicates are ignored.  Entries are taken as written:  no separator rewriting, no relative-path resolution, no existence check. | `includePaths` |
| `-lib=DIR` | Location of the Beguile system library (the `beguiLib` tree).  An absolute path is used verbatim; a relative path is anchored to the directory containing the `beguiler` binary.  Surrounding `"` or `'` quotes are stripped. | `beguiLibPath` |
| `-filterVerbSplitWarnings=true\|false` | Whether to hide Inform 6's warning about a library verb left with no words by a split (§13.4.4). | `filterVerbSplitWarnings` |
| `--debug`, `-D` | Debug build (§20.3):  Inform 6 is run with `-k` and `-D`, the debug bundle is written, and `DEBUG` is defined. | none |
| `--lsp` | Language-server mode (§17.6).  No compilation takes place. | none |

### 17.3.1 Pass-through switches

Any argument beginning with `-` which is not one of the above options is appended verbatim to the Inform 6 command line, after the compiler's own switches.  This is how you hand Inform 6 a switch Beguile doesn't model, such as `-s` (statistics) or `-~S` (strict mode off).  Nothing is validated; an unknown switch will surface as an Inform 6 error.

The `-EN` option is the one exception to "exactly as listed":  any `-E` followed by a digit is consumed by Beguile rather than passed through.

## 17.4 Precedence Between the Command Line and `#beguilerSettings`

Most options can also be set from the source with a `#beguilerSettings` block (§15.1).  Where both are given, the rule depends on the property:

| Property | Rule |
| --- | --- |
| `target` | Command line wins. |
| `errorFormat` | Command line wins. |
| `outputPath` | Command line (`-o`) wins. |
| `informName` / `informPath` | Command line (`-inform=`) wins over both; between the two block properties, `informPath` wins over `informName` (§17.5). |
| `includePaths` | Additive.  Command-line entries are added first, then block entries in the order they are parsed. |
| `filterVerbSplitWarnings` | Command line wins. |
| `beguiLibPath` | Command line wins.  Otherwise the entry file's block names the library for the whole program, read before any of it (§15.1). |

`--debug`, `--lsp`, and pass-through switches have no settings equivalent; `generateBlorb`, `blorbAssetPath`, `economy`, `omitUnusedRoutines`, `autoInitialize`, `release`, `serial`, the runtime sizes, and the game-metadata properties have no command-line equivalent.

## 17.5 Locating the Toolchain

The compiler never searches the system `PATH`.  By default, both of its dependencies are located relative to the `beguiler` binary.

**The Beguile system library** (`beguiLib`).  This is `-lib=DIR` if given, or else the `beguiLib` subdirectory of the directory containing the binary.  This is where the auto-loaded core files, the built-in templates (§18.9), and every `#include <…>` extension live.

**The Inform 6 compiler** is located by the following, in decreasing order of precedence:

1. `-inform=<name>` on the command line.
2. `informPath = "…"` in `#beguilerSettings`:  a full path to the binary, used verbatim.
3. `informName = "…"` in `#beguilerSettings`:  a file name, resolved relative to the directory containing the `beguiler` binary.
4. The default:  a file named `inform` next to the `beguiler` binary.  If nothing was configured and no such file exists, `../inform6/inform6` relative to the binary is tried as a last resort.

Rules 1 and 3 accept `none`, which skips the Inform 6 stage entirely.

## 17.6 Language-Server Mode

`--lsp` anywhere on the command line switches the binary into Language Server Protocol mode.  It will then read JSON-RPC messages from standard input and write responses to standard output, using the LSP `Content-Length` framing, until its input closes.  No source file is compiled and nothing is written to disk.

In this mode only `-lib=` and `-includepaths=` are honored (as in §17.3); every other argument is ignored.

## 17.7 Exit Status and Console Output

The process exits with status 0 when the requested work completed, and 1 when it did not:  a usage error, a missing source file, a compile-time error (§19.1, §19.2), a failure to write the transpiled file, or a non-zero exit from Inform 6.

What a build prints is covered in §20.5.

## 17.8 Entry Mode

The source file's extension, and nothing else, selects the compilation mode (§16.1).  In precompiler mode the target is read from the file's own `!%` lines (§16.1.2).

## 17.9 Examples

The following are a few typical command lines:

```text
beguiler game.bgl
beguiler -z5 -o build game.bgl
beguiler -G --debug -includepaths=../lib,../shared game.bgl
beguiler -inform=none game.bgl            # transpile only; leaves output/game.bgl.transpiled.inf
beguiler -lib=/opt/beguile/beguiLib -inform=/opt/inform6/inform6 game.bgl
```

## 17.10 Patch Tools

The patch tools make and read the patches which `#mergeI6` applies (§14.1.6).  Each must be the first argument; nothing is compiled, and no banner is printed.

**`--make-patch`** writes one patch of the changes from each *original* to its *edited* copy, as a unified diff with three lines of context, naming each file by the original's file name, after any comment lines at the top of the patch it replaces.  Lines which differ only in trailing whitespace are unchanged.  When nothing changed, it prints `no changes` and writes no file.

**`--apply-patch`** writes *original* with the patch's merge chunks for that file name applied.  A merge chunk which matches nowhere is skipped rather than being an error; the tool prints a JSON object giving the number applied and the text of each skipped merge chunk:

```text
{"applied": 3, "failed": ["@@ -2458 @@ (patch line 19)\n …"]}
```

Both exit with status 0 on success, and with 1 when a file can't be read or written or the patch is malformed.
