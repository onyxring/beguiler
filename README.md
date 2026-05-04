# Beguiler

***Beguile*** is a a clean, type-aware language inspired by C++, C#, and TypeScript.  It is used to create Z-Machine and Glulx story files.

***Beguiler*** is the compiler which transpiles Beguile into the Inform 6 language and instruments the [Inform 6][i6] compiler. 

## Status: Preview

This is an **experimental preview**. The language and compiler are evolving rapidly and not all features are stable. Feedback and bug reports are welcome via [GitHub Issues](https://github.com/onyxring/beguiler/issues).

## Companion: VS Code Extension

If you are looking into Beguile, you should also use the associated VS Code extenion, [Beguilex](https://github.com/onyxring/beguilex).  It provides syntax highlighting, diagnostics, hover, completion, embedded interpreters, and run-time debugging support. It handles all the configuration and is ***highly recommended***. Install it alongside this compiler for the full authoring experience.

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

See [`docs/languageSpec.md`](docs/languageSpec.md) for the full language reference, [`Beguile Quick Start.md`](docs/quickStart.md) to get up and running quickly, and [`Beguile for the I6 Developer.md`](docs/Beguile%20for%20the%20I6%20Developer.md) for a guide aimed at existing Inform 6 authors.

## Documentation

- [`Beguile Quick Start`](docs/quickStart.md) — walkthrough to get things setup and running
- [`Beguile language Spec`](docs/languageSpec.md) — language specification
- [`Beguile for the I6 Developer`](docs/Beguile%20for%20the%20I6%20Developer.md) — introduction for Inform 6 authors

## License

MIT — see [LICENSE](LICENSE).

[i6]: https://github.com/DavidKinder/Inform6
[Beguilex]: https://github.com/OnyxRing/Beguilex
