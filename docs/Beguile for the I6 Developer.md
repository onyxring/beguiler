> NOTE: This document is only just started.  I'll develop it over time as Beguile develops.

# Beguile for the I6 Developer 

This document serves as an introduction to the Beguile programming language for developers already experienced with the I6 language. 

## Key concepts
### Terms: Beguile, Beguiler, Beguilex
First, some clarity of terms:
* **Beguile** refers to the language itself, but when used in a general context, the term may also include the tools which make Beguile real.
* **Beguiler** is the compiler for the Beguile language.  It also serves double duty as a language server for Beguilex.
* **Beguilex** is the Beguile extension (for VS Code) which enables syntax colorization, code-completion, and runtime debugging.

    > ***YES**: Beguile supports runtime debugging of your resulting story file.*

### Language Affinity 
Beguile's syntax is closely aligned with other C-like languages.  If you've worked with Typescript, C#, or C++, you'll be comfortable with the language, but reading Beguile code will *also* be familiar in a different way: Beguile is closely related to Inform 6 and you'll see Inform's fingerprints all over it.  Reading Beguile will feel like you are reading a variant of I6.

### Design Goals
A handful of principles guided the design of the language:

* **Harmony with I6** - Beguile provides *rich interop* capabilities with Inform 6, enabling you to use the foundational libraries and I6 extensions you are familiar with. 

    > ***YES**: You can use Beguile with Puny Inform or the Inform Standard Library, without modifying those libraries.*

* **Syntactic Consistency** - The language adheres to an object-oriented paradigm, and eschews the addition of new idioms to cover language edge cases. Sometimes the cost of this semantic clarity is slightly longer code.  In others, the Beguile code is shorter. 

* **Ease of Use** - Abiding by the "Syntactic Consistency" principle above can sometimes translate into boilerplate code.  Although the language tries to avoid the special-case syntaxes and keywords used in some languages to abbreviate things, it still seeks to balance expressiveness with conciseness.  You'll see this principle manifest in several ways. 

* **Type safety** - Beguile is a type-safe language. I6 leaves typing to the author's discipline; Beguile encodes that discipline in the compiler. This is one of the most material differences between the two. 
    
* **Expandable type system** - Because you can't have type safety without types, Beguile provides a set of these, and lets you create your own. 

### Pipeline
The same Inform 6 compilation pipeline you are familiar with remains with only minor additions at either end:
```
    <idea>      ->  Author    ->  game.bgl
    game.bgl    ->  Beguiler  ->  game.inf
    game.inf    ->  Inform 6  ->  game.ulx (or .z3/.z5/.z8)
    game.ulx    ->  Beguiler  ->  game.gblorb (or .zblorb)
```
Beguiler orchestrates all compilation steps, making them transparent to users — including the final blorb-packaging step, which Beguiler performs via its built-in blorb tool (no separate utility required).  The golden path: A single run of Beguiler produces a playable game, blorbified at your discretion.  Managing I6 becomes Beguile's responsibility, unless you choose to do it yourself, making the effective pipeline:
```
    <idea>      ->  Author    ->  game.bgl
    game.bgl    ->  Beguiler  ->  game.gblorb (or .zblorb)
```
> ***Note**: actually, the full name of the intermediate Inform file, listed above as `game.inf` for brevity, is actually `game.bgl.transpiled.inf`. Curious minds can look at it to see what `Beguiler` has produced. The compilation process leaves it in place for this reason, as well as to enable source-code debugging.*

## An Overview: Cloak of Darkness 
Arguably, the fastest way to familiarize yourself with a language is to review a working example.  The canonical Cloak of Darkness game is perfect for this since it is well-known and there are Inform 6 versions readily available for comparison against.  The following is a straight-forward port of that work, ported to Beguile and using Puny Inform.

   > ***Note**: The version of Cloak Of Darkness which I started with worked for both the I6 Standard Library and Puny Inform.  In the interest of clarity, I've stripped down the cross-library support.  Additionally, the code reflects decisions which favor the explanation, rather than the **best** way of doing things.*    

Compiling the file is a single command:
```
beguiler cloak.bgl
```
Beguiler reads the `target=Z5` setting from the `#beguilerSettings` block and produces `cloak.z5`, which you can load in any Z-Machine interpreter.

Here we  present the whole file first, for reference...

```
/*============================================================================
  Cloak of Darkness - a simple demonstration of Interactive Fiction
    This port to Beguile translated by Jim Fisher from the Inform version 
    which was written by Roger Firth as a template.
  ============================================================================*/
//settings for the compiler...
#beguilerSettings { 
    includePaths="../../inform6/puny/lib"; 
    target=Z5;      
    title="Cloak of Darkness"; 
    headline = "\nA basic IF demonstration.\n";
}

const int MAX_SCORE = 2;
const object INITIAL_LOCATION_VALUE = foyer;

void DeathMessage(){ 
    print("You have lost"); 
}

#includeI6 "globals"
#includeI6 "puny"
#include <bindings/punyInform>

object cloakroom{
    string short_name="Cloakroom";
    string description="The walls of this small room were clearly once lined 
        with hooks, though now only one remains. The exit is a door to 
        the east.";
    object e_to=foyer;
    attributeList attributes = {light, general};
}
extend class object { 
    string short_name; 
    string description; 
    object s_to; 
    object e_to; 
    object w_to; 
    object n_to;  
}
object foyer{
    short_name="Foyer of the Opera House";
    description = "You are standing in a spacious hall, splendidly decorated
        in red and gold, with glittering chandeliers overhead. The entrance 
        from the street is to the north, and there are doorways south and west.";
    s_to = bar;
    w_to = cloakroom;
    string n_to = "You've only just arrived, and besides, the weather outside 
        seems to be getting worse."; 
    attributes = {light}; 
}
object bar{
    short_name="Foyer bar";
    description="The bar, much rougher than you'd have guessed after the 
        opulence of the foyer to the north, is completely empty. There seems 
        to be some sort of message scrawled in the sawdust on the floor.";
    n_to=foyer;
    bool before(){
        switch(action){
            case Look, Inv, Going: rfalse;
            case Go: if (attributes.hasnt(light) && (selected_direction != (property)n_to) ) {     
                        message.number += 2;     
                        rtrue("Blundering around in the dark isn't a good idea!"); 
                    }
            default: if (bar.hasnt(light)) {
                        message.number++;                             
                        rtrue("In the dark? You could easily disturb something!");
                    }
        }
        rfalse;
    }
    attributes = {!light};
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
                    rtrue("This isn't the best place to leave a smart cloak lying 
                        around.^");
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
        rtrue($"It's just a small brass hook, {cloak.parent == hook ? "with a cloak hanging on it.^" : "screwed to the wall.^"}");         
    }
    attributes = {scenery, supporter}; 
}
object message {
    string short_name = "scrawled message";
    object parent = bar;
    array<dictionaryWord> name = {.message, .sawdust, .floor};
    bool description() {
        if (number < 2) {
            score++; 
            print("The message, neatly marked in the sawdust, reads...");
            deadflag = 2; 
        } else {
            print("The message has been carelessly trampled, making it difficult 
                to read. You can just distinguish the words...");
            deadflag = 3;
        }
        rtrue;
    }
    int number = 0;
    attributes = {scenery}; 
}
bool Initialise(){
    rfalse("^^Hurrying through the rainswept November night, you're glad to 
        see the bright lights of the Opera House. It's surprising that there 
        aren't more people about but, hey, what do you expect in a cheap 
        demo game...?^^^");
}
extend PutOn { grammar = {{.hang, HELD, .on, NOUN}}; }
```
Now we'll step over the above code, bit-by-bit, to compare and contrast with I6...

### Comments
Beguile uses C-like comments.  Multi-line comments begin with `/*` and continue until `*/`:

```
/*============================================================================
  Cloak of Darkness - a simple demonstration of Interactive Fiction
    This port to Beguile translated by Jim Fisher from the Inform version 
    which was written by Roger Firth as a template.
  ============================================================================*/
```
Single line comments begin with `//` and continue through the end of the line:
```
//settings for the compiler...
```
### The #beguilerSettings Directive
The `#beguilerSettings` directive is a block of compile-time settings.  It is similar to I6's ICL settings but does quite a bit more. All settings are declared as members of the directive block:
```
#beguilerSettings { 
    includePaths="../../inform6/puny/lib"; 
    target=Z5;      
    title="Cloak of Darkness"; 
    headline = "\nA basic IF demonstration.\n";
}
```
> ***Notice** that compile-time directives begin with `#`.  Beguile provides many directives other than `#beguilerSettings`.*    

Unlike Inform's ICL comments, which must begin on line 1 (if defined at all) and always end at the first line which does not start with `!%`, the `#beguilerSettings` block may appear anywhere in your Beguile source, in any file which is compiled, *any number of times*.  

Generally, settings retain the first value assigned.  Attempts to reassign values to settings are silently ignored — this is intentional, so that a command-line switch (which always wins, even over the first block) can override a value without producing spurious errors. There is one exception to this rule...  `includePaths`.

#### 1. `includePaths`
```
includePaths="../../inform6/puny/lib, ../../orLibraryI6";
```

Unlike other settings, Beguiler aggregates all definitions of `includePaths`, deduplicates these, and emits them into I6's ICL header in the order they were first declared. The above line generates something like the following I6:

```
!% ++include_path=/Users/jim/projects/IF-Projects/orLibraryI6
!% ++include_path=/Users/jim/projects/IF-Projects/inform6/puny/lib
```

There are some things to note about the way Beguile processes paths.
- You can specify paths in a comma-separated format, just like you can in I6; however, Beguile emits these as individual lines. 
- The same path, specified more than once, is deduplicated, and only emitted once.
- The order paths are emitted is in reverse of the order they are declared. This is due to how I6 manages the `++include_path` ICL setting, which is to prepend the path to its running list of paths. 
  > ***In other words**: The order you write paths in Beguile is the order I6 searches them. The reverse emission just makes this work as expected.*
  
- Beguile converts relative paths to absolute paths.  This keeps things unambiguous and guards against any "relative from what directory" questions that sometimes arise.  
- Beguile converts path delimiters to whatever your current OS expects, so if you are compiling source written on Windows (e.g., `..\..\folder`), Beguile will still process this on a Linux box.
- If the path you are specifying cannot be found, Beguile will throw an error and tell you what folder it was looking for:

    ```
    ERROR: beguilerSettings includePaths entry '../../orLibraryI6a' does not resolve to an existing directory ('/Users/jim/projects/IF-Projects/orLibraryI6a'). Use @"..." to emit a literal path without filesystem validation.
    ```
- If you want to bypass all of Beguile's path intelligence, you can always specify a "raw" string by prefixing it with the `@` symbol.  This causes Beguile to forego any validation or path correction.  It will still emit each path on a separate line, however.
  
> ***Note**: The `includePaths` setting is not simply used by I6, it is used by Beguile as well.*

#### 2. `target`
```
target=Z5;     
```
This setting, `target`, specifies which virtual machine your game is to be compiled to.  Allowable values are contained within the `eTarget` enumeration, so you *can* specify them as `eTarget.Z5`, for example; however, specifying the `enum` type is optional. The four legal values are: `Z3`, `Z5`, `Z8`, and `Glulx`.    

#### 3. `title` & `headline`
```
title="Cloak of Darkness"; 
headline = "\nA basic IF demonstration.\n";
```

The last two settings contain the name of the story and its subtitle. These settings may seem redundant to I6's `story` and `headline` constants, but these represent the attributes used when packing your game into a `blorb` file.  In fact, we could *also* have declared them as constants, separately in our code later, but doing it in the `#beguilerSettings` block is best practice, since it allows us to specify these settings in one place and reference them when declaring the constants:

```
const string story = #beguilerSettings.title;  
const string headline = #beguilerSettings.headline;
```

> ***Note**: You usually don't actually have to do this last bit.  The `i6StandardLibrary` and `punyInform` bindings both include these so you don't have to (more on bindings later).*

Another point worth mentioning is the embedded `\n` in the `headline` setting.  Beguile borrows from C to support escape sequences and `\n` translates directly into a newline. I6 uses `^` for this purpose and, while the formal Beguile language itself doesn't give special meaning to that character, neither does it disallow it.  As a practical matter, we are more likely to write this in Beguile...

```
headline = "^A basic IF demonstration.^";
```

...which passes through as-is to Inform. 

#### 4. Other Settings
The `#beguilerSettings` directive supports ***many*** more settings than we've covered in this example. The full list is in the Language Specification; however, if you are using **Beguilex**, the Beguile extension for VS Code, typing `Ctl+SPACE` within a `#beguilerSettings` block will prompt you with a complete list to choose from.

### Constants
Beguile's syntax for constants is similar to I6's:
```
const int MAX_SCORE = 2;
const object INITIAL_LOCATION_VALUE = foyer;
```
If you peek at the transpiled `.inf` file, you'll see that emits to I6 as:
```
constant MAX_SCORE = 2;
constant INITIAL_LOCATION_VALUE = foyer;
```
Aside from the abbreviated keyword, notice that Beguile requires a ***data type***.  This is, perhaps, the most profound difference between Beguile and I6.  Beguile is a type-safe language.  The following will throw an error:

```
const int MAX_SCORE = "2";

ERROR: Cannot assign value of type 'stringLiteral' to variable of type 'int'
```

While ***I6*** happily accepts the following, assigning `MAX_SCORE` the *memory address* of the string `"2"`.
```
constant MAX_SCORE = "2";
```
> ***Note**: Of course, you can't have a type-safe language without types. Beguile distinguishes a lot of types and enables you to opt-in to more advanced types or to create your own.*

> ***Note**: It's also worth mentioning that, like I6, Beguile is case-*insensitive*, differing from C and C++ in this regard.  So `MAX_SCORE`, `Max_Score`, and `MaX_sCoRE` are synonymous.*

### Global Routines
Beguile uses the structure used by C-like languages for routines:
```
void DeathMessage(){ 
    print("You have lost"); 
}
```
The above emits to I6 as follows:
```
[DeathMessage;
    print (string)"You have lost"; 
];
```
There's quite a bit of substance to Beguile routines which will become more apparent later in this guide, but here are a few noteworthy things to take away from the above:

- Beguile routines always have a return type, even if that type means "No return type". That's the case here, where the return type is `void`. If you attempt to return a value from a `void` routine, Beguile will tell you that's not allowed. 
- Parameters are enclosed in parentheses.  There are no parameters to the `DeathMessage` routine, so the parenthetical is empty.
- The body of a routine is contained within braces; the routine name and parameters are always defined outside of the body.
- The name of a Beguile routine is usually emitted exactly to I6 as it is in Beguile.

    > ***Note**: There are a few features which, when used, cause this to not be true.  For example, Beguile supports signature overloading, allowing you to define two routines with the same name, but taking different parameters.  In this case, Beguiler employs a technique called "name mangling" which emits different I6 names entirely.*
>
- Notice the single statement in `DeathMessage`. In Beguile, `print` is a global routine, not a language construct.  This distinction has more nuance than it may seem at first glance.  We'll see some of this when we revisit `print` in a bit.

### `#includeI6`
The `#includeI6` directive emits the `#include "file"` directive to I6.

```
#includeI6 "globals"
#includeI6 "puny"
```
These expand to something like...
```
#include "/Users/jim/projects/IF-Projects/inform6/puny/lib/globals.h";
#include "/Users/jim/projects/IF-Projects/inform6/puny/lib/puny.h";
```
These are two library files used to include the Puny Inform library.  Notice that they are emitted with fully qualified paths.  Beguile resolves these by searching the paths specified in the `includePaths` setting of `#beguilerSettings` and throws an error if not found (it infers the file extension).  If a raw string is specified (e.g. `#includeI6 @"puny"`), it is passed as is to I6, without validation.

> ***Note**: Directives in Beguile do not end in `;`.  This is an I6 convention; however, extraneous `;` characters are ignored by Beguile, so specifying them out of muscle memory seldom raises an error.*

It's worth clarifying that Beguile does not process these files. Beguile is not an I6 compiler and can't parse I6 to know what objects and routines are available for use.  That's where Beguile "bindings" come into play (we'll cover these in a bit).

### `#include`
The `#include` directive tells Beguiler to include another file for processing.  Semantically, this is identical to the I6 version of the `#include` directive, except the Beguile version pulls in Beguile files and processes them before the I6 handoff.

It has two forms:

- `#include "file"` which doesn't appear in our example above, but this quoted form scans all paths defined by `includePaths` for the file specified. You can use this to break your works into individual source files, or to include extensions which you've downloaded.
  
- `#include <file>` which looks for the file in the Beguile Language Runtime (BLR), contained within the `beguiLib` folder.  This folder accompanies the `beguiler` binary and does not need to appear in the `includePaths` list.

> ***Note**: The BLR is a collection of types, emitters, and bindings which are used to translate the Beguile language into something the I6 compiler can accept.  It is part of the standard Beguiler install.*

Usually, when a piece of the BLR is pulled in, only the filename (sans the `.bgl` extension) is specified.  For example, you pull in Beguile's advanced string support with just `#include <string>`; however, Beguile differs from I6 in that it allows sub-folders to be specified...

```
#include <bindings/punyInform>
```
In this example, the file being included is the `punyInform` binding file, which sits in the BLR's `bindings` subfolder.  

> ***Note**: Searching sub-folders is something Beguile does even for the `#includeI6` directives. Beguile will look for the specified subfolder, relative to every instance of `includePaths` and pass the fully resolved path to Inform.*
### Binding Files
Since Beguile doesn't know how to parse I6, files pulled in using `#includeI6`, mean nothing to it.  Binding files bridge the gap between I6 and Beguile, making the I6 declarations - attributes, routines, objects, and variables - available to be referenced in Beguile. The `punyInform` binding is used when writing games using Puny Inform; there's also a binding file for the I6 Standard Library, `i6StandardLibrary` (pulled in with `#include <bindings/i6StandardLibrary>`). 

> ***Note**: Recall from the above that Beguile accepts either path delimiter, `/` or `\` regardless of OS.*
 
> ***Note**: Binding files are analogous to TypeScript's Type Declaration files.*

### Declaring Objects
Beguile uses a C-like syntax to declare objects...

```
object cloakroom{
    string short_name="Cloakroom";
    string description="The walls of this small room were clearly once lined 
        with hooks, though now only one remains. The exit is a door to 
        the east.";
    object e_to=foyer;
    attributeList attributes = {light, general};
}
```
> ***Note** in the above that, unlike C but just like I6, multi-line string literals are acceptable.*

The emission to I6 looks very similar:
```
object cloakroom
  with short_name "Cloakroom",
       description "The walls of this small room were clearly once lined 
            with hooks, though now only one remains. The exit is a door to 
            the east.",
       e_to foyer
  has light general
;
```

A few additional key differences between Beguile objects and I6 objects become apparent when you compare the two syntaxes:
- **Braces**
    
    Just as we saw with global functions, Beguile uses `{`braces`}` to enclose the object's body.  
    ```
    object cloakroom{
        ...
    }
    ```
  Beguile is consistent about the use of braces, relying heavily on the precedent set by languages such as C++, Java, C#, TypeScript, JavaScript, and many others.

- **Required Typing**

    We discussed this above, but it bears repeating: all object members are typed.  
    ```
    string short_name="Cloakroom";
    string description="The walls of this small room...";
    object e_to=foyer;    
    ```
    Note also that assignment is made to member variables with the assignment operator (`=`) and the declaration ends in `;`.  This differs from I6's comma-based, spaced-value syntax:
    ```
    short_name "Cloakroom",
    description "The walls of this small room...",
    e_to foyer
    ```

- **The "Syntactic Consistency" Design Principle, Attributes, the Array-Initializer Syntax**
  
    One of the design goals of Beguile, discussed above, is **Syntactic consistency**.  This translates roughly into, *reuse the language constructs wherever possible, avoiding the introduction of new language idioms which are not general-purpose*.  The `attribute` concept is one example where Beguile's implementation, with this applied principle, deviates from I6.  Instead of attributes being part of an object and manipulated with attribute-specific keywords, Beguile exposes attributes using the `attributeList` type:
    
    ```
    attributeList attributes = {light, general};
    ```
    Notice that the attributes `light` and `general`, which are instances of the `attribute` class, are assigned to an instance of the `attributeList` type, using the array-initializer syntax, which is a comma-separated list of attributes, wrapped in braces.  Beguile uses this same syntax to initialize `array`s of other types too.

    > ***Note**: The members of the list, assigned to an `attributeList` instance, must be instances of the `Attribute` class.  Beguile will throw an error otherwise.*

### Member Type Inference

The next section introduces the `extend` keyword:

```
extend class object { 
    string short_name; 
    string description; 
    object s_to; 
    object e_to; 
    object w_to; 
    object n_to;  
}
```

> ***Note**: `extend` is a powerful feature that deserves its own discussion; here, we'll explain just enough to follow the example. In short: `extend` adds members to an already-declared object or class — including the built-in `object` itself. By extending `object` with directional properties like `s_to` and `e_to`, we declare once that any `object` in our game may have these members, with the types specified.*

Here we extend `object` by defining several properties and their types.  On the surface, what this does isn't obvious, but it demonstrates the Beguile rule of "Type Inference".  Put another way, when a member is declared on a class, its type will be inferred, even if not specified, on instances of that class.  This enables the following to compile:
```
object foyer{
    short_name="Foyer of the Opera House";
    description = "You are standing in a spacious hall, splendidly decorated
        in red and gold, with glittering chandeliers overhead. The entrance 
        from the street is to the north, and there are doorways south and west.";
    s_to = bar;
    w_to = cloakroom;
    string n_to = "You've only just arrived, and besides, the weather outside 
        seems to be getting worse."; 
    attributes = {light}; 
}
```
Notice that most of the members don't have types specified.  Both `short_name` and `description` are understood to be of type `string` because they are members of `foyer`, which is of type `object`, and `object` defines the data type for these as such.  The same is true of `s_to` and `w_to`, both of which are of type `object`.  

> ***Note**: The "Type Inference" rule is an example of the **Ease of Use** principle applied.*

Two points of clarification:

- `n_to`, which would normally be inferred as type `object` has been overridden to type `string`.  We could just as easily have declared the default types for direction properties as `string`, then overridden `s_to` and `w_to` to be `objects`.  Either way is acceptable.

- You might note that we never defined the default type for `attributes` as type `attributeList` and Beguiler still accepted the property without error.  This is because the `attributes` property type is already set on the `object` definition, even without `extend`ing it.  We declared the type explicitly on the `cloakroom` object to further the example.  Had we left that particular member untyped, even without the `extend object` code, the example would still have compiled.

> ***Note**: Taken at face value, the above examples imply that objects declared *before* the object extension require member types to be specified, but not after.  This is actually untrue, and was presented in this fashion for illustration purposes.  Beguile supports declarations in any order, so the presence of `extend object` affects all instances of `object` across all files, regardless of the order declarations appear in.*

### Analyzing `Before` as an Example Member Routine

Among its other members, the `bar` object contains a member routine, specifically the `before` routine which the Puny Inform and the I6 Standard Library both use: 
```
object bar{
    ...
    bool before(){
        switch(action){
            case Look, Inv, Going: rfalse;
            case Go: if (attributes.hasnt(light) && (selected_direction != (property)n_to) ) {     
                        message.number += 2;     
                        rtrue("Blundering around in the dark isn't a good idea!"); 
                    }
            default: if (bar.hasnt(light)) {
                        message.number++;                             
                        rtrue("In the dark? You could easily disturb something!");
                    }
        }
        rfalse;
    }
    ...
}
```
There are a few things in the above example to take note of...

- The syntax for a Beguile *member* routine has the same structure as a Beguile *global* routine: Return value, routine name, parameters, braces...  All in the same place.  The only difference is that it occurs within the body of a `class` or `object`:
    ```
    bool before(){
    ```
    > ***Note**: Unlike the previous, `DeathMessage` example, `before` specifies a return type of `bool`.  Beguile requires every path which exits this routine to `return` a `true` or `false` value.*

- The `switch` statement, which tests the current `action`, must be called out explicitly.  I6's magic implied `switch` is not supported by design:
  ```
  switch(action){
  ```

- Verbs are tested by name, and are not preceded with `##`:
    ```
    case Look, Inv, Going: 
    ```
- Beguile borrows I6's signature shorthand "binary return" statements, `rtrue` and `rfalse` as synonyms for `return true` and `return false`:
    ```
    rfalse;
    ```
    > ***Note**: Beguile actually extends the syntax of binary returns in a way that I6 doesn't support.  We'll see this below.*
    
    
- The following deserves some scrutiny, because there's quite a bit to unpack: 
    ```
    if (attributes.hasnt(light) && (selected_direction != (property)n_to) ) {  
    ```
    - First, Beguile does not require the use of `self.` to access members of the same object.  
    
        ```
        attributes
        ```

        In I6, this would need to be written as `self.attributes`, and it still *can* be written like that; however, Beguile follows the same logic as C#, and matches names to sibling members automatically.  The `self.` qualifier is generally optional.

    - Second, recall from earlier that the `attributes` member is an instance of type `attributeList`.  The `attributeList` object hosts several members to manage and test the attributes it has been assigned, including `has` and `hasnt`:

        ```
        attributes.hasnt(light)
        ```
        
        > ***Notice** above that the `self.` is assumed, as per the previous note.  It could just as easily have been written as `self.attributes.hasnt(light)`.*

        > ***Note**: In service of the **Ease of use** principle, the `object` type has member routines which mirror, and feed into, those on the `attributeList` object.  That makes the following expression possible, effectively mirroring the conciseness of I6:*

        ```
        myObj.has(light)
        ```
    
    - Third, Beguile supports a form of type casting, where a type is wrapped in parentheses before a variable or value:
        ```
        (property)n_to
        ```

        Beguile type casting can be thought of as "compiler hints".  Usually they mimic the C++/C# behavior of type conversion; here, the cast changes *what gets emitted*, not the value's type.  Without the cast, `n_to` would emit as `self.n_to` — the *value stored at* that property.  With the cast, it emits as `n_to` — the property *identifier itself* (an integer).  The property id is what Puny stores in the `selected_direction` variable, so that's what we need to compare against.


-  Beguile also supports a host of additional operators, unavailable in I6:
  
    ```
    message.number += 2;     
    ```
    
    This particular expression adds two to the message `object`'s `number` member.  It is exactly equivalent to `message.number = message.number + 2;`.

- In addition to the normal syntax for `rtrue` and `rfalse`, Beguile supports an additional variant for both:
  
    ```
    rtrue("Blundering around in the dark isn't a good idea!"); 
    ```
    This form is equivalent to `print("Blundering around in the dark isn't a good idea!"); rtrue;`.  Conceptually, it is like I6's `print_ret`; however — and this is the gotcha — **it does not add a trailing newline**.  If you want one, include it explicitly with `^` or `\n` in the string.

- This next line looks like the previous example of assigning attributes:
    ```
    attributes = {!light}; 
    ```    
    The difference here is the logical NOT operator, which ensures the object does *not* have the attribute specified.  This is done either because an attribute is set by the class's ancestor or, as is the case here: simply to keep the attribute's absence explicit and clear (the `bar` room didn't previously have the attribute set, so turning it off serves no purpose). 


### The `cloak` Object
Our locations now covered, we turn to the `cloak` object:

```
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
                    rtrue("This isn't the best place to leave a smart cloak lying 
                        around.^");
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
```

A few elements stand out, warranting clarification.

#### Positioning in the World Map

A counterpoint to I6, which allows the object's parent to be declared on the first line of the object declarations (or alternatively, adjacent to the previous declaration with the `->` syntax), Beguile objects are explicitly set via the `parent` member:

```
object parent=selfobj;
```

This member variable is the primary means of positioning objects in the world model, both in the object declaration context, but also at runtime, as we'll see later.

#### Dictionary Words

From the Beguile perspective, the I6 Standard Library and Puny Inform define the `name` property as an `array` of dictionary words:
```
array<dictionaryWord> name = {.handsome, .dark, .black, .velvet, .satin, .cloak}; 
```

So let's break this down, starting with..
```
array<dictionaryWord> name 
```

Because Beguile is a typed language, so are its `array`s.  That is, we define an `array` with the type it contains, using the `<`angle-bracket`>` notation.  In the above line, we define `name` as an `array` of type `dictionaryWord`.  

```
 = {.handsome, .dark, .black, .velvet, .satin, .cloak}; 
```
In the above, we see the syntax for assigning values to an `array` includes enclosing a comma-separated list of the elements of the array in braces.  This is precisely the same syntax we use to assign values to an `attributeList` which is just an `array` of `attributes`, wrapped in an object which supplies specialized access members.

```
.handsome
```
Above we see the final form of the `dictionaryWord` literal, which begins with a period (`.`).  This example of `.handsome` is equivalent to I6's form, `'handsome'`; however, the apostrophe can be added as is.  I6's `'Michael^s'` is represented in Beguile as `.Michael's`.

> ***Note**: Plural words are represented with **two** dots, while singular words are represented with **one**.  So: `..dimes` is equivalent to I6's `'dimes//p'`.*

#### Adjusting Attributes

The `cloak` object's `before` routine goes one step further than we've seen before, testing attributes with `has` and `hasnt`; it adjusts them:
```
bar.give(light);
```
The above adds the `light` attribute to the `bar` object.  The flip side of this is...
```
cloak.ungive(general);
```
...which removes the `general` attribute from the `cloak` object.

> ***Note**: Recall from the earlier discussion that the `object` class provides helper routines which elevate most methods of their `attributes` member.  So `cloak.ungive(general);` is the same as `cloak.attributes.ungive(general);`.  The same is true when `give`-ing `light` to the `bar`.*
                            
### Interpolated Strings 

The hook object has one significant element which needs to be called out...
```
object hook { 
    string short_name = "small brass hook";
    object parent = cloakroom;
    array<dictionaryWord> name = {.small, .brass, .hook, .peg};
    bool description() {
        rtrue($"It's just a small brass hook, {cloak.parent == hook ? "with a cloak hanging on it.^" : "screwed to the wall.^"}");         
    }
    attributes = {scenery, supporter}; 
}         
```
Specifically, within the `description` routine, which prints an interpolated string: 
```    
    $"It's just a small brass hook, {cloak.parent == hook ? "with a cloak hanging on it.^" : "screwed to the wall.^"}"
```
Beguile borrows Interpolated Strings from C#.  Interpolated strings begin with `$` and are processed just like normal strings, except that everything bounded by `{`braces`}` is interpreted as Beguile code, rather than a raw quoted string.

So the above string is interpolated as beginning with...

```It's just a small brass hook,```
 
 ...then evaluates the "embraced" expression as Beguile:
 
 ``` 
 cloak.parent == hook ? "with a cloak hanging on it.^" : "screwed to the wall.^"
 ```
 ### Conditional Operators
 Within the braces of the previous interpolated string example is a conditional operator.  Conditionals are crafted with the following syntax: `expression ? val1 : val2`.  At run time, `expression` evaluates to either `true` or `false`.  If `true` the operator resolves to the first value, otherwise it resolves to the second. 
 
 Such operators are not limited to interpolated strings, they can be used for any assignment, but the above interpolated example is equivalent to the following I6:
 
 ```
    print "It's just a small brass hook, ";
    if (cloak.parent == hook) 
        print "with a cloak hanging on it.^"; 
    else 
        print "screwed to the wall.^";
 ```

### The `Initialise` Routine

```
bool Initialise(){
    rfalse("^^Hurrying through the rainswept November night...");
}
```

`Initialise` is called by the I6 library at game startup, just as it is in any I6 game.  Beguile declares it as `bool` to follow the library convention: returning `false` (the default) lets the standard banner print; returning `true` suppresses it.  The `rfalse("...")` form prints the intro text *and* returns `false` in a single statement.

### Extending Verbs
The last item of focus in the Cloak of Darkness example is the added command pattern, which enables the player to `PUT CLOAK ON HOOK`:

```
extend PutOn { grammar = {{.hang, HELD, .on, NOUN} }; }
```
Let's analyze the above line, bit by bit...
```
extend PutOn
```
Beguile represents all verbs as objects, specifically instances of the `verb` class. Puny Inform's `PutOn` verb was previously declared as such (`verb PutOn{...}`) in the bindings file, so by using `extend PutOn` we are simply `extend`ing the existing object, just as we did above (see "Member Type Inference").

```
grammar = {{.hang, HELD, .on, NOUN}};
```

- The `grammar` member (defined on type `verb`) is of type `grammarRuleList`, which you can think of as `array<grammarRule>`.
  
- A `grammarRule` contains an `array<patternElement>`.
  
- A `patternElement` can be any of several allowable value types used to define the pattern.  Here we are using two of the possible value types:

    - `dictionaryWord`s, which we covered above
    - members of the `grammarToken` enumeration, which includes HELD and NOUN, as well as other values, like MULTI, SPECIAL, etc...

So...

```
{.hang, HELD, .on, NOUN}
```
...is the array of `patternElements`, the `grammarRule`, which defines one of the grammar patterns associated with the `putOn` verb.  
```
{{.hang, HELD, .on, NOUN}}
```
Notice that the `grammarRule` itself is just one element in an enclosing, `grammarRuleList` array. Multiple grammar rules can be defined together if convenient:

```
{
    {.hang, HELD, .on, NOUN},
    {.drop, HELD, .over, NOUN}
}
```

## Select Additional Topics
While Cloak of Darkness serves as a gentle introduction to Beguile, it does little to highlight the enhancements of the language.  In this section, I'll go over select topics which are meaningful to the I6 developer. 

### The Beguile Language Runtime

We've referenced the **Beguile Language Runtime** — the BLR — several times in passing already.  Before going further, it's worth pausing to look at what it actually is.

The BLR is Beguile's standard library, written almost entirely in Beguile itself (with a sprinkling of I6 where we have to touch the metal).  It lives in the `beguiLib` folder, alongside the `beguiler` binary, and ships as part of the standard install.

Conceptually, the BLR is divided into three kinds of content:

- **Core types** — the language's runtime fabric.  The `object` class, `attribute` and `attributeList`, `array<T>`, and so on.  These are pulled in implicitly; you never `#include` them by name.

- **Extensions** — opt-in features that bolt onto the language.  You ask for them by name:
    ```
    #include <string>      // mutation, content equality, concatenation
    #include <array>       // LINQ-style operations on arrays
    #include <float>       // IEEE-754 arithmetic (Glulx only)
    #include <bglWorld>    // collection-oriented world-tree queries
    ```
    Each extension adds members to existing types or introduces new types entirely.  We'll meet several of these as we go.

- **Bindings** — wrappers around I6 libraries you bring in via `#includeI6`.  Two ship by default: `i6StandardLibrary` and `punyInform`.  We met one of these in the Cloak example.

> ***Note**: I designed the BLR to be opt-in by default.  A program that includes nothing pays nothing — no scratch buffers, no helper routines, no platform shims emit unless the feature that needs them is actually used.*

#### Namespaces

The BLR organizes its surface area under a small set of namespace objects rooted at `bgl`.  You'll see this in `bgl.world.getAll()`, `bgl.glulx.createMainWindow()`, and friends.  Under the hood, it's a regular Beguile feature: the `auto` qualifier.

The pattern looks like this — taken straight from the `<bglWorld>` extension:

```
object _bglWorld {
    array<object> getAll(...)    { ... }
    array<object> inParent(...)  { ... }
    array<object> instances(...) { ... }
}

extend bgl { auto world = _bglWorld; }
```

We declare the implementation as a normal `object` (here, `_bglWorld`), then `extend bgl` to attach it under a friendlier name.  From the author's perspective, you just write `bgl.world.getAll()` and the compiler routes the call through.

Other namespaces follow the same pattern: `bgl.glulx` for Glulx-specific helpers, `bgl.utilities` for general-purpose routines, and so on.  You'll meet them in the sections that follow.

### Strings

I6's mental model for strings is austere but elegant: a literal like `"hello"` evaluates to a memory address; you can `print` it, you can compare addresses for object-identity-style equality, and that's the surface.  Real string work — comparing contents, building strings at runtime, accumulating text — requires either careful use of the print stream or library code that hides the byte-twiddling.

Beguile keeps the I6-style address-of-literal as the default, then layers on a richer feature set you opt into.

#### Without `<string>`

The `string` type out of the box does what an I6 dev expects.  A literal:

```
string greeting = "Hello, world.";
```

...gives `greeting` the address of a packed string literal in memory.  You can:
- `print(greeting)` and it prints
- pass it around, store it in object members, copy it (you're copying the address)
- compare two `string` values with `==` — but you're comparing addresses, not content

The bare type carries no runtime overhead beyond what I6 would emit.  If your game just declares object descriptions and prints them, this is all you need.

#### With `<string>`

The moment you write...

```
#include <string>
```

...the `string` type grows up.  It's still the same type — you don't change any declarations — but a wealth of new methods and operators become available.  The most important shift, for the I6 dev's mental model: `==` now compares content.

```
string a = "hello";
string b = "hello";
if(a == b) { print("matched."); }
```

That snippet does the thing you'd naively expect, and it does so the same way whether `a` and `b` are literals, runtime values, or one of each.  Beguile dispatches by argument type behind the scenes; you don't think about which.

Other things the extension gives you, in no particular order:
- Construction at runtime, copying, mutation
- Concatenation with `+` and `+=`
- `switch` on string values, with the same content-equality semantics as `==`
- Slicing (`mid`, `left`, `right`), search, replace
- Case conversion, trimming
- Indexed character access (`s[3]`)
- Format strings via `string.format(pattern, args...)`

> ***Note**: Including `<string>` does add runtime cost — a small string pool and the dispatch routines that make typed comparisons work.  If your game is content-string heavy, the cost is well-paid.  If you're only printing literals and never comparing them, you don't need the extension and shouldn't include it.*

#### `print` is a routine, not a statement

We touched on this in the CoD walkthrough, but it's worth saying again here while strings are top of mind.  Beguile's `print` is a global routine with overloads:

```
print("plain text");      // string overload
print(score);             // int overload — prints decimal
print(noun);              // object overload — prints short_name
```

Each call takes a single argument of a known type.  If you want to print three things, that's three calls — or, much more commonly, one interpolated string.

> ***Note**: This differs sharply from I6's `print` statement, which takes comma-separated arguments and uses keyword tokens like `(string)`, `(name)`, `(the)` to coerce them.  Beguile relies on the type system instead — the right overload runs because the argument carries its type.*

#### Interpolation revisited

The CoD walkthrough introduced interpolated strings; here's the I6-dev mental model for them:

- Interpolation is a *compile-time* expansion. The compiler walks the literal, finds each `{...}` expression, and emits a sequence of `print` calls with the right overload for each piece.  There is no runtime "format" call.

- Because each `{...}` expression resolves to its natural type and `print` is overloaded, you can interpolate ints, objects, strings, and any other type with a `print` overload without explicit conversion:
    ```
    print($"You picked up {count} {item}.");
    ```

- Escape sequences (`\n`, `\t`, `\"`) work inside interpolated strings.  I6's `^` for newline passes straight through.

#### What about buffers?

For genuinely mutable, byte-level work — accumulating output into a buffer, parsing, building text procedurally — Beguile has a separate `<buf>` extension that gives you sized buffer objects with `[]` access, length, capacity, and the rest.  We'll cover buffers when we get to the section on BLR utilities.

### Using Arrays
- for in syntax
- accessor syntax
- some DM4 examples contrasted in Beguile

### Declaring a Verb from Scratch

The `extend` form lets us add patterns to a verb already declared in the binding.  If your game needs a brand-new verb, the syntax mirrors object declaration — a `verb` block with its own `grammar` and a `perform` routine that fires when the verb matches:

```
verb Flip {
    void perform(){
        print($"You flip {noun}.^");
    }
    grammar = {
        {.flip, HELD},
        {.toggle, HELD}
    };
}
```

This declares a `Flip` action and binds both `flip` and `toggle` as trigger words; either matches a `HELD` noun and runs `perform`.
