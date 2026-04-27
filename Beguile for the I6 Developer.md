# Beguile for the I6 Developer 

This document serves as a introduction to the Beguile programming language for developers already experienced with the I6 language.  

## Key concepts
### Terms: Beguile, Beguiler, Beguilex
First, some clarity of terms:
* **Beguile** refers to the language itself, but when used in a general context, the term may also include the tools which make Beguile real.
* **Beguiler** is the compiler for the Beguile language.  It also serves double duty as a language server for Beguilex.
* **Beguilex** is the Beguile extension (for VS Code) which enables syntax colorization, code-completion, and runtime debugging.

    > ***YES**: Beguile supports runtime debugging of your resulting story file.*

### Language Affinity 
Beguile is closely related to Inform 6, but also uses a syntax inspired by C-like languages.  If you've worked with Typescript, C#, or C++, you'll be comfortable with Beguile; but reading Beguile code will also feel familiar in a different way, as though you are reading I6.  Inform's fingerprints all over the Beguile language.

### Design Goals
A handful of principals guided the design of the language:

* **Harmony with I6** - Beguile provides *rich interop* capabilities with Inform 6, enabling you to use the foundational libraries and I6 extensions you are familiar with. 

    > ***YES**: You can use Beguile with Puny Inform or the Inform Standard Library, without modifying those libraries.*

* **Syntactic consistency** - The language adheres to an abiding, object-oriented paradigm, and eschews the addition of new syntax constructs to cover language edge cases. Sometimes the cost of this semantic clarity is slightly longer code.  In others, the Beguile code is shorter. 

* **Type safety** - Beguile is a type-safe language. This is one of the most material deviations from I6, which opts for the type-free paradigm. 
    
* **Expandible type system** - Because you can't have type safety without types, Beguile provides a set of these, and let's you create your own. 


### Pipeline
The same Inform 6 compilation pipeline you are familiar with remains with only minor changes:
```
    <idea>      ->  Author    ->  game.bgl
    game.bgl    ->  Beguiler  ->  game.inf
    game.inf    ->  Inform 6  ->  game.ulx (or .z3/.z5/.z8)
    game.ulx    ->  Beguiler  ->  game.gblorb (or .zblorb)
```
Beguiler orchestrates all compilation steps, making them transparent to users.  The golden path: A single run of Beguiler produces a playable game, blorbified at your discretion.  Managing I6 becomes Beguile's responsibility, unless you choose to do it yourself.

## Anatomy of a Game
Arguably, the fastest way to familiarize yourself with a language is to review a working example.  The following is a simple, two-room game written in Beguile. We'll present the whole file first, for reference, then step over it bit-by-bit in the following sections to compare and contrast with I6.

### A Two Room Example
The following is a short, two-room example, written in Beguile with Puny Inform.
```
/* Blah: a Two-Room Example.

*/
#beguileSettings{ 
    target=Z5;
    includePath="../"; 
}

#includeI6 "parser"
#include <bindings/punyInform>

object snackRoom{
    string short_name = "Snack Room";
    object w_to = lobby;
    string description = "blah";
}
object lobby{
    string short_name = "Lobby";
    object w_to = lobby;
    string description = "blah";
}


```