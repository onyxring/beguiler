# Appendix K Gotchas

Behavior that follows from the rules but tends to surprise. Each entry shows the code, what happens, why, and
how to get what was meant. Every entry is specified in the section it names; this appendix adds no rules.

## K.1 A Method Named Like a Library Property Is That Property

<!-- doctest: skip -->
```bgl
class Card : object {
    stringObj Describe() { return "a card"; }
}
Card cardA { "red card"; parent = roomA; }
```

`cardA` no longer appears in the room's "You can see …" list, and nothing warns.

Beguile names are case-insensitive, so `Describe` is `describe`, and a member named like a library property keeps
that name (§8.1): the method becomes the object's `describe` property. The standard library calls `describe` when
it lists an object in a room and treats a non-zero result as "the object described itself; don't list it"
(§23.3.11). The method returns a string, which is never zero.

Give the method a name of its own (`Details()`), or, to take over the room listing on purpose, declare it with the
return type the library expects: `bool describe()`, returning true only when it printed the object's paragraph.

> **Which names.** The library's properties are listed with their binding (§23.3); the compiler does not check a
> program's member against the library's expectations, since that knowledge belongs to the library.

## K.2 An Array Member Assigned in a Copy Operator Is Shared

<!-- doctest: skip -->
```bgl
struct Tally {
    array<int> items[4];
    int hits;
    Tally operator = (Tally o) { items = o.items; hits = o.hits; return self; }
}
Tally a;
Tally b;
b = a;
b.items += 7;
```

`a.items` gains the 7 as well: after `b = a`, the two tallies are separate instances that share one array.

A struct is copied through its own copy operator (§8.2.7), and each line of that operator is an ordinary
assignment. An array is a reference type (§12.5), so `items = o.items` makes `b.items` refer to `a.items`'s array,
just as copying a pointer member in a C++ copy constructor makes a shallow copy. `hits` is copied because `int` is a
value type.

To give each instance its own elements, copy them: `items.copyFrom(o.items);`.
