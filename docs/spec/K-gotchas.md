# Appendix K Gotchas

This appendix collects behavior which follows from the rules but tends to surprise.  Each entry shows the code, what happens, why, and how to get what you meant.  Every entry is covered in the section it names.

## K.1 A Method Named Like a Library Property Is That Property

<!-- doctest: skip -->
```bgl
class Card : object {
    stringObj Describe() { return "a card"; }
}
Card cardA { "red card"; parent = roomA; }
```

`cardA` will no longer appear in the room's "You can see …" list, and nothing warns you.

Since Beguile names are case-insensitive, `Describe` is `describe`, and a member named like a library property keeps that name (§16.9):  the method becomes the object's `describe` property.  The standard library calls `describe` when it lists an object in a room and treats a non-zero result as "the object described itself; don't list it" (§23.3.11).  The method returns a string, which is never zero.

Give the method a name of its own (`Details()`), or, to take over the room listing on purpose, declare it with the return type the library expects:  `bool describe()`, returning true only when it printed the object's paragraph.

> ***Note:** The library's properties are listed with their binding (§23.3); the compiler doesn't check a program's member against the library's expectations.*

## K.2 An `IdentifyGlkObject` Returning True Stops Window Recovery

<!-- doctest: skip -->
```bgl
int IdentifyGlkObject(int phase, int type, int ref, int rock) {
    if (phase == 2) { redrawPanels(); return 1; }
    return 0;
}
```

After a restore, the `<glulxWindow>` windows aren't reopened or closed, and printing to one which Glk closed is a fatal error.

The standard library calls the program's `IdentifyGlkObject` first, and calls its extensions' only when that returns false; the binding's call to `bgl.ui.recoverWindows()` (§21.10) is one of those extensions.  Returning true in phase 2 tells the library the program has handled everything.

Return `0` from phase 2, or call `bgl.ui.recoverWindows()` yourself before returning true.

