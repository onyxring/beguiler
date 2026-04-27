# Beguile Compiler — Architecture

This is the orientation map. Read it once to learn where things live, then keep it open while working.

The companion docs:
- `NAME_RESOLUTION.md` — how identifiers get matched to declarations.
- `GOTCHAS.md` — non-obvious rules and historical hazards.

---

## At a glance

Beguile is a C++17 transpiler that takes a `.bgl` source file, produces Inform 6 source, and then invokes the I6 compiler binary to produce Z-machine or Glulx bytecode. Optionally it then packages that bytecode (plus assets and metadata) into a Blorb archive.

```
.bgl source
    │
    ▼
[ Pre-scan ]      register stubs for forward references
    │
    ▼
[ Parse ]         tokenise, dispatch via grammar table, build AST
    │             (AST lives in languageService.globals)
    ▼
[ Emit ]          walk AST, write I6 source to .transpiled.inf
    │
    ▼
[ Inform 6 ]      external compiler binary → bytecode (.ulx / .z5 / etc.)
    │
    ▼
[ Blorb ]         (optional) package bytecode + assets → .gblorb / .zblorb
```

Each phase reads from / writes to global state (`languageService`, `emitter`) — no plumbing of intermediate values between phases.

---

## Pipeline phases

| Phase                | Driver                                                  | Reads / writes                                  |
|----------------------|---------------------------------------------------------|-------------------------------------------------|
| Argument parsing     | [`beguiler::parseArgs`](../beguiler.cpp)                | CLI args → `settings` struct                    |
| Blorb pre-scan       | [`extractBlorbSettings`](../beguiler.cpp)               | `.bgl` → `beguilerSettings` (target, IFID, …)   |
| Asset scan (blorb)   | [`Blorb::scanAssets`](../blorb.cpp)                     | Asset directory → `_blorbAssets.bgl`            |
| **Pre-scan (Pass 1)**| [`preScanFile`](../bglPreScanner.cpp)                   | `.bgl` → type/object/function stubs in `languageService` |
| **Parse (Pass 2)**   | [`parseFile`](../bglParser.cpp) → `processStatementDispatch` | `.bgl` → AST in `languageService.globals` |
| Schema defaults      | `applySchemaDefaults`                                   | Fills `beguilerSettings` from declared defaults |
| **Emit**             | `emitter.emit(languageService.globals)`                 | AST → `.transpiled.inf`                         |
| Debug bundle         | `emitter.writeDebugBundle`                              | (with `--debug`) → `.bgldbg` (source map + meta)|
| I6 invocation        | `popen(i6Cmd)` in [`go()`](../beguiler.cpp)             | `.transpiled.inf` → `.ulx` / `.z5` / etc.       |
| Blorb packaging      | [`Blorb::build`](../blorb.cpp)                          | Bytecode + assets → `.gblorb` / `.zblorb`       |

Entry point is [`_main.cpp`](../_main.cpp), which instantiates `beguiler` and calls `go()`. Everything else flows from there.

---

## File responsibilities

One sentence each. Match against actual contents — names can mislead.

| File | What it owns |
|------|--------------|
| [`_main.cpp`](../_main.cpp) | Entry point. Constructs `beguiler` and calls `go()`. |
| [`beguiler.cpp`](../beguiler.cpp) | Pipeline driver. CLI parsing, phase orchestration, I6 invocation, output paths. |
| [`fileLexer.cpp`](../fileLexer.cpp) | Token producer. File-include stack, `#i6{}` raw-text reader, Unicode→ZSCII for diacriticals. |
| [`bglPreScanner.cpp`](../bglPreScanner.cpp) | Pass-1 stub registration so Pass 2 can resolve forward references. |
| [`bglParser.cpp`](../bglParser.cpp) / [`.h`](../bglParser.h) | The parser. `parseFile`, `processStatementDispatch`, all the `processXxx` handlers, and most resolution logic. |
| [`bglLanguageService.cpp`](../bglLanguageService.cpp) / [`.h`](../bglLanguageService.h) | Type registry and AST root. Holds `globals`, `verbs`, `globalInits`, counter state. Singleton `languageService`. |
| [`grammarTable.cpp`](../grammarTable.cpp) | Pattern → handler table that drives parser dispatch. |
| [`token.cpp`](../token.cpp) / [`token.h`](../token.h) | Token type, classification helpers (`is`, `isDataType`, etc.). |
| [`typeDef.cpp`](../typeDef.cpp) / [`.h`](../typeDef.h) | AST node hierarchy: `classDef`, `objectDef`, `functionDef`, `variableDeclaration`, `enumDef`, `typeMember`, `statement` and its subclasses. Also `eCompileContext`. |
| [`i6Emitter.cpp`](../i6Emitter.cpp) / [`.h`](../i6Emitter.h) | AST → I6 source. Singleton `emitter`. Holds source-map and Z-machine spill state. |
| [`lspServer.cpp`](../lspServer.cpp) / [`.h`](../lspServer.h) | Language Server Protocol handlers. Standalone — only used when invoked in LSP mode. |
| [`blorb.cpp`](../blorb.cpp) / [`.h`](../blorb.h) | Blorb packaging. Asset scanning, IFiction XML, archive build. |

---

## Key shared state

Beguile relies on a small number of process-wide singletons rather than threading data through function arguments. Knowing what each one holds is the fastest way to get oriented.

### `languageService` — the AST and symbol table

Defined in `bglLanguageService.h`/`.cpp`. The compiler's central registry.

| Field | Holds |
|-------|-------|
| `globals` | Top-level declarations in **source order**. Mixed list: `classDef`, `enumDef`, `objectDef`, `functionDef`, `variableDeclaration`, `arrayDeclaration`. The emitter walks this in order. |
| `objectTypes` | Class/enum type definitions, indexed for type lookup. |
| `objectInstances` | All `objectDef` instances. |
| `verbs` | `verbObjectDef`s — used for resolving action-constant references like `##Take`. |
| `globalInits` | `(varName, initBody)` pairs — bodies of `init` emitters for global class-typed variables. Drives the synthesized `bglInit()` routine. |
| `startupBlocks` | Raw I6 from `#startup { … }` directives — also goes into `bglInit()`. |
| `emitFirstBlocks` / `emitLastBlocks` | Raw I6 emitted at specific points in the I6 output. |
| `ternaryTempCount`, `tryCatchCounter`, `captureCounter` | Counters that name synthesised globals/labels uniquely. |

`getType(name)` is the type-registry lookup. Returns the declared `typeDef*` for the name, or a sentinel "unknown" type. Casting the result to `classDef*`, `enumDef*`, or `objectDef*` (with `dynamic_cast`) tells you which kind it is.

### `emitter` — the I6 emission state

Defined in `i6Emitter.h`/`.cpp`. Active during the emit phase.

| Field | Purpose |
|-------|---------|
| `out` | `std::stringstream` accumulating the I6 source. |
| `sourceMap` | `vector<tuple<int, string, int>>` — `(i6Line, bglFile, bglLine)` triples. Drives diagnostic remapping when I6 errors come back, and feeds the `.bgldbg` debug bundle. |
| `currentSpillAliases`, `currentSpillCount`, `framePoolSize` | Z-machine local-variable spill state — only relevant for Z3/Z5/Z8 targets where local count is constrained. |

### `beguilerSettings`

Holds compile-time settings declared via `#beguilerSettings { … }` and overridden by CLI args. Target VM, output filename, IFID, headline, blorb config, etc.

### `settings` (in `beguiler.cpp`)

Holds CLI/runtime settings: input file path, output directory, debug flag, I6 binary path. Distinct from `beguilerSettings` (which is from the source file).

---

## The parser dispatch model

Source declarations and statements are recognised by a **pattern table**, not a recursive-descent grammar. This makes adding new constructs cheap.

The table lives in `grammarTable.cpp`. Each entry is `{ name, pattern, handler }`:

```cpp
{"enum declaration",   {P::anyOf({"enum","bnum"}), TYPE_NAME}, &Self::processEnum},
{"class declaration",  {"class", TYPE_NAME},                   &Self::processClass},
{"routine declaration",{TYPE_NAME, NEW_NAME, "("},             &Self::processRoutine},
{"variable declaration",{TYPE_NAME, NEW_NAME, "="},            &Self::processVariable},
…
```

Pattern elements can be:
- A literal keyword (string).
- A token-type predicate (`TYPE_NAME`, `NEW_NAME` — defined in `bglParser.cpp:initGrammarTable`).
- `P::anyOf({...})` — alternation.
- `P::semantic(lambda, "description")` — arbitrary predicate, used for namespaced type paths.

**Dispatch entry point**: `processStatementDispatch(token, abstractObject&)` in `bglParser.cpp`. For each statement:

1. Parse leading qualifiers (`replace`, `extern`, `emitter`, `static`, …) into a `Qualifiers` struct.
2. Handle qualifier-dependent specials (emitter classes, `extend`).
3. Resolve namespace-scoped type paths (`bgl.glulx.window` → flat type token).
4. Resolve `#using`-imported aliases.
5. Run `matchGrammar(tok)` — first table entry whose pattern matches wins.
6. Call the matched handler — `bool (bglParser::*)(vector<token>&, Qualifiers&, abstractObject&)`.

**Handler return**: `true` means "block ended" (e.g. matched a `}`); `false` means "keep parsing this block".

If no rule matches at global scope, the parser emits a targeted error — for example, `"Missing type before identifier 'X'"` when an identifier appears in a position that looks like a variable declaration but no type precedes it.

---

## The emitter

Triggered by `writeFile()` in `beguiler.cpp`, which calls `emitter.emit(languageService.globals)`.

Emission is multi-pass over the same data:

1. **ICL directives** (`emitICL`) — `!% -G` / `!% -v5` etc. for the target VM, `!% -E1` for error format, and `!% ++include_path=...` for each search path. ICL lines are read by the I6 compiler before parsing the source.
2. **Settings-driven I6 directives** (`emitSettingsConstants`) — `Serial "..."`, `Release N`, and the Treaty of Babel `UUID_ARRAY` for IFID. **Note**: story title, headline, author etc. are *not* auto-emitted here. They're declared in Beguile source against `#beguilerSettings.title` / `.headline` / `.author` (see the comment at `i6Emitter.cpp:641` for the canonical pattern).
3. **Scratch globals** — temp slots for ternary, switch, try/catch.
4. **Z-machine spill setup** (Z3/Z5/Z8 only) — frame pool array and excess-parameter globals.
5. **`#emitfirst` blocks** — raw I6 placed before `bglInit`.
6. **`bglInit` synthesis** — wraps `#startup` blocks and `globalInits` into a single routine that runs once at game start.
7. **AST walk** — iterate `globals` in source order; for each `typeDef*` dispatch to `emitClass`, `emitObject`, `emitGlobal`, `emitFunction`, `emitEnum`, etc. Class definitions are lazily emitted just-before-first-use.
8. **`#emitlast` blocks** — raw I6 placed at the end.

Emitter conventions worth knowing:

- **`$self` substitution**: inside an emitter routine body, `$self` is replaced with the receiver expression text.
- **`$paramName`**: replaced with the corresponding argument's text.
- **`$prop`**: replaced with the property name (for property-aware emitters).
- **`$target`**: replaced with the assignment-LHS text — used so an emitter body can do its own store rather than letting the caller wrap it in `LHS = ...;`.
- **`#bgl{...}`** inside a raw I6 body: switches back to Beguile parsing for the contents, then back to raw I6.

Identifier-casing convention is enforced earlier in the pipeline (lexer + parser), not here. The emitter writes lowercased `name` fields for identifiers — see *Identifier casing* below for the full story.

---

## Identifier casing

Beguile is case-insensitive. The lexer at [`fileLexer.cpp:639`](../fileLexer.cpp#L639) lowercases every token's `value` field before handing it to the parser, so `Glulx`, `glulx`, and `GLULX` all match the same identifier in the symbol table.

The pre-lowercase form is preserved as `originalValue` on the token. It's used by the **parser** (not the emitter) for two purposes:

1. **Error messages and IDE diagnostics** — so the user sees their original spelling reported back. Most AST nodes (`functionDef`, `classDef`, `enumDef`, `paramDef`, `variableDeclaration`, `enumValueDef`) carry a `displayName` field set from `originalValue` at parse time. Helper [`typeDef::dName()`](../typeDef.h#L31) returns `displayName` when set, else falls back to `name`.

2. **A few case-sensitive identifier categories** — verb / grammar names (used as I6 action constants like `##Take`), `#include` and `#using` path segments, and similar where I6 cares about the original spelling.

The **emitter** writes the lowercased `name` field for ordinary identifiers, plus the mangled `i6name` when set (e.g. operator routines whose Beguile names aren't valid I6 identifiers — `+` → `_opadd`, `=` → `_opeq`). It uses `displayName` only inside a debug/symbol-table comment ([`i6Emitter.cpp:587`](../i6Emitter.cpp#L587)) — never in actual generated I6 source.

If you're writing a new AST node and you want the user to see "Camera" rather than "camera" in diagnostics, set its `displayName` from `nameToken.originalValue` at parse time. If you want it lowercased everywhere (the common case), `name` alone is fine.

---

## The compile-context stack

Defined in `bglParser.h` as `deque<eCompileContext> compileContextStack`. The enum is in `typeDef.h`:

```
noContext   // before parsing begins
global      // top-level declarations
objectDef   // inside an object body
codeBlock   // inside a routine, if-block, while-body, etc.
```

Maintained by `openCompileContext()` / `closeCompileContext()`. The current context is checked by handlers to validate language rules — e.g. `extern` is only legal at `global`, `static` only inside a class.

Three companion fields track *which* class/object/function we're currently inside:

| Field | Meaning |
|-------|---------|
| `currentClass` | The `classDef*` whose body we're parsing (or `null`). |
| `currentObject` | The `objectDef*` whose body we're parsing (or `null`). |
| `currentFunc` | The outermost enclosing `functionDef*` — does **not** change for nested if/for/while; lambdas are handled separately. |

These drive identifier resolution (`self.x`, inherited members, etc.) — see `NAME_RESOLUTION.md`.

There's also `activeBlockStack` (vector of `statementBlock*`) — pushed when each new code block opens, used so identifier resolution can walk ancestor block locals as well as the current block's.

---

## Output artifacts

After a successful run:

| File | When written | Contains |
|------|--------------|----------|
| `<source>.transpiled.inf` | Always | Inform 6 source produced from the AST. |
| `<output>.ulx` / `.z3` / `.z5` / `.z8` | When I6 invocation succeeds | Bytecode for the chosen target. |
| `<source>.bgldbg` | With `--debug` | Source-map + AST metadata for the IDE/debugger. |
| `<output>.gblorb` / `.zblorb` | When blorb is enabled | Bytecode + cover art + iFiction metadata + game data. |
| `_blorbAssets.bgl` | When blorb is enabled | Generated enum file enumerating discovered assets; regenerated each run. |

The `.transpiled.inf` is preserved deliberately so you can read it. When something looks wrong in compiled output, this is usually the first place to check.

---

## Where to start when something breaks

A practical decision tree:

- **Parse error you don't understand** → read the message; check `processStatementDispatch` and the relevant `processXxx` handler; if the diagnostic is generic, consider adding a more targeted one for the specific shape that triggered it.
- **Identifier resolves wrong** → see `NAME_RESOLUTION.md`. Look at `resolveIdentifierType`, `qualifyIdentifier`, `resolvePathType`.
- **Wrong I6 emitted** → diff the `.transpiled.inf` against expected; trace the relevant `emitXxx` function; check that the AST node carries the data you expect (the parser builds it, the emitter reads it).
- **I6 errors that point to wrong lines** → the `sourceMap` is the suspect; `emitRawTextWithSourceMap` is where remapping is set up.
- **Z-machine-only failure** → spill mechanism (locals overflow). Check `funcNeedsSpill`, `buildSpillMap`, `_bglXPN` globals.
- **"Type X has no methods" or similar dispatch error** → see `NAME_RESOLUTION.md`; the dispatch sites in `bglParser.cpp` around expression and statement method calls handle both `classDef` and `objectDef` receivers.

