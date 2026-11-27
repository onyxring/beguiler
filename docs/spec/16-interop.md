# 16 Inform 6 Interoperability

<!-- toc -->
- [16.1 Compilation Modes](#161-compilation-modes)
  - [16.1.1 Default Mode](#1611-default-mode)
  - [16.1.2 Precompiler Mode](#1612-precompiler-mode)
  - [16.1.3 Islands and Nesting](#1613-islands-and-nesting)
- [16.2 I6 Islands](#162-i6-islands)
- [16.3 Beguile Islands](#163-beguile-islands)
  - [16.3.1 In-routine Islands](#1631-in-routine-islands)
  - [16.3.2 File-scope Islands](#1632-file-scope-islands)
  - [16.3.3 Loose Identifier Mode](#1633-loose-identifier-mode)
- [16.4 `extern` Declarations](#164-extern-declarations)
  - [16.4.1 Extern Functions and `default` Stubs](#1641-extern-functions-and-default-stubs)
  - [16.4.2 Extern Variables, Constants, Attributes, Properties, Verbs and Enums](#1642-extern-variables-constants-attributes-properties-verbs-and-enums)
  - [16.4.3 Extern Classes](#1643-extern-classes)
  - [16.4.4 Extern Objects](#1644-extern-objects)
- [16.5 Including I6 Source](#165-including-i6-source)
- [16.6 Replacing I6 Library Routines](#166-replacing-i6-library-routines)
- [16.7 `superposed`](#167-superposed)
- [16.8 `_bglGlobalDeclaration`](#168-bglglobaldeclaration)
- [16.9 I6 Reserved Words and Name Collisions](#169-i6-reserved-words-and-name-collisions)
- [16.10 Third-party I6 Libraries and Raw Arrays](#1610-third-party-i6-libraries-and-raw-arrays)
<!-- /toc -->

Every Beguile program is translated to Inform 6 and then compiled by the Inform 6 compiler.  In this chapter we cover the constructs which cross between the two languages.  Emitter bodies, raw I6 inlined at each call site, are the primary path to I6 capabilities with no Beguile syntax; they are covered with emitters (§7), not here.

## 16.1 Compilation Modes

The compiler operates in one of two modes, chosen by the extension of the entry file.  Both produce an I6 program, can use every Beguile feature, and load the Beguile Language Runtime.

| Mode | Entry file | Host language | Guest language, reached through |
|---|---|---|---|
| Default mode | `.bgl` | Beguile | I6, via I6 islands (§16.2) |
| Precompiler mode | `.inf` | Inform 6 | Beguile, via Beguile islands (§16.3) |

### 16.1.1 Default Mode

In default mode the source file is Beguile, and raw I6 can be reached through `#i6` islands for anything with no Beguile equivalent.  The `bgl` namespace is imported implicitly.

**Example**

```bgl
class Room : object {
    string short_name;
}

Room foyer { short_name = "Foyer"; }

#i6 {
    [ DebugDump x ;
        objectloop(x ofclass $i6Name(Room)) print (name) x, "^";
    ];
}
```

> ***Note:** Beguile emits the class `Room` as `_bglClass_Room` (§16.9), so an island names it with `$i6Name(Room)`, which expands to the emitted identifier, rather than with the bare source name.*

### 16.1.2 Precompiler Mode

In precompiler mode the source file is I6.  The whole file is raw I6 text passed to the I6 compiler, and Beguile is reached through file-scope islands (`#bgl`, `#bglDecl`, `#bglStmt`) and in-routine islands (`#bgl` inside a routine).  These rules apply only in this mode:

- **ICL header.**  The compiler synthesizes none.  The file's own `!%` lines pass through verbatim and must be its first lines, with no blank line between them.  They are the sole authority, and the target is read from them (`-G` selects Glulx; `-z` selects the Z-machine, version 5; `-v5` or `-v8` select an explicit Z-machine version).  A `-G` or `-z5`/`-z8` option on the command line, or a `target` in a `#beguilerSettings` block, is still accepted; however, the `!%` value is what Inform 6 will see.
- **Runtime initialization.**  `#startup` blocks (§14.4.1) run inside `bglInit()`, which is declared but not called; your program must call `bglInit()` itself, from `Main` or `Initialise`, or through a library binding which does so.
- **`#using bgl` is required.**  The `bgl` namespace is not imported implicitly.  Write `#using bgl;` inside a Beguile island and it will apply to every later Beguile island in the file.
- **No-island fast path.**  A file with no Beguile islands is simply passed through unchanged.

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

**See also** §17.8, §18.8.

### 16.1.3 Islands and Nesting

An island is a region of one language embedded in a stream of the other, named by its content, not its host:  an **I6 island** is raw I6 inside Beguile, and a **Beguile island** is Beguile inside I6.  Islands nest to any depth:  `#i6 { #bgl { #i6 { #bgl { … } } } }` is valid in default mode, as is the symmetric pattern in precompiler mode.  A nested island inherits the identifier-resolution rules of the outermost Beguile island containing it (§16.3.3).

## 16.2 I6 Islands

**Syntax**

```syntax
#i6 RAW-I6-TO-END-OF-LINE
#i6 {
    RAW-I6
}
```

An I6 island injects raw I6 into the generated program, at the directive's source position.  The body is not parsed or type-checked:  the compiler tracks only braces, string literals (`"…"`), and word or character literals (`'…'`), just far enough to find the closing brace of the block form.  The single-line form takes everything to the end of the line.

Two substitution tokens are recognized in the body, the same two an emitter body uses to reach a Beguile declaration:  `$i6Name(PATH)` (§7.3.3) for the identifier Inform 6 knows a declaration by, and `$i6Expr(EXPRESSION)` (§7.3.4) for the I6 a Beguile expression emits.  An island has no receiver and no parameters, so no other `$` token means anything in one; inside a routine, the payload of `$i6Expr` resolves against that routine's scope and can name its locals.  Everything else passes through untouched, including I6's own `$` hexadecimal and `$$` binary literals (only the exact `$i6Name(` and `$i6Expr(` forms are claimed).

To run Beguile *statements* in an island, use `#bgl` (§16.3.1), the only one of the three which needs the block form.

§18.7 covers the ordering guarantees around classes and instances.

**Example**

<!-- doctest: skip -->
```bgl
#i6 Constant DEBUG_FLAG = 1;

#i6 {
    [ MyRoutine x; print "hello ", x, "^"; ];
    Object foo "Foo Object" with description "An item.";
}

void tick(){
    int n = 5;
    #i6 n = $i6Expr(bump(n));            ! one expression, over a Beguile local
    #i6 {
        n = $i6Name(bump)(n);            ! the routine's emitted name
        if(n > $ff) n = 0;               ! `$ff` is an I6 hex literal, left alone
    }
}
```

**See also** §7.3.3, §7.3.4, §16.3.1 (`#bgl`), §14.5.1, §18.7.

## 16.3 Beguile Islands

### 16.3.1 In-routine Islands

**Syntax**

```syntax
#bgl STATEMENT ;
#bgl { STATEMENT ; … }
```

An in-routine island switches back to Beguile from inside raw I6, whether that I6 is an I6 island or the host stream of a precompiler-mode file.  The body is a sequence of Beguile statements in code-block scope, translated in place.  You can write assignments, expressions, method and function calls, control flow, and any construct which translates to inline statements.  You can't write variable declarations (locals belong in the enclosing I6 routine's local list) or function, class, enum, or other declarations.  `#bglDecl` inside a routine is a compile-time error; `#bglStmt` is the explicit form of a statement island there.

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

### 16.3.2 File-scope Islands

**Syntax**

```syntax
#bgl { DECLARATIONS | STATEMENTS }
#bglDecl { DECLARATIONS }
#bglStmt { STATEMENTS }
```

At the top level of a precompiler-mode file, a Beguile island can hold declarations (classes, enums, globals, functions) or statements.  `#bgl` chooses by its content.  `#bglDecl` accepts declarations only and `#bglStmt` statements only; content of the other kind is a compile-time error.  Declarations made in a file-scope island are visible to later islands in the file and, by their emitted names, to the I6 host stream.

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

**See also** §14.5.2, §16.1.2.

### 16.3.3 Loose Identifier Mode

Identifier resolution inside every Beguile island is *loose*:  an identifier not declared in Beguile is passed through verbatim to the I6 program and resolved by the I6 compiler.  This applies to bare identifiers, dotted method calls, and function calls, and it propagates into every sub-parse of the island, including the bodies of methods declared in a file-scope island.  Identifiers in loose mode are not checked; a typo will surface as an I6 error.

Argument type checking is relaxed in the same places:  when a call's name resolves to exactly one Beguile function or method by name and arity, the call binds even if the argument types don't match, and the arguments are passed as written.

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

## 16.4 `extern` Declarations

`extern` declares that a name is defined in I6.  The compiler registers the name and its type for compile-time checking, but emits no definition.  The forms are:

| Form | Covered in |
|------|--------------|
| `extern TYPE NAME ( PARAMS ) ;` | §16.4.1 |
| `extern TYPE NAME ;` / `extern const TYPE NAME ;` | §16.4.2, §3.5 |
| `extern attribute NAME ;` / `extern property NAME ;` | §16.4.2, §11.6, §11.7.1 |
| `extern verb NAME { … }` | §16.4.2, §13.2.3 |
| `extern enum NAME { … }` / `extern bnum NAME { … }` | §16.4.2, §2.7.4 |
| `extern class NAME { … }` | §16.4.3, §8.2.2 |
| `extern object NAME ;` / `extern object NAME { … }` | §11.11 |

An `extern` declaration names the I6 identifier itself; an `asBgl NAME` clause gives it a different Beguile name (§3.11).

### 16.4.1 Extern Functions and `default` Stubs

**Syntax**

```syntax
extern TYPE NAME ( PARAMS ) ;
extern default TYPE NAME ( PARAMS ) ;
```

This declares a function implemented in I6.  The declaration is a bodyless signature terminated by `;`; a body, even an empty `{}`, is a compile-time error.  The implementation is not checked, and a non-`void` extern function needs no return.

Since `extern` asserts that the routine already exists, a plain Beguile definition of the same name is a compile-time error.  The exception is `extern default`, which marks an I6 library *stub* (`Stub` in I6):  a weak default the library expects your program to override.  A plain definition of an `extern default` function supplants the stub without `replace`.  To replace a strongly defined library routine, use `replace` (§16.6).

**Example**

```bgl
extern void ClearScreen();
extern int  ChooseObjects(var obj, var code);

extern default void Epilogue();          // library stub
void Epilogue() { print("The End.^"); }  // overrides it; no replace needed
```

> ***Note:** I6 code which keeps an array it was given must pin it first (§12.6).*

**See also** §6.1, §6.5, §12.6.

### 16.4.2 Extern Variables, Constants, Attributes, Properties, Verbs and Enums

See §3.5 (variables and constants), §11.6 (attributes), §11.7.1 (properties), §13.2.3 (verbs), and §2.7.4 (enums and bnums).

### 16.4.3 Extern Classes

**Syntax**

```syntax
extern class NAME [ : BASE [ , BASE … ] ] { MEMBERS }
extern class NAME[] ;
extern class NAME[] { MEMBERS }
extern emitter class NAME [ : BASE [ , BASE … ] ] { MEMBERS }
```

The `[]` after the name is literal:  the pooled-class marker.

An extern class is implemented in I6.  The declaration serves type-checking and emitter dispatch; no I6 class is generated.  Its form and member syntax are those of §8.1 and §8.3, and its member rules (emitters, `static` methods, member variables, the marker form) those of §8.2.2.  On the I6 side:

- A non-emitter method must be `static`; it has a body and emits as a free routine (§8.2.2).
- A parameter's name can be omitted, as in any parameter list (§6.3).
- In the marker form `extern class Name[];`, the class is pooled in I6, with a pool size the I6 declaration owns (§8.2.6).

**Example**

<!-- doctest: skip -->
```bgl
extern class object {
    parentProp parent;
    attributeList attributes;
    emitter void give(attribute attr){ give $val $attr }
    emitter eBool has(attribute attr){ $val has $attr }
}

extern class string {
    emitter void show(){ print (string) $val }
}
```

**See also** §7.2, §8.2.2, §8.2.5, §8.2.6.

### 16.4.4 Extern Objects

An I6-defined object is declared `extern object NAME;`, or with a body which types its members; both forms are covered in §11.11.  For a reusable type, use an `extern class` (§16.4.3) with an `extern const` instance (§3.5).

## 16.5 Including I6 Source

See §14.1.5.

## 16.6 Replacing I6 Library Routines

**Syntax**

```syntax
replace TYPE NAME ( PARAMS ) { … }
```

`replace` on a global function whose predecessor is an `extern` function (§16.4.1) replaces the I6 library routine of that name.  The `replace` qualifier itself, and `replaced()` for calling the original from the new body, are covered in §6.5.  For an `extern` predecessor the compiler hands the name over with I6's `Replace` directive, placed ahead of every include so that it always precedes the library which defines the routine.

I6 allows only one `Replace` per routine, so if another library already replaces it, the I6 stage will report an error.  You can then disable the other replacement, override through that library's own hook mechanism, or change only the lines needed with a patch (§14.1.6).

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

## 16.7 `superposed`

`superposed` is a declaration qualifier; its rule is in §3.12 and its emission behavior in §18.9.

## 16.8 `_bglGlobalDeclaration`

**Syntax**

```syntax
class NAME : object {
    emitter void _bglGlobalDeclaration() { RAW-I6 }
}
```

`_bglGlobalDeclaration` is an emitter hook (§7).  A class can define an emitter of that name, and its body will be injected as a top-level I6 declaration once for every object instance of the class.  The emitter is inherited:  an instance of a subclass gets the declaration too, and a subclass which defines its own `_bglGlobalDeclaration` uses that one for its instances instead.

The body is raw I6 and takes the standard substitution tokens (§7.3, Appendix G); one additional token is available here:

| Token | Expands to |
|-------|-----------|
| `$selfsub` | The instance's name with `sub` appended (for `examine`, `examinesub`) |

**Example**

```bgl
class counter : object {
    int count = 0;
    emitter void _bglGlobalDeclaration() {
        [ $selfsub;                     ! one routine per instance: stepssub, clickssub, …
            $self.count++;
            return $self.count;
        ];
    }
}
counter steps  {}
counter clicks {}
```

**See also** §7.3, Appendix G.

## 16.9 I6 Reserved Words and Name Collisions

Beguile's own reserved words are listed in Appendix A; those marked I6-significant reach the generated program verbatim.  Inform 6 reserves many more words which Beguile does not, and they matter in two situations.

**Directives with no Beguile form.**  `Abbreviate`, `Zcharacter`, `Dictionary`, `Fake_action`, `Lowstring`, `Stub`, `Trace`, `System_file`, and the like have no Beguile keyword.  They are reached through raw I6:  an I6 island (§16.2) or `#includeI6` (§16.5) will pass them to the I6 compiler untouched.

```bgl
#i6 {
    Abbreviate "the ";
}
```

**Reserved words in generated names.**  A Beguile name which reaches the generated program verbatim (an object, function, global, or `extern` name) must not be an Inform 6 reserved word (i.e., a directive, statement, condition keyword, or built-in identifier of I6); the I6 stage will reject it even though Beguile accepted it.  To keep such a name, give the declaration an explicit I6 name with an `asI6` clause (§3.11).  The Inform 6 Designer's Manual holds the authoritative, version-current list of reserved words.

**Emitted names.**  In the generated Inform 6, a class declared in Beguile is named `_bglClass_NAME`, which keeps it clear of Inform 6's other names.  An `extern` class keeps the Inform 6 name it binds, and a name which already begins with `_bgl` is reserved and is emitted unchanged.  I6 code that names a Beguile class reaches it through `$i6Name` (§7.3.3).

Members share Inform 6's namespace too, and one rule decides which name moves, regardless of who declares the two.  A routine, global variable, array, or object named like a member property is emitted as `_g_NAME`, and the member keeps its name.  A member moves instead (`_m_NAME`) only when the other name is fixed:  an `extern` declaration, a routine which replaces an `extern` one, a name chosen with `asI6` (§3.11), an Inform 6 operator word, a system function, a class, or `main`.  A method named like an Inform 6 statement keyword (`print`) keeps its name.  Every Beguile use follows the moved name.  I6 code, in an emitter body or an `#i6` block, names a moved global or member through `$i6Name` (§7.3.3); naming a moved global by its bare name there is a compile-time error.

## 16.10 Third-party I6 Libraries and Raw Arrays

See §12.8.2.
