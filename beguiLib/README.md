# beguiLib

The Beguile files that ship with the compiler. The language extensions sit at the root and are included by
name (`#include <array>`); the rest are included by folder and name (`#include <bindings/i6StandardLibrary>`).
`core/` is never included by hand.

| Folder | What it holds | Included |
|---|---|---|
| `core/` | The Beguile Language Runtime (BLR): the language's types and the VMs' mechanics, exposed as `bgl.*` namespace objects | always |
| (root) | Opt-in language extensions: `<array>`, `<string>` (the earlier one archived as `<archive/string>`), `<buf>`, `<linq>`, `<ui>`, `<glulxWindow>`, `<glulxImage>` | by `#include` |
| `bindings/` | Bindings to the Inform 6 foundational libraries: `<bindings/i6StandardLibrary>`, `<bindings/punyInform>` | by `#include` |
| `extensions/general/` | Official Beguile extensions that work with any library | by `#include` |
| `extensions/stdlib/` | Official Beguile extensions to the Inform 6 standard library | by `#include` |
| `extensions/puny/` | Official Beguile extensions to PunyInform | by `#include` |
| `libraries/` | Beguile foundational libraries, which replace an Inform 6 one: `<libraries/bglStdLib>` | by `#include` |

A folder depends only on the folders above it in this table, with two exceptions:

- An extension for one library depends on that library's binding.
- A foundational library depends on no binding.

`core/` refers to no library.

The include lookup searches the whole tree, so names must be unique across all folders.

Not shipped here:

- **Bindings to orLibrary** live with orLibrary (`orLibraryI6/src/bglBindings/`).
- **Third-party extensions** are the author's own.
- **Personal extensions** are reached through a game's `includePaths`. They may later be published as official extensions or to orLibrary.
