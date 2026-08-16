# Beguile Quick Start

Welcome to the Beguile Quick Start Guide.  Here I'll walk you through setting up a **working Beguile environment**: the compiler and the VS Code extension, wired together and working with Inform 6. It does not teach the language; for that, read [Beguile for the I6 Developer](Beguile%20for%20the%20I6%20Developer.md) next.

## What you are setting up
Beguile compiles your `.bgl` source to Inform 6, then hands off to the Inform 6 compiler to produce a story file:

```
game.bgl  ->  beguiler  ->  game.inf  ->  inform6  ->  game.z5 / game.ulx
```

Three pieces:

- **Inform 6** is the downstream compiler that Beguiler invokes to produce the final story file.  I don't spend time on the I6 install itself; I just walk through making sure it works with Beguiler. 
- **Beguiler** is the compiler (and language server): a single native binary.
- **Beguilex** is the VS Code extension. Although not strictly required, it is ***highly*** recommended: it drives Beguiler for you, adds syntax highlighting and completion, and provides step-through debugging.  The rest of this document assumes you have it installed.
## Prerequisites

- **A supported OS:** macOS (Intel or Apple Silicon), Linux (x64), or Windows (x64).
- **Visual Studio Code** ([code.visualstudio.com](https://code.visualstudio.com)).
- **The Inform 6 compiler.** 
## Step 0: Install Inform 6
Assuming you don't already have it installed, you can get it from [it's official repository](https://github.com/DavidKinder/Inform6). Download a pre-built executable, or build it from source, and note the path to the resulting `inform6` binary. Beguiler invokes it to produce story files. 

If you build from source, read this...

> **Build Inform 6 with a real path length.** Inform 6 keeps each `#include` path in a fixed-size buffer, and a default build, using the instructions from the Inform 6 repository ( `cc -O2 -o inform *.c`) falls back to a **128-character** limit, a size that was generous back when directory trees were shallower. Beguile's resolved include paths routinely exceed this.  If you are building it yourself, be sure and specify the platform (one of these lines, and assuming you have a cpp compiler installed locally):
> 
> # macOS
> cc -DMACOS -O2 -o inform6 *.c
> 
> # Linux
> cc -DLINUX -O2 -o inform6 *.c
> 
> # Windows  (MinGW / gcc)
> gcc -DPC_WIN32 -O2 -o inform6.exe *.c

You will also need to install one of I6's foundational libraries, either [Puny Inform](https://github.com/johanberntsson/PunyInform) for the popular, compressed library targeting the Z-machine, or the [Inform 6 Standard Library](https://gitlab.com/DavidGriffith/inform6lib) the OG library which supports Glulx as well as Z5 and Z8 versions.  Take note of the paths to where your library of choice lands.

> *Both libraries have their strengths.  If you want to target Glulx, you'll need the I6 standard library; however, for this quick start, Puny is referenced.  Best choice: You can download them both and choose the right tool for the right project.*
## Step 1: Install Beguiler

**Download a pre-built binary** from the Beguiler [Releases](https://github.com/onyxring/beguiler/releases) page and pick the archive for your platform:

- `beguile-<version>-macos-arm64.tar.gz` (Apple Silicon)
- `beguile-<version>-macos-x86_64.tar.gz` (Intel Mac)
- `beguile-<version>-linux-x64.tar.gz`
- `beguile-<version>-windows-x64.zip`

Unpack it and note the path to the `beguiler` executable. On macOS or Linux you may need to mark it executable (`chmod +x beguiler`); on macOS you may also need to clear the quarantine flag the first time (`xattr -d com.apple.quarantine beguiler`).

**Verify it runs.** This transpiles the bundled `hello.bgl`, which is library-free and needs no Inform 6:

```sh
./beguiler examples/hello.bgl
```

You should see `Compilation successful` and a transpiled `.inf` in the output directory. If so, Beguiler works.

## Step 2: Install the Beguilex extension

This step requires **Visual Studio Code** (see Prerequisites). If you do not already have it, install it from [code.visualstudio.com](https://code.visualstudio.com) first.

Download the latest `.vsix` from the Beguilex [Releases](https://github.com/onyxring/beguilex/releases) page, then install it either way:

- **From VS Code:** open the **Extensions** view, use the `...` menu, choose **Install from VSIX...**, and select the downloaded file.
- **From a terminal:** `code --install-extension beguile-language-<version>.vsix`. This needs the `code` command on your `PATH`; on macOS, run **Shell Command: Install 'code' command in PATH** from the VS Code command palette (Cmd+Shift+P) once, then reopen the terminal.

## Step 3: Point the tools at each other

The extension needs to know where Beguiler and Inform 6 live. Open VS Code **Settings** and set:

- **the Beguiler path** to the `beguiler` executable from Step 1, and
- **the Inform 6 path** to the `inform6` executable from the prerequisites.

That is the whole configuration. The extension passes these to Beguiler on every build, so you never touch command-line switches.
## Step 4: Build a complete game

`hello.bgl` proves the transpiler runs, but it emits only a bare routine (it sets `informName = "none"` to skip the Inform 6 handoff). To build a real, playable story you need Inform 6 configured (Steps 1 to 3) and an IF library.

- **In VS Code:** open `examples/cloakOfDarknessPuny.bgl` 
	The bundled **Cloak of Darkness** example uses [PunyInform](https://github.com/johanberntsson/PunyInform).  Add its folder to the example's include path (`#beguilerSettings { includePaths = "..."; }` in the `.bgl`. 

- Run the build or debug command from the extension.

## Step 5: Debugging

With the extension, set a breakpoint in your `.bgl` and start debugging to step through your code as it runs. Step-through debugging works for both **Z-machine** and **Glulx** targets.
## Troubleshooting

- **A library include reports "file not found":** the library folder is not on your include path. Add it via `includePaths` in `#beguilerSettings` or the extension setting.
- **Beguiler runs but no story file appears:** Inform 6 is not configured, or the source sets `informName = "none"`. Configure Inform 6 (Step 3) and remove or adjust `informName`.
- **macOS blocks the binary:** run `xattr -d com.apple.quarantine beguiler`.

## Next steps

- **[Beguile for the I6 Developer](Beguile%20for%20the%20I6%20Developer.md):** learn the language through a complete, annotated Cloak of Darkness port, mapped back to the Inform 6 you already know. This is the place to start.
- **[Language Specification](languageSpec.md):** the full reference.

Found a rough edge? That is expected in this preview. Please open an issue and include your `.bgl`, your target, the beguiler version, and the generated `.transpiled.inf`.
