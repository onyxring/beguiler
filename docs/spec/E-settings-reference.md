# Appendix E Settings Reference

The following table lists every property accepted in a `#beguilerSettings` block (§15.1), alphabetically, including the two which `<string>` declares (§15.9).  *Precedence* is how repeated assignments combine:  **first** (first-writer-wins) or **additive**; a command-line value always counts as written first.  *Readable* marks the properties which `#beguilerSettings.prop` can read (§15.7).

| Property | Type | Default | Precedence | CLI equivalent | Readable | Entry |
| --- | --- | --- | --- | --- | --- | --- |
| `author` | string | `""` | first | - | yes | §15.5 |
| `autoInitialize` | bool | `true` | first | - | yes | §15.4 |
| `beguiLibPath` | string | `"beguiLib"` | first | `-lib=DIR` | yes | §15.2 |
| `blorbAssetPath` | string | `"assets"` | first | - | yes | §15.6 |
| `description` | string | `""` | first | - | yes | §15.5 |
| `economy` | bool | `false` | first | - | yes | §15.3 |
| `errorFormat` | `eErrorFormat` | `E1` | first | `-E1`, `-E2` | yes | §15.3 |
| `filterVerbSplitWarnings` | bool | `true` | first | `-filterVerbSplitWarnings=true\|false` | yes | §15.3 |
| `firstPublished` | string | `""` | first | - | yes | §15.5 |
| `forgiveness` | string | `""` | first | - | yes | §15.5 |
| `framePoolSize` | int | sized from the program, at least `64` | first | - | yes | §15.4 |
| `generateBlorb` | bool | `false` | first | - | yes | §15.6 |
| `genre` | string | `""` | first | - | yes | §15.5 |
| `headline` | string | `""` | first | - | yes | §15.5 |
| `heapSize` | int | `1536` (Z-machine), `16384` (Glulx) (also for 0) | first | - | yes | §15.4 |
| `ifid` | string | `""` (generated) | first | - | yes | §15.5 |
| `includePaths` | string | none | additive | `-includepaths=DIRS` | yes | §15.2 |
| `informName` | string | `"inform"` | first | `-inform=<name>` | yes | §15.2 |
| `informPath` | string | none | first | `-inform=PATH` | yes | §15.2 |
| `language` | string | `""` | first | - | yes | §15.5 |
| `omitUnusedRoutines` | bool | `true` | first | - | yes | §15.3 |
| `outputPath` | string | `"output"` | first | `-o DIR` | yes | §15.3 |
| `release` | int | `0` | first (0 = unset) | - | yes | §15.3 |
| `rewritePaths` | bool | `true` | first | - | yes | §15.3 |
| `serial` | string | `""` | first | - | yes | §15.3 |
| `series` | string | `""` | first | - | yes | §15.5 |
| `seriesNumber` | int | `0` | first (0 = unset) | - | yes | §15.5 |
| `stringCaptureSize` | int | `512` (also for 0); declared by `<string>` | first | - | yes | §22.3 |
| `stringHeapSize` | int | `5120` (also for 0); declared by `<string>` | first | - | yes | §22.3 |
| `target` | `eTarget` | `Glulx` | first | `-G`, `-z5`, `-z8` | yes | §15.3 |
| `title` | string | `""` | first | - | yes | §15.5 |
| `worldBufSize` | int | `128` | first | - | yes | §15.4 |

**Enum values.**  `eTarget`:  `Glulx`, `Z5`, `Z8`.  `eErrorFormat`:  `E1`, `E2`.

**Validation.**  `serial` must be exactly six digits; `framePoolSize` and `worldBufSize` must be at least 1, and `heapSize` 0 or at least 64; `target` must be an `eTarget` member; a regular-string `includePaths` entry must name an existing directory.
