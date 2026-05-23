> NOTE: This document is only just started.  I'll develop it over time as Beguile develops.

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
Beguile's syntax which is closely aligned with other C-like languages.  If you've worked with Typescript, C#, or C++, you'll be comfortable with the language, but reading Beguile code will *also* be familiar in a different way: Beguile is closely related to Inform 6 and you'll see Inform's fingerprints all over it.  Reading Beguile will feel like you are reading a variant of I6.

### Design Goals
A handful of principals guided the design of the language:

* **Harmony with I6** - Beguile provides *rich interop* capabilities with Inform 6, enabling you to use the foundational libraries and I6 extensions you are familiar with. 

    > ***YES**: You can use Beguile with Puny Inform or the Inform Standard Library, without modifying those libraries.*

* **Syntactic consistency** - The language adheres to an abiding, object-oriented paradigm, and eschews the addition of new idioms to cover language edge cases. Sometimes the cost of this semantic clarity is slightly longer code.  In others, the Beguile code is shorter. 

* **Type safety** - Beguile is a type-safe language. This is one of the most material deviations from I6, which opts for the type-free paradigm. 
    
* **Expandible type system** - Because you can't have type safety without types, Beguile provides a set of these, and let's you create your own. 


### Pipeline
The same Inform 6 compilation pipeline you are familiar with remains with only minor additions at either end:
```
    <idea>      ->  Author    ->  game.bgl
    game.bgl    ->  Beguiler  ->  game.inf
    game.inf    ->  Inform 6  ->  game.ulx (or .z3/.z5/.z8)
    game.ulx    ->  Beguiler  ->  game.gblorb (or .zblorb)
```
Beguiler orchestrates all compilation steps, making them transparent to users.  The golden path: A single run of Beguiler produces a playable game, blorbified at your discretion.  Managing I6 becomes Beguile's responsibility, unless you choose to do it yourself.

## Anatomy of a Game
Arguably, the fastest way to familiarize yourself with a language is to review a working example.  The canonical Cloak of Darkness game is perfect for this since it is well-known and there are Inform 6 versions readily available for comparison against.  The following is a straight-forward port of that work, ported to Beguile and using Puny Inform.

   > ***Note**: The version of Cloak Of Darkness which I started with worked for both the I6 Standard Library and Puny Inform.  In the interest of clarity, I've stripped down the cross-library support.  Additionally, the code reflects decisions which favor the explanation, rather than the **best** way of doing things.*    


### Cloak of Darkness (Beguile Port)

Here we  present the whole file first, for reference, then step over it bit-by-bit in the following sections to compare and contrast with I6...

```


```
### The #beguileSettings Block
```
/*============================================================================
  Cloak of Darkness - a simple demonstration of Interactive Fiction
    This port to Beguile was translated by Jim Fisher using the Inform
    version (written by Roger Firth) as a template.
  ============================================================================*/
#beguilerSettings { 
    includePaths="../../inform6/puny/lib"; //path to the Standard Library
    target=Z5;     
    title="Cloak of Darkness"; 
    headline = "^A basic IF demonstration.^";
}

#includeI6 "globals"
#include <bindings/punyInform>

const int MANUAL_PRONOUNS=0;
const int MAX_SCORE = 2;

alias class room for object { 
    string short_name; 
    string description; 
    room s_to; 
    room e_to; 
    room w_to; 
    room n_to; 
    attributeList attributes; 
}
room foyer{
    short_name="Foyer of the Opera House";
    description = "You are standing in a spacious hall, splendidly decorated 
        in red and gold, with glittering chandeliers overhead. The entrance 
        from the street is to the north, and there are doorways 
        south and west.";
    s_to = bar;
    w_to = cloakroom;
    string n_to = "You've only just arrived, and besides, the weather outside 
        seems to be getting worse."; 
    attributes = {light}; 
}
room cloakroom{
    short_name="Cloakroom";
    description="The walls of this small room were clearly once lined with 
        hooks, though now only one remains. The exit is a door to the east.";
    e_to=foyer;
    attributes = {light};
}
room bar{
    short_name="Foyer bar";
    description="The bar, much rougher than you'd have guessed after the 
        opulence of the foyer to the north, is completely empty. There seems to 
        be some sort of message scrawled in the sawdust on the floor.";
    n_to=foyer;
    bool before(){
        switch(action){
            case Go: if (self.hasnt(light) && (noun != n_obj)) {     
                        message.number += 2;     
                        print("Blundering around in the dark isn't a good idea!"); 
                        rtrue; 
                    }
            case Going: rfalse;
            default: if (bar.hasnt(light)) {
                        message.number++;                             
                        print("In the dark? You could easily disturb something!");
                        rtrue;
                    }
        }
        rfalse;
    }
}
object cloak {
    object parent=selfobj;
    string short_name = "velvet cloak";
    string description = "A handsome cloak, of velvet trimmed with satin, and 
        slightly spattered with raindrops. Its blackness is so deep that it
        almost seems to suck light from the room.";
    array<dictionaryWord> name = {.handsome, .dark, .black, .velvet, .satin, .cloak}; 
    bool before() {
        switch(action){
            case Drop, PutOn:
                if (location == cloakroom) {
                    bar.give(light);
                    if (action == PutOn) {
                        if (cloak.has(general)) {
                            cloak.ungive(general);
                            score++; 
                        }
                    }
                } else {
                    print("This isn't the best place to leave a smart cloak lying 
                        around.^");
                    rtrue;
                }
        }
        rfalse;
    }
    bool after() {          
        if(action==Take) bar.ungive(light);
        rfalse;
    }
    attributes = {clothing, general, worn}; 
}
object hook { 
    string short_name = "small brass hook";
    object parent = cloakroom;
    array<dictionaryWord> name = {.small, .brass, .hook, .peg};
    bool description() {
            print($"It's just a small brass hook, {cloak.parent == hook ? "with a 
                cloak hanging on it.\n" : "screwed to the wall.\n"}"); 
        rtrue;
    }
    attributes = {scenery, supporter}; 
}
object message {
    string short_name = "scrawled message";
    object parent = bar;
    array<dictionaryWord> name = {.message, .sawdust, .floor};
    bool description() {
        if (self.number < 2) {
            score++; 
            print("The message, neatly marked in the sawdust, reads...");
            deadflag = 2; 
        } else {
            deadflag = 3;
            print("The message has been carelessly trampled, making it 
                difficult to read. You can just distinguish the words...");
        }
        rtrue;
    }
    int number = 0;
    attributes = {scenery}; 
}
bool Initialise(){
    location = foyer;
    print("\n\nHurrying through the rainswept November night, you're glad
        to see the bright lights of the Opera House. It's surprising that 
        there aren't more people about but, hey, what do you expect in a 
        cheap demo game...?\n\n");
    rtrue;
}
void DeathMessage(){ print("You have lost"); }
#includeI6 "grammar"
extend PutOn { grammar += {.hang, HELD, .on, OBJ}; }
```