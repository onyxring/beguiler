# Introduction to Beguile

<!-- toc -->
- [What Beguile Is](#what-beguile-is)
- [Design Goals](#design-goals)
- [Relationship to Inform 6](#relationship-to-inform-6)
<!-- /toc -->

## What Beguile Is

Beguile is a statically typed, compiled language for writing interactive fiction.  Its structured syntax transpiles to Inform 6 and, from there, to a Z-machine or Glulx story file.

Although it borrows features from general-purpose languages, Beguile is *not* one.  Its features are shaped entirely by the needs of interactive fiction, and by the constraints of the virtual machines its programs run on.

## Design Goals

A number of principles framed how the language was designed:

- **Familiar syntax.**  The syntax draws on C++, C#, and TypeScript; if you are comfortable with any of them, you will find it intuitive.
- **A consistent syntax.**  Every construct follows a few core rules (e.g., declarations live in member bodies, statements end in `;`, types come before names).  The language avoids micro-grammars for special features, in favor of general-purpose constructs which work the same way everywhere:  a verb is an object, and its grammar is simply a member initialized with an ordinary list (§13).
- **Strong typing.**  Every variable, parameter, and return value carries a declared type.  Type mismatches will be caught when you compile, not when your game is running.
- **Transparency.**  The generated Inform 6 is readable and maps closely to the Beguile source.  If you know Inform 6, you can easily inspect the output, or add to it.
- **Extensibility through emitters.**  Performance-sensitive or platform-specific operations can be expressed as **emitters**:  inline fragments of Inform 6, substituted where they are called (§7).  Library authors get full control over the generated code without giving up type safety on the Beguile side.

## Relationship to Inform 6

Beguile is built on top of Inform 6; it is *not* a replacement for it.  It generates human-readable Inform 6 source; however, several features go a bit further and bridge the two languages:

- `extern` declarations let Beguile use the types, functions, attributes, and constants defined in Inform 6 without implementing them again.
- `#i6` islands place raw Inform 6 right alongside Beguile code.
- Emitters give you precise control over the Inform 6 that is generated.

When you need something Beguile doesn't expose, you can always drop down to Inform 6 through the above (§16).
