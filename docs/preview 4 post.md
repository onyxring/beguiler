
This preview release of Beguiler includes a handful of language features and bug fixes.  

---
## 1. Inline, automatically typed objects
An expansion of the last update, which brought inline declaration of objects with known types, ***this release*** now allows those objects to be *anonymously* typed.  That is, you can declare an object instance inline, and let the compiler synthesize the type for you via the `auto` pseudo type:

```bgl
object player {
    auto stats = {          // no named class, beguiler creates the type
        int strength = 10;
        int agility  = 7;
        void sayStatTot(){ print(strength + agility);}
    }
}
```

The above is equivalent to...

``` bgl
object statsObj {          // named class
    int strength = 10;
    int agility  = 7;
    void sayStatTot(){ print(strength + agility);}
}
    
object player {
    object stats = statsObj;
}
```

### This makes declaring getters and setters easier
If you are familiar with C#, you've probably heard of getters and setters.  Since Beguile's support for this pattern is built on overloaded operators, adding getters/setters to objects can seem like more trouble than its worth.  The new, inline, automatically-typed syntax simplifies things:

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

The above creates a `level` property object with getters and setters implemented using the normal cast and assignment operators.  This lets you run arbitrary code when accessing or writing to the property:

```bgl
gadget.level = 4;        // setter: _raw = 4 * 3
int a = gadget.level;    // getter: a = 12
```

---
## 2.  New operator: `:=`   (The reference-binding operator)  
This release introduces a new operator which allows you to change the object instance a `ref` member actually references at runtime:

```
object holder { ref inventory pack; }   // no instance created

inventory spare;

holder.pack := spare;     // holder.pack and spare now point to the same object
```

Unlike the assignment operator (`=`), which assigns a *value* to an object, the `:=`  operator binds the reference to a different object altogether.  Additionally, the `:=` operator cannot be redefined.

---
## 3. New operator: `<=>` (The spaceship operator)
The `<=>` operator, also called the three-way comparison operator, rolls the `<`, `>`, and `==` operators into one.  The canonical behavior of `<=>` is to compare two values and return -1 if the first is less than the second, 0 if they are the same, and 1 if the first is greater:  

```bgl
class Item {
    int weight;
    int operator <=> (Item o) { if(weight < o.weight) return -1;
                                if(weight > o.weight) return  1; return 0; }
}
```

This convention is commonly used for sorting.  In fact, if you are defining a class that needs to be sortable in an `array`, you'll want to define the spaceship.  Without it, `sort()` falls back to comparing addresses. 

---
## 4. `children` lets you place a room's contents in one line
Prior to this update, objects needed to place themselves in the world model using their `parent` property. Now a container can alternatively declare its contents in the `children` property, so you can populate a room in a single declaration:

```bgl
object table {}
object chair {}
object kitchen {
    children = { table, chair };
}
```

The `children` and `parent` properties represent the same information from two perspectives; changing one at runtime changes the other.  

Unlike I6's statement with the same name, `children` is a collection and you can do collection-like things with it: 

```bgl
for (object o in kitchen.children) { o.give(seen); }

bowl.children += { apple, pear };     // moves both into the bowl
```

---
## 5. `#declare` — persistent, order-independent `#define`
This new directive, `#declare`, is similar to `#define`.  _Like_ `#define`, you can create compile time symbols and test them using `#if`.  _Unlike_ `#define`, `#declare`d symbols are scoped to the entire compilation.  That is, they can be tested even before their declarations appear in code.  Additionally, once specified, they are permanent and cannot subsequently be changed; attempting to `#undef` or  `#redef` a `#declare`d symbol will raise a compile time error.

```bgl
// a core file, parsed early:
#if I6_STANDARD_LIBRARY
    // use something the standard library provides
#endif
#if SNAP_VAL
    // won't be compiled because defined later
#endif

// the standard-library binding, even if #included later, sets the symbol...
#declare I6_STANDARD_LIBRARY

// SNAP_VAL will only be visible after this point
#define  SNAP_VAL 
```

The `#if` above sees `I6_STANDARD_LIBRARY` even though it is `#declare`d afterward.  It would _not_ see `SNAP_VAL` unless referenced later.

---
## 6.  Magic strings are dead.  Long live string objects 
Beguile Preview 1 gave you basic I6 strings by default, then silently translated them into mutable string objects when the `<string>` language extension was included.  Although nifty, this practice hid technical realities which rightly should be visible to the developer.  With this release,`strings` no longer ***magically*** transform into buffer-backed objects.  Instead, the `<string>` extension introduces mutable strings as a separate `stringObj` type, which you choose intentionally.  

Although complementary, and designed to work together, `string` and `stringObj` are different:

- **`string`** is a pointer to static text, the packed literal address, exactly what I6 provides. 
- **`stringObj`** is an object ***which owns a buffer***. Assignable and changeable, it's the type of `string` you get in other languages.
 
```bgl
string  title = "Cloak";        // a pointer to static text
stringObj name = title;         // copies the TEXT into name's own buffer
name = name + " of Darkness!";  // name is "Cloak of Darkness!"; title unchanged 
```

---
## Simplified build-it-yourself 

**If you are building Beguiler yourself,** there are now reduced C++ requirements.  Previous versions of the source used idioms from the C++ 20 standard, making brand-new compilers a requirement.  These dependencies have been factored out, bring this requirement down to C++ 17 and opening up the process to older versions of the C++ compiler.

Additionally, I've endeavored to simplify and standardize the command used to build **beguiler**.  The README reflects the latest commandline.
