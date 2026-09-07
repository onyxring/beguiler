
This build adds a handful of bug fixes and language features.

---
## 1. Inline, automatically typed objects
An expansion of the last update, which brought inline declaration of objects with known types, ***this release*** now allows those objects to be be _untyped_.  That is, you can declare an object instance *inline*, and the compiler synthesizes the type for you via the `auto` pseudo type:

```bgl
object player {
    auto stats = {          // no named class, beguiler creates the type
        int strength = 10;
        int agility  = 7;
        void sayStatTot(){ print(strength + agility);}
    }
}
```
The above is equivilant to...

```
object statsObj {          // no named class, beguiler creates the type
    int strength = 10;
    int agility  = 7;
    void sayStatTot(){ print(strength + agility);}
}
    
object player {
    object stats = statsObj;
}
```

### Made easier : Getters and setters
If you are familiar with C#, you've probably heard of getters and setters.  Since Beguile's support for this pattern is built on overloaded operators, adding getters/setters to objects can seem like more trouble than its worth.  This new inline, automatically-typed syntax simplifies things:

```bgl
object gadget {
    int scale = 3;
    auto level = {  //getter and setter
        int _raw = 0;
        int  operator()        { return _raw; }
        void operator = (int v){ _raw = v * outer.scale; } 
    }
}
```

The above creates a `level` property object with getters and setters implemented using the normal cast and assignment operators.  This let's you run arbitrary code when accessing or writing to the property:

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

If you'd rather the member point at an instance owned somewhere else, declare it `ref` — the slot is then left empty for you to fill:

```bgl
object holder { ref inventory pack; }   // no instance created

holder.pack := spare;                   // bind: both names now refer to the same inventory
```

`:=` is the **reference binding operator** — **rebinding** when the slot already holds one. It is a separate operator because a class can overload `=` for copying, which leaves no way to spell "point at this." `=` keeps its meaning exactly — it copies, through `operator =` when the type defines one — and `:=` binds the reference without dispatching it. Both sides must be the same class, so it stays a reference binding rather than a hole in the type system.

A reference does two different things, so they read differently. `:=` rebinds; `=` assigns *through* the reference into whatever it points at:

```bgl
stringObj first;  first = "original";
ref stringObj r := first;

r = "value";       // assigns through — `first` is now "value"
r := other;        // rebinds — `first` keeps its value
```

That means a `ref` slot otherwise behaves exactly like the thing it points at; only rebinding is spelled differently, because only rebinding is a different operation. Binding uses `:=` everywhere, declaration included — `ref Box r = …` is an error, and so is `:=` on a slot that owns its instance.

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
This release provides a new directive, `#declare`, which is similar to `#define`.  _Like_ `#define`, you can create compile time symbols and test them using `#if`.  _Unlike_ `#define`, `#declare`d symbols are scoped to the entire compilation.  That is, they can be tested even before their declarations appear in code.  Additionally, once `#define`d they are permanent and cannot subsequently be changed; attempting to `#undef` or  `#redef` a `#declare`d symbol will raise a compile time error.

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
## Fixes

- **`trimRight()` trimmed the left.** It called the buffer's `trimLeft` — a defect carried over verbatim from orLibrary's `orString.h`, where the same line is one word wrong. `trim()` was unaffected, so it only showed on a direct `trimRight()` call. Fixed in both Beguile and orLibrary.
- **`indexOf` / `contains` now search the elements an array *holds*.** They scanned the full allocated capacity, so a value sitting in an unused slot past `length()` could be "found". They now cover `0 .. length()-1`, matching `removeValue`, `sort` and `first`/`last`. `clear()` still zeroes the whole capacity.
- **`sort()` uses your type's ordering.** It previously always compared underlying words; it now routes through `operator <=>` when the element type publishes one — which is what makes sorting `<string>` and `<float>` arrays correct rather than address- or bit-ordered.
- **Overloaded operators emitted invalid I6.** Two overloads of one operator mangled to the same name, so the generated code named the same routine or property twice and Inform 6 rejected it. Both the `static` and instance forms are now distinguished by parameter type.
- **Comparing against a numeric literal missed an overload.** `money == 100` reported no matching operator while `money == someInt` matched, because an `intLiteral` was not widened to `int` during overload resolution.

---
## Updated Documentation

- **Building from source is now one command line on every platform** — `make`, or a single `c++ -std=c++17 -O2 ... *.cpp` invocation. The three per-platform lines are gone, along with the `-D` renames they carried (`isnumber`, `strncasecmp`, `popen`/`pclose`), which are handled in the source where they belong. The stock `clang++` on macOS previously could not build Beguiler at all — a `constexpr std::string` needed a newer standard library than Xcode ships. **The requirement is now C++17**, down from C++20: `std::format` was the only thing needing the newer standard, and since not one call site used a format specifier, the positional substitution it was doing is now a few lines in `helpers.h`. That takes the floor from GCC 13 / LLVM 17 to GCC 7 / LLVM 5.
- The **language spec** documents `stringObj`, what `array<T>` asks of an element type, and the operator-reference forms.
- **Beguile for the I6 Developer** covers the `string` / `stringObj` split and arrays of your own classes.
- The **`ref` sections** of the spec are rewritten: a `ref` slot is now bound with `:=`, is not auto-instantiated, and assigns through with `=`.
