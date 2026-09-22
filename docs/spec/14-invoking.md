# 14 Invoking the Compiler

<!-- toc -->
- [14.1 Synopsis](#141-synopsis)
- [14.2 Positional Arguments](#142-positional-arguments)
- [14.3 Options](#143-options)
  - [14.3.1 Pass-through switches](#1431-pass-through-switches)
- [14.4 Precedence Between the Command Line and `#beguilerSettings`](#144-precedence-between-the-command-line-and-beguilersettings)
- [14.5 Locating the Toolchain](#145-locating-the-toolchain)
- [14.6 Language-Server Mode](#146-language-server-mode)
- [14.7 Exit Status and Console Output](#147-exit-status-and-console-output)
- [14.8 Entry Mode](#148-entry-mode)
- [14.9 Examples](#149-examples)
<!-- /toc -->

## 14.1 Synopsis

```text
beguiler [options] <source> [<output>]
beguiler --lsp [-lib=<dir>] [-includepaths=<dir>[,<dir>…]]
```

`beguiler` is a single executable that transpiles a Beguile program to Inform 6, hands the result to
the Inform 6 compiler, and (optionally) packages the story file into a blorb. Run with no arguments it
prints a usage summary and exits with status 1.

The first line of console output is always a banner naming the compiler version and build date; the
compiler's version is also visible to the program as the pre-defined symbol `beguiler` (Appendix F).

## 14.2 Positional Arguments

**Source.** The first argument that does not begin with `-` is the source file. Its extension selects
the entry mode (§14.8): a `.bgl` file is compiled in default mode, a `.inf` file in precompiler mode.

**Output.** An optional second bare argument is the story file to write. When it is given it is used
exactly as written; when it is omitted the story file is placed in the output directory
(§18.1) as `<stem>.<ext>`, where `<stem>` is the source file name without its
extension and `<ext>` is chosen by the target: `ulx` for Glulx, `z5` or `z8` for the Z-machine.

A third bare argument is an error.

## 14.3 Options

Options are recognized only when they appear exactly as listed (upper- and lower-case variants are
listed where both are accepted). Any other argument beginning with `-` is not an error: it is passed
through, unchanged, to the Inform 6 command line (§14.3.1).

| Option | Effect | `#beguilerSettings` equivalent |
| --- | --- | --- |
| `-o <dir>` | Output directory for the story file and all intermediate files. `-o` without a following argument is an error. | `outputPath` |
| `-G`, `-g` | Target Glulx. | `target = Glulx` |
| `-z5`, `-Z5` | Target the Z-machine, version 5. | `target = Z5` |
| `-z8`, `-Z8` | Target the Z-machine, version 8. | `target = Z8` |
| `-E<n>` | Inform 6 error-message format; `<n>` is a single digit. `E1` is the Microsoft style, `E2` the Macintosh style. Emitted into the generated source as `!% -E<n>`. | `errorFormat` |
| `-inform=<name>` | The Inform 6 compiler to run. An absolute path is used verbatim; anything else is resolved relative to the directory containing the `beguiler` binary. The name `none` skips the Inform 6 hand-off: the transpiled `.inf` is written and compilation stops there. | `informName` / `informPath` |
| `-includepaths=<dir>[,<dir>…]` | Adds one or more directories to the include search path (§16.4). Comma-separated; whitespace around each entry is trimmed; duplicates are ignored. Entries are taken as written: no separator rewriting, no relative-path resolution, no existence check. | `includePaths` |
| `-lib=<dir>` | Location of the Beguile system library (the `beguiLib` tree). An absolute path is used verbatim; a relative path is anchored to the directory containing the `beguiler` binary. Surrounding `"` or `'` quotes are stripped. | `beguiLibPath` |
| `--debug` | Debug build: Inform 6 is run with `-k`, and the debug bundle is written (§18.3). | none |
| `--lsp` | Language-server mode (§14.6). No compilation takes place. | none |

### 14.3.1 Pass-through switches

Any argument beginning with `-` that is not one of the options above is appended verbatim to the
Inform 6 command line, after the compiler's own switches. This is the way to hand Inform 6 a switch
Beguile does not model, for example `-s` (statistics) or `-~S` (strict mode off). No validation is
performed; an unknown switch surfaces as an Inform 6 error.

The `-E<n>` option is the one exception to "exactly as listed": any `-E` followed by a digit is
consumed by Beguile rather than passed through.

## 14.4 Precedence Between the Command Line and `#beguilerSettings`

Most options can also be set from the source with a `#beguilerSettings` block (§15.1). Where both are
given, the rule is per property:

| Property | Rule |
| --- | --- |
| `target` | Command line wins. |
| `errorFormat` | Command line wins. |
| `outputPath` | Command line (`-o`) wins. |
| `informName` / `informPath` | Command line (`-inform=`) wins over both; between the two block properties, `informPath` wins over `informName` (§14.5). |
| `includePaths` | Additive. Command-line entries are added first, then block entries in the order they are parsed. |
| `beguiLibPath` | The command line (`-lib=`, or the default) locates the library for the core files loaded before the program's own source is read; a block value replaces it for every `#include` processed after the block. |

`--debug`, `--lsp` and pass-through switches have no settings equivalent; `generateBlorb`,
`blorbAssetPath`, `economy`, `omitUnusedRoutines`, `autoInitialize`, `release`, `serial`, the runtime
sizes and the game-metadata properties have no command-line equivalent.

## 14.5 Locating the Toolchain

The compiler never searches the system `PATH`. Both of its dependencies are located relative to the
`beguiler` binary unless told otherwise.

**The Beguile system library** (`beguiLib`). `-lib=<dir>` if given; otherwise the `beguiLib`
subdirectory of the directory containing the binary. This is where the auto-loaded core files, the
built-in templates (§16.9) and every `#include <…>` extension live.

**The Inform 6 compiler**, in decreasing order of precedence:

1. `-inform=<name>` on the command line.
2. `informPath = "…"` in `#beguilerSettings`: a full path to the binary, used verbatim.
3. `informName = "…"` in `#beguilerSettings`: a file name, resolved relative to the directory
   containing the `beguiler` binary.
4. The default: a file named `inform` next to the `beguiler` binary. If nothing was configured and no
   such file exists, `../inform6/inform6` relative to the binary is tried as a last resort.

Rules 1 and 3 accept `none` to skip the Inform 6 stage entirely.

## 14.6 Language-Server Mode

`--lsp` anywhere on the command line switches the binary into Language Server Protocol mode: it reads
JSON-RPC messages from standard input and writes responses to standard output, using the LSP
`Content-Length` framing, until its input closes. No source file is compiled and nothing is written to
disk.

In this mode only `-lib=` and `-includepaths=` are honored (with the meanings in §14.3); every other
argument is ignored. The VS Code extension starts the compiler this way to provide diagnostics,
completion and navigation while editing.

## 14.7 Exit Status and Console Output

The process exits with status 0 when the requested work completed, and 1 when it did not: a usage
error, a missing source file, a compile-time error (§17.2), a failure to write the transpiled file, or a
non-zero exit from Inform 6. Only the first compile-time error is reported; compilation stops there.

On success the console shows the transpile confirmation, the exact Inform 6 command line that was run,
and Inform 6's own output with its diagnostics rewritten to point at Beguile source lines (§17.1.1).
When the Inform 6 stage is skipped (`none`), the message says so.

## 14.8 Entry Mode

The source file's extension, and nothing else, selects the compilation mode: `.bgl` is default mode
and `.inf` is precompiler mode, as specified in §13.1.

In precompiler mode the compiler synthesizes no ICL header: the `!%` lines at the top of the `.inf`
file are the sole authority, and the target is read from them (`-G` selects Glulx; `-z` selects the
Z-machine, version 5; `-v3`, `-v5` or `-v8` select an explicit Z-machine version). A `-G` or
`-z5`/`-z8` option on the command line, or a `target` in a `#beguilerSettings` block, is still
accepted but the `!%` value is what Inform 6 sees.

## 14.9 Examples

```text
beguiler game.bgl
beguiler -z5 -o build game.bgl
beguiler -G --debug -includepaths=../lib,../shared game.bgl
beguiler -inform=none game.bgl            # transpile only; leaves build/game.bgl.transpiled.inf
beguiler -lib=/opt/beguile/beguiLib -inform=/opt/inform6/inform6 game.bgl
```
