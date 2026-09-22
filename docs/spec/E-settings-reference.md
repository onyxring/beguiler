# Appendix E Settings Reference

Every property accepted in a `#beguilerSettings` block (§15.1), alphabetically. *Precedence* is how
repeated assignments combine: **first** (first-writer-wins), **last** (last-writer-wins) or
**additive**; a command-line value always counts as written first. *Readable* marks the properties
that `#beguilerSettings.prop` may read (§15.7).

| Property | Type | Default | Precedence | CLI equivalent | Readable | Entry |
| --- | --- | --- | --- | --- | --- | --- |
| `author` | string | `""` | first | — | yes | §15.5 |
| `autoInitialize` | bool | `true` | last | — | no | §15.4 |
| `beguiLibPath` | string | `"beguiLib"` | first | `-lib=<dir>` | no | §15.2 |
| `blorbAssetPath` | string | `"assets"` | first | — | yes | §15.6 |
| `description` | string | `""` | first | — | yes | §15.5 |
| `economy` | bool | `false` | last | — | no | §15.3 |
| `errorFormat` | `eErrorFormat` | `E1` | first | `-E1`, `-E2` | no | §15.3 |
| `firstPublished` | string | `""` | first | — | yes | §15.5 |
| `forgiveness` | string | `""` | first | — | yes | §15.5 |
| `forInScratchSize` | int | `31` | first | — | yes | §15.4 |
| `framePoolSize` | int | `64` | first | — | yes | §15.4 |
| `generateBlorb` | bool | `false` | last | — | no | §15.6 |
| `genre` | string | `""` | first | — | yes | §15.5 |
| `headline` | string | `""` | first | — | yes | §15.5 |
| `ifid` | string | `""` (generated) | first | — | yes | §15.5 |
| `includePaths` | string | none | additive | `-includepaths=<dirs>` | no | §15.2 |
| `informName` | string | `"inform"` | first | `-inform=<name>` | yes | §15.2 |
| `informPath` | string | none | first | `-inform=<path>` | no | §15.2 |
| `language` | string | `""` | first | — | yes | §15.5 |
| `linqScratchSize` | int | `32` | first | — | yes | §15.4 |
| `omitUnusedRoutines` | bool | `true` | last | — | no | §15.3 |
| `outputPath` | string | `"output"` | first | `-o <dir>` | yes | §15.3 |
| `release` | int | `0` | first (0 = unset) | — | yes | §15.3 |
| `rewritePaths` | bool | `true` | first | — | no | §15.4 |
| `serial` | string | `""` | first | — | yes | §15.3 |
| `series` | string | `""` | first | — | yes | §15.5 |
| `seriesNumber` | int | `0` | first (0 = unset) | — | yes | §15.5 |
| `target` | `eTarget` | `Glulx` | first | `-G`, `-z5`, `-z8` | yes | §15.3 |
| `title` | string | `""` | first | — | yes | §15.5 |

**Enum values.** `eTarget`: `Glulx`, `Z5`, `Z8`. `eErrorFormat`: `E1`, `E2`.

**Validation.** `serial` must be exactly six digits; the three `int` sizes must be at least 1;
`target` must be an `eTarget` member; a regular-string `includePaths` entry must name an existing
directory.
