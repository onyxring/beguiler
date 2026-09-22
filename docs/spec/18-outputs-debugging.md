# 18 Build Outputs and Debugging

<!-- toc -->
- [18.1 Output Directory](#181-output-directory)
- [18.2 Files Produced](#182-files-produced)
- [18.3 Debug Builds](#183-debug-builds)
- [18.4 The Inform 6 Debug File](#184-the-inform-6-debug-file)
- [18.5 Console Output](#185-console-output)
<!-- /toc -->

## 18.1 Output Directory

Every file a build produces, other than `_blorbAssets.bgl`, is written to one **output directory**.
It is named by `-o` on the command line, else by `outputPath` in `#beguilerSettings`, else it is
`output`. A relative name is resolved against the directory of the entry source file, never against
the working directory; the directory is created if it does not exist. An explicit output-file
argument on the command line (§14.2) is the one exception: it is written exactly
where named.

## 18.2 Files Produced

For an entry file `game.bgl` targeting Glulx, a full build with blorb packaging and `--debug` leaves:

| File | Location | Produced |
| --- | --- | --- |
| `game.bgl.transpiled.inf` | output directory | always: the generated Inform 6 source, retained after the build |
| `game.ulx` | output directory | on Inform 6 success (`game.z5` / `game.z8` for the Z-machine targets) |
| `game.gblorb` | output directory | with `generateBlorb` (`game.zblorb` for the Z-machine) |
| `_blorbAssets.bgl` | beside `game.bgl` | with `generateBlorb`, before parsing (§15.6) |
| `game.bgl.bgldbg` | output directory | with `--debug` (§18.3) |
| `game.bgl.transpiled.inf.dbg` | output directory | with `--debug`: Inform 6's debug file (§18.4) |

The transpiled file is always `<source file name>.transpiled.inf`, including the source extension, so
a precompiler-mode `story.inf` yields `story.inf.transpiled.inf`. It is the file Inform 6 compiles
and the file its diagnostics refer to (§17.1.1); with `-inform=none` it is the
build's final product.

**Current-file-relative I6 includes.** Inform 6 resolves `Include ">name"` relative to the file
containing it, which after transpilation is the output directory rather than the source directory.
The compiler rewrites every such directive in the transpiled file so that it still resolves against
the source directory; an author's `#i6 { Include ">lib.h"; }` therefore behaves as it would in a
hand-written `.inf` beside the source.

## 18.3 Debug Builds

`--debug` (§14.3) changes two things: Inform 6 is run with `-k`, so it writes its own debug
file, and the compiler writes its **debug bundle**, `<source file name>.bgldbg`, beside the transpiled
file. Everything else about the build is unchanged; in particular `omitUnusedRoutines` still applies
unless the program sets it `false` (§16.11).

The bundle is plain text in three sections, each introduced by a bracketed header:

| Section | Contents |
| --- | --- |
| `[map]` | One line per generated I6 line that came from Beguile source: the I6 line number, the Beguile file and the Beguile line. This is the table the compiler itself uses to rewrite Inform 6 diagnostics, and the debugger uses to step through Beguile source while the story runs. |
| `[sym]` | The program's globals, objects (with their properties), functions and extern objects, each with its I6 identifier and kind. A `superposed` declaration that was never referenced is omitted, since it has no I6 counterpart. |
| `[types]` | Type information: each enum with its values; each class and each object with its own properties, giving every property's Beguile type; and the declared type of every routine local and global. This is what lets the debugger show a `dictionaryWord` or an `array<object>` as such rather than as a number. |

The bundle describes only what the compiler knows; VM addresses come from the Inform 6 file.

## 18.4 The Inform 6 Debug File

With `-k`, Inform 6 writes `gameinfo.dbg`, an XML database mapping VM addresses to I6 source lines and
describing routines, their local-variable frames, globals and properties. The compiler moves it into
the output directory as `<source file name>.transpiled.inf.dbg`. Its format is Inform 6's and is not
specified here.

Together the two files give a debugger the chain *VM address → I6 line → Beguile file and line*, plus
typed variables; the VS Code extension consumes both to provide source-level debugging of a running
story.

## 18.5 Console Output

A build reports, in order: the compiler banner; blorb scan and IFID notes when packaging is on; any
compile-time diagnostics (§17.1); on a successful transpile, the exact Inform 6 command line that is
about to run; Inform 6's own output, rewritten; and blorb assembly notes. Inform 6's command line
shows the switches that were passed through unchanged (§14.3.1), the `-k` added
by `--debug`, and the `-e` added by `economy`.
