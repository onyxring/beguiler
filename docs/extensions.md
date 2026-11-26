# Beguile Extensions

Beguile ships with a collection of extensions: optional files which add a feature to your game when you include them, and cost nothing when you don't.  This guide covers what each one is for and how to use it.  It does not teach the language itself; for that, see [Beguile for the I6 Developer](Beguile%20for%20the%20I6%20Developer.md) and the [Language Specification](languageSpec.md).

Many of these began life in the orLibrary, my collection of Inform 6 extensions, and have been rewritten in Beguile.  If you know the orLibrary, most of what follows will feel familiar.

## Contents

- [Using extensions](#using-extensions)
  - [Where extensions fit](#where-extensions-fit)
  - [Where they live](#where-they-live)
  - [Including one](#including-one)
  - [Where the include goes](#where-the-include-goes)
- [General extensions](#general-extensions)
  - [center](#center): centered and inset text
  - [transition](#transition): a pause between parts of the story
  - [loopArray](#looparray): cycle through a list of messages
- [Narrative flexibility](#narrative-flexibility)
  - [firstImpressions](#firstimpressions): text shown only the first time
  - [stateDescriptors](#statedescriptors): the notes after an object's name
  - [uniqueMultiMessage](#uniquemultimessage): one line for many identical replies
  - [vagueQuantity](#vaguequantity): "several marbles", not "six marbles"
- [Parsing and disambiguation](#parsing-and-disambiguation)
  - [adjective](#adjective): words which describe, but do not name
  - [betterChoice](#betterchoice): a little more common sense
- [Modeling and mechanics](#modeling-and-mechanics)
  - [backdrop](#backdrop): scenery without an object for every word
  - [remoteLook](#remotelook): look in a direction
  - [playerCommandQueue](#playercommandqueue): commands the game types for the player
  - [implicitCommands](#implicitcommands): implied actions which take a turn
  - [dialogue](#dialogue): ASK, TELL and SAY
- [Miscellaneous enhancements](#miscellaneous-enhancements)
  - [menu](#menu): help, hints and choices
  - [statusLine](#statusline): a status line in three parts
- [Additional verbs](#additional-verbs)
  - [distinctRead](#distinctread): READ is not EXAMINE
  - [review](#review): look again, for free
- [Supporting extensions](#supporting-extensions)
  - [trackSeen](#trackseen): what the player has seen
  - [parserUtil](#parserutil): helpers for parser extensions
  - [hooks](#hooks): entry points into the Standard Library
- [Quick reference](#quick-reference)

## Using extensions

### Where extensions fit

The Beguile source which comes with the compiler is arranged in layers.  Each layer builds on the ones beneath it, and never on the ones above:

```
                    +-------------------------+
  Functional        |  Functional Extensions  |   <- this guide
  extensions        +-------------------------+
                    |  Bindings               |
                    +-------------------------+
  Beguile Language  |  Language Extensions    |
  Runtime (BLR)     +-------------------------+
                    |  Core                   |
                    +-------------------------+
                         the compiler
```

- **Core** is the foundation, closest to the compiler.  Every program gets it without asking.
- **Language extensions** add to the language itself: `<array>`, `<string>`, `<buf>`, `<linq>` and `<ui>`, for example.  Together with the core, these are the Beguile Language Runtime.
- **Bindings** describe an Inform 6 library, such as the Standard Library or PunyInform, so Beguile code can use it.
- **Functional extensions** are furthest from the compiler.  They don't change the language; they add something to the *game*: a verb, a smarter parser, a new way to describe things.

So there are two kinds of extension.  A *language extension* is part of the runtime and changes what you can write; a *functional extension* changes what your game can do.  This guide is about the second kind, the top layer, and where it says "extension" on its own, that is what it means.

### Where they live

The functional extensions are in the `extensions` folder of the files which come with the compiler:

| Folder | What is in it |
|---|---|
| `extensions/general/` | Extensions which work with any library, or with none. |
| `extensions/stdlib/` | Extensions to the Inform 6 Standard Library. |

> **Note.** Everything in `extensions/stdlib/` needs the Inform 6 Standard Library.  They do not work with PunyInform.

### Including one

An extension is included by its path, in angle brackets and without the `.bgl`:

```bgl
#include <extensions/stdlib/firstImpressions>
```

That's it.  If an extension depends on another, it includes that one itself, so you never need to work out what order the pieces underneath go in.  Including the same extension twice is harmless.

### Where the include goes

The general extensions can be included anywhere before you use them.

The Standard Library extensions go **after the library's `verblib`**, and before your game's own objects.  A typical game looks like this:

```bgl
#beguilerSettings { target = z5; includePaths = "path/to/inform6/stdlib"; }
#include <bindings/i6StandardLibrary>

#includeI6 "parser"
#includeI6 "verblib"

#include <extensions/stdlib/firstImpressions>
#include <extensions/stdlib/backdrop>
#include <extensions/stdlib/adjective>      // last; see below

// ...the game...

#includeI6 "grammar"
```

There are only two exceptions, and both are called out again where they apply:

- **adjective** goes after the other extensions, so theirs get the first say when the parser is choosing between objects.
- **dialogue** comes in two files.  The second, `dialogueGrammar`, goes after the library's `grammar`.

## General extensions

These need no particular library.

### center

```bgl
#include <extensions/general/center>
```

Prints text centered in the window, wrapped to fit.  It also prints text inset between two margins, which is what I use for help screens.

```bgl
bgl.output.center("*** You have won! ***^(Good job!)");
bgl.output.inset("If you are new to text adventures, don't worry.", 8);
```

The text may be a string, a routine which prints, or an object (its name is printed).

`center` takes a few optional parameters after the text:

| Parameter | Meaning |
|---|---|
| `maxWidth` | Wrap at this many characters.  0, the default, is the width of the window. |
| `highlight` | Reverse video: `centerHighlight.none`, `.textOnly`, or `.all` (the text and the spaces before it). |
| `leftAlign` | True starts every line at the same column, instead of centering each one. |
| `frame` | A routine which draws something before and after each line. |
| `subIndent` | Extra indent for every line after the first. |

`inset` takes the text, the `margin` in columns (8 when left as 0), and a `subIndent`.

> **Note.** The longest text either one takes is 1024 characters.

### transition

```bgl
#include <extensions/general/transition>
```

A break between parts of a story: optional centered text, a centered prompt, a wait for a key, and then a cleared window.

```bgl
bgl.output.transition("Chapter Two");               // text, the default prompt, then clear
bgl.output.transition(null, "Press a key, friend"); // just a prompt of my own
bgl.output.transition("Later...", null, true);      // don't clear the window afterward
```

The default prompt is "Press any key to continue".

### loopArray

```bgl
#include <extensions/general/loopArray>
```

It is a common pattern: a list of messages, where each use should show the next one.  A loop array is an `array<var>` whose first entry is a tracker, which you leave as 0, followed by the values:

```bgl
object clearing {
    array<var> hints = {0, "Try the hand.", "Try going west.", "West. Really."};
}

verb Hint {
    meta = true;
    grammar = { {.hint} };
    void handler() {
        print((string)bgl.util.loopArray.getNext(clearing, (property)hints, loopEnd.repeatLast));
        print("^");
    }
}
```

```
>hint
Try the hand.

>hint
Try going west.

>hint
West. Really.

>hint
West. Really.
```

There are three routines, each of which takes either an object and one of its members, as above, or just an array:

| Routine | What it does |
|---|---|
| `getNext` | The next unused entry, in order. |
| `getAny` | An unused entry, chosen at random. |
| `isComplete` | Whether every entry has been used. |

The optional last parameter says what happens once every entry has been used:

| Value | Meaning |
|---|---|
| `loopEnd.restart` | Start over.  This is the default. |
| `loopEnd.stop` | Return `null` from then on. |
| `loopEnd.repeatLast` | Keep returning the last entry. |

> **Note.** `getAny` reorders the entries as it picks them, so don't count on the order of a list you have used it on.

## Narrative flexibility

This group adds to the Standard Library's flexibility when describing things.  Since virtually every game written this way is made of text, that's worth some attention.

### firstImpressions

```bgl
#include <extensions/stdlib/firstImpressions>
```

When describing a room or object for the first time, it can be useful to toss in some initial, passing thoughts from the player character.  It helps flesh out the character, or communicate something the character knows which the player doesn't.

**Normal behavior.**  The Standard Library has no once-only description text.

**Revised behavior.**  This extension provides two members, `preImpression` and `postImpression`, which can be given to any room or object:

```bgl
object candlestick {
    "candlestick"; name = {.candlestick}; parent = parlor;
    string description = "It is copper, or perhaps brass, polished to a dull gleam.";
    string preImpression = "A sense of unease settles on you as you look at the candlestick. ";
    string postImpression = " You heft it, calculating the force it would exert. Definitely lethal.";
}
```

```
>x candlestick
A sense of unease settles on you as you look at the candlestick. It is copper, or perhaps brass,
polished to a dull gleam. You heft it, calculating the force it would exert. Definitely lethal.

>x candlestick
It is copper, or perhaps brass, polished to a dull gleam.
```

The same works for a room and LOOK.  Either member can be a routine instead of text.

> **Note.** Nothing separates the impressions from the description.  Add your own spaces or line breaks, as I did above.

Once shown, an impression is not shown again; but the player can revisit one with REVIEW, if you include [review](#review).

### stateDescriptors

```bgl
#include <extensions/stdlib/stateDescriptors>
```

**Normal behavior.**  In room descriptions and inventories, the Standard Library adds notes after an object's name, like "(closed)" or "(providing light)".  There is no simple way to add one of your own, or to take one away.

**Revised behavior.**  Each note comes from a small routine, a handler, and you can add, remove or replace them.  Simply including the extension changes almost nothing.

A handler is given the object and a `suppress` flag.  It returns whether the note applies, and prints it unless `suppress` is true:

```bgl
attribute blessed;

bool sdBlessed(object obj, bool suppress) {
    if(!suppress && obj.has(blessed)) print("glowing with purity");
    return obj.has(blessed);
}
```

> **Note.** The `suppress` flag is there because the library asks twice: once to count the notes, so it knows where the commas and the "and" go, and once to print them.

Register it, usually in `Initialise()`, for room descriptions, inventories, or both:

```bgl
stateDescriptors.addRoomDescriptionHandler(sdBlessed);
stateDescriptors.addInventoryHandler(sdBlessed);
```

```
You can see a magic box (closed and glowing with purity) here.

>take box
Taken.

>i
You're carrying:
  a magic box (closed and glowing with purity)
```

The built-in handlers are `sdWorn`, `sdClosedContainer`, `sdOpenContainer`, `sdLockedContainer`, `sdEmptyContainer` and `sdLight`.  Taking one out is as easy as adding one:

```bgl
stateDescriptors.removeRoomDescriptionHandler(sdLight);
```

Setting `stateDescriptors.suppressSingleItemWhich = true` leaves out the "which is" the library puts before a single note in an inventory.

### uniqueMultiMessage

```bgl
#include <extensions/stdlib/uniqueMultiMessage>
```

**Normal behavior.**  A command on many objects answers once for each, even when the answers are all the same:

```
>take all from bowl
marble: Removed.
marble: Removed.
marble: Removed.
marble: Removed.
marble: Removed.
marble: Removed.
```

**Revised behavior.**  Identical replies which follow one another are consolidated:

```
>take all from bowl
6 marbles : Removed.
```

No additional code is needed.  The objects should have a `plural`, which is what the count is printed with.

> **Note.** A reply longer than 500 characters is not consolidated.  To raise that, define `orUniqueMultiMessageBufferSize` before the include: `#define orUniqueMultiMessageBufferSize 800`.

### vagueQuantity

```bgl
#include <extensions/stdlib/vagueQuantity>
```

**Normal behavior.**  The Standard Library describes a group of identical objects with an exact count: "In the bowl are six marbles."

**Revised behavior.**  Simply including the extension gives vaguer language which scales with the quantity:

```
>x bowl
In the bowl are several marbles.
```

By default, two is "a couple of", three and four are "a few", five through nine are "several", and ten or more are "numerous".

To override this for a kind of object, give it a `vagueQuantity` list: pairs of the count a phrase starts at, and the phrase.  Counts below the first one are printed exactly.

```bgl
class fish : object {
    string short_name = "fish";
    rawArray<dictionaryWord> name = {.fish};
    string plural = "fish";
    rawArray<var> vagueQuantity = {2, "a pair of", 4, "a school of", true};
}
```

The `true` after "a school of" says that phrase is singular.  Without it, four fish would read "In the tank *are* a school of fish"; with it:

```
>x tank
In the tank is a school of fish.
```

> **Note.** Sometimes plurality is hard to hear.  It helps me to turn the sentence around: "A pair of fish *are* in the tank", but "A school of fish *is* in the tank."

To keep exact counts for an object, or for everything inside a container, give it the `specificQuantity` attribute.

## Parsing and disambiguation

These change how the player's input is turned into a command.

### adjective

```bgl
#include <extensions/stdlib/adjective>      // after the other extensions
```

**Normal behavior.**  The Standard Library doesn't distinguish nouns from adjectives.  Give a drinking glass and a glass marble the words they need, and the parser can't tell which one "glass" means:

```
>x glass
Which do you mean, the drinking glass or the glass marble?
```

**Revised behavior.**  This extension adds an `adjective` member, for the words which describe an object without naming it.  Declare the members once, then use them:

```bgl
extend class object { array<dictionaryWord> adjective; bool requireNoun; }

object drinkingGlass {
    "drinking glass"; name = {.glass}; adjective = {.drinking}; parent = shop;
    string description = "For drinking.";
}
object glassMarble {
    "glass marble"; name = {.marble}; adjective = {.glass}; parent = shop;
    string description = "For rolling.";
}
```

Now the parser has enough to decide correctly:

```
>x glass
For drinking.

>x marble
For rolling.
```

By default an object can still be referred to by its adjectives alone ("x drinking"); the parser just prefers an object named by a noun.  To insist on a noun everywhere, usually in `Initialise()`:

```bgl
adjectiveParser.mode = adjectiveMode.nounRequired;    // nounPreferred by default
```

Whatever the mode, a single object can decide for itself with `requireNoun = true;` (or `false`).

> **Note.** I first ran across the idea of adjectives in the DM4, exercise 75.

### betterChoice

```bgl
#include <extensions/stdlib/betterChoice>
```

**Normal behavior.**  When the parser has to choose between objects, it sometimes offers choices which make no sense.  Consider a candy dispenser, fixed in place, holding a chocolate candy and a plastic candy toy:

```
>take candy
Which do you mean, the big candy dispenser, the chocolate candy or the plastic candy toy?
```

The dispenser can't be taken.  Why offer it?

**Revised behavior.**  The parser gets a little smarter.  Specifically:

- TAKE gives lower priority to objects which are fixed in place or already carried.
- DROP gives lower priority to objects which are not carried.
- EAT prefers the edible.
- For a command with two objects, the one already chosen as the first is not offered as the second.

```
>take candy
Which do you mean, the chocolate candy or the plastic candy toy?
```

No additional code is needed.  When you do want an object to speak for itself, give it a `chooseobject` routine:

```bgl
object shyBlock {
    "shy block"; name = {.shy, .block}; parent = hall;
    chooseAnswer chooseobject(chooseCode code) {
        if(code != chooseCode.askingForHint) return chooseAnswer.exclude;   // keep me out of "all"
        if(action_to_be == Examine) return excludeFromChoice(self);         // and out of EXAMINE's choices
        return chooseAnswer.defer;
    }
}
```

The parser asks with one of three codes: `planExclude` and `planInclude` while it builds a list for "all", and `askingForHint` when choosing between matches.  While it plans a list, the answers are `include`, `exclude`, `defer` (let the rules above decide) and `parser` (skip those rules too).

## Modeling and mechanics

### backdrop

```bgl
#include <extensions/stdlib/backdrop>
```

Backdrops are scenery: things the player may examine but do little else with.

**Normal behavior.**  Inform gives you two ways to make scenery.  You can put the words in the room's `name`, which is simple but answers everything with one global message ("That's not something you need to refer to...").  Or you can create a scenery object for each thing, which is flexible, and verbose.

**Revised behavior.**  A room lists its scenery in a `backdrops` member, one row per thing.  A row is the words, then a description, then a reply to anything else:

```bgl
object clearing {
    short_name = "Clearing"; attributes = {light};
    string description = "Thick plants surround you in all directions. A hand, rising from the bushes to the west, beckons to you.";
    array<array<var>> backdrops = {
        {.plants, .bushes, "The plants are thick, mostly impenetrable.", bdDefault},
        {.hand, "It gestures for you to follow it west.", "The hand probably wouldn’t go along with that."}
    };
}
```

```
>x plants
The plants are thick, mostly impenetrable.

>take plants
That's not something you need to refer to in the course of this game.

>x hand
It gestures for you to follow it west.

>push hand
The hand probably wouldn't go along with that.
```

The last two entries of a row can each be:

| Entry | Meaning |
|---|---|
| text | Printed.  Interpolated text works too. |
| `bdDefault` | The library's usual message. |
| `bdDescr` | In the reply slot only: answer everything with the description. |
| a routine | Run, to decide for itself. |

A reply routine is where a backdrop gets interesting, since it can respond to particular actions:

```bgl
{.path, "A path, leading east.", () => {
    if(action == Enter) { Go.perform(e_obj); return bdRedirected; }
    rtrue("It’s just a path.");
}}
```

Return true when the routine has printed its answer, false for the library's message, and `bdRedirected` when it ran another action instead.

A few more things worth knowing:

- **Scenery in several rooms.**  Declare an object of class `backdrop` with its own `backdrops` rows, and a `found_in` listing the rooms.
- **Real objects win.**  If a real object and a backdrop share a word, the parser picks the real object, without comment.
- **Other verbs which describe.**  EXAMINE prints the description.  To have another verb do the same: `backdropEngine.registerDescriptionVerb(Search);`
- **Which word was used.**  From a `before` routine, `backdropEngine.getKeyword()` returns the first word of the row the command matched, or `null`.

### remoteLook

```bgl
#include <extensions/stdlib/remoteLook>
```

**Normal behavior.**  The Standard Library has a `compass_look` routine, which a room can use to answer LOOK NORTH and the like.  It is one routine for all directions, and it knows nothing about the room next door.

**Revised behavior.**  This extension adds a member for each of the twelve directions, which work alongside `compass_look`:

`n_look`, `s_look`, `e_look`, `w_look`, `ne_look`, `nw_look`, `se_look`, `sw_look`, `u_look`, `d_look`, `in_look`, `out_look`

It also lets a room describe itself as seen from next door, with `remoteDescription`:

```bgl
object clearing {
    short_name = "Clearing"; attributes = {light};
    object w_to = glade;
    string n_look = "More plants.";
    string cant_look = "Nothing but leaves that way.";
}
object glade {
    short_name = "Glade"; string description = "A glade."; attributes = {light};
    object e_to = clearing;
    string remoteDescription = "Through the bushes you glimpse a sunlit glade.";
}
```

```
>look n
More plants.

>look w
Through the bushes you glimpse a sunlit glade.

>look e
Nothing but leaves that way.

>look up
You see nothing unexpected in that direction.
```

When the player looks in a direction, the answer comes from the first of these which applies:

1. The room's member for that direction (`n_look`).
2. The room's `compass_look` routine, if it handles the direction.
3. The next room's `remoteDescription`.  A closed door in the way prints the door's `remoteClosed`, or a message saying it is closed.
4. The room's `cant_look`, or `cant_look_alt` for up, down, in and out.
5. The library's usual "You see nothing unexpected in that direction."

Any of these can be a routine.  A `remoteDescription` routine is told the direction it is being looked at from.

> **Note.** Most members the extensions add are camelCase.  `cant_look` is so closely related to the Standard Library's `cant_go` that I kept its convention.

### playerCommandQueue

```bgl
#include <extensions/stdlib/playerCommandQueue>
```

**Normal behavior.**  The Standard Library does not support queued commands.

**Revised behavior.**  This extension introduces the `playerCommands` queue.  While it is empty, the game behaves normally.  Once commands have been added, they are run and removed, one per prompt, as though the player were typing them:

```bgl
playerCommands.pushCommand("go down");
playerCommands.pushCommand("take banana");
playerCommands.pushCommand("give banana to monkey");
```

Nothing restricts what you queue.  A command is parsed when its turn comes, so a verb or noun the game doesn't know produces the same error a typo would.  That has practical uses: the same queued command can mean different objects, depending on where the player is by then.

| Routine | What it does |
|---|---|
| `pushCommand(text)` | Add a command after those waiting. |
| `preemptCommand(text)` | Add a command before those waiting. |
| `pushAction(verb, noun, second)` | As `pushCommand`, for an action; it is written out as a command. |
| `preemptAction(verb, noun, second)` | As `preemptCommand`, for an action. |
| `clear()` | Forget everything waiting. |
| `getLength()` | How many are waiting. |

The command routines take two optional parameters after the text: `meta`, which makes the command take no game time, and `silent`, which keeps the command itself from being shown.

```bgl
playerCommands.pushCommand("open box", true);           // takes no time
playerCommands.pushCommand("say hello to monkeys", false, true);   // not shown
```

Two more things:

- **Redirecting.**  From an action routine, `return redirectCommand("x canopy");` runs that command in place of the current action.
- **Interrupting.**  Set `playerCommands.interrupt` to true, or to a routine, to stop the queue at its next command.  The rest of the queue is discarded.

> **Note.** Ten commands can wait at once.  To change that, define the Inform 6 constant `actionQueueBuffer` ahead of the include.  `queuedCommandPrompt`, the text shown before a queued command, works the same way.

### implicitCommands

```bgl
#include <extensions/stdlib/implicitCommands>
```

> **Note.** For this discussion, a "command" is text the player types, and an "action" is what the game resolves it to and carries out.

**Normal behavior.**  When a command needs something first (EAT BANANA when the banana is on the floor), the Standard Library quietly performs the TAKE in the same turn.  Explicit actions take a turn each; implied ones happen out of time.  No daemons run between the two, and no `each_turn` routines.

**Revised behavior.**  The implied action becomes a command of its own, run at its own prompt through the [playerCommandQueue](#playercommandqueue), followed by the original command.  Each takes a turn, and each can be undone on its own.

Out of the box, this covers:

- taking something before using it;
- closing something before locking it;
- unlocking something before opening it;
- taking something off before dropping it, giving it away, or putting it somewhere.

To add your own, answer the `ext_generateImplicitCommands` hook from a library extension object (see [hooks](#hooks)).  Return `tempAction(...)` for what is needed first, true to stop the action altogether, or false to have no opinion:

```bgl
object myImplicitRules {
    parent = LibraryExtensions;
    var ext_generateImplicitCommands() {
        if(action == Read && noun == letter && glasses.hasnt(worn)) {
            print("(first putting on your glasses)^");
            return tempAction(Wear, glasses);
        }
        rfalse;
    }
}
```

Set the library's `no_implicit_actions` to true to turn all of this off.

### dialogue

```bgl
#include <extensions/stdlib/dialogue>               // after verblib
// ...
#includeI6 "grammar"
#include <extensions/stdlib/dialogueGrammar>        // right after the library's grammar
```

Conversation, through ASK, TELL and SAY, about topics the characters know.

**Normal behavior.**  The Standard Library hands ASK and TELL to a character's `life` routine, along with the words the player typed.  Deciding what those words mean is up to you.

**Revised behavior.**  A topic is an object.  It has words the player can refer to it by, a record of who knows it, and the conversation itself:

```bgl
object cook {
    "cook"; name = {.cook}; parent = kitchen; attributes = {animate};
    string iDunno = "The cook shrugs.";
}

dialogueTopic aboutSoup {
    ""; knownBy = cook; name = {.soup};
    rawArray<var> quip = {"“What’s in the soup?” you ask.", "“Onions,” says the cook."};
}

dialogueTopic aboutFire {
    ""; knownBy = selfobj; name = {.fire};
    quip = "“The kitchen is on fire,” you say.^“Again?”";
}
```

```
>ask cook about soup
"What's in the soup?" you ask.

"Onions," says the cook.

>tell cook about fire
"The kitchen is on fire," you say.
"Again?"

>ask cook about fish
The cook shrugs.
```

The members of a topic:

| Member | Meaning |
|---|---|
| `name` | The words the player refers to the topic by. |
| `knownBy` | Who can say it.  The player can ASK a character about what that character knows, and TELL anyone what the player (`selfobj`) knows. |
| `context` | Who it means something to.  Leave it out for anyone. |
| `quip` | The conversation.  One part is something to be told.  Two parts are a question and its answer, which is what makes a topic something the player can ASK about. |

`knownBy` and `context` can each be a character, `true` for everyone, a routine, or a list of characters.  A quip part can be text or a routine.

**Topics inherit.**  A topic which doesn't say who knows it, or who it is for, takes that from its parent topic.  This saves a lot of typing: create one topic to hold the settings, and hang the rest from it.

```bgl
dialogueTopic knownBySelf { ""; knownBy = selfobj; }
dialogueTopic forTheCook  { "", knownBySelf; context = cook; }
dialogueTopic aboutRent   { "", forTheCook; name = {.rent}; quip = "“The rent is due,” you say."; }
```

**What the player can type.**  Besides the full forms, the extension works out who is being spoken to, and accepts a few shorthands:

```
>ask about soup
(Asking the cook)
"What's in the soup?" you ask.

"Onions," says the cook.

>cook, fire
"The kitchen is on fire," you say.
"Again?"
```

ASK, TELL and SAY can be shortened to A, T and S, and "cook, fire" means SAY FIRE TO COOK.

**On a character:**

| Member | Meaning |
|---|---|
| `iDunno` | What the character says to a topic it doesn't respond to: text or a routine. |
| `interactingWith` | Who the character is talking to. |
| `learnTopic` | A routine run when the character is told a topic with the `learnable` attribute. |

**On a topic,** to ask what has been said so far: `hasBeenTold()`, `hasBeenToldTo(who)` and `hasBeenToldBy(who)`.

A library extension object (see [hooks](#hooks)) can watch every conversation: `ext_topicAsk` and `ext_topicTell` run before a topic and can stop it by returning true; `ext_topicAsked` and `ext_topicTold` run after.

## Miscellaneous enhancements

### menu

```bgl
#include <extensions/stdlib/menu>
```

A tree of menus which the player moves through with the keyboard: for help, hints and about screens, or for offering a choice.

**Normal behavior.**  The Standard Library has no menus.

**Revised behavior.**  Each `menu` object is a choice, and its children are the choices beneath it.  They are arranged the same way objects in the game world are:

```bgl
menu helpMenu    { "Help"; }
menu aboutItem   { "About this game", helpMenu; string description = "A small example."; }
menu creditsItem { "Credits", helpMenu; void rawDescription() { bgl.output.center("Written by me."); } }

verb Help {
    meta = true;
    grammar = { {.help} };
    void handler() { helpMenu.show(); }
}
```

The player moves with the arrow keys (or N and P), chooses with Enter or the space bar, and backs out with Q, X or Escape.

`show()` takes one optional parameter:

| Style | Meaning |
|---|---|
| `menuStyle.fullScreen` | The menu takes the whole display, clearing it first.  The default; good for help. |
| `menuStyle.topOnly` | The game's text stays, and the menu takes a few lines at the top.  Good for offering the player a choice in the middle of play. |

What `show()` returns comes down to two rules:

- Choosing an item with no children and no text ends the menu, and that item is returned.
- Backing out of the menu returns `null`.

An item with a `description` (or a `rawDescription` routine, to print it yourself) shows its text, and the menu carries on.

A few more members:

| Member | Meaning |
|---|---|
| `menuDisplay` | The text shown for an item in its parent's menu, in place of its name. |
| `childMenuItems` | A list of the menu's choices, in place of its children. |
| `concealed` (attribute) | The item is not offered.  Handy for hints which unlock later. |
| `itemWindowId` | Glulx only: a window to draw the choices in, in place of the status line. |

### statusLine

```bgl
#include <extensions/stdlib/statusLine>
```

**Normal behavior.**  Changing the Standard Library's status line means replacing the whole routine which draws it.

**Revised behavior.**  The status line has three sections, each printed by its own routine: `statusLeft`, `statusMiddle` and `statusRight`.  Replace only the ones you want to change:

```bgl
replace void statusRight()  { print($"{turns} turns"); }
replace void statusMiddle() { print("Chapter One"); }
```

Left alone, the left shows the location, the middle is empty, and the right shows the score and moves (or the time).

## Additional verbs

### distinctRead

```bgl
#include <extensions/stdlib/distinctRead>
```

**Normal behavior.**  READ is a synonym for EXAMINE.

**Revised behavior.**  READ is its own action, which prints an object's `readDescription`:

```bgl
object sign {
    "sign"; name = {.sign}; parent = parlor; attributes = {static};
    string description = "A brass sign, screwed to the wall.";
    string readDescription = "It says: KEEP OUT.";
}
```

```
>read sign
It says: KEEP OUT.

>read candlestick
The candlestick cannot be read.
```

When reading something is no different from looking at it, point one at the other: `property readDescription = description;`

### review

```bgl
#include <extensions/stdlib/review>
```

Adds the REVIEW verb.  REVIEW looks at the room again, and REVIEW *something* examines it again; in both cases as though for the first time, so [first impressions](#firstimpressions) are shown again.  Reviewing takes no game time.

```
>review candlestick
A sense of unease settles on you as you look at the candlestick. It is copper, or perhaps brass,
polished to a dull gleam. You heft it, calculating the force it would exert. Definitely lethal.
```

The player can only review what they have already examined.

## Supporting extensions

You will rarely include these directly.  The extensions above pull them in as needed, but they are there if you are writing an extension of your own.

### trackSeen

```bgl
#include <extensions/stdlib/trackSeen>
```

Adds the `beenSeen` attribute, and keeps it up to date: an object gets it when the player examines it, and a room when the player has looked at it.

```bgl
if(candlestick.has(beenSeen)) print("You remember the candlestick.^");
```

### parserUtil

```bgl
#include <extensions/stdlib/parserUtil>
```

Names and helpers for extensions which change how the parser chooses between objects: the codes the parser passes to `ext_chooseobjects` (`PLANNING_EXCLUDE`, `PLANNING_INCLUDE`, `ASKING_FOR_HINT`), the answers (`DEFER_CHOICE`, `FORCE_INCLUDE`, `FORCE_EXCLUDE`), and two routines, `excludeObjFromDisambiguation(obj)` and `removeMatchPos(pos)`, for taking an object out of the running.

### hooks

```bgl
#include <extensions/stdlib/hooks>              // every group
#include <extensions/stdlib/hooks/verbs>        // or just one
```

Most of the extensions in this guide work by changing something the Standard Library does.  The traditional way to do that is to replace a library routine; but only one extension can replace any given routine, so two extensions which both need to change `LookSub` cannot be used together.

Hooks solve this.  A hook is a call, merged into a library routine, which asks every *library extension* whether it has something to say at that point.  Any number of extensions can answer the same hook.

A library extension is nothing more than an object whose parent is `LibraryExtensions`, with a routine named after the hook:

```bgl
#include <extensions/stdlib/hooks/verbs>

object myHooks {
    parent = LibraryExtensions;
    bool ext_lookSub() { print("[About to look]^"); rfalse; }
}
```

Returning false lets the library carry on; for most hooks, returning true means "I have handled this."

> **Note.** Including a group of hooks changes nothing about how the library behaves until something answers one.

The hooks come in seven groups, one file each.  Include only the groups you answer.

| Group | Where its hooks are | Hooks |
|---|---|---|
| `hooks/informLibrary` | The play loop | `ext_afterGamePrologueNotify`, `ext_playLoopRewriteParsedCommands`, `ext_playLoopAssignObjectVariables`, `ext_playLoopPerformSingleObjectAction`, `ext_playLoopPerformMultiObjectAction` |
| `hooks/keyboard` | Reading the command | `ext_keyboardPrimitiveNotify`, `ext_keyboardPrimitive`, `ext_keyboardGetInput`, `ext_beforePrompt`, `ext_keyboardPrepInput`, `ext_keyboardHandleComment`, `ext_keyboardHandleUndo`, `ext_keyboardHandleOops` |
| `hooks/parser` | Parsing | `ext_forceLookAhead`, `ext_suppressParsingImplicitTake`, `ext_adjudicate`, `ext_redirectActionNotify` |
| `hooks/standardLibrary` | Routines across the library | `ext_identical`, `ext_nounWord`, `ext_nextWord`, `ext_beforeAction`, `ext_afterAction`, `ext_afterRoutines`, `ext_displayStatus` |
| `hooks/verbs` | LOOK, EXAMINE, GO and moving the player | `ext_lookSub`, `ext_examineSub`, `ext_preDescription`, `ext_postDescription`, `ext_goSub`, `ext_goSubNoDir`, `ext_playerToNotify` |
| `hooks/writeAfterEntry` | The text after each entry of a list | `ext_sdPartInvBit`, `ext_sdFullInvBit`, `ext_sdConcealBit`, `ext_sdAlwaysBit`, `ext_sdRecurseBit`, `ext_sdIsOrAre`, `ext_sdNewLineBit`, `ext_sdDoList` |
| `hooks/writeList` | Counting the groups of a list | `ext_forceSingular`, `ext_wlrPluralMany` |

Each hook's own file says what it is given and what its answer means.

## Quick reference

| Extension | Include | In a sentence |
|---|---|---|
| center | `<extensions/general/center>` | Centered and inset text. |
| transition | `<extensions/general/transition>` | Text, a key press, and a cleared window. |
| loopArray | `<extensions/general/loopArray>` | The next message from a list, in order or at random. |
| adjective | `<extensions/stdlib/adjective>` | Words which describe an object without naming it. |
| backdrop | `<extensions/stdlib/backdrop>` | Scenery listed in rows on a room. |
| betterChoice | `<extensions/stdlib/betterChoice>` | Smarter choices between objects. |
| dialogue | `<extensions/stdlib/dialogue>` and `<extensions/stdlib/dialogueGrammar>` | ASK, TELL and SAY about topics. |
| distinctRead | `<extensions/stdlib/distinctRead>` | READ as its own action. |
| firstImpressions | `<extensions/stdlib/firstImpressions>` | Text shown with a description the first time only. |
| hooks | `<extensions/stdlib/hooks>` | Entry points into the Standard Library's routines. |
| implicitCommands | `<extensions/stdlib/implicitCommands>` | Implied actions run as commands, in game time. |
| menu | `<extensions/stdlib/menu>` | A tree of menus. |
| parserUtil | `<extensions/stdlib/parserUtil>` | Helpers for parser extensions. |
| playerCommandQueue | `<extensions/stdlib/playerCommandQueue>` | Commands run as though the player typed them. |
| remoteLook | `<extensions/stdlib/remoteLook>` | Look in a direction, and into the next room. |
| review | `<extensions/stdlib/review>` | The REVIEW verb. |
| stateDescriptors | `<extensions/stdlib/stateDescriptors>` | Add, remove or replace notes like "(closed)". |
| statusLine | `<extensions/stdlib/statusLine>` | A status line in three replaceable parts. |
| trackSeen | `<extensions/stdlib/trackSeen>` | The `beenSeen` attribute. |
| uniqueMultiMessage | `<extensions/stdlib/uniqueMultiMessage>` | "6 marbles : Removed." |
| vagueQuantity | `<extensions/stdlib/vagueQuantity>` | "several marbles" |
