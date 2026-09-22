# Appendix H Glossary

Terms of art used throughout this specification, each defined once here. Each entry points to the
section with the full treatment.

## H.1 Terms

- **additive property** — A property whose contributions from a class and its subclasses and instances accumulate into one contiguous run of words instead of overriding one another; declared `additive property name;`. Every contribution must be a `rawArray<T>` or a routine. `name` is additive in the Inform 6 compiler itself; every other additive property is declared by a library binding. See §11.7.2, §23.3.5.
- **anchor** — In verb priority, the priority declared in a verb's own body (default 10), against which `extend` contributions sort. See §13.2.5.
- **`auto { … }` accessor** — An inline property accessor: the braces hold a class body (backing members plus the get and set operators) and the compiler synthesizes a hidden class for it. See §9.9.2.
- **Beguiler** — The Beguile compiler, which transpiles Beguile source to Inform 6 and then invokes the Inform 6 compiler. See §16.1.
- **`#bglDecl` / `#bglStmt`** — The file-scope island forms of precompiler mode that accept only declarations or only statements, respectively; `#bgl` accepts both. See §14.5.2, §15.3.2.
- **binding** — A BLR declaration layer that exposes one external Inform 6 library's symbols to Beguile as typed `extern` declarations and integrates it with the runtime core. See §23.1.
- **BLR (Beguile Language Runtime)** — The library of Beguile source every program compiles against: the auto-loaded core, the opt-in extensions, and the IF library bindings. See §21.1, §22.1, §23.1.
- **bnum** — A bit-flag enumeration: its values are powers of two, starting at `1` and doubling, so that they can be combined with `|`. See §2.7.2.
- **byVal** — A class qualifier marking a class as value-semantic for parameter passing: a `byVal class` argument is copied into the callee through the class's `operator =`, so the callee's changes do not reach the caller's instance. See §8.2.7.
- **claimed word** — A dictionary word that a verb, native or `extern`, has declared as one of its trigger words. A grammar line whose trigger word is already claimed extends that verb instead of declaring a new one. See §13.2.3.
- **class form** — One of the seven kinds of class declaration (normal, `extern`, `emitter`, `alias`, veneer, pooled, `byVal`); the form fixes what instances of the class exist and which members it may declare. See §8.2.
- **declaration** — A construct that introduces a named type, variable, constant, function, object, verb or grammar. See §3.1.
- **default mode** — The normal compilation mode: the entry file is a `.bgl` file, and I6 is reached through `#i6` islands. Contrast **precompiler mode**. See §15.1.1.
- **directive** — A `#name` instruction to the compiler (`#include`, `#define`, `#i6`, …); directives are not statements. See §14, Appendix B.
- **eBool** — The enum `{ true, false }` that the comparison and logical operators and `operator ?()` return; Beguile's boolean-result type, which interoperates with `bool`. See §2.2.
- **emitter** — A function, method or operator whose body is raw Inform 6 that the compiler inlines at each call site instead of emitting a routine call. See §7.1.
- **emitter body** — The raw I6 text of an emitter, containing substitution tokens. See §7.1, §15.2.
- **emitter class** — A class whose instances have no Inform 6 object backing; it serves as a type label and a host for emitter members. See §8.2.3.
- **emitter namespace** — A group of emitters declared under one name without `class` (`emitter name { … }`) and called as `name.member(…)`; the name is not a type. See §7.7.
- **emitter value** — An emitter declared without a parameter list: a typed inline expansion used by bare name, without `()`. See §7.6.
- **ephemeral** — A returned local array or a string produced by a string operation, whose backing storage is reclaimed before the caller can hold it; it must be captured, by assigning it to a typed local, to persist. See §12.6, §22.3.
- **eType** — The enum returned by `typeof(v)`: the machine category of a value (`unknown`, `int`, `string`, `routine`, `object`, `class`). See §2.8.1, §21.5.9.
- **evict** — To remove a dictionary word from an `extern` verb entirely, with the word-level form `extend V { grammar -= { {.w} }; }`; a native verb that also declares the word reclaims it, otherwise the word is disabled. See §13.5.2.
- **expression** — Operands joined by operators, resolving to a value with a static type. See §4.1.
- **extension** — An opt-in BLR file enabled with `#include <name>`, as opposed to the core, which is always loaded. See §22.1.
- **frame pool** — The global pool of slots into which a routine on the Z-machine spills local variables beyond the machine's per-routine limit; sized by the `framePoolSize` setting. See §18.10.
- **function** — A Beguile-declared callable, which compiles to an I6 routine. Contrast **routine**. See §6.1.
- **grammar line** — One pattern the player may type, declared on a verb or in a grammar object: a trigger word followed by pattern tokens. See §13.4.
- **grammar object** — A standalone `grammar` declaration holding `grammarRule` entries that name their verbs explicitly, as opposed to grammar declared on the verb itself. See §13.4.5.
- **`.i6b` template** — A built-in Inform 6 template, shipped with the runtime core, from which the compiler emits constructs that have no fixed I6 form until it knows how they are used (the frame pool, `for`-in loops, the literal-list scratch buffer). It is not `#include`d. See §18.9.
- **island** — A region of one language embedded in a file written in the other: an `#i6` island is raw Inform 6 inside a Beguile file; a `#bgl` island is Beguile inside an Inform 6 file. See §15.2, §15.3.
- **loose identifier mode** — The relaxed name resolution used inside `#bgl` islands and throughout precompiler mode, where an unresolved identifier passes through to Inform 6 rather than raising an error. See §15.3.3.
- **materialize** — To make a `superposed` declaration part of the program, which happens the first time its name is referenced. See §3.12, §18.9.
- **member** — Any named part of a class or object: a variable, method, operator or emitter. Beguile makes no property-versus-field distinction. See §8.3.
- **meta verb** — A verb with `meta = true`: an out-of-world action that runs without advancing the turn counter or triggering daemons. See §13.2.4.
- **named union** — A `union Name = A | B { … }` declaration: a union type with a name and a place to hang members. See §2.8.2.
- **object-backed class** — A class whose instances are Inform 6 objects with storage of their own (a normal class, including a pooled one), as opposed to an emitter, alias or veneer class, whose instances are bare words or do not exist. See §8.2, §11.3.2.
- **outer** — Inside a property accessor body, the host object the accessor is declared on, as distinct from `self`, the accessor instance itself; resolved at compile time and usable to read and write the host's other members. See §9.9.3.
- **owned member** — A member whose type is a value class with stored members, declared without an initializer; each instance of the enclosing type gets its own backing instance, so the member is a live object rather than a bare slot. See §8.3.4.
- **pass-through conversion** — A conversion operator declared without a body (`emitter T operator ();`): the value is left unchanged and merely retyped. See §2.12, §9.4.
- **pooled class** — A class declared `class Name[N]`, which reserves `N` statically allocated instances that `new` and `delete` hand out and reclaim. See §8.2.6, §4.13.
- **precompiler mode** — The compilation mode where the entry file is an `.inf` (Inform 6) file and Beguile is reached through `#bgl` islands. Contrast **default mode**. See §15.1.2.
- **primary trigger** — The first dictionary word in a verb's first grammar line; the word an `extend V { grammar += … }` targets. See §13.2.3.
- **program** — The set of source files compiled together to produce one story file. See §3.1.
- **property accessor** — A member that reads and writes like a plain member but runs code on each access: a value class declaring a getter `operator ()` and a setter `operator =`, used as an owned member of the host class or object. See §9.9.
- **proxy member** — A member that represents a relation over its owner rather than storing a value, such as `children` on `object`; it has no slot of its own, and its emitter methods act on the owner through `$host`. See §7.3, §11.5.
- **pseudo-type** — The compile-time type of a literal (`intLiteral`, `stringLiteral`, …), inferred by the compiler and never written by the author; it takes part in overload and operator resolution separately from the runtime type it corresponds to. See §2.4.
- **resolved type** — The static type the compiler assigns to an expression, which drives operator resolution, type checking and emitter dispatch. See §4.1.
- **routine** — An I6 callable. A Beguile **function** compiles to a routine. See §6.1.
- **size vs. length** — For a Beguile array, `size()` is the capacity reserved at compile time; `length()` is the runtime count of in-use elements. See §12.3.
- **source file** — A file the compiler reads: a `.bgl` file, or an `.inf` file in precompiler mode. See §3.1, §15.1.
- **statement** — An executable unit inside a function body, ending in `;` or a `{ }` block. See §5.1.
- **static instance** — An instance declared at file scope (`Name m;`, or an object declaration) and allocated once for the program, as opposed to a pooled instance obtained with `new` or an instance local to a routine. See §8.2.6, §11.2.
- **`#storedEmitFirst` / `#storedEmitLast`** — A named, deferred raw-I6 block that is emitted only when a built-in I6 template whose `triggers` clause names it is applied. See §14.4.4, §18.9.
- **story file** — The executable output of a build (`.z5`, `.z8` or `.ulx`), run by a Z-machine or Glulx interpreter. See §20.2.
- **stringObj** — The `<string>` extension's owning text type: a slot that owns a buffer from the string pool and can be changed in place, as opposed to `string`, a slot referring to static text. See §22.3.
- **substitution token** — A `$`-prefixed placeholder in an emitter body (`$self`, `$val`, `$target`, `$⟨parameter⟩`) replaced at the call site. See §7.3, Appendix G.
- **superposed** — A declaration qualifier marking a routine, global, object or class that is emitted only if something references it. See §3.12, §18.9.
- **target** — The virtual machine a story file runs on: Z-machine or Glulx. See §17.3.
- **tracked / untracked array** — A tracked array carries a runtime length word; an untracked array (a raw Inform 6 array, or a `rawArray` view) does not. See §12.3, §12.8.
- **tracked buf** — With `<buf>` included, a sized `array<char>` that records its capacity and current length; its value behaves as an Inform 6 hybrid buffer. See §22.2.
- **trigger word** — The dictionary word that begins a grammar line and selects its verb; a verb's first trigger word is its primary trigger. See §13.2.3, §13.4.
- **typesealed** — A member qualifier that locks the member's type: a subclass or instance may re-initialize the member but not re-declare it with another type. See §8.2.8.
- **value class** — A class that does not derive from `_bglObject` (and so not from `object`): a variable of the type holds the members themselves, is zero-initialized at routine entry, and is copied on assignment through the class's `operator =`. Contrast a class with reference semantics, whose variable holds an identity. See §2.10, §8.2.1, §21.5.8.
- **veneer class** — A class declared `extern emitter class X : _bglObject`: a distinct Beguile type with no representation of its own, whose runtime value is the bare word it wraps; it adds a type and behavior but no storage. `int`, `char`, `uint` and `glulxImage` are veneer classes. See §8.2.5.
- **word** — The machine's native integer unit: 16 bits on the Z-machine, 32 on Glulx. Every scalar value, reference and `array<T>` element other than an `array<char>` element occupies one word. See §2.2, Appendix J.

## H.2 Symbols

- **`$self` / `$val` / `$target` / `$host` / `$prop`** — Substitution tokens used inside emitter bodies: `$self` is the receiver (in an operator or assignment emitter on a member, the member's owner), `$val` the full receiver expression, `$target` an assignment's left-hand side, `$host` the object a proxy member is accessed on, and `$prop` an array property's name. See §7.3, Appendix G.
- **`$opref(op)`** — A lookup, not a substitution: it yields a reference to the receiver type's operator `op` — the routine for a `static` operator, the property for an instance operator, `0` when the type publishes none — so that a shared runtime routine can apply a type's operation. See §7.3.1, §12.10, Appendix G.
- **`_bglObject`** — The root base class of the BLR, from which `object`, the primitive wrappers and the IF-domain types derive. Deriving from it gives a class with stored members reference semantics (§2.10). See §8.2.5, §21.5.8.
- **`_bgl…` / `bgl…`** — Identifier prefixes reserved for the runtime and for compiler-generated symbols. See §1.4.
