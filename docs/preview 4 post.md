
This build adds a handful of bug fixes and language features.

---
## 1. Inline, automatically typed objects
An expansion of the last update, which brought inline declaration of objects with a known type, this release now allows those objects to be be _untyped_.  That is, you can declare an object instance *inline*, and the compiler will synthesizes the type for you via the `auto` pseudo type:

```bgl
object player {
    auto stats = {          // no named class, beguiler creates the type
        int strength = 10;
        int agility  = 7;
        void sayStatTot(){ print(strength + agility);}
    }
}

void main() {
    print(player.stats.strength);   // 10
    player.stats.agility = 12;      // read/write to members, as normal
    player.sayStatTot();            // 22
}
```

## Made easier : Getters and setters
If you are familiar with C#, you've probably heard of getters and setters.  Beguile’s support for these is as wrapped objects with overloaded operators.  The new inline, automatically typed object syntax makes these easier to implement:

```bgl
object gadget {
    int scale = 3;
    auto level = {
        int _raw = 0;
        int  operator()        { return _raw; }
        void operator = (int v){ _raw = v * outer.scale; }
    }
}
```

The above creates a `level` property object with getters and setters implemented with normal cast and assignment operators.  This let's you run arbitrary code when accessing or writing to the property:

```bgl
gadget.level = 4;        // setter: _raw = 4 * 3
int a = gadget.level;    // getter: a = 12
```

---

## 2. Class-typed members are now real instances
A member whose type is a class is now a live, self-contained instance you can use directly.  It is automatically instantiated, so you can call its methods and access its fields, instead of staying a bare `0` slot which you have to allocate and wire up yourself:

```bgl
class inventory {
    int gold   = 0;
    int arrows = 12;
    void add(int n){ gold = gold + n; }
}

object player { inventory pack; }       // `pack` is automatically created

player.pack.add(50);
player.pack.arrows = player.pack.arrows - 3;
```

---
## 3. `children` — place a room's contents in one line
Prior to this update, objects needed to place themselves in the world model using the `parent` property. Now a container can name its contents with `children`, so you can populate a room in a single declaration:

```bgl
object table {}
object chair {}
object kitchen {
    children = { table, chair };
}
```

`children` is a collection: you can iterate it with `for`, read `.length()` for the count:

```bgl
for (object o in kitchen.children) { o.give(seen); }
```

You can also move objects at runtime with `+=`
```bgl
bowl.children += { apple, pear };     // moves both into the bowl
```

The is above is the same as...

```bgl
apple.parent = bowl;
pear.parent = bowl;
```

Declaration conflicts between `parent` and `children` properties will throw a compile time error.

---
## 4. `#declare` — persistent, order-independent `#define`
This release provides a new directive, `#declare`, which is similar to `#define`.  _Like_ `#define`, you can create compile time symbols with values and test those values using `#if`.  _Unlike_ `#define`, `#declare`d symbols are scoped to the entire compilation.  They can be tested even before their declarations appear in code.  Additionally, once `#define`d they are permanent and cannot subsequently be changed; attempting to `#undef` or  `#redef` a `#declare`d symbol will raise a compile time error.

```bgl
// a core file, parsed early:
#if I6_STANDARD_LIBRARY;
    // use something the standard library provides
#else;
    // self-contained fallback
#endif;

// the standard-library binding, even if #included later would set...
#declare I6_STANDARD_LIBRARY
```

The `#if` above sees `I6_STANDARD_LIBRARY` even though it is `#declare`d afterward.  It would _not_ see it if instead it were `#define`d.  So `#define` for ordinary constants and toggles; use `#declare` when one file needs to detect a feature or binding that may (or may not) be pulled in elsewhere.

---
## 5. `string` and `stringObj` — two types, because they are two things
I6 gives you one thing when you write `"hello"`: an address. You can print it, but comparing two of them compares addresses, and changing one isn't possible at all. Beguile used to blur that by letting `string` mean both the literal and a mutable buffer. It no longer does.

- **`string`** is a pointer to static text — the packed literal address, exactly what an I6 dev expects. Free, immutable, nothing owned.
- **`stringObj`** is a slot that *owns* a buffer. Mutable, and it allocates and frees itself.

Both print and compare by **content**, so `==`, `<` and `switch` behave the way you'd want regardless of which you're holding, and the two mix freely. The rule is simply: anything that *produces* text hands you a `stringObj`.

```bgl
string  title = "Cloak";        // a pointer to static text
stringObj name;
name = title;                   // copies the TEXT into name's own buffer
name = name + " of Darkness";
name += "!";                    // name is "Cloak of Darkness!"; title unchanged
```

Assignment copies, so `b = a` gives `b` its own buffer — changing `b` later leaves `a` alone. That's the opposite of a bare pointer assignment in I6, and it's deliberate.

---
## 6. Containers that manage their own elements
An `array<stringObj>` now allocates and releases the text in its slots for you. Write text, not allocations — dropping an element or leaving scope frees the buffer:

```bgl
array<stringObj> names[3];
names += "pear";
names += "apple";
names += "fig";
names.sort();                   // apple fig pear — ordered by content, not address
```

Every way in goes through the same door — `names[0] = "plum"`, `append`, `prepend`, `insert` — and every way out (`remove`, `removeValue`, `-=`, `clear`, and simply leaving the routine) releases what it held.

---
## 7. Sorting your own types: `operator <=>`
The three-way comparison operator tells generic code how to order your type. Give a class one and `sort()` just works:

```bgl
class Item {
    int weight;
    int operator <=> (Item o) { if(weight < o.weight) return -1;
                                if(weight > o.weight) return  1; return 0; }
}

bag.sort();                     // ordered by weight
```

`<=>` is no longer required to be `static` — it has the same freedom as `<` or `>`, and can take the left operand as its receiver like any other instance operator.

**If you don't define one, Beguile now tells you.** Without an ordering, sorting a class falls back to comparing the underlying words — which for an object means its *address*, i.e. wherever I6 happened to place it. That's deterministic, unrelated to any field, and looks like it worked. So it's a compile-time warning rather than a silent surprise:

```
warning: 'Item' publishes no 'operator <=>', so 'sort()' falls back to word semantics
— for a class that means comparing object addresses, not values.
```

Nothing warns for `int`, `char` or `object`, where the word *is* the value and the default ordering is correct. Passing your own comparator never warns either.

The same idea covers searching: `indexOf`, `contains` and `removeValue` use your `operator ==` when you define one, and fall back to identity when you don't — which is usually what you want for objects, so that one stays quiet.

---
## 8. Referring to an operator directly
An operator you've already defined can be named and passed around, instead of being re-wrapped in a lambda:

```bgl
array<string> words[3];
words.sort(string::operator <=>);           // no wrapper lambda

func<int, string, string> cmp = string::operator <=>;
```

If a type has several overloads, name the operand type to pick one: `Money::operator ==(int)`.

---
## 9. Properties as values you can actually use
Beguile could already give you a property as a value with `(property) name`, but there was nothing you could do with it. Now you can dereference one against a receiver — reads and method calls both:

```bgl
var p = (property) weight;
int w = axe.p;                  // reads the property p names

var m = (property) describe;
axe.m();                        // message send — binds self, as I6 does
```

Which property is read is decided at runtime by the value in `p`, so this is the I6 `obj.(prop)` idiom, expressible in Beguile at last.

---
## Updated Documentation

- **Building from source is now one command line on every platform** — `make`, or a single `c++ -std=c++20 -O2 ... *.cpp` invocation. The three per-platform lines are gone, along with the `-D` renames they carried (`isnumber`, `strncasecmp`, `popen`/`pclose`), which are handled in the source where they belong. The stock `clang++` on macOS previously could not build Beguiler at all — a `constexpr std::string` needed a newer standard library than Xcode ships — so the requirement is now simply a C++20 toolchain with `<format>`.
- The **language spec** documents `stringObj`, what `array<T>` asks of an element type, and the operator-reference forms.
- **Beguile for the I6 Developer** covers the `string` / `stringObj` split and arrays of your own classes.
