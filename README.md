# Beguiler

First some context:

***Beguile*** is a clean, type-aware language inspired by C++, C#, and TypeScript.  You can use it to create Z-Machine and Glulx story files.

***Beguiler*** is the *Beguile compiler* which transpiles *Beguile* source into I6 and instruments the [Inform 6][i6] compiler.  It can serve as either an I6 precompiler, or as a compiler for the full Beguile language. It also hosts a Beguile language server for the VS Code extension.

***Beguilex*** is the *Beguile extension* for VS Code which you can find in the [Beguilex repo](https://github.com/onyxring/beguilex).  It provides syntax highlighting, diagnostics, hover, completion, embedded interpreters, run-time debugging support, and instruments the Beguiler compiler. It handles Beguiler configuration and is ***highly recommended***. Install it alongside this compiler for the full authoring experience.

Here's the Beguile compilation pipeline:

```
{idea}   -> you      -> game.bgl
game.bgl -> beguiler -> game.inf
game.inf -> inform6  -> game.ulx (or .z3, .z5, or .z8)
game.ulx -> beguiler -> game.gblorb (or .zblorb)
```
But most of this is transparent to the user. For all practical purposes, here's the pipeline for game designers:

```
{idea}   -> you      -> game.bgl
game.bgl -> beguiler -> game.gblorb (or .zblorb)
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

## Project Origin...

I've been writing code since the late 70's (starting on my TRS-80 model I), and I have some preconceived opinions on the *right* way to code; however, this AI-assisted coding thing is clearly more than a fleeting trend.  I believe it's a seismic shift, here to stay.  Naturally, I felt the need to understand it and explore what's possible.

**_orI6Precompiler_** was an existing project I started years ago, but set aside shortly after I achieved a "Hello World" level of functionality.  I used it as the starting point for my exploration into what LLM-assisted coding could do. The output of this exercise is what I eventually named Beguile.  To be clear: this wasn't actually a project I expected to release, it was more of test.  But, here I am releasing it...

The point is, some people have a moral issue with use of code-assisting LLMs, and that's okay.  If you hold that opinion, Beguile may not not be right for you.  Personally, I came out of this experiment with different ideas than I had going in.  And I still have strong boundaries on what I will and won't use AI for (for example, I don't use it for creative works of any kind, nor even the code I use to implement such works like Parser-based if).  But those are my personal opinions.  

