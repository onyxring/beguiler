# 15 Inform 6 Interoperability

<!-- toc -->
- [15.1 Compilation Modes](#151-compilation-modes)
  - [15.1.1 Default Mode](#1511-default-mode)
  - [15.1.2 Precompiler Mode](#1512-precompiler-mode)
  - [15.1.3 Islands and Nesting](#1513-islands-and-nesting)
- [15.2 I6 Islands](#152-i6-islands)
- [15.3 Beguile Islands](#153-beguile-islands)
  - [15.3.1 In-routine Islands](#1531-in-routine-islands)
  - [15.3.2 File-scope Islands](#1532-file-scope-islands)
  - [15.3.3 Loose Identifier Mode](#1533-loose-identifier-mode)
- [15.4 `extern` Declarations](#154-extern-declarations)
  - [15.4.1 Extern Functions and `default` Stubs](#1541-extern-functions-and-default-stubs)
  - [15.4.2 Extern Variables, Constants, Attributes, Properties, Verbs and Enums](#1542-extern-variables-constants-attributes-properties-verbs-and-enums)
  - [15.4.3 Extern Classes](#1543-extern-classes)
  - [15.4.4 Extern Objects](#1544-extern-objects)
- [15.5 Including I6 Source](#155-including-i6-source)
- [15.6 Replacing I6 Library Routines](#156-replacing-i6-library-routines)
- [15.7 `superposed`](#157-superposed)
- [15.8 `_bglGlobalDeclaration`](#158-bglglobaldeclaration)
- [15.9 I6 Reserved Words and Name Collisions](#159-i6-reserved-words-and-name-collisions)
- [15.10 Third-party I6 Libraries and Raw Arrays](#1510-third-party-i6-libraries-and-raw-arrays)
<!-- /toc -->


Every Beguile program is translated to Inform 6 and then compiled by the Inform 6 compiler. This
chapter specifies the constructs that cross between the two languages: the two compilation modes and
their islands, `extern` declarations that make I6-defined names visible to Beguile, replacement of I6
library routines, `superposed` declarations, per-instance I6 injection, and the naming rules imposed
by the I6 stage. Emitter bodies (§7), which are raw I6 inlined at each call site, are the primary
path to I6 capabilities that have no Beguile syntax and are specified with emitters, not here.

## 15.1 Compilation Modes

The compiler operates in one of two modes, chosen by the extension of the entry file. Both produce an
I6 program, both can use every Beguile feature, and the Beguile Language Runtime is loaded in both.
They differ in which language owns the file and how the other is reached.

| Mode | Entry file | Host language | Guest language, reached through |
|---|---|---|---|
| Default mode | `.bgl` | Beguile | I6, via I6 islands (§15.2) |
| Precompiler mode | `.inf` | Inform 6 | Beguile, via Beguile islands (§15.3) |

### 15.1.1 Default Mode

**Description**

The source file is Beguile. Raw I6 is reachable through `#i6` islands for anything that has no
Beguile equivalent. The `bgl` namespace is implicitly imported.

**Example**

```bgl
class Room : object {
    string short_name;
}

Room foyer { short_name = "Foyer"; }

#i6 {
    [ DebugDump x ;
        objectloop(x ofclass Room) print (name) x, "^";
    ];
}
```

### 15.1.2 Precompiler Mode

**Description**

The source file is I6. The whole file is raw I6 text passed to the I6 compiler, and Beguile is
reached through file-scope Beguile islands (`#bgl`, `#bglDecl`, `#bglStmt`) and in-routine Beguile
islands (`#bgl` inside a routine). The following rules apply only in this mode:

- **ICL header.** The compiler does not synthesize an ICL header. The file's own `!%` lines are
  passed through verbatim and must be the first lines of the file, with no blank line between them.
- **Runtime initialization.** `#startup` blocks (§14.4.1) run inside `bglInit()`, which is declared
  but not called; the program must call `bglInit()` itself, from `Main` or `Initialise`, or through
  a library binding that does so.
- **`#using bgl` is required.** The `bgl` namespace is not imported implicitly. Write `#using bgl;`
  inside a Beguile island; it then applies to every later Beguile island in the file.
- **No-island fast path.** A file with no Beguile islands is passed through unchanged.

**Example**

```i6
!% -G

[ Main ;
    print "Welcome.^";
    bglInit();
    Initialise();
];

#bgl {
    class magicButton[5] : object {
        int strength = 0;
        void create(int s) { strength = s; }
    }
}

[ Initialise ;
    #bgl { new magicButton(10); }
];
```

**See also** §16.8, §18.8.

### 15.1.3 Islands and Nesting

An island is a region of one language embedded in a stream of the other. Islands are named by their
content, not their host: an **I6 island** is raw I6 inside Beguile, a **Beguile island** is Beguile
inside I6. Islands nest to any depth: `#i6 { #bgl { #i6 { #bgl { … } } } }` is valid in default mode
and the symmetric pattern in precompiler mode. A nested island inherits the identifier-resolution
rules of the outermost Beguile island that contains it (§15.3.3).

## 15.2 I6 Islands

**Syntax**

```syntax
#i6 ⟨raw I6 to end of line⟩
#i6 {
    ⟨raw I6⟩
}
```

**Description**

Injects raw I6 at the directive's source position in the generated program. The body is not parsed,
type-checked or modified: the compiler tracks only braces, string literals (`"…"`) and word or
character literals (`'…'`), far enough to find the closing brace of the block form. The single-line
form takes everything to the end of the line.

An I6 island is placed in source order relative to the surrounding declarations; the ordering
guarantees around classes and instances are in §18.7.

**Example**

```bgl
#i6 Constant DEBUG_FLAG = 1;

#i6 {
    [ MyRoutine x; print "hello ", x, "^"; ];
    Object foo "Foo Object" with description "An item.";
}
```

**See also** §14.5.1, §18.7.

## 15.3 Beguile Islands

### 15.3.1 In-routine Islands

**Syntax**

```syntax
#bgl ⟨statement⟩ ;
#bgl { ⟨statement⟩ ; … }
```

**Description**

Switches back to Beguile from inside raw I6, whether the I6 is an I6 island or the host stream of a
precompiler-mode file. The body is a sequence of Beguile statements in code-block scope and is
translated in place. Permitted: assignments, expressions, method and function calls, control flow,
and any construct that translates to inline statements. Not permitted: variable declarations (locals
belong in the enclosing I6 routine's local list) and function, class, enum or other declarations.

Argument type checking is relaxed for calls resolved inside a Beguile island: when a name resolves
to exactly one Beguile function or method by name and arity, the call binds even if the argument
types do not match, and the arguments are passed as written.

**Example**

```bgl
#i6 {
    [ MyRoutine x;
        print "I6 prologue^";
        #bgl {
            x = bgl.glulx.window.getRoot();
            invoke(x);
        }
        print "I6 epilogue^";
    ];
}
```

**See also** §14.5.2.

### 15.3.2 File-scope Islands

**Syntax**

```syntax
#bgl { ⟨declarations⟩ | ⟨statements⟩ }
#bglDecl { ⟨declarations⟩ }
#bglStmt { ⟨statements⟩ }
```

**Description**

At the top level of a precompiler-mode file, a Beguile island may hold declarations (classes, enums,
globals, functions) or statements. `#bgl` chooses by its content. `#bglDecl` accepts declarations
only and `#bglStmt` statements only; content of the other kind is a compile-time error. Declarations
made in a file-scope island are visible to later islands in the file and to the I6 host stream by
their emitted names.

**Example**

```i6
Object  RedSpell "redspell";
Constant redtangent = 7;

#bgl {
    class wand : object {
        void cast() {
            switch(action) {
                case redtangent: RedSpell.cast(player, 5);
            }
        }
    }
}
```

**See also** §14.5.2, §15.1.2.

### 15.3.3 Loose Identifier Mode

**Description**

Identifier resolution inside every Beguile island is *loose*: an identifier that is not declared in
Beguile is passed through verbatim to the I6 program and resolved by the I6 compiler. This applies to
bare identifiers, dotted method calls and function calls, and it propagates into every sub-parse of
the island, including the bodies of methods declared in a file-scope island. Identifiers in loose
mode are not checked; a typo surfaces as an I6 error.

**Example**

```bgl
#i6 {
    [ MyRoutine local1 local2;
        local1 = 5;
        #bgl {
            local2 = local1 * 2;        // I6 locals, passed through
            print(bglDeclaredFunc());   // declared in Beguile, resolved normally
        }
    ];
}
```

**See also** §3.8.

## 15.4 `extern` Declarations

`extern` declares that a name is defined in I6. The compiler registers the name and its type for
compile-time checking and emits no definition for it. The forms are:

| Form | Specified in |
|------|--------------|
| `extern ⟨type⟩ ⟨name⟩ ( ⟨params⟩ ) ;` | §15.4.1 |
| `extern ⟨type⟩ ⟨name⟩ ;` / `extern const ⟨type⟩ ⟨name⟩ ;` | §15.4.2, §3.5 |
| `extern attribute ⟨name⟩ ;` / `extern property ⟨name⟩ ;` | §15.4.2, §11.6, §11.7.1 |
| `extern verb ⟨name⟩ ;` | §15.4.2, §13.2.3 |
| `extern enum ⟨name⟩ { … }` / `extern bnum ⟨name⟩ { … }` | §15.4.2, §2.7.4 |
| `extern class ⟨name⟩ { … }` | §15.4.3, §8.2.2 |
| `extern object ⟨name⟩ ;` / `extern object ⟨name⟩ { … }` | §15.4.4, §11.11 |

An `extern` declaration may carry an `as i6name` clause naming the I6 identifier (§3.11).

### 15.4.1 Extern Functions and `default` Stubs

**Syntax**

```syntax
extern ⟨type⟩ ⟨name⟩ ( ⟨params⟩ ) ;
extern default ⟨type⟩ ⟨name⟩ ( ⟨params⟩ ) ;
```

**Description**

Declares a function implemented in I6. The declaration is a bodyless signature terminated by `;`; a
body, even an empty `{}`, is a compile-time error. The implementation is not checked; a non-`void`
extern function needs no return.

Because `extern` asserts that the routine already exists, a plain Beguile definition of the same name
is a compile-time error. The exception is `extern default`, which marks an I6 library *stub* (`Stub`
in I6): a weak default the library expects the program to override. A plain definition of an
`extern default` function supplants the stub without `replace`. To replace a strongly defined
library routine, use `replace` (§15.6).

**Example**

```bgl
extern void ClearScreen();
extern int  ChooseObjects(var obj, var code);

extern default void Epilogue();          // library stub
void Epilogue() { print("The End.^"); }  // overrides it; no replace needed
```

**See also** §6.1, §6.5.

### 15.4.2 Extern Variables, Constants, Attributes, Properties, Verbs and Enums

See §3.5 (variables and constants), §11.6 (attributes), §11.7.1 (properties), §13.2.3 (verbs) and §2.7.4 (enums and bnums).

### 15.4.3 Extern Classes

**Syntax**

```syntax
extern class ⟨name⟩ [ : ⟨base⟩ ] { ⟨members⟩ }
extern class ⟨name⟩[] ;
extern class ⟨name⟩[] { ⟨members⟩ }
extern emitter class ⟨name⟩ [ : ⟨base⟩ ] { ⟨members⟩ }
```

The `[]` after the name is literal: the pooled-class marker.

**Description**

Declares a class implemented in I6. The declaration serves type-checking and emitter dispatch; no I6
class is generated. The declaration form and member syntax are those of §8.1 and §8.3; the following
rules are specific to `extern`:

- Emitter methods are permitted and require the `emitter` keyword.
- A non-emitter method is a bodyless signature; parameter names are optional and only the types are
  required (`bool contains(string);`). A non-emitter method with a body is a compile-time error.
- A member variable declaration (type and name, no initializer) is permitted and contributes to
  type inference on instances.
- A member variable with an initializer is accepted, but the value has no effect: no code is
  generated for it and it is invisible to programs.
- The marker form `extern class Name[];` declares that the class is pooled in I6, with a pool size
  the I6 declaration owns. It enables `new Name(…)` and `delete` (§8.2.6). `extern class Name[N]`
  with a size is a compile-time error.
- `extern emitter class` declares a veneer class over a primitive or value (§8.2.5).

**Example**

```bgl
extern class object {
    parentProp parent;
    attributeList attributes;
    emitter void give(attribute attr){ give $val $attr }
    emitter eBool has(attribute attr){ $val has $attr }
}

extern class string {
    bool contains(string);
}
```

**See also** §7.2, §8.2.2, §8.2.5, §8.2.6.

### 15.4.4 Extern Objects

**Syntax**

```syntax
extern object ⟨name⟩ ;
extern object ⟨name⟩ {
    ⟨type⟩ ⟨method⟩ ( ⟨params⟩ ) ;
    ⟨type⟩ ⟨property⟩ ;
    emitter ⟨type⟩ ⟨method⟩ ( ⟨params⟩ ) { … }
}
```

**Description**

Declares a single I6-defined object. The bare form is specified in §11.11. The body form additionally
declares the types of the object's members so that calls and property reads type-check:

- A method is a bodyless signature; a body on a non-emitter method is a compile-time error. Emitter
  members with a body are permitted.
- A property is typed and has no initializer; an initializer is a compile-time error.
- Default parameter values are honored for arity and overload resolution at call sites, as for any
  method (§6.3).

The object is a referenceable file-scope name, and no I6 object is generated for it. This binds a
singleton I6 object directly; an `extern class` with an `extern` instance is the form for a reusable
type.

**Example**

```bgl
extern object playerCommands {
    void pushCommand(string cmd, bool isMeta = false, bool isSilent = false);
    object interrupt;
}

playerCommands.pushCommand("say hello");
```

**See also** §11.11.

## 15.5 Including I6 Source

See §14.1.5.

## 15.6 Replacing I6 Library Routines

**Syntax**

```syntax
replace ⟨type⟩ ⟨name⟩ ( ⟨params⟩ ) { … }
```

**Description**

`replace` on a global function whose predecessor is an `extern` function (§15.4.1) replaces the I6
library routine of that name. The `replace` qualifier itself, and `replaced()` for calling the
original from the new body, are specified in §6.5. For an `extern` predecessor the compiler hands the
name over with I6's `Replace` directive, placed ahead of every include so that it always precedes the
library that defines the routine; the original remains callable through `replaced()`.

I6 permits only one `Replace` per routine. If another library already replaces the routine, the I6
stage reports an error; disable the other replacement or override through that library's own hook
mechanism.

**Example**

```bgl
extern void Banner();                  // declared by the standard-library binding

replace void Banner(){
    print("before ");
    replaced();                        // the library's original
    print(" after");
}
```

**See also** §6.5.

## 15.7 `superposed`

`superposed` is a declaration qualifier; its rule is in §3.12 and its emission behavior in §18.9. It
marks a whole declaration — a function, global, object or class — or a `static` method.

## 15.8 `_bglGlobalDeclaration`

**Syntax**

```syntax
class ⟨name⟩ : object {
    emitter void _bglGlobalDeclaration() { ⟨raw I6⟩ }
}
```

**Description**

`_bglGlobalDeclaration` is an emitter hook (§7): a class may define an emitter of that name, and its
body is injected as a top-level I6 declaration once for every object instance of the class, so a
class can generate a companion routine, array or other I6 construct that accompanies each instance.
The body is raw I6 and takes the standard substitution tokens (§7.3, Appendix G); one additional
token is available here:

| Token | Expands to |
|-------|-----------|
| `$selfsub` | The instance's name with `sub` appended (for `examine`, `examinesub`) |

**Example**

```bgl
class counter : object {
    int count = 0;
    emitter void _bglGlobalDeclaration() {
        [ $selfsub;                     // one routine per instance: stepssub, clickssub, …
            $self.count++;
            return $self.count;
        ];
    }
}
counter steps  {}
counter clicks {}
```

**See also** §7.3, Appendix G.

## 15.9 I6 Reserved Words and Name Collisions

Beguile's own reserved words are listed in Appendix A; those marked I6-significant reach the generated
program verbatim. Inform 6 reserves many more words that Beguile does not, and they matter in two
situations.

**Directives with no Beguile form.** `Abbreviate`, `Zcharacter`, `Dictionary`, `Fake_action`,
`Lowstring`, `Stub`, `Trace`, `System_file` and the like have no Beguile keyword. They are reached
through raw I6: an I6 island (§15.2) or `#includeI6` (§15.5) passes them to the I6 compiler untouched.

```bgl
#i6 {
    Abbreviate "the ";
}
```

**Reserved words in generated names.** A Beguile name that reaches the generated program verbatim
(an object, function, global or `extern` name) must not be an Inform 6 reserved word — a directive,
statement, condition keyword or built-in identifier of I6; the I6 stage rejects it even though
Beguile accepted it. Where such a name must be kept, give the declaration an explicit I6 name with an
`as` clause (§3.11). The Inform 6 Designer's Manual holds the authoritative, version-current list of
reserved words.

## 15.10 Third-party I6 Libraries and Raw Arrays

See §12.8.2.
