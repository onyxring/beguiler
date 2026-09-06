# Beguiler

First some nomenclature:

***Beguile*** is a clean, type-aware language inspired by C++, C#, and TypeScript.  You write works of Interactive Fiction in it, which are then passed through the compiler to output Z-Machine and Glulx story files

***Beguiler*** is the *Beguile compiler* which transpiles *Beguile* source into I6 and instruments the [Inform 6][i6] compiler.  It can serve as either an I6 precompiler, or as a compiler for the full Beguile language. It also hosts a Beguile language server for the VS Code extension.

***Beguilex*** is the *Beguile extension* for VS Code which you can find in the [Beguilex repo](https://github.com/onyxring/beguilex).  It provides syntax highlighting, diagnostics, hover, completion, embedded interpreters, run-time debugging support. It instruments Beguiler and is ***highly recommended***. Install it alongside this compiler for the full authoring experience.

Here's the Beguile compilation pipeline:

```
{idea}   -> you      -> game.bgl
game.bgl -> beguiler -> game.inf
game.inf -> inform6  -> game.ulx (or .z5 or .z8)
game.ulx -> beguiler -> game.blorb (optionally)
```
But most of this is transparent to the user. For all practical purposes, here's the pipeline for game designers:

```
{idea}   -> you      -> game.bgl
game.bgl -> beguiler -> game.blorb (or just the game file; blorbification is optional)
```

## Status: Preview

This is an **experimental preview**. The language and compiler are evolving rapidly and not all features are stable. Feedback and bug reports are welcome via [GitHub Issues](https://github.com/onyxring/beguiler/issues).

## Companion: VS Code Extension

If you are looking into Beguile, you should also use the associated VS Code extension, [Beguilex](https://github.com/onyxring/beguilex).  Although not strictly necessary, it handles all the commandline switches and let's you get down to building your stories quickly. Even if you decide to not write games in Beguile, the extension is still worth adding to your repertoire, since it lets you debug your I6 code as well.

## Quick Start

### Download the binaries...

Pre-built binaries for macOS (Intel + Apple Silicon), Linux, and Windows are attached to each [Release](https://github.com/onyxring/beguiler/releases).

### Or build it from source...

Beguile needs a **C++17 compiler**. Nothing newer is required:

| Toolchain | Minimum |
|---|---|
| GCC | 7 |
| Clang / LLVM | 5 |
| Apple clang (bundled with Xcode) | 10 |
| MSVC | 2017 (19.14) |

*Built and tested here on Apple clang 15, LLVM 23 and GCC 16 — against both libc++ and
libstdc++, at `-std=c++17` and `-std=c++20`. The minimums above are the versions that
first shipped complete C++17 support.*

One command line builds it on every platform:

```sh
make
```

Or without `make`, which is the same thing:

```sh
c++ -std=c++17 -O2 -Wno-deprecated-declarations *.cpp -o beguiler
```

On Windows, name the output `beguiler.exe`. Substitute `g++` or `clang++` for `c++` if you want a specific toolchain; no other flags differ by platform.

`make` builds each file separately and tracks header dependencies, so an edit rebuilds only what it affects — worth using if you plan to change the compiler itself. `make debug` produces an unoptimised build with symbols.

### Compile a Beguile program from the command line
*(if you really **aren't** going to use the extension)*

```sh
./beguiler -inform=path\to\inform\ myStory.bgl
```

You'll also need [Inform 6][i6] installed.  You can pass the path to Inform to Beguiler in a number of ways, but the easiest is to configure it in the Beguilex extension.  Otherwise, you can pass it via the `-inform=` switch, define it in your Beguile source code, or define it on your `PATH` environment variable.  Beguile invokes Inform to produce the final story file.

Use [`Beguile Quick Start.md`](docs/quickStart.md) to get up and running quickly, and [`Beguile for the I6 Developer.md`](docs/Beguile%20for%20the%20I6%20Developer.md) for a guide aimed at existing Inform 6 authors. See [`docs/languageSpec.md`](docs/languageSpec.md) for the full language reference. At the moment, since this is a preview, much of these documents are actively being revised.

## License

MIT — see [LICENSE](LICENSE).

## A Note on the project origin and the use of AI...

I've been writing code since the late 70's (starting on my TRS-80 model I), and I've had some preconceived opinions on the *right* way to code; however, this AI-assisted coding thing is clearly more than a fleeting trend.  I believe it's a seismic shift, here to stay.  Naturally, I felt the need to understand it and explore what's possible.

**_orI6Precompiler_** was an existing project I started years ago, but set aside shortly after I achieved a "Hello World" level of functionality.  I used it as the starting point for my exploration into what LLM-assisted coding could do. The output of this exercise is what I eventually named Beguile.  To be clear: this wasn't actually a project I expected to release, it was more of test.  But, here I am releasing it...

The point is, some people have a moral issue with use of code-assisting LLMs.  And that's okay.  If you hold that opinion, Beguile is likely not right for you.  Personally, I came out of this experiment with different ideas than I had going in, but I retain strong boundaries on what I will and won't use AI for.  For example, I don't use it for creative works of any kind, graphics or narrative, and I won't use it to code the implementation of such works.  So my IF games themselves are off limits.  But those are my personal preferences.  

# If you have any questions...
...feel free to send me a note.

Thanks!

[i6]: https://github.com/DavidKinder/Inform6
[Beguilex]: https://github.com/OnyxRing/Beguilex


