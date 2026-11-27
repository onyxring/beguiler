# 20 Build Outputs and Debugging

<!-- toc -->
- [20.1 Output Directory](#201-output-directory)
- [20.2 Files Produced](#202-files-produced)
- [20.3 Debug Builds](#203-debug-builds)
- [20.4 The Inform 6 Debug File](#204-the-inform-6-debug-file)
- [20.5 Console Output](#205-console-output)
<!-- /toc -->

## 20.1 Output Directory

Every file which a build produces, other than `_blorbAssets.bgl`, is written to a single **output directory**.  The directory is named by `-o` on the command line or, failing that, by `outputPath` in `#beguilerSettings`; by default it is `output`.  A relative name is resolved against the directory of the entry source file, never against the working directory, and the directory will be created if it doesn't exist.  An explicit output-file argument on the command line (§17.2) is the one exception:  it is written exactly where named.

## 20.2 Files Produced

For an entry file `game.bgl` targeting Glulx, a full build with blorb packaging and `--debug` will leave the following:

| File | Location | Produced |
| --- | --- | --- |
| `game.bgl.transpiled.inf` | output directory | always:  the generated Inform 6 source, retained after the build |
| `game.ulx` | output directory | on Inform 6 success (`game.z5` / `game.z8` for the Z-machine targets) |
| `game.gblorb` | output directory | with `generateBlorb` (`game.zblorb` for the Z-machine) |
| `_blorbAssets.bgl` | beside `game.bgl` | with `generateBlorb`, before parsing (§15.6) |
| `game.bgl.bgldbg` | output directory | with `--debug` (§20.3) |
| `game.bgl.transpiled.inf.dbg` | output directory | with `--debug`:  Inform 6's debug file (§20.4) |
| `i6patched/` | output directory | with `#mergeI6`:  the patched I6 files (§14.1.6) |

The transpiled file is always `SOURCE-FILE-NAME.transpiled.inf`, including the source extension, so a precompiler-mode `story.inf` yields `story.inf.transpiled.inf`.

## 20.3 Debug Builds

`--debug` (§17.3) changes three things:

- Inform 6 is run with `-k`, so it writes its own debug file, and the compiler writes its **debug bundle**, `SOURCE-FILE-NAME.bgldbg`, beside the transpiled file.
- Inform 6 is run with `-D`, so the library's debugging features are present:  its debugging verbs (`tree`, `purloin`, `actions`, …) and action tracing.
- The symbol `DEBUG` is defined (Appendix F), which turns on `bgl.output.log()` (§21.4).  The library bindings declare their debugging verbs only under it, so a program which names one outside `#if DEBUG` will fail to compile in an ordinary build.

A debug build of a program with a library binding also has a `heap` verb.  It prints the heap's size, the words in use, and the most ever in use, for setting `heapSize` (§12.6, §15.4), and reports the string heap of `<string>` the same way (§22.3):  its size, the bytes in use, and the most the run has needed.  A `stringHeapSize` set to that figure will hold the same run.

Everything else about the build is unchanged; `omitUnusedRoutines`, for one, still applies unless the program sets it to `false` (§18.11).

The bundle is plain text in three sections, each introduced by a bracketed header:

| Section | Contents |
| --- | --- |
| `[map]` | One line per generated I6 line that came from Beguile source:  the I6 line number, the Beguile file, and the Beguile line. |
| `[sym]` | The program's globals, objects (with their properties), functions, and extern objects, each with its I6 identifier and kind.  A `superposed` declaration which was never referenced is omitted. |
| `[types]` | Type information:  each enum with its values; each class and each object with its own properties, giving every property's Beguile type; and the declared type of every routine local and global. |

The bundle describes only what the compiler knows; VM addresses come from the Inform 6 file.

## 20.4 The Inform 6 Debug File

With `-k`, Inform 6 writes `gameinfo.dbg`, an XML database which maps VM addresses to I6 source lines and describes routines, their local-variable frames, globals, and properties.  The compiler moves it into the output directory as `SOURCE-FILE-NAME.transpiled.inf.dbg`.  Its format is Inform 6's own and is not covered here.

## 20.5 Console Output

A build reports the following, in order:  the compiler banner, naming the compiler version and build date, always the first line; blorb scan and IFID notes when packaging is on; any compile-time diagnostics (§19.1); on a successful transpile, the transpile confirmation and the exact Inform 6 command line which is about to run; Inform 6's own output, with its diagnostics rewritten to point at Beguile source lines (§19.1.1); and blorb assembly notes.  When the Inform 6 stage is skipped (`-inform=none`, §17.3), a message says so.  Inform 6's command line shows the switches which were passed through unchanged (§17.3.1), the `-k` and `-D` added by `--debug`, and the `-e` added by `economy`.
