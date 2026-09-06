
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
## Updated Documentation

- Platform build instructions now spell out the **required compiler flags**, fixing a build failure some people hit compiling the Beguiler from source (notably on Windows/clang).
