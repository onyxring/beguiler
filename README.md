# Beguiler

First some context:

***Beguile*** is a clean, type-aware language inspired by C++, C#, and TypeScript.  You can use it to create Z-Machine and Glulx story files.

***Beguiler*** is the *Beguile compiler* which transpiles *Beguile* source into I6 and instruments the [Inform 6][i6] compiler. 

***Beguilex*** is the *Beguile extension* for VS Code which you can find in the [Beguilex repo](https://github.com/onyxring/beguilex).  It provides syntax highlighting, diagnostics, hover, completion, embedded interpreters, run-time debugging support, and instruments the [Inform 6][i6] compiler. It handles Beguiler configuration and is ***highly recommended***. Install it alongside this compiler for the full authoring experience.

Here's the Beguile compilation pipeline:

```
{idea}   -> you      -> game.bgl
game.bgl -> beguiler -> game.inf
game.inf -> inform6  -> game.ulx (or .z3, .z5, or .z8)
game.ulx -> beguiler -> game.gblorb (or .zblorb)
```

## Status: Preview

This is an **experimental preview**. The language and compiler are evolving rapidly and not all features are stable. Feedback and bug reports are welcome via [GitHub Issues](https://github.com/onyxring/beguiler/issues).

## Companion: VS Code Extension

If you are looking into Beguile, you should also use the associated [VS Code extension](https://github.com/onyxring/beguilex).  Although not strictly necessary, it makes handles all the commandline switches and let's you get down to building your stories quickly. Even if you decide to not write you games in Beguile, the extension is still worth adding to your repertoire, since it let's you debug your I6 code as well.

## Quick Start

### Download

Pre-built binaries for macOS (Intel + Apple Silicon), Linux, and Windows are attached to each [Release](https://github.com/onyxring/beguiler/releases).

### Build from source

Beguile requires a C++20 compiler.

```sh
clang++ -std=c++20 -O2 *.cpp -o beguiler
```

You'll also need [Inform 6][i6] installed.  You can pass the path to Inform to Beguiler in a number of ways, but the easiest is to configure it in the Beguilex extension.  Otherwise, you can pass it via the `-inform=` switch or define it on your `PATH`.  Beguile invokes Inform to produce the final story file.

### Compile a Beguile program from the command line
*(if you really **aren't** going to use the extension)*

```sh
./beguiler -inform=path\to\inform\ myStory.bgl
```

See [`docs/languageSpec.md`](docs/languageSpec.md) for the full language reference, [`Beguile Quick Start.md`](docs/quickStart.md) to get up and running quickly, and [`Beguile for the I6 Developer.md`](docs/Beguile%20for%20the%20I6%20Developer.md) for a guide aimed at existing Inform 6 authors. At the moment, since this is a preview, much of these documents are actively being revised.

## Documentation

- [`Beguile Quick Start`](docs/quickStart.md) — walkthrough to get things setup and running (In progress)
- [`Beguile for the I6 Developer`](docs/Beguile%20for%20the%20I6%20Developer.md) — introduction for Inform 6 authors (In progress)
- - [`Beguile language Spec`](docs/languageSpec.md) — language specification

## License

MIT — see [LICENSE](LICENSE).

[i6]: https://github.com/DavidKinder/Inform6
[Beguilex]: https://github.com/OnyxRing/Beguilex

## Use of AI

I've been writing code for decades (on my TRS-80 model I), but this AI-assisted coding thing is new (as I write this) and I wanted to explore it.  Beguile - including the compiler (Beguiler) and the extension (Beguilex) - is my first exploration into what Claude Code is capable of.  I used it to different degrees with each project.

