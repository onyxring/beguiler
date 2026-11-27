# 10 Namespaces

<!-- toc -->
- [10.1 Namespace-Scoped Types](#101-namespace-scoped-types)
- [10.2 Value Aliases](#102-value-aliases)
- [10.3 Alias Members on Emitter Classes](#103-alias-members-on-emitter-classes)
- [10.4 `#using`](#104-using)
- [10.5 Global Emitter Aliases](#105-global-emitter-aliases)
<!-- /toc -->

A **namespace** is a container reached by a dotted path:  `lib.gfx.window`, `bgl.util.math.pow(2, 8)`.  Beguile has no namespace keyword:  an ordinary object (§11) or an emitter class (§8.2.3) serves as the container, and the names under it (types, values, and further namespaces) are its members.

An emitter namespace (§7.7) is a related but simpler construct:  it groups emitters under a name which is not a type, is called by name only, and is not composed with the forms of this chapter.  The root namespace of the runtime, `bgl` (§21.3), is an object of the kind we cover here.

## 10.1 Namespace-Scoped Types

**Syntax**

```syntax
alias NAME for TYPE ;
```

To reach a type through a dotted path, declare it at top level and alias it onto a namespace object with `alias name for Type;` in the object's body.  Classes and enums can be aliased, and a dotted type path is accepted wherever a type name is.  Inside the object's own body its type aliases can also be named bare, as a nested type is inside the C# type which holds it.

When the aliased type is an enum or bnum, one further segment names a value of it, and the path is resolved as the enum value itself.  When it is a class, the path goes on into the class's static members as the class's name does (`ns.part.made`, `ns.part.twice(3)`).

**Example**

```bgl
class  gfxWindow : object { int handle = 0; }
enum   eGfxPlacement { above, below, left, right }

object _gfx {
    alias window    for gfxWindow;
    alias placement for eGfxPlacement;
}
object lib { }
extend lib { auto gfx = _gfx; }

lib.gfx.window w;                                   // declaration
void open(lib.gfx.window win){ }                    // parameter
eGfxPlacement where = lib.gfx.placement.above;      // enum value
```

## 10.2 Value Aliases

**Syntax**

```syntax
auto  NAME = TARGET ;
alias NAME = TARGET ;
```

A namespace member can also alias a *value* (an object or a class), which makes its members reachable through the path.  The two forms differ in whether a runtime member exists on the host:

- `auto name = obj` binds a runtime member whose value is `obj`.  `host.name` is a first-class value, and `obj` (with everything it references) will always be present in the program.
- `alias name = Target` is compile-time only:  `host.name.m()` resolves to `Target.m()` and no member exists on the host.  Since `Target` is referenced only where the alias is used, pairing it with a `superposed` target (§3.12) gives a namespace which costs nothing until it is called.  `Target` can be a class; `alias asm = bglOpCodes` is equivalent to the alias-member form `emitter auto asm = bglOpCodes;` (§10.3), a compile-time redirect with no I6 backing.

`TARGET` can be a path through other namespace members (`alias o = root.ops;`); it names the object or class which the path reaches.

Only the above forms make a path step a namespace step.  An ordinary member whose declared value happens to name an object (e.g., `object door { room target = hallway; }`) is storage:  `door.target` reads the member at run time and can be assigned.

| Form | Target | Runtime member | Use |
|---|---|---|---|
| `alias name for Type` | class or enum | no | Namespace-scoped types (§10.1) |
| `alias name = Target` | object or class | no | Compile-time value alias; gates a `superposed` target on use |
| `auto name = obj` | object | yes | When `host.name` must be a runtime value |
| `Type name;` in an emitter class | class | no | Alias member (§10.3) |
| `emitter auto name = Type;` | class | no | Alias member, in any class or object body (§10.3) |

**Example**

```bgl
superposed object _world { array<object> getAll(){ … } }
object lib { }
extend lib { alias world = _world; }
// A program that never writes lib.world contains neither _world nor its routines.
```

## 10.3 Alias Members on Emitter Classes

**Syntax**

```syntax
emitter class HOST { CLASS NAME ; }
emitter auto NAME = CLASS ;
```

An emitter class can declare **alias members**:  typed references to other classes, declared as `TypeName name;` and resolved at compile time.  The second form, `emitter auto name = TypeName;`, names the class in an initializer instead, and can appear in any class or object body.  We use them to compose a root namespace which delegates to sub-namespaces.

- The member type must be a declared class, emitter or normal.
- The typed form takes no initializer.
- The typed form is an alias member only on an emitter class; on an object it declares an ordinary member.  An object uses `emitter auto` or a value alias (§10.2) instead.
- Aliases chain:  `a.b.c.method()` resolves through any number of hops.
- `const` and `inline` apply as they do on any member (§8.3.2, §8.3.5).

**Example**

```bgl
emitter class libStrings {
    void banner { _orStr_banner() }
    int  count  { _orStr_count }
}
emitter class lib { }
extend class lib { libStrings strings; }

lib.strings.banner;                 // -> libStrings.banner
int c = lib.strings.count;

#using lib
strings.banner;                     // -> lib.strings -> libStrings.banner
```

## 10.4 `#using`

**Syntax**

```syntax
#using NAME [ .NAME ] … [ ; ]
```

`#using` imports the members of the named class or object into the current file's scope, which lets you reference them without qualification.  A dotted path names a member class or object of the first name (`bgl.glulx`).  `#using` controls only how much of the namespace path can be omitted; it declares nothing.

**File scope.**  The import is active from the directive to the end of the file.  It does not cross `#include` in either direction:  an included file will not see the includer's imports, and a `#using` in an included file will not affect the includer.

**Priority.**  Imported names form the lowest tier of identifier resolution (§3.8.3); they rank below locals, parameters, class and object members, and globals.  A global with the same name as an imported member wins, with a warning.  A verb doesn't compete with an imported member at the head of a path (`stock.count`) or in a call (`Show(5)`).  If two imports declare a member with the same name, using that name unqualified is a compile-time error; qualify it.

**Target.**  The target must be a class or object which is already declared when the directive is read.  If it is not yet declared, the directive is ignored with a warning and never takes effect, even when a later file declares the target (§18.3).  What an import contributes depends on the kind of target:

| Target | Imported | Resolution |
|---|---|---|
| Emitter class | Value emitters, emitter functions | Expanded at the use site |
| Object | Methods, properties, type aliases | Through the object path |
| Class with static members | Static variables and static methods | As a static member reference |

`#using` a regular class which has only non-static instance members is a compile-time error, since instance methods require a receiver.

**Alias imports.**  Type aliases declared in the target (§10.1) are imported as bare type names, and a partial path resolves from the imported point:  with `#using bgl.glulx`, `window` resolves to the aliased class; with `#using bgl`, `glulx.window` resolves through the partial path.

**Example**

<!-- doctest: glulx -->
```bgl
emitter class myPlatform { int wordsize { WORDSIZE } }
#using myPlatform
void Main() { int ws = wordsize; }    // myPlatform.wordsize

#include <glulxWindow>
#using bgl.glulx
window myWin;                         // resolves to glulxWindow
```

> ***Note:** In default mode `#using bgl` is implicit; however, in precompiler mode it must be written inside a Beguile island (§16.1.2).*

**See also** §3.8.3 (the resolution tiers); §16.1.2 (precompiler mode); §21.3 (the `bgl` namespace); Appendix B (the directive index).

## 10.5 Global Emitter Aliases

**Syntax**

```syntax
alias NAME = OBJECT [ .MEMBER … ] .NAME ;
```

At file scope, `alias` can make an object's emitters callable without their path.  Every overload of `NAME` on the object can then be called as a global function of that name, including overloads which a later `extend` of the object adds.  An overload which an `extend … { replace … }` replaces is replaced for the global name too.

- The object path follows the rules of value aliases (§10.2).
- The alias keeps the member's name.
- Only emitters can be aliased.  An emitter has no routine of its own, so the global name and the member are the same definition.  An object's ordinary method is a compile-time error here.
- A global function of the same name, declared anywhere, joins the same overload set.

The print family is declared this way:  `print`, `printLine`, and `printName` live in `bgl.output` (§21.4) and are global through aliases.

**Example**

```bgl
superposed object _report {
    emitter void note(int v){ print "#", $v; }
}
alias note = _report.note;
extend _report {
    emitter void note(string v){ print (string)$v; }
}

void Main(){
    note(3);          // _report.note(int)
    note("done");     // _report.note(string), added after the alias
}
```

**See also** §10.2 (value aliases); §21.4 (`bgl.output`).
