# 10 Namespaces

<!-- toc -->
- [10.1 Namespace-Scoped Types](#101-namespace-scoped-types)
- [10.2 Value Aliases](#102-value-aliases)
- [10.3 Alias Members on Emitter Classes](#103-alias-members-on-emitter-classes)
- [10.4 `#using`](#104-using)
<!-- /toc -->

A **namespace** is a container reached by a dotted path: `lib.gfx.window`, `bgl.util.math.pow(2, 8)`.
Beguile has no namespace keyword. An ordinary object (§11) or an emitter class (§8.2.3) serves as
the container, and the names under it — types, values and further namespaces — are its members. A
type is placed in a namespace with `alias name for Type;` (§10.1); an object or class is placed there
as a value with `auto` or `alias` (§10.2); an emitter class composes sub-namespaces through alias
members (§10.3). `#using` (§10.4) imports a namespace, or a branch of one, so that its members may be
written without the path.

An emitter namespace (§7.7) is a related but simpler construct: it groups emitters under a name that
is not a type, is called by name only, and is not composed with the forms of this chapter. The root
namespace of the runtime, `bgl` (§21.3), is an object of the kind this chapter describes.

## 10.1 Namespace-Scoped Types

**Syntax**

```syntax
alias ⟨name⟩ for ⟨type⟩ ;
```

This form appears inside an object body.

**Description**

A type may be reached through a dotted path on an object. The type is declared at top level and then
aliased onto a namespace object with `alias name for Type;` in the object's body; the `for` keyword
distinguishes a type alias from an instance member (`auto x = y;`). Classes and enums may be aliased.
A dotted type path is accepted wherever a type name is: variable declarations, parameters, return
types, and globals.

When the aliased type is an enum or bnum, one further segment names a value of it; the path is
resolved as the enum value itself. A path ending at a class alias cannot continue into static or
member access.

Type aliases may be imported with `#using` (§10.4), which shortens the path a use site must write.

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
int where = lib.gfx.placement.above;                // enum value
```

## 10.2 Value Aliases

**Syntax**

```syntax
auto  ⟨name⟩ = ⟨target⟩ ;
alias ⟨name⟩ = ⟨target⟩ ;
```

**Description**

A namespace member may also alias a *value* — an object or a class — so that its members are
reachable through the path. The two forms differ in whether a runtime member exists on the host:

- `auto name = obj` binds a runtime member whose value is `obj`. `host.name` is a first-class value,
  and `obj` (with everything it references) is always present in the program.
- `alias name = Target` is compile-time only: `host.name.m()` resolves to `Target.m()` and no member
  exists on the host. `Target` is referenced only where the alias is used, so paired with a
  `superposed` target (§3.12) the namespace costs nothing until it is called. `Target` may be a class;
  `alias asm = bglOpCodes` is the shorthand for the alias-member form `emitter auto asm = bglOpCodes;`, a
  compile-time redirect with no I6 backing.

| Form | Target | Runtime member | Use |
|---|---|---|---|
| `alias name for Type` | class or enum | no | Namespace-scoped types (§10.1) |
| `alias name = Target` | object or class | no | Compile-time value alias; gates a `superposed` target on use |
| `auto name = obj` | object | yes | When `host.name` must be a runtime value |
| `Type name;` in an emitter class | class | no | Alias member (§10.3) |

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
emitter class ⟨host⟩ { ⟨class⟩ ⟨name⟩ ; }
```

**Description**

An emitter class may declare **alias members**: typed references to other classes, declared as
`TypeName name;` with no initializer, resolved at compile time. They compose a root namespace that
delegates to sub-namespaces, and combine with `#using` (§10.4).

- The member type must be a declared class, emitter or normal.
- No initializer is permitted.
- Alias members are valid only on emitter classes; an object uses value aliases (§10.2) instead.
- Aliases chain: `a.b.c.method()` resolves through any number of hops.

**Example**

```bgl
emitter class libStrings {
    void banner { _orStr_banner() }
    int  count  { _orStr_count }
}
emitter class lib { }
extend class lib { libStrings strings; }

lib.strings.banner;                 // → libStrings.banner
int c = lib.strings.count;

#using lib
strings.banner;                     // → lib.strings → libStrings.banner
```

## 10.4 `#using`

**Syntax**

```syntax
#using ⟨name⟩ [ .⟨name⟩ ] … [ ; ]
```

**Description**

`#using` imports the members of the named class or object into the current file's scope, so that
they may be referenced without qualification. A dotted path names a member class or object of the
first name (`bgl.glulx`). `#using` controls only how much of the namespace path may be omitted; it
declares nothing.

**File scope.** The import is active from the directive to the end of the file. It does not cross
`#include` in either direction: an included file does not see the includer's imports, and a `#using`
in an included file does not affect the includer.

**Priority.** Imported names form the lowest tier of identifier resolution (§3.8.3): they rank below
locals, parameters, class and object members and globals. A global with the same name as an imported
member wins, with a warning. If two imports declare a member with the same name, using that name
unqualified is a compile-time error; qualify it.

**Target.** The target must be a class or object that is already declared when the directive is read.
If it is not yet declared, the directive is ignored with a warning and never takes effect, even when a
later file declares the target (§18.3). What an import contributes depends on the kind of target:

| Target | Imported | Resolution |
|---|---|---|
| Emitter class | Value emitters, emitter functions | Expanded at the use site |
| Object | Methods, properties, type aliases | Through the object path |
| Static members | Static variables | As a static member reference |

`#using` a regular class that has only non-static instance members is a compile-time error; instance
methods require a receiver.

**Alias imports.** Type aliases declared in the target (§10.1) are imported as bare type names, and a
partial path resolves from the imported point: with `#using bgl.glulx`, `window` resolves to the
aliased class; with `#using bgl`, `glulx.window` resolves through the partial path.

**Example**

```bgl
emitter class myPlatform { int wordsize { WORDSIZE } }
#using myPlatform
void Main() { int ws = wordsize; }    // myPlatform.wordsize

#using bgl.glulx
window myWin;                         // resolves to glulxWindow
```

**Notes**

In default mode `#using bgl` is implicit; in precompiler mode it must be written inside a Beguile
island (§15.1.2).

**See also** §3.8.3 — the resolution tiers; §15.1.2 — precompiler mode; §21.3 — the `bgl` namespace;
Appendix B — the directive index.
