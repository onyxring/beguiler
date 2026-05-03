# Beguile

> Beautifully deceptive — a modern programming language for interactive fiction.

Beguile is a compiler that transpiles a clean, type-aware language inspired by C++, C#, and TypeScript into [Inform 6][i6], targeting Z-Machine (versions 3, 5, 8) and Glulx story files.

## Status: Preview

This is an **experimental preview**. The language and compiler are evolving rapidly, breaking changes between versions are expected, and not all features are stable. Feedback and bug reports are welcome via [GitHub Issues](https://github.com/onyxring/beguile/issues).

## Companion: VS Code Extension

[Beguilex](https://github.com/onyxring/beguilex) provides syntax highlighting, diagnostics, hover, completion, debugging, and an embedded Z-Machine interpreter for Beguile in VS Code. Install it alongside this compiler for the full authoring experience.

## Quick Start

### Download

Pre-built binaries for macOS (Intel + Apple Silicon), Linux, and Windows are attached to each [Release](https://github.com/onyxring/beguile/releases).

### Build from source

Beguile requires a C++20 compiler.

```sh
make
```

Or directly:

```sh
clang++ -std=c++20 -O2 *.cpp -o beguiler
```

You'll also need [Inform 6][i6] on your `PATH` (or pointed at via the `-inform=` flag), which the compiler invokes to produce the final story file.

### Compile a Beguile program

```sh
./beguiler myStory.bgl
```

See [`docs/languageSpec.md`](docs/languageSpec.md) for the full language reference, and [`Beguile for the I6 Developer.md`](Beguile%20for%20the%20I6%20Developer.md) for a guide aimed at existing Inform 6 authors.

## Documentation

- [`docs/languageSpec.md`](docs/languageSpec.md) — language specification
- [`Beguile for the I6 Developer.md`](Beguile%20for%20the%20I6%20Developer.md) — introduction for Inform 6 authors
- [`tests/`](tests/) — example `.bgl` programs and expected outputs

## License

MIT — see [LICENSE](LICENSE).

[i6]: https://github.com/DavidKinder/Inform6
