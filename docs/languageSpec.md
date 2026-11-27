<!-- GENERATED FILE: do not edit.
     Built from docs/spec/ by docs/spec/tools/build-spec.py.
     Edit the chapter there and re-run the script; edits here are lost on the next build. -->

# Beguile Language Specification

A reference for the Beguile language, the Beguiler compiler, and the Beguile Language Runtime.
Part I teaches the language in dependency order and can be read front to back (a rule that needs a
later construct gives a one-sentence version and a forward reference); Parts II and III and the
appendices are for lookup.


---

**Single-file build.** `docs/languageSpec.md` is this specification merged into one file, generated
by `tools/build-spec.py` from the chapters here. Edit a chapter and re-run the script; `--check`
reports whether the merged file is out of date, as `tools/build-toc.py --check` does for the tables
of contents.

## Contents

**Front matter**
- [About This Specification](#about-this-specification)
  - [Purpose and Scope](#purpose-and-scope)
  - [What This Specification Is, And Is Not](#what-this-specification-is-and-is-not)
  - [Conventions](#conventions)
    - [Typography](#typography)
    - [Syntax Notation](#syntax-notation)
    - [Examples](#examples)
    - [Notes](#notes)
    - [Target Markers](#target-markers)
    - [Cross-References](#cross-references)
    - [Entry Shape](#entry-shape)
- [Introduction to Beguile](#introduction-to-beguile)
  - [What Beguile Is](#what-beguile-is)
  - [Design Goals](#design-goals)
  - [Relationship to Inform 6](#relationship-to-inform-6)

**Part I: The Beguile Language**
- [1 Lexical Structure](#1-lexical-structure)
  - [1.1 Source Text and Encoding](#11-source-text-and-encoding)
  - [1.2 Comments](#12-comments)
  - [1.3 Case-Insensitivity](#13-case-insensitivity)
  - [1.4 Identifiers](#14-identifiers)
  - [1.5 Reserved Words](#15-reserved-words)
  - [1.6 Literals](#16-literals)
    - [1.6.1 Integer Literals](#161-integer-literals)
    - [1.6.2 Float Literals](#162-float-literals)
    - [1.6.3 String Literals](#163-string-literals)
    - [1.6.4 Raw String Literals](#164-raw-string-literals)
    - [1.6.5 Interpolated String Literals](#165-interpolated-string-literals)
    - [1.6.6 Character Literals](#166-character-literals)
    - [1.6.7 Dictionary Word Literals](#167-dictionary-word-literals)
  - [1.7 Operators](#17-operators)
  - [1.8 Directives](#18-directives)
- [2 Types and Values](#2-types-and-values)
  - [2.1 Overview](#21-overview)
  - [2.2 Primitive Types](#22-primitive-types)
  - [2.3 The `float` Type](#23-the-float-type)
  - [2.4 Literal Pseudo-Types](#24-literal-pseudo-types)
    - [2.4.1 `negativeIntLiteral`](#241-negativeintliteral)
    - [2.4.2 `interpolatedStringLiteral`](#242-interpolatedstringliteral)
  - [2.5 `null`](#25-null)
  - [2.6 The `var` Type](#26-the-var-type)
  - [2.7 Enumerations](#27-enumerations)
    - [2.7.1 `enum`](#271-enum)
    - [2.7.2 `bnum`](#272-bnum)
    - [2.7.3 Widening to `int`](#273-widening-to-int)
    - [2.7.4 `extern enum` and `extern bnum`](#274-extern-enum-and-extern-bnum)
    - [2.7.5 Naming Members](#275-naming-members)
  - [2.8 Union Types](#28-union-types)
    - [2.8.1 `typeof` and `eType`](#281-typeof-and-etype)
    - [2.8.2 Named Unions](#282-named-unions)
  - [2.9 Function Types](#29-function-types)
    - [2.9.1 `interpolatedString`](#291-interpolatedstring)
  - [2.10 Type Rules](#210-type-rules)
    - [2.10.1 Type Compatibility](#2101-type-compatibility)
    - [2.10.2 Conversion](#2102-conversion)
    - [2.10.3 Primitive, Reference, and Value Types](#2103-primitive-reference-and-value-types)
  - [2.11 `selfType`](#211-selftype)
- [3 Declarations, Variables and Scope](#3-declarations-variables-and-scope)
  - [3.1 Program Structure](#31-program-structure)
  - [3.2 Declaration Qualifiers](#32-declaration-qualifiers)
  - [3.3 Global Variables](#33-global-variables)
  - [3.4 Constants](#34-constants)
  - [3.5 Extern Variables](#35-extern-variables)
  - [3.6 Local Variables](#36-local-variables)
  - [3.7 References: `ref` and `:=`](#37-references-ref-and-)
  - [3.8 Identifier Resolution](#38-identifier-resolution)
    - [3.8.1 Local Scope](#381-local-scope)
    - [3.8.2 Class and Object Scope](#382-class-and-object-scope)
    - [3.8.3 Global Scope](#383-global-scope)
  - [3.9 The Global-Scope Qualifier `::`](#39-the-global-scope-qualifier-)
  - [3.10 Shadowing](#310-shadowing)
  - [3.11 The `asI6` and `asBgl` Clauses](#311-the-asi6-and-asbgl-clauses)
  - [3.12 `superposed`](#312-superposed)
  - [3.13 `literal`](#313-literal)
- [4 Expressions and Operators](#4-expressions-and-operators)
  - [4.1 Evaluation](#41-evaluation)
  - [4.2 Operands](#42-operands)
  - [4.3 Operator Precedence](#43-operator-precedence)
  - [4.4 Binary Operator Resolution](#44-binary-operator-resolution)
  - [4.5 Arithmetic Operators](#45-arithmetic-operators)
  - [4.6 Comparison Operators](#46-comparison-operators)
  - [4.7 Logical Operators](#47-logical-operators)
  - [4.8 Bitwise and Shift Operators](#48-bitwise-and-shift-operators)
  - [4.9 Ternary Operator](#49-ternary-operator)
  - [4.10 Optional Chaining, Null Coalescing and Postfix Query](#410-optional-chaining-null-coalescing-and-postfix-query)
  - [4.11 Casts](#411-casts)
  - [4.12 Address-of `&`](#412-address-of-)
  - [4.13 `new`](#413-new)
  - [4.14 Lambdas](#414-lambdas)
  - [4.15 Operator References](#415-operator-references)
- [5 Statements and Control Flow](#5-statements-and-control-flow)
  - [5.1 Statements](#51-statements)
  - [5.2 Block Statement](#52-block-statement)
  - [5.3 Expression Statement](#53-expression-statement)
  - [5.4 Declaration Statement](#54-declaration-statement)
  - [5.5 Assignment](#55-assignment)
  - [5.6 Compound Assignment](#56-compound-assignment)
  - [5.7 Increment and Decrement](#57-increment-and-decrement)
  - [5.8 `if` / `else`](#58-if-else)
  - [5.9 `for`](#59-for)
    - [5.9.1 `for-in`](#591-for-in)
  - [5.10 `while`](#510-while)
  - [5.11 `do` / `while` and `do` / `until`](#511-do-while-and-do-until)
  - [5.12 `switch`](#512-switch)
  - [5.13 `break` and `continue`](#513-break-and-continue)
  - [5.14 `return`, `rtrue` and `rfalse`](#514-return-rtrue-and-rfalse)
  - [5.15 `delete`](#515-delete)
  - [5.16 `try` / `catch` / `throw`](#516-try-catch-throw)
- [6 Functions](#6-functions)
  - [6.1 Function Declarations](#61-function-declarations)
  - [6.2 Return Types](#62-return-types)
  - [6.3 Parameters](#63-parameters)
  - [6.4 Overload Resolution](#64-overload-resolution)
  - [6.5 `replace` and `replaced()`](#65-replace-and-replaced)
  - [6.6 `self`](#66-self)
  - [6.7 `Main`](#67-main)
    - [6.7.1 `HandleGlkEvent`](#671-handleglkevent)
  - [6.8 Distributors](#68-distributors)
- [7 Emitters](#7-emitters)
  - [7.1 What an Emitter Is](#71-what-an-emitter-is)
  - [7.2 Emitter Functions](#72-emitter-functions)
  - [7.3 Substitution Tokens](#73-substitution-tokens)
    - [7.3.1 `$opref` and `$oprefReq`](#731-opref-and-oprefreq)
    - [7.3.2 Choosing `$self`, `$val` and `$target`](#732-choosing-self-val-and-target)
    - [7.3.3 `$i6Name`](#733-i6name)
    - [7.3.4 `$i6Expr`](#734-i6expr)
  - [7.4 Conditional Text: `##if`, `##else`, `##endif`](#74-conditional-text-if-else-endif)
  - [7.5 Global Emitters](#75-global-emitters)
  - [7.6 Emitter Values](#76-emitter-values)
  - [7.7 Emitter Namespaces](#77-emitter-namespaces)
  - [7.8 Emitter Members on Enums and Bnums](#78-emitter-members-on-enums-and-bnums)
  - [7.9 Emitters and Functions Compared](#79-emitters-and-functions-compared)
- [8 Classes](#8-classes)
  - [8.1 Class Declaration](#81-class-declaration)
    - [8.1.1 Type Parameters](#811-type-parameters)
  - [8.2 Class Forms](#82-class-forms)
    - [8.2.1 Normal Classes](#821-normal-classes)
    - [8.2.2 `extern class`](#822-extern-class)
    - [8.2.3 `emitter class`](#823-emitter-class)
    - [8.2.4 `alias class`](#824-alias-class)
    - [8.2.5 Primitive Classes (`primitive class`)](#825-primitive-classes-primitive-class)
    - [8.2.6 Pooled Classes](#826-pooled-classes)
    - [8.2.7 Structs (`struct`)](#827-structs-struct)
    - [8.2.8 Making a Reference Type Behave Like a Value Type](#828-making-a-reference-type-behave-like-a-value-type)
  - [8.3 Members](#83-members)
    - [8.3.1 Member Variables](#831-member-variables)
    - [8.3.2 `const` Members](#832-const-members)
    - [8.3.3 `static` Members](#833-static-members)
    - [8.3.4 Owned Members](#834-owned-members)
    - [8.3.5 `inline` Members](#835-inline-members)
    - [8.3.6 `required` Members](#836-required-members)
    - [8.3.7 `typesealed` Members](#837-typesealed-members)
  - [8.4 Methods](#84-methods)
  - [8.5 Lifecycle: `init` and `deinit`](#85-lifecycle-init-and-deinit)
    - [8.5.1 Held Temporaries](#851-held-temporaries)
  - [8.6 Inheritance](#86-inheritance)
  - [8.7 Extending and Replacing Members](#87-extending-and-replacing-members)
    - [8.7.1 `extend class`](#871-extend-class)
    - [8.7.2 `replace`](#872-replace)
    - [8.7.3 Shadowing and `default`](#873-shadowing-and-default)
    - [8.7.4 `hide`](#874-hide)
    - [8.7.5 Matching Rules](#875-matching-rules)
- [9 Operators and Accessors](#9-operators-and-accessors)
  - [9.1 Overloadable Operators](#91-overloadable-operators)
  - [9.2 Emitter and Non-Emitter Operators](#92-emitter-and-non-emitter-operators)
  - [9.3 Subscript: `operator []` and `operator []=`](#93-subscript-operator-and-operator-)
  - [9.4 Conversion: `operator ()`](#94-conversion-operator-)
  - [9.5 Special Operators](#95-special-operators)
    - [9.5.1 `operator auto()`](#951-operator-auto)
  - [9.6 `static` Operators and Three-Way Comparison](#96-static-operators-and-three-way-comparison)
  - [9.7 Compound Assignment and Increment Fallback](#97-compound-assignment-and-increment-fallback)
  - [9.8 Emitter-Required Operators](#98-emitter-required-operators)
  - [9.9 Property Accessors](#99-property-accessors)
    - [9.9.1 Getters and Setters](#991-getters-and-setters)
    - [9.9.2 Inline Accessors: `auto { … }`](#992-inline-accessors-auto-)
    - [9.9.3 `outer`](#993-outer)
- [10 Namespaces](#10-namespaces)
  - [10.1 Namespace-Scoped Types](#101-namespace-scoped-types)
  - [10.2 Value Aliases](#102-value-aliases)
  - [10.3 Alias Members on Emitter Classes](#103-alias-members-on-emitter-classes)
  - [10.4 `#using`](#104-using)
  - [10.5 Global Emitter Aliases](#105-global-emitter-aliases)
- [11 Objects](#11-objects)
  - [11.1 Overview](#111-overview)
  - [11.2 Declaring an Object](#112-declaring-an-object)
  - [11.3 Inline Objects: `Type{ … }`](#113-inline-objects-type-)
    - [11.3.1 Positional and Named Members](#1131-positional-and-named-members)
    - [11.3.2 Type Inference from the Target](#1132-type-inference-from-the-target)
    - [11.3.3 Inline Objects as Arguments](#1133-inline-objects-as-arguments)
    - [11.3.4 Constant and Run-time Members](#1134-constant-and-run-time-members)
    - [11.3.5 Nested Aggregates](#1135-nested-aggregates)
    - [11.3.6 Standalone Declarations](#1136-standalone-declarations)
    - [11.3.7 Members Declared with a Body](#1137-members-declared-with-a-body)
  - [11.4 Members and Type Inference](#114-members-and-type-inference)
  - [11.5 Special Members: `parent`, `children`, `attributes`, `instanceName`](#115-special-members-parent-children-attributes-instancename)
    - [11.5.1 `parent`](#1151-parent)
    - [11.5.2 `children`](#1152-children)
    - [11.5.3 `attributes`](#1153-attributes)
    - [11.5.4 `instanceName`](#1154-instancename)
  - [11.6 Attribute Declarations](#116-attribute-declarations)
  - [11.7 Property Declarations](#117-property-declarations)
    - [11.7.1 `property` and `extern property`](#1171-property-and-extern-property)
    - [11.7.2 `additive` Properties](#1172-additive-properties)
    - [11.7.3 Computed Property Access and `property` Parameters](#1173-computed-property-access-and-property-parameters)
  - [11.8 Array Members](#118-array-members)
  - [11.9 Methods](#119-methods)
    - [11.9.1 Dispatch on Object Receivers](#1191-dispatch-on-object-receivers)
    - [11.9.2 Overloads](#1192-overloads)
  - [11.10 `extend` for Objects](#1110-extend-for-objects)
  - [11.11 `extern object`](#1111-extern-object)
- [12 Arrays](#12-arrays)
  - [12.1 Overview](#121-overview)
  - [12.2 Declaring Arrays](#122-declaring-arrays)
  - [12.3 Subscripts, Size and Length](#123-subscripts-size-and-length)
  - [12.4 Byte Arrays: `array<char>`](#124-byte-arrays-arraychar)
  - [12.5 Assignment and Copy Semantics](#125-assignment-and-copy-semantics)
  - [12.6 Local Arrays, the Heap and Collection](#126-local-arrays-the-heap-and-collection)
  - [12.7 Member Arrays](#127-member-arrays)
  - [12.8 `rawArray<T>`](#128-rawarrayt)
    - [12.8.1 Raw Views](#1281-raw-views)
    - [12.8.2 File-scope `rawArray<T>` Literals](#1282-file-scope-rawarrayt-literals)
    - [12.8.3 Member `rawArray<T>`](#1283-member-rawarrayt)
  - [12.9 Arrays of Arrays](#129-arrays-of-arrays)
  - [12.10 Element Type Requirements](#1210-element-type-requirements)
    - [12.10.1 Arrays of Structs](#12101-arrays-of-structs)
  - [12.11 `extend` for Arrays](#1211-extend-for-arrays)
  - [12.12 Emitters on Arrays](#1212-emitters-on-arrays)
- [13 Dictionary Words, Verbs and Grammar](#13-dictionary-words-verbs-and-grammar)
  - [13.1 Dictionary Words](#131-dictionary-words)
  - [13.2 Verb Declarations](#132-verb-declarations)
    - [13.2.1 `handler()`](#1321-handler)
    - [13.2.2 `perform()`](#1322-perform)
    - [13.2.3 Extern Verbs and Claimed Words](#1323-extern-verbs-and-claimed-words)
    - [13.2.4 Meta Verbs](#1324-meta-verbs)
    - [13.2.5 Verb Priority](#1325-verb-priority)
  - [13.3 Action Comparisons](#133-action-comparisons)
  - [13.4 Grammar](#134-grammar)
    - [13.4.1 Grammar Types](#1341-grammar-types)
    - [13.4.2 Pattern Tokens](#1342-pattern-tokens)
    - [13.4.3 Alternation and Multi-trigger Lines](#1343-alternation-and-multi-trigger-lines)
    - [13.4.4 Line Modifiers: `REVERSE`, `withI6Synonyms`](#1344-line-modifiers-reverse-withi6synonyms)
    - [13.4.5 Grammar on Verbs and Grammar Objects](#1345-grammar-on-verbs-and-grammar-objects)
  - [13.5 Extending Grammar](#135-extending-grammar)
    - [13.5.1 `grammar +=`](#1351-grammar-)
    - [13.5.2 `grammar -=`](#1352-grammar--)
    - [13.5.3 `replace grammar =`](#1353-replace-grammar-)
    - [13.5.4 Synonyms](#1354-synonyms)
- [14 Directives](#14-directives)
  - [14.1 Source Organization](#141-source-organization)
    - [14.1.1 `#include <NAME>`](#1411-include-name)
    - [14.1.2 `#include "path"`](#1412-include-path)
    - [14.1.3 `#include @"path"`](#1413-include-path)
    - [14.1.4 `#include ?"path"` and `#include ?<NAME>`](#1414-include-path-and-include-name)
    - [14.1.5 `#includeI6`](#1415-includei6)
    - [14.1.6 `#mergeI6`](#1416-mergei6)
    - [14.1.7 `#once`](#1417-once)
    - [14.1.8 `#emitAfterI6Include`](#1418-emitafteri6include)
  - [14.2 Symbols and Conditional Compilation](#142-symbols-and-conditional-compilation)
    - [14.2.1 `#define`, `#defineI6`](#1421-define-definei6)
    - [14.2.2 `#redef` and `#undef`](#1422-redef-and-undef)
    - [14.2.3 `#declare`](#1423-declare)
    - [14.2.4 Pre-defined Symbols](#1424-pre-defined-symbols)
    - [14.2.5 `#if`, `#elif`, `#else`, `#endif`](#1425-if-elif-else-endif)
  - [14.3 Diagnostics and Control](#143-diagnostics-and-control)
    - [14.3.1 `#message`](#1431-message)
    - [14.3.2 `#warning`](#1432-warning)
    - [14.3.3 `#error`](#1433-error)
    - [14.3.4 `#exit`](#1434-exit)
  - [14.4 Raw I6 Placement](#144-raw-i6-placement)
    - [14.4.1 `#startup`](#1441-startup)
    - [14.4.2 `#emitfirst`](#1442-emitfirst)
    - [14.4.3 `#emitlast`](#1443-emitlast)
    - [14.4.4 `#storedEmitFirst` and `#storedEmitLast`](#1444-storedemitfirst-and-storedemitlast)
    - [14.4.5 `##beguilerSettings.KEY` Substitution](#1445-beguilersettingskey-substitution)
  - [14.5 Islands](#145-islands)
    - [14.5.1 `#i6`](#1451-i6)
    - [14.5.2 `#bgl`, `#bglDecl`, `#bglStmt`](#1452-bgl-bgldecl-bglstmt)
  - [14.6 Namespace Import](#146-namespace-import)
    - [14.6.1 `#using`](#1461-using)
  - [14.7 Settings](#147-settings)
    - [14.7.1 `#beguilerSettings { … }`](#1471-beguilersettings-)
    - [14.7.2 `#beguilerSettings.prop`](#1472-beguilersettingsprop)
- [15 Settings (`#beguilerSettings`)](#15-settings-beguilersettings)
  - [15.1 The Settings Block](#151-the-settings-block)
  - [15.2 Toolchain Paths](#152-toolchain-paths)
  - [15.3 Compilation Settings](#153-compilation-settings)
  - [15.4 Runtime Settings](#154-runtime-settings)
  - [15.5 Game Metadata](#155-game-metadata)
  - [15.6 Blorb Packaging](#156-blorb-packaging)
    - [15.6.1 Asset Discovery and `_blorbAssets.bgl`](#1561-asset-discovery-and-blorbassetsbgl)
    - [15.6.2 IFID Generation and Persistence](#1562-ifid-generation-and-persistence)
  - [15.7 Reading Settings From Source](#157-reading-settings-from-source)
  - [15.8 Bindings and the Library Banner Constants](#158-bindings-and-the-library-banner-constants)
  - [15.9 Declaring Settings](#159-declaring-settings)
- [16 Inform 6 Interoperability](#16-inform-6-interoperability)
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

**Part II: The Beguiler Compiler**
- [17 Invoking the Compiler](#17-invoking-the-compiler)
  - [17.1 Synopsis](#171-synopsis)
  - [17.2 Positional Arguments](#172-positional-arguments)
  - [17.3 Options](#173-options)
    - [17.3.1 Pass-through switches](#1731-pass-through-switches)
  - [17.4 Precedence Between the Command Line and `#beguilerSettings`](#174-precedence-between-the-command-line-and-beguilersettings)
  - [17.5 Locating the Toolchain](#175-locating-the-toolchain)
  - [17.6 Language-Server Mode](#176-language-server-mode)
  - [17.7 Exit Status and Console Output](#177-exit-status-and-console-output)
  - [17.8 Entry Mode](#178-entry-mode)
  - [17.9 Examples](#179-examples)
  - [17.10 Patch Tools](#1710-patch-tools)
- [18 Compilation Model](#18-compilation-model)
  - [18.1 Overview](#181-overview)
  - [18.2 Phases](#182-phases)
  - [18.3 Passes and Forward References](#183-passes-and-forward-references)
  - [18.4 Include Resolution](#184-include-resolution)
  - [18.5 Path Resolution](#185-path-resolution)
  - [18.6 Layout of the Generated File](#186-layout-of-the-generated-file)
  - [18.7 Emission Ordering](#187-emission-ordering)
  - [18.8 `bglInit()`](#188-bglinit)
  - [18.9 Pay-Only-If-Used Emission](#189-pay-only-if-used-emission)
  - [18.10 The Frame Pool](#1810-the-frame-pool)
  - [18.11 Dead Code and Economy](#1811-dead-code-and-economy)
  - [18.12 Z-machine Capacity](#1812-z-machine-capacity)
- [19 Diagnostics](#19-diagnostics)
  - [19.1 Message Format](#191-message-format)
    - [19.1.1 Inform 6 Diagnostics](#1911-inform-6-diagnostics)
  - [19.2 Compile-Time Errors](#192-compile-time-errors)
  - [19.3 Warnings](#193-warnings)
  - [19.4 Runtime Failures](#194-runtime-failures)
- [20 Build Outputs and Debugging](#20-build-outputs-and-debugging)
  - [20.1 Output Directory](#201-output-directory)
  - [20.2 Files Produced](#202-files-produced)
  - [20.3 Debug Builds](#203-debug-builds)
  - [20.4 The Inform 6 Debug File](#204-the-inform-6-debug-file)
  - [20.5 Console Output](#205-console-output)

**Part III: The Beguile Language Runtime (BLR)**
- [21 Runtime Core](#21-runtime-core)
  - [21.1 Overview](#211-overview)
  - [21.2 `bglInit()`](#212-bglinit)
  - [21.3 The `bgl` Namespace](#213-the-bgl-namespace)
  - [21.4 `bgl.output`: `print()`, `printLine()`, `log()` and Article Helpers](#214-bgloutput-print-printline-log-and-article-helpers)
  - [21.5 IF-Domain Types](#215-if-domain-types)
    - [21.5.1 `attribute` and `attributeList`](#2151-attribute-and-attributelist)
    - [21.5.2 `property`](#2152-property)
    - [21.5.3 `dictionaryWord`](#2153-dictionaryword)
    - [21.5.4 `verb`](#2154-verb)
    - [21.5.5 Grammar Types](#2155-grammar-types)
    - [21.5.6 `bglClass`](#2156-bglclass)
    - [21.5.7 `parentProp` and `childrenProp`](#2157-parentprop-and-childrenprop)
    - [21.5.8 `_bglObject` and `_bglPrimitive`](#2158-bglobject-and-bglprimitive)
    - [21.5.9 `eType` and `typeof()`](#2159-etype-and-typeof)
    - [21.5.10 `iString`, `iStringOrObject` and `iStringOrRoutine`](#21510-istring-istringorobject-and-istringorroutine)
  - [21.6 Numeric Utilities](#216-numeric-utilities)
    - [21.6.1 `uint`](#2161-uint)
    - [21.6.2 `bgl.util.math`](#2162-bglutilmath)
    - [21.6.3 `bgl.util.random`](#2163-bglutilrandom)
  - [21.7 Character Utilities](#217-character-utilities)
  - [21.8 `bglAllocated`](#218-bglallocated)
  - [21.9 `bgl.world`](#219-bglworld)
  - [21.10 `bgl.ui`](#2110-bglui)
  - [21.11 `bgl.printRules`](#2111-bglprintrules)
  - [21.12 Utility Types](#2112-utility-types)
  - [21.13 `bgl.asm`](#2113-bglasm)
    - [21.13.1 Glulx Opcodes](#21131-glulx-opcodes)
    - [21.13.2 Glk Calls](#21132-glk-calls)
    - [21.13.3 Z-machine Opcodes](#21133-z-machine-opcodes)
  - [21.14 `bgl.header`](#2114-bglheader)
  - [21.15 `bgl.gc`](#2115-bglgc)
- [22 Language Extensions](#22-language-extensions)
  - [22.1 Overview](#221-overview)
  - [22.2 `<buf>`](#222-buf)
  - [22.3 `<string>`](#223-string)
  - [22.4 `<array>`](#224-array)
  - [22.5 `<linq>`](#225-linq)
  - [22.6 `<ui>`](#226-ui)
  - [22.7 `<glulxWindow>`](#227-glulxwindow)
    - [22.7.1 Window Types](#2271-window-types)
    - [22.7.2 Roots](#2272-roots)
    - [22.7.3 Splitting](#2273-splitting)
    - [22.7.4 Sizing and Re-arrangement](#2274-sizing-and-re-arrangement)
    - [22.7.5 Images](#2275-images)
    - [22.7.6 Cursor and Lifecycle](#2276-cursor-and-lifecycle)
    - [22.7.7 Styles](#2277-styles)
    - [22.7.8 Colors](#2278-colors)
    - [22.7.9 Style Validation](#2279-style-validation)
    - [22.7.10 Enums](#22710-enums)
    - [22.7.11 Window Output](#22711-window-output)
    - [22.7.12 Capabilities](#22712-capabilities)
  - [22.8 `<glulxImage>`](#228-glulximage)
  - [22.9 Library-specific Extensions](#229-library-specific-extensions)
- [23 IF Library Bindings](#23-if-library-bindings)
  - [23.1 What a Binding Is](#231-what-a-binding-is)
  - [23.2 Available Bindings](#232-available-bindings)
  - [23.3 What a Binding Provides](#233-what-a-binding-provides)
    - [23.3.1 Entry Point and `bglInit()`](#2331-entry-point-and-bglinit)
    - [23.3.2 `story` and `headline`](#2332-story-and-headline)
    - [23.3.3 Capability Flags](#2333-capability-flags)
    - [23.3.4 Attributes, Globals, Constants and Routines](#2334-attributes-globals-constants-and-routines)
    - [23.3.5 Additive Properties](#2335-additive-properties)
    - [23.3.6 Verbs and Grammar Tokens](#2336-verbs-and-grammar-tokens)
    - [23.3.7 Shared Types](#2337-shared-types)
    - [23.3.8 Status Bar and Main Window](#2338-status-bar-and-main-window)
    - [23.3.9 Banner Texts](#2339-banner-texts)
    - [23.3.10 Library Options and Debug Builds](#23310-library-options-and-debug-builds)
    - [23.3.11 Standard Library Properties](#23311-standard-library-properties)
    - [23.3.12 Collection at the Prompt](#23312-collection-at-the-prompt)
    - [23.3.13 Parser State](#23313-parser-state)
  - [23.4 Differences Between the Bindings](#234-differences-between-the-bindings)
  - [23.5 Writing a Binding](#235-writing-a-binding)

**Appendices**
- [Appendix A Reserved Words](#appendix-a-reserved-words)
- [Appendix B Directive Index](#appendix-b-directive-index)
- [Appendix C Operators](#appendix-c-operators)
  - [C.1 Precedence](#c1-precedence)
  - [C.2 Overloadable Operators](#c2-overloadable-operators)
  - [C.3 Operator-to-Section Index](#c3-operator-to-section-index)
- [Appendix D String Escapes and Character Tables](#appendix-d-string-escapes-and-character-tables)
  - [D.1 Basic Escapes](#d1-basic-escapes)
  - [D.2 Numeric Escapes](#d2-numeric-escapes)
  - [D.3 Diacritical Shorthands](#d3-diacritical-shorthands)
  - [D.4 Context-Sensitive `\^` and `\~`](#d4-context-sensitive-and-)
  - [D.5 Directly Typed Characters](#d5-directly-typed-characters)
  - [D.6 Typographic Quotes and the Backtick](#d6-typographic-quotes-and-the-backtick)
  - [D.7 Character Literals](#d7-character-literals)
- [Appendix E Settings Reference](#appendix-e-settings-reference)
- [Appendix F Pre-defined Symbols](#appendix-f-pre-defined-symbols)
- [Appendix G Emitter Substitution Tokens](#appendix-g-emitter-substitution-tokens)
  - [G.1 Token Reference](#g1-token-reference)
- [Appendix H Glossary](#appendix-h-glossary)
  - [H.1 Terms](#h1-terms)
  - [H.2 Symbols](#h2-symbols)
- [Appendix I Symbol and Method Index](#appendix-i-symbol-and-method-index)
- [Appendix J Limits](#appendix-j-limits)
- [Appendix K Gotchas](#appendix-k-gotchas)
  - [K.1 A Method Named Like a Library Property Is That Property](#k1-a-method-named-like-a-library-property-is-that-property)
  - [K.2 An `IdentifyGlkObject` Returning True Stops Window Recovery](#k2-an-identifyglkobject-returning-true-stops-window-recovery)

---

# Front matter

# About This Specification

## Purpose and Scope

Welcome to the Beguile Language Specification.  This document is organized into three parts, plus a few appendices:

- **Part I: The Beguile Language** (§1 to §16) covers the language itself.
- **Part II: The Beguiler Compiler** (§17 to §20) describes the compiler which processes the Beguile language.
- **Part III: The Beguile Language Runtime** (§21 to §23) goes over the runtime library every program uses, including the core and the opt-in language extensions.
- **Appendices A to K** collect the lookup tables, including reserved words, directives, operators, character escapes, etc...

## What This Specification Is, And Is Not

It ***is not*** an Inform 6 manual.  The Inform 6 language, the libraries commonly used with it, and the Z-machine and Glulx virtual machines are documented elsewhere and are assumed.

It is also ***not*** a tutorial.  Readers new to Beguile should start with the [Quick Start](../quickStart.md) document which walks you through the installation and helps you get your development environment ready.  Readers coming from Inform 6 can start with [Beguile for the I6 Developer](../Beguile%20for%20the%20I6%20Developer.md) guide, which teaches the language through a complete, runnable port and maps the syntax back to the Inform 6 it replaces.

This specification ***is*** the reference to graduate to once the language is familiar.

## Conventions

### Typography

Identifiers, keywords, operators, file names, and any other literal source text appear in `code font`.  Keywords are written lowercase, although Beguile is case-insensitive (§1.3).  A term is **bold** where it is defined.  Inform 6 is abbreviated I6.

### Syntax Notation

Syntax forms are given in fenced blocks, like so:

```syntax
[ QUALIFIER … ] TYPE NAME [ = INITIALIZER ] ;
```

Here's how to read one:

- `NAME` an uppercase word is a placeholder for text you supply.
- `[ text ]` brackets represent an optional part.
- `…` the ellipsis repeats the part before it.
- `|` the vertical bar separates alternatives.
- Every other character is literal.

The form above reads:  zero or more qualifiers, a type, a name, an optional initializer introduced by `=`, and a semicolon.  Where a form uses `[`, `]` or `|` as actual Beguile tokens (e.g., array sizes, union types), the sentence under the block says so.

A placeholder of more than one word is joined with hyphens, as in `RETURN-TYPE`.

A placeholder which is only part of a name is written in mixed case, with the placeholder parts capitalized:  `splitDirectionKind` (§22.7.3) names one method for every combination of the listed values (e.g., `splitUpGrid`); the sentence under the block lists the values.

In running text and tables, we write a form the way you would say it:  `Type::operator op` stands for `TYPE::operator OP` (§4.15), and `obj.member` for `EXPR.MEMBER`.

### Examples

An example is the smallest fragment which shows the rule.  A `// ->` comment gives the resulting value or the text printed.  Program output appears in its own block.

A fragment of statements belongs inside a routine.  When an example also needs lines at file scope (an `#include`, a global, a class), a `// … in a routine:` line separates the two:  everything above it goes at file scope, and everything below it goes in a function body.

Inform 6 code appears only in the sections about what Beguile generates (Part II and §16), and in emitter bodies.

Examples use real keywords and real library names, with `Main` as the entry point.  An example which uses a name declared by the Inform 6 Standard Library (`location`, `player`, `light`, `selfobj`, …) assumes `#include <bindings/i6StandardLibrary>` (§23.2) and leaves the name undeclared.

### Notes

Material which can be skipped on a first reading (a consequence of the rule, a caution, a limit) is set apart as a note, right where it applies:

> ***Note:** The note's text.*

The notes are also where I speak in the first person; the rest of the text sticks to "we."

### Target Markers

Behavior which depends on the virtual machine is marked `[Z-machine]`, `[Glulx]` or `[Z-machine/Glulx difference]`, either inline or in the form of a note:

> ***[Glulx]:** Applies only when the target is Glulx.*

Unmarked text applies to both targets.

### Cross-References

`§N.M` and `§N.M.K` refer to a section; inside a chapter they refer to that chapter's own sections.  A bare `§N` means the whole chapter.  `Appendix X` refers to an appendix.

### Entry Shape

Every language construct (keyword, directive, operator, declaration form, library method) is documented as an entry, with its parts in this order:

- **Syntax** gives the form, in the notation above.
- The rule itself follows, unlabeled, including any shorter form which is equivalent to it.
- **Example** shows it in use.
- **See also** points to sections which are not neighbors of the entry.

Within an entry, a paragraph which covers one alternative or aspect of the rule opens with a bold label ending in a period (e.g., **Placement.** or **Capture.**).  It is a label, not a heading.

Notes and target markers sit inline, where they apply.  A part with nothing to say is left out.  Chapters 17, 18, 19, 20, and 23 are narrative and don't use this shape; the extension entries of §22 use their own, which §22.1 lays out.

---

# Introduction to Beguile

## What Beguile Is

Beguile is a statically typed, compiled language for writing interactive fiction.  Its structured syntax transpiles to Inform 6 and, from there, to a Z-machine or Glulx story file.

Although it borrows features from general-purpose languages, Beguile is *not* one.  Its features are shaped entirely by the needs of interactive fiction, and by the constraints of the virtual machines its programs run on.

## Design Goals

A number of principles framed how the language was designed:

- **Familiar syntax.**  The syntax draws on C++, C#, and TypeScript; if you are comfortable with any of them, you will find it intuitive.
- **A consistent syntax.**  Every construct follows a few core rules (e.g., declarations live in member bodies, statements end in `;`, types come before names).  The language avoids micro-grammars for special features, in favor of general-purpose constructs which work the same way everywhere:  a verb is an object, and its grammar is simply a member initialized with an ordinary list (§13).
- **Strong typing.**  Every variable, parameter, and return value carries a declared type.  Type mismatches will be caught when you compile, not when your game is running.
- **Transparency.**  The generated Inform 6 is readable and maps closely to the Beguile source.  If you know Inform 6, you can easily inspect the output, or add to it.
- **Extensibility through emitters.**  Performance-sensitive or platform-specific operations can be expressed as **emitters**:  inline fragments of Inform 6, substituted where they are called (§7).  Library authors get full control over the generated code without giving up type safety on the Beguile side.

## Relationship to Inform 6

Beguile is built on top of Inform 6; it is *not* a replacement for it.  It generates human-readable Inform 6 source; however, several features go a bit further and bridge the two languages:

- `extern` declarations let Beguile use the types, functions, attributes, and constants defined in Inform 6 without implementing them again.
- `#i6` islands place raw Inform 6 right alongside Beguile code.
- Emitters give you precise control over the Inform 6 that is generated.

When you need something Beguile doesn't expose, you can always drop down to Inform 6 through the above (§16).

---

# Part I: The Beguile Language

# 1 Lexical Structure

## 1.1 Source Text and Encoding

A source file is plain text, encoded as UTF-8 or Latin-1.  A byte-order mark at the start of a UTF-8 file is ignored.

Outside of comments, string literals, character literals, and dictionary words, source text is ASCII.  Inside those, you can directly type any character which Beguile can represent on the target (Appendix D); any other is a compile-time error.

## 1.2 Comments

**Syntax**

```syntax
// TEXT-TO-END-OF-LINE
/* TEXT */
/// DOCUMENTATION-TO-END-OF-LINE
/** DOCUMENTATION */
```

Beguile has two comment forms, the line comment `//` and the block comment `/* */`.  Comments are simply thrown away.

A comment which opens with a third character, `///` or `/**`, is a *doc comment*.  It has no effect on compilation either; however, the language server (§17.6) will show its text, formatted as Markdown, wherever you hover over the declared name.  In a `/** */` comment, a leading `*` on each line is not part of the text.

A doc comment documents a declaration from one of two positions:

- **Before it.**  Doc comments on the lines directly above a declaration document it, and consecutive doc comments join into one text.  A blank line between a doc comment and the next declaration detaches the comment.
- **After it, on the same line.**  A doc comment which follows code on its line documents the declaration that starts on that line, never the next one.  If a declaration is documented both ways, the text above it is shown first.

**Example**

```bgl
// to the end of the line

/* to the closing
   delimiter */

/// The room the player is in.
extern object location;
extern int    score;      /// Points earned so far.

/** Prints the banner.
 * Called once, at startup. */
void showBanner(){ print("Welcome."); }
```

## 1.3 Case-Insensitivity

Beguile is case-insensitive for every token except the contents of string literals, and normalizes the rest to lowercase; the following lines are entirely equivalent:

<!-- doctest: skip -->
```bgl
if(X == 1) print("yes");
IF(x == 1) Print("yes");
If(x == 1) PRINT("yes");
```

## 1.4 Identifiers

**Syntax**

```syntax
LETTER-OR-UNDERSCORE [ LETTER,-DIGIT-OR-UNDERSCORE … ]
```

An identifier is a sequence of letters, digits, and underscores which doesn't start with a digit.  You should not use a reserved word (§1.5) as a name.  A member can share its name with a type (§3.8.3).

**Example**

<!-- doctest: skip -->
```bgl
score   myVar   _internal   room1   velvetCloak
```

> ***Note:** Names beginning with `_bgl` or `bgl` are earmarked for the compiler and the runtime.  The compiler won't reject such a name in your code; however, one which matches one of the system's can conflict with it, and the behavior is then undefined.  Where the runtime exposes a prefixed name for you to use, it is documented with the feature (§16.8, §21.5.8).*

## 1.5 Reserved Words

The words below have meaning in Beguile and should not be used as names.  The compiler won't reject every use of one as a name; such a use is unsupported regardless.  Declaring a type with the name of a built-in type is a compile-time error, and a global named after a word shared with Inform 6 is rejected by the Inform 6 stage.  All of this applies in any letter case.  Appendix A lists the words alphabetically.

**Declaration words** begin or qualify a declaration.

`alias` `asBgl` `asI6` `class` `const` `default` `distributor` `emitter` `explicit` `extend` `extern`
`inline` `literal` `operator` `primitive` `ref` `replace` `required` `static` `struct` `superposed`
`typesealed`

`primitive` is a keyword only directly before `class`, `struct` only directly before a name, `required` only directly before a member's type, `literal` (§3.13) only directly before a type, and `distributor` (§6.8) only directly before a name and its `;`; elsewhere they are ordinary names (`int struct;`).

**Type-forming words** build a type rather than name one.

`auto` `bnum` `enum` `func` `selfType` `var` `void`

**Built-in type names** are the types every program has, with no `#include`.

`array` `attribute` `bool` `char` `dictionaryWord` `float` `int` `interpolatedString` `object`
`property` `rawArray` `string` `uint` `verb`

**Literal pseudo-types** are the types of literal values.

`charLiteral` `dictionaryWordLiteral` `intLiteral` `interpolatedStringLiteral` `negativeIntLiteral`
`nullLiteral` `stringLiteral`

**Statement words** begin or structure a statement.

`break` `case` `catch` `continue` `delete` `do` `else` `for` `if` `in` `return` `rfalse` `rtrue`
`switch` `throw` `to` `try` `until` `while`

**Expression words** introduce an expression.

`new` `replaced`

**Value words** name a fixed value or the current receiver.  None of them can be declared as a name.

`false` `null` `self` `true`

**Contextual words** have meaning in one position only and are ordinary identifiers elsewhere.

`hide` `inject` `move` `outer` `remove` `synonyms` `union`

**Words shared with Inform 6** also appear verbatim in the generated Inform 6, so a program which uses one as a name can produce output the Inform 6 compiler will reject.

`additive` `array` `attribute` `class` `false` `nothing` `object` `property` `replace` `self` `string`
`true` `verb`

Inform 6 reserves still more words which Beguile does not (§16.9).

## 1.6 Literals

### 1.6.1 Integer Literals

**Syntax**

```syntax
DIGITS
$HEX-DIGITS
$$BINARY-DIGITS
```

An integer literal is decimal, hexadecimal (prefix `$`, digits `0` to `9` and `A` to `F` in either case), or binary (prefix `$$`, digits `0` and `1`).  For a negative value, simply prefix a `-`.

Its pseudo-type is `intLiteral`, or `negativeIntLiteral` when negated (§2.4).

**Example**

<!-- doctest: skip -->
```bgl
42        $FF         $$11111111     // -> 42, 255, 255
-1234     $0A         $$11010        // -> -1234, 10, 26
```

> ***Note:** Beguile writes hexadecimal with `$` (`$FF`), as Inform 6 does; a `0x` literal (`0xFF`) is a compile-time error.*

### 1.6.2 Float Literals

**Syntax**

```syntax
DIGITS.DIGITS
.DIGITS
```

A float literal is a decimal number containing a `.` with at least one digit after it.  Again, prefix a `-` for a negative value.  `1.` is *not* a float literal and is a compile-time error; write `1.0`.

**Example**

<!-- doctest: skip -->
```bgl
1.0     .3     -1.2
```

> ***Note:** A `.` followed by anything other than a digit is a member access, so `42.someMethod()` calls a method on the integer `42` (§2.4) rather than starting a float.*

> ***[Glulx]:** Float literals and the `float` type (§2.3) exist only when the target is Glulx.*

### 1.6.3 String Literals

**Syntax**

```syntax
"CHARACTERS"
```

A string literal is text enclosed in double quotes.  It recognizes quite a few backslash escapes; here are the ones you'll need in everyday text:

| Escape | Character |
|---|---|
| `\n` | newline |
| `\"` | `"` |
| `\\` | `\` |
| `\^` | `^` |
| `\~` | `~` |
| `\@` | `@` |
| `\{` `\}` | `{` `}` |

A backslash before any other character is a compile-time error, unless Appendix D lists the escape.

Beguile keeps two of Inform 6's string conventions:  an unescaped `^` is a newline and an unescaped `~` is a double quote.  Escape them to print them literally.  Every other character prints as itself, `@` included (accented letters are written with the `\` escapes of Appendix D, not I6's `@` codes).  Appendix D covers the remaining escapes and characters.

A string literal has the pseudo-type `stringLiteral` (§2.4).

**Example**

<!-- doctest: skip -->
```bgl
"Hello, world!"
"She said, \"well done.\""
"Line one^Line two"
```

### 1.6.4 Raw String Literals

**Syntax**

```syntax
@"CHARACTERS"
```

A raw string literal turns off all escape processing except `\"`.  Every other character is taken literally, including `\`, `^`, and `~`.  It can appear anywhere a string literal can, including `#beguilerSettings` values (§15.1), and has the same pseudo-type, `stringLiteral`.

**Example**

```bgl
string path = @"C:\Users\jim\IF-Games\medusa.bgl";
```

### 1.6.5 Interpolated String Literals

**Syntax**

```syntax
$"CHARACTERS { EXPRESSION } CHARACTERS …"
```

An interpolated string literal is prefixed with `$` and can contain Beguile expressions inside `{ }` spans.  Its escapes are those of a string literal; `\{` is a literal `{` rather than the start of a span.

A span holds any expression, including one with braces of its own (e.g., a lambda with a block body).  A string literal inside a span is written with plain quotes.  Escapes belong to the characters of a string, so `\"` inside a span is a compile-time error.

What happens to an interpolated string depends on where you use it:  an emitter which declares an `interpolatedStringLiteral` parameter expands it in place (§2.4.2), a `stringObj` captures its output (§22.3), and a `func<void>`, a `var`, or an `interpolatedString` takes it as a routine which prints it (§2.9.1).

**Example**

```bgl
object lamp { string title = "brass lamp"; int weight = 2; }

// … in a routine:
print($"The {lamp.title} weighs {lamp.weight} stone.");   // -> The brass lamp weighs 2 stone.
print($"Press \{enter} to continue.");                    // -> Press {enter} to continue.
print($"Lit: {lamp.weight > 1 ? "yes" : "no"}.");         // -> Lit: yes.
```

### 1.6.6 Character Literals

**Syntax**

```syntax
'CHARACTER'
```

A character literal represents exactly one character and is written in single quotes.  It accepts the same escapes as a string literal and the same directly typed Unicode characters (Appendix D), so the literal itself can be several characters long; however, one which represents more than one character is a compile-time error.

`\'` followed by a vowel in the acute-accent set is the acute accent (`'\'e'` is `é`); a `\'` which isn't followed by such a vowel is an escaped single quote.  A character literal has the pseudo-type `charLiteral` (§2.4).

**Example**

<!-- doctest: skip -->
```bgl
'a'   '\n'   '\\'   'ä'   '\:a'
```

> ***Note:** Inform 6 writes a dictionary word in single quotes (`'sword'`).  Beguile does **not**:  single quotes are the character literal, and a dictionary word is written with a leading `.` (§1.6.7).*

### 1.6.7 Dictionary Word Literals

**Syntax**

```syntax
.WORD
..WORD
```

A dictionary word literal names an entry in the I6 dictionary (i.e., the words which the parser matches the player's input against).  The `.` form is singular; the `..` form is plural.  The apostrophe is an acceptable character.  A `-` between two characters is part of the word, not the subtraction operator.

Its pseudo-type is `dictionaryWordLiteral`, which is compatible with `dictionaryWord` (§2.4, §21.5.3).  §13.1 covers what singular and plural words mean.

**Example**

<!-- doctest: skip -->
```bgl
.cloak   ..cloaks   .medium-sized   .monkey's
```

A word the dot form can't spell (e.g., one starting with a digit) is written as a string literal cast to `dictionaryWord`.  The cast is made at compile time and applies only to a string literal holding one word.

```bgl
object key101 { name = {.key, (dictionaryWord)"101"}; }
```

> ***Note:** The above cast is the only way to write that word:  `.101` is the `float` literal 0.101, **not** a dictionary word.*

## 1.7 Operators

The following multi-character sequences are parsed as single operators.  A longer token always wins:  `<=>` is the three-way comparison, never `<=` followed by `>`.

```text
<<=  >>=  <=>  :==
-=  +=  *=  /=  %=  &=  |=  ^=  :=  ?=  ==  !=  <=  >=  =~
&&  ||  ++  --  <<  >>  =>  ?.  ??
```

The single-character operators are:

```text
=  +  -  *  /  %  <  >  !  &  |  ^  ?  :  .  ,  ;  #  (  )  {  }  [  ]
```

`::` is the global-scope qualifier (§3.9) and, after a type name, introduces an operator reference (`string::operator <=>`, §4.15).  When immediately followed by an identifier, it forms a single token with it:  `::name`.

Appendix C.3 indexes the role of each operator and the section which covers it; §9.1 lists the overloadable operators, and §4.3 tabulates precedence.

## 1.8 Directives

**Syntax**

```syntax
#IDENTIFIER
```

A `#` immediately followed by an identifier, with no whitespace between, is a directive token:  `#include`, `#define`, `#if`, `#i6`.  §14 covers directives, and Appendix B indexes them.

---

# 2 Types and Values

## 2.1 Overview

Beguile is statically typed:  every variable, parameter, and return value has a type known when you compile.  Types fall into four categories:  primitive types (§2.2), literal pseudo-types (§2.4), user-defined types (enumerations §2.7, unions §2.8, classes §8), and the `var` escape type (§2.6).  The `array` type gets a chapter of its own (§12).

§2.10 covers the rules which apply across all types.

## 2.2 Primitive Types

The primitive types are built in; there is nothing to `#include`.

| Type | Description |
|---|---|
| `int` | Signed integer, one native word:  16 bits on the Z-machine (-32768 to 32767), 32 bits on Glulx. |
| `uint` | Unsigned integer (§21.6.1). |
| `float` | IEEE 754 single-precision floating point.  `[Glulx]` See §2.3. |
| `bool` | A true/false type, backed by the enumeration `eBool` (§2.7.4). |
| `char` | A single character:  its ZSCII code on the Z-machine, its Unicode code point on Glulx (Appendix D). |
| `string` | A reference to static text. |

`object` (§11.1) and `verb` (§13.2) are classes, and `void` is a return type (§6.2).

`==` and `!=` between two `string`s (a literal included) compare the references, not the text:  two literals with the same text are two strings, and compare unequal.  The compiler warns about such a comparison, unless the program has replaced `string`'s operator (§8.7.2).  Comparing their values as integers (`(int)a == (int)b`) asks for the references on purpose, without the warning.  `<string>` adds `equals()` to compare text (§22.3).

## 2.3 The `float` Type

A `float` occupies one native word, the same width as `int`, holding an IEEE 754 single-precision value.  It supports the arithmetic operators `+` `-` `*` `/` `%` and their compound-assignment forms, the comparisons `==` `!=` `<` `<=` `>` `>=`, and `print()`.

A `float` is initialized from a float literal (§1.6.2) or from an `int`, which converts implicitly, preserving its value (`5` becomes `5.0`); an `int` is never reinterpreted bit-for-bit as a `float`.  Converting a `float` to an `int` always requires a cast, `(int)f`, which truncates toward zero.

**Mixed arithmetic.**  If either operand of an arithmetic operator is a `float`, the other is converted to `float` and the result will be a `float`; two `int` operands give an `int`.  The comparison operators convert the same way, although they yield `bool`.

`print()` writes a `float` as a decimal rounded to four places:  `2.0 / 3` prints `0.6667`, and `0.99999` prints `1.0000`.

**Example**

```bgl
#beguilerSettings { target = Glulx; }

void Main() {
    float a = 2.5;
    float b = 5;                    // -> 5.0
    float c = (float)314 / 100;     // -> 3.14: one float operand makes the division float
    int   d = 314 / 100;            // -> 3: two ints divide as integers
    print(b / a);                   // -> 2.0000
    if(b > a) print("^bigger^");
    int n = (int)c;                 // -> 3
}
```

> ***[Glulx]:** `float` is available only when the target is Glulx; the Z-machine simply has no floating-point support.*

## 2.4 Literal Pseudo-Types

A literal has a **pseudo-type**, which the compiler infers from the way it is written.  The names are written mostly to extend one, or to declare an overload which accepts one.  `auto` infers a pseudo-type's corresponding type when the pseudo-type declares `operator auto()` (§9.5.1); `stringLiteral` doesn't, so `auto s = "hello";` declares a `stringLiteral` (§3.6).  Pseudo-types take part in operator and overload resolution independently of the runtime types they correspond to.

| Pseudo-type | Written | Corresponding type |
|---|---|---|
| `intLiteral` | `42`, `$FF`, `$$1010` | `int` |
| `negativeIntLiteral` | `-1` | `int` |
| `stringLiteral` | `"hello"`, `@"raw"` | `string` |
| `charLiteral` | `'a'` | `char` |
| `dictionaryWordLiteral` | `.cloak`, `..cloaks` | `dictionaryWord` (§21.5.3) |
| `interpolatedStringLiteral` | `$"hello {x}"` | none; see §2.4.2 |

A pseudo-type converts to its corresponding type only through a conversion operator on the pseudo-type (`intLiteral` declares one to `int`) or an `operator =` on the target type which accepts it (`float` declares `operator = (intLiteral)`); §2.10.1 gives the order.

Pseudo-types are first-class types, declared as `primitive class` (§8.2.5) and extensible with `extend` (§8.7.1).  As such, a method defined against one can be called directly on a literal.

**Example**

```bgl
extend class intLiteral {
    emitter void twice(){ print $val * 2; }
}

// … in a routine:
"hello".print();       // a method on stringLiteral
42.twice();            // -> 84
```

### 2.4.1 `negativeIntLiteral`

A negated integer literal such as `-1` has the pseudo-type `negativeIntLiteral` rather than `intLiteral`.  It converts to `int` implicitly; however, it is not compatible with `uint` (§2.2), and assigning one to a `uint` requires an explicit cast.

**Example**

```bgl
int  x = -5;         // -> -5
uint u = -1;         // compile-time error
uint v = (uint)-1;   // the largest unsigned value
```

### 2.4.2 `interpolatedStringLiteral`

An interpolated string (§1.6.5) has no corresponding runtime type.  Its literal type, `interpolatedStringLiteral`, can only be passed to an emitter which declares a parameter of that type, where it expands in place into a block of statements; passing it to a non-emitter function's `interpolatedStringLiteral` parameter is a compile-time error.  Passed to a parameter which takes a routine (`func<void>`, `var`, or `interpolatedString`), it becomes a routine instead (§2.9.1).  `print()` and `bgl.output.log()` accept it (§21.4).  The call must be a statement of its own; a call inside an expression is a compile-time error.  An expression-bodied lambda whose body is such a call, `=> print($"…")`, *is* that statement (§4.14).

## 2.5 `null`

`null` is the absent or unset value, numerically `0`.

Its type (§4.1) is the literal pseudo-type `nullLiteral`, which is compatible with every type.

A struct can't hold `null` (§8.2.7).

> ***Note:** Inform 6's `nothing` is not a Beguile word; Beguile writes `null`.*

**Example**

<!-- doctest: skip -->
```bgl
object o;
if(o == null) print("not yet set");

marbleClass m = new marbleClass();
if(m == null) print("pool exhausted");
```

> ***Note:** `null` is emitted as the Inform 6 constant `nothing`, which is what you will see in generated code and what is written in emitter bodies (§7).*

## 2.6 The `var` Type

`var` is a universal escape type which bypasses static type checking in both directions:  any value can be assigned to a `var`, and a `var` can be assigned to any type.

In overload resolution an overload with `var` parameters is a fallback, selected only when no typed overload matches (§6.4).

**Example**

```bgl
var x = 5;
int y = x;        // no type check
object o = x;     // no type check
```

## 2.7 Enumerations

### 2.7.1 `enum`

**Syntax**

```syntax
enum NAME { MEMBER [ = INTEGER ] [ , MEMBER [ = INTEGER ] … ] }
```

An `enum` declares a named set of integer constants.  Values start at 1 and automatically increment by 1.  A member can be given an explicit value, including a negative one, and numbering resumes from the last assigned value.  Two members can hold the same value, although they are then indistinguishable at run time.

Enumerations can carry emitter methods, declared in the body or added with `extend enum` (`extend bnum` for a bnum); §7.8 covers the form.

**Example**

```bgl
enum direction { north, south, east, west }   // -> 1, 2, 3, 4

enum myPhase {
    setup  = 0,
    play   = 10,
    ending           // -> 11
}
```

### 2.7.2 `bnum`

**Syntax**

```syntax
bnum NAME [ : BASE-BNUM ] { MEMBER [ = INTEGER ] [ , MEMBER [ = INTEGER ] … ] }
```

A `bnum` is a bit-flag enumeration.  Values start at 1 and double with each member, so the members are non-overlapping powers of two, suitable for combining as flags.  An explicit value must be a non-negative power of two, and numbering resumes by doubling from it.  `0` is always a legal value.

**Shared base.**  A `bnum` can name a base `bnum`, declaring that it occupies a sub-field of the same packed integer.  The base must itself be a `bnum`.  Within a shared base the power-of-two rule is relaxed, since a sub-field's members need not be single bits.

**Combining values.**  Bitwise `|`, `&`, and `^` between two `bnum` values are allowed only when the operands share a common `bnum` ancestor (one can be the other's ancestor, or both the same type).  The result has the shared ancestor's type.  Combining values with no common base is a compile-time error.

**Example**

```bgl
bnum itemFlag { portable, fragile, lit, locked }   // -> 1, 2, 4, 8

bnum winMethodFlags { }                          // empty base: defines the bit-field space
bnum windowPlacement : winMethodFlags { left = 0, right = 1, above = 2, below = 3 }
bnum windowScale     : winMethodFlags { fixed = 16, proportional = 32 }
bnum windowBorder    : winMethodFlags { border = 0, noBorder = 256 }

int winMethod = left | fixed | noBorder;    // all share winMethodFlags
int bad       = left | itemFlag.portable;   // compile-time error
```

> ***[Z-machine/Glulx difference]:** A `bnum` can have at most 16 distinct values on the Z-machine and 32 on Glulx.*

### 2.7.3 Widening to `int`

A `bnum` value converts implicitly to `int`; the reverse requires an explicit cast.  A plain `enum` does not automatically widen:  converting between an `enum` and `int`, in either direction, always requires an explicit cast.

**Example**

```bgl
bnum itemFlag { portable, fragile }
enum direction { north, south }

int a = fragile;               // -> 2: a bnum widens to int
itemFlag f = (itemFlag)2;      // int to bnum: explicit cast
int b = (int)south;            // -> 2: enum to int: explicit cast
direction d = north;
int c = d;                     // compile-time error: an enum does not widen
```

### 2.7.4 `extern enum` and `extern bnum`

**Syntax**

```syntax
extern enum NAME { MEMBER [ , MEMBER … ] }
extern bnum NAME { MEMBER [ , MEMBER … ] }
```

An ordinary `enum` or `bnum` exists only in Beguile:  each use of a member is emitted as its number, and the enumeration itself never appears in the generated Inform 6.

An `extern` enumeration instead names values defined in I6.  Its members have no values in Beguile; each use of one is emitted as its name, and Inform 6 supplies the value.  `eBool`, Beguile's boolean-result type (§2.2), is declared this way.

**Example**

<!-- doctest: skip -->
```bgl
extern enum eBool { true, false }
extern enum eErrorFormat { E1, E2 }
```

### 2.7.5 Naming Members

An enumeration member can be referenced by its bare name (`north`, `true`) or qualified by its type (`direction.north`, `eBool.true`).

Where a value of an enumeration type is expected (an initializer, an assignment, an argument, an array element of that type), a bare name which is both a member of that enumeration and some other global, such as the attribute `door`, is the member.  Elsewhere the qualified form is required when two enumeration types, or an enumeration and another global, share the name.

## 2.8 Union Types

**Syntax**

```syntax
TYPE | TYPE [ | TYPE … ]
```

The `|` here is the union type operator, a literal token.

A union type declares that a value is one of several types, distinguished at run time.  It is valid at every type position:  parameter, local, return type, class or object member, and array element type.

A union type is also *inferred* in one place:  a ternary whose two branches have unrelated types resolves to their union (§4.9).

**Canonical form.**  Members are order-independent and de-duplicated:  `string | func<void>` and `func<void> | string` name the same type, and a union of a type with itself is simply that type.

**Operations.**  A union value can be assigned, passed, returned, and compared with `==` and `!=`.  It can't be called, printed, member-accessed, or used in arithmetic while it is still a union; doing so is a compile-time error.  The value must first be **narrowed** with an ordinary cast, `(MEMBER)x`, which retypes the value without converting or checking it; discriminate it first with `typeof` (§2.8.1).

**Compatibility.**  A value fits a union when it fits one of the union's members:  a `string` can be stored in a `string | func<void>`.  One union fits another when each of its members fits the other:  a `string | int` can be stored in a `string | int | func<void>`.  A union does not fit one of its member types directly; narrow it first with a cast.  §6.4 ranks an overload which takes a union.

**Example**

```bgl
string | func<void> L = "hello";
string | func<void> pick(int n) { … }
class Slot { string | func<void> handler; }
array<string | func<void>> items;

void describe(string | func<void> x) {
    if(typeof(x) == eType.routine) {
        func<void> f = (func<void>)x;
        f();
    } else {
        print((string)x);
    }
}
```

### 2.8.1 `typeof` and `eType`

**Syntax**

```syntax
typeof( EXPRESSION )
```

`typeof` returns the run-time machine category of any value, including a `var`, as an `eType`.  It needs no `#include` and works on both targets.

Note that `typeof` reports machine categories, *not* source types:

- `bool`, `char`, and enumeration values are represented as `int` and report `eType.int`.
- A scalar which happens to equal a valid object number, or a string or routine address, is reported as that reference category.  A union mixing a scalar with a reference type (e.g., `int | string`) must be discriminated by a test of your own, then narrowed with a cast.
- `null` reports `eType.unknown`, as does any value whose category can't be determined.

Objects and classes report `eType.object` and `eType.class`.  To test a value's class, use `x.is(SomeClass)` (§21.5.6).

**Example**

`eType` is declared as follows (shown for reference; a program does not declare it).

<!-- doctest: skip -->
```bgl
enum eType { unknown = 0, int, string, routine, object, class }
```

```bgl
if(typeof(x) == eType.routine) …
switch(typeof(x)) { case eType.string: …  case eType.object: … }
```

**See also** §21.5.9.

### 2.8.2 Named Unions

**Syntax**

```syntax
union NAME = TYPE | TYPE [ | TYPE … ] ;
union NAME = TYPE | TYPE [ | TYPE … ] { MEMBER … }
extend NAME { MEMBER … }
```

The `|` here is the union type operator, a literal token.

A named union gives a union a name and a place for members.  It must have at least two distinct member types and takes no qualifiers.  The form without a body can be given members later, with `extend`.

Compatibility is a bit of a hybrid.  For assignment and passing, a named union is transparent (a `textOrRoutine` is a `string | func<void>`, and satisfies a parameter of that anonymous type); for member lookup it is nominal (only a value statically typed as the named union sees its members).

Members are emitters (§7.2), inlined by static type, or `static` methods.  A named union is printed through a global `print(NAME)` overload, not a member (§21.4).

**Example**

```bgl
union textOrRoutine = string | func<void> ;

void show(textOrRoutine v) {              // also accepts a string | func<void>
    if(typeof(v) == eType.routine) { func<void> f = (func<void>)v; f(); }
    else print((string)v);
}
```

> ***Note:** The library bindings ship `iString` and `iStringOrObject`, with a `print` overload and members reporting which type a value holds, for the properties the library prints, such as `description` (§21.5.10, §23.3.7).*

## 2.9 Function Types

**Syntax**

```syntax
func< RETURN-TYPE [ , PARAMETER-TYPE … ] >
```

The `<` and `>` here are literal tokens enclosing the type arguments.

`func<>` is the type of a value which refers to a function.  The first type argument is the return type; the remaining arguments are the parameter types, in order.  A function with no parameters has type `func<RETURN-TYPE>`; one returning nothing has `func<void, …>`.

`func<>` is valid wherever a type is, including nested forms such as `array<func<T>>`.  A `func<>` value is called like a function, wherever it is held, and so is any parenthesized expression:  `((func<void>)v)()` narrows a `var` or union value to a function and calls it.

The name of a function, written without a call, is a function value whose type is the function's signature:  `int twice(int n)` gives a `func<int, int>`.  It can be stored in, passed to, or returned as a `func<…>` of that signature; a different signature is a compile-time error.  A lambda (§4.14) is checked the same way:  its parameter types are as written, and its return type is inferred from its body.  A bare function name used as a value (`func<int,int> f = twice;`) must name exactly one routine, so an overloaded name is a compile-time error.  Simply wrap the overload you want in a function of its own and reference that.

A global function or a `static` method is a function value (`func<int, int> f = Calc.twice;`).  A non-static method of an object or class is not, since it needs its object.  Naming one without a call is a compile-time error, and the compiler will suggest a lambda which supplies the object:  `(int n) => fx.succ(n)`.

**Example**

```bgl
func<void, int> printer;    // takes one int, returns nothing
func<int, int>  doubler;    // takes one int, returns int
func<void>      callback;   // takes nothing, returns nothing

int twice(int n) { return n * 2; }
doubler = twice;            // a function name is a func value
int x = doubler(4);         // -> 8: called through a variable
obj.handler(3);             // called through a member

array<func<eVerdict>> rulebook = { ruleA, ruleB };
for(func<eVerdict> r in rulebook) { eVerdict v = r(); … }   // called through a loop variable
```

### 2.9.1 `interpolatedString`

Under the covers, an `interpolatedString` is code which evaluates and prints values at runtime:  a string literal prefixed with `$` defines one.  Conceptually, it's a string with values which are reevaluated each time it prints, *not* when it is defined, so printing the same `interpolatedString` multiple times may produce different results.  The parts print left to right as they are reached, so a call in a span which prints output of its own will print it at that point.

Used as a value anywhere but an emitter's `interpolatedStringLiteral` parameter (§2.4.2), an interpolated string becomes a routine which prints it each time it runs:  a `var`, an `interpolatedString`, a `func<void>`, or a type which holds one will take it that way.  Interpolated strings may be assigned to variables or properties; in such cases the actual assignment is usually a `func<void>` routine.  An `interpolatedString` is printed, not called:  `print(live)` runs it, and `live()` is a compile-time error.  Assigned or added (`+=`) to a `stringObj`, its output is captured instead, at the time it is assigned (§22.3).

As a member of an object, an interpolated string has access to all of the object's members.  It reads them when printed, `print(lamp.label)`.

In a routine's body, an interpolated string is a lambda, and captures the locals it reads as a lambda does (§4.14).

**Example**

```bgl
#include <string>

int watts = 60;

object lamp {
    int watts = 60;
    short_name = $"{watts}W lamp";        // a library property that takes a string or a routine
}

// … in a routine:
stringObj snap          = $"{watts}W";   // the text now: "60W"
interpolatedString live = $"{watts}W";   // a routine that prints it
watts = 100;

print(snap);   // -> 60W
print(live);   // -> 100W
```

## 2.10 Type Rules

The rules here apply across all types:  when a value of one type can be used where another is expected (§2.10.1), how a value converts from one type to another (§2.10.2), and what assigning a class-typed value does (§2.10.3).

### 2.10.1 Type Compatibility

Compatibility is checked at every assignment, declaration initializer, and function-call argument.  A value of type `A` is compatible with a target of type `B` when any of the following rules holds, tested in order.  If none of them holds, the assignment or call is a compile-time error.

**1. `var`:**  either side is `var` (§2.6).

```bgl
var any = 5;
int a = any;
```

**2. `null`:**  the value is `null` (§2.5).

```bgl
class Dog : object { }
Dog d = null;
```

**3. Assignment operator:**  `B` declares an `operator =` which accepts `A`.  Candidates are tried in this order, and the first one found is used:

1. an emitter whose parameter type is exactly `A`;
2. an emitter whose parameter type is `var`;
3. an emitter whose parameter type is the same generic type as `A` with different type arguments (`operator = (array<T>)` accepts an `array<int>`);
4. a non-emitter whose parameter type is exactly `A`;
5. a non-emitter whose parameter type is `var`;
6. an emitter whose parameter type is a base class of `A`;
7. only when `A` is not otherwise compatible with `B`:  an emitter, then a non-emitter, whose parameter type `A` is compatible with under these rules (this is how a literal reaches an `operator = (int)`).

```bgl
float n = 42;                    // float declares operator = (intLiteral)
uint u = -1;                     // compile-time error: uint accepts no negative literal (§2.4.1)
```

**4. Exact match:**  `A` and `B` are the same type.

```bgl
bnum itemFlag { portable, fragile }
enum direction { north, south }
itemFlag f = fragile;
itemFlag g = f;
itemFlag h = north;              // compile-time error: a different enumeration
```

**5. Class hierarchy:**  `A` inherits from `B`, directly or through a chain of base classes and aliases.  The reverse is not compatible.

```bgl
class Animal : object { }
class Dog : Animal { }
Dog rex { }
Animal pet = rex;                // a Dog is an Animal
Dog d = pet;                     // compile-time error: an Animal need not be a Dog
```

**6. Conversion operator:**  `A` declares an implicit `operator()` returning `B` (§2.10.2); an emitter conversion is preferred to a non-emitter one.

```bgl
class Celsius { int degrees = 0; int operator () { return degrees * 9 / 5 + 32; } }
Celsius t { }
int degrees = t;                 // Celsius converts to int
string s = t;                    // compile-time error: no conversion to string
```

**7. Generic types:**  a function name is compatible with a `func<…>` type of the same signature (§2.9); an `array<T>` is compatible with an `array<U>` when `T` is compatible with `U`.

```bgl
void tick() { }
func<void> callback = tick;      // tick's signature is func<void>
func<int, int> bad = tick;       // compile-time error: a different signature
array<int> numbers = { 1, 2 };
array<string> words = numbers;   // compile-time error: int does not fit string
```

**8. `bnum` widening:**  `A` is a `bnum` and `B` is `int` (§2.7.3).

```bgl
bnum itemFlag { portable, fragile }
int bits = portable | fragile;   // a bnum widens to int
itemFlag back = bits;            // compile-time error: int to bnum needs a cast (§2.7.3)
```

**9. Unions:**  `B` is a union with a member compatible with `A`, or both are unions and every member of `A` is compatible with some member of `B` (§2.8).

```bgl
class Dog : object { }
Dog rex { }
string | func<void> v = "hello"; // a string is one of the members
string | func<void> w = rex;     // compile-time error: Dog fits no member
```

### 2.10.2 Conversion

A type can declare a **conversion operator**, `operator()`, returning another type (§9.4 covers the declaration syntax).  A conversion is **implicit** by default; one qualified `explicit` is applied only under a cast, as in `(string)t` below.

The other casts (narrowing a union, `int` to `bnum`, `float` to `int`) are covered with their types; the full cast syntax is in §4.11.  §2.10.1 gives the order in which conversions are tried, and §6.4 the order among overloads.

**Example**

```bgl
class Celsius {
    int degrees = 0;
    int operator () { return degrees * 9 / 5 + 32; }          // implicit: Celsius -> int
    explicit string operator () { return "a temperature"; }   // explicit: only under a cast
}

Celsius t { }
int n = t;              // implicit conversion
string s = t;           // compile-time error
string u = (string)t;   // explicit cast
```

### 2.10.3 Primitive, Reference, and Value Types

Every type can have methods (§2.4); what differs is what a variable of the type holds:

| Declaration | Kind | A variable holds | `=` | Passing and returning | `ref` |
|---|---|---|---|---|---|
| `primitive class int` | primitive | the value itself | copies the value | copies the value | compile-time error |
| `class Room` | object (reference) | its own object when declared with no value; else the instance it is given | refers to the same instance | shares the instance | a reference, empty until bound |
| `class Note` with `operator = (Note o)` | object that copies | as an object | copies into its own instance | shares the instance | a reference, empty until bound |
| `array<int> a[4]` | object that copies | its own storage | copies into its own storage | shares the array | a reference, empty until bound |
| `struct Vec2` | struct (value) | its own instance | copies into its own instance | copies | shares one instance |

- **Primitive types** (§8.2.5):  `int`, `uint`, `bool`, `char`, `float`, `string` (the address of fixed text), `dictionaryWord`, `attribute`, `property`, `bglClass` (a class name's value), the literal pseudo-types, `glulxImage`, and every enum.
- **Objects:**  instances of a class.  A global, member, or local declared with no value has an object of its own:  at file scope the declaration is that object (`Room hall;` is `Room hall { }`), a member's is created with its host, and a local's is reset at routine entry.  One declared with a value (`Room r = hall;`) refers to that instance and creates nothing; so do parameters and loop variables.  A slot which may be empty is declared `ref` (§3.7).
- **Objects that copy:**  a class which declares a copy operator (§8.2.8), and an array with storage (§12.5).
- **Structs** (§8.2.7):  a global, local, member, or parameter owns an instance of its own, whose members are zeroed (a local's at routine entry, and again by its declaration on each pass of a loop it is declared in).  A struct is never null and has no subclasses.

Classes which manage their own storage through `init`/`deinit` emitters (e.g., `stringObj`, `array<T>`) follow their own emitters (§8.5).

**Example**

```bgl
struct Vec2 { int x = 0; int y = 0; }   // copies field by field
class Marker : object { int x = 0; } // a reference class
Marker home { }
Vec2 unit;

void compare() {
    Vec2 v;              // its own instance: x = 0, y = 0 on entry
    unit.x = 1;
    v = unit;            // copies the members: v.x -> 1
    v.x = 5;             // unit.x is still 1

    Marker m = home;     // m and home are the same instance
    m.x = 5;             // home.x -> 5
}
```

## 2.11 `selfType`

**Syntax**

```syntax
selfType
```

`selfType` is written in a class's member declarations, including those of a class which other classes list as a parent (§8.2.1), to mean "the class this member is used on".  At each use it is the static class of the receiver:  the class of the variable the member is called or read through, or an object's class.  Inside the declaring class's own body it is that class.

It can be a parameter type, a return type, a member's type, or an array's element type.  It affects type checking only.

- A **parameter** of type `selfType` accepts the receiver's class or a subclass of it:  `bglAllocated.copy(selfType src)` (§21.8) copies a subclass into a base class but not the reverse.
- A **return type** keeps the subclass's type through a method a base class declares, so a chain can go on to the subclass's own methods.
- An **operator's parameter** (`int operator <=> (selfType o)`) compares a class with its own kind.
- A **member or element type** (`selfType next; array<selfType> kids[4];`) is the subclass's class on a subclass instance.

If you know TypeScript, its polymorphic `this` type is the nearest equivalent.

**Example**

```bgl
#include <array>
class Widget : object { int width; selfType wide(int n) { width = n; return self; } }
class Button : Widget { int col; selfType color(int c) { col = c; return self; } }
Button ok { }

class Node : object { int key; selfType next; array<selfType> kids[4]; }
class Room2 : Node { int area = 5; }
Room2 r1 { }
Room2 r2 { area = 3; }

void Main() {
    ok.wide(5).color(3);    // wide() returns Button here, so color() is found
    r1.next = r2;
    print(r1.next.area);    // -> 3: next is a Room2 on a Room2
}
```

---

# 3 Declarations, Variables and Scope

## 3.1 Program Structure

A program is one or more source files.  The declarations at the outermost level of a file (types, classes, enums, variables, functions, objects, verbs, and grammar) make up the **global scope**, visible throughout the compilation.  They can appear in any order, and you can use a name before it is declared (§18.3).

Every program has a `Main` function as its entry point (§6.7).

A global name must be unique across every kind of global declaration, ignoring case:  a variable, array, function, class, object, enum, or verb with the name of an existing global of any kind is a compile-time error (§19.2), unless one of the two is given an `asI6` name (§3.11).  There are three exceptions:  functions of one name overload by parameter types (§6.4), an emitter function can share its name with a variable or object (e.g., `print` or `log`, §21.4), and a verb can share its name with a class (§13.2).

The emitter function and the variable or object which share a name are told apart by **use**, not by declaration order:

| Use | Resolves to |
|---|---|
| `name(args)` | the function |
| `name.member`, `name = v` | the variable or object |
| a bare `name`, with no variable of that name declared | the function, as a `func<>` reference |

Calling a shadowed name unqualified resolves to the variable, as ordinary shadowing does (§3.10).

**Names shared with the library.**  Uniqueness includes the globals a binding declares.  A program's global array, variable, or object can't share its name, ignoring case, with a library verb:  an array `order` clashes with the library's `Order`.  A function can, since it is always called.  Nor can any of the program's globals (function, variable, array, or object) share its name with a global, attribute, or object the library binding declares:  a function `Score` clashes with the library global `score`.  A program's variable, array, or object can't take a library routine's name either (an object `cap` clashes with `Cap`); a function can, since it defines the routine.  Each clash is a compile-time error that names the other declaration.  A program can still define the library's entry points by name (§23.3.4).  A verb named like a library verb is covered in §13.2.

## 3.2 Declaration Qualifiers

**Syntax**

```syntax
[ QUALIFIER … ] DECLARATION
```

A declaration can be preceded by one or more qualifiers, in any order:  `emitter replace void foo()` and `replace emitter void foo()` are equivalent.

| Qualifier | Meaning | See |
|---|---|---|
| `const` | Read-only variable or member. | §3.4 |
| `static` | Member belongs to the type rather than to an instance. | §8.3.3 |
| `extern` | Declared in Inform 6; registered for type-checking only, produces no output. | §16.4 |
| `emitter` | The body is an I6 template expanded at each use. | §7.2 |
| `extend` | Adds members to an existing class, object, enum, or array. | §8.7.1, §11.10, §12.11 |
| `alias` | Another name for an existing type or value. | §8.2.4, §10.2 |
| `replace` | Replaces an already-declared function or member. | §6.5, §8.7.2 |
| `default` | A base-class member that a derived declaration can override without warning. | §8.7.3 |
| `explicit` | A conversion operator that fires only under a cast. | §9.4 |
| `superposed` | A routine, global, object, or class which is emitted only if it is used. | §3.12 |
| `typesealed` | A member whose type a derived class can't change. | §8.3.7 |
| `primitive` | A class whose variables hold the value itself (`primitive class`). | §8.2.5 |
| `struct` | A value type:  each variable owns an instance, and assignment copies. | §8.2.7 |
| `inline` | A member variable that is a positional slot for inline object construction. | §8.3.5, §11.3.1 |
| `ref` | A variable, member, or parameter which refers to an instance owned elsewhere, empty until bound. | §3.7 |
| `required` | A class member every object must get a value for. | §8.3.6 |
| `additive` | A property whose values accumulate along the class chain. | §11.7.2 |
| `literal` | A parameter, variable, member, or array element type that holds only values known at compile time. | §3.13, §6.3 |

The following combinations are compile-time errors:  `explicit` on anything but `operator()`; `const` with `static`; `static` with `emitter`; `explicit` with `const` or `static`; `alias` with `extern`; `alias` with `emitter`; `default` in an object or verb body.

`global` is not a qualifier:  a variable is global by being declared at file scope (§3.3), and writing `global` before a declaration is a compile-time error.

## 3.3 Global Variables

**Syntax**

```syntax
TYPE NAME [ = INITIALIZER ] ;
auto NAME = INITIALIZER ;
```

A variable declared at file scope is a global.  An initializer that is a constant expression (a literal, constant arithmetic, an object or routine name, or a `#define` value) is part of the declaration.  **Any other initializer is applied at startup**, in `bglInit()`, in declaration order:  it can call a routine or read a global declared before it; however, the variable will hold `0` until `bglInit()` runs (§21.2).  A class-typed global whose type declares a parameterless `init` is also applied at startup.

`auto` infers the type from the initializer (§3.6).

**Example**

```bgl
bool isGood = true;
int score = 5 + 3;
string playerName;
```

## 3.4 Constants

**Syntax**

```syntax
const TYPE NAME = VALUE ;
extern const TYPE NAME ;
```

`const` fixes what a name stands for.  Assigning to it (including `++`, `--`, and compound assignment) is a compile-time error, and a `const` declaration must give its value.  It applies to the types whose value is one word:  the primitives (`int`, `float`, `char`, `bool`, …), `string`, enums, and references to objects.

On a reference, `const` fixes which object the name stands for, *not* the object itself:  `const Thing pick = rock;` will always stand for `rock`, although `rock`'s members can still change.

Two kinds of type are *storage* rather than a one-word value, and `const` is a compile-time error on both:

| Type | What a variable of it holds | Why `const` has nothing to fix |
|---|---|---|
| an array (`array<T>`, `rawArray<T>`) | the array's elements | the name always stands for the same storage; assigning to it copies elements in (§12) |
| a struct (§8.2.7) | an instance of its own, with storage for its fields | the name always stands for its own instance; assigning to it copies fields in |

**Example**

<!-- doctest: compile -->
```bgl
struct Money {
    int cents;
    Money operator = (int c) { cents = c; return self; }
}
class Thing : object { int weight; }
Thing rock { weight = 5; }

const int     PRICE_CENTS = 325;    // a one-word value: an I6 Constant
const Thing   pick = rock;          // a reference: pick always stands for rock

void main() {
    pick.weight = 7;                // fine: const fixes the name, not rock's members
    Money price = PRICE_CENTS;      // a Money made from the constant's value
}
```

> ***Note:** `const Money price = 325;` is an error:  `price` would hold its own `Money`, whose `cents` can always be changed.  Keep the constant on the plain value, as `PRICE_CENTS` is above, and assign it where a `Money` is needed.*

> ***Note:** `const array<int> bands = {5, 10, 20};` is an error for the same reason:  `bands` is the array's own storage.  Drop the `const`.*

`extern const` declares a constant defined in Inform 6:  it is registered for type-checking, produces no output, and takes no initializer.

**Example**

```bgl
const int MAX_SCORE = 2;
extern const int STUCK_PE;
```

## 3.5 Extern Variables

**Syntax**

```syntax
extern TYPE NAME ;
```

An `extern` variable is declared in Inform 6 and registered for type-checking only.  It produces no output and can't be initialized.  You can read it and assign to it; `extern const` (§3.4) is read-only.  The other `extern` declarations are covered in §16.4.

`extern CLASS NAME;` declares an I6 *variable* of that class's type, which a member initializer can't read.  An I6 object typed by its class is declared `extern const CLASS NAME;`, which an initializer can name (`parent = trayA;`).

**Example**

```bgl
extern int score;
extern object location;
```

## 3.6 Local Variables

**Syntax**

```syntax
TYPE NAME [ = INITIALIZER ] ;
auto NAME = INITIALIZER ;
```

A local variable is visible from its declaration to the end of the enclosing block (§5.2); reading it after that block has closed is a compile-time error.

`auto` infers the type from the initializer and requires one; `auto x;` is a compile-time error.  The inferred type is fixed at the declaration, and later assignments will be checked against it.  `auto` is accepted in local, global, and member declarations.

If the variable's type declares an `init` emitter, it fires immediately after the declaration and before the initializer is assigned (§8.5).  A routine can declare more locals than the Z-machine's per-routine limit (§18.10).

**Example**

```bgl
class Room : object { }
Room myRoom { }

auto x = 5;          // int
auto s = "hello";    // stringLiteral
auto r = myRoom;     // Room, the object's class
```

## 3.7 References: `ref` and `:=`

**Syntax**

```syntax
ref TYPE NAME := EXPRESSION ;    // bound at declaration
ref TYPE NAME ;                    // starts empty
SLOT := EXPRESSION ;               // rebind
```

A class-typed slot is either an **owning slot** or a **reference slot**.  A global, member, or local declared without `ref` and with no value owns an instance:  always for a struct (§8.2.7), and for a reference class as its own object (§2.10.3).  A reference-class slot declared with a value refers to that instance, as do parameters of a reference class and loop variables.  A slot of a primitive type holds the value itself (§2.10.3) and can't take `ref`.

**Reference slots.**  A slot declared `ref` owns nothing; it names an instance owned elsewhere.  It is empty (`null`) until bound.  `ref` is valid on file-scope and local variables, on parameters, and on class and object members, of any class type; on a primitive type, or on an `extern` or `const` declaration, it is a compile-time error.  A `ref` parameter is bound to its argument.  A slot which can be empty is declared `ref`.  A member of a struct whose type is that class must be `ref`.

**Binding and assignment.**  `:=` binds a `ref` slot:  it stores the reference and never dispatches `operator =`.  Both sides must be the same class, or the right side a subclass; binding an unrelated class or a non-instance value is a compile-time error.  `:=` is not overloadable, and on a slot not declared `ref` it is a compile-time error.

`=`, by contrast, is the assignment of the slot's type, the same in a declaration and a statement and on a plain or a `ref` slot:  a reference class's re-points, and a struct's copies, into the referent when the slot is a bound reference.  Reads and member writes through a reference chain normally (`node.next.id`).

**Declaration pairing.**  A `ref` declaration, a member's included, binds with `:=`, whatever its type; `=` on a `ref` declaration is a compile-time error.

**Example**

```bgl
struct Node { int id; ref Node next; Node operator = (Node o){ id = o.id; return self; } }

// … in a routine:
Node first;   first.id = 1;
Node second;  second.id = 2;
ref Node r := first;          // bind
r.id = 9;                     // through the reference: first.id is now 9
r := second;                  // rebind: first keeps its value
first.next := second;         // a ref member is bound the same way
```

**See also** §4.13, §5.15.

## 3.8 Identifier Resolution

An identifier is resolved by searching three tiers in order, and the first match wins.  An identifier which matches no tier is undeclared, a compile-time error.

### 3.8.1 Local Scope

The first tier is the enclosing function, searched in this order:

1. Parameters of the enclosing function.
2. Local variables of the current block.
3. Local variables of enclosing blocks of the same function.

### 3.8.2 Class and Object Scope

Inside a method body, the second tier is the members of the enclosing class or object, including members inherited through the base chain.  A bare member name resolves as `self.name`; `self` is the receiver (§6.6).  Members declared later in the same body resolve normally.

Class scope comes before file scope:  inside a class's or object's code, a bare name which is both a member (method or variable) and a global (function, variable, or type) means the member, as in C++ and C#.  `::name` (§3.9) reaches the file-scope declaration.

**Example**

<!-- doctest: compile -->
```bgl
int twice(int n) { return n * 2; }
class Spot { }

object counter {
    int spot = 0;
    int twice(int n) { return n + n; }
    int run(object x) {
        int a = twice(3);           // the member
        int b = ::twice(3);         // the global function
        if(x.is(::Spot)) a++;       // the type, which the member `spot` hides
        return a + b;
    }
}
```

### 3.8.3 Global Scope

The last tier is everything declared at file scope, searched in this order:

1. Enum values, which share one flat global namespace.
2. Global variables, constants, `extern` declarations, and verb names.
3. Members imported with `#using`, which rank below every global; the directive is covered in §10.4.

**Verb names.**  A verb is an object and follows the same rules as any other identifier; a local or parameter with the same name as a verb takes priority.  A verb can share its name with a `#define` symbol.  The name then means the symbol, except where a verb is expected:  assigned to a `verb`, passed to a `verb` parameter, in an `array<verb>`, as a `case` label on an action, or on either side of `==` or `!=` with a verb.

**Members named after types.**  A member or method can share a name with a type, including a built-in type keyword such as `object`, since a name following `.` is unambiguously a member.  Type names remain reserved for top-level identifiers (§1.5).

## 3.9 The Global-Scope Qualifier `::`

**Syntax**

```syntax
::NAME
::NAME.MEMBER
```

A leading `::` resolves `NAME` at global scope, skipping §3.8.1 and §3.8.2.  It is the counterpart of `self.name`, which selects the member.  It is valid as an lvalue or an rvalue and applies to the head of a dotted path.  Inside a class's or object's code, it reaches a global hidden by a member of the same name (§3.8.2).  If no such global exists, it is an undeclared-identifier error; `::` never falls back to a member.

**Example**

```bgl
int count = 0;

object tally {
    int count = 0;
    void bump() {
        self.count++;       // this object's member
        ::count++;          // the global
    }
}
```

## 3.10 Shadowing

Local variables, parameters, and `for`-loop variables are checked against the enclosing scopes.  A local can shadow a global; inside its scope the bare name is the local and `::name` (§3.9) is the global.

**Errors.**
- Re-declaring a local which an enclosing block still has open, or a parameter of the same function.  A nested declaration doesn't shadow the outer name, so the two must have different names.  Blocks which don't enclose one another can reuse a name freely.

**Warnings.**
- Shadowing a global variable, function (a library routine's too), object, or type name; the warning names what is shadowed.  Globals of the symbolic-constant kinds `attribute`, `property`, `verb`, and `grammarToken` are exempt, since they name compile-time constants, not runtime storage.
- A local or loop variable shadowing a direct member of the enclosing class or object, or a member inherited from a base class; `self.name` reaches the member.  A parameter can share a member's name without a warning, as in C# and C++:  `void resize(int width) { self.width = width; }`.
- A lambda-local variable shadowing a capturable outer local or parameter (§4.14).

A member overriding a base-class member is covered in §8.7.3, and two file-scope declarations of one name in §3.1.

**Example**

```bgl
int score = 0;
void foo() {
    int score = 5;       // warning: shadows global
    score++;             // the local: 6
    ::score = score;     // the global: 6
}
```

## 3.11 The `asI6` and `asBgl` Clauses

**Syntax**

```syntax
TYPE NAME asI6 I6-NAME ;
object NAME asI6 I6-NAME { … }
TYPE MEMBER asI6 I6-NAME ;              // class or object member
TYPE METHOD ( … ) asI6 I6-NAME { … }
extern TYPE I6-NAME asBgl NAME ;
extern TYPE I6-ROUTINE ( … ) asBgl NAME ;
```

A declaration carries two names:  the one Beguile source uses and the one which reaches the Inform 6 output.  They are the same unless a clause says otherwise.

**The declared name is always the name in the language which defines the thing; the clause names it in the other one.**  A plain declaration is defined in Beguile, so the declared name is the Beguile one and `asI6` *creates* the name Inform 6 will get.  An `extern` declaration adopts a symbol Inform 6 already defines, so the declared name is the I6 one and `asBgl` *names* it for Beguile.

```bgl
attribute heightened asI6 excited;   // a new attribute: Beguile says heightened, I6 says excited
extern attribute light asBgl lit;    // I6 already has light; Beguile says lit
```

`asI6` on an `extern` is a compile-time error, and so is `asBgl` without `extern`.

`asI6` is valid on any typed instance declaration, on a named object definition (including instances of subclasses such as `room Name asI6 place { }`), and on class and object members, where it follows the member name.  It is ignored on operator methods; on a type declaration (`extern class`, `alias class`) or on a free function it is a compile-time error.  On an `extern` function `asBgl` follows the parameters, as `asI6` does on a method:  `extern int I6Twice(int x) asBgl twice;`.  Usually, you will reach for either clause because the name required on one side is a keyword or reserved word on the other (§16.9).

A member named like a class declared in Beguile, such as `Box box;` beside `class Box`, needs no `asI6` (§16.9).

Both clauses cross the language boundary; `alias` never does.  `alias class Foo for Bar` (§8.2.4), `alias name for Type;`, and `alias name = Target;` (§10.2) each introduce a second *Beguile* name for something already named in Beguile, and leave the emitted I6 untouched.

**Example**

```bgl
extern attribute light asBgl lit;
object myHook asI6 hook { … }
class Widget : object {
    int count asI6 internalCount;
    void refresh() asI6 _widgetRefresh { … }
}
```

## 3.12 `superposed`

**Syntax**

```syntax
superposed DECLARATION
```

A declaration with the `superposed` qualifier (§3.2) is part of the program only if something references it; one that nothing names is absent from the story file and costs nothing.  It can qualify a global function, a file-scope global variable or array, a whole object declaration, or a whole class declaration.

- **Reference.**  A declaration is **materialized**, made part of the program, the first time its name is used.  For a class, a use is a static instance, a subclass, `new` on a pooled class, an `is` test, or any typed use.
- **Transitive.**  A materialized declaration's own references materialize in turn; a superposed declaration can freely reference other superposed declarations.
- **Whole declarations, plus methods.**  Inside a class body it applies only to a `static` method (`static superposed TYPE NAME(…)`), which then materializes only when referenced (§8.4); on a non-`static` method it has no effect and the compiler will issue a warning.  Inside an object body it applies to a method, which stays in the object only if it is called:  as `OBJECT.METHOD` from code which is part of the program, or from another of the object's methods which stays.  A method called only from Inform 6 code which the compiler doesn't emit is not seen, and drops.  On an object's data member it is a compile-time error.
- **Rejected on** `extern`, `emitter`, and `alias` classes, which have no definition to withhold, and on `extend class`, where it belongs to the original declaration.

The `omitUnusedRoutines` setting (§15.3) is complementary:  `superposed` withholds a declaration which is never referenced, while `omitUnusedRoutines` asks the I6 compiler to drop routines which were emitted and remain unreferenced.

**Example**

```bgl
superposed array<char> vowels = "aeiou";

superposed bool charIsVowel(char c){
    for(char v in vowels) if(v == c) rtrue;   // materializes vowels as well
    rfalse;
}

superposed object worldHelpers {
    superposed array<object> getAll() { … }      // dropped unless something calls worldHelpers.getAll
    superposed int count() { … }
}
extend bgl { alias world = worldHelpers; }   // bgl.world.getAll() materializes worldHelpers
```

> ***Note:** An `alias` value member (§10.2) references its target only where the alias is used, so an alias to a superposed object keeps the object absent until the alias is used; an `auto` member references its target unconditionally.*

**See also** §10.2, §15.3, §18.7 (placement of a materialized class), §18.9 (emission).

## 3.13 `literal`

`literal` qualifies a variable, a member, or an array's element type, as well as a parameter (§6.3).  On a variable or member it covers the whole type:

<!-- doctest: skip -->
```bgl
literal func<void> onStart = intro;              // a global
class door { literal func<void> onOpen; }        // a member
array<literal func<eVerdict>> rules = { r1 };    // the elements
```

An initializer, an assignment, a subscript write, `+=`, or an array method which stores its argument must then supply a literal value (§6.3).  Reading a `literal` variable or member yields a literal value.

---

# 4 Expressions and Operators

## 4.1 Evaluation

Every expression has a **resolved type**.  `&&` and `||` evaluate the right operand only when the left operand doesn't decide the result; `??` evaluates its right operand only when the left is null (§4.10); and a ternary evaluates only the selected branch (§4.9).

Apart from those, the order in which an expression's operands are evaluated is unspecified, as in C++:  in `f() + g()`, either call can run first, and so can the arguments of one call.

## 4.2 Operands

An expression is one or more operands joined by operators.  An operand is one of the following:

| Operand | Resolved type |
|---|---|
| Literal: integer, string, `@"raw"`, `$"interpolated"`, character, dictionary word | The literal's pseudo-type (§2.4) |
| Identifier | The declared type (resolution: §3.8) |
| `null` | Compatible with any type (§2.5) |
| `self` | The enclosing class (§6.6) |
| Member access `expr.member` | The member's type |
| Call `f(args)` / `expr.m(args)` | The function's return type |
| Subscript `arr[i]` | The array's element type (§12.3) |
| `(expr)` | The type of `expr` |
| `(Type)expr` | The cast target (§4.11) |
| `&expr` | `int` (§4.12) |
| `new Type(args)` | `Type` (§4.13) |
| Lambda literal | `func<…>` (§4.14) |

## 4.3 Operator Precedence

Higher levels bind more tightly.

| Prec | Operators | Kind | Assoc | Meaning |
|:---:|---|---|---|---|
| 14 | `.` `?.` `[]` `()` `v?` `++` `--` | postfix | left | Member access, optional access, subscript, call, postfix query, postfix increment/decrement |
| 13 | `!` `-` `&` `(Type)` `++` `--` | prefix | right | Logical not, negation, address-of, cast, prefix increment/decrement |
| 11 | `*` `/` `%` | infix | left | Multiplicative |
| 10 | `+` `-` | infix | left | Additive |
| 9 | `<<` `>>` `<=>` | infix | left | Shift, three-way comparison |
| 8 | `<` `<=` `>` `>=` | infix | left | Relational |
| 7 | `==` `!=` `?=` `=~` `:==` | infix | left | Equality, identity (§4.6) |
| 6 | `&` | infix | left | Bitwise and |
| 5 | `^` | infix | left | Bitwise exclusive or |
| 4 | `\|` | infix | left | Bitwise or |
| 3 | `&&` | infix | left | Logical and |
| 2 | `\|\|` | infix | left | Logical or |
| 1 | `? :` | ternary | right | Conditional (§4.9) |
| 1 | `??` | infix | right | Null coalescing (§4.10) |
| 0 | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` `:=` | infix | right | Assignment (§5.5), compound assignment (§5.6), reference binding (§3.7) |

There is no unary bitwise-not operator, and Inform 6's `~` and `~~` are not Beguile operators; write `!` for logical not.

## 4.4 Binary Operator Resolution

A binary operator is resolved against the resolved type of its left operand, in these steps:

1. Look for `operator op` (emitter or method) on the left type whose parameter accepts the right type:  its exact type, a literal's base type, a type it converts to, or a class it derives from.
2. Otherwise, if the left type has a conversion operator (`operator()`) to the right type, fall back to the built-in operator.
3. Otherwise, if the right type has a conversion operator to a type for which the left type does declare `operator op`, apply that conversion and use the operator.
4. Otherwise, if the left type is known, it is a compile-time error.  A `var` operand falls back to the built-in operator.

Assignment and arithmetic operators keep the left operand's type, except that a `float` operand on either side of an arithmetic operator makes the result `float` (§2.3).  The overloadable operators are listed in §9.1.

> ***Note:** When a type declares several overloads of one operator, the right-hand operand selects between them in four passes:  the operand's **exact** type; the **base type of a literal** (`intLiteral` gives `int`); a type the base is **convertible** to, or an **ancestor** of the operand's class; and finally a parameter declared `var`.  Each pass looks at the type's own operators before the inherited ones.  An operand whose type is itself `var`, or whose type is unknown, still matches in the first pass.*

**Example**

```bgl
class Money {
    int cents = 0;
    emitter bool operator == (Money v){ $self.cents == $v.cents }
    int operator () { return cents; }          // implicit conversion to int
}
Money a { } Money b { } int five = 5;
bool same = a == b;       // 1: Money declares operator == (Money)
int  sum  = a + five;     // 2: no operator + on Money; a converts to int, built-in + applies
```

## 4.5 Arithmetic Operators

The built-in operators need no `#include`.  They are ordinary overloadable operators (§9.1), which a type can declare for itself.  The compound-assignment forms are in §5.6, and the postfix query, optional chaining, and null coalescing are in §4.10.

| Operators | Built in for | Result | Example |
|---|---|---|---|
| `+` `-` `*` `/` `%` | `int`, `uint` (§21.6.1), `float` (§2.3); `+` and `-` also for `char` with a `char` or `int` right operand (§21.7) | The left operand's type; `float` when either operand is a `float` (§2.3) | `7 / 2` gives `3`; `7 % 2` gives `1`; `'a' + 1` gives `'b'` |

**`int` arithmetic.**  Arithmetic which leaves the range of an `int` (§2.2) wraps (`32767 + 1` gives `-32768` on the Z-machine), on literals as on variables.  `/` truncates toward zero (`-7 / 2` gives `-3`) and `%` takes the sign of the left operand (`-7 % 2` gives `-1`, `7 % -2` gives `1`).  Dividing by zero, with `/` or `%`, is a run-time error of the virtual machine.

## 4.6 Comparison Operators

The comparison operators are built in for these types:

| Operators | Built in for | Result | Example |
|---|---|---|---|
| `==` `!=` `<` `>` `<=` `>=` | `int`, `uint`, `float`, `char`; `==` and `!=` also for `bool`, `string` (identity of the text), and `object` | `eBool` | `score >= 50`; `noun == lamp` |
| `?=` | None; a type gives it its meaning (§9.5) | `eBool` | - |
| `=~` | `char`:  case-insensitive equality (§21.7); `string` with `<string>` (§22.3) | `eBool` | `'A' =~ 'a'` gives `true` |
| `<=>` | None; `<string>` declares it for `string` (§22.3), and any type can declare it (§9.6) | `int`:  negative, `0`, or positive | `(a <=> b) < 0` |

**Identity.**  `a :== b` is true when `a` and `b` are the same instance.  It compares references and never calls an `operator ==`, just as `:=` bypasses `operator =` (§3.7).  Both operands are instances:  objects, instances of any class (structs and `stringObj` included), arrays, or `null`.  On a primitive it is a compile-time error ("use '=='").  `:==` can't be overloaded, and neither can `:=`.

```bgl
#include <string>

// … in a routine:
stringObj s = "hi";
stringObj t = "hi";
ref stringObj u := s;
print(s == t);       // true: the same text
print(s :== t);      // false: two strings
print(u :== s);      // true: u refers to s
```

## 4.7 Logical Operators

`&&`, `||`, and prefix `!` are built in for these types:

| Operators | Built in for | Result | Example |
|---|---|---|---|
| `&&` `\|\|` `!` | `bool` and `eBool` (§2.2); `!` on a type that declares `operator !()` uses that emitter (§9.5) | `eBool` | `a > 0 && b < 10` |

An operand of `&&` or `||` which is a reference (an object, a class instance, a `ref`, a string, or a function value) is its null test, as it is when it stands alone as a condition:  `if (ready && door.lock)` is `if (ready && door.lock?)`.

## 4.8 Bitwise and Shift Operators

`& | ^ << >>` are built in for these types:

| Operators | Built in for | Result | Example |
|---|---|---|---|
| `&` `\|` `^` `<<` `>>` | `int`, `uint`; `&`, `\|`, and `^` also combine `bnum` values which share a base (§2.7.2) | The left operand's type; for two `bnum` values, their shared ancestor's type (§2.7.2) | `flags & lit`; `1 << 4` gives `16` |

`>>` on an `int` is arithmetic, keeping the sign (`-256 >> 4` gives `-16`); on a `uint` it fills with zeros (§21.6.1).  The compound forms `&= |= ^= <<= >>=` are in §5.6.

## 4.9 Ternary Operator

**Syntax**

```syntax
CONDITION ? TRUE-EXPR : FALSE-EXPR
```

The ternary operator selects one of two values, and only the selected branch is evaluated.

**Type of the result.**  When the branches have the same type, that is the type.  When one branch's type is assignable to the other's (e.g., a derived class and its base), the result takes the more general of the two, regardless of which side it is written on.  A literal counts as the type it denotes, and a branch typed `var` imposes nothing.

When the branches have unrelated types, the result is the **union** of the two (§2.8).  A union-typed target accepts it (§2.8.1); a target of one branch's type rejects it.

**Example**

<!-- doctest: skip -->
```bgl
print(x > 0 ? "positive" : "non-positive");
int result = (cond ? a : b) + extra;

string | func<void> action = useText ? "nothing happens" : doSomething;  // union of both branches
int wrong = cond ? 1 : "text";   // error: 'int|string' is not assignable to 'int'
```

## 4.10 Optional Chaining, Null Coalescing and Postfix Query

**Syntax**

```syntax
EXPR?.MEMBER
EXPR?.METHOD(ARGS)
EXPR ?? FALLBACK
EXPR?
```

All three operators are type-driven:  the operand's type must declare an `operator ?()` emitter (§9.5), whose result is the null test.  For `object` the test is "not `null`", and for `string` it is "non-zero handle".

**`?.`** accesses a member or calls a method only if the left operand is non-null.  In an expression, each `?.` step tests the value so far, and if it is null the whole chain yields `null`.  As a statement (`x?.remove();`), the target can be any expression, such as a call or an element (`find()?.remove();`, `a[i]?.remove();`).  Note that a plain `.` after a `?.` adds no guard:  if the preceding `?.` yielded null, the member access runs on `null`, and the result is undefined.

**`??`** yields the left operand if it is non-null, otherwise the right operand, which is evaluated only in that case.  The left operand's type must have a null test:  a `?.` chain which ends in an `int` or another primitive can't take `??`.

**Postfix `?`** yields the null test as an `eBool`; `noun?` is `noun != null` for objects.  A function value (`func<…>`) is tested the same way.  Where a `?` could begin a ternary, as the right operand of a binary operator, it is the ternary; the postfix query applies only when the `?` ends the expression.

**Example**

<!-- doctest: skip -->
```bgl
object dest = actor?.destination ?? location;
string desc = noun?.parent.description;   // parent guarded, .description not
if (noun?) print("something is here");
```

## 4.11 Casts

**Syntax**

```syntax
(TYPE)EXPR
(INSTANCE-NAME)EXPR
```

A cast applies to the whole operand which follows it, member accesses and calls included, as in C++ and C#:  `(Dog)a.barks` casts the value of `a.barks`.  To treat the receiver as another type, parenthesize the cast and the receiver together:  `((Dog)a).barks` reads `Dog`'s member.  The result of a cast is not assignable, so `(Dog)a.barks = 9` is a compile-time error; however, `(T)name = v;` assigns through `T`'s `operator =`.

> ***Note:** `(float)(314 / 100)` divides the integers first and then converts the result, giving `3.0`.*

A cast has four uses.

**Ancestor-qualified dispatch.**  Casting the receiver to a strict ancestor of its static type selects that ancestor's version:  `((Animal)myDog).speak()`.  On `self` inside an override, this calls the overridden method without recursion.  An identity cast, a downcast, or a cast to an unrelated type keeps dynamic dispatch.  The receiver must be a class or object with real methods; an ancestor cast on an `emitter` method is a compile-time error.  On a member variable the receiver cast resolves the member against the cast type, which reaches a member the subtype hides (§8.7.4).  A base-typed *variable* stays dynamic:  `Animal a = myDog; a.speak();` will run `Dog`'s override.

> ***Note:** In C#, `((Animal)myDog).Speak()` still runs `Dog`'s override; the base version is reached only through `base.Speak()` inside the class.  C++ writes `myDog.Animal::speak()`.*

**Explicit conversion.**  A conversion operator declared `explicit` fires only under a cast:  `string s = (string)myValue;` (§9.4).  A cast also forces resolution through a specific type when the inferred type would resolve differently.

**Reinterpretation.**  Between a reference type and a primitive (`object` and `dictionaryWord`, `int` and `object`), or between two primitives without a conversion operator, a cast changes only the type:  the value is the same word, read as the target type.  It is unchecked, like `(var)`.

**Class vs. instance.**  The target can be a class or a named object.  Casting to a class exposes the class's members; casting to an instance also exposes members declared on that object alone.  The cast is an unchecked downcast:  the member name is checked against the target at compile time; however, the runtime object is assumed to be of that type.  Reaching a member which `object` doesn't have through an `object`-typed value without a cast is a compile-time error.  `(var)` gives an untyped read with no member check.

**Example**

<!-- doctest: compile -->
```bgl
class Room : object { int lit; }
Room library { int shelves; }
object obj;                                // statically object
int a = ((library)obj.parent).shelves;     // instance member
int b = ((Room)obj.parent).lit;            // class member
```

**See also** §2.10.2 (conversion operators), §8.6 (inheritance and overriding).

## 4.12 Address-of `&`

**Syntax**

```syntax
&EXPR
```

Prefix `&EXPR` yields the raw machine address of its operand as an `int`; it is exactly `(int)EXPR`.  On an array, buffer, or object it gives the base address; on a scalar it yields the value itself.  It never gives the storage address of a variable or property.  A `&` with a left operand is the bitwise and (§4.8).

**Example**

<!-- doctest: compile -->
```bgl
extern void FillBuffer(int address, int length);   // an I6 routine

// … in a routine:
array<char> buf[32];
FillBuffer(&buf, 32);                              // the base address of buf
int n = 7;
int m = &n;                                        // -> 7: a scalar yields its value
```

**See also** §16.4.1 (`extern` functions).

## 4.13 `new`

**Syntax**

```syntax
new TYPE(ARGS)
```

`new` allocates an instance of a pooled class and yields a reference to it, or `null` if the pool is exhausted; the result must be checked before use.  The arguments are passed to the class's `create()` method; if the class declares no `create`, `new TYPE()` is the only valid form.  `new` on a class which is not pooled is a compile-time error.  Pooled classes, pool size, and the `create`/`destroy` lifecycle are covered in §8.2.6; the `delete` statement is in §5.15.

**Example**

```bgl
class Marble[10] : object { }

// … in a routine:
Marble m = new Marble();
if (m == null) print("The pool is exhausted.");
```

> ***Note:** For the `create()` argument limit, see §8.2.6.*

## 4.14 Lambdas

**Syntax**

```syntax
(TYPE PARAM, …) => { BODY }
(TYPE PARAM, …) => EXPR
() => { BODY }
=> { BODY }
=> EXPR
```

A lambda is an anonymous function literal, whose type is a `func<>` (§2.9).  The return type is inferred:  from the expression of a `return expr;` in the body, or `void` if the body has no `return`.  A lambda in argument position is passed by reference, like a named `func<>` value.

The expression-bodied form `(PARAMS) => EXPR` is equivalent to `(PARAMS) => { return EXPR; }`.  In both forms every parameter is typed; `x => …` is not a lambda.  A lambda with no parameters can omit the empty parameter list:  `=> BODY` is equivalent to `() => BODY`.

An expression body with no value, such as a call to a `void` function, is the lambda's body rather than its return value.  An assignment has no value either, so a lambda whose expression body is an assignment (`=> hits += 10`) is `void` (`func<void>`), as in C#.

**Capture.**  A lambda body can use locals and parameters of the enclosing function, wherever in that function the lambda appears.  Each captured variable's value is copied when the lambda is created.  For a lambda passed directly as an argument, changes the body makes to a captured variable will be visible in the enclosing scope after the call returns, and in the `catch` when the call throws (§5.16).  Inside a method, a lambda which reads a member captures `self`.

Captures are for callbacks which run before the enclosing function moves on.  A lambda which captures a variable can't outlive it, so it is a compile-time error to return one, to store one in a global or a member, or to pass one to a `literal` parameter (§6.3).  Storing one in a local is allowed, except inside a loop, where storing a capturing lambda (assigning it, writing it to an element, or passing it to an array method which keeps its argument, such as `push`) is a compile-time error.  A lambda used during the statement, such as a predicate passed to `filter`, can capture loop variables freely.  A capture of a `const` local initialized from a compile-time value is exempt from all of these.

**Constraints.**  A lambda literal can't be invoked immediately (`((int n) => { … })(42)`); assign it or pass it first.

**Example**

```bgl
#include <array>
array<int> scores = {3, 1, 2};

void applyToAll(array<int> arr, func<void, int> fn) {
    for (int item in arr) fn(item);
}
void test(int multiplier) {
    applyToAll(scores, (int x) => { print(x * multiplier); });   // captures multiplier
    scores.sort((int a, int b) => b - a);                        // expression-bodied: returns b - a
}
```

## 4.15 Operator References

**Syntax**

```syntax
TYPE::operator OP
TYPE::operator OP(TYPE)
```

An operator reference names an operator which `TYPE` declares, yielding its address for use wherever a `func<>` is expected.  Only a `static` operator is referenceable; referencing an instance operator is a compile-time error.  When a type declares several static overloads of the operator, the parenthesized operand type selects one.  The rules for declaring static operators are in §9.6.

**Example**

```bgl
#include <array>
#include <string>
class Money {
    int cents = 0;
    static bool operator == (Money a, int b) { return a.cents == b; }
}

// … in a routine:
array<string> names = {"cherry", "apple"};

names.sort(string::operator <=>);
func<bool, Money, int> byCents = Money::operator ==(int);
```

**See also** §7.3.1 (`$opref`, the same lookup inside an emitter body).

---

# 5 Statements and Control Flow

## 5.1 Statements

The body of a function, method, or emitter is a sequence of statements executed in order.  A statement ends with a semicolon unless it ends with a closing brace.  A stray `;` is an empty statement and is discarded, at every statement boundary and at file scope.

## 5.2 Block Statement

**Syntax**

```syntax
{ STATEMENT … }
```

A block groups statements and introduces a scope:  a variable declared in the block is visible from its declaration to the block's closing brace (§3.6).

## 5.3 Expression Statement

**Syntax**

```syntax
EXPRESSION ;
```

An expression followed by a semicolon is a statement.  The expressions which can stand alone are function and method calls (chained and optional-chained calls included), increment and decrement (§5.7), and assignment (§5.5, §5.6).

## 5.4 Declaration Statement

**Syntax**

```syntax
TYPE NAME [ = EXPRESSION ] ;
```

A local variable declaration is a statement.  The forms, including `auto`, are covered in §3.6; `ref` and `:=` are in §3.7.

## 5.5 Assignment

**Syntax**

```syntax
LVALUE = EXPRESSION ;
```

The left-hand side is a declared variable or a dotted member path.  The assignment is allowed when the right-hand type is compatible with the left-hand type (§2.10.1).  Reference binding (`:=`) is covered in §3.7.

An assignment is a statement and has no value:  chaining one, `a = b = c;`, or using one as a condition, `if(a = 3)`, is a compile-time error.

**Example**

<!-- doctest: skip -->
```bgl
score = score + 10;
lamp.parent = library;
```

## 5.6 Compound Assignment

**Syntax**

```syntax
LVALUE OP= EXPRESSION ;
```

`OP` is one of `+ - * / % & | ^ << >>`.

A compound assignment modifies the variable in place.  When the left type declares the compound operator, that operator runs.  When it declares none, `n op= v` is `n = n op (v)` (§9.7), and it is a compile-time error only when that assignment is.  All of them are defined for `int` (§2.2) and `uint` (§21.6.1).

**Example**

```bgl
int n = 5;
n += 2;      // -> 7
n <<= 1;     // -> 14
```

## 5.7 Increment and Decrement

**Syntax**

```syntax
VARIABLE++ ;   VARIABLE-- ;   ++VARIABLE ;   --VARIABLE ;
```

An increment or decrement uses the type's `operator ++` / `operator --`.  A prefix form uses `operator prefix++` / `operator prefix--` if the type declares it; otherwise, it falls back to the postfix operator.  A type which declares neither takes `n = n + 1` (or `n = n - 1`) instead (§9.7); if that isn't available either, it is a compile-time error.

**Example**

```bgl
int n = 5;
n++;         // -> 6
--n;         // -> 5
```

## 5.8 `if` / `else`

**Syntax**

```syntax
if (CONDITION) STATEMENT
if (CONDITION) { … } else { … }
if (CONDITION) { … } else if (CONDITION) { … } else { … }
```

The body can be a single statement or a block, and `else` is optional.

`else if (CONDITION) { … }` is equivalent to `else { if (CONDITION) { … } }`.

**Example**

```bgl
if (score < 0)       print("Invalid score.");
else if (score < 50) print("Keep going.");
else                 print("Well done!");
```

## 5.9 `for`

**Syntax**

```syntax
for (INITIALIZER; CONDITION; INCREMENT) STATEMENT
```

All three parts are required.  The initializer can declare a new variable or assign to an existing one; however, it can't redeclare a variable which is still in scope (§3.10).  A later loop can declare the same name again.  A ternary can be used in the condition and increment parts (§4.9).

**Example**

```bgl
for (int i = 0; i < 3; i++) print(i);    // -> 012
```

### 5.9.1 `for-in`

**Syntax**

```syntax
for (TYPE V in SOURCE) STATEMENT
for (V in SOURCE) STATEMENT
for (TYPE V in FIRST to LAST) STATEMENT
```

In the second form, `V` is a variable declared earlier.

A `for-in` loop iterates over every element of `SOURCE`, which is one of the following:

| Source | Element type |
|---|---|
| A declared array variable | The array's element type |
| An inline list `{a, b, …}` | The type of the first element; each element is checked against `v` |
| A call expression returning an array | `var`:  any declared type for `v` is accepted |
| A range `first to last` | Integer; inclusive at both ends; the bounds are any expressions |
| An object's `children` | The class which declares `children`; the body can move the current child (§11.5.2) |

A loop variable declared in the head belongs to the loop:  after the loop, the name means what it meant before, and a later loop can declare it again with another type.  Its type must be compatible with the element type or it is a compile-time error; `var` matches any element type, and `auto` infers the element type from the source, including through `operator auto()` (§9.5.1).  Any other source expression is a compile-time error.

A loop variable of a struct owns its instance, as any local of the class does (§8.2.7):  each element is copied into it, so a write to the variable won't change the array.

**Example**

```bgl
array<int> primes = {2, 3, 5, 7};
int start = 10;
int count = 3;

for (auto p in primes) print(p);                        // -> 2357
for (object o in bgl.world.getAll()) print(o);
for (int i in start to start + count - 1) print(i);     // -> 101112
```

**See also** §21.9 (`bgl.world`).

## 5.10 `while`

**Syntax**

```syntax
while (CONDITION) STATEMENT
```

The condition is evaluated before each iteration.

**Example**

```bgl
int n = 3;
while (n > 0) { print(n); n--; }    // -> 321
```

## 5.11 `do` / `while` and `do` / `until`

**Syntax**

```syntax
do { … } while (CONDITION) ;
do { … } until (CONDITION) ;
```

The body runs at least once, and the condition is evaluated after each iteration.  `do`/`while` repeats while the condition is true; `do`/`until` repeats until it becomes true.

**Example**

```bgl
int n = 0;
do { n++; } while (n < 3);    // n -> 3
do { n--; } until (n == 0);   // n -> 0
```

## 5.12 `switch`

**Syntax**

```syntax
switch (EXPRESSION) {
    case VALUE [, VALUE …] :   STATEMENTS
    case LOW to HIGH :           STATEMENTS
    case OP VALUE :              STATEMENTS
    default :                        STATEMENTS
}
```

`OP` is one of `>`, `>=`, `<`, `<=`.

A case lists one or more values separated by commas.  Cases don't fall through, so an empty case written straight before another (`case Take: case Drop: …`) does nothing and draws a warning.

A `break` in a case body leaves the `switch`, whether it is at the top of the body or nested in a block within it; however, a `break` inside a loop within a case body belongs to that loop.

Case values are type-checked against the switch expression (§2.10.1):  integer literals match an `int`, and an enum value must be of the switch expression's enum type.  When the switch expression is a `verb`, case values are verb names (§13.2).  A case value is a constant:  a literal, an enum value, a `#define`, a `const`, an object, or a verb.  A variable (a local, a global variable, a member) is a compile-time error.

**Ranges.**  `low to high` matches the inclusive range and can be mixed with single values in one case:  `case 1, 3, 5 to 10:`.

**Comparison guards.**  `case >= 50:` tests the switch value against a comparison.  When any case in a switch is a guard, the switch expression is evaluated once and the cases are tested in order.

**Type-driven comparison.**  When the switch expression's type declares `operator switch()`, that operator performs each case comparison; with `#include <string>`, `string` declares one so that `case "north":` compares content.  Overloads for different case-value types can coexist.  A range or a guard case in such a switch is a compile-time error, and a string literal case on a type which declares no `operator switch()` is one as well.  The declaration form is in §9.5.

**Example**

```bgl
switch (score) {
    case 0:        print("Nothing yet.");
    case 1 to 49:  print("Keep going.");
    case >= 50:    print("Well done!");
    case < 0:      print("Invalid score.");
}
```

## 5.13 `break` and `continue`

**Syntax**

```syntax
break ;
continue ;
```

`break` exits the innermost enclosing `for`, `while`, or `do` loop, or the innermost enclosing `switch` when that is nearer (§5.12).  A `break` in neither is reported by the Inform 6 stage.  `continue` skips the rest of the current iteration of the innermost `for`, `while`, or `do` and re-evaluates the loop condition; a `switch` inside the loop doesn't intercept it.  `continue` outside a loop is a compile-time error.

**Example**

```bgl
for (int i = 0; i < 10; i++) {
    if (i == 3) break;            // leaves the loop
    if (i % 2 == 0) continue;     // skips the even values
    print(i);
}                                 // -> 1
```

## 5.14 `return`, `rtrue` and `rfalse`

**Syntax**

```syntax
return ;
return EXPRESSION ;
rtrue ;   rfalse ;
rtrue(EXPRESSION) ;   rfalse(EXPRESSION) ;
```

`return` exits the function, with a value if given, which is type-checked against the declared return type.  In a `void` function `return expr;` is a compile-time error unless `expr` is itself of type `void`, in which case it is equivalent to `expr; return;`.  In loose mode (`#bgl` islands and precompiler mode, §16.3.3) an expression of type `var` is also accepted there.

In a function whose return type is `bool`, `rtrue;` is equivalent to `return true;` and `rfalse;` to `return false;`.

`rtrue(expr)` and `rfalse(expr)` print `expr` as `print()` would (§21.4), and then return.

**Return-path analysis.**  A non-`void` function must return on every path; a path which can reach the end of the body without returning is a compile-time error.  A path is satisfied by an unconditional `return` or `throw` (§5.16) at the top level of the body; by an `if`/`else` whose two branches are both satisfied; by a `switch` with a `default` whose every case body is satisfied (a `switch` without `default` is not); by a `try`/`catch` whose `try` body and `catch` body are both satisfied; or by an `#i6` island which returns (§16.2).  A loop body alone doesn't satisfy it.

If a local's type declares a `deinit` emitter, it fires before the function returns (§8.5).

**Example**

```bgl
bool isOpen(object door) {
    if (door.has(open)) rtrue;
    rfalse("It is closed.");
}
```

**See also** §6.2 (return types).

## 5.15 `delete`

**Syntax**

```syntax
delete IDENTIFIER ;
```

`delete` returns a pooled-class instance to its pool, calling the class's `destroy()` method first if one is declared.  The instance leaves the object tree, and the slot is free for the next `new`, which will start it again from its members' declared values.  `delete` on a variable whose type is not a pooled class is a compile-time error.  Pooled classes are covered in §8.2.6; allocation with `new` is in §4.13.

**Example**

<!-- doctest: skip -->
```bgl
Marble m = new Marble();
if (m != null) delete m;       // destroy() runs; the slot returns to the pool
```

## 5.16 `try` / `catch` / `throw`

**Syntax**

```syntax
try { … } catch (TYPE NAME) { … }
throw EXPRESSION ;
```

If `throw` executes anywhere during the `try` block, including inside called functions at any depth, execution unwinds to the nearest enclosing `catch` and the thrown value is assigned to the catch variable.  The thrown value is one word:  an integer, an object reference, or any other word-sized value.  `try` blocks nest; a `throw` inside a `catch` re-throws to the next enclosing `catch`.  If `throw` executes with no active `try` on the call stack, the program will print an error and halt.

Before it unwinds, a `throw` releases the owning locals (§8.5) of the blocks it leaves in its own routine, as far as the `try` there which catches it, or all of the routine's when nothing in the routine catches it.  A routine which the throw passes through on its way to a `catch` in a caller doesn't release its locals.  The routine which catches it releases the owning locals of its own `try` block before the `catch` body runs.

**Example**

```bgl
void deep() { throw 7; }

// … in a routine:
try { deep(); } catch (int e) { print(e); }    // -> 7
```

---

# 6 Functions

## 6.1 Function Declarations

**Syntax**

```syntax
RETURN-TYPE NAME(TYPE PARAM, …) { BODY }
```

A function declared at global scope is a **global function**.  Functions of one name overload by parameter types (§6.4); however, the name can't be shared with any other kind of global (§3.1).

Functions can also be members of a class (§8.4) or an object (§11.9).  An `extern` function has no body, since it is defined in Inform 6 (§16.4.1); an `emitter` function has an I6 template for its body (§7.2).

**Example**

```bgl
object foyer { }

void DeathMessage() { print("You have lost"); }
bool Initialise() { location = foyer; rtrue; }
```

## 6.2 Return Types

The return type determines what a function's `return` statements look like:

| Return type | Meaning |
|---|---|
| `void` | No value.  `return;` is allowed; `return expr;` is an error unless `expr` is `void` (§5.14). |
| Any other type | Every path must end in `return expr;` with `expr` compatible with the type (§5.14). |
| `array<T>` | Returns a typed array.  A returned local array passes to the caller (§12.6). |

`rtrue`, `rfalse`, and the return-path rules are in §5.14.

**Example**

```bgl
array<int> primes = {2, 3, 5};

void greet()            { print("Hello."); }        // void: no value
int  twice(int n)       { return n * 2; }           // every path returns an int
array<int> table()      { return primes; }          // a typed array
int  sign(int n)        { if (n < 0) return -1; }   // compile-time error: no return when n >= 0
```

## 6.3 Parameters

**Syntax**

```syntax
( [ literal ] TYPE NAME [ = DEFAULT ], …)
( literal TYPE | TYPE … NAME, … )
()
```

Parameters are local to the body.  A parameter with a default value is optional, and required parameters come before optional ones.  A call must supply between the required and the total number of arguments, or it is a compile-time error.  A default is one value, worked out at each call which leaves its argument out; one which needs statements to run first (a `?.` chain, `??`) is a compile-time error.  A parameter's name can be omitted (`void f(int) { … }`):  the argument is still passed, but the body can't read it.

**Named arguments.**  An argument can be passed as `name: value`, in any order and mixed with positional arguments:  the positional ones fill the first unfilled parameters in order, and each named one fills its target.  It is an error to name a parameter the function doesn't have, to supply one both positionally and by name, or to omit a required one.

**Passing.**  An argument is passed as its type's kind dictates (§2.10.3):

- a primitive value is copied;
- a reference-class parameter refers to the argument's instance;
- a struct parameter owns an instance of its own, into which the argument is copied through the class's copy operator; the class's `init` and `deinit`, if it has them, run at entry and exit (§8.5);
- a parameter of a class which manages its own storage (`stringObj`) is given its own copy the same way (§8.5);
- a `ref` parameter of a struct or storage-managing class is bound to the argument itself (§3.7).

> ***[Z-machine]:** A routine call passes at most five arguments natively; parameters beyond the fifth travel through compiler-emitted globals, which appear in the header when any routine declares more than five.*

**Literal parameters.**  A parameter whose type is qualified `literal` accepts only a value known at compile time.  For a data type, that is a literal, constant arithmetic, a `#define` value, an enum member, or a `const` initialized from one of these; a variable, a member read, or a call is a compile-time error.  For a function type, it is a named function, or a lambda whose captured variables (§4.14) are all `const` locals with such a value.

In a union, `literal` qualifies the one member it precedes.  An argument is checked when it fits a `literal` member and no other:  in `literal func<void> | int`, a function must be literal, although an `int` can be a variable.  A `literal` parameter is itself literal inside the function.  `literal` on a variable, a member, or an array's element type is covered in §3.13.

**Example**

```bgl
void spawn(string name, int x, int y, bool hostile = false) { … }
spawn(x: 10, y: 20, name: "goblin", hostile: true);   // spawn("goblin", 10, 20, true)

void onClick(literal func<void> action) { … }        // keeps `action` to call later
onClick(=> print("Click."));                          // accepted: captures nothing
string dir = "north";
onClick(=> print(dir));                               // error: the lambda captures 'dir'
```

> ***Note:** A capturing lambda keeps its captured values in one slot per lambda, not one per call, so a kept lambda will read whatever was captured last.  `literal` turns that runtime surprise into a compile-time error, at the call that would cause it.*

## 6.4 Overload Resolution

Functions and methods with the same name and different parameter-type signatures can coexist:  functions at global scope, emitter or not, and emitter and non-emitter methods on classes and objects, including `operator()`, `operator[]`, and `operator[]=`.

Two declarations of the same name must differ in their **parameters**.  The return type is not part of the signature.  Declaring the same name twice with the same parameter types is a compile-time error.

An overload's emitted name is not part of the language; from raw I6, reach it through `$i6Name(add(int,int))` (§7.3.3).

A call is resolved in two steps:

1. **Arity.**  A candidate matches only if the argument count lies between its required and total parameter counts.
2. **Types.**  Among the candidates that match by arity, an exact type match for every argument wins over a match through an implicit conversion (`operator()`, §9.4), which in turn wins over a match through `var`.  An overload which takes a union is chosen only if no more specific overload matches.  A parameter of a value type (a struct, or a primitive class such as `float`) also takes an argument that the type's `operator =` accepts, converted by it.  A reference class's `operator =` converts no argument.  A declared object is an exact match for its class, just as a variable of that class is.

Two candidates which fit a call equally well, and differ only in parameters left to their defaults, make the call a compile-time error.

Type compatibility itself is in §2.10.1, and an overloaded name used as a value is covered in §2.9.

## 6.5 `replace` and `replaced()`

**Syntax**

```syntax
replace RETURN-TYPE NAME(PARAMS) { BODY }
replaced(ARGS)
```

`replace` replaces an already-declared global function.  If no matching function exists, the compiler will issue a warning and treat the declaration as a new function.

**Emitter functions.**  The body is swapped.  Matching is by name, return type, and the full parameter-type signature.  `replaced()` is not available in an emitter.

**Non-emitter functions.**  A replacement names the overload it replaces by its own parameter types; with a name that has only one definition, the parameters don't need to match it.  Replacements chain:  inside each replacement body, `replaced(args)` calls the immediately preceding definition, the one it directly replaced.  The arguments are type-checked against that predecessor's signature, which can differ from the replacement's own.  A predecessor that no replacement calls is not part of the program.

Replacing an `extern` routine defined by an I6 library works the same way in the source; the I6-side rules are in §16.6.

**Example**

```bgl
int step(int n) { return n; }
replace int step(int n) { return replaced(n) + 100; }   // calls the original
replace int step(int n) { return replaced(n) + 200; }   // calls the first replacement
// step(5) -> 305
```

**See also** §8.7.2 (`replace` for class and object members).

## 6.6 `self`

**Syntax**

```syntax
self
self.MEMBER
```

Inside a class or object method, `self` is the receiver:  the instance the method was called on.  A bare member name in a method body resolves as `self.member` (§3.8.2).  `self` is not valid outside a method body.

Beguile's `self` differs from Inform 6's `self`, which is the object that owns the running routine.  In an emitter body, `$self` is the receiver (§7.3).

**Example**

```bgl
class Counter {
    int count = 0;
    void increment() {
        count = count + 1;              // same as the next line
        self.count = self.count + 1;
    }
}
```

## 6.7 `Main`

**Syntax**

```syntax
void Main() { … }
```

`Main` is the program's entry point.  The compiler doesn't check for it:  a program built without an IF library binding defines `Main` itself, and a missing `Main` is reported when Inform 6 runs.

**Example**

```bgl
void Main() {
    print("Hello, world!^");
}
```

**See also** §23.3.1 (entry point under a binding).

### 6.7.1 `HandleGlkEvent`

`HandleGlkEvent(event, context, buffer)` is an `extern default` entry point on Glulx, with the Inform 6 library's meaning:  `event` is the four-word Glk event, `context` is `0` while a line is being read and `1` while a key is awaited, and the return value is `0` to carry on, `2` to end the input, or `-1` to keep waiting.  The runtime wraps whatever `HandleGlkEvent` the program defines.  On a hyperlink event it renews the window's hyperlink request and calls a function target itself (§21.11); every other event goes to the program's function.  A program will behave the same with or without a library.

A program declares it as `int HandleGlkEvent(var event, var context, var buffer)`.  You read the event's words with `bgl.asm.aload(event, n)` (§21.13.1):  word 0 is its type, an `eGlulxEventType` (`none`, `timer`, `charInput`, `lineInput`, `mouseInput`, `arrange`, `redraw`, `soundNotify`, `hyperlink`, `volumeNotify`), and for a hyperlink, word 2 is the link's value.

<!-- doctest: compile -->
```bgl
#beguilerSettings { target = Glulx; }
enum eLinks { wait = 2 }
int HandleGlkEvent(var event, var context, var buffer){
    if (bgl.asm.aload(event, 0) == (int)eGlulxEventType.hyperlink
        && bgl.asm.aload(event, 2) == (int)eLinks.wait) print("[waiting]");
    return 0;
}
void main(){ }
```

Note that a program's own `HandleGlkEvent` which prints at the prompt must cancel line input itself, as in Inform 6 (§21.11).

## 6.8 Distributors

**Syntax**

```syntax
distributor NAME ;
```

A **distributor** is a function which can be called with several arguments, each of which becomes a call of its own, in the order written.  The declaration names a function which already exists, and declares no parameters and no return type:

```bgl
void say(string line) { print(line); print("^"); }
distributor say;

// … in a routine:
say("One.", "Two.", "Three.");     // say("One."); say("Two."); say("Three.");
```

Each argument is resolved against the function's overloads separately (§6.4).  An argument is evaluated when its call is reached:  the first call has returned before the second argument is read.

A **distributed call** is a statement and has no value, although it can be the whole body of a lambda (`() => print("a", n)`).  Using one where a value is wanted is a compile-time error, as is naming an argument (§6.3).  The function itself can return a value; however, each call's result is discarded.  A call with one argument, or with none, is an ordinary call.

A call with several arguments is the call as written when an overload accepts those arguments, and a distributed call otherwise.  The same holds for parameters with defaults (§6.3).

A distributor's overloads are restricted:  no overload's second parameter can take a value which any overload's first parameter takes, or the declaration is a compile-time error:

```bgl
void say(string line, string end = "^") { print(line, end); }
distributor say;      // ERROR: 'end' takes what a first argument takes

void echo(string line, int times = 1) { … }
void echo(int n) { … }
distributor echo;     // ERROR: 'times' takes an int, and echo(int) takes one first
```

A second parameter of type `var` takes every value, so a function with one can't be a distributor.  The check covers every overload of the name, including one declared after the `distributor` line.

> ***Note:** A mistyped call can be a distributed call.  `echo("twice", "2")`, where `2` was meant, is two calls, since each argument is acceptable as a first argument.*

A distributor is declared at global scope, for a global function, or in the body of a class or an object, for a method of it.  A class's distributors are inherited.  Naming something which is not a function there is a compile-time error.  The declaration can come before or after the function and the calls.

```bgl
class journal : object {
    void add(string line) { print(line, "^"); }
    void add(int n)       { print(n, "^"); }
    distributor add;
}
journal log { }

// … in a routine:
log.add("Entered the cave.", 3, "bats.");
```

The receiver of a distributed method call can't contain a call:  `nextJournal().add("one", "two")` is a compile-time error.

> ***Note:** `print` is a distributor (§21.4):  `print("You have ", count, " coins.^");`.*

**See also** §6.4 (overload resolution), §21.4 (`print`).

---

# 7 Emitters

## 7.1 What an Emitter Is

An **emitter** is a function-like declaration whose body is an Inform 6 template rather than Beguile statements.  No routine exists for it:  the body text is substituted in place at every use site, with its **substitution tokens** (`$self`, `$paramName`, …) replaced by the expressions at that site.  The use site itself is ordinary, type-checked Beguile:  an emitter has a return type and typed parameters, and its result takes part in expressions like any other value.

Emitters can be declared at global scope (§7.5), as members of any class, as members of an enum or bnum (§7.8), or grouped in an emitter namespace (§7.7).

Like functions, emitters can be **overloaded** (§6.4).  Class-member emitters with rules of their own (operator emitters, `init`/`deinit`, and the conversion and accessor operators) are covered in §8.5 and §9.

## 7.2 Emitter Functions

**Syntax**

```syntax
emitter RETURN-TYPE NAME( [ TYPE PARAM [, …] ] ) { I6-TEMPLATE }
```

`emitter` is a declaration qualifier (§3.2) and can appear in any order with the other qualifiers.  The body between the braces is raw I6 text:  it is not parsed as Beguile, and only substitution tokens (§7.3) and `##` directives (§7.4) are recognized in it.  Everything else, including single-hash directives such as `#ifdef`, passes through to the output unchanged.

- The return type is the type of the substituted expression at the use site.  An `emitter void` function can be used only as a statement.
- Parameters are referenced in the body as `$name`.  The bare name is *not* substituted, so a body can freely mention an I6 identifier which happens to share a parameter's name.
- An emitter always has a body.  The one exception is the bodiless conversion operator `emitter T operator();`, which is a pass-through (§9.4).
- `static` and `emitter` can't be combined.
- Recursion is meaningless; an emitter body can't refer to itself as a routine, and a `$i6Expr` chain which comes back around to a body already being expanded is a compile-time error (§7.3.4).

**Example**

```bgl
class Counter {
    int value = 0;
    emitter void increment(){ $self.value++ }
}

Counter c { }

// … in a routine:
c.increment();      // the body is substituted here with $self = c
```

**See also** §8.2 (which class forms require, permit, or imply `emitter` on members).

## 7.3 Substitution Tokens

Every substitution token begins with `$`.  The tokens below are recognized in every emitter body; a feature can add a token of its own, documented with that feature (`$selfsub`, §16.8).  Appendix G is the one-page index.

| Token | Replaced with |
|---|---|
| `$self` | In an operator or assignment emitter, the receiver expression with its trailing `.member` removed when the receiver is a member access (`obj` for `obj.score + 1`); otherwise, and in a method emitter, the receiver itself (`x` for `x + 1`, `container.children` for `container.children.length`); a method emitter on `parent` or `attributes` (§11.5) is the exception and receives the owner.  Not meaningful in a global emitter. |
| `$val` | The full receiver expression as written:  `obj.score` for `obj.score + 1`; otherwise identical to `$self`. |
| `$host` | The object a proxy member (§7.3.2) is accessed on, the owner of the proxy:  the receiver with its trailing `.member` removed, in every kind of emitter.  For a receiver that is not a member access, `$host` equals `$self`. |
| `$paramName` | The argument expression supplied for the parameter of that name. |
| `$target` | The assignment target as a full lvalue path (`obj.prop`, or `x`); see below. |
| `$prop` | In an `array<T>` emitter, the property name when the array is a class or object member (§12.7); `0` for a global or local array.  Using it in any other emitter is a compile-time error. |
| `$opref(op[, T])` | A callable reference to the receiver type's `operator op` (§7.3.1). |
| `$oprefReq(op[, T])` | As `$opref`, but a missing operator is a compile-time warning (§7.3.1). |
| `$i6Name(path)` | The identifier Inform 6 knows the named declaration by (§7.3.3). |
| `$i6Expr(expr)` | The I6 which a Beguile expression emits, expanded inline (§7.3.4). |

**`$target`.**  When the emitter's result is assigned (`int r = f();`), `$target` is the left-hand side; when the emitter is used as a bare statement, it is a compiler-supplied temporary.  A body which mentions `$target` performs the store itself:  the usual `lhs = body` assignment is suppressed.

### 7.3.1 `$opref` and `$oprefReq`

**Syntax**

```syntax
$opref( OP [, OPERAND-TYPE] )
$oprefReq( OP [, OPERAND-TYPE] )
```

`$opref` is a lookup, not a value.  It substitutes a reference to one of a type's operators.  The type searched is the receiver's **element type** inside an `array<T>` emitter, and the receiver's own type elsewhere.  What is substituted depends on how the operator was declared:

| The operator is | `$opref` substitutes |
|---|---|
| `static` | The name of the free routine; call it as `op(a, b)` |
| an instance member | A routine that sends the operator to its first argument; call it as `op(a, b)` |
| an emitter, or absent | `0` |

When it substitutes `0`, the runtime uses its default.

When a type publishes more than one referenceable overload of the operator, the reference is ambiguous and is a compile-time error; name the operand type to select one:

```bgl
class Money {
    int cents;
    static bool operator == (Money a, Money b) { return a.cents == b.cents; }
    static bool operator == (Money a, int b)   { return a.cents == b; }
    emitter int refMoney(){ $opref(==, Money) }     // $opref(==) alone is ambiguous
}
```

`$oprefReq` resolves exactly as `$opref` does and substitutes `0` in the same cases; however, it also reports a **compile-time warning** when the lookup is empty.  No warning is raised for the built-in word types (`int`, `char`, `object`, …), nor for a call which supplies its own comparator.

**Example**

<!-- doctest: skip -->
```bgl
emitter int  indexOf(T item) { _bglArray.indexOf($self, $prop, $item, $opref(==)) }
emitter void sort()          { _bglArray.sortDefault($self, $prop, $oprefReq(<=>)) }
```

**See also** §4.15 (`Type::operator op`, the same lookup in ordinary code), §9.6.

### 7.3.2 Choosing `$self`, `$val` and `$target`

Which of `$self`, `$val` and `$target` to use depends on the type:

- A **value type** (`int`, `bool`, `string`, …) uses `$val`:  the body acts on the value the property holds, so `obj.score + 1` must substitute `obj.score`, not `obj`.
- A **proxy member** (a storageless type such as `parentProp` whose operators act on the owning object) uses `$self`:  `o.parent == bar` must act on `o`, not on `o.parent`.
- An `operator =` that performs a literal store uses `$target`, so `obj.prop = v` stores through the dotted lvalue.  A proxy `operator =` that redirects to a non-store I6 statement uses `$self`.

**Example**

<!-- doctest: skip -->
```bgl
primitive class int {
    emitter int  operator +  (int v){ $val + $v }
    emitter int  operator =  (int v){ $target = $v; }
}

extern class parentProp {
    emitter parentProp operator =  (object v){ move $self to $v }
    emitter parentProp operator == (object v){ parent($self) == $v }
}
```

**See also** §21.5.7 (`parentProp` and `childrenProp`).

### 7.3.3 `$i6Name`

**Syntax**

```syntax
$i6Name( PATH )
$i6Name( PATH( TYPE [, …] ) )
```

`$i6Name` substitutes the identifier Inform 6 knows a declaration by, which can differ from its Beguile name (an `asI6` clause, §3.11, or a mangled `static` method).

`PATH` is a Beguile path, resolved as an ordinary name would be, including through namespace aliases (`bgl.asm.readChar`).  What is substituted depends on what the path names:

| The path names | `$i6Name` substitutes |
|---|---|
| A global function | Its routine name, or its `asI6` name |
| A `static` method | The free routine's name, `_bgl_CLASS_METHOD`, with the overload discriminator when the class declares more than one |
| An instance method | Its property name; call it as `receiver.(NAME)(…)` |
| A data member | Its property name, or its `asI6` name |
| An object, class or enum | Its emitted name, or its `asI6` name |
| An enum value | The value's bare word |

When the path names an overloaded method, the parenthesized type list selects one.  Omitting it where more than one candidate exists is a compile-time error naming the candidates.

Two cases are compile-time errors rather than a substituted `0`:

- **An emitter.**  Use `$i6Expr` to expand it inline instead (§7.3.4).
- **An unresolved path.**

A body is expanded only where it is used, so a `$i6Name` in an emitter that is never called is never resolved and never reports.

**Example**

```bgl
extern attribute light asBgl lit;
object myHook asI6 hook;

emitter bool isLit(object o)      { ($o has $i6Name(lit)) }          // -> ($o has light)
emitter int  readKey()            { $i6Name(bgl.asm.readChar)(1) }   // -> _bgl_bglOpCodes_readChar(1)
emitter int  scale(int a, int b)  { $i6Name(Money.scale(int, int))($a, $b) }
```

> ***Note:** A global function with no `asI6` clause is emitted under its own name, so a body can simply write that name.  Use `$i6Name` where the emitted identifier is **not** the Beguile one:  anything carrying an `asI6` clause, a member or method (which are emitted as property names), and above all a `static` method, whose mangled name changes as soon as a second overload is declared.*

**See also** §3.11 (`asI6`), §7.3.1 (`$opref`, the same lookup for operators), §4.15, §16.2 (the same token in an `#i6` island).

### 7.3.4 `$i6Expr`

**Syntax**

```syntax
$i6Expr( EXPRESSION )
```

`$i6Expr` substitutes the I6 which a Beguile expression emits.  This is how one emitter reaches another, since an emitter has no routine to call by name.  A body which names a global emitter as plain text is a compile-time error.

The payload is ordinary Beguile, parsed and type-checked at the use site, and a callee's own `##if` gating (§7.4) comes with it.

**Binding.**  The body's tokens stand for **typed values** inside the payload:  `$v` denotes a value of `v`'s declared type whose emitted text is the argument at that use site, and `$self`/`$val`/`$host` do likewise at the receiver's type.  A parameter declared `var` takes the type of the argument at that use site instead.

The payload can therefore mention only this emitter's own parameters and receiver; any other `$name` is a compile-time error.  It can't mention a bare I6 identifier either, only a Beguile symbol.

**Extent.**  The payload is exactly one expression:  either one which produces a value, or a single call which returns nothing.  A `;`-separated list is not accepted; multi-statement logic belongs in a routine, which an emitter body can reach by name (§7.3.3).

**Termination.**  A `$i6Expr` chain which reaches a body already being expanded is a compile-time error, as is a chain deeper than eight expansions.

> ***Note:** A body which names `$v` twice already duplicates the argument expression, evaluating its side effects twice.  Chaining multiplies this:  an outer body passing `$self` into an inner one that names `$val` twice yields four copies of the receiver.  Prefer a routine where the argument is expensive or has side effects.*

**Example**

```bgl
emitter int twice(int v){ ($v * 2) }
emitter int quad (int v){ $i6Expr(twice(twice($v))) }        // -> ((v * 2) * 2)

// bgl.asm.add is an emitter: no routine exists for it, so only expansion can reach it
emitter int sum(int a, int b){ $i6Expr(bgl.asm.add($a, $b)) }

// the callee owns the target gating; this caller inherits it
emitter void reseed(){ $i6Expr(bgl.util.random.seed(0)); }   // z-code -> random(-0)
                                                             // Glulx  -> @setrandom 0
class Counter : object {
    int value;
    emitter int doubled(){ $i6Expr(bgl.asm.add($self.value, $self.value)) }
}
```

**See also** §7.3.3 (`$i6Name`, for a name where no expression is legal), §3.12 (`superposed`), §7.4, §16.2 (the same token in an `#i6` island).

## 7.4 Conditional Text: `##if`, `##else`, `##endif`

**Syntax**

```syntax
##if EXPRESSION
    I6-TEXT
[ ##else
    I6-TEXT ]
##endif
```

Inside an emitter body, the double-hash directives select which body text is substituted.  `EXPRESSION` accepts the same forms as `#if` (§14.2.5), with the same definedness-versus-value rule.  The directives are evaluated when the emitter is substituted, and are not valid in ordinary Beguile source, where the conditional is `#if`.  `##ifdef` and `##ifndef` don't exist at all:  use `##if SYMBOL` here, `#if SYMBOL` there.

As with `#if`, the condition runs to the end of its line, so the text it controls starts on the next line.  Text after the condition on the same line is a compile-time error.

Single-hash directives are raw I6 and pass through to the output, where they act as I6 compile-time conditionals.  Any other `##name` (for instance, an I6 action constant such as `##Take`) also passes through unchanged, so `$self == ##$v` substitutes the I6 action name of `$v`.

One further double-hash form, `##beguilerSettings.key`, is recognized only in the raw-I6 directive bodies of §14.4.5; it is not substituted in emitter bodies.

**Example**

```bgl
emitter void newline(){
##if TARGET_GLULX
    glk_put_char(10);
##else
    new_line;
##endif
}
```

**See also** §14.2.4 (pre-defined symbols), Appendix F.

## 7.5 Global Emitters

**Syntax**

```syntax
emitter RETURN-TYPE NAME( [ PARAMS ] ) { I6-TEMPLATE }
```

The declaration is written at file scope.

A global emitter is used like a global function.  `print()` is a global emitter (§21.4).

**Example**

```bgl
emitter void print(stringLiteral str){ print (string)str; }
emitter void print(string str)       { print (string)str; }
emitter void print(var val)          { print val; }
```

## 7.6 Emitter Values

**Syntax**

```syntax
emitter TYPE NAME { I6-TEMPLATE }
```

An emitter without a parameter list is an **emitter value**:  a typed inline expansion used by its bare name, without `()`.  A typed value (`emitter int`, `emitter string`, …) can appear wherever an expression of that type is accepted; an `emitter void` value can be used only as a statement, terminated by `;`.  Emitter values can be declared at global scope, in class bodies, and in object bodies.  On a member, `$self` substitutes as it does for an emitter function.

An emitter value and an emitter function are distinct declarations with distinct uses:

| Declaration | Use |
|---|---|
| `emitter int foo() { … }` | `foo()` |
| `emitter int foo { … }` | `foo` |

Using a value with parentheses is a compile-time error.  A function is used only with parentheses.  A type can therefore expose a value and a zero-argument function of the same name as different members.

**Example**

```bgl
emitter int  wordSize   { WORDSIZE }
emitter void setBold    { style bold }

// … in a routine:
int ws = wordSize;          // -> WORDSIZE
setBold;                    // statement form
```

## 7.7 Emitter Namespaces

**Syntax**

```syntax
emitter NAME {
    RETURN-TYPE MEMBER( [ PARAMS ] ) { I6-TEMPLATE }
    …
}
```

An emitter namespace groups emitters under one name without declaring a class.  Members are called as `name.member(…)`.

- Every member is an emitter; the `emitter` keyword on a member is optional.
- The namespace name is not a type and can't declare a variable.
- An emitter namespace can't be inherited from or extended; either is a compile-time error.

An `emitter class` (§8.2.3), by contrast, *is* a type.

`style` is a built-in emitter namespace.  It coexists with the `style` struct of `<glulxWindow>` (§22.7.7), since a call `style.member(…)` reaches the namespace while the inline-object form `style { … }` builds a value of the class.

**Example**

```bgl
emitter style {
    void italics() { style underline; }
    void roman()   { style roman; }
}

// … in a routine:
print($"{style.italics()}Italic text{style.roman()}");
```

**See also** §10 (namespaces in general:  objects and emitter classes as containers, `alias`, `#using`); §10.3 (alias members on emitter classes, for composing namespaces hierarchically).

## 7.8 Emitter Members on Enums and Bnums

**Syntax**

```syntax
enum NAME { VALUE, …, EMITTER-MEMBER … }
bnum NAME { VALUE, …, EMITTER-MEMBER … }
extend enum NAME { EMITTER-MEMBER … }
extend bnum NAME { EMITTER-MEMBER … }

EMITTER-MEMBER ::= emitter TYPE METHOD( [ PARAMS ] ) { I6-TEMPLATE }
                   | emitter TYPE PROPERTY { I6-TEMPLATE }
```

An enum or bnum can host emitter members.  They are substituted at the use site with `$self` bound to the value and `$paramName` to each argument.  They are declared in the enum body, or added later with `extend enum` (`extend bnum` for a bnum), using the same member forms as an emitter class.  A member can be used on a bare value or on an enum-typed variable, and before the declaration that adds it.

Only emitter methods and values can be attached.  `operator` overloads, `static` members, and plain members are compile-time errors in an enum body.  The methods are not reachable as a nameable type.

**Example**

```bgl
enum eDirection {
    north, south, east, west,
    emitter int bump()      { ($self + 100) }
    emitter int plus(int n) { ($self + $n)  }
}
extend enum eDirection {
    emitter int tenfold     { ($self * 10) }
}

int a = north.bump();       // -> 101
eDirection d = south;
int b = d.bump();           // -> 102
int c = east.plus(10);      // -> 13
int e = west.tenfold;       // -> 40
```

**See also** §2.7 (enumerations).

## 7.9 Emitters and Functions Compared

The following table lays the two side by side:

| | Function | Emitter |
|---|---|---|
| Body | Beguile statements, compiled to a routine | I6 template, substituted at each use site |
| Parameters in the body | Bare name (`myParam`) | `$myParam` |
| Receiver in the body | `self` | `$self` / `$val` / `$host` |
| Overloading | Supported, by parameter types (§6.4) | Supported, by parameter types |
| Recursion | Supported | Not meaningful; a `$i6Expr` chain which revisits a body is an error (§7.3.4) |
| In an `extern class` body | `static` only, with a body (§16.4.3) | Must have a body (or `;` for a pass-through conversion) |
| At global scope | Yes | Yes |

For a raw-I6 block inside a function body, see `#i6` (§16.2).

---

# 8 Classes

A class declares a type:  the members its instances hold, the methods and emitters that act on them, and the parents it inherits from.

## 8.1 Class Declaration

**Syntax**

```syntax
[ QUALIFIER … ] class NAME [ <T> ] [ : PARENT [ , PARENT … ] ] {
    MEMBER …
}
[ QUALIFIER … ] class NAME[N] [ : PARENT [ , PARENT … ] ] {
    MEMBER …
}
```

`<` and `>` in the first form, and `[` and `]` in the second, are literal:  `<T>` declares a type parameter (§8.1.1) and `[N]` a pool size (§8.2.6).

A class's members are member variables, methods, emitters, and operators (§9).  The class name must be unique among types.  Member variables follow the same `TYPE NAME [ = VALUE ]` form as global variables (§3.3).  Qualifiers (`extern`, `emitter`, `alias`, `extend`, `replace`, `superposed`, …) are the declaration qualifiers of §3.2 and can appear in any order.  The names a class and its members take in the generated Inform 6 are covered in §16.9.

**Example**

```bgl
class Point {
    int x = 0;
    int y = 0;
    void describe(){ print(x, y); }
}
```

### 8.1.1 Type Parameters

Beguile has no generic classes.  The `<T>` form belongs to the two array types, `array<T>` and `rawArray<T>` (§12.1); a type parameter on any other class, including one derived from `array`, is a compile-time error.  `array<Room>` binds `T` to `Room` in the array's method signatures.

An `extend class array` block adds emitters to every array, and `T` in their signatures binds the same way.  The extension does not repeat the `<T>`.  Writing a body against the array's layout is covered in §12.12.

**Example**

```bgl
extend class array {
    emitter T second() { $i6Expr($self[1]) }
}
array<int> scores = {4, 9, 2};
void main(){ int s = scores.second(); print(s); }   // 9
```

## 8.2 Class Forms

A class comes in one of the following forms, which differ in what an instance is and in the members each allows:

| Form | Syntax | Instances | Members permitted |
|---|---|---|---|
| Normal | `class Foo` | Objects with their own storage, shared by reference | Variables (with or without initializers), methods, emitters |
| Value | `struct Foo` | Objects each variable owns, copied on assignment | As normal class; a copy operator to be assigned |
| Extern | `extern class Foo` | Defined outside Beguile (I6) | Variable declarations, emitters, `static` methods |
| Emitter | `emitter class Foo` | None (type label only) | Emitters (`emitter` implied), `static` methods, alias members |
| Alias | `alias class Foo for Parent` | Same as `Parent` | Variable declarations (no initializer), emitters, `static` methods |
| Primitive | `primitive class Foo` | None: a variable holds the value itself | As emitter class |
| Pooled | `class Foo[N]` | `N` preallocated slots | As normal class, plus `create()`/`destroy()` |

Normal, `extern`, and `emitter` classes can inherit with `: Parent` (§8.6).  An alias class names exactly one `Parent` after `for`; this is a type-aliasing relationship, not inheritance.  Every form can be extended with `extend class` (§8.7.1), or `extend struct` for a struct (§8.2.7).  `alias` and `extern` are mutually exclusive.

### 8.2.1 Normal Classes

**Syntax**

```syntax
class NAME [ : PARENT [ , PARENT … ] ] { MEMBER … }
```

A class with no form qualifier supports the full member set:  variables with initializers, methods with statement bodies, and emitters.  Instances are objects with their own storage.

A normal class is a **reference class** (§2.10.3).  Assignment makes a slot refer to the assigned instance, and a `ref` slot is empty until bound (§3.7).  A reference class can declare a copy operator, which makes `=` copy (§8.2.8).  For a value type (i.e., one owned by every variable and parameter, and copied), declare a `struct` (§8.2.7).

A class which other classes list as a parent can write `selfType` (§2.11) in its members to mean the class each one is used on:  a parameter, return type, or field which is the subclass on a subclass.

**Example**

```bgl
class Animal : object {
    string short_name;
    const int maxAge = 20;
    void speak(){}
    emitter bool operator == (Animal v){ $val == $v }
}
class Dog : Animal {
    replace void speak(){ print("Woof!"); }
}
```

### 8.2.2 `extern class`

**Syntax**

```syntax
extern class NAME [ : PARENT ] { MEMBER … }
extern class NAME[] ;
```

In the second form `[]` is literal:  it is the marker form described below.

An `extern class` describes a type implemented outside Beguile.  The declaration is used for type checking and emitter dispatch only:

- Emitters are allowed and require the `emitter` keyword.
- A method which is not an emitter must be `static` and have a body; it emits as a free routine, since it needs no instance.  Any other non-emitter method is a compile-time error.
- Variable declarations (type and name) are allowed and drive type inference on instances.
- Variable definitions (with `=`) are accepted; however, the value is metadata only:  no code is generated for it and it can't be observed from Beguile.

The **marker form** `extern class Name[];` declares that the type is pooled externally, with a pool size Beguile does not know.  It allows `new Name(…)` and `delete` on the type (§8.2.6).  The body can be present or omitted.  `extern class Name[N]` with an explicit size is a compile-time error.

**Example**

<!-- doctest: skip -->
```bgl
extern class object {
    parentProp    parent;
    attributeList attributes;
    emitter void  give(attribute attr){ give $val $attr }
    emitter eBool has(attribute attr){ $val has $attr }
}
```

**See also** §16.4.3 (the interoperability rules for extern classes).

### 8.2.3 `emitter class`

**Syntax**

```syntax
emitter class NAME [ : PARENT [ , PARENT … ] ] { MEMBER … }
```

An `emitter class` is a type with no instance storage:  it exists to give a name to a set of emitters and operators.  Like any class it can name more than one parent.  Every method is an emitter, so the `emitter` keyword on a member is optional.  A member variable is a compile-time error, except for an **alias member** (§10.3), which has no storage of its own and can be `const` or `inline` (the root `_bglObject` declares the object name this way, §11.5.4).  An emitter class is used as a type (variables can be declared of it), which distinguishes it from an emitter namespace (§7.7), which is only called by name.

A type label over a word which is *not* a primitive value (a class which manages its own storage through `init`/`deinit`, such as `array<T>`) is declared `extern emitter class`; such labels are sometimes called *veneer classes*.  A primitive value's label is a `primitive class` (§8.2.5).

**Example**

<!-- doctest: skip -->
```bgl
emitter class celsius {
    fahrenheit operator(){ $val * 9 / 5 + 32 }
    celsius operator = (celsius v){ $target = $v; }
}
```

### 8.2.4 `alias class`

**Syntax**

```syntax
alias class NAME for PARENT { MEMBER … }
```

An `alias class` is the same type as its parent under another name; it adds typed member declarations, which let instances omit a type keyword when initializing those members.  An instance of an alias class is an instance of the root non-alias type reached by following `for` through any chain of aliases, and its body resolves members against the alias and every class along that chain.

- Emitters are allowed and require the `emitter` keyword.
- Non-`static`, non-emitter methods are a compile-time error.
- Variable declarations (type and name) are allowed; a definition with `=` is a compile-time error.
- Exactly one parent follows `for`.

**Example**

```bgl
alias class worldObject for object {
    string description;
}
worldObject foyer {
    description = "A grand hall.";      // string, from worldObject
    attributes  = {light};              // attributeList, from object
}
```

### 8.2.5 Primitive Classes (`primitive class`)

**Syntax**

```syntax
primitive class NAME [ : BASE ] { MEMBER … }
```

A `primitive class` declares a **primitive type** (§2.10.3):  a variable of the type holds the value itself, one word.  There is no instance and nothing is allocated.  Assignment and passing copy the word, and `ref` is a compile-time error.  The class adds a type and behavior (emitters, operators), not storage.  `primitive` implies `extern emitter`:  I6 already knows the representation, and every member is an emitter.  The primitive types are listed in §2.10.3.

A value of the base type is a complete instance:  given a matching `operator =`, a primitive over `int` will accept an `int` (or a value that widens to one, such as a `char`) directly; an enum member takes a cast (§2.7.3).

A primitive over another type is a *newtype*, not an alias:  `glulxImage` and `int` are different types that share a representation, and conversion between them is explicit (`operator()` to the base, `operator =(base)` from it).  An `alias class` (§8.2.4) is the *same* type under another name.

**Example**

<!-- doctest: skip -->
```bgl
primitive class int {
    emitter int operator + (int v){ $val + $v }
}

glulxImage cover = eImages.coverArt;    // a primitive over int accepts the int
int w = cover.width;                    // behavior without storage
```

### 8.2.6 Pooled Classes

**Syntax**

```syntax
class NAME[N] [ : PARENT ] { MEMBER … }
extern class NAME[] ;
```

`[` and `]` are literal:  `N` is a positive integer literal or the name of a compile-time integer constant, and the empty `[]` is the extern marker form (§8.2.2).

A normal class can reserve a fixed number of instances by adding `[N]` after its name.  Instances are then obtained and released with `new` (§4.13) and `delete` (§5.15).  There is no dynamic allocation:  `new` returns one of the `N` preallocated slots, or `null` when the pool is exhausted, and the pool never grows.  A pooled instance has no name of its own:  printed without a `short_name` (or a `print()` of its class), it will show the Inform 6 name of its slot, such as `_bglClass_Token_1`.

**Pool size.**

- `[N]`, a positive integer literal.
- `[IDENT]`, an identifier naming a compile-time integer constant (`const int IDENT = …;`).  Owned members (§8.3.4) are not allowed on an identifier-sized pool.
- `[]` is valid only on `extern class` (§8.2.2).
- `[N]`/`[IDENT]` are compile-time errors on `emitter class` and `alias class`.
- `extend class Name[…]` is a compile-time error, since the pool size belongs to the original declaration.

**Inheritance.**  Each pooled class has its own pool, as in Inform 6:  a subclass used with `new` declares its own `[N]`, and inherits the parent's members, `create()`, and `destroy()`.  A subclass without `[N]` can't be allocated with `new`, even when its parent is pooled.

**`create()` and `destroy()`.**  A pooled class can declare a `create()` method, run when a slot is allocated, and a `destroy()` method, run before a slot is returned to the pool.  Both are optional.

- `create()` returns `void` and can declare parameters; the arguments of `new Name(args)` are passed to it.  If `create()` is not declared, `new Name()` with no arguments is the only valid form.
- **[Z-machine]** `create()` can declare at most three parameters; a fourth is a compile-time error.  The limit doesn't apply on Glulx.
- `destroy()` returns `void` and takes no parameters.
- One `create` and one `destroy` per class; overloads are compile-time errors.

**Members in a pool.**  `new` resets every member of the slot to its declared value.  Each slot also has its own backing for every owned member (§8.3.4), which `new` attaches and `delete` releases.  Any `create()`/`destroy()` the class declares runs after this reset.

**Example**

```bgl
class marbleClass[10] : object {
    int weight = 0;
    void create(int w){ weight = w; }
    void destroy(){ }
}

void Main(){
    marbleClass m = new marbleClass(5);     // create(5) runs; m.weight -> 5
    if(m == null) return;                   // pool exhausted
    delete m;                               // destroy() runs, slot returns to the pool
}
```

> ***Note:** A file-scope instance of a pooled type (`Name m { }` at file scope, not obtained from `new`) is not part of the pool.  Passing such an instance to `delete`, or otherwise mixing file-scope and pooled instances of one type, is not detected at compile time, and the behavior is undefined.*

### 8.2.7 Structs (`struct`)

**Syntax**

```syntax
struct NAME { MEMBER … }
extend struct NAME { MEMBER … }
```

A `struct` declares a **value type** (§2.10.3), as in C#:  a global, local, member, or parameter of the type owns an instance of its own, whose members are zeroed (a local's at routine entry; a member's is created with its host, §8.3.4).  A struct local declared in a loop body is that one instance, zeroed again each time its declaration runs.  A routine which calls itself keeps each call's locals and parameters its own across the inner call.  A slot with an initializer starts as a copy of it.  Assignment copies into that instance, a parameter receives a copy of its argument, and a function returns a copy.  A struct is never `null`.

A struct has fields, methods, operators, and accessors as a class does, and `extend struct` (not `extend class`) adds more.

Copying goes through the struct's **copy operator**, `operator =` taking the struct.  A struct which declares none gets one which copies each field, as an assignment of that field would:  a number is copied, a struct field goes through its own copy operator, and a `stringObj` field's text and an array field's elements are copied (§12.5); however, a class field or a `ref` field is a reference, and the copy will refer to the same object.  It covers fields which `extend struct` adds.  To copy differently, a struct simply declares its own copy operator.

- A struct doesn't inherit:  it has no bases, and a class can't derive from it.
- Assigning to a field of a function's result (`getPos().x = 5;`) is a compile-time error.  Calling a method on one (`getPos().move()`) runs on the copy.
- A struct can't hold `null` (§2.5):  assigning `null` to one, or returning `null` from a routine which returns one, is a compile-time error.
- A `ref` variable, parameter, or member of a struct refers to an instance owned elsewhere instead:  it is bound with `:=`, and `=` through it copies into the referent (§3.7).  A `ref` can be bound to `null` (`r := null;`).
- Each slot of an `array<NAME>` declared with a capacity or a list owns an instance too, and a store into a slot copies; an array with no capacity of its own holds its source's instances (§12.10.1).
- A struct can't be `extern`, `emitter`, `alias`, or `primitive`.
- An instance is an Inform 6 object and its members are properties, so a member can't take the name of an additive property such as `name` unless it is declared the way that property is (a `rawArray<T>`).

**Example**

```bgl
struct Vec2 { int x; int y; }      // copied field by field
extend struct Vec2 {
    int sum() { return x + y; }
}

void nudge(Vec2 v){ v.x++; }

// … in a routine:
Vec2 a;                     // owns an instance
Vec2 b = a;                 // a copy
nudge(a);                   // the function gets a copy: a.x unchanged
ref Vec2 p := a;            // p refers to a's instance
```

> ***Note:** In C++, `struct` is a class whose members are public by default.  In Beguile, as in C#, it is a value type.*

### 8.2.8 Making a Reference Type Behave Like a Value Type

**Syntax**

```syntax
class NAME [ : PARENT … ] { … NAME operator = ( NAME PARAM ) { … } … }
```

A reference class (§8.2.1) which declares a copy operator (§9.1) copies on `=`, as a struct does:  `=` is the type's assignment everywhere (§3.7), and this type's assignment is its copy operator.  A subclass chains to its ancestor's copy operator with a cast, `(Base)self = o;`.

- A plain global, member, or local owns an instance of its own, whether or not it is given a value.  Given one, the value is copied into it:  a local's when the declaration runs, a member's from the named object's values, a global's at startup.
- `=` on a plain slot copies into its instance.  `:=` on it is a compile-time error, since it owns its instance.
- A `ref` slot refers to an instance owned elsewhere:  it binds with `:=`, which never calls the copy operator, and `=` through it copies into the instance it refers to.  A `ref` that is still empty has nothing to copy into.
- Parameters and loop variables stay references, as for any reference class:  passing doesn't copy.

> ***Note:** A plain slot is like a C++ object with an `operator=`; a `ref` is like a pointer whose `*p = x` copies and whose `p = &x` re-points (`:=`).*

**Example**

```bgl
class Card : object {
    int n;
    Card operator = (Card o){ n = o.n; return self; }
}
Card a { n = 1; }
void f(Card c){ c.n = 5; }

// … in a routine:
Card b = a;            // its own Card, a copy of a
f(a);                  // c refers to a: a.n is now 5
ref Card r := a;       // r refers to a
r = b;                 // copies b into a
r := b;                // r now refers to b
```

**See also** §3.7, §8.2.7, §9.1.

## 8.3 Members

### 8.3.1 Member Variables

**Syntax**

```syntax
TYPE NAME [ = VALUE ] ;
array<TYPE> NAME[N] ;
array<TYPE> NAME = { VALUE , … } ;
```

`<`, `>`, `[` and `]` are literal in the array forms, which are those of §12.2.

Member variables declare the per-instance state of a class.  An `array<T>` member accepts the same forms as at file scope and in object bodies (§12.2, §12.7), either a sized declaration or an initializer list, and each instance gets its own storage for it.  Class-typed members hold a reference unless they are owned (§8.3.4).

**Example**

```bgl
object lamp {}  object key {}
class Inventory : object {
    array<int>    slots[6];
    array<object> heldRefs = { lamp, key };
    array<char>   nameBuf[16];
}
```

### 8.3.2 `const` Members

**Syntax**

```syntax
const TYPE NAME [ = VALUE ] ;
```

A `const` member can be initialized in the class or in an object declaration; assigning it afterward is a compile-time error.  A value which an object declaration gives an inherited `const` member is that member's one assignment:  it stays `const`.  `const` and `static` are mutually exclusive.

**Example**

```bgl
class Config {
    const int    maxScore = 100;
    const string title    = "My Game";
}
Config gameConfig { }

// … in a routine:
gameConfig.maxScore = 200;  // compile-time error
```

### 8.3.3 `static` Members

**Syntax**

```syntax
static TYPE NAME [ = VALUE ] ;
```

A `static` member is class-level state shared by all instances, read and written as `ClassName.member`.  Inside the class's own code (its methods and inline accessors) the bare name reaches it too, as in C# and C++; outside the class, the class name is required.  An inline accessor's hidden class can't be named, so its own statics are reached only by the bare name.  Note that Beguile's `static` is unrelated to Inform 6's `static` (an immovable object).

A class name reaches **only** what belongs to the class:  its `static` members, its `static` methods, its emitter values, and its alias members.  Naming a per-instance member through the type (`Counter.someField`, or through a type alias, §10.1) is a compile-time error.  A subclass name reaches its base classes' statics too:  `Sword.Made` names `Item.Made`, one variable, the declaring class's; a subclass's own static of the same name shadows it.

A `static` `func<>` member is called directly (`Policy.step(10)`).  Which methods are function values is covered in §2.9.

**Example**

```bgl
class Counter {
    static int instanceCount = 0;
    void increment(){ instanceCount = instanceCount + 1; }   // or Counter.instanceCount
}
```

**See also** §9.6 (`static` methods and operators).

### 8.3.4 Owned Members

**Syntax**

```syntax
STRUCT NAME [ = STRUCT{ … } ] ;
```

A class-typed member declared without `ref` is an **owned member** when its type is a struct (§8.2.7) with stored members, declared without an initializer or with an inline-object initializer (§11.3), or when its type is a reference class and it is declared without an initializer.  An owned member is a live instance of its own (every instance of the enclosing class has an independent backing) rather than a bare reference slot.  A member of a reference class which is given a value refers to that instance (a declared object is stored as a direct reference, and no `init()` is called); a `ref` member is empty until bound (§3.7).  A member whose type is its own class would own another of itself without end, so it must be `ref`.  A class-typed member of an `extern` class or object is declared `ref` (`ref object n_to;`).

An inline-object initializer gives the owned member its starting value (`Coins price = Coins{ cents = 3; };`); every instance will start with that value, including a pooled instance made with `new`.

**Example**

```bgl
struct Box { int _val = 0; void set(int v){ _val = v; } }
class thing : object { Box b; }         // b is owned: each thing has its own Box
thing t1 {}
thing t2 {}                              // t1.b and t2.b are distinct instances
```

### 8.3.5 `inline` Members

**Syntax**

```syntax
inline TYPE NAME [ = VALUE ] ;
inline EXISTING-MEMBER ;
```

An `inline` member is a positional slot:  when an instance is written with positional values, in a declaration body (`Pet rex { "rex", 3; }`, §11.2) or an inline object (`Pet{ "rex", 3 }`, §11.3), the values fill the class's positional members in order.  A member which is not positional can be set only by name.  `inline` can be declared on any class, and an `array<T>` member can be `inline`.

The second form, with no type, **re-lists** an existing member (inherited, or the class's own) as positional at that point in this class's order.  The member itself is unchanged; it keeps its type, its `const`, and where it is declared.  Naming a member which the class doesn't have, or one already positional in this class, is a compile-time error.

A class's positional order is its own list (its `inline` members and the members it re-lists, in declaration order), followed by each base's order, **subclass first**.  A member appears once, where it is first listed, so a subclass which re-lists an inherited member decides its position.  Slots which an `extern` class lists come after the program's own, as the root's name does, unless a class of the program re-lists them itself.  The positional and named forms are covered in §11.3.1.  A positional value is type-checked against the member it fills, as a named one is.

**With `extend class`** (§8.7.1), both forms append to the extended class's own list:  `extend class A { inline int a2; }` adds `a2` after A's existing positional members, and `extend class A { inline n; }` makes the existing member `n` positional there.  The order is a property of the program, not of source position:  every instance of the class, or of a class derived from it, uses the extended order, including instances declared before the `extend`.

**Example**

```bgl
class point : object { inline int x; inline int y; string label; }
point origin = point{ 0, 0 };            // x, y positional; label by name only

// `_bglObject` declares `inline const string instanceName;` (§11.5.4), so every class ends with it.
object lamp { "brass lamp"; }                                    // the name is object's only slot
class Animal : object { inline int legs; }                       // legs, then the name
class Pet : object { inline instanceName; inline int legs; }     // name first: re-listed
Animal cat { 4, "the cat"; }
Pet rex { "rex", 3; }

class A : object { inline int a1; int n; }
class B : A { inline int b1; }
extend class A { inline int a2; inline n; }   // A: a1, a2, n
B item { 1, 2, 3, 4, "the item"; }             // b1, a1, a2, n, name
```

### 8.3.6 `required` Members

**Syntax**

```syntax
required TYPE NAME ;
required TYPE NAME ( PARAMETERS ) ;
```

A `required` member is one which every object of the class must get a value for.  The requirement is met by the nearest declaration which gives one:  the object itself, or any class between the object and the class which declared the member.  An object which gets no value is a compile-time error naming the object and the member.

A required routine is declared with its parameters and a `;` in place of a body; each object, or a subclass, gives the body.  A `required default` routine has a body which doesn't meet the requirement:  it is the fallback for `extern` objects, which the requirement doesn't cover.

- `required` applies to class members; in an object body it is a compile-time error.
- A `required` member which is given a value in the same declaration is a warning, since nothing needs to supply it.  A `default` body is the exception (above).
- A class which marks `required` a member an ancestor already gives a value is a warning, and has no effect.

**Example**

```bgl
class Shape : object {
    required array<int> sides;
    required int area();
}
class Square : Shape {
    array<int> sides = {4, 4, 4, 4};
    int area() { return 16; }
}
Square tile { }                                          // Square gives both
Shape wedge { sides = {3, 4, 5}; int area() { return 6; } }
```

> ***Note:** C#'s `required` makes the code which creates an object set the member; a Beguile object declaration is that creation.  There are no abstract classes:  a class which leaves a member required can still have objects, and each one meets the requirement.*

### 8.3.7 `typesealed` Members

**Syntax**

```syntax
typesealed TYPE NAME ;
```

A member declared `typesealed` in a base class has its type locked.  A subclass or object instance can re-initialize the member; however, if it re-declares it with a different type keyword, the sealed type is kept, the written type is ignored, and a warning is reported.  The initializer is still validated against the written type.

**Example**

```bgl
class place : object { }
object southOfRockWall { }

place cliffEdge {
    parent = southOfRockWall;               // idiomatic: no type keyword
    // object parent = southOfRockWall;     // accepted, with a warning; stays parentProp
}
```

## 8.4 Methods

**Syntax**

```syntax
[ QUALIFIER … ] TYPE NAME ( [ PARAMETER , … ] ) { STATEMENT … }
```

A method is a function declared in a class body.  Within it, a bare member name resolves to `self.name`; the explicit `self.name` form is equivalent.  Methods can be overloaded by parameter-type signature; however, two methods with the same name and the same signature are a compile-time error (§11.9.2).  A method can be declared `static` (§9.6), `default`, `replace` (§8.7.2, §8.7.3), or `static superposed` (§3.12; `superposed` on a non-`static` method has no effect and is a warning).

**Example**

```bgl
class Counter {
    int count = 0;
    void incrementBy(int amount){ count = count + amount; }    // count -> self.count
    bool isAbove(int threshold){ return count > threshold; }
}
```

## 8.5 Lifecycle: `init` and `deinit`

**Syntax**

```syntax
emitter void init() { I6-TEMPLATE }
emitter void init( OWN-TYPE SOURCE ) { I6-TEMPLATE }
emitter void deinit() { I6-TEMPLATE }
static TYPE deinit( TYPE VALUE ) { STATEMENT … }
```

A class of any form can declare `init` and `deinit`, which run automatically for a variable of that type.  A class which manages its own storage (such as `stringObj`, which holds a pool buffer) uses them to acquire and release it.

- **Local.**  `init` fires immediately after the variable is declared, before any initializer assignment.  `deinit` fires at the end of the block the variable was declared in, before any `return` that leaves that block, and before a `break` or `continue` that leaves it (for that block and every block between it and the loop).  What a `throw` releases is covered in §5.16.  Releases run in reverse declaration order.  A reference class has no `init` or `deinit` to fire (its locals are in §2.10.3); a pooled instance is created and released with `new` and `delete` (§8.2.6).
- **Parameter.**  A parameter which owns its instance (a struct, §8.2.7, or an emitter class with both `init` and `deinit`) runs them as a local does:  `init` fires at entry, before the argument is copied in through the class's `operator =` taking its own type, and `deinit` fires at the routine's end and before every `return`.  An emitter class with no copy operator can be passed only by `ref` (a compile-time error otherwise).  When the class also declares the **copy form**, `init` with one parameter of its own type, a parameter is set up by that one call with the argument instead of `init` followed by `operator =`.  The copy form sees the argument before the parameter's instance is acquired.  A reference-class parameter shares its argument, so neither fires.
- **Global.**  `init` fires at startup, in `bglInit`, before the declared value is applied.  A global is never released.
- **`ref`.**  A `ref` local or parameter owns nothing, so neither fires:  it shares the referent.
- **Returned value.**  A local returned from a routine has already been released when the caller receives it, so the caller must copy it at once (the ephemeral pattern, §22.3; held temporaries, §8.5.1).
- In these instance forms both must be emitters, and `deinit` declares no parameters; `init` declares none, or one of its own type (the copy form, used only for parameters).  Any other form is a compile-time error, and `init` can't be `static`.  Neither can be called as a method:  `init` is substituted at the declaration site, and `deinit` at scope exit and `return`.

A `static deinit` with exactly one parameter is the **value form**:  it releases a value a container holds without a receiver, and is what `array<T>` calls for dropped elements (§12.10).

**Example**

```bgl
emitter class stringObj {
    emitter void init()   { $self = _bglStr.new(); }
    emitter void deinit() { $self.free(); }
    emitter stringObj operator = (stringObj v){ $self.setFromStr($v); }
    …
}

void doSomething(){
    stringObj s;        // init fires
    s = "hello";
    return;             // deinit fires first
}                       // deinit also fires on fall-through

void shout(stringObj s){ s += "!"; }     // s is a copy: init, then the argument copied in
void shoutHere(ref stringObj s){ s += "!"; }   // shares the caller's buffer
```

**See also** §12.10 (the value form of `deinit` as `array<T>` uses it).

### 8.5.1 Held Temporaries

A class can declare three emitters which keep a returned value, already released (§8.5), safe inside a call:  `int temporaryMark()`, `T holdTemporary(T v)` and `var releaseTemporaries(var result, int marked)`.  When a call or operator is given a value of that type which is a call's result, beside another operand that is a call's result too, each such value is passed through `holdTemporary`, and the call through `releaseTemporaries(CALL, temporaryMark())`; operands which are names or literals are left alone.  `temporaryMark` opens a mark, and the matching `releaseTemporaries` releases what was held since then and closes it; the marks nest with the calls, and the type keeps them, so `marked` only makes the mark come first.  `holdTemporary` must leave a value which isn't released untouched.  `stringObj` declares them, so `join(s.left(3), s.right(4))` will receive both substrings intact.

## 8.6 Inheritance

**Syntax**

```syntax
class NAME : PARENT [ , PARENT … ] { MEMBER … }
```

A class inherits every member of the parents listed after the colon, and multiple inheritance is allowed.  Circular inheritance (`class A : B` where `B` derives, directly or transitively, from `A`) is a compile-time error, detected when the inheritance clause is parsed, independent of emission.  A class declared without a parent derives from a root (§21.5.8):  `_bglPrimitive` for a `primitive` class and `_bglObject` for any other.  Member lookup walks the hierarchy depth-first, left to right, and the first match wins; when two parents declare the same member name, the first-listed parent's member is used.  Inside a method, bare identifiers resolve inherited **variable** members from all bases by this search, while inherited methods resolve through method dispatch.

To dispatch to a specific ancestor's version of a member, cast the receiver:  `((Animal)myDog).speak()` (§4.11).

**Example**

```bgl
class Flyer   : object { int altitude = 0; }
class Swimmer : object { int depth = 0; }
class FlyingFish : Flyer, Swimmer {
    void status(){ print(altitude, depth); }            // from Flyer, from Swimmer
}
```

## 8.7 Extending and Replacing Members

### 8.7.1 `extend class`

**Syntax**

```syntax
extend class NAME { MEMBER … }
```

`extend class` adds members to an already-declared class.  A class declared in Beguile accepts any member, and every instance will get each new variable with its initializer, or the type's default (`0`) when it has none.

An `extend` can't give an `extern` class's instances new storage:  a variable declared in it gives the type of a property Inform 6 already provides, as one in an `extern class` declaration does (§8.2.2); it can't take a value there, since the instances aren't Beguile's to initialize.  Whether a class is `extern` is its declaration's to say:  `extern` on an `extend` is a compile-time error.

Such a variable, on an `extern` class or in the binding's or a program's `extend class object { … }`, gives no object the property:  an object holds a property only when its class, or the object itself, declares or sets it.  Writing one on an object which doesn't hold it fails at run time, not at compile time.  Declare the property in the class that writes it (`object with_key;`, `string short_name;`).

`extend class` can't change a type parameter (§8.1.1), a pool size (§8.2.6), or the class's form (`primitive`, `superposed`).

An extension never adds parents:  `extend class C : A { … }` is a compile-time error.

**Order.**  Source order matters only while a class's definition is being assembled:  its declaration comes first, then its `extend`s, applied in the order they appear.  An `extend` which precedes the declaration is a compile-time error.  Every use of the class (its instances, subclasses, method calls, and positional values) works on the class's **final** definition, wherever the use appears:  an instance declared between the class and an `extend` will have the members and the positional order which the `extend` adds.

**Example**

```bgl
class Counter { int value; }
extend class Counter {
    emitter bool isZero(){ $self.value == 0 }
}
extend class int {
    emitter string asHex(){ $self.toHexString() }
}
extend class object {
    string description;       // the library's property, typed for Beguile
}
```

**See also** §11.10 (`extend` for objects); §12.11 (`extend` for arrays).

### 8.7.2 `replace`

**Syntax**

```syntax
replace MEMBER-DECLARATION
```

Inside `extend class`, the `replace` qualifier replaces an existing member instead of adding one.  It is required when the new member would duplicate an existing one; a duplicate without `replace` is a compile-time error.  `replace` on a member which does not exist is a warning, and the member is added.

**Example**

<!-- doctest: skip -->
```bgl
extend class stringObj {
    replace emitter stringObj operator = (stringLiteral v){ $self.setFromLit($v); }
}
```

### 8.7.3 Shadowing and `default`

**Syntax**

```syntax
default METHOD-DECLARATION
```

When a derived class or an object body declares a method which already exists in a base class, a warning is reported unless the declaration carries `replace`, which states that the override is intentional.

A base-class method marked `default` is expected to be overridden:  overriding it needs no `replace` and produces no warning.  `default` is valid only in a class declaration; in an object or verb body it is a compile-time error.  An override which is not itself marked `default` ends the chain (further descendants again need `replace`).  In an `extend class`, `replace default` (in either order) gives a method a new body and keeps it `default`; a plain `replace` there ends the chain.

**Example**

```bgl
class Animal : object {
    default void speak(){ print("..."); }
    void eat(){}
}
class Dog : Animal {
    void speak(){ print("Woof!"); }     // no warning: base is default
    replace void eat(){}                // explicit override: no warning
}
class Cat : Animal {
    void eat(){}                        // warning: shadows Animal.eat
}
```

### 8.7.4 `hide`

**Syntax**

```syntax
hide MEMBER ;
hide MEMBER . operator OP [ ( TYPE , … ) ] ;
hide METHOD [ ( TYPE , … ) ] ;
```

`hide` removes an **inherited** member, or one operator of it, from a subtype's static surface, so accessing it *through that type* is a compile-time error.  It can be written in a subclass body and in `extend class`.

- `hide member;` hides the whole member, read and write.
- `hide member.operator op;` hides only that operator; `hide height.operator =;` blocks writes and leaves reads intact.
- A parenthesized operand list narrows the hide to one overload; without it, all overloads are hidden.
- Only inherited members can be hidden; a `hide` naming a member which is not inherited, or an operator its type does not have, is a warning with no effect.

Hiding changes only what resolves through the subtype; the member still exists, and a base type which doesn't hide it still reaches it.

**Example**

```bgl
struct dim {                     // a getter and a setter (§9.9.1)
    int _val = 0;
    int  operator ()        { return _val; }
    void operator = (int v) { _val = v; }
}
class baseWin : object {
    dim width;
    dim height;
    void setColor(int c){ }
}
class vertWin : baseWin {
    hide height.operator =;           // height is read-only on vertWin
    hide setColor();
}

vertWin side {}

// … in a routine:
side.height = 5;                      // compile-time error
((baseWin)side).height = 5;           // OK: the base surface still has the write
```

**See also** §9.9 (property accessors, the usual target of `hide member.operator =;`).

### 8.7.5 Matching Rules

The following rules decide whether two declarations name the same member, for both `replace` and shadowing:

- In `extend class`:  `replace` replaces a member of the same class.  In `extend` on an object (§11.10):  the same rules, applied to the object's members.
- In a derived class or object body:  `replace` suppresses the shadowing warning for a method found anywhere in the base hierarchy.
- **Methods** and **emitters** match by name and full parameter-type signature, since both can be overloaded.  A method in a derived class with new parameter types adds an overload beside the base's, with no warning.
- **Member variables** match by name alone.

**Example**

```bgl
class Animal : object {
    int  legs = 4;
    void speak(){ print("..."); }
    emitter bool fits(int size){ $self.legs <= $size }
}

extend class Animal {
    replace int  legs = 2;                          // member variable: matched by name
    replace void speak(){ print("Hello."); }        // method, same signature: replaced
    emitter bool fits(string label){ $self.legs }   // emitter, new signature: added, no replace
    replace emitter bool fits(int size){ $self.legs == $size }   // emitter, same signature: replaced
}

class Dog : Animal {
    replace void speak(){ print("Woof!"); }         // found in the base hierarchy: no warning
    void speak(int times){ print("Woof x", times); }         // new signature: an added overload
}
```

---

# 9 Operators and Accessors

Operator overloading and property accessors are the two ways a class gives its values behavior at sites which are not method calls.  An overloaded operator supplies the meaning of `a + b`, `x == y`, `buf[i]`, or a conversion for values of the class; a property accessor runs code on every plain read or write of a member.

Both are class members and follow the member rules of §8, and each comes in an emitter form (§7) and a regular-method form.

## 9.1 Overloadable Operators

**Syntax**

```syntax
[ emitter | static ] TYPE operator OP ( [ PARAMETER , … ] ) { BODY }
```

An operator can be overloaded on any class, as an **emitter** (§7.2) or as a **regular method**.  The operator symbol takes the place of the method name; the left operand is the receiver and the right operand, if any, is the parameter.  When `a op b` is compiled, `operator op` is looked up on the type of `a` with a parameter matching the type of `b`, walking the inheritance hierarchy (§8.6).  The symbols which can follow `operator` are:

| Group | Operators | Declaration shape | Result |
|---|---|---|---|
| Assignment | `=` | `T operator = (U v)` | Author's choice |
| Arithmetic | `+` `-` `*` `/` `%` | `T operator + (U v)` | Author's choice |
| Comparison | `==` `!=` `=~` `<` `>` `<=` `>=` `?=` | `eBool operator == (U v)`; `==` also `static eBool operator == (T a, U b)` | `eBool` |
| Three-way | `<=>` | `int operator <=> (U v)` or `static int operator <=> (T a, U b)` | `int` (negative / 0 / positive) |
| Logical | `&&` `\|\|` | `eBool operator && (U v)` | `eBool` |
| Bitwise, shift | `&` `\|` `^` `<<` `>>` | `T operator & (U v)` | Author's choice |
| Compound assignment | `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | `T operator += (U v)` | Author's choice |
| Increment, decrement | `++` `--` `prefix++` `prefix--` | `T operator ++ ()` | Author's choice |
| Logical not | `!` | `eBool operator ! ()` | `eBool` |
| Subscript | `[]` `[]=` | `E operator [] (int i)`, `R operator []= (int i, E v)` | Element type; author's choice |
| Conversion | `()` | `[explicit] T operator ()` | The target type |
| Query | `?` | `emitter eBool operator ? ()` | `eBool`; emitter only |
| Switch comparison | `switch` | `emitter eBool operator switch (U v)` | `eBool`; emitter only |
| Auto inference | `auto` | `T operator auto();` | §9.5.1 |

`prefix++` and `prefix--` name the prefix forms (`++n`), while `++` and `--` name the postfix forms, so a class can give each its own behavior.  Declaring `operator` with any symbol outside this set is a compile-time error.

The valid operator tokens `?.`, `??`, `=>`, the reference-binding operator `:=` (§3.7), the identity comparison `:==` (§4.6), and the global-scope qualifier `::` (§3.9) are not overloadable.

**Choosing `operator =`.**  An assignment `x = v` uses the `operator =` of `x`'s class (or one it inherits) whose parameter is the type of `v` or, failing that, the nearest ancestor of that type.  Every class inherits a root assignment from its root, `_bglObject` or `_bglPrimitive` (§21.5.8).  An operator whose parameter is `x`'s class or one of its ancestors assigns a value *of that class*, so it applies only when `v` is one.  An `operator =` taking another type is a conversion (a setter's `operator = (int)`).  One taking `var` would never be chosen, and declaring it is a compile-time error.

An `operator =` which takes the class itself (or an ancestor) is a **copy operator** (§8.2.7, §8.2.8).

**See also** §4.4 (binary operator resolution); Appendix C (the same table as a quick reference).

## 9.2 Emitter and Non-Emitter Operators

An emitter operator substitutes its body at each use site, with `$self`/`$val` bound to the left operand and `$paramName` to the right (§7.3).

A non-emitter operator is a method called at each use site, with the right operand as its argument.

**Example**

```bgl
class Counter {
    int value = 0;
    emitter Counter operator ++ (){ $self.value++ }
    emitter bool    operator == (Counter v){ $self.value == $v.value }
}

class Animal : object {
    int id;
    replace bool operator == (Animal other){ return self.id == other.id; }
}
```

## 9.3 Subscript: `operator []` and `operator []=`

**Syntax**

```syntax
ELEMENT-TYPE operator [] ( int INDEX ) { BODY }
TYPE operator []= ( int INDEX , ELEMENT-TYPE VALUE ) { BODY }
```

`[` and `]` are literal:  they are part of the operator name.

`operator []` takes the index and returns the element type.  `operator []=` takes the index and then the value, and returns a type of your choosing, commonly the assigned type.  A subscript assignment is a statement, like any assignment (§5.5), so the returned value is not read.

Either can be an emitter or an ordinary method.  Any class can declare them, and a subclass inherits them.  The result of a subscript read supports member access, resolved against the element type.

**Example**

<!-- doctest: skip -->
```bgl
extern class myBuf {
    emitter var  operator []  (int i)        { $val-->$i }
    emitter void operator []= (int i, var v) { $val-->$i = $v }
}
myBuf buf;
var x  = buf[3];
buf[3] = x + 1;

array<Room> rooms = { kitchen, hall };
string s = rooms[0].description;        // member access on the element type
```

**See also** §12.3 (subscripts on `array<T>`).

## 9.4 Conversion: `operator ()`

**Syntax**

```syntax
[ explicit ] emitter TYPE operator ( ) ;
[ explicit ] emitter TYPE operator ( ) { I6-TEMPLATE }
[ explicit ] TYPE operator ( ) { STATEMENT … }
```

A zero-parameter `operator ()` declares that a value of this class converts to `TYPE`.  The rules which decide *when* a conversion is applied are in §2.10.1 and §2.10.2.

- By default, a conversion is **implicit** and is applied automatically wherever the rules of §2.10.1 allow.  An `explicit` conversion applies only at an explicit cast `(T)expr` (§4.11).  `explicit` is valid only on `operator ()` and can't be combined with `const` or `static`.
- The emitter form substitutes its body with `$self`/`$val` bound to the source value.
- The regular-method form runs at runtime and also fires on a **bare read** of a member of this type (in an initializer, an argument, or an expression), not only at cast sites.  This is the getter half of a property accessor (§9.9.1).

**Example**

```bgl
class MyType {
    emitter int operator ();                   // implicit
    explicit emitter string operator ();       // only via (string)x
}
MyType t { }
int    n = t;              // OK
string s = (string)t;      // OK
string e = t;              // compile-time error: the conversion is explicit

class heightProxy {
    int _val = 0;
    int operator (){ return _val; }            // regular method: fires on bare read
}
```

> ***Note:** `emitter T operator ();` (semicolon, no body) is the pass-through conversion; it is equivalent to `emitter T operator () { $val }`.*

## 9.5 Special Operators

The special operators, and where each one is used, are as follows:

| Operator | Use site | Description |
|---|---|---|
| `operator ? ()` | `x?`, `x?.m`, `x ?? y` | Defines what "present" (non-null) means for the type, returning `eBool`.  A type without it can't use any of the three; doing so is a compile-time error.  Must be an emitter. |
| `operator switch (U v)` | `switch(x){ case v: }` | The comparison applied to each `case` value.  Must be an emitter.  Statement semantics:  §5.12. |
| `operator ?= (U v)` | `x ?= y` | A binary comparison at equality precedence with result `eBool`.  No built-in type defines it; a class gives it a meaning. |
| `operator <=> (U v)` | `x <=> y` | Three-way comparison, result `int`:  negative when the left operand orders first, `0` when equivalent, positive otherwise.  Declarable `static` or as an instance operator (§9.6). |
| `operator ! ()` | `!x` | Prefix logical not. |

`null` is a built-in value (§2.5); however, what counts as present is type-defined:  a type takes part in `?`, `?.`, and `??` only through its `operator ? ()`.  The expression semantics of `?`, `?.`, and `??` are in §4.10.

**Example**

<!-- doctest: skip -->
```bgl
extern class object {
    emitter eBool operator ? () { $self ~= nothing }
}
```

### 9.5.1 `operator auto()`

**Syntax**

```syntax
TYPE operator auto() ;
```

When a variable is declared with `auto` (§3.6), its type is normally the resolved type of its initializer.  A class which declares `operator auto()` overrides this:  the declared return type is the type `auto` infers for a value of that class.

- Takes no parameters and has no body; only the return type matters.
- At most one per class; a duplicate is a compile-time error.
- A type without `operator auto()` infers as itself.

The literal pseudo-types declare it so that `auto x = 5;` will infer `int` rather than `intLiteral`:

| Literal type | `operator auto()` returns |
|---|---|
| `intLiteral` | `int` |
| `charLiteral` | `char` |
| `dictionaryWordLiteral` | `dictionaryWord` |

**Example**

<!-- doctest: skip -->
```bgl
primitive class intLiteral {
    emitter int operator();     // implicit conversion to int
    int operator auto();        // auto infers int
}
```

**See also** §2.4 (literal pseudo-types), §5.9.1 (`auto` in `for-in`).

## 9.6 `static` Operators and Three-Way Comparison

**Syntax**

```syntax
static TYPE operator OP ( TYPE LEFT , TYPE RIGHT ) { STATEMENT … }
static TYPE NAME ( [ PARAMETER , … ] ) { STATEMENT … }
```

A `static` operator takes **both** operands as parameters and has no receiver.  It is a free routine rather than a member of an instance, which is why it can be declared on an `extern`, `emitter`, or `alias` class, none of which allow other non-emitter methods.  `static` can't be combined with `emitter` or `const`.

Where a type declares both an instance and a static form of one operator, the instance form is used at ordinary use sites and the static form where generic code needs an address; which form `array<T>` needs from its element type is in §12.10.

`<=>`, like `==`, is declared `static` or as an instance operator with the left operand as its receiver.

A type with `<=>` but without `<`, `>`, `<=`, or `>=` gets the missing ones from it:  `a < b` means `(a <=> b) < 0`, and likewise for the others.  An ordering operator the type declares, or inherits, takes precedence.

`==` and `!=` are never derived from `<=>`, and `!=` is not derived from `==`.  A class which declares `operator ==` without `operator !=` keeps the inherited `!=`, which compares identity.  The compiler will warn about such a class; declare both.

**Example**

```bgl
#include <array>
extern int compareText(string a, string b);     // an I6 routine: negative, 0 or positive

extend class string {
    static bool operator ==  (string a, string b) { return compareText(a, b) == 0; }
    static int  operator <=> (string a, string b) { return compareText(a, b); }
}

// … in a routine:
array<string> names = { "cherry", "apple", "banana" };
names.indexOf("apple");                             // -> 1, via string's ==
names.sort();                                       // apple, banana, cherry, via <=>

string a = "pear";
string b = "plum";
int r = a <=> b;                                    // -1, 0, or +1
names.sort((string a, string b) => a <=> b);
```

**See also** §4.15 (`Type::operator op`; only a `static` operator is referenceable from ordinary code); §3.12 (`static superposed` methods).

## 9.7 Compound Assignment and Increment Fallback

When no `operator op=` is declared for a compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`), `n op= v` is expanded to `n = n op v`.  Likewise, when no `operator ++`/`--` (or `prefix++`/`prefix--`) is declared, `n++` and `n--` become `n = n + 1` and `n = n - 1`.

**Example**

<!-- doctest: skip -->
```bgl
n += 2;         // -> n = n + 2;  when n's type declares no operator +=
```

**See also** §5.6 (the compound-assignment statement); §5.7 (increment and decrement).

## 9.8 Emitter-Required Operators

The following must be declared with `emitter` (implied inside an `emitter class`); declaring any of them as a regular method is a compile-time error:

| Declaration | Reason |
|---|---|
| `operator ? ()` | Substituted as the null test in `?.`, `??`, and postfix `?` |
| `operator switch (U v)` | Substituted as the comparison of each `case` |

The instance forms of `init()` and `deinit()` must be emitters too (§8.5).

## 9.9 Property Accessors

A **property accessor** is a member which reads and writes like a plain member but runs code on each access.  It is built from a regular-method `operator ()` (the getter, §9.4) and an `operator =` (the setter) declared on a struct (§8.2.7), which is then used as an owned member (§8.3.4) of the host class or object.  A read of the member dispatches the getter; a write dispatches the setter, never the getter.

An operator the accessor class declares applies to the member itself, ahead of the getter (§4.4).  An ordering the class gets from its `<=>` counts as declared.  An operator the class doesn't declare works on the value the getter reads.

Accessors work on single objects, on every instance of a class, and on pooled instances obtained with `new`.  An accessor is declared either as a named class which then types the member (§9.9.1) or in place with `auto { … }` (§9.9.2).

### 9.9.1 Getters and Setters

**Syntax**

```syntax
struct ACCESSOR {
    [ TYPE NAME [ = VALUE ] ; … ]
    TYPE operator ( ) { STATEMENT … }
    TYPE operator = ( TYPE VALUE ) { STATEMENT … }
}
ACCESSOR MEMBER ;
```

The first form declares the accessor class:  optional backing members, a getter, and a setter.  The second form declares the accessor as an owned member of a host class or object.

- The getter is a regular-method `operator ()` with no parameters.  It runs on every bare read of the member (§9.4), and its return type is the type the member reads as.
- The setter is an `operator =` taking one parameter.  It runs on every assignment to the member; the assigned value is its argument.
- The accessor class must be a struct (§8.2.7) with at least one stored member, and the member must be declared without an initializer or `ref`, so that it is owned (§8.3.4); otherwise the member refers to an instance owned elsewhere and no accessor dispatch occurs.  An in-place accessor (§9.9.2) is a struct implicitly.
- A struct variable of the accessor's own type which is assigned or initialized from the member (`heightProxy h = t.height;`) is a copy of the accessor, backing members included; the getter doesn't run.
- Inside either operator, `self` is the accessor instance and its backing members.

**Example**

```bgl
struct heightProxy {
    int _val = 0;
    int  operator ()        { return _val; }
    void operator = (int v) { _val = v * 2; }
}
class thing : object { heightProxy height; }
thing t {}

// … in a routine:
t.height = 5;               // setter: _val -> 10
int a = t.height;           // getter: a -> 10
```

**See also** §8.7.4 (`hide member.operator =;` makes an accessor read-only on a subtype).

### 9.9.2 Inline Accessors: `auto { … }`

**Syntax**

```syntax
auto MEMBER = { ACCESSOR-BODY } ;
```

`ACCESSOR-BODY` is the class body of §9.9.1:  backing members, a getter, and a setter.

`auto name = { … }` declares the accessor in place, inside a class or object body.  `auto`, not `var`, is required, since the type is inferred from the body.  The braces are read as a class body, rather than as a `{ v1, v2 }` initializer list, when they begin with a member declaration or an `operator`.

**Example**

```bgl
object gadget {
    auto level = {
        int _raw = 0;
        int  operator ()        { return _raw; }
        void operator = (int v) { _raw = v * 3; }
    }
}

// … in a routine:
gadget.level = 4;           // _raw -> 12
int n = gadget.level;       // n -> 12
```

> ***Note:** `auto level = { int _raw = 0; int operator (){ … } void operator = (int v){ … } }` is equivalent to declaring a hidden class with that body and writing `HiddenClass level;`.*

### 9.9.3 `outer`

**Syntax**

```syntax
outer . MEMBER
```

Inside an accessor body two receivers are available:  `self`, the accessor instance and its own backing members; and `outer`, the host object the accessor is declared on.  `outer` is resolved at compile time and can read and write the host's other members.

`outer` is available only inside an inline `auto { … }` accessor (§9.9.2) declared directly in an object body (§11.2).  It is a compile-time error in a named accessor class (§9.9.1), and in an inline accessor declared in a class body.

**Example**

```bgl
object gadget {
    int scale = 3;
    auto level = {
        int _raw = 0;
        int  operator ()        { return _raw; }
        void operator = (int v) { _raw = v * outer.scale; }
    }
}

// … in a routine:
gadget.level = 4;           // _raw -> 12
```

---

# 10 Namespaces

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

---

# 11 Objects

## 11.1 Overview

An *object* is a named, globally visible instance which exists as a concrete entity in the story file:  where a class (§8.1) is a type, an object is a single instance of one.  **Any** class can be instantiated as a named object; however, most are world-model entities (rooms, things, characters), which inherit from `object` to get the world-model members.

## 11.2 Declaring an Object

**Syntax**

```syntax
object NAME [ asI6 I6NAME ] { [ VALUE , … ; ] MEMBER … }
object NAME [ asI6 I6NAME ] : BASE [ , BASE … ] { [ VALUE , … ; ] MEMBER … }
CLASS NAME [ asI6 I6NAME ] { [ VALUE , … ; ] MEMBER … }
object NAME ;
CLASS NAME ;
```

An object is declared at global scope.  Without a body, `object NAME;` and `CLASS NAME;` (of a reference class) declare an object exactly as an empty body does; a global which refers to an object instead is declared `ref` (§3.7).

The body holds member declarations in the same form as a class body (§8.3.1):  a member is written `Type name [= value];` and members are `;`-separated.  A member with no initializer will default to `0`, `false`, or `null` according to its type.  The one exception is `static`, which is a compile-time error in an object body, since an object has no class-wide members of its own.  An inline accessor declared in an object (`auto x = { … }`, §9.9.2) is a class, though, so its own members can be `static`.

The body can open with a **positional section**:  `,`-separated values which fill the class's positional members in order (§8.3.5), ended by the first `;`, exactly as in an inline object (§11.3.1).  A member given positionally can't also be given by name.

```bgl
object hall { "hall"; }
class Pet : object { inline instanceName; inline int legs; string owner; }
Pet rex { "rex", 3; owner = "Jim"; parent = hall; }
object lamp { "brass lamp"; }        // the name, positional on every class (§11.5.4)
```

The class name as the type keyword (`ClassName Name { … }`) is the usual form; the inheritance form (`object Name : Base`) is required when the object inherits from more than one base.  An object declared with neither (`object Name { … }`) is an instance of `object`.

An object declared with no base, or with a base which derives from `object`, is an instance of `object` and carries `parent`, `children`, and `attributes` (§11.5).  `object` is **not** added implicitly; to get the world-model members for an object whose bases don't derive from `object`, give it `object` as an explicit base.

**Every declared object is its own type**, regardless of which form declares it:  a type with one instance, derived from the object's class, which holds the members its body adds.  A verb object (§13) is the exception:  its value is an action, typed by its verb class.

The optional `asI6` clause names the object differently in the emitted I6 (§3.11).

**Example**

```bgl
class worldObject : object {
    string description;
}

worldObject foyer {
    description = "A grand hall decorated in red and gold.";
    attributes = {light};
}

class Robot  { int power; }
class Animal { string short_name; void describe() { print(short_name); } }

object dog : object, Animal, Robot {   // object listed explicitly: dog is a world-model object
    string short_name = "shaggy dog";
    int power = 10;
}
```

> ***Note:** `ClassName Name { … }` is equivalent to `object Name : ClassName { … }`.*

**See also** §11.4 (members and type inference); §11.11 (`extern object`).

## 11.3 Inline Objects: `Type{ … }`

**Syntax**

```syntax
TYPE{ VALUE , … }
TYPE{ NAME = VALUE ; … }
TYPE{ VALUE , … ; NAME = VALUE ; … }
{ … }
```

The last form omits `TYPE`; it can be used only where the target type is known (§11.3.2).

An object declaration written in expression position without a name declares an *anonymous* object and evaluates to a reference to it.  It is the same declaration as the named form (`point p { x = 1; y = 2; }`) with the name left off, and it is a compile-time declaration, unrelated to `new` (§4.13), which allocates at run time.

The form is available for any class which can be declared as a named object; it has no meaning for namespace types (`emitter class`, `alias class`).  For a struct (§8.2.7) it is a value of that class with the given members:  a variable or argument receives a copy through the class's `operator =`, and an owned member takes it as its starting value (§8.3.4).

An inline object can appear anywhere an expression can, including standing alone as a statement (§11.3.6).

### 11.3.1 Positional and Named Members

A member is given *positionally* or *by name*, and the separators carry meaning:

| Form | Separator | Fills |
|---|---|---|
| Positional: `{ 3, 4 }` | `,` | The next positional member (§8.3.5):  the class's own `inline` and re-listed members, then its bases', subclass first.  Only positional members participate. |
| Named: `{ x = 1; y = 2; }` | `;` | The member named, `inline` or not.  The `=` is required. |
| Combined: `{ 5, 6; label = "p"; }` | `,` then a single `;` | Positional values first; the first `;` ends the positional section and begins the named section. |

Supplying more positional values than there are positional members is a compile-time error.  After the `;` which ends the positional section only named members can follow; a `,` before or among named members is a compile-time error, as is a named member before a positional one.  A declaration body takes the same positional section (§11.2); however, a class body is `;`-only, and `,` is never a member separator there.

**Example**

```bgl
class point : object { inline int x; inline int y; string label; }

array<point> pts = {
    point{ 3, 4 },                 // positional
    point{ x = 1; y = 2; },        // named
    point{ 5, 6; label = "p"; },   // positional, then named
};
```

### 11.3.2 Type Inference from the Target

Where the target type is already known, a bare `{ … }` takes it and the leading `Type` can be left off.  Inference applies in six positions:

- an `array<T>` element;
- an object-backed variable initializer (`Type name = { … }`);
- the element of a declarative `inject` (§12.11);
- a call argument whose parameter is an object-backed class;
- an argument to an `array<T>` method whose parameter is `T`, when `T` is an object-backed class or a struct with `inline` members (`pts.append({ 1, 2 })`);
- a member's value inside an aggregate or object body, when the member's type is a struct with `inline` members (`Pair{ { 1 }, { 2 } }`, `Panel p1 { fill = { 40, 200, 40 }; }`).

A bare `{ … }` produces an object only when the target type is an object-backed class.  If the target is an `array<…>`, or a struct or collection type with an `operator =(initializerList)`, a bare `{ … }` is a braced list.  An explicit `Type{ … }` is always available, and also distinguishes a single element from a list.

**Example**

```bgl
enum eVerdict { pass, halt }
object shirt {}
object cloak {}
object any {}                                   // matches every noun
eVerdict dropBody() { return eVerdict.halt; }
eVerdict takeBody() { return eVerdict.halt; }
eVerdict lookBody() { return eVerdict.pass; }

class rule : object { inline verb action; inline var matcher; inline func<eVerdict> body; }

array<rule> book = {
    { Drop, shirt, dropBody },     // each element inferred as rule
    { Take, cloak, takeBody },
};

rule fallback = { Look, any, lookBody };
```

> ***Note:** `Type name = { … };` is equivalent to the named object declaration `Type name { … }`.*

### 11.3.3 Inline Objects as Arguments

For a bare `{ … }` argument (positional, named (§6.3), or on a method call) the parameter type must resolve unambiguously:  the type is inferred only when every viable overload of that name expects the same aggregate-constructible class at that argument position (an object-backed class, or a struct declaring `inline` members).  If the overloads disagree, or the parameter can't take an aggregate, it is a compile-time error; write an explicit `Type{ … }` instead.

**Example**

```bgl
void place(point p) { … }
place(point{3, 4});
place({3, 4});
place(p: {3, 4});
obj.method({ x = 1; y = 2; });
```

### 11.3.4 Constant and Run-time Members

When every member value is a compile-time constant, the expression denotes one *constant instance*, so repeated evaluations will yield a reference to the *same* object.  When a member value is a run-time expression (a local or parameter, as in `foo({ width, height })`), the expression denotes a per-site instance whose run-time members are populated immediately before each evaluation.  A recursive call which re-enters the same site repopulates that site's single instance, so the outer call will see the inner call's values once it returns.

**Example**

```bgl
class dims : object { inline int width; inline int height; }
void report(dims d) { print(d.width, d.height); }

void Main() {
    report({ 3, 4 });            // constant: one constant instance
    int w = 5;
    int h = 6;
    report({ w, h });            // run-time: populated before each evaluation
}
```

### 11.3.5 Nested Aggregates

A member value can itself be a `{ … }` aggregate, whose shape is taken from the member's type:  an `array<T>` member takes a braced array literal, and an object-backed member takes a nested inline object.  An `array<T>` member can be declared `inline`, which makes it a positional slot like any other.

**Example**

```bgl
class menu : object {
    inline object linkTo;
    inline string title;
    required inline array<dictionaryWord> words;
}

object root { }
menu child = { root, "a child", {.foo, .bar, .baz} };
```

### 11.3.6 Standalone Declarations

An inline object can stand alone as a statement, with no name and no assignment.  The reference is discarded, so the object is reachable only through a link it sets itself (e.g., a positional `parent` or `linkTo` which places it in the object tree) or, without one, by an object-tree walk (§21.9).

**Example**

<!-- doctest: skip -->
```bgl
menu{ root, "text to display", {.type, .kind} };
```

### 11.3.7 Members Declared with a Body

**Syntax**

```syntax
CLASS-TYPE NAME { POSITIONAL-AND-NAMED-MEMBERS } [ ; ]
```

A member of a class type can be given its instance with a body, just as a file-scope instance is (§11.2):  `Pet dog { "rex", 3; }` in an object body means `Pet dog = Pet{ "rex", 3 };`.  A member of any other type, or a `ref` member, takes its value with `=`.

**Example**

```bgl
class Pet { inline string title; inline int legs; }

object house {
    Pet dog { "rex", 3; }
    Pet cat { title = "tom"; legs = 4; }
}
```

## 11.4 Members and Type Inference

Members of an object body are declared with a type, as in a class body; however, when the object is an instance of a class, a member already declared on that class can be set without repeating the type.  The compiler searches the object's declared class first, then its base classes, and finally the base `object` class.  A typeless member name found on no class in the hierarchy is a compile-time error.

A member can also hold a reference to another object (§8.3.4), as `sys` does below.

**Example**

```bgl
alias class worldObject for object {
    string description;
}

worldObject foyer {
    description = "A grand hall.";      // string, from worldObject
    attributes = {light};               // attributeList, from object
}

class Subsystem : object { void activate() { … } }
Subsystem combat { }

object gameState {
    Subsystem sys = combat;             // an object reference
}

gameState.sys.activate();
```

**See also** §8.3.4 (owned members); §3.7 (`ref` members); §11.8 (array members).

## 11.5 Special Members: `parent`, `children`, `attributes`, `instanceName`

Four members have compiler-level support.  `parent`, `children`, and `attributes` are declared on the base `object` class and tied to the world model.  `instanceName`, which gives the object its name, is declared on `_bglObject` (§21.5.8), so it is available on every object, world-model or not.

### 11.5.1 `parent`

**Syntax**

```syntax
object parent = CONTAINER ;
OBJECT . parent = CONTAINER ;
```

The first form appears in an object body; the second is a run-time statement.

In an object body, `parent` places the object inside another object at game start.  At run time, assigning to `obj.parent` moves the object; assigning `null` takes it out of the object tree, so it is nowhere until it is moved again.

Both library bindings (§23) make the object's name and its `parent` positional, in that order (§8.3.5).

**Example**

```bgl
object cloak {
    object parent = selfobj;     // the player carries it at game start
}
```

**See also** §8.3.7 (`typesealed` members); §21.5.7 (`parentProp`, the type of `parent`).

### 11.5.2 `children`

**Syntax**

```syntax
children = { OBJECT , … } ;
OBJECT . children += { OBJECT , … } ;
for ( object NAME in OBJECT . children ) STATEMENT
OBJECT . children . length
```

The first form appears in an object body; the others are run-time expressions and statements.

`children` is the inverse of `parent`:  the object's child collection in the object tree.

**Placement.**  In an object body, `children = { a, b, c }` places each listed object inside this one at game start.  It is equivalent to setting `parent` on each child, and the container can be declared before or after its contents.  Since an object has exactly one parent, conflicting placement is a compile-time error:  listing an object in two containers' `children`, or listing it in one container's `children` while it sets `parent` to a different object.  Declaring the same link both ways (`kitchen.children = { table }` and `table.parent = kitchen`) is accepted.

**Reading.**  `obj.children` is a collection:  you iterate it with `for … in`, and `.length` (or its synonym `.size`) returns the number of direct children.  It is the live tree, not an array, so it can't initialize or be assigned to an `array<object>`; `bgl.world.inParent(obj)` returns one (§21.9).

**Iterating while moving.**  `for … in obj.children` reads each child's next sibling before running the body, so the body can move the current child elsewhere.  A body which moves *other* children of `obj`, or adds new ones, changes what the remaining iterations see; to visit exactly the children present at the start, iterate the snapshot `bgl.world.inParent(obj)` instead.

**First child and next sibling.**  `obj.child` is the object's first child, or `null`; `obj.sibling` is the next object with the same parent, or `null`.

**Run-time placement.**  `obj.children += { … }` moves each listed object into `obj`.  `=` on `children` can be used only in an object body, and `-=` can't be used at all; to remove an object, move it by assigning its `parent`.

**Example**

```bgl
attribute seen;
object table {}
object chair {}
object kitchen {
    children = { table, chair };
}
object bowl {}
object apple {}
object pear {}

// … in a routine:
for (object o in kitchen.children) { o.give(seen); }
int n = kitchen.children.length;
bowl.children += { apple, pear };
```

### 11.5.3 `attributes`

**Syntax**

```syntax
attributes = { [ ! ] ATTRIBUTE , … } ;
attributes = [ ! ] ATTRIBUTE ;
```

`attributes` is an `attributeList` member (§21.5.1) which declares the object's initial attributes.  Each entry names an attribute; a `!` prefix clears an attribute the object would otherwise inherit from its class.  The list is additive relative to the class:  attributes the class gives are kept unless negated, and `attributes = {}` clears nothing.  In an `extend` block, `attributes =` is allowed only when the object's own declaration has no `attributes` member; otherwise it is a compile-time error, even with `replace`.

A single attribute needs no braces:  `attributes = light;` is `attributes = {light};`, the one-element form which every list-typed member accepts (§12.2).

`attributeList` accepts `=` only; `+=` and `-=` are not allowed.  To change attributes at run time, use `give(attr)` and `ungive(attr)`, and test them with `has(attr)` (§21.5.1).

**Example**

```bgl
object foyer {
    attributes = {light};
}
object pantry {
    attributes = light;                 // one element: the same as {light}
}

class post : object { attributeList attributes = {scenery}; }
post lampPost {}

extend lampPost {
    attributes = {light, !scenery};
}
```

### 11.5.4 `instanceName`

**Syntax**

```syntax
instanceName = STRING-LITERAL ;
```

The form appears only in an object body.  The name is also positional (§8.3.5):  `_bglObject` declares it `inline`, so it is the last positional member of every class (`object lamp { "brass lamp"; }`, `point p { 3, 4, "origin"; }`).  A class which adds positional members re-lists it (`inline instanceName;`) to put it first.

`instanceName` is the object's name:  what `print(obj)` prints, with or without a library.  An object without one will print as its identifier in parentheses, `(lamp)`.

It is a `const string` member, fixed when the program is compiled:

- its value must be a string literal;
- assigning it anywhere else is a compile-time error (§8.3.2);
- it can't be read as a value;
- a class body can't set it:  each object names itself.

A library binding's `short_name` property can print a different name (§23.3.11).

**Example**

```bgl
object hall { instanceName = "Great Hall"; }
object lamp { "brass lamp"; parent = hall; }           // positional

class Pet : object { inline instanceName; inline int legs; }
Pet rex { "rex", 3; }

void Main(){
    print(lamp);                   // -> brass lamp
    print($"You see {lamp}.");     // -> You see brass lamp.
}
```

## 11.6 Attribute Declarations

**Syntax**

```syntax
attribute NAME [ asI6 I6NAME ] ;
extern attribute I6NAME [ asBgl NAME ] ;
```

An `attribute` declaration introduces a named flag which can be given to objects.  An attribute must be declared before use.  The `extern` form refers to an attribute defined in I6 (typically by the IF library binding, §23.3.4).  Once declared, the name is an identifier of type `attribute`.

**Example**

```bgl
attribute myNewAttr;
extern attribute light;
```

## 11.7 Property Declarations

### 11.7.1 `property` and `extern property`

**Syntax**

```syntax
property NAME ;
extern property NAME ;
```

Every member name which appears in any class or object declaration is a property name, and `obj.provides(name)` (§21.5.2) tests at run time whether `obj` carries that property.  No declaration is needed for a member of a declared class.

A `property` declaration introduces a property name which is not a member of any Beguile class.  The plain form defines the property; the `extern` form refers to one defined in I6.  Both make the name available to `obj.provides(name)`.  A defined property is an Inform 6 *individual* property, which the Z-machine places no limit on; only an `additive` property (§11.7.2) is a common one.

A property declaration carries no type; every class or object member which contributes to the property declares its own.  A non-additive property is unconstrained, so the same property name can be a member of two unrelated classes with different types.

A `property` identifier has type `property` and is accepted wherever a `property` parameter is expected.  A free-standing declaration does not grant `obj.name` access; declare the name as a class member to read or write it.

In default mode, `obj.provides(unknownName)` on an undeclared name is a compile-time error.  In loose mode (`#bgl` islands and precompiler mode, §16.3.3) the name passes through unchecked.

**Example**

```bgl
property hidden_flag;
extern property libDefinedProp;

class Box : object { int weight; }
Box g_box { }

void Main() {
    if (g_box.provides(weight))         { … }   // class member
    if (g_box.provides(hidden_flag))    { … }   // free-standing declaration
    if (g_box.provides(libDefinedProp)) { … }   // extern declaration
}
```

### 11.7.2 `additive` Properties

**Syntax**

```syntax
additive property NAME ;
extern additive property NAME ;
```

By default, a property is overriding:  when an object and one of its ancestor classes both supply it, the object's value replaces the class's.  An `additive` property accumulates instead:  the object's contribution and all of its ancestors' are gathered into one contiguous run of words.  `additive` is meaningful only on a `property` declaration, not on a class or object member and not on a type declaration.  On an `extern property` it records that the I6 declaration is already additive.

`object` declares `name`, a `rawArray<dictionaryWord>` holding the dictionary words which the parser matches for the object, and any object can set it without a type (`name = {.brass, .key};`).  `name` is additive and always declared, so the rules below apply to it in every program; a program which repeats `extern property name;` is in error.  Every other additive property is declared by the IF library binding which defines it (§23.3.5).

**Contributions are raw arrays.**  An additive property has no length word, which is the `rawArray<T>` layout (§12.8.1), so every member which contributes to it must be a `rawArray<T>` or a routine.  The element type is fixed by the highest ancestor which declares the member, walking the class hierarchy root-first; every other contribution in that hierarchy must use the same element type.  Unrelated hierarchies can each fix their own.  A single value is a one-element list:  `rawArray<int> hook = 3;` contributes `{3}`, and where an ancestor declares the member, `hook = 3;` does the same.  A member declared `array<T>` or as a scalar (`int name`, `stringObj name`) on an additive property is a compile-time error.

**Routine contributions add up too.**  A method bound to an additive property (`before`, `after`, …) contributes its routine alongside every ancestor's.  How the routines are called, and when the calling stops, is up to the code which reads the property (the IF library, for its hooks).

**Nothing is replaced.**  Every class's and object's contribution is kept, so `replace` on a contribution is a compile-time error.  A class or object can still replace its own earlier contribution in an `extend` body (`extend lamp { replace bool after() { … } }`).

**Extent and operations.**  A contributing member's extent is the total accumulated across every layer.  `size`, `length`, and subscripting work; however, every operation which needs a length word (assigning `length`, `clear`, `append`, `insert`, `prepend`, `remove`, `removeValue`, `push`, `pop`, `dequeue`, `enqueue`, `popEnd`, `peek`, `peekEnd`, `indexOf`, `reverse`, `sort`) is a compile-time error on such a member.

**Example**

```bgl
class Room       { rawArray<dictionaryWord> name = {.box, .crate}; }   // fixes the element type
object r1 : Room { name = {.wooden}; }                                 // matches all three words
object r2 : Room { name = .shed; }                                     // one word: {.shed}
```

**See also** §12.8.3 (member `rawArray<T>`).

### 11.7.3 Computed Property Access and `property` Parameters

**Syntax**

```syntax
OBJECT . PROPERTY-VARIABLE
OBJECT . PROPERTY-VARIABLE ( [ ARGUMENT , … ] )
```

A local or parameter declared `property` (or `var`) can be dereferenced against a receiver:  `obj.p` reads the property which `p` names, and `obj.m(2)` sends the message, binding `self` to `obj`.  The property is resolved at run time.  The name after the dot is treated this way only when it is such a local or parameter *and* it is not a real member of the receiver's type; a genuine member always wins.  A file-scope `property foo;` does not qualify.

When a function or emitter parameter is typed `property`, a bare property-name argument is taken as the property identifier (an implicit `(property)` cast, §4.11) rather than as a value read.  This holds inside an object method body, where a bare member name would otherwise mean `self.name`.  Any known property name is accepted:  a member of any class or object, or a free-standing `property` or `extern property` declaration.

**Example**

<!-- doctest: skip -->
```bgl
var p = (property) val;
int v = obj.p;
int r = obj.m(2);

extern void achieved(property task);

object gameState { int taskGetBanana = 1; }

// inside any object method body:
achieved(taskGetBanana);        // passes the property identifier
```

## 11.8 Array Members

**Syntax**

```syntax
array<TYPE> NAME = { VALUE , … } ;
array<TYPE> NAME[N] ;
array<char> NAME = "TEXT" ;
rawArray<TYPE> NAME = { VALUE , … } ;
```

`<`, `>`, `[`, and `]` are literal.

A member can be an array, with the same semantics and element type checking as a global array (§12.2).  The storage rules for member arrays, including `ref` members, are given in §12.7; the `rawArray<T>` member form is covered in §12.8.3, and its use for additive properties in §11.7.2.

**Example**

```bgl
object scoreboard {
    array<int> highScores = {100, 75, 50, 25};
}

object foo {
    array<char> greeting = "hello";
    array<char> codes = {'a', 'b', 'c'};
}
```

## 11.9 Methods

**Syntax**

```syntax
TYPE NAME ( [ PARAMETER , … ] ) { STATEMENT … }
```

An object can define methods:  functions which are members of the object rather than free functions.

**Example**

```bgl
object bar {
    bool before() {
        switch (action) {
            case Go:
                print("You can't go that way.");
                rtrue;
            default:
                rfalse;
        }
    }
}
```

### 11.9.1 Dispatch on Object Receivers

When a method is called on an object, including `self.method()` from inside the object's own method body, the method is looked up in the following order:

1. the object's own members (per-instance overrides);
2. the object's class, then its base classes recursively.

There is no special case for the `object` base class, which is an ordinary class; an object which derives from it reaches its methods (§21.5) through the same walk.

**Example**

```bgl
class Animal : object {
    void speak() { print("..."); }
}
Animal cat {
    replace void speak() { print("Meow."); }   // per-instance override
}
Animal cow { }

// … in a routine:
cat.speak();            // Meow.        - the object's own member
cow.speak();            // ...          - the class's method
cat.give(light);        // from object, through the hierarchy walk
```

### 11.9.2 Overloads

A class or object body can declare several methods with the same name and different parameter signatures, dispatched by signature under the general resolution rule (§6.4).  Overloads of `operator()`, `operator[]`, and `operator[]=` follow the same rule; operator emitters can likewise be overloaded by parameter type.

**Example**

```bgl
class Logger {
    void log(int n)        { print(n); }
    void log(string s)     { print(s); }
    void log(int n, int m) { print(n, ":", m); }
}

Logger lg { }

// … in a routine:
lg.log(5);          // log(int)
lg.log("hi");       // log(string)
lg.log(3, 7);       // log(int, int)
```

## 11.10 `extend` for Objects

**Syntax**

```syntax
extend OBJECT {
    TYPE NAME = VALUE ;
    replace TYPE NAME = VALUE ;
    replace TYPE NAME ( [ PARAMETER , … ] ) { STATEMENT … }
    NAME += { VALUE , … } ;
    NAME -= { VALUE , … } ;
}
```

Any previously declared object can be extended.  Inside the body:

- A new member is declared with `=`, as in an object body.
- `replace` replaces an existing method or member value.  Adding a duplicate member without `replace` is a compile-time error; `replace` on a member which does not exist is a warning.
- A replaced member keeps its kind:  an accessor (`auto NAME = { … }`, §9.9) is replaced by an accessor, and a value by a value.  Changing the kind is a compile-time error.
- `+=` appends to, and `-=` removes from, an existing collection member (`grammarRuleList` or `array<T>`).  The member must exist and be a collection type; otherwise it is a compile-time error.  `attributeList` accepts `=` only (§11.5.3).  The grammar forms are covered in §13.5.

Extending an `extern` object (§11.11) is restricted to its `grammar` member:  `grammar += { … }`, `replace grammar = { … }`, and `grammar -= { … }` are allowed (§13.5).  Adding a member or method, or using `-=` on any other member of an extern object, is a compile-time error.

**Example**

```bgl
object myRoom {
    int score = 10;
    void describe() { print("A room."); }
}

extend myRoom {
    int turnCount = 0;
    replace int score = 20;
    replace void describe() { print("A dark room."); }
    attributes = {light, !scenery};
}
```

**See also** §8.7.1 (`extend class`); §8.7.5 (`replace` matching rules); §12.11 (`extend` for arrays).

## 11.11 `extern object`

**Syntax**

```syntax
extern object NAME ;
extern object NAME {
    TYPE METHOD ( [ PARAMETER , … ] ) ;
    emitter TYPE METHOD ( [ PARAMETER , … ] ) { I6-TEMPLATE }
    TYPE MEMBER ;
}
```

An object defined in I6 rather than Beguile is declared `extern`.  The bare form records only the name.  The body form also declares the types of the object's members, so calls and member reads against it will type-check.  The object is a referenceable file-scope name like any global object; however, no object is defined for it in the story file.

An I6 object typed by its class is declared `extern const CLASS NAME;` (§3.5).

In the body:

- Methods are bodyless signatures; the implementation is in I6.  A brace body on a non-emitter method is a compile-time error.  `emitter` members with a body are allowed.
- Members are typed and carry no initializer; an initializer is a compile-time error.
- Default parameter values are honored for arity and overload resolution at call sites, as for ordinary methods (§6.3).

**Example**

<!-- doctest: compile -->
```bgl
extern object playerCommands {
    void pushCommand(string cmd, bool isMeta = false, bool isSilent = false);
    object interrupt;
}

// … in a routine:
playerCommands.pushCommand("say hello");
```

**See also** §16.4 (declaring I6 names); §8.2.2 (`extern class`).

---

# 12 Arrays

## 12.1 Overview

`array<T>` is a typed word array with a capacity fixed at declaration and a length tracked at run time.  The element type `T` is mandatory; a bare `array` is not a type.  `T` can be any base type (`int`, `bool`, `string`, `object`, `char`, `dictionaryWord`), any user-defined class, or another array type (§12.9).  `rawArray<T>` (§12.8) is the untracked form used at the I6 boundary.

Subscripting, `size`, `length`, assignment (which copies, §12.5), and `for … in` are built in.  Assigning `length`, `clear()`, and the remaining methods (`append`, `indexOf`, `sort`, …) come from the `<array>` extension, which you enable with `#include <array>`; they are cataloged in §22.4.  A program which doesn't include it carries none of its code.

## 12.2 Declaring Arrays

**Syntax**

```syntax
array<TYPE> NAME[N] ;
array<TYPE> NAME = { VALUE , … } ;
array<TYPE> NAME = VALUE ;
array<TYPE> NAME[N] = { VALUE , … } ;
```

`<`, `>`, `[`, and `]` are literal.  The first form is a sized array:  capacity `N`, zero-initialized, length 0.  `N` is any compile-time integer:  an integer literal, a `#define`d symbol whose value is an integer (§14.2.1), a `const int` initialized with an integer literal (§3.4), or an integer `#beguilerSettings` property (§15.7).  The second is an initialized array, with capacity and length equal to the number of values.

The third is the one-element form of the second:  a single value of the element type needs no braces, so `array<int> x = 3;` is `array<int> x = {3};`.  A value which is itself an array initializes `NAME` from that array instead; the value's type decides which is meant.  A local declared from a call's or a chain's result takes that array; one declared from another array gets a copy of its own, with that array's capacity; a sized declaration (`array<int> x[N] = src;`) copies into its declared capacity (§12.5, §12.6).  These forms are for locals:  a file-scope or member array declared from another array or a run-time value is a compile-time error; copy into it inside a routine instead.  A local `array<char>` declared from another needs a capacity unless its source is a file-scope array, whose capacity it takes.

The fourth combines the first two:  capacity `N`, length the number of values, the remaining slots zeroed.  A list longer than `N` is a compile-time error.

An array always has storage:  a capacity of at least 1, or at least one element.  A global or local array declared with neither (`array<int> g;`), with an empty `{}`, or with a capacity of 0 is a compile-time error.  To share an array which lives elsewhere, declare a `ref` instead (§3.7).

Arrays can be declared at file scope, as locals inside a function body (§12.6), and as class or object members (§12.7).  A global array has no element-count limit.  Element types are checked at every subscript site and in initializers, so a cross-type assignment (e.g., a `string` into an `array<int>`) is a compile-time error.

**Example**

```bgl
array<int> scores[5];
array<int> primes = {2, 3, 5, 7, 11};
array<Room> visited[20];
```

## 12.3 Subscripts, Size and Length

**Syntax**

```syntax
ARRAY[INDEX]
ARRAY[INDEX] = VALUE
ARRAY . size
ARRAY . length
ARRAY . length = N
ARRAY . clear ( )
```

`[` and `]` are literal.

Subscripts are zero-based.  `size` is the number of elements allocated for the array, regardless of whether those slots hold meaningful values.  It is read-only.

Every subscript of an `array<T>` is checked against its `size`, for reads and writes alike.  A constant index outside `0` to `size - 1` of an array whose size is declared is a compile-time error; any other out-of-range index will print a run-time message, and the read (which yields `0`) or the write is skipped.  A subscript never consults or changes the length.  `rawArray<T>` subscripts are not checked (§12.8).

An array also carries an explicit *length*, the count of in-use entries.  Length is set at allocation (N for an initializer list, 0 for a sized array) and changes only through explicit operations:  assigning `length`, `clear()`, and the mutators of `<array>` (§22.4).  A slot write (`arr[i] = v`) does not change length; effectively, the array behaves as a buffer with a cursor.  An assigned `length` is limited to `size`, and a negative one is a run-time error; `clear()` zeroes every slot up to `size` and resets length to 0.

Every traversal in `<array>` walks the in-use range only, so a sized array filled only by slot writes reads as empty; use `+=`, `insert()`, or assign `length` to make the slots live.

On an `array<char>` (§12.4), `size` is its capacity, and `length` starts at the declared capacity (with `<buf>`, a sized declaration starts empty); assigning its `length` needs `<buf>` (§22.4).  On a `rawArray<T>` (§12.8) `size`, `length`, and `for … in` are compile-time errors, except that a raw member array reports its property length from both.  On an untracked `extern` array, `length` reads as `size`, and assigning it has no effect.  `isTracked()` (§22.4) tells a tracked array from an untracked one.

**Example**

```bgl
#include <array>

// … in a routine:
array<int> scores[5];

int x = scores[2];
scores[0] = 99;
int n = scores.size;            // -> 5
int used = scores.length;       // -> 0: a slot write does not change length
scores.length = 1;              // -> length is now 1
scores.clear();                 // every slot 0, length 0
```

> ***[Z-machine]:** `length` accepts 0..32767, and no more than `size`.*

> ***[Glulx]:** `length` accepts 0..2^31-1, and no more than `size`.*

## 12.4 Byte Arrays: `array<char>`

**Syntax**

```syntax
array<char> NAME[N] ;
array<char> NAME = { VALUE , … } ;
array<char> NAME = "TEXT" ;
```

`<`, `>`, `[`, and `]` are literal.

`array<char>` is a byte array:  its elements are stored and accessed as bytes.  All other element types are word arrays.  A byte array holds character or small-integer data; its initializer and element writes accept both character literals and integers (`{'H', 'i'}`, `{5, 10, 15}`, `bytes[i] = 99`).  An integer literal outside 0..255 is a compile-time error; a non-literal `int` which exceeds a byte will wrap to its low byte at run time.  A string initializer fills the array with the characters of the text.

Reading an element yields a `char`, which widens to `int` freely.  A `char` result is byte-wide, so accumulate into an `int`.  Printing a `char` prints a character; to print its numeric value, simply read it into an `int` first.

With `<buf>` (which `<string>` includes), a sized `array<char>` starts empty, and a slot write doesn't change its length:  after filling one by index, assign its `length` (§12.3, §22.2).  `clear()` and the other `<array>` methods don't apply to an `array<char>`, whose operations are `<buf>`'s (§22.2).  An `array<char>` can't be passed to a `rawArray<char>` parameter (§12.8).

**Example**

```bgl
array<char> bytes = {'H', 'i', 5, 10};
array<char> word  = "hello";

bytes[2] = 99;                  // an integer into a byte slot
int big = 300;
bytes[3] = big;                 // -> 44: wraps to the low byte at run time
int total = bytes[2] + bytes[3];
print(word[0]);                 // -> h
int code = word[0];
print(code);                    // -> 104
```

## 12.5 Assignment and Copy Semantics

**Syntax**

```syntax
DESTINATION = SOURCE ;
REF-DESTINATION := SOURCE ;
```

An array owns its storage, just as an object with a copy operator owns its instance (§2.10.3):  `dst = src` copies `src`'s elements and length into `dst`'s storage, up to `dst`'s capacity, and the two stay independent.  `dst.copyFrom(src)` from `<array>` does the same.  A source which nothing else holds (a chain's result or a call's returned array) is freed once it is copied.

Only a `ref array` (§3.7) re-points:  `r := a` makes `r` refer to `a`'s array, and `=` through `r` copies into the array it refers to.  Parameters and returns share:  a function's array parameter refers to the caller's array, and a returned array passes to the caller (§12.6).  `a == b` is true when they are the same array.

**Example**

```bgl
#include <array>

// … in a routine:
array<int> src = {1, 2, 3};
array<int> dst[3];

dst = src;                      // dst has its own copy
dst[0] = 99;                    // src[0] is still 1
ref array<int> same := src;     // same refers to src's array
same[0] = 5;                    // src[0] is now 5
```

## 12.6 Local Arrays, the Heap and Collection

A local array (declared inside a function body, sized or initialized) takes its storage from the **heap** when its routine is entered, one array per call, so each call (a recursive one included) has its own.  A local array declared in a loop body is that one array, emptied each time its declaration runs.  File-scope and member arrays live in permanent storage and are never freed.

A local `array<char>` is the exception:  it lives in its routine's frame (§18.10), so returning one is a compile-time error; return a file-scope `array<char>` or a `stringObj` instead.

A heap array lives as long as something refers to it, as in C# and TypeScript.  `<linq>` results (§22.5) are heap arrays too.

Arrays are freed in three ways:

- **At the end of the statement.**  A call's or a chain's result which nothing takes is freed once the statement which used it is done:  `f()[0]`, `f().length`, `a.filter(…) == b`.  A result is taken by a local (an array, `ref`, `var`, or union local), a parameter, a `for … in` (for the loop), anything which outlives the routine, or a pin.  An Inform 6 routine given a result takes nothing; pin the array if the I6 code keeps it.  A function whose every `return` hands back a file-scope array returns that array itself, so there is nothing to free or own.
- **At the routine's exit.**  A local array which no reference has left is freed as its routine returns, and so is an array a local took from a call or a chain, and the rows of a local array of arrays which its own `{ … }` initializer made and nothing else took.  Rows it gained later are left to the collector.  A returned array passes to the caller instead.  A routine containing an `#i6` block leaves its local arrays to the collector.
- **By the collector.**  The rest are freed by a mark-and-sweep collector when nothing refers to them.  It runs at the prompt, through the library binding (§23), and acts only once a quarter of the heap has been allocated since it last ran.

The collector finds references from declared types.  Its roots are the globals and properties whose type can hold an array (an array type, `var`, or a union with one of them), the elements of file-scope and member arrays of such a type, the local arrays and struct variables of routines still running, and the arrays pinned with `bgl.gc.keep` (§21.15).  It follows them through the arrays they reach.  A reference it can't see (i.e., one held only by Inform 6 code or in a local variable) doesn't keep an array; pin the array instead.  An Inform 6 global declared to Beguile with an array type (`extern array<int> held;`) is typed, so it is a root.

The heap's size is the `heapSize` setting (§15.4).  When it runs out, the collector runs at once and the allocation is tried again; running out after that is a run-time error which names the setting.  A debug build's `heap` verb reports the figures for setting it (§20.3).

`bgl.gc` (§21.15) frees one array at once (`bgl.gc.free(a)`), runs a collection (`bgl.gc.collect()`), and reports the heap's figures.  A program which never reaches a prompt, or a library with a turn loop of its own, calls `bgl.gc.collect()` at a point where no array it still needs is held only in a local.

**Example**

```bgl
array<int> build() {
    array<int> tmp[3];
    tmp[0] = 1; tmp[1] = 2; tmp[2] = 3;
    return tmp;                 // the caller gets this array
}

array<int> keep = build();      // keep takes build's array
int n = keep[0];                // -> 1
```

> ***Note:** I6 code which keeps an array it was given, in a global of its own or an `#i6` block's variable, needs one that lives on:  pin it with `bgl.gc.keep(a)` first (§21.15), and `bgl.gc.unpin(a)` once the I6 code lets go, or pass a file-scope array.  An array which Inform 6 code holds without `bgl.gc.keep` is freed at the next collection, and a later use of it will read freed memory.*

> ***Note:** An array of a struct declared with a capacity or a list owns its slots' instances (§12.10.1), and freeing the array releases them.  A function can return such an array; the instances go with it.  A view of a routine's own array of structs can't be returned (§12.10.1).*

## 12.7 Member Arrays

**Syntax**

```syntax
array<TYPE> NAME[N] ;
array<TYPE> NAME = { VALUE , … } ;
array<TYPE> NAME[N] = { VALUE , … } ;
required array<TYPE> NAME ;
ref array<TYPE> NAME ;
```

`<`, `>`, `[`, and `]` are literal.  These forms appear in a class or object body.

An `array<T>` declared as a class or object member (§8.3.1, §11.8) is its property's data, as Inform 6 has it:  the elements are in the property, so Inform 6 code reads it directly.  An emitter on a member array sees `$self` as the owner and `$prop` as the property (§12.12).  Every instance of a class has an array of its own, with the declared capacity and initializer.

A member array is its owner's data, with no address of its own, so it can't be passed as an argument or returned; pass its owner, or copy it into an array first.

A member array is declared with a capacity or at least one element.  In a class body, a member which every object must give is declared `required` with neither (§8.3.6); one which can stay empty declares room, `array<int> items[4];`.  Otherwise a member array with neither, or with an empty `{}`, is a compile-time error, except in an `extern` or `emitter` class, whose body describes a layout rather than storage.

Alternatively, a member can be declared `ref`:  it holds a reference to an array owned elsewhere, is bound with `:=` (§3.7), and owns no storage.  A `ref` member can be passed as an argument.

An element store into a member array of a class which manages its own storage (`stringObj`) reads the slot twice, so its index is a variable or a constant; a computed index is a compile-time error.

**Example**

```bgl
#include <array>
class Inventory : object {
    array<object> held[8];                  // per-instance, in the property
}

array<int> shared[8];
object w { ref array<int> log; }

// … in a routine:
w.log := shared;      // bind
w.log += 5;           // appends to shared
```

> ***[Z-machine]:** A property holds at most 32 words.  A member array larger than that (counting the length word) is kept in a table of its own, with the property holding its address; it behaves the same, although Inform 6 code will no longer find the elements in the property, and an emitter sees it as a global array (`$self` the table, `$prop` `0`, §12.12).  A `rawArray<T>` member can't exceed the limit (§12.8.3).*

## 12.8 `rawArray<T>`

### 12.8.1 Raw Views

**Syntax**

```syntax
rawArray<TYPE> NAME
```

`<` and `>` are literal.  The form appears as a parameter, an `extern` declaration, or a member.

`rawArray<T>` is a typed view over a bare I6 word array:  no length header and no tracking.  `rawArray<char>` is the byte form, a view over a bare I6 byte array (`Array X ->`, or an address inside the input buffer); like `array<char>` (§12.4) it is indexed by byte, from offset 0.  It is for interoperability:  an I6 buffer handed to Beguile (e.g., the `results` array of a `parse_error` entry point) has no count word, and receiving it as a `rawArray<T>` parameter lets you use ordinary subscript syntax on it.  An I6 byte array or buffer reached by name is declared the same way, `extern rawArray<char>`.

| | `array<T>` | `rawArray<T>` |
|---|---|---|
| Layout | an I6 `table`: the count word (the capacity), then the elements; the length and a marker word sit in the two words before it | elements only |
| `size` / `length` | available | unavailable; the length is passed explicitly |
| Length tracking | yes | none |

Since a `rawArray<T>` carries no length, `for … in` over a `rawArray<T>` parameter is a compile-time error; iteration uses an indexed loop bounded by a length supplied separately.  Elements are type-checked at every subscript and can be cast like any other value.

**Example**

<!-- doctest: skip -->
```bgl
bool ext_parsererror(eParserError etype, rawArray<var> results) {   // PutOn, Insert: library actions
    if (etype == eParserError.nothingToDo && ((verb)results[0] == PutOn || (verb)results[0] == Insert))
        rtrue("You are not holding one.");
    rfalse;
}

void process(int v) { print(v); }

void walk(rawArray<int> buf, int n) {
    for (int i in 0 to n - 1) { process(buf[i]); }
}
```

### 12.8.2 File-scope `rawArray<T>` Literals

**Syntax**

```syntax
rawArray<TYPE> NAME = { VALUE , … } ;
```

`<` and `>` are literal.

A file-scope `rawArray<T>` declared with an initializer is emitted as exactly the I6 array its values make:  the elements, with no count word, length, or marker word.  It has the same elements-only layout as a `rawArray<T>` parameter, so it can be passed wherever a `rawArray<T>` is expected, and its length is known only to the code which declared it.  An I6 API which reads an array by a leading count word takes an `array<T>`.

**Example**

```bgl
array<string>    trk = { "a", "b", "c" };   // tracked
rawArray<string> raw = { "a", "b", "c" };   // Array raw --> "a" "b" "c";
```

### 12.8.3 Member `rawArray<T>`

**Syntax**

```syntax
rawArray<TYPE> NAME = { VALUE , … } ;
```

`<` and `>` are literal.  The form appears in a class or object body.

A `rawArray<T>` member is a bare property array.  It is required for members which contribute to an `additive` property; its `size`, `length`, and allowed operations on such members are covered in §11.7.2.

**Example**

```bgl
class Room { rawArray<dictionaryWord> name = {.box, .crate}; }
```

## 12.9 Arrays of Arrays

**Syntax**

```syntax
array<array<TYPE>> NAME = { { VALUE , … } , … } ;
NAME[I]
NAME[I][J]
```

`<`, `>`, `[`, and `]` are literal.  `NAME[I]` is an `array<TYPE>`; `NAME[I][J]` is an element of `TYPE`.

The element type of an array can itself be an array type, two deep at most:  `array<array<T>>`.  A third level is a compile-time error; keep it in a class member instead.  A struct can't hold an array of arrays either; however, a struct member can be a `ref` to one kept elsewhere.

A nested initializer supplies one braced list per inner array, and the inner arrays can differ in length.  The inner arrays of an `array<array<char>>` are byte arrays (§12.4); each row is written as a braced list (`{'o','n','e'}`), since a string literal is not accepted as a row.  A nested array can be declared as a local; its inner arrays are allocated per call with it (§12.6).  A wrong element type or a wrong nesting depth in an initializer is a compile-time error.

A slot holds a reference to its inner array.  `grid[i] = row;` copies `row` into the array the slot holds, as `=` on any array does (§12.5).  Appending (`grid += row;`) stores `row` itself in the new slot, and the outer array then keeps it alive.  A member array has no address of its own, so storing one as a row (or in a `var`) stores a copy.  A whole `grid = other;` treats each row as an element:  a row `grid` already holds is copied into, and a slot past `grid`'s length takes a copy of `other`'s row, so the two stay independent.

**Example**

```bgl
array<array<int>> grid = { {1,2,3}, {4,5} };

int v = grid[0][1];                    // 2
int rows = grid.length;              // 2
int cols = grid[1].length;           // 2
grid[0][1] = 99;
int total = 0;
for (array<int> row in grid) {
    for (int i = 0; i < row.length; i++) { total += row[i]; }
}
```

## 12.10 Element Type Requirements

`array<T>` can't see the element type at run time.  It asks `T` for four operations, each supplied as an operator reference (§7.3.1), and every one is optional.  A type which publishes nothing gets plain word semantics.

| `T` publishes | Used by | Absent |
|---|---|---|
| `operator ==` (static or instance) | `indexOf`, `find`, `contains`, `removeValue`, `-=` | word comparison (identity) |
| `operator <=>` (static or instance) | `sort()` | signed word ordering, and a compile-time warning for a class (ordering by object address) |
| `static operator =` | `[i] =`, `append`, `prepend`, `insert` | raw word store |
| `static deinit(T)` | `remove`, `removeValue`, `-=`, `clear`, local scope exit | nothing is released |

For `int`, `char`, `bool`, and `object` the defaults are correct.  The default is silently wrong for a type whose value semantics differ from its word, such as a content-comparing type (`string` and `stringObj` publish `operator ==`), a type which owns storage, or `float`, which is sign-magnitude and publishes `operator <=>` to sort negatives correctly.

**Static versus instance.**  `==` and `<=>` can be either form; the instance form is sent to the left operand.  A bare-word element type (`string`, `float`) has no object to receive a message and must publish the static form; only an object-backed element type can use the instance form.  `operator =` must be `static`:  on a first write the slot holds 0, so there is no receiver.  An instance `operator =` is ignored and the store is a word write.  `deinit` has both forms and they do different jobs:  `emitter deinit()` releases a receiver at scope exit, while `static deinit(T v)` releases a slot the container has no receiver for.  A type which owns storage generally wants both (§8.5).

**Assignment copies.**  Every store into an owning element type goes through `static operator =`, which copies the incoming value into the buffer the slot owns.  That store is `<array>`'s, so an element write into an array of an owning type without `#include <array>` is a compile-time error.  Passing an already-allocated value allocates a second time; the original is not released.

A method of `array<T>`, its own or one added by `extend class array` (§12.12), which stores an element marks its parameter `literal T`:  `emitter void push(literal T item)`.  The mark applies only when the receiver's element type is `literal` (§3.13).

**Example**

```bgl
#include <array>
#include <string>

// … in a routine:
array<stringObj> slots[4];

slots += "alpha";                    // the slot allocates and owns
slots[0] = "replaced";               // reuses slot 0's buffer

stringObj tmp = "beta";              // allocated on declaration (§22.3)
slots += tmp;                        // copies into a new slot allocation; tmp keeps its own buffer,
                                     // released at scope exit, not by the array
```

**See also** §9.6 (`static` operators); §8.5 (the value form of `deinit`); §22.4 (the `<array>` methods which consult these operators).

### 12.10.1 Arrays of Structs

An array of a struct (§8.2.7) declared with a capacity or a list owns an instance per slot, as a variable of the struct does.  The instances are made at compile time:  one set per struct, holding as many instances as the program's arrays of that struct declare slots, each handed to a slot the first time it is stored into or one of its members is reached, starting from the struct's defaults.  `a[i] = v` and `a += v` copy `v` into the slot's instance through the struct's copy operator.  `remove`, `removeValue`, `-=`, `clear`, and a local array's exit give instances back, so each call of a routine (a recursive one included) has instances of its own.  A program which holds more filled slots at once than it declared will run out:  the collector runs first, giving back the instances of arrays nothing refers to any more, and only then is it a run-time error.

An array of a struct with no capacity of its own (a `<linq>` result, a returned array, a local declared from another array) holds the instances of the array it came from:  changing an element through it changes the source, and removing one gives nothing back.  It has no instances to copy a new element into, so adding one (`+=`, `append`, `insert`, `push`, a store past its length) is a run-time error; declare the array with a capacity to grow it:

```bgl
#include <linq>
struct Coin { int value; }

// … in a routine:
array<Coin> purse = { Coin{ value = 5; }, Coin{ value = 20; }, Coin{ value = 10; } };
Coin spare;
array<Coin> big = purse.filter((Coin c) => c.value >= 10);       // shares purse's coins
big += spare;                                                     // run-time error
array<Coin> mine[8] = purse.filter((Coin c) => c.value >= 10);   // eight coins of its own, two copied in
mine += spare;                                                    // copied into the third
```

Assigning a result into an array with a capacity of its own (`kept = hand.filter(…);`) copies the elements into that array's instances.

Such a view of a routine's own array of structs lives no longer than that routine, since the instances go back when it returns.  Returning one, binding a global or member to one with `:=`, or keeping one in a global or member `var` is a compile-time error; copy it into an array with a capacity of its own first.  A view pinned for Inform 6 code (`bgl.gc.keep`) is the program's to keep from outliving its source.

- The initial elements of a file-scope or object member array are the slots' instances, as static data names an instance (§3.7).  An inline object `T{ … }` is an instance of its own; a named instance (`T named { … }`) is shared with the array; a global variable of the class (`T v = T{ … };` or `T v;`) gives its slot a copy of its initial value.
- A member array of a struct declared in a class can't have initial elements; that is a compile-time error.
- `pop()`, `popEnd()`, and `dequeue()` release the slot's instance, as `remove` does, and return it.  It stays readable until the instance is handed out again, so copy what you need before adding to an array of that struct.  An element stored by reference (an instance of an ordinary class) is the caller's once it is returned:  its `static deinit` doesn't run, as it does for `remove`.
- Reordering an array of structs into itself (`trail = trail.orderBy(…);`) moves the instances between slots:  a `ref` to an element follows that element to its new place.
- A view is valid only while its source is unchanged:  once the source removes or clears an element, the view may still show that instance, which another array can then be given.

## 12.11 `extend` for Arrays

**Syntax**

```syntax
extend ARRAY {
    inject ELEMENT [ after REF | before REF | first | last ] ;
    remove REF ;
    move   REF [ after REF | before REF | first | last ] ;
}
```

`REF` is an element name or a zero-based index written `[N]`, where `[` and `]` are literal.

`extend arrayName { … }` edits the initializer of a previously declared array at compile time, from a later file or a later point in the same file.  Run-time mutation uses the array's methods (§22.4).  The body holds *array extension statements*:  `inject`, `remove`, and `move`.

- `inject element [position]` splices an element in.  The element is a named object, a literal, or an inline object `Type{ … }` (§11.3); when the element type is an object-backed class the type can be omitted (`inject { … } last;`).  With no position clause the element is appended.
- `remove X` removes an element.
- `move X [position]` repositions an existing element (remove, then inject at the new position).

`after X` and `before X` place the element immediately next to an existing element; `first` and `last` name the two ends.  An element name in a reference is the identifier of the object an element refers to (`after [0]` and `remove [2]` use an index instead).

The statements apply in source order, so a later `move` or `remove` will see the effect of earlier ones, and `[N]` indices are relative to the array's state at that point.  An unknown name or an out-of-range `[N]` is a compile-time error.

**Example**

<!-- doctest: skip -->
```bgl
array<rule> before = { cantTakeYourself, cantTakeScenery };

extend before {
    inject enteringDark  after  cantTakeScenery;
    inject reachRule     before cantTakeYourself;
    inject fallbackRule  first;
    inject wrapUpRule;                              // append
    remove cantTakeScenery;
    move   reachRule     last;
    move   fallbackRule  after cantTakeYourself;
}
```

**See also** §11.10 (`extend` for objects); §13.5 (`extend` for verb grammar); §8.7.1 (`extend class`).

## 12.12 Emitters on Arrays

An `extend class array` block adds emitters to every array (§8.1.1).  A body is raw I6, so it works with the array's own layout.  For a global or local array, `$self` is the array, element `i` is `$self-->(i+1)`, and `$prop` is `0` (§7.3).  A member array is its property's data (§12.7):  `$self` is the owning object, `$prop` the property, and element `i` is `$self.&$prop-->i`.  Alternatively, a read can be written `$i6Expr($self[i])` (§7.3.4), which emits the right form for either.

---

# 13 Dictionary Words, Verbs and Grammar

## 13.1 Dictionary Words

**Syntax**

```syntax
.WORD
..WORD
dictionaryWord NAME = .WORD ;
```

`dictionaryWord` is the type of a dictionary entry:  a token the parser matches against the player's input.  A singular word is written with a leading `.` and a plural word with a leading `..`; both have the type `dictionaryWord`.  The token form, including internal hyphens and apostrophes, is covered in §1.6.7.

Dictionary words are compared with `==` and `!=`; the `dictionaryWord` runtime type and its printing behavior are covered in §21.5.3.

**Example**

```bgl
dictionaryWord w = .lamp;
if (w == .lamp) print(w);      // -> lamp
```

**See also** §1.6.7, §21.5.3.

## 13.2 Verb Declarations

**Syntax**

```syntax
verb NAME {
    grammar = { { PATTERN } , … } ;
    void handler() { … }
    [ meta = true ; ]
    [ priority = N ; ]
    [ MEMBER … ]
}
```

`verb` is an alias class for `object` (§21.5.4) whose members are `grammarRuleList grammar`, `bool meta`, `int priority`, `handler()`, and `perform()`.  A `verb` declaration creates a named instance of that class.  The body uses ordinary object member syntax (§11.4).

A class can derive from `verb` to share behavior among several verbs.  A verb is then declared through that class, `CLASS NAME { … }`, and is a verb in every respect.

```bgl
class chatVerb : verb {
    string reply = "Nothing to say.";
    void handler() { print(reply); }
}
chatVerb Hum     { grammar = {.hum}; reply = "You hum."; }
chatVerb Whistle { grammar = {.whistle}; }

class infoVerb : verb { meta = true; }
infoVerb Credits { grammar = {.credits}; void handler() { print("Written by …"); } }
```

A variable, parameter, or array element of type `verb` can hold any verb, native or `extern`.  One typed as a subclass of `verb` holds a verb declared through that subclass, and can reach its members.

```bgl
class ChatVerb : verb { string reply; void handler() { print(reply); } }
ChatVerb Hum     { grammar = {.hum};     reply = "You hum."; }
ChatVerb Whistle { grammar = {.whistle}; reply = "You whistle."; }
array<ChatVerb> chats = { Hum, Whistle };

ChatVerb v = chats[1];
v.perform();                 // runs the Whistle action
if (v == action) { … }
verb any = v;                // any verb fits a `verb`
print(v.reply);
```

A verb name resolves like any identifier:  a local variable of the same name will shadow the verb (§3.10).

A verb named like a library verb (`verb Empty` beside the binding's `extern verb Empty`) is a compile-time error, and you extend the library verb instead (§13.5).  A program's own verb can't take a fake action's name either (§13.2.3).  A verb can share its name with a class (`verb Seal` beside `struct Seal`).

In the generated Inform 6, the verb's action and its action routine keep the verb's name (`##Size`, `SizeSub`), while the object carrying `handler()` is named `_bglVerb_NAME`.  I6 code naming the verb object reaches it through `$i6Name` (§7.3.3).  A verb whose action routine would take the name of a routine the binding declares (`verb Multi` and the library's `MultiSub`) is a compile-time error.

**Example**

```bgl
verb Inspect {
    grammar = {
        {.inspect, NOUN},
        {.study, NOUN},
    };
    void handler() {
        print("You inspect it closely.");
    }
}
```

> ***Note:** When the first token inside `grammar = { … }` is a dictionary-word literal, the outer braces are read as those of a single grammar line, so the inner braces can be omitted:  `grammar = {.whistle, NOUN};` is equivalent to `grammar = { {.whistle, NOUN} };`.  `|`-alternation is allowed in this form (`grammar = {.hum|.murmur, NOUN};`).  The single-line form is recognized wherever a grammar line list is accepted.*

**See also** §11.2, §21.5.4.

### 13.2.1 `handler()`

**Syntax**

```syntax
void handler() { … }
```

`handler()` is the verb's action body:  it will run when the player enters a command matching the verb's grammar.  The `verb` class declares it `required default` (§8.3.6), so a verb declared in Beguile must provide one; omitting it is a compile-time error.  The `default` body, which runs the verb's I6 routine, is used only by extern verbs (§13.2.3).

**Example**

```bgl
verb Twirl {
    grammar = { {.twirl} };
    void handler() { print("You twirl on the spot."); }
}
```

### 13.2.2 `perform()`

**Syntax**

```syntax
VERB.perform() ;
VERB.perform( NOUN ) ;
VERB.perform( NOUN , SECOND ) ;
```

`perform()` runs the verb's action from code.  It uses the active IF library's action runner, which saves and restores the current actor, action, noun, and second, and runs the surrounding before/after rules.  (The Inform 6 standard library skips the before rules while a `meta` verb is running.)

`perform()` works on any verb, native or `extern`, whether or not it defines a `handler()`.  To run an action and then return true from the enclosing function, follow the call with an explicit `rtrue;`.

**Example**

<!-- doctest: skip -->
```bgl
Enter.perform(door);
Take.perform(coin, pouch);
Take.perform(coin); rtrue;
```

### 13.2.3 Extern Verbs and Claimed Words

**Syntax**

```syntax
extern verb NAME { }
extern verb NAME { .WORD [ | .WORD ] … }
extern verb NAME { grammar = { { .WORD [ | .WORD ] … } , … } ; }
```

The `|` in these forms is the literal alternation token (§13.4.3), not notation.

A verb whose behavior is defined by the I6 library is declared `extern verb` with a body:  its trigger words, or `{ }` for an action the player can't type.  An extern verb needs no `handler()`; it falls back to the `default` one from `verb`, which runs the I6 routine (`<name>Sub`).

Without a body, `extern verb NAME;` declares a *variable* of type `verb`, as any class-typed declaration without a body does (`extern object noun;`).  The library's `action` is declared this way.

An I6 verb can claim several trigger words (`inventory`, `inv`, `i`).  Declaring them in the extern verb's body makes the claims visible, so a grammar line elsewhere using one of those words will extend the library verb rather than define a new one.  The body uses the same `grammar` syntax as a native verb, with these rules:

- Several words are listed with `|`-alternation (§13.4.3).  Splitting them over several lines or listing them all in one yields the same claimed-word set.
- Pattern tokens after the trigger word are ignored, and writing one is a warning.
- The first word of the first grammar line is the verb's *primary trigger*, the word used as the target when the verb is extended (§13.5).  All listed words are equally claimed for collision detection.

Extern verbs can't be marked `meta` and can't carry a `priority`.

When an extern verb's body begins with a dictionary-word literal, the body is read as the trigger-word section of a single grammar line:  `extern verb Inv { .inventory|.inv|.i }` is equivalent to `extern verb Inv { grammar = { {.inventory|.inv|.i} }; }`.  This form accepts trigger words only; a body with several lines or other members uses the full form.

**Example**

```bgl
extern verb Take { .take|.get }
extern verb Inv  { .inventory|.inv|.i }
extern verb Quit { .q|.quit|.die }
```

**See also** §16.4.2, §23.3.6.

### 13.2.4 Meta Verbs

**Syntax**

```syntax
meta = true ;
```

Setting the `verb` member `meta` to `true` marks the verb as out-of-world:  it will run without advancing the turn or triggering daemons or timers.  `meta` is recognized only on `verb` instances and is not a run-time property.  Extern verbs can't be marked meta (§13.2.3).

Inform 6 records meta-ness on the dictionary word, not on the action.  A word the library's grammar already declares (`wipe`, `shut`) keeps the library's non-meta marking, so a meta verb claiming such a word will still take a turn when the player types it; the compiler warns.  Give a meta verb words of its own.

**Example**

```bgl
verb Inventory {
    meta = true;
    grammar = { {.inventory}, {.i} };
    void handler() { … }
}
```

### 13.2.5 Verb Priority

**Syntax**

```syntax
priority = N ;
extend VERB { priority = N ; grammar += { … } ; }
grammarRule NAME = { VERB , { PATTERN } , N } ;
```

Priority orders grammar lines which come from several sources and target the same trigger word; the parser tries a lower number earlier.  The `verb` member `priority` defaults to `10`; it is recognized only on `verb` instances and is not a run-time property.

A verb's *anchor* is the priority declared in its own `verb` body, or `10` when none is declared (an extern verb always has the default anchor).  A program verb whose own grammar uses a word a library verb already owns is placed against the library's lines by the `verb` member `libraryPriority`, which is `20` by default; with an anchor below it (the default), the program verb's lines are tried first.  Every other contribution to that verb's trigger words sorts relative to the anchor (see **Ordering** below).  The verb a `synonyms` list attaches to (§13.5.4) is not an anchor in this sense.

| Source | Syntax | Scope | Default |
|---|---|---|---|
| Verb body (the anchor) | `priority = N;` inside `verb V { … }` | The verb's own lines; stored on the verb | `10` |
| `extend` block | `priority = N;` inside `extend V { … }` | Every line added by that block's `grammar +=`; not stored on the verb | `10` |
| Grammar-object rule | Third positional element of a `grammarRule` initializer, `{V, {pattern}, N}` | That one rule, sorted against its target verb's anchor; a grammar object has no block-level default, so the value is repeated per rule | `10` |
| Inferred-verb line | none | A line written `{pattern}` takes the owning verb's anchor, or the enclosing `extend` block's priority | - |

Combining `priority = N;` with `replace grammar = { … }` in the same `extend` block is a compile-time error (§13.5.3).  An extern verb can't carry a `priority` in its declaration (§13.2.3).

**Ordering.**  For each trigger word of a verb, lines with `priority < anchor` are tried before the verb's own lines, the verb's own lines come next, and lines with `priority >= anchor` from other sources come last.  Per-rule and block-local priorities take part in the same ordering.

**Example**

```bgl
verb Pocket {
    priority = 5;
    grammar = { {.pocket, NOUN}, {.stash, NOUN} };
    void handler() { … }
}

extend Look {
    priority = 5;                              // before Look's own lines
    grammar += { {.peek, NOUN} };
}

extend Look {
    priority = 12;                             // after Look's own lines
    grammar += { {.look, .carefully, NOUN} };
}

grammar additions {
    grammarRule r1 = {Pocket, {.nab, NOUN}};           // 10
    grammarRule r2 = {Drop, {.toss, HELD}, 5};         // 5
}
```

**See also** §13.4.5, §13.5.

## 13.3 Action Comparisons

**Syntax**

```syntax
action == VERB
action != VERB
switch (action) { case VERB : … }
```

The library variable `action` has the type `verb`.  You can compare it against a verb name with `==` and `!=`, and a `switch` on `action` takes verb names as its case values (§5.12).

**Example**

```bgl
if (action == Take) { … }

switch (action) {
    case Take: print("Taken.");
    case Drop: print("Dropped.");
}
```

A verb which shares its name with a `#define` symbol is still a verb where one is expected (§3.8.3).

**See also** §5.12, §23.3.4.

## 13.4 Grammar

Grammar lines define what the player can type and which verb it will trigger.  A line is declared on a verb (§13.4.5) or in a grammar object, to the same effect.

### 13.4.1 Grammar Types

The following types are used to declare grammar:

| Type | Purpose |
|---|---|
| `grammarToken` | An extern enum declared by the IF library binding (§23.3.6); its values are the parser tokens `NOUN`, `HELD`, `CREATURE`, ….  Both the bare value (`HELD`) and the qualified form (`grammarToken.HELD`) are valid in pattern position.  `NOUN(Routine)` and `SCOPE(Routine)` are its parameterized forms. |
| `patternElement` | One element of a pattern:  a dictionary word, a grammar token, an attribute, or a general parsing routine (§13.4.2).  A pattern is an `array<patternElement>`, written `{.examine, NOUN}`.  A program writes patterns in a verb's `grammar`; declaring a variable or member of this type is a compile-time error. |
| `grammarRule` | One verb-targeted pattern, with an optional priority:  `{Examine, {.examine, NOUN}}` or `{Examine, {.examine, NOUN}, 5}`. |
| `grammarRuleList` | A list of grammar rules; the type of the `grammar` member on `verb` and of a grammar object. |

`patternElement` is the base type of one element of a grammar pattern.  These types are the receivers of the grammar operators (`=`, `+=`, `-=`, `replace … =`).

A `grammarRule` has two initializer forms:

- **Explicit verb**:  `{Verb, {pattern}[, priority]}`.  Valid in any context.
- **Inferred verb**:  `{pattern}`.  The verb is the owning object, which should be a `verb` or a subclass of `verb`; otherwise a warning is issued.  The priority is the owning verb's anchor or the enclosing `extend` block's (§13.2.5).

A `grammarRule` member takes exactly one `{verb, {pattern}}` pair, while an `array<grammarRule>` member takes a list of them.  Any other shape is a compile-time error.

### 13.4.2 Pattern Tokens

Each element of a pattern is one of the following:

| Token | Matches |
|---|---|
| `.word` | the player typing that word |
| `..words` | the plural form of the word |
| `.w1 \| .w2` | any one of the listed words; may be parenthesized |
| `NOUN` | any in-scope object |
| `HELD` | a held object |
| `CREATURE` | a creature or actor |
| `TOPIC` | a topic phrase |
| `MULTI` | one or more in-scope objects |
| `MULTIHELD` | one or more held objects |
| `MULTIEXCEPT` | one or more in-scope objects, excluding one already matched (used after a preposition:  `MULTIEXCEPT, .in, NOUN`) |
| `MULTIINSIDE` | one or more objects inside a specific container (used after a preposition:  `MULTIINSIDE, .from, NOUN`) |
| `NUMBER` | a number typed by the player, range-checked |
| `ANYNUMBER` | any number, no range check |
| `SPECIAL` | a number or a dictionary word |
| *attributeName* | an object that has that attribute (`container`, `animate`) |
| *RoutineName* | a general parsing routine:  a global function the parser calls to consume input words (`NextWord()`, `wn`); it returns -1 for no match, 0 when it matched words but no value, 1 when it put a number in `parsed_number`, or the object it matched |
| `NOUN(Routine)` | a noun filter:  the parser matches nouns normally, puts each candidate object in `noun`, and calls the global `bool` routine, which accepts or rejects it |
| `SCOPE(Routine)` | a scope setter:  the global `bool` routine decides which objects are in scope for this line, using the library's scope routines (`PlaceInScope()`, `ScopeWithin()`) |

A `NUMBER`, `ANYNUMBER`, or `SPECIAL` token fills a noun slot as an object token does:  the number becomes `noun` (or `second`, for the line's second value), which a handler can read with a cast, `(int)second`.  The library's `parsed_number` holds the last number matched.  A `TOPIC` token takes a slot too:  in `{.type, TOPIC, .on, NOUN}` the object is `second`, and `noun` is 0.

The parser calls each routine token with no arguments, so a routine named in a pattern which declares parameters is a compile-time error.

Both bindings declare `scope_stage`, the pass (1, 2, or 3) the parser is making when it calls a scope routine.  Objects are placed in scope on pass 2, as in `if (scope_stage == 2) { PlaceInScope(obj); }`.

A bare identifier in a pattern must be declared as a `grammarToken`, an `attribute`, or a global function; any other declaration, or an undeclared name, is a compile-time error.  In pattern position a bare `NOUN` resolves to the grammar token even when an `extern object noun` exists at file scope; however, outside a pattern the global wins, and `grammarToken.NOUN` selects the token explicitly.

By convention, grammar tokens are written in uppercase (`NOUN`, `HELD`, `REVERSE`).  Beguile is case-insensitive (§1.3), so `noun` in a pattern is the same token.

**Example**

```bgl
bool isEdible() { return noun.has(edible); }

verb Taste {
    grammar = { {.taste, NOUN(isEdible)} };
    void handler() { … }
}

int parseColor() { … }

verb Paint {
    grammar = { {.paint, NOUN, parseColor} };      // "paint OBJECT COLOR"
    void handler() { … }
}

verb Chat {
    grammar = { {.chat, animate, .about, TOPIC} };
    void handler() { … }
}
```

**See also** §11.6, §23.3.6.

### 13.4.3 Alternation and Multi-trigger Lines

Dictionary words separated by `|` will match any one of them, and the group can be parenthesized.  Alternation is allowed in any position, including the first.

In the first position, alternation declares one pattern which fires on any of the listed trigger words.  The line behaves as one line per word, each treated independently (§13.5.2).  A new word defines that word's grammar, while a word already claimed by another verb (a library verb through its claimed words, or an earlier Beguile verb) adds the line to that word's grammar.  The same applies to multi-trigger lines inside `extend V { grammar += { … } }`.

**Example**

```bgl
verb Stow {
    grammar = {
        {.stow, HELD, .on | .onto | .upon, NOUN},
        {.stow, HELD, (.in | .into | .inside), NOUN},
    };
    void handler() { … }
}

verb TypeNum {                                     // three trigger words: one line per word
    grammar = { {.type | .enter | .put, NUMBER, .into | .in | .on | .onto, NOUN} };
    void handler() { print("You can't type anything there."); }
}
```

### 13.4.4 Line Modifiers: `REVERSE`, `withI6Synonyms`

**Syntax**

```syntax
{ PATTERN [ , REVERSE ] [ , withI6Synonyms ] }
```

Two pseudo-tokens can end a grammar line, in the above order; neither is matched against input.  The dotted forms `.reverse` and `.withI6Synonyms` are ordinary dictionary words and are unaffected.

- `REVERSE` swaps `noun` and `second` when the action receives its parsed arguments:  `{.give, CREATURE, HELD, REVERSE}`.
- `withI6Synonyms` widens the line's effect on a library verb.  A library verb can group several synonym words under one grammar table; when a line's trigger word belongs to such a group, the line affects only that word and its synonyms are untouched.  With `withI6Synonyms`, the line applies to every word in the group.  On a native word, or a library word with no synonyms, the modifier has no effect.

A line on a library word splits that word off its I6 `Verb`; when it was the only word of its `Verb`, Inform 6 warns that the declaration is left with no words; the compiler hides this warning unless `filterVerbSplitWarnings` is `false` (§15.3).  For background (not part of this specification), see [Verbs and Grammar](../Verbs-Grammar.md).

**Example**

```bgl
verb Keypad {
    grammar = { {.enter, NUMBER, .into, NOUN} };    // "enter 5 into keypad"; "cross" unaffected
    void handler() { … }
}

extend Take { grammar += { {.take, .all, .from, NOUN, withI6Synonyms} }; }   // take/get/carry/…
```

### 13.4.5 Grammar on Verbs and Grammar Objects

**Syntax**

```syntax
verb NAME { grammar = { { PATTERN } , … } ; … }

grammar NAME {
    grammarRule RULE = { VERB , { PATTERN } [ , PRIORITY ] } ;
    array<grammarRule> RULES = { { VERB , { PATTERN } [ , PRIORITY ] } , … } ;
}
```

The `grammar` member of a verb is a `grammarRuleList` whose lines use the inferred-verb form; the verb is the owning object.

A `grammar` declaration declares an object of class `grammarRuleList`.  A grammar object is cross-cutting:  one object can carry rules which target many verbs, each pairing a pattern with an explicit verb.  Member types can be inferred, and an `array<grammarRule>` member holds several rules.  The per-rule priority is the optional third element (§13.2.5).

**Example**

```bgl
grammar customPatterns {
    rule1 = {PutOn, {.hang, HELD, .on, NOUN}};
    array<grammarRule> rules = {
        {PutOn,  {.put, HELD, .on, NOUN}},
        {Insert, {.put, HELD, .in, NOUN}, 5},
    };
}
```

**See also** §13.2.5.

## 13.5 Extending Grammar

**Syntax**

```syntax
extend VERB {
    [ priority = N ; ]
    grammar += { { PATTERN } , … } ;
    grammar -= { { .WORD , PATTERN } , … } ;
    grammar -= { { .WORD } , … } ;
    replace grammar = { { PATTERN } , … } ;
    synonyms = { .WORD , … } ;
}
```

The two `grammar -=` forms are the line-level and word-level shapes of §13.5.2; entries of both shapes can be mixed in one list.

Grammar is added to or removed from an existing verb, including an `extern verb`, inside an `extend V { … }` body (§11.10).  An operator is required:  a bare `grammar = { … }` is valid only in the original `verb` or `extern verb` declaration, and is a compile-time error inside an `extend`.  A bare `priority = N;` in the block applies to the lines it appends (§13.2.5).

### 13.5.1 `grammar +=`

`grammar += { … }` appends lines to the verb's grammar.  It accepts multi-trigger lines (§13.4.3) and line modifiers (§13.4.4).

**Example**

```bgl
extern verb PutOn { .put }
extend PutOn {
    grammar += { {.hang, HELD, .on, NOUN} };
}
```

### 13.5.2 `grammar -=`

`grammar -= { … }` removes grammar; how much depends on how much of the line you name:

- **Line-level**:  `{.w, pattern…}` removes the one line which matches exactly:  same trigger word, same tokens, same `REVERSE` flag.
- **Word-level**:  `{.w}` removes all of that word's grammar.

A partial (prefix) pattern matches nothing:  `-= { {.give, NOUN} }` removes only the line whose pattern is exactly `NOUN`, never `{.give, NOUN, .to, NOUN}`.  An alternation in a `-=` entry (`.a|.b|.c`) is one removal per word, each matched and warned separately.  Removal is source-ordered:  a `-=` sees only the lines declared before it, so a later `+=` of the same line is unaffected.  A `-=` which matches nothing is a warning.

**Example**

```bgl
verb TypeNum { grammar = { {.type|.dial, .into, NOUN}, {.dial, .to, NOUN} }; void handler() { … } }
extend TypeNum { grammar -= { {.dial, .into, NOUN} }; }   // that one dial line
extend TypeNum { grammar -= { {.dial} }; }                // every dial line
```

**Extern verbs.**  For a library verb, Beguile knows the words it claims and the lines Beguile itself added with `+=`; however, the library's own patterns are opaque.  A line-level `-=` therefore matches only Beguile-added lines, and naming a claimed word with a pattern which matches none of them is a warning.  A word-level `-=` on a claimed word *evicts* the word from the library verb:  if a native verb also declares that word, its lines take the word over; otherwise the word is disabled.  The word must be genuinely claimed by the library verb.

Eviction discards the library's grammar for the word.  The lines a native verb declares for it become its complete grammar, and lines another extern verb adds for it with `grammar +=` are kept.  For background (not part of this specification), see [Verbs and Grammar](../Verbs-Grammar.md).

```bgl
extend Enter  { grammar -= { {.enter} }; }      // evict 'enter' from the library verb …
verb  Keypad  { grammar = { {.enter, NUMBER, .into, NOUN} }; void handler() { … } }   // … and reclaim it
extend Disturb { grammar -= { {.xyzzy} }; }     // no reclaimer: 'xyzzy' is disabled
```

**See also** §13.2.3, §13.4.3.

### 13.5.3 `replace grammar =`

`replace grammar = { … }` discards all of the verb's grammar (every trigger word and every prior `+=` and `-=`, regardless of their source order), and the listed lines become its complete grammar.  Only this verb is affected; a word which another verb also uses keeps its grammar there.  On an extern verb, the replacement overrides the library's grammar for the named words.  Combining `replace grammar =` with `priority = N;` in the same block is a compile-time error.

**Example**

```bgl
verb TypeNum { grammar = { {.type|.dial, .into, NOUN} }; void handler() { … } }
extend TypeNum {
    replace grammar = { {.dial, .to, NOUN} };     // 'type' is dropped
}

extend Take {
    replace grammar = { {.take, .firmly, NOUN} };  // overrides the library's 'take'
}
```

**See also** §13.2.5.

### 13.5.4 Synonyms

`synonyms = { .word, … }` inside an `extend` makes the listed words true aliases of the extended verb:  they share the verb's grammar rather than copying it, so a later `extend` of the verb will reach every synonym.  By contrast, adding the words as separate trigger lines with `grammar +=` copies the pattern set at that point, and later extensions won't reach them.

The alias is attached to the verb's primary trigger word (i.e., its first claimed word, or its name if it declares none).  `synonyms` works on both extern and native verbs.  The one-letter and plural word conventions apply (§1.6.7).  An empty `synonyms = { }` is a compile-time error.

There is no Beguile form for I6's `Extend 'w' only` directive; if you need it, write it in an `#i6` island (§16.2).

**Example**

```bgl
extend Take { synonyms = {.steal, .grab, .pilfer}; }
extend Take { grammar += { {.take, .quietly, NOUN} }; }   // also "steal quietly", "grab quietly"
```

**See also** §13.2.3, §16.2.

---

# 14 Directives

A directive is a compile-time instruction introduced by `#`.  Directive names are case-insensitive, like every Beguile identifier (§1.3).  Unless an entry says otherwise, a directive is written at global scope.  An unrecognized directive is a compile-time error.

Inside a function body, you can write only the conditionals (§14.2.5), `#message`, `#warning`, `#error`, and the island directives (§14.5).  Any other directive there is a compile-time error.

Appendix B lists every directive alphabetically.

## 14.1 Source Organization

### 14.1.1 `#include <NAME>`

**Syntax**

```syntax
#include <NAME>
#include <SUB/NAME>
#include <A/B/NAME>
```

The angle brackets are literal.

This includes a Beguile source file from the Beguile Language Runtime library tree (`beguiLib`, located by the `beguiLibPath` setting, §15.2).  The tree is searched recursively for `NAME.bgl`; the compiler supplies the `.bgl` extension, which you must not write.

- `<NAME>` matches `NAME.bgl` anywhere in the tree, root or any subfolder.
- `<SUB/NAME>` matches `NAME.bgl` only when its immediate parent folder is `SUB`, at any depth.
- `<A/B/NAME>` requires the resolved path to end with `A/B/NAME.bgl`.

The search is deterministic:  the root folder's files are checked first, then each subfolder in alphabetical order, depth first, and the first match wins.  Matching is case-insensitive regardless of the file system.  A file which is not found is a compile-time error.  Only the library is searched; the source folder and `includePaths` belong to the quoted form (§14.1.2).

**Example**

<!-- doctest: skip -->
```bgl
#include <string>                  // beguiLib/string.bgl
#include <i6StandardLibrary>       // same as <bindings/i6StandardLibrary>
#include <bindings/punyInform>     // beguiLib/bindings/punyInform.bgl
```

**See also** §18.5, §22.

### 14.1.2 `#include "path"`

**Syntax**

```syntax
#include "PATH"
```

This includes a Beguile source file by path.  The compiler searches the directory of the current source file, then each `includePaths` directory (§15.3), trying *path*`.bgl` first and then *path* exactly as written.  Subdirectory paths are allowed and resolve relative to each directory searched, with either `/` or `\` as the separator.  Matching is case-insensitive.  A file which is not found is a compile-time error.

A quoted include never finds a file in the Beguile library, even when a directory it searches lies inside it; a quoted include of a library file is a compile-time error naming the angle-bracket form (§14.1.1).

**Example**

<!-- doctest: skip -->
```bgl
#include "myLibrary"
#include "utils/helpers"
```

> ***Note:** Beguile has no equivalent of Inform 6's `>filename` prefix.*

**See also** §14.1.7, §18.5.

### 14.1.3 `#include @"path"`

**Syntax**

```syntax
#include @"PATH"
#include ?@"PATH"
```

This works as `#include "path"` does, except that the path is a raw string (§1.6.4) with no escape processing.  An interpolated string (`$"…"`) is not accepted as an include path.

**Example**

<!-- doctest: skip -->
```bgl
#include @"vendor\legacy\helpers"
```

**See also** §1.6.4.

### 14.1.4 `#include ?"path"` and `#include ?<NAME>`

**Syntax**

```syntax
#include ?"PATH"
#include ?<NAME>
```

The angle brackets are literal.

These work as the corresponding unmodified forms do, except that a file that is not found is silently skipped.

**Example**

```bgl
#include ?"optionalExtension"
#include ?<glulxImage>
```

### 14.1.5 `#includeI6`

**Syntax**

```syntax
#includeI6 "NAME"
#includeI6 ?"NAME"
#includeI6 @"NAME"
```

`#includeI6` includes an Inform 6 source file in the generated program.  The file is resolved like `#include "path"` (§14.1.2):  the current source file's directory, then each `includePaths` directory, trying *name* as written and then *name*`.h`.  Subdirectory paths are allowed.  A file which is not found is a compile-time error.

`?"name"` silently skips a file that is not found.  `@"name"` hands the string to the I6 compiler verbatim, with no search, existence check, or separator rewriting.

An included I6 file's own includes are also searched for in every `includePaths` directory.

A `.patch` or `.diff` file is a compile-time error here; patches are applied with `#mergeI6` (§14.1.6).

**Example**

```bgl
#includeI6 "parser"
```

> ***Note:** A Beguile declaration which depends on something the I6 file defines must follow the include (§16.4, §18.7).*

> ***Note:** An `extern` declaration for a name the included file defines can come before or after the include, except that a class using an `extern attribute` must follow that attribute's declaration (§18.7).*

**See also** §16.5, §18.5.

### 14.1.6 `#mergeI6`

**Syntax**

```syntax
#mergeI6 "NAME"
#mergeI6 ?"NAME"
```

`#mergeI6` applies a patch to the Inform 6 files of the build.  The patch is a unified diff, as written by `diff -u` or `git diff`.  It is found like `#includeI6` (§14.1.5), trying *name* as written, then *name*`.patch`, then *name*`.diff`.  A patch which is not found is a compile-time error; however, `?"name"` silently skips it, and skips any file the patch names which is not found.

Each file the patch changes is found through the same search, by its `+++` path without a leading `a/` or `b/`, then by its file name alone.  Its **merge chunks**, the sections which each start with `@@`, are applied in order.  A merge chunk must match its lines exactly, ignoring trailing whitespace, at the line it states or the nearest place they match.  A merge chunk which matches nowhere, a file that is not found, or a malformed patch is a compile-time error at the directive.  A patch can't create or delete a file.

A **routine chunk** replaces a whole routine without quoting its old text.  It starts with `@@ routine NAME @@`, and the lines after it, up to the next chunk or file header, are the routine which takes the old one's place:

```
--- a/verblib.h
+++ b/verblib.h
@@ routine Banner @@
[ Banner;
    print "My banner.^";
];
```

The routine is found by name, wherever it is in the file; where the file doesn't hold it, the files it includes are searched.  A routine which is not found, or which the file defines more than once, is a compile-time error.

A merge applies to the whole build, not at its position:  a patch can come before or after the `#includeI6` of the file it changes.  Patches apply in source order, each to the file as the earlier ones left it.  Two patches which change the same lines conflict.  A patch merged more than once applies once.

The patched files are written to the `i6patched` folder of the output directory, and every include of an original will read the patched copy, including one made inside another I6 file.  When the source file is a `.inf`, merges are not applied; its Inform 6 files are read as they are.

**Example**

Here's a patch which makes the library's `LookSub` call a Beguile function first:

```diff
--- a/verblib.h
+++ b/verblib.h
@@ -2446,4 +2446,5 @@
 [ LookSub allow_abbrev  visibility_levels i j k nl_flag;
+    LookHook();
     if (parent(player) == 0) return RunTimeError(10);
 
   .MovedByInitial;
```

An extension merges it and defines the function, so a program need only include the extension:

<!-- doctest: skip -->
```bgl
#once
#mergeI6 "lookHook"
void LookHook() { printLine("You look around."); }
```

> ***Note:** Patches can't be used in precompiler mode (§17.8).*

> ***Note:** To write a patch, edit a copy of the file and write the differences with `diff -u original.h edited.h > name.patch`, `git diff`, or `beguiler --make-patch` (§17.10).  Any unified diff will do, including one pasted into a `.patch` file.*
>
> ***Note:** Lines before the first `---` line are not part of any change, so a patch can start with comments (by convention, lines starting with `#`) which never reach the patched file.  `beguiler --make-patch` keeps them when it rewrites the patch.*

> ***Note:** Replacing a library routine (§16.6) takes the whole routine, and only one replacement can win; however, a patch changes only the lines it names, so extensions which each change a routine can be used together.*

**See also** §16.6, §20.1.

### 14.1.7 `#once`

**Syntax**

```syntax
#once
```

Placed at the top of a source file, `#once` marks the file so that any later `#include` of the same file (by any path resolving to the same location) is silently ignored.  Without `#once`, a file is processed every time it is included, so circular includes of such files exceed the include nesting limit (§18.4).

**Example**

```bgl
#once
// rest of myLibrary.bgl …
```

> ***Note:** Every file intended to be included as a library should begin with `#once`.*

**See also** §14.4 (per-file deduplication that does not depend on `#once`).

### 14.1.8 `#emitAfterI6Include`

**Syntax**

```syntax
#emitAfterI6Include "NAME"
```

Some code simply has to come after a piece of the library; an object which lives in `LibraryExtensions` can't appear before the parser defines it.  Put this directive at the top of a file and everything below it is written out right after the program's `#includeI6 "NAME"` (§14.1.5), no matter where the file itself is included.  The name is the Inform 6 file's, with or without the `.h` and in any letter case.

A file which depends on an Inform 6 file says so once, and a program can then include it anywhere:

```bgl
// lookCount.bgl
#once
#emitAfterI6Include "verblib"

int looks = 0;
replace void LookSub() { looks++; print("You look around.^"); }
```

<!-- doctest: skip -->
```bgl
#include <bindings/i6StandardLibrary>
#include "lookCount"          // emitted after verblib

#includeI6 "parser"
#includeI6 "verblib"
```

The directive covers this file's own declarations, from the directive to the end of the file.  A file it includes is emitted where that file's own directive says, or at the include when it has none.  Files placed after the same Inform 6 file keep the order in which they were included.  When the `#includeI6` comes before this file's `#include`, the file is emitted at its include.

A file can have only one `#emitAfterI6Include`.  A program which never includes the named Inform 6 file is a compile-time error at the directive.

**See also** §14.1.5, §16.5.

## 14.2 Symbols and Conditional Compilation

### 14.2.1 `#define`, `#defineI6`

**Syntax**

```syntax
#define NAME
#define NAME VALUE
#defineI6 NAME
#defineI6 NAME VALUE
```

`#define` defines a compilation symbol.  Without a value, the symbol has the boolean value `true`, which is what `#if` tests (§14.2.5); such a symbol is not a name in a Beguile expression, where using it is a compile-time error.  A symbol with a value can also be used in a Beguile expression, where the name is replaced by its value at compile time:  an integer value is an integer literal, a decimal number (`0.5`) is a float literal, and a quoted string is that string.  Any other word is a string literal of that word.

`#define` is linear:  it affects only the source which follows it, in the order files are processed.  Defining a symbol which is already defined is a compile-time error (use `#redef`, §14.2.2), as is defining one that was `#declare`d (§14.2.3).

A symbol shares its name with declarations.  A parameter, local, loop variable, or lambda parameter of that name hides the symbol where it is in scope, and so does a member of the class or object whose code is being compiled; each draws a warning.  A member's initializer is outside that code and reads the symbol.  `::NAME` skips the hiding declaration and reaches the symbol, unless a variable is declared at file scope under the name; in that case `::NAME` is that variable, and a bare `NAME` there is ambiguous and an error.  A property of the name doesn't compete with the symbol, except where a property is expected (`obj.provides(capacity)`).

`#defineI6` is a `#define` which also passes the symbol to Inform 6 as a `Constant` of the same name and value, placed ahead of any I6 include.  In a Beguile expression, a `#defineI6` symbol with a value is emitted by name, as the I6 constant, rather than replaced by its value.  A later `#undef` or `#redef` affects only the Beguile symbol; the I6 constant stays.

Use `#defineI6` to set an I6 library's option constants (`NO_PLACES`, `OPTIONAL_EXTENDED_METAVERBS`, …), since the library bindings declare a library's optional parts only under the matching symbol (§23.3.10).  Note the following:

- The `#defineI6` must come before the `#include` of the binding, or the binding's `#if` won't see it.
- A constant written in raw I6 (`#i6 { Constant NO_PLACES; }`) is invisible to `#if`, so the binding will still declare the parts it removes.

**Example**

```bgl
#define CHEATS         // CHEATS is true
#define MAX_SCORE 100
#defineI6 OPTIONAL_EXTENDED_METAVERBS   // also Constant OPTIONAL_EXTENDED_METAVERBS; in I6
const int maxScore = MAX_SCORE;

// … in a routine:
if(score >= MAX_SCORE) print("You win!");   // compiles as: if(score >= 100)
```

### 14.2.2 `#redef` and `#undef`

**Syntax**

```syntax
#redef NAME
#redef NAME VALUE
#undef NAME
```

`#redef` behaves exactly like `#define`, except that an already-defined symbol is overwritten rather than reported.  `#undef` removes a symbol, so that a later `#if` will treat it as undefined.  Both are linear, like `#define`.  Applying either to a `#declare`d symbol is a compile-time error.

**Example**

```bgl
#define LEVEL 1
#redef  LEVEL 2      // now 2
#undef  LEVEL        // now undefined
```

### 14.2.3 `#declare`

**Syntax**

```syntax
#declare NAME
#declare NAME VALUE
```

`#declare` defines a symbol just as `#define` does, with two differences:

- **Order-independent.**  A declared symbol is visible to every `#if` in the program, including those in files processed before the file containing the `#declare`.
- **Immutable.**  A declared symbol can't be `#undef`d, `#redef`d, or shadowed by a `#define`; each is a compile-time error, as is a second `#declare` of the same name with a different value.

In every other respect it matches `#define`.

| | `#define` / `#redef` | `#declare` |
|---|---|---|
| Visibility | Linear:  source after the directive | Global:  every `#if`, before or after |
| Across `#include` | Only from an earlier-processed file | Any file, in any include order |
| Mutability | `#redef` overwrites, `#undef` removes | Immutable |
| Intended for | Configuration, constants, toggles | Capability flags a library advertises |

**Example**

```bgl
// A core file, processed first:
#if I6_STANDARD_LIBRARY
    // use something the standard-library binding provides
#else
    // self-contained fallback
#endif

// The binding, included later:
#declare I6_STANDARD_LIBRARY
```

**See also** §23.3.3.

### 14.2.4 Pre-defined Symbols

The compiler defines the following symbols before any source file is processed:  `beguiler`, `beguilerMajor`, `beguilerMinor`, `beguilerPatch` (the compiler version); exactly one of `TARGET_GLULX` or `TARGET_ZCODE` (the compilation target, from the `target` setting); `DEBUG` in a debug build (§20.3); and `bglAutoInitialize` when the `autoInitialize` setting is `true` (§15.4).  They behave as `#define`d symbols in `#if` expressions, and those with a value as inline literals in Beguile expressions.  Their values and the resolution rule are given in Appendix F.

`generateBlorb` is also visible to `#if` as the symbol `generateBlorb`, carrying the value `true` or `false` (§15.6).  Test it by value, `#if (generateBlorb == true)`, since the symbol is always declared (Appendix F).

The two target symbols are flags with no value.  To tell `Z5` from `Z8`, read the setting instead:  `#if #beguilerSettings.target == "z8"` (§14.2.5, §15.7).

**See also** Appendix F, §15.3.

### 14.2.5 `#if`, `#elif`, `#else`, `#endif`

**Syntax**

```syntax
#if EXPRESSION
    …
[ #elif EXPRESSION
    … ] …
[ #else
    … ]
#endif
```

These directives include or exclude the enclosed source text according to a compile-time expression over the currently defined symbols.  Source in an excluded branch is skipped without being parsed.  Conditional blocks nest, and an excluded declaration doesn't exist.

`#if`, `#elif`, `#else`, and `#endif` can appear anywhere a declaration, member, statement, or `extend` edit can:  at file scope, in a function body, in an object or class body, in an `extend` body, and among the `inject`, `remove`, and `move` lines of an array's `extend` body (§12.11).

The expression can contain:  symbol names; integer literals; string literals; `#beguilerSettings.PROPERTY` references (§15.7); `true` and `false` (the values `1` and `0`); the comparisons `==`, `!=`, `<`, `>`, `<=`, `>=`; `&&`, `||`, `!`; and parentheses.

A `#beguilerSettings.PROPERTY` reference resolves to the property's value, as it does in a Beguile expression (§14.7.2).  String comparison ignores case.  The property must be written with no space around the `.`; an undeclared property is a compile-time error, not a false condition.

The condition runs to the end of its line.  Anything after it on that line, other than a comment, is a compile-time error.

A bare symbol name is true when the symbol is *defined*, regardless of its value, so `#if V` is true even when `V` was defined as `0` or `false`.  In a comparison, the name resolves to the symbol's *value*; an undefined symbol compares as `0`.  A symbol whose value is a quoted string compares as its text.

There is no `#ifdef` or `#ifndef`, in either the single- or double-hash form; `#if SYMBOL` and `#if !SYMBOL` test definedness.  `#if` tests *Beguile* symbols (a symbol set with `#defineI6` is both, §14.2.1).  To emit a conditional for the Inform 6 compiler to evaluate (over one of its own constants, say), write it in an `#i6` island, where a single-hash directive is raw I6 and passes through (§7.4):

```bgl
#i6 { #Ifndef NO_SCORE; print "^"; #Endif; }
```

**Example**

```bgl
#define CHEATS
#define FEATURE_LEVEL 3

#if CHEATS && beguiler >= 1010
    print("cheats on, Beguile 1.1+");
#endif

#if FEATURE_LEVEL > 2
    // FEATURE_LEVEL is 3 or higher
#elif FEATURE_LEVEL == 1
    // exactly 1
#else
    // fallback
#endif

#if TARGET_ZCODE
    // any Z-machine target
#endif

#if #beguilerSettings.target == "z8"
    // Z8 only: the target symbols say which machine, the setting says which version
#endif
```

> ***Note:** The `##if` / `##else` / `##endif` forms provide the same conditional logic inside emitter bodies and are not valid in ordinary Beguile source; see §7.4.*

**See also** §7.4.

## 14.3 Diagnostics and Control

### 14.3.1 `#message`

**Syntax**

```syntax
#message "TEXT"
```

`#message` writes the string to the compiler's standard output during compilation.  It has no effect on the generated output or on the result of compilation.

**Example**

```bgl
#message "Loading custom library..."
```

**See also** §20.5.

### 14.3.2 `#warning`

**Syntax**

```syntax
#warning "TEXT"
```

`#warning` reports a warning with the given text, in the compiler's standard warning format with file name and line number, and compilation continues.

**Example**

```bgl
#if !FEATURE_XYZ
    #warning "FEATURE_XYZ is not defined - some functionality will be disabled."
#endif
```

**See also** §19.3.

### 14.3.3 `#error`

**Syntax**

```syntax
#error "TEXT"
```

`#error` halts compilation with an error carrying the given text, reported in the compiler's standard error format with file name and line number.

As the whole body of an emitter, `#error` makes using that emitter the error, reported at the line that uses it.  An `operator = (nullLiteral)` written this way is how a class refuses `null`:

```bgl
emitter class Tag {
    emitter Tag operator = (nullLiteral v) { #error "A Tag can't be null." }
}
```

**Example**

<!-- doctest: error -->
```bgl
#if !PLATFORM_DEFINED
    #error "You must define PLATFORM_DEFINED before including this file."
#endif
```

**See also** §19.2.

### 14.3.4 `#exit`

**Syntax**

```syntax
#exit
```

`#exit` stops processing the current file immediately, as though its end had been reached.  Any conditional nesting (§14.2.5) still open in that file is discarded.  Open code blocks are not closed, so `#exit` is used only at the top level of a file.

**Example**

```bgl
#once
#if !FEATURE_ENABLED
    #exit
#endif
// … feature implementation follows …
```

**See also** §14.2.5.

## 14.4 Raw I6 Placement

The directives in this section carry a body of raw I6 text between braces.  The body is not parsed as Beguile; only the substitution in §14.4.5 is applied.  All of them share two rules:

- **Deduplication.**  Each source file contributes its blocks at most once, however many times the file is included.  `#once` is not required for this.
- **Ordering.**  Blocks from different files are placed in file-inclusion order (i.e., the order in which the compiler first encounters each file).

### 14.4.1 `#startup`

**Syntax**

```syntax
#startup {
    RAW-I6-STATEMENTS
}
```

`#startup` registers I6 statements to run at program startup.  They will run inside the runtime's `bglInit()` routine, before any global variable initializers; who calls `bglInit()` is covered in §21.2.

**Example**

```bgl
#startup {
    _initializeStringBuffers();
}
```

> ***Note:** This is intended for library code; a program normally uses object initializers or the game's starting routine instead.*

**See also** §18.8, §21.2, §23.3.1.

### 14.4.2 `#emitfirst`

**Syntax**

```syntax
#emitfirst {
    RAW-I6
}
```

`#emitfirst` places the body at the beginning of the generated I6 program, before the runtime and before every declaration, so that it precedes any I6 library included later.  The body can use `##beguilerSettings.KEY` (§14.4.5).

**Example**

```bgl
#emitfirst {
    Replace DrawStatusLine;
    Constant story    = ##beguilerSettings.title;
    Constant headline = ##beguilerSettings.headline;
}
```

**See also** §16.6, §18.6, §23.3.2.

### 14.4.3 `#emitlast`

**Syntax**

```syntax
#emitlast {
    RAW-I6
}
```

`#emitlast` places the body at the very end of the generated I6 program, after every declaration, grammar directive, and object definition.  The body can use `##beguilerSettings.KEY` (§14.4.5).

**Example**

```bgl
#emitlast {
    [ DrawStatusLine;
        ! replacement routine
    ];
}
```

**See also** §18.6.

### 14.4.4 `#storedEmitFirst` and `#storedEmitLast`

**Syntax**

```syntax
#storedEmitFirst NAME {
    RAW-I6
}
#storedEmitLast NAME {
    RAW-I6
}
```

These directives register a named, deferred raw-I6 block.  Unlike `#emitfirst` and `#emitlast`, a stored block is not placed in the output by default:  it is emitted, once, only when a built-in I6 template which needs it is applied (§18.9).  When emitted, the block takes the position of the corresponding non-stored form:  the top of the program for `#storedEmitFirst`, the end for `#storedEmitLast`.

A `#storedEmitFirst` block and a `#storedEmitLast` block can share one name, and one template can need several names.  Registering a second block of the same kind under a name replaces the earlier one; `#once` on the declaring file is the normal guard.  The body can use `##beguilerSettings.KEY` (§14.4.5).

**Example**

```bgl
#storedEmitFirst scratchSupport {
    Array  scratchStack --> 33;
    Global scratchTop = 0;
    [ scratchPush v; … ];
}
```

> ***Note:** These are intended for BLR authors.*

**See also** §18.9, Appendix G.

### 14.4.5 `##beguilerSettings.KEY` Substitution

**Syntax**

```syntax
##beguilerSettings.KEY
```

Inside the raw I6 body of `#emitfirst`, `#emitlast`, `#storedEmitFirst`, or `#storedEmitLast`, this marker is replaced by the compile-time value of the named `#beguilerSettings` property.  A string property becomes an I6 quoted string (`"…"`); an integer property becomes a decimal literal.  The property names are those accepted by the `#beguilerSettings.prop` expression form (§14.7.2, §15.7).  The substitution is not applied in `#startup`, `#i6`, or emitter bodies.

**Example**

```bgl
#emitfirst {
    Constant story = ##beguilerSettings.title;
}
```

**See also** §14.7.2, §15.7, Appendix G.

## 14.5 Islands

Here we give only the syntax of the island directives; islands are defined in §16.1.3, the two compilation modes are covered in §16.1, the semantics of I6 islands in §16.2, those of Beguile islands in §16.3, and loose identifier resolution in §16.3.3.

### 14.5.1 `#i6`

Syntax:  `#i6 RAW-I6-TO-END-OF-LINE` or `#i6 { RAW-I6 }`.  See §16.2.

### 14.5.2 `#bgl`, `#bglDecl`, `#bglStmt`

Syntax:  `#bgl STATEMENT ;`, `#bgl { … }`, `#bglDecl { DECLARATIONS }`, `#bglStmt { STATEMENTS }`.  See §16.3.

## 14.6 Namespace Import

### 14.6.1 `#using`

**Syntax**

```syntax
#using NAME [ .NAME ] … [ ; ]
```

`#using` imports the members of a class or object into the current file's scope so they can be used unqualified.

**See also** §10.4.

## 14.7 Settings

### 14.7.1 `#beguilerSettings { … }`

Syntax:  `#beguilerSettings { PROPERTY = VALUE ; … }`.  See §15.1 and Appendix E.

### 14.7.2 `#beguilerSettings.prop`

Syntax:  `#beguilerSettings.PROPERTY`.  See §15.7.

---

# 15 Settings (`#beguilerSettings`)

## 15.1 The Settings Block

**Syntax**

```syntax
#beguilerSettings {
    [ TYPE ] PROPERTY = VALUE ;
    …
}
```

`VALUE` is an integer literal, a string literal (`"…"` or raw `@"…"`), `true`/`false`, or an enum member written bare (`Z5`) or qualified by its enum (`eTarget.Z5`).

A `#beguilerSettings` block configures the compiler and the Inform 6 invocation which follows.  It can appear only at global scope.  You can write any number of blocks, in the entry file or in any included file, and a block can be empty.

Each entry names one declared property:  one of the compiler's own, in §15.2 to §15.6 (Appendix E lists them all), or one an extension declares (§15.9).  The optional leading `TYPE` is documentation only; it must be a type name or a data-type keyword, and is otherwise ignored.  Naming a property which doesn't exist, giving a value of the wrong type, or qualifying an enum value with the wrong enum name is a compile-time error.

**Precedence.**  Properties follow **first-writer-wins**:  the first block, in parse order, which sets a property fixes it, and later assignments to the same property are ignored.  A value given on the command line counts as written before any block (§17.4).  There are two exceptions:

- `includePaths` is **additive**:  every occurrence appends to the search path (duplicates are dropped).
- `release` and `seriesNumber` treat `0` as "not set", so a block which assigns `0` does not fix them.

**Entry-file properties.**  The following properties are read from the entry source file before parsing begins, by a textual scan of its `#beguilerSettings` blocks:

- `target`
- `includePaths`
- `beguiLibPath`
- `generateBlorb`
- `blorbAssetPath`
- `autoInitialize`
- every `int` property set to an integer literal, including those the library and extensions declare (`heapSize`, `worldBufSize`, `<string>`'s `stringHeapSize`)

These properties must be written in the entry file, as literal values, and not in an included file.

**Example**

```bgl
#beguilerSettings {
    target       = Z5;            // bare enum value
    outputPath   = "build";
    release      = 2;
    serial       = "260921";
    title        = "Cloak of Darkness";
    author       = "Roger Firth";
}
```

**See also** §17.4, §18.4, Appendix E.

## 15.2 Toolchain Paths

These properties locate external tools.

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `informPath` | string | none | Full path to the Inform 6 compiler binary.  Wins over `informName`. |
| `informName` | string | `"inform"` | File name of the Inform 6 binary, looked up next to the `beguiler` binary.  `"none"` skips the Inform 6 hand-off. |
| `beguiLibPath` | string | `"beguiLib"` | Directory of the Beguile system library, replacing the binary-adjacent default for the whole program; `-lib=` wins.  A relative path is relative to the entry file's folder.  Set in the entry file:  a block in another file naming a different library is a compile-time error. |
| `includePaths` | string | none | One or more directories, comma-separated, added to the search path for both `#include "…"` and `#includeI6` (§18.4).  Additive across blocks and with `-includepaths=`. |

A regular `"…"` `includePaths` entry is normalized:  separators are rewritten (§18.5), a relative path is resolved against the directory of the file containing the block, and the result is canonicalized.  Each such entry must name an existing directory, or the block is a compile-time error.  A raw `@"…"` entry is taken verbatim, with no rewriting, resolution, or check.

## 15.3 Compilation Settings

The following properties control the compilation itself:

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `target` | `eTarget` | `Glulx` | `Glulx`, `Z5`, or `Z8`.  Exposed to `#if` as the valueless `TARGET_GLULX` / `TARGET_ZCODE` flags (§14.2.4, Appendix F). |
| `outputPath` | string | `"output"` | Directory for the story file and every intermediate file; relative to the source file's directory (§20.1). |
| `errorFormat` | `eErrorFormat` | `E1` | Inform 6 diagnostic style.  Only `E1` and `E2` are accepted in a block. |
| `release` | int | `0` | Story release number, emitted as an I6 `Release` directive when non-zero. |
| `serial` | string | `""` | Story serial number, emitted as an I6 `Serial` directive when set.  Must be exactly six digits. |
| `omitUnusedRoutines` | bool | `true` | Inform 6 drops routines nothing references (§18.11). |
| `economy` | bool | `false` | Opt-in automatic text abbreviation (§18.11). |
| `filterVerbSplitWarnings` | bool | `true` | Hides Inform 6's "Verb declaration no longer has any verbs associated" warning for a library verb whose only word a Beguile line split off (§13.4.4); `false` shows it. |
| `rewritePaths` | bool | `true` | Rewrite `/` and `\` in every path (settings paths, `#include`, `#includeI6`) to the platform separator (§18.5). |

## 15.4 Runtime Settings

These properties size runtime structures in the generated code.  `framePoolSize` and `worldBufSize` must be at least 1, and `heapSize` must be 0 (the target's default) or at least 64.

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `framePoolSize` | int | sized from the program, at least `64` | Word slots in the frame pool, which backs local `array<char>` buffers on both targets and, on the Z-machine, local-variable overflow (§18.10). |
| `heapSize` | int | `1536` on the Z-machine, `16384` on Glulx | Words in the heap that local arrays and `<linq>` results are allocated from (§12.6).  Must be set in the entry file (§15.1). |
| `worldBufSize` | int | `128` | Objects per scratch buffer for `bgl.world` queries (§21.9); a walk which would exceed it stops there.  Must be set in the entry file (§15.1). |
| `autoInitialize` | bool | `true` | When `true`, the library binding wraps the program's entry point so that `bglInit()` runs first (§21.2, §23.3.1).  Set `false` when another library already replaces `Main`; the program then calls `bglInit()` itself.  A program built on no binding at all always calls it itself, regardless of this setting. |

`<string>` declares two more runtime settings, `stringHeapSize` and `stringCaptureSize` (§22.3).  The string pool used by `<string>` is not a setting:  its size is the I6 constant `bglStringPoolReserve`, declared in an `#i6` island before the extension is included (§22.3).

## 15.5 Game Metadata

These properties describe the work.  They feed the iFiction record which is written into a blorb (§15.6) and can be read from source (§15.7); however, apart from `release`, `serial`, and `ifid`, the compiler does **not** emit them into the story file.

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `title` | string | `""` | Title (iFiction `<title>`).  `"Untitled"` in the blorb record if unset. |
| `author` | string | `""` | Author (iFiction `<author>`).  `"Anonymous"` in the blorb record if unset. |
| `headline` | string | `""` | Subtitle or tagline (iFiction `<headline>`). |
| `genre` | string | `""` | Genre, e.g., `"Mystery"` (iFiction `<genre>`). |
| `description` | string | `""` | Blurb (iFiction `<description>`). |
| `language` | string | `""` | ISO-639 code, e.g., `"en"` (iFiction `<language>`). |
| `series` | string | `""` | Series name (iFiction `<series>`). |
| `seriesNumber` | int | `0` | Position in the series (iFiction `<seriesnumber>`); written only when greater than 0. |
| `firstPublished` | string | `""` | `"YYYY"` or `"YYYY-MM-DD"` (iFiction `<firstpublished>`). |
| `forgiveness` | string | `""` | `"Merciful"`, `"Polite"`, `"Tough"`, `"Nasty"`, or `"Cruel"` (iFiction `<forgiveness>`). |
| `ifid` | string | `""` | IFID in UUID form.  Auto-generated when blorb packaging is on (§15.6.2). |

**What reaches the story file.**  When `ifid` is set (explicitly or by generation), the compiler embeds it in the story file in the Treaty of Babel form which cataloging tools search for:

```i6
Array UUID_ARRAY string "UUID://A0B1C2D3-E4F5-6789-ABCD-EF0123456789//";
```

Everything else is available only through `#beguilerSettings.prop` (§15.7).

## 15.6 Blorb Packaging

Two properties control blorb packaging:

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `generateBlorb` | bool | `false` | Master switch for the asset scan and the blorb build. |
| `blorbAssetPath` | string | `"assets"` | Directory scanned for assets.  Relative to the source file's directory. |

When `generateBlorb` is `true`, two extra stages will run (§18.2):

1. **Before parsing**, the asset directory is scanned and `_blorbAssets.bgl` is written next to the source file.
2. **After Inform 6 succeeds**, the story file and every discovered asset are assembled into a blorb next to the story file:  `STEM.gblorb` for Glulx, `STEM.zblorb` for the Z-machine (§20.2).  The blorb carries an iFiction record built from §15.5.

A missing asset directory is reported, and packaging proceeds with no assets.

### 15.6.1 Asset Discovery and `_blorbAssets.bgl`

The asset directory is scanned **non-recursively**, and files whose name begins with `.` are ignored.  Each remaining file becomes one resource, classified by its extension (case-insensitively):

| Extension | Resource | Enum |
| --- | --- | --- |
| `png`, `jpg`, `jpeg` | picture | `eImages` |
| `aiff`, `aif` | sound | `eSounds` |
| anything else | generic data | `eUnknownAsset` |

Unrecognized files are packaged rather than dropped, and a console note lists them.

The enum member name is the file's stem in camelCase (`-`, `_`, and space are word breaks) followed by the capitalized extension:  `priest.png` gives `priestPng`, and `title-theme.aiff` gives `titleThemeAiff`.  Resource numbers are assigned from 1, pictures first, then sounds, then generic data, alphabetically within each group; the number is the member's value.

The enums `eImages`, `eSounds`, and `eUnknownAsset` are declared empty, along with the union `eAssets = eImages | eSounds | eUnknownAsset` (§21.12).  The generated `_blorbAssets.bgl` extends each of them:

```bgl
// Auto-generated by beguiler blorbifier: do not edit
extend enum eImages {
    priestPng = 1
}
extend enum eSounds {
    titleThemeAiff = 2
}
extend enum eUnknownAsset {
}
```

The file is regenerated on every build with packaging on.  Include it once from your source (`#include "_blorbAssets.bgl"`) so the members are in scope.

`generateBlorb` is also visible to `#if` as a symbol (§14.2.4).

### 15.6.2 IFID Generation and Persistence

When packaging is on and no `ifid` is set, the compiler supplies one:

1. **Deterministic.**  The IFID is a name-based UUID derived from the source file name, `author`, and `title`; the same three inputs will always yield the same IFID.
2. **Persisted.**  It is written to the head of `_blorbAssets.bgl` as `#beguilerSettings { ifid = "…"; }`, from where later builds read it back like any other setting.
3. **Explicit wins.**  An `ifid` in the program's own source takes precedence under first-writer-wins; the persisted value is a fallback.

## 15.7 Reading Settings From Source

**Syntax**

```syntax
#beguilerSettings.PROPERTY
```

In an expression, `#beguilerSettings.property` is replaced at parse time by a literal holding the property's value:  a string literal for string and enum properties (an enum value is its name, e.g., `"z5"`), an integer literal for integer properties, and `true`/`false` for boolean ones.  **Every declared property can be read.**  A name which is not a declared property is a compile-time error.

| Result | Properties |
| --- | --- |
| string | `title`, `author`, `headline`, `genre`, `description`, `language`, `series`, `firstPublished`, `forgiveness`, `ifid`, `target`, `outputPath`, `blorbAssetPath`, `informName`, `informPath`, `beguiLibPath`, `errorFormat`, `serial`, `includePaths` |
| int | `release`, `seriesNumber`, `framePoolSize`, `heapSize`, `worldBufSize` |
| bool | `generateBlorb`, `autoInitialize`, `economy`, `filterVerbSplitWarnings`, `omitUnusedRoutines`, `rewritePaths` |

`includePaths` is a list, which has no literal form, so it reads back as the `;`-joined search path.

A setting an extension declares (§15.9) is read the same way.

The value is whatever has been fixed when the reference is parsed.  A string property not yet set reads as `""` and an integer property as `0`, except for the runtime sizes (§15.4), which read as their defaults.  As such, the block which sets a property must precede, in parse order, any reference to it.

Inside the raw-I6 body of `#emitfirst`, `#emitlast`, `#storedEmitFirst`, and `#storedEmitLast`, the same properties are available as `##beguilerSettings.property` (§14.4.5).

**In a `#if` condition.**  The same reference reads the same value in a compile-time condition (§14.2.5).

**Example**

```bgl
#beguilerSettings { title = "Cloak of Darkness"; release = 3; }

const string story      = #beguilerSettings.title;     // -> "Cloak of Darkness"
const int    gameRelease = #beguilerSettings.release;  // -> 3

#if #beguilerSettings.target == "z8"
    // this build has the larger Z-machine address space
#endif
```

**See also** §14.2.5, §14.4.2, §14.4.5, §15.5.

## 15.8 Bindings and the Library Banner Constants

The IF library bindings declare the library's `story` and `headline` constants from the `title` and `headline` settings; see §23.3.2.

## 15.9 Declaring Settings

An extension can declare int settings of its own by extending the settings type, and reads them the same way (§15.7); unset, one reads as `0`.  A setting's entry can come before the include which declares it:

```bgl
extend class beguilerSettingsType {
    int stringHeapSize;   /// Bytes in the string heap (unset: 5120).
}
#if #beguilerSettings.stringHeapSize > 0
    const int bglStringHeapSize = #beguilerSettings.stringHeapSize;
#else
    const int bglStringHeapSize = 5120;
#endif
```

---

# 16 Inform 6 Interoperability

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

---

# Part II: The Beguiler Compiler

# 17 Invoking the Compiler

## 17.1 Synopsis

```text
beguiler [options] SOURCE [OUTPUT]
beguiler --lsp [-lib=DIR] [-includepaths=DIR[,DIR…]]
beguiler --make-patch ORIGINAL EDITED [ORIGINAL EDITED …] -o PATCH
beguiler --apply-patch PATCH ORIGINAL -o COPY
```

`beguiler` is a single executable which transpiles a Beguile program to Inform 6, hands the result to the Inform 6 compiler, and (optionally) packages the story file into a blorb.  Run with no arguments, it will print a usage summary and exit with status 1.

## 17.2 Positional Arguments

**Source.**  The first argument which doesn't begin with `-` is the source file.  Its extension selects the entry mode (§17.8).

**Output.**  An optional second bare argument is the story file to write, used exactly as written.  When it is omitted, the story file is placed in the output directory (§20.1) as `STEM.EXT`, where `STEM` is the source file name without its extension and `EXT` is chosen by the target:  `ulx` for Glulx, `z5` or `z8` for the Z-machine.

A third bare argument is an error.

## 17.3 Options

Options are recognized only exactly as listed below.  Any other argument beginning with `-` is not an error; it is simply passed through to the Inform 6 command line (§17.3.1).

| Option | Effect | `#beguilerSettings` equivalent |
| --- | --- | --- |
| `-o DIR` | Output directory for the story file and all intermediate files.  `-o` without a following argument is an error. | `outputPath` |
| `-G`, `-g` | Target Glulx. | `target = Glulx` |
| `-z5`, `-Z5` | Target the Z-machine, version 5. | `target = Z5` |
| `-z8`, `-Z8` | Target the Z-machine, version 8. | `target = Z8` |
| `-EN` | Inform 6 error-message format; `N` is a single digit.  `E1` is the Microsoft style, `E2` the Macintosh style. | `errorFormat` |
| `-inform=<name>` | The Inform 6 compiler to run.  An absolute path is used verbatim; anything else is resolved relative to the directory containing the `beguiler` binary.  The name `none` skips the Inform 6 hand-off:  the transpiled `.inf` is written and compilation stops there. | `informName` / `informPath` |
| `-includepaths=DIR[,DIR…]` | Adds one or more directories to the include search path (§18.4).  Comma-separated; whitespace around each entry is trimmed; duplicates are ignored.  Entries are taken as written:  no separator rewriting, no relative-path resolution, no existence check. | `includePaths` |
| `-lib=DIR` | Location of the Beguile system library (the `beguiLib` tree).  An absolute path is used verbatim; a relative path is anchored to the directory containing the `beguiler` binary.  Surrounding `"` or `'` quotes are stripped. | `beguiLibPath` |
| `-filterVerbSplitWarnings=true\|false` | Whether to hide Inform 6's warning about a library verb left with no words by a split (§13.4.4). | `filterVerbSplitWarnings` |
| `--debug`, `-D` | Debug build (§20.3):  Inform 6 is run with `-k` and `-D`, the debug bundle is written, and `DEBUG` is defined. | none |
| `--lsp` | Language-server mode (§17.6).  No compilation takes place. | none |

### 17.3.1 Pass-through switches

Any argument beginning with `-` which is not one of the above options is appended verbatim to the Inform 6 command line, after the compiler's own switches.  This is how you hand Inform 6 a switch Beguile doesn't model, such as `-s` (statistics) or `-~S` (strict mode off).  Nothing is validated; an unknown switch will surface as an Inform 6 error.

The `-EN` option is the one exception to "exactly as listed":  any `-E` followed by a digit is consumed by Beguile rather than passed through.

## 17.4 Precedence Between the Command Line and `#beguilerSettings`

Most options can also be set from the source with a `#beguilerSettings` block (§15.1).  Where both are given, the rule depends on the property:

| Property | Rule |
| --- | --- |
| `target` | Command line wins. |
| `errorFormat` | Command line wins. |
| `outputPath` | Command line (`-o`) wins. |
| `informName` / `informPath` | Command line (`-inform=`) wins over both; between the two block properties, `informPath` wins over `informName` (§17.5). |
| `includePaths` | Additive.  Command-line entries are added first, then block entries in the order they are parsed. |
| `filterVerbSplitWarnings` | Command line wins. |
| `beguiLibPath` | Command line wins.  Otherwise the entry file's block names the library for the whole program, read before any of it (§15.1). |

`--debug`, `--lsp`, and pass-through switches have no settings equivalent; `generateBlorb`, `blorbAssetPath`, `economy`, `omitUnusedRoutines`, `autoInitialize`, `release`, `serial`, the runtime sizes, and the game-metadata properties have no command-line equivalent.

## 17.5 Locating the Toolchain

The compiler never searches the system `PATH`.  By default, both of its dependencies are located relative to the `beguiler` binary.

**The Beguile system library** (`beguiLib`).  This is `-lib=DIR` if given, or else the `beguiLib` subdirectory of the directory containing the binary.  This is where the auto-loaded core files, the built-in templates (§18.9), and every `#include <…>` extension live.

**The Inform 6 compiler** is located by the following, in decreasing order of precedence:

1. `-inform=<name>` on the command line.
2. `informPath = "…"` in `#beguilerSettings`:  a full path to the binary, used verbatim.
3. `informName = "…"` in `#beguilerSettings`:  a file name, resolved relative to the directory containing the `beguiler` binary.
4. The default:  a file named `inform` next to the `beguiler` binary.  If nothing was configured and no such file exists, `../inform6/inform6` relative to the binary is tried as a last resort.

Rules 1 and 3 accept `none`, which skips the Inform 6 stage entirely.

## 17.6 Language-Server Mode

`--lsp` anywhere on the command line switches the binary into Language Server Protocol mode.  It will then read JSON-RPC messages from standard input and write responses to standard output, using the LSP `Content-Length` framing, until its input closes.  No source file is compiled and nothing is written to disk.

In this mode only `-lib=` and `-includepaths=` are honored (as in §17.3); every other argument is ignored.

## 17.7 Exit Status and Console Output

The process exits with status 0 when the requested work completed, and 1 when it did not:  a usage error, a missing source file, a compile-time error (§19.1, §19.2), a failure to write the transpiled file, or a non-zero exit from Inform 6.

What a build prints is covered in §20.5.

## 17.8 Entry Mode

The source file's extension, and nothing else, selects the compilation mode (§16.1).  In precompiler mode the target is read from the file's own `!%` lines (§16.1.2).

## 17.9 Examples

The following are a few typical command lines:

```text
beguiler game.bgl
beguiler -z5 -o build game.bgl
beguiler -G --debug -includepaths=../lib,../shared game.bgl
beguiler -inform=none game.bgl            # transpile only; leaves output/game.bgl.transpiled.inf
beguiler -lib=/opt/beguile/beguiLib -inform=/opt/inform6/inform6 game.bgl
```

## 17.10 Patch Tools

The patch tools make and read the patches which `#mergeI6` applies (§14.1.6).  Each must be the first argument; nothing is compiled, and no banner is printed.

**`--make-patch`** writes one patch of the changes from each *original* to its *edited* copy, as a unified diff with three lines of context, naming each file by the original's file name, after any comment lines at the top of the patch it replaces.  Lines which differ only in trailing whitespace are unchanged.  When nothing changed, it prints `no changes` and writes no file.

**`--apply-patch`** writes *original* with the patch's merge chunks for that file name applied.  A merge chunk which matches nowhere is skipped rather than being an error; the tool prints a JSON object giving the number applied and the text of each skipped merge chunk:

```text
{"applied": 3, "failed": ["@@ -2458 @@ (patch line 19)\n …"]}
```

Both exit with status 0 on success, and with 1 when a file can't be read or written or the patch is malformed.

---

# 18 Compilation Model

## 18.1 Overview

Beguile is a transpiler:  a program is translated to a single Inform 6 source file, which the Inform 6 compiler then turns into a story file.  In this chapter we go over the pipeline as you can observe it.

The same pipeline serves both entry modes (§16.1).  In precompiler mode the Beguile passes operate on the embedded `#bgl` islands and the surrounding I6 passes through (§16.1.2).

## 18.2 Phases

A build runs the following stages in order.  Stages 1 and 8 run only when blorb packaging is enabled (§15.6); stages 6 to 8 run only when the Inform 6 hand-off is not disabled (§17.3, `-inform=none`).

1. **Asset scan.**  The asset directory is scanned and `_blorbAssets.bgl` is (re)written next to the source file.
2. **Pre-scan (pass 1).**  The source and everything it includes are read once to register the names of every global declaration (§18.3).
3. **Parse (pass 2).**  The source is lexed and parsed in full; types are resolved and checked.
4. **Whole-program checks.**  Checks that need the complete program, such as duplicate member detection across `extend` blocks and the hidden-member rules (§8.7.4), run here; unset settings take their defaults.
5. **Emission.**  The transpiled file `SOURCE.transpiled.inf` is written to the output directory (§20.2); in a debug build the debug bundle is written beside it.
6. **Abbreviation pass** (only with `economy = true`, §18.11).
7. **Inform 6.**  The Inform 6 compiler is run on the transpiled file; its diagnostics are rewritten to Beguile locations (§19.1.1).
8. **Blorb assembly.**  The story file and the assets are packaged into a `.gblorb` or `.zblorb`.

The first compile-time error ends the build.

## 18.3 Passes and Forward References

Declarations at global scope can appear in any order.  The pre-scan registers a stub for every global name before the main parse begins, so you can use a name before it is declared, and an included file can refer to declarations in a file included after it.  It registers:

- classes (including `extern`, `alias`, and generic ones) and structs, with their members;
- enums, bnums, and unions, with their members;
- objects, verbs, global variables, and constants, with the members declared in an object body;
- global functions;
- members added by `extend` bodies of objects, whether the object is declared before or after the `extend` (a class or an array must be declared before it is extended);
- `#declare` symbols (§14.2.3).

Conditional directives (`#if`, `#elif`, `#else`, `#endif`) and `#define`/`#undef` are honored during **both** passes, so a declaration excluded by `#if` is not registered either.

Some things are not order-independent, since their value is fixed at the point of parsing:

- `#beguilerSettings.prop` reads the value fixed so far (§15.7).
- The emission-ordering rule for `extern attribute` (§18.7, rule 3) depends on source order.
- A `#using` naming a class or object declared later is accepted but ignored, with a warning (§19.3).

## 18.4 Include Resolution

**Library includes, `#include <name>`.**  The `beguiLib` tree (§17.5) is searched as §14.1.1 describes.

**Relative includes, `#include "path"` and `#includeI6 "name"`.**  The search is described in §14.1.2; its `includePaths` entries are tried in order, command-line entries before block entries.  A `#includeI6` that resolves is emitted with its full absolute path, so Inform 6 never searches for it; the raw form `#includeI6 @"…"` bypasses resolution and is emitted verbatim.  A file that a patch changes is read from its patched copy (§14.1.6).

**Current-file-relative I6 includes.**  An `Include ">name"` in the transpiled file still resolves against the source directory, so your `#i6 { Include ">lib.h"; }` will behave just as it would in a hand-written `.inf` beside the source.

**Case-insensitive matching.**  Every component of an include path, directory or file, matches case-insensitively.  An exact-case match is preferred; if two entries in one directory differ only in case, which one is chosen is unspecified.

**Nesting.**  Include nesting is limited to 255 levels.  Exceeding the limit (e.g., through circular includes of files without `#once`, §14.1.7) is a compile-time error.

## 18.5 Path Resolution

Every path written in Beguile source (include paths and the path-valued settings) is normalized when parsed:

- **Separator rewriting.**  Each `/` and `\` becomes the platform separator, so a path written with either separator will work on any platform.  `rewritePaths = false` disables this for the program.
- **Relative anchoring.**  `includePaths` and `blorbAssetPath` are relative to the directory of the file which sets them; `outputPath` is relative to the entry source file's directory (§20.1).

## 18.6 Layout of the Generated File

The transpiled `.inf` file is laid out in the following order, regardless of the order of the source.  Items marked *(when used)* are absent from programs that don't need them.

1. The ICL header:  `!% -G` / `!% -v5` / `!% -v8`, `!% -EN`, `!% $OMIT_UNUSED_ROUTINES=1`, and one `!% ++include_path=` line per `includePaths` entry.  In precompiler mode the entry file's own `!%` lines appear here instead.
2. The `beguiler` version constant, then `Serial` and `Release` when set, the runtime size constants *(when used)*, and the `UUID_ARRAY` IFID string *(when set)*.
3. Compiler scratch globals *(when used)*.
4. The frame pool *(when used, §18.10)* and Z-machine excess-parameter globals *(when used, §6.3)*.
5. Every `#emitfirst` block, in file-inclusion order, each file's block at most once; then the fired `#storedEmitFirst` blocks *(when triggered, §18.9)*.
6. The `bglInit` routine (§18.8).
7. The program's declarations in source order, subject to the ordering rules of §18.7:  classes, objects, globals, arrays, functions, verbs and grammar, and the contents of `#i6` islands.
8. Static class routines, then any grammar evictions.
9. Every `#emitlast` block, in file-inclusion order; then the fired `#storedEmitLast` blocks *(when triggered)*.
10. Referenced `superposed` declarations, appended in the order they were observed (§18.9).
11. In precompiler mode, the entry file's trailing `end;` and anything after it.  It comes after the `superposed` declarations since Inform 6 stops reading at `end;`.

## 18.7 Emission Ordering

Inform 6 requires a class to be defined before any instance of it or class derived from it, and an attribute to be declared before any `has` clause that names it.  The emitter keeps source order, except where the following three rules require otherwise:

1. **Lazy class emission.**  A class is normally emitted at its source position.  If an earlier declaration is an instance of it (`ClassName var;`, or an object of that class), the class is emitted immediately before that first instance instead.
2. **Base before derived.**  Before a class is emitted, at its own position or by rule 1, its base classes are emitted first, transitively.
3. **`extern attribute` as a gate.**  A class whose attribute list names an `extern attribute` can't be emitted before the `extern attribute` declaration has been passed in source order.  That declaration stands in for the `#includeI6` which defines the attribute in I6, so a bindings file must be included before any class, or first instance of a class, that uses one of its attributes.  Violating this is a compile-time error (§19.2).

Member types, method bodies, and value references impose no ordering constraint:  a class can declare a member of a class declared later, and a routine can call a routine or name an object declared later.

## 18.8 `bglInit()`

Every program contains a routine `bglInit`, emitted at position 6 of the generated file (§18.6), before the program's own declarations.  What it does, in what order, and who calls it are covered in §21.2.

## 18.9 Pay-Only-If-Used Emission

Three mechanisms let the library declare code and data which cost nothing in a program which doesn't use them.  The `superposed` qualifier itself is covered in §3.12.

**`superposed` declarations.**  A `superposed` routine, global, array, object, or class is withheld from the transpiled file.  After everything else has been emitted, the output is searched for the declaration's name as a whole word, case-insensitively; if it occurs, the declaration is **appended at the end** of the file, and the search repeats until nothing more is pulled in.  A `superposed` class which is used in a declaration position (a static instance or a subclass) is instead emitted in place ahead of that use, with its `superposed` bases first, since an I6 `Class` must precede its instances and subclasses.  A `superposed` declaration that nothing references appears neither in the transpiled file nor in the debug bundle (§20.3).

Since the test is textual, a name which also occurs in a string literal or comment will materialize its declaration (a harmless dead entry); a genuinely referenced declaration is never dropped.

**Stored emit blocks.**  `#storedEmitFirst name` and `#storedEmitLast name` (§14.4.4) register a raw-I6 block which is emitted, at the position of the corresponding unstored form, only when a built-in template that needs it is applied.  Each block is emitted at most once.

**Built-in templates.**  A few constructs have no fixed I6 form until the compiler knows how the program uses them:  the frame pool is sized to the program, and the several `for … in` forms each expand to a different loop.  The compiler holds these as built-in I6 templates, which are part of the system library but are not Beguile source and are never `#include`d; you don't write or edit them.  A template names the stored blocks it needs, so that a helper is emitted only in programs that use the construct.

## 18.10 The Frame Pool

The Z-machine gives a routine 15 local slots for parameters and locals together; one slot is reserved for the frame pointer, so a routine needing more than 14 has its excess locals spilled into a global **frame pool**.  The mechanism is recursion-safe and invisible to you.

The pool is also the backing store for local `array<char>` buffers on both targets, and it records each routine's heap arrays (§12.6).  It holds the saved fields of a routine's struct locals and parameters while that routine is called again before it returns, so each call of a recursive routine takes words from it.

The pool is emitted only when some routine needs it.  Its size in words is `framePoolSize` (§15.4) when the program sets it; otherwise, the compiler sizes it from the program, as the largest routine frame times a nominal depth of 4, and never below 64.  When `framePoolSize` is set, a routine which calls itself and holds a local array will draw a warning if four of its calls wouldn't fit.  A program that exhausts the pool halts with a runtime error (§19.4).

> ***[Glulx]:** Glulx has no practical local-variable limit; spilling never occurs, and the pool is emitted only for local arrays.*

## 18.11 Dead Code and Economy

Three independent controls reduce the size of the story file.

**`superposed`** (§18.9) withholds a declaration from the transpiled file unless it is referenced.

**`omitUnusedRoutines`** (default `true`) emits `!% $OMIT_UNUSED_ROUTINES=1`, asking Inform 6 to drop every routine which is emitted but never referenced; a routine whose address is taken is kept.  Set it to `false` for a debug build in which every routine should be present.

**`economy`** (default `false`) enables automatic text abbreviation.  After the transpiled file is written, the compiler runs Inform 6 once with `-u` to compute the optimal `Abbreviate` set, inserts those directives at the top of the transpiled file, and compiles with `-e`.  If the program already contains any `Abbreviate` directive of its own, the pass is skipped and a console note says so.  The pass is best-effort:  if the pre-pass fails, the build proceeds without abbreviations.

## 18.12 Z-machine Capacity

A Z-machine story shares fixed tables between the program, the runtime library, and the IF library:

| Resource | Z-machine capacity | What uses it |
| --- | --- | --- |
| Global variables | 233 | Library globals, the runtime's, a program's globals, and compiler temporaries such as lambda captures |
| Attributes | 48 | Library attributes and a program's `attribute` declarations |
| Common properties | 61 | Library properties and a program's `property` declarations |
| Readable memory | 64K | Objects (every slot of a pooled class copies all its properties), arrays, the `<string>` pool's buffers |
| Entries in one array | 32,767 | One `array<T>`, its capacity (the length and marker are a separate two-word array before it) |

The compiler itself reports an array declared larger than the limit.  The others depend on what the library declares, so Inform 6 finds them; the compiler then reports which capacity ran out, in its own words instead of Inform 6's.  Glulx has none of these limits.

---

# 19 Diagnostics

## 19.1 Message Format

Every diagnostic which the compiler produces about the source is a single line, in one of the following forms:

```text
FILE:LINE:COLUMN: ERROR: MESSAGE
FILE:LINE:COLUMN: warning: MESSAGE
```

`FILE` is the path of the Beguile source file containing the construct, as the compiler resolved it.  When a diagnostic is anchored to a statement, the column is `1`; otherwise, it is the position at which the lexer stopped.  Diagnostics are written to standard error.

**Errors stop the build.**  The first error ends compilation with exit status 1 (§17.7), so you will never see more than one per build.

**Warnings continue.**  A warning is advisory, and the build proceeds.  Warnings whose location lies inside the system library (the `beguiLib` tree) are not shown.

**Location-free messages.**  A few checks run after parsing, when no source position is current; their messages carry no `file:line:column` prefix.

### 19.1.1 Inform 6 Diagnostics

Inform 6's own output is passed through the console, with each diagnostic rewritten to Beguile conventions:

```text
FILE.transpiled.inf:LINE:1: ERROR: MESSAGE
  ↳ FILE.bgl:LINE:1
```

The first line names the transpiled file.  The indented second line names the Beguile source line from which that I6 line was generated, and is omitted when no Beguile line corresponds (e.g., in a compiler-generated header line).  Inform 6's "Fatal error" and "Error" both appear as `ERROR`, and "Warning" appears as `warning`.  Inform 6 warnings which map into the system library, or which name a `_bgl`-prefixed symbol, are hidden, and Inform 6's closing "Compiled with N warnings" count is adjusted to the number actually shown.  Any Inform 6 error fails the build, even when Inform 6 itself exits successfully.

## 19.2 Compile-Time Errors

The categories below are the ones you are most likely to meet.  The messages are quoted as representatives; `'x'` stands for the offending name.

**Global name collisions.**  A global name which is not unique (§3.1) is an error, and the message always cites the original declaration:

```text
'score' is already defined (originally declared at myLibrary.bgl:17)
'score' is already defined as a type (originally declared at myLibrary.bgl:17)
```

Collisions between Beguile names and symbols defined only in raw I6 (`#i6`, `#includeI6`) are not visible to Beguile, and will surface as Inform 6 errors instead; declare such symbols `extern` (§16.4) to bring them into the Beguile namespace.

**Member collisions.**  A member declared twice in one class or object, or added by `extend` when a member of that name exists, is an error unless `replace` is used (§8.7):

```text
class 'Room': member 'light' is already defined
extend class 'Room': member 'describe' is already defined; use 'replace' to override
```

**Ambiguous names.**  A bare identifier which could refer to more than one declaration (for instance, members of two `#using` imports), or an operator reference with more than one candidate:

```text
'val' is ambiguous: matches #using-imported member 'libA.val' and #using-imported member 'libB.val'. Qualify the use explicitly to disambiguate.
```

**Inheritance and emission order** (§8.6, §18.7):

```text
class 'A': circular inheritance; 'B' transitively inherits from 'A'
class 'Room' lists attribute 'light' (`attributes = {light}`), but its declaration `extern attribute light;` comes later in source (needed by: …). Declare the attribute, or include the bindings file that declares it, before the class.
```

**Includes and directives** (§14):

```text
#include: file 'x' not found
Maximum include nesting depth (255) exceeded while including 'x'
'#define x' redefines a symbol that is already defined; use '#redef x' to intentionally redefine it
```

**Settings** (§15.1):

```text
Unknown beguilerSettings property 'x'
beguilerSettings property 'x' expects an int, got 'y'
beguilerSettings property 'serial' must be exactly 6 digits (e.g., "250328")
beguilerSettings property 'framePoolSize' must be at least 1
beguilerSettings includePaths entry 'x' does not resolve to an existing directory ('…'). Use @"..." to emit a literal path without filesystem validation.
#beguilerSettings.x: unknown or unsupported property
```

**Qualifier misuse.**  Combining qualifiers which exclude one another, or applying one where it has no effect, is an error; for example, `superposed` on an `extern`, `emitter`, or `alias` class, or on `extend class` (§3.2, §3.12).

**Post-emission check.**  After the transpiled file is written, the compiler verifies that no property-class member (§9.9) was emitted as a raw property access.  A failure names the transpiled file and line, and suggests reading the value into a local first.

## 19.3 Warnings

The compiler's warnings fall into the following categories.

**`#using`** (§10.4).  The directive is ignored, with a warning, when its target is not a declared class or object, when a named member doesn't exist, or when the member is not of an importable type:

```text
#using 'myLib': not a declared class or object; directive ignored
#using 'myLib.x': member not found; directive ignored
```

**Shadowing.**  A local variable, parameter, or loop variable which hides a global or a capturable name from an enclosing function, or a local or loop variable which hides a member of the enclosing class or object (a parameter can share a member's name):

```text
Local variable 'score' shadows the global variable 'score'; '::score' reaches it.
Local variable 'name' shadows a member of class 'Thing'.
```

**Object-member ambiguity.**  Inside an object body, a bare identifier which resolves at file scope but is also a property of the enclosing object (§3.8):

```text
Bare 'light' resolves as global variable 'light', but 'light' is also a property of the enclosing object. Beguile cannot tell which you mean. Write 'self.light' for the object's property, or '::light' to force the global and silence this warning.
```

**Method overriding without `replace`** (§8.7.3):

```text
class 'Door': method 'describe' shadows definition in base class 'Thing'; use 'replace' to suppress this warning
replace: no existing global function 'x' found; treating as new definition
```

**String comparison** (§2.2).  `==` or `!=` between two `string`s, which compares their addresses:

```text
'==' between two strings compares their addresses, not their text.
```

**Verbs and grammar** (§13.2.3, §13.5.2).  Pattern tokens after the trigger word in an `extern verb`'s grammar are ignored; a `grammar -=` which matches nothing removes nothing:

```text
extern verb 'PutOn': pattern tokens after the trigger word(s) in `grammar = {...}` are ignored; …
grammar -= on 'Take': no grammar for word `'grab'` exists, so nothing was removed. …
```

**Precompiler mode.**  A routine, global, constant, object, or other symbol declared in the surrounding I6 whose name is also a Beguile global:

```text
FILE: warning: I6 routine 'Score' collides with Beguile-declared global of the same name
```

**Unreachable `bglInit()`** (§21.2).  The program has runtime initialization to do (a sized byte array to stamp, a `#startup` block, or a deferred global initializer) and nothing in the transpiled file calls `bglInit()`.

```text
WARNING: nothing calls bglInit(), so this program starts with the BLR uninitialized.
```

**Other.**  `hide` naming a member which is not inherited; re-typing a `typesealed` member (the retype is ignored); `superposed` on a non-`static` method (no effect).

## 19.4 Runtime Failures

The generated code and the BLR detect a small number of conditions at run time.  Each prints a bracketed message and halts the story with `quit`, except where stated.

| Condition | Message | Recovery |
| --- | --- | --- |
| Frame pool full (§18.10) | `[Beguile runtime error: frame pool exhausted]` | Raise `framePoolSize`. |
| The heap is full (§12.6); the program stops | `[Beguile run-time error: the heap is full; raise heapSize in #beguilerSettings (it is N words)]` | Raise `heapSize`, or free arrays sooner with `bgl.gc.free` (§21.15). |
| The string heap is full (§22.3); the text is cut short; debug builds only | `[Beguile run-time warning: the string heap is full, so text was cut short; raise the stringHeapSize setting]` | Raise `stringHeapSize`; the `heap` debug verb (§20.3) reports the most a run needed. |
| The string heap can't hold the string pool at startup, or has no room for a Z-machine capture (§22.3); the program stops | `[Beguile run-time error: the string heap is full; raise the stringHeapSize setting]` | Raise `stringHeapSize`. |
| Text printed into a string runs past its room (§22.3); the text is cut short; debug builds only | `[Beguile run-time warning: N characters were printed into room for M, so the text was cut short; raise …]` | Raise the setting it names:  `stringCaptureSize` with `<string>`, `bglStringDefaultSize` for a capture into an untracked buffer. |
| The same on the Z-machine, where the text has overwritten memory in use (§22.3); the program stops | `[Beguile run-time error: N characters were printed into room for M; raise …]` | Raise the setting it names. |
| `bgl.gc.free` given an array already freed (§21.15); the program continues | `[Beguile run-time error: bgl.gc.free(): the array was already freed]` | - |
| `bgl.gc.free` given a file-scope or member array (§21.15); the program continues | `[Beguile run-time error: bgl.gc.free(): the array isn't on the heap (…)]` | - |
| Copying into a `ref array` bound to nothing (§12.5); the program continues | `[Beguile run-time error: copying into an array that is null; bind it with := first]` | Bind it with `:=`. |
| Copying into an array that was freed (§21.15); the program continues | `[Beguile run-time error: copying into an array that was freed]` | - |
| An array index out of range (§12.3); the program continues | `[Beguile run-time error: array index I is outside the N elements it holds; the read is skipped]` | - |
| Every instance of a struct in use (§12.10.1); the program stops | `[Beguile run-time error: all N instances of S are in use: …]` | Declare more slots, or let arrays go sooner. |
| Adding a struct to an array that shares its elements (§12.10.1); the program continues | `[Beguile run-time error: adding a struct to an array that shares its elements; …]` | Declare the array with a capacity. |
| `first()` or `last()` of an empty array of structs (§22.5); the program stops | `[Beguile run-time error: first() of an empty array of values; check count() first]` | Check `count()` first. |
| An array `length` assigned beyond the word range (§12.3) | `[Beguile runtime error: array length exceeds signed range (max 32767 on Z, 2^31-1 on Glulx)]` | - |
| `<string>` pool full (§22.3); the program stops | `[Beguile runtime error: the <string> pool is out of strings; raise bglStringPoolReserve]` | Raise `bglStringPoolReserve`. |
| `<buf>` registry full (§22.2) | `[Beguile runtime: tracked buffer registry full; raise bglTrackedBufferReserve]` | Raise `bglTrackedBufferReserve`. |
| Pooled class full (§8.2.6) | none:  `new` returns `nothing` | Test the result of `new`, or size the pool larger. |
| `bgl.world` result buffer full (§21.9) | none:  the walk stops at `worldBufSize` objects | Narrow the query, or raise `worldBufSize` (§15.4). |
| `throw` with no enclosing `try` (§5.16); the program stops | `[Unhandled exception]` | Throw only where a `try` is on the call stack. |

---

# 20 Build Outputs and Debugging

## 20.1 Output Directory

Every file which a build produces, other than `_blorbAssets.bgl`, is written to a single **output directory**.  The directory is named by `-o` on the command line or, failing that, by `outputPath` in `#beguilerSettings`; by default it is `output`.  A relative name is resolved against the directory of the entry source file, never against the working directory, and the directory will be created if it doesn't exist.  An explicit output-file argument on the command line (§17.2) is the one exception:  it is written exactly where named.

## 20.2 Files Produced

For an entry file `game.bgl` targeting Glulx, a full build with blorb packaging and `--debug` will leave the following:

| File | Location | Produced |
| --- | --- | --- |
| `game.bgl.transpiled.inf` | output directory | always:  the generated Inform 6 source, retained after the build |
| `game.ulx` | output directory | on Inform 6 success (`game.z5` / `game.z8` for the Z-machine targets) |
| `game.gblorb` | output directory | with `generateBlorb` (`game.zblorb` for the Z-machine) |
| `_blorbAssets.bgl` | beside `game.bgl` | with `generateBlorb`, before parsing (§15.6) |
| `game.bgl.bgldbg` | output directory | with `--debug` (§20.3) |
| `game.bgl.transpiled.inf.dbg` | output directory | with `--debug`:  Inform 6's debug file (§20.4) |
| `i6patched/` | output directory | with `#mergeI6`:  the patched I6 files (§14.1.6) |

The transpiled file is always `SOURCE-FILE-NAME.transpiled.inf`, including the source extension, so a precompiler-mode `story.inf` yields `story.inf.transpiled.inf`.

## 20.3 Debug Builds

`--debug` (§17.3) changes three things:

- Inform 6 is run with `-k`, so it writes its own debug file, and the compiler writes its **debug bundle**, `SOURCE-FILE-NAME.bgldbg`, beside the transpiled file.
- Inform 6 is run with `-D`, so the library's debugging features are present:  its debugging verbs (`tree`, `purloin`, `actions`, …) and action tracing.
- The symbol `DEBUG` is defined (Appendix F), which turns on `bgl.output.log()` (§21.4).  The library bindings declare their debugging verbs only under it, so a program which names one outside `#if DEBUG` will fail to compile in an ordinary build.

A debug build of a program with a library binding also has a `heap` verb.  It prints the heap's size, the words in use, and the most ever in use, for setting `heapSize` (§12.6, §15.4), and reports the string heap of `<string>` the same way (§22.3):  its size, the bytes in use, and the most the run has needed.  A `stringHeapSize` set to that figure will hold the same run.

Everything else about the build is unchanged; `omitUnusedRoutines`, for one, still applies unless the program sets it to `false` (§18.11).

The bundle is plain text in three sections, each introduced by a bracketed header:

| Section | Contents |
| --- | --- |
| `[map]` | One line per generated I6 line that came from Beguile source:  the I6 line number, the Beguile file, and the Beguile line. |
| `[sym]` | The program's globals, objects (with their properties), functions, and extern objects, each with its I6 identifier and kind.  A `superposed` declaration which was never referenced is omitted. |
| `[types]` | Type information:  each enum with its values; each class and each object with its own properties, giving every property's Beguile type; and the declared type of every routine local and global. |

The bundle describes only what the compiler knows; VM addresses come from the Inform 6 file.

## 20.4 The Inform 6 Debug File

With `-k`, Inform 6 writes `gameinfo.dbg`, an XML database which maps VM addresses to I6 source lines and describes routines, their local-variable frames, globals, and properties.  The compiler moves it into the output directory as `SOURCE-FILE-NAME.transpiled.inf.dbg`.  Its format is Inform 6's own and is not covered here.

## 20.5 Console Output

A build reports the following, in order:  the compiler banner, naming the compiler version and build date, always the first line; blorb scan and IFID notes when packaging is on; any compile-time diagnostics (§19.1); on a successful transpile, the transpile confirmation and the exact Inform 6 command line which is about to run; Inform 6's own output, with its diagnostics rewritten to point at Beguile source lines (§19.1.1); and blorb assembly notes.  When the Inform 6 stage is skipped (`-inform=none`, §17.3), a message says so.  Inform 6's command line shows the switches which were passed through unchanged (§17.3.1), the `-k` and `-D` added by `--debug`, and the `-e` added by `economy`.

---

# Part III: The Beguile Language Runtime (BLR)

# 21 Runtime Core

## 21.1 Overview

The Beguile Language Runtime (BLR) is the library of Beguile source every program compiles against.  Its *core*, the subject of this chapter, is loaded automatically, with no `#include`, in both default and precompiler mode (§16.1).  Language extensions, each enabled with `#include <…>`, are covered in §22;  IF library bindings are covered in §23.

The core provides:

- the `bgl` namespace and its target-specific branches (§21.3);
- the output routines in `bgl.output` and the article helpers (§21.4);
- the IF-domain types:  `attribute`, `property`, `dictionaryWord`, `verb`, the grammar types, `bglClass`, `parentProp`, `childrenProp`, `_bglObject`, `_bglPrimitive`, `eType` (§21.5);
- the numeric, character, and allocation utilities:  `uint`, `bgl.util.math`, `bgl.util.random`, the `char` methods, `bglAllocated` (§21.6 to §21.8);
- object-tree queries, `bgl.world` (§21.9);
- the user-interface roots `bgl.ui.mainWin` / `bgl.ui.statusBar` and the print rules (§21.10 to §21.11);
- the utility types `bglSize` and the blorb asset enums (§21.12).

The built-in types (`int`, `uint`, `bool`, `char`, `float`, `string`, §2.2), the rest of the primitive types (§2.10.3), the `var` type (§2.6), and their literals are also core, as is the `object` class (§11.1).

In precompiler mode the `bgl` namespace is not imported:  a `#bgl` island must declare `#using bgl;` before using `bgl.…` (§16.1.2).

## 21.2 `bglInit()`

**Syntax**

```syntax
bglInit();
```

The compiler synthesizes `bglInit()` for every program (§18.8).  It will run at most once, regardless of how often it is called, working through the following in order:

1. the length headers of sized byte arrays (§12.4, §22.2);
2. every `#startup` block in the program, in file-inclusion order (§14.4.1);
3. the deferred initializers of globals, in declaration order (§3.6):  for a class-typed global whose type declares a parameterless `init` (§8.5), the `init` body and then the `operator =` which applies a declared value; for any global whose initializer is not a constant expression, that initializer.

With an IF library binding, you don't call `bglInit()` yourself:  the binding runs it first (§23.3.1).  A program built without a binding, or with `autoInitialize = false` (§15.4), must call `bglInit()` from its entry point before using anything which depends on it.  The call is always available, and harmless when nothing has registered work.

Leaving the call out stops neither the build nor the program, which simply runs on uninitialized data; the compiler will warn you when the program has initialization to do and nothing in the transpiled file calls `bglInit()` (§19.3).

An extension which needs `bglInit()` says so in its entry in §22.

**Example**

```bgl
void Main() {
    bglInit();
    // …
}
```

**See also** §14.4.1, §16.1.2, §18.8, §23.3.1.

## 21.3 The `bgl` Namespace

`bgl` is the root namespace object of the runtime.  Its members are reached by dotted path (`bgl.asm.add(a, b)`, `bgl.util.math.pow(2, 8)`); alternatively, `#using bgl.glulx;` and the other `#using` forms import a branch so its members can be reached bare (§10.4).

| Path | Contents | Target |
|---|---|---|
| `bgl.wordsize` | The word size of the active target:  `2` on the Z-machine, `4` on Glulx. | both |
| `bgl.asm.*` | Direct opcode emitters for the active virtual machine; the set of members differs per target (§21.13). | both |
| `bgl.glulx.*` | Glulx-specific names:  the short aliases `eStyleType`, `eWinType`, `eImgAlign`, `eImgDimension`, `bWinBorder`, `bWinPlacement`, and `bWinScale`, for the core enums `eGlulxStyleType`, `eGlulxWindowType`, `eGlulxImageAlign`, `eGlulxImageDimension`, `bGlulxWindowBorder`, `bGlulxWindowPlacement`, and `bGlulxWindowScale` (§22.7.10); the window types and `color` when `<glulxWindow>` is included (§22.7). | Glulx |
| `bgl.zcode.*` | Z-machine-specific names.  Currently empty. | Z-machine |
| `bgl.util.*` | Utilities:  `math` (§21.6.2), `random` (§21.6.3), and `buf` when `<buf>` is included (§22.2). | both |
| `bgl.world.*` | Object-tree queries (§21.9). | both |
| `bgl.ui.*` | `mainWin`, `statusBar` (§21.10); `screen` with `<glulxWindow>` (§22.7.2); `hideCursor`, `showCursor`, `waitForKey` with `<ui>` (§22.6). | both |
| `bgl.printRules.*` | Text-style print rules for interpolated strings (§21.11). | both |
| `bgl.header.*` | The release number and serial code from the story file header (§21.14). | both |

Only the branch for the active target is loaded, so referencing a member of the other target's branch is a compile-time error.

**Example**

```bgl
int bits = bgl.wordsize * 8;             // 16 on Z-machine, 32 on Glulx
int roll = bgl.util.random.get(6);       // 1..6
```

**See also** §10, §10.4, §22.

## 21.4 `bgl.output`: `print()`, `printLine()`, `log()` and Article Helpers

**Syntax**

```syntax
print( VALUE [ , VALUE … ] ) ;
print( $"TEXT {EXPR} TEXT" ) ;
printLine( [ VALUE ] ) ;
bgl.output.spaces( N ) ;
bgl.output.log( VALUE ) ;
printName( OBJ ) ;
bgl.printRules.a( OBJ ) ;   bgl.printRules.cA( OBJ ) ;
bgl.printRules.the( OBJ ) ; bgl.printRules.cThe( OBJ ) ;
```

The output routines are emitters on `bgl.output`.  `print`, `printLine`, and `printName` are also global names, through aliases (§10.5), so they need no path; `spaces` and `log` do.

The global `print` is a distributor (§6.8):  given several arguments, it prints each in turn, using the overload for that argument's type.

`print()` writes a value to the current output stream immediately, and is overloaded on the argument's type, with an overload for every primitive type and the IF-domain types.  Extensions and bindings add or replace overloads for the types they introduce, using `extend _bglOutput`; your program can also declare a global `print(T)` of its own, which joins the same overload set.  An interpolated string (§1.6.5) prints each segment with the overload for that segment's type.

Given a value whose type derives from `_bglObject` (§21.5.8), `print(obj)` calls the value's own `print()` method if it defines one; otherwise, it prints the object's name with I6's `(name)` rule.

The name `(name)` prints is the object's `instanceName` (§11.5.4).  Both library bindings declare `short_name` on `object` (§23.3.11):  the text (or routine) which the library prints in its place when the object has one, and what the article helpers and `printName()` print.

`printLine(v)` prints `v` exactly as `print(v)` would, and then a newline; `printLine()` prints just the newline.  Unlike `print`, `printLine` is not a distributor (§6.8):  it takes one argument.  Note that it is not I6's `print_ret`:  it does not return from the enclosing function.

`bgl.output.spaces(n)` prints `n` spaces, or nothing when `n` is 0 or less.

`bgl.output.log()` accepts the same arguments as `print()`; however, it produces output only when the symbol `DEBUG` is defined:  in a debug build (§20.3), or when the program turns it on with `#redef DEBUG` (§14.2.2).  Its arguments are parsed and type-checked in every build.

The article helpers `a`, `cA`, `the`, and `cThe` print a world-tree object with an article.  They are members of `bgl.printRules` (§21.11), so you either write them qualified or bring in the short spelling with `#using bgl.printRules;` (§10.4).  `printName(obj)` gives "lamp", the bare short name with no article.

> ***Note:** An Inform 6 program can freely name a variable or object `the`, and a log is a likely object in a game, so these names stay out of file scope.*

**Example**

```bgl
object lamp { short_name = "brass lamp"; }

void Main() {
    print($"You see {bgl.printRules.a(lamp)}.");   // -> You see a brass lamp.
    bgl.printRules.cThe(lamp); print(" glows.");   // -> The brass lamp glows.
    printLine($"Weight: {2}");                     // -> Weight: 2, then a newline
    bgl.output.log("reached Main");                // output only in a debug build
}
```

> ***Note:** The article forms depend on the active IF library's article and naming conventions (e.g., the `proper` attribute).*

**See also** §1.6.5, §14.2.1, §22.3.

## 21.5 IF-Domain Types

### 21.5.1 `attribute` and `attributeList`

**Syntax**

```syntax
attribute NAME ;
extern attribute NAME ;
attributeList attributes = { ATTR [ , !ATTR ] … } ;
OBJ.give( ATTR )   OBJ.ungive( ATTR )   OBJ.has( ATTR )   OBJ.hasnt( ATTR )
```

The `attributeList` form appears in a class or object body; the `!` is literal.

`attribute` is the type of a single Inform 6 attribute (`light`, `container`, `static`, …).  A library binding usually binds them with `extern attribute` (§23.3.4); however, a program can also declare its own (§11.6).

`attributeList` is the type of a class's or object's `attributes` member:  the set of attributes the object initially has, written as an initializer list; its declaration rules are in §11.5.3.

At runtime, you change an object's attributes with `give()` and `ungive()` and test them with `has()` and `hasnt()`; all four are defined on `object` and on `attributeList`.

`NO_ATTRIBUTE` is an `attribute`-typed constant meaning "no attribute".  It is the only value, other than a real attribute, that can be assigned to an attribute-valued member; a bare `0` is a type error.

**Example**

```bgl
class lampPost : object {
    attributeList attributes = { light, static };
}
object brokenPost : lampPost {
    attributes = { !light };          // a lampPost, but not lit
}

void Main() {
    if (brokenPost.hasnt(light)) brokenPost.give(light);
}
```

**See also** §11.5.3, §11.6, §23.3.4.

### 21.5.2 `property`

**Syntax**

```syntax
property NAME ;
extern property NAME ;
[ extern ] additive property NAME ;
OBJ.provides( NAME )
```

`property` is the type of a free-standing Inform 6 property name:  a name in the global property table which is not a member of any Beguile class.  When a name needs a `property` declaration is covered in §11.7.1.

A `property` value is the property identifier, one word in size.  It supports `=`, `==`, and `!=` only; arithmetic on a property identifier is a compile-time error.

The built-in `name` property and the other additive properties are covered in §11.7.2.

**Example**

```bgl
extern property door_to;      // defined by an I6 library
property visited;             // no Beguile class owns it

object cellar { }

void Main() {
    if (cellar.provides(door_to)) print("leads somewhere");
    property p = visited;     // a property-typed value
    if (p == visited) print("same identifier");
}
```

**See also** §11.7, §23.3.5.

### 21.5.3 `dictionaryWord`

**Syntax**

```syntax
dictionaryWord NAME = .WORD ;
dictionaryWord NAME = ..WORD ;
```

`dictionaryWord` is the type of an Inform 6 dictionary word.  Literals are written `.word` (singular) or `..word` (plural); the literal syntax is covered in §1.6.7, and the use of dictionary words in grammar in §13.4.2.

`print()` on a dictionary word prints the word's text.  Dictionary words compare with `==` and `!=`.

**Example**

```bgl
object sword { rawArray<dictionaryWord> name = { .blade, .sword }; }

void Main() {
    print(sword.name[1]);      // -> sword
}
```

**See also** §1.6.7, §13.1.

### 21.5.4 `verb`

`verb` is the class from which verbs are declared; its members and the whole verb model are covered in §13.2.

### 21.5.5 Grammar Types

The types which stand behind a grammar declaration, `patternElement`, `grammarToken`, `grammarRule`, and `grammarRuleList`, are covered in §13.4.1.  You will rarely name them; the grammar declaration syntax and its rules are in §13.4.

**Example**

```bgl
#include <bindings/i6StandardLibrary>

grammar extraLines {
    grammarRule hang = { PutOn, {.hang, HELD, .on, NOUN} };   // a verb and a pattern of pattern elements
}
```

### 21.5.6 `bglClass`

**Syntax**

```syntax
OBJ.is( CLASS )
```

`bglClass` is the parameter type of `is()` on `_bglObject` (§21.5.8), the runtime class test (true when `obj` is an instance of the class or of any subclass).  It is a primitive class:  a class name's value is the number I6 assigns the class.  Every registered class, whether declared with `class Name {…}` or `extern class Name : object {…}`, is type-compatible with `bglClass`.  Two class values compare with `==` and `!=`, and a class name can stand on the right (`k == Box`).  In default mode the class must be declared; in loose identifier mode (§16.3.3) the name passes through unchecked.

**Example**

```bgl
class Container : object { }
class Box : Container { int weight; }
Box crate { }

void Main() {
    if (crate.is(Container)) print("a container");   // true: Box inherits Container
}
```

**See also** §8.6, §16.3.3.

### 21.5.7 `parentProp` and `childrenProp`

**Syntax**

```syntax
OBJ.parent = NEW-PARENT ;
OBJ.parent
for ( object NAME in OBJ.children ) STATEMENT
OBJ.children.length
OBJ.children.size
OBJ.children += { OBJ , … } ;
```

`parentProp` is the type of the `parent` member that every `object` has (§11.5.1).  Reading it yields the parent object, and `==` and `!=` compare against an object.  The member is `typesealed`:  an object body can re-initialize `parent` but can't change its type (§8.3.7).

`childrenProp` is the type of the `children` member:  the collection of an object's direct children, whose reading and placement rules are in §11.5.2.  It is a storageless member, with no slot of its own.

**Example**

```bgl
object cave { }
object lamp { parent = cave; }

void Main() {
    lamp.parent = player;                    // move lamp to player
    int n = cave.children.length;          // 0
}
```

**See also** §11.5.

### 21.5.8 `_bglObject` and `_bglPrimitive`

**Syntax**

```syntax
class NAME : _bglObject { … }
primitive class NAME : _bglPrimitive { … }
```

Every class derives one of two roots, both `emitter class`es.  A class declared without a base derives the one which fits its form (§8.6):

- **`_bglObject`**:  the root of every other class, whose values are I6 objects:  classes and structs, `object`, and the namespace objects (`bgl`, `_bglUtil`, …).  An emitter class derives it too, unless it is `primitive`.  It carries what I6 gives any object:

  | Member | Meaning |
  |---|---|
  | `instanceName` (`inline const string`) | the object's name, set in its body; the last positional member (§11.5.4) |
  | `operator =` (`_bglObject`) | the root assignment:  the variable refers to the assigned instance |
  | `operator ?()` | true unless `nothing` |
  | `operator ==` / `operator !=` (`_bglObject`) | identity:  the same instance |
  | `provides(property)` | the object declares the property |
  | `is(bglClass)` | the object is of the class, or of one derived from it |
  | `print()` | prints the object's short name; what `print(obj)` calls |

  A struct copies through its own copy operator instead of the root assignment (§2.10.3, §9.1).  `object` adds world-tree citizenship (`parent`, `children`, attributes) on top.

- **`_bglPrimitive`**:  the root of every primitive class (`int`, `char`, `string`, `bglClass`, …, §8.2.5), whose value is held directly.  It declares only the root assignment, which copies the word.  Everything else a primitive supports, it declares itself.

**See also** §2.10.3, §8.2.

### 21.5.9 `eType` and `typeof()`

**Syntax**

```syntax
typeof( EXPR )
```

`typeof(v)` returns the machine category of a value as an `eType`.  It is the discriminator for union-typed values; the members of `eType`, what each value reports, and the casts and limits are in §2.8.1.

**Example**

```bgl
object lamp { }

void Main() {
    var x = lamp;
    if (typeof(x) == eType.object) print("an object");   // -> an object
    bool b = typeof(true) == eType.int;                  // -> true: bool reports as int
}
```

**See also** §2.8.

### 21.5.10 `iString`, `iStringOrObject` and `iStringOrRoutine`

**Syntax**

```syntax
iString NAME ;
iStringOrObject NAME ;
iStringOrRoutine NAME ;
VALUE.isInterpolated()
VALUE.isObject()
VALUE.isRoutine()
print( VALUE ) ;
```

`iString` is the named union `string | interpolatedString`:  a string, plain or interpolated.  An interpolated string assigned to one becomes a routine which prints it, with its expressions evaluated each time it prints (§2.9.1).  `isInterpolated()` reports which form the value holds, and `print(x)` prints either.

`iStringOrObject` is the named union `string | interpolatedString | object`:  an `iString` or an object.  `isObject()` reports which the value holds.

`iStringOrRoutine` is the named union `string | interpolatedString | func<void>`:  an `iString` or a routine which prints.  `isRoutine()` reports whether the value is a routine (an interpolated string included), and `print(x)` prints the string or runs the routine.

All three are provided by the IF library bindings (§23.3.7).  An object can give such a property a method instead of a value (§23.3.11).

**Example**

```bgl
#include <bindings/punyInform>
extend class object { iString description; iStringOrObject n_to; iStringOrObject s_to; iStringOrObject e_to; }
int storms = 3;
object bar { }
object cloakroom { }

object foyer {
    description = "A spacious hall.";                    // a plain string
    s_to = bar;                                          // the room the exit leads to
    n_to = $"The {storms} storms block the way.^";       // printed instead of moving, read each time
    object e_to() {                                      // decided each time
        if (cloakroom.has(light)) return cloakroom;
        rtrue("The cloakroom door is stuck.");
    }
}
```

**See also** §2.8.2, §2.9.1, §23.3.7.

## 21.6 Numeric Utilities

### 21.6.1 `uint`

**Syntax**

```syntax
uint NAME [ = EXPR ] ;
(uint) EXPR
(int) EXPR
```

`uint` is an unsigned integer with the same bit pattern and width as `int` (16 bits on the Z-machine, 32 on Glulx).  There is no implicit conversion between `int` and `uint` in either direction.  A non-negative integer literal converts implicitly; however, a negative literal converts only with an explicit `(uint)` cast, so `uint x = -1;` is a compile-time error.

| Operators | Behavior |
|---|---|
| `+` `-` `*` `++` `--` `+=` `-=` `*=` | As for `int`; overflow wraps. |
| `/` `%` `/=` `%=` | Unsigned division and modulo. |
| `==` `!=` | As for `int`. |
| `<` `<=` `>` `>=` | Unsigned ordering:  a value with the high bit set is large, not negative. |
| `&` `\|` `^` `&=` `\|=` `^=` `<<` `<<=` | As for `int`. |
| `>>` `>>=` | Logical (zero-fill) right shift. |
| unary `-` | Not defined on `uint`. |

`print(uint)` prints the full unsigned value:  `print((uint)-1)` prints `65535` on the Z-machine and `4294967295` on Glulx.

**Example**

```bgl
int  n = 7;
uint x = 12;          // literal
uint y = (uint)n;     // explicit cast from int
int  z = (int)y;      // explicit cast back
uint w = (uint)-1;    // a negative literal requires the cast
```

**See also** §2.2, §2.4.1.

### 21.6.2 `bgl.util.math`

`bgl.util.math` holds the math helpers which have no operator form.

| Function | Returns | Description |
|---|---|---|
| `bgl.util.math.abs(x)` | `int` | Absolute value. |
| `bgl.util.math.pow(base, exp)` | `int` | Integer exponentiation; a negative `exp` divides repeatedly. |
| `bgl.util.math.shiftLeft(x, n)` | `int` | Shift left by `n` bits. |
| `bgl.util.math.shiftRight(x, n)` | `int` | Arithmetic shift right by `n` bits. |
| `bgl.util.math.min(a, b)` / `max(a, b)` | `int` | Smaller / larger of two values. |
| `bgl.util.math.clamp(v, lo, hi)` | `int` | `v` limited to the range `lo..hi`. |
| `bgl.util.math.sign(v)` | `int` | `-1`, `0` or `1`. |
| `bgl.util.math.unsignedCompare(a, b)` | `int` | `-1`, `0` or `1` comparing `a` and `b` as unsigned values. |
| `bgl.util.math.unsignedDiv(a, b)` / `unsignedMod(a, b)` | `int` | Unsigned division / modulo; `b == 0` yields `0`. |

Call the unsigned helpers directly only for unsigned arithmetic on plain `int` values.

> ***[Glulx]:** On Glulx, `bgl.util.math` also takes `float` arguments:  `abs(x)` and `pow(a, b)` have float forms, and `sqrt(x)`, `exp(x)`, `log(x)`, `sin(x)`, `cos(x)`, `tan(x)`, `asin(x)`, `acos(x)`, `atan(x)`, `atan2(y, x)`, `ceil(x)`, and `floor(x)` take and return `float`.  Angles are in radians.*

**Example**

```bgl
int p = bgl.util.math.pow(2, 8);              // -> 256
int c = bgl.util.math.clamp(120, 0, 100);     // -> 100
int s = bgl.util.math.sign(-7);               // -> -1
```

### 21.6.3 `bgl.util.random`

`bgl.util.random` is the random-number source.

| Function | Returns | Description |
|---|---|---|
| `bgl.util.random.get(n)` | `int` | A uniformly random value in `1..n`. |
| `bgl.util.random.get(a, b[, c …])` | `var` | One of the listed values, chosen uniformly.  Two to eight values can be listed. |
| `bgl.util.random.seed(s)` | `void` | Seed the generator:  `s == 0` re-seeds from the environment; `s > 0` selects a deterministic sequence. |

**Example**

```bgl
object coin {}  object gem {}  object key {}

// … in a routine:
int d6 = bgl.util.random.get(6);
object prize = bgl.util.random.get(coin, gem, key);
```

> ***[Z-machine/Glulx difference]:** Both targets accept the same `seed(s)` calls, and the contract above holds on both.*

## 21.7 Character Utilities

The `char` type provides classification, case conversion, and case-insensitive comparison.  All of these handle the accented letters and ligatures:  on the Z-machine the ZSCII extended characters, and on Glulx the Latin-1 range (through Glk's case tables) and `œ`/`Œ`.

| Member | Returns | Description |
|---|---|---|
| `c.isLower()` / `c.isUpper()` | `bool` | Lowercase / uppercase letter, including accented letters. |
| `c.isAlpha()` | `bool` | Any letter. |
| `c.isNumeric()` | `bool` | A digit `0`..`9`. |
| `c.isAlphaNumeric()` | `bool` | A letter or digit. |
| `c.isVowel()` / `c.isConsonant()` | `bool` | Vowel (including accented vowels) / consonant. |
| `c.toUpper()` / `c.toLower()` | `char` | Case conversion; a character with no case maps to itself. |
| `c =~ d` | `bool` | Case-insensitive equality:  `'A' =~ 'a'` is true. |

The `char` comparison and arithmetic operators are in §4.5 and §4.6.

**Example**

```bgl
char c = 'a';
bool v = c.isVowel();        // -> true
char u = c.toUpper();        // -> 'A'
bool same = c =~ 'A';        // -> true
```

## 21.8 `bglAllocated`

**Syntax**

```syntax
class NAME [ N ] : object, bglAllocated { … }
INSTANCE.copy( OTHER ) ;
INSTANCE.remaining()
```

The `[ N ]` brackets are literal:  the pool size of §8.2.6.

`bglAllocated` is a mixin for pooled classes (§8.2.6).  A pooled class inheriting it gains two operations:

| Method | Returns | Description |
|---|---|---|
| `instance.copy(other)` | `void` | Copies every property of `other` into `instance`.  `other` is `selfType` (§2.11):  `instance`'s own class or a subclass of it. |
| `instance.remaining()` | `int` | The number of free slots remaining in the class's pool. |

Copying is explicit; assigning one pooled instance to another does not copy.  Copying a base-class instance into a subclass instance, or between unrelated classes, is a compile-time error.

**Example**

```bgl
class marble[10] : object, bglAllocated { int weight = 0; }

void Main() {
    marble a = new marble();
    marble b = new marble();
    b.copy(a);
    int free = a.remaining();     // 8
}
```

**See also** §4.13, §5.15, §8.2.6.

## 21.9 `bgl.world`

**Syntax**

```syntax
bgl.world.getAll( [ PRED ] )
bgl.world.inParent( PARENT [ , PRED ] )
bgl.world.instances( CLASS [ , PRED ] )
```

`bgl.world` queries the object tree and returns the matching objects as a tracked `array<object>`.

| Method | Returns |
|---|---|
| `bgl.world.getAll()` | Every object in the program. |
| `bgl.world.inParent(parent)` | The direct children of `parent`. |
| `bgl.world.instances(cls)` | Every object of class `cls` or a subclass. |

Each method takes an optional predicate, a `func<bool, object>`; only the objects for which it returns true are returned.

Results live in a shared, rotating set of four scratch buffers, each holding `worldBufSize` objects (default 128, §15.4).  The number of buffers is fixed.  A result is valid until the fourth subsequent query.  Don't store a result across turns; if you need it later, copy it into an `array<object>` of your own (assignment copies, §12.5).  Note that a walk which would exceed a buffer stops silently at its capacity.

**Example**

```bgl
class Treasure : object { }

void Main() {
    for (object o in bgl.world.instances(Treasure)) print(o);
    for (object o in bgl.world.inParent(location, (object v) => v.has(light))) print(o);
}
```

**See also** §4.14, §22.4, §22.5.

## 21.10 `bgl.ui`

**Syntax**

```syntax
bgl.ui.mainWin.id
bgl.ui.statusBar.id
bgl.ui.statusBar.height [ = LINES ]
bgl.ui.mainWin.width
bgl.ui.statusBar.width
bgl.ui.statusBar.moveCursor( COL , LINE )
bgl.ui.recoverWindows()
```

The two root windows of the display are objects under `bgl.ui`.  Their members are the same on both targets; however, an IF library binding replaces the members that the library itself controls (§23.3.8), and `<glulxWindow>` adds the window API to the same objects (§22.7.2).

| Member | Type | Description |
|---|---|---|
| `bgl.ui.mainWin.id` | `int` | The main window handle.  `0` until a binding or the program assigns it. |
| `bgl.ui.statusBar.id` | `int` | The status window handle.  `0` until assigned; `0` and read-only under a binding on the Z-machine, which has no window handles. |
| `bgl.ui.statusBar.height` | `int` | The status window's height in lines.  Writing it splits the status window to that height. |
| `bgl.ui.mainWin.width` | `int` | The main window's width in characters.  Read-only; `<glulxWindow>` makes it writable (§22.7.4). |
| `bgl.ui.statusBar.width` | `int` | The status window's width in characters.  Read-only; `<glulxWindow>` makes it writable (§22.7.4). |
| `bgl.ui.statusBar.moveCursor(col, line)` | `void` | Moves the status window's cursor to column `col`, line `line`, both counted from 0.  Output must be going to the status window. |
| `bgl.ui.recoverWindows()` | `void` | After a restore, undo or restart, brings the windows on screen back in line with the program's:  the status bar gets back the program's height, and with `<glulxWindow>` its windows are kept, closed or opened again (§22.7.6).  Calling it again has no effect. |

**Example**

```bgl
bgl.ui.statusBar.height = 2;
```

> ***[Glulx]:** Without a binding, writing `statusBar.height` opens a text-grid window above the root window on the first write, and re-arranges it thereafter.*

> ***[Z-machine]:** Without a binding, writing `statusBar.height` splits the upper window to that height.*

> ***[Glulx]:** `width` is 80 until the window is open.  Glk measures a text-buffer window in units of its font's `0`, so with a proportional font the main window's width is approximate.*

> ***[Z-machine]:** Both windows are as wide as the screen, so both `width`s read the header's screen width.*

> ***[Glulx]:** Glk windows live outside the program's memory, so a restore, undo or restart rolls back what the program believes about its windows but not what is on screen.  The Inform 6 standard library binding calls `recoverWindows()` for you once the library has found its own windows; a program with no library, or another library, calls it after its own restore, undo or restart.*

> ***[Z-machine]:** `recoverWindows()` does nothing.*

**See also** §22.6, §22.7, §23.3.8.

## 21.11 `bgl.printRules`

**Syntax**

```syntax
$"… {bgl.printRules.RULE} …"
$"… {bgl.printRules.img( IMAGE [ , ALIGN [ , WIDTH [ , HEIGHT ] ] ] )} …"
$"… {bgl.printRules.link( TEXT , TARGET )} …"
```

`bgl.printRules` holds print rules for use inside interpolated strings (§1.6.5).  The style rules switch the output style at that point in the text, while the article rules take the object to print.  With `#using bgl.printRules;` the rules can be reached bare.

| Rule | Effect |
|---|---|
| `a(obj)` | The object's short name with an indefinite article:  "a lamp". |
| `cA(obj)` | As `a`, capitalized:  "A lamp". |
| `the(obj)` | The object's short name with the definite article:  "the lamp". |
| `cThe(obj)` | As `the`, capitalized:  "The lamp". |
| `bold` | Bold text. |
| `italics` | Italic text (rendered as underline where the target has no italics). |
| `underline` | Underlined text. |
| `reverse` | Reverse video. |
| `fixed` | Fixed-pitch text. |
| `roman` | Return to plain text. |
| `img(image[, align[, width[, height]]])` | Draw an image inline in the main window.  **[Glulx]**, and only when `generateBlorb` is true (§15.6).  `image` is an `eImages` value; `align` is an `eGlulxImageAlign` (default `inlineCenter`); a `0` dimension is computed from the other, preserving aspect ratio, and `0, 0` draws at natural size. |
| `link(text, target)` | Print `text` as a hyperlink.  **[Glulx]**; a compile-time error on the Z-machine.  `target` is a `literal func<void> \| int`; what a click does depends on it.  See *Hyperlinks* below. |

**Example**

```bgl
#using bgl.printRules;

// … in a routine:
print($"The troll {bgl.printRules.italics}hit{bgl.printRules.roman} the table.");
print($"{bold}Warning{roman}");
print($"You cannot open {the(noun)}.");
```

**Hyperlinks**

The `target` of a `link` decides what a click does:

- A **function**, whether a lambda (§4.14) or a named function, is called.  The parameter is `literal` (§6.3):  the function runs when the player clicks, long after the `print`, so a lambda capturing a variable is a compile-time error.
- An **`int`** or an **enum value** is delivered to `HandleGlkEvent` (§6.7.1) as the event's link value, as in Inform 6.  A `target` of `0` makes no link.

<!-- doctest: skip -->
```bgl
#using bgl.printRules;
print($"There's a {link("button", => { print("Click.^"); })} on the machine.");  // run code
print($"There's a {link("lever", handlePullLever)} near it.");                   // call a function
print($"Under it is {link("broom", eLinks.broom)}.");                            // HandleGlkEvent
```

Glk does not allow printing to a window waiting for a line.  When a function target is clicked at a command prompt, the runtime suspends line input while the function runs.  The player's partial input is echoed, and the prompt is then reprinted with that input restored.  The library binding provides this for the Inform 6 library, but not for a program's own `HandleGlkEvent` (§6.7.1).

An interpreter without hyperlink support prints the link text plainly.

**See also** §1.6.5, §4.14, §15.6, §22.8.

## 21.12 Utility Types

**`bglSize`.**  A struct with `int width` and `int height` members and a copying `operator =`.

**Blorb asset enums.**  There are three enums, empty unless blorb packaging is enabled, and one union over them:

| Type | Contents |
|---|---|
| `eImages` | Picture resource ids. |
| `eSounds` | Sound resource ids. |
| `eUnknownAsset` | Data resource ids:  any packaged file which is neither an image nor a sound. |
| `eAssets` | The union `eImages \| eSounds \| eUnknownAsset`, for APIs which accept any resource. |

When `generateBlorb` is true, the compiler extends `eImages` and `eSounds` with one member per asset file found (§15.6.1).  A value of `eImages` is the raw resource id; with `<glulxImage>` it also has the properties `width`, `height`, and `size` (§22.8).

**See also** §2.7, §2.8, §15.6.1, §22.8.

## 21.13 `bgl.asm`

`bgl.asm` is the namespace of direct opcode emitters for the active target, an alias (§10.2) for a target-specific class.  A member is either an emitter, which inlines the opcode at the call site, or a `static superposed` function (§3.12), which wraps a value-returning opcode in a free routine.  In the tables below, kind **E** is an emitter and **S** a `static superposed` function.

**Example**

<!-- doctest: glulx -->
```bgl
#using bgl;

// … in a routine:
int n = asm.random(6);        // Glulx: @random; Z-machine: @random
asm.streamchar('!');          // Glulx only
```

### 21.13.1 Glulx Opcodes

On Glulx, `bgl.asm` provides the following opcodes:

| Member | Signature | Kind | Meaning |
|---|---|---|---|
| `add` / `sub` / `mul` / `div` / `mod` | `int OP(int a, int b)` | E | Arithmetic |
| `neg` | `int neg(int a)` | E | `-a` |
| `bitand` / `bitor` | `int OP(int a, int b)` | E | Bitwise and, or |
| `bitxor` | `int bitxor(int a, int b)` | S | `@bitxor` |
| `bitnot` | `int bitnot(int a)` | E | `~a` |
| `shiftl` | `int shiftl(int a, int b)` | S | Logical shift left |
| `sshiftr` | `int sshiftr(int a, int b)` | S | Arithmetic shift right |
| `ushiftr` | `int ushiftr(int a, int b)` | S | Logical shift right |
| `aload` / `aloads` / `aloadb` / `aloadbit` | `int OP(int addr, int idx)` | S | Load word, short, byte, bit |
| `astore` / `astores` / `astoreb` / `astorebit` | `void OP(int addr, int idx, int v)` | E | Store word, short, byte, bit |
| `copy` | `int copy(int v)` | E | `@copy` |
| `sexs` / `sexb` | `int OP(int v)` | S | Sign-extend short, byte |
| `stkcount` | `int stkcount()` | S | `@stkcount` |
| `stkpeek` | `int stkpeek(int idx)` | S | `@stkpeek` |
| `stkswap` | `void stkswap()` | E | `@stkswap` |
| `stkroll` | `void stkroll(int count, int dir)` | E | `@stkroll` |
| `stkcopy` | `void stkcopy(int count)` | E | `@stkcopy` |
| `callf` / `callfi` / `callfii` / `callfiii` | `int OP(int addr, …)` | S | Call with 0 to 3 arguments |
| `streamchar` / `streamnum` / `streamstr` / `streamunichar` | `void OP(int v)` | E | Stream output |
| `getstringtbl` | `int getstringtbl()` | S | `@getstringtbl` |
| `setstringtbl` | `void setstringtbl(int addr)` | E | `@setstringtbl` |
| `getmemsize` | `int getmemsize()` | S | `@getmemsize` |
| `setmemsize` | `int setmemsize(int size)` | S | `@setmemsize` |
| `malloc` | `int malloc(int size)` | S | Heap allocate |
| `mfree` | `void mfree(int addr)` | E | `@mfree` |
| `mzero` | `void mzero(int count, int addr)` | E | `@mzero` |
| `mcopy` | `void mcopy(int count, int src, int dst)` | E | `@mcopy` |
| `linearsearch` / `binarysearch` | `int OP(int key, int keySize, int start, int structSize, int numStructs, int keyOffset, int options)` | S | Table search |
| `linkedsearch` | `int linkedsearch(int key, int keySize, int start, int keyOffset, int nextOffset, int options)` | S | `@linkedsearch` |
| `random` | `int random(int range)` | S | `@random` |
| `setrandom` | `void setrandom(int seed)` | E | `@setrandom` |
| `quit` / `restart` | `void OP()` | E | `@quit`, `@restart` |
| `save` / `restore` | `int OP(int stream)` | S | `@save`, `@restore` |
| `saveundo` / `restoreundo` | `int OP()` | S | `@saveundo`, `@restoreundo` |
| `protect` | `void protect(int addr, int len)` | E | `@protect` |
| `verify` | `int verify()` | S | `@verify` |
| `debugtrap` | `void debugtrap(int val)` | E | `@debugtrap` |
| `glk` | `int glk(int sel, int argc)` | E | Raw Glk dispatch |
| `setiosys` | `void setiosys(int mode, int rock)` | E | `@setiosys` |
| `numtof` | `int numtof(int n)` | S | Integer to float bits |
| `ftonumz` / `ftonumn` | `int OP(int f)` | S | Float to integer, truncating / rounding |
| `fadd` / `fsub` / `fmul` / `fdiv` | `int OP(int a, int b)` | S | Float arithmetic |
| `ceil` / `floor` / `sqrt` / `exp` / `log` | `int OP(int f)` | S | Float unary functions |
| `pow` | `int pow(int a, int b)` | S | `@pow` |
| `sin` / `cos` / `tan` / `asin` / `acos` / `atan` | `int OP(int f)` | S | Float trigonometry |
| `atan2` | `int atan2(int a, int b)` | S | `@atan2` |
| `accelfunc` | `void accelfunc(int idx, int addr)` | E | `@accelfunc` |
| `accelparam` | `void accelparam(int idx, int val)` | E | `@accelparam` |

### 21.13.2 Glk Calls

Each of these is an emitter which performs the named Glk call; the member name is the Glk function name in camel case, without the `glk_` prefix (`windowOpen` is `glk_window_open`).  Parameters are `int` and follow the Glk argument order unless noted.

| Member | Signature | Glk call |
|---|---|---|
| `exit` | `void exit()` | `glk_exit` |
| `tick` | `void tick()` | `glk_tick` |
| `gestalt` | `int gestalt(int a, int b)` | `glk_gestalt` |
| `gestaltExt` | `int gestaltExt(int a, int b, int c, int d)` | `glk_gestalt_ext` |
| `windowIterate` | `int windowIterate(int a, int b)` | `glk_window_iterate` |
| `windowGetRock` | `int windowGetRock(int a)` | `glk_window_get_rock` |
| `windowGetRoot` | `int windowGetRoot()` | `glk_window_get_root` |
| `windowOpen` | `int windowOpen(int splitWinId, eGlulxWindowType type, int size, bGlulxWindowMethodFlags winMethod = noBorder, int winRock = 0)` | `glk_window_open` (the parameters are not in Glk order; the call is made in Glk order) |
| `windowClose` | `void windowClose(int a, int b)` | `glk_window_close` |
| `windowGetSize` | `void windowGetSize(int a, int b, int c)` | `glk_window_get_size` |
| `windowSetArrangement` | `void windowSetArrangement(int a, int b, int c, int d)` | `glk_window_set_arrangement` |
| `windowGetArrangement` | `void windowGetArrangement(int a, int b, int c, int d)` | `glk_window_get_arrangement` |
| `windowGetType` | `int windowGetType(int a)` | `glk_window_get_type` |
| `windowGetParent` | `int windowGetParent(int a)` | `glk_window_get_parent` |
| `windowClear` | `void windowClear(int a)` | `glk_window_clear` |
| `windowMoveCursor` | `void windowMoveCursor(int a, int b, int c)` | `glk_window_move_cursor` |
| `windowGetStream` | `int windowGetStream(int a)` | `glk_window_get_stream` |
| `windowSetEchoStream` | `void windowSetEchoStream(int a, int b)` | `glk_window_set_echo_stream` |
| `windowGetEchoStream` | `int windowGetEchoStream(int a)` | `glk_window_get_echo_stream` |
| `setWindow` | `void setWindow(int a)` | `glk_set_window` |
| `windowGetSibling` | `int windowGetSibling(int a)` | `glk_window_get_sibling` |
| `streamIterate` | `int streamIterate(int a, int b)` | `glk_stream_iterate` |
| `streamGetRock` | `int streamGetRock(int a)` | `glk_stream_get_rock` |
| `streamOpenFile` | `int streamOpenFile(int a, int b, int c)` | `glk_stream_open_file` |
| `streamOpenMemory` | `int streamOpenMemory(int a, int b, int c, int d)` | `glk_stream_open_memory` |
| `streamClose` | `void streamClose(int a, int b)` | `glk_stream_close` |
| `streamSetPosition` | `void streamSetPosition(int a, int b, int c)` | `glk_stream_set_position` |
| `streamGetPosition` | `int streamGetPosition(int a)` | `glk_stream_get_position` |
| `streamSetCurrent` | `void streamSetCurrent(int a)` | `glk_stream_set_current` |
| `streamGetCurrent` | `int streamGetCurrent()` | `glk_stream_get_current` |
| `streamOpenResource` | `int streamOpenResource(int a, int b)` | `glk_stream_open_resource` |
| `filerefCreateTemp` | `int filerefCreateTemp(int a, int b)` | `glk_fileref_create_temp` |
| `filerefCreateByName` | `int filerefCreateByName(int a, int b, int c)` | `glk_fileref_create_by_name` |
| `filerefCreateByPrompt` | `int filerefCreateByPrompt(int a, int b, int c)` | `glk_fileref_create_by_prompt` |
| `filerefDestroy` | `void filerefDestroy(int a)` | `glk_fileref_destroy` |
| `filerefIterate` | `int filerefIterate(int a, int b)` | `glk_fileref_iterate` |
| `filerefGetRock` | `int filerefGetRock(int a)` | `glk_fileref_get_rock` |
| `filerefDeleteFile` | `void filerefDeleteFile(int a)` | `glk_fileref_delete_file` |
| `filerefDoesFileExist` | `int filerefDoesFileExist(int a)` | `glk_fileref_does_file_exist` |
| `filerefCreateFromFileref` | `int filerefCreateFromFileref(int a, int b, int c)` | `glk_fileref_create_from_fileref` |
| `putChar` / `putCharStream` | `void putChar(int a)` / `void putCharStream(int a, int b)` | `glk_put_char`, `glk_put_char_stream` |
| `putString` / `putStringStream` | `void putString(int a)` / `void putStringStream(int a, int b)` | `glk_put_string`, `glk_put_string_stream` |
| `putBuffer` / `putBufferStream` | `void putBuffer(int a, int b)` / `void putBufferStream(int a, int b, int c)` | `glk_put_buffer`, `glk_put_buffer_stream` |
| `setStyle` / `setStyleStream` | `void setStyle(int a)` / `void setStyleStream(int a, int b)` | `glk_set_style`, `glk_set_style_stream` |
| `getCharStream` | `int getCharStream(int a)` | `glk_get_char_stream` |
| `getLineStream` | `int getLineStream(int a, int b, int c)` | `glk_get_line_stream` |
| `getBufferStream` | `int getBufferStream(int a, int b, int c)` | `glk_get_buffer_stream` |
| `charToLower` / `charToUpper` | `int OP(int a)` | `glk_char_to_lower`, `glk_char_to_upper` |
| `stylehintSet` | `void stylehintSet(int a, int b, int c, int d)` | `glk_stylehint_set` |
| `stylehintClear` | `void stylehintClear(int a, int b, int c)` | `glk_stylehint_clear` |
| `styleDistinguish` | `int styleDistinguish(int a, int b, int c)` | `glk_style_distinguish` |
| `styleMeasure` | `int styleMeasure(int a, int b, int c, int d)` | `glk_style_measure` |
| `select` / `selectPoll` | `void OP(int a)` | `glk_select`, `glk_select_poll` |
| `requestLineEvent` | `void requestLineEvent(int a, int b, int c, int d)` | `glk_request_line_event` |
| `cancelLineEvent` | `void cancelLineEvent(int a, int b)` | `glk_cancel_line_event` |
| `requestCharEvent` / `cancelCharEvent` | `void OP(int a)` | `glk_request_char_event`, `glk_cancel_char_event` |
| `requestMouseEvent` / `cancelMouseEvent` | `void OP(int a)` | `glk_request_mouse_event`, `glk_cancel_mouse_event` |
| `requestTimerEvents` | `void requestTimerEvents(int a)` | `glk_request_timer_events` |
| `imageGetInfo` | `int imageGetInfo(int a, int b, int c)` | `glk_image_get_info` |
| `imageDraw` | `int imageDraw(int a, int b, int c, int d)` | `glk_image_draw` |
| `imageDrawScaled` | `int imageDrawScaled(int win, var img, var alignOrX, int y, int width, int height)` | `glk_image_draw_scaled` |
| `windowFlowBreak` | `void windowFlowBreak(int a)` | `glk_window_flow_break` |
| `windowEraseRect` | `void windowEraseRect(int a, int b, int c, int d, int e)` | `glk_window_erase_rect` |
| `windowFillRect` | `void windowFillRect(int a, int b, int c, int d, int e, int f)` | `glk_window_fill_rect` |
| `windowSetBackgroundColor` | `void windowSetBackgroundColor(int a, int b)` | `glk_window_set_background_color` |
| `schannelIterate` | `int schannelIterate(int a, int b)` | `glk_schannel_iterate` |
| `schannelGetRock` | `int schannelGetRock(int a)` | `glk_schannel_get_rock` |
| `schannelCreate` / `schannelCreateExt` | `int schannelCreate(int a)` / `int schannelCreateExt(int a, int b)` | `glk_schannel_create`, `glk_schannel_create_ext` |
| `schannelDestroy` | `void schannelDestroy(int a)` | `glk_schannel_destroy` |
| `schannelPlay` / `schannelPlayExt` | `int schannelPlay(int a, int b)` / `int schannelPlayExt(int a, int b, int c, int d)` | `glk_schannel_play`, `glk_schannel_play_ext` |
| `schannelPlayMulti` | `int schannelPlayMulti(int a, int b, int c, int d, int e)` | `glk_schannel_play_multi` |
| `schannelStop` / `schannelPause` / `schannelUnpause` | `void OP(int a)` | `glk_schannel_stop`, `glk_schannel_pause`, `glk_schannel_unpause` |
| `schannelSetVolume` / `schannelSetVolumeExt` | `void schannelSetVolume(int a, int b)` / `void schannelSetVolumeExt(int a, int b, int c, int d)` | `glk_schannel_set_volume`, `glk_schannel_set_volume_ext` |
| `soundLoadHint` | `void soundLoadHint(int a, int b)` | `glk_sound_load_hint` |
| `setHyperlink` / `setHyperlinkStream` | `void setHyperlink(int a)` / `void setHyperlinkStream(int a, int b)` | `glk_set_hyperlink`, `glk_set_hyperlink_stream` |
| `requestHyperlinkEvent` / `cancelHyperlinkEvent` | `void OP(int a)` | `glk_request_hyperlink_event`, `glk_cancel_hyperlink_event` |
| `bufferToLowerCaseUni` / `bufferToUpperCaseUni` | `int OP(int a, int b, int c)` | `glk_buffer_to_lower_case_uni`, `glk_buffer_to_upper_case_uni` |
| `bufferToTitleCaseUni` | `int bufferToTitleCaseUni(int a, int b, int c, int d)` | `glk_buffer_to_title_case_uni` |
| `bufferCanonDecomposeUni` / `bufferCanonNormalizeUni` | `int OP(int a, int b, int c)` | `glk_buffer_canon_decompose_uni`, `glk_buffer_canon_normalize_uni` |
| `putCharUni` / `putStringUni` | `void OP(int a)` | `glk_put_char_uni`, `glk_put_string_uni` |
| `putBufferUni` | `void putBufferUni(int a, int b)` | `glk_put_buffer_uni` |
| `putCharStreamUni` / `putStringStreamUni` | `void OP(int a, int b)` | `glk_put_char_stream_uni`, `glk_put_string_stream_uni` |
| `putBufferStreamUni` | `void putBufferStreamUni(int a, int b, int c)` | `glk_put_buffer_stream_uni` |
| `getCharStreamUni` | `int getCharStreamUni(int a)` | `glk_get_char_stream_uni` |
| `getBufferStreamUni` / `getLineStreamUni` | `int OP(int a, int b, int c)` | `glk_get_buffer_stream_uni`, `glk_get_line_stream_uni` |
| `streamOpenFileUni` | `int streamOpenFileUni(int a, int b, int c)` | `glk_stream_open_file_uni` |
| `streamOpenMemoryUni` | `int streamOpenMemoryUni(int a, int b, int c, int d)` | `glk_stream_open_memory_uni` |
| `streamOpenResourceUni` | `int streamOpenResourceUni(int a, int b)` | `glk_stream_open_resource_uni` |
| `requestCharEventUni` | `void requestCharEventUni(int a)` | `glk_request_char_event_uni` |
| `requestLineEventUni` | `void requestLineEventUni(int a, int b, int c, int d)` | `glk_request_line_event_uni` |
| `setEchoLineEvent` | `void setEchoLineEvent(int a, int b)` | `glk_set_echo_line_event` |
| `setTerminatorsLineEvent` | `void setTerminatorsLineEvent(int a, int b, int c)` | `glk_set_terminators_line_event` |
| `currentTime` | `void currentTime(int a)` | `glk_current_time` |
| `currentSimpleTime` | `int currentSimpleTime(int a)` | `glk_current_simple_time` |
| `timeToDateUtc` / `timeToDateLocal` | `void OP(int a, int b)` | `glk_time_to_date_utc`, `glk_time_to_date_local` |
| `simpleTimeToDateUtc` / `simpleTimeToDateLocal` | `void OP(int a, int b, int c)` | `glk_simple_time_to_date_utc`, `glk_simple_time_to_date_local` |
| `dateToTimeUtc` / `dateToTimeLocal` | `void OP(int a, int b)` | `glk_date_to_time_utc`, `glk_date_to_time_local` |
| `dateToSimpleTimeUtc` / `dateToSimpleTimeLocal` | `int OP(int a, int b)` | `glk_date_to_simple_time_utc`, `glk_date_to_simple_time_local` |
| `garglkSetZcolors` / `garglkSetZcolorsStream` | `void garglkSetZcolors(int a, int b)` / `void garglkSetZcolorsStream(int a, int b, int c)` | `garglk_set_zcolors`, `garglk_set_zcolors_stream` |
| `garglkSetReversevideo` / `garglkSetReversevideoStream` | `void garglkSetReversevideo(int a)` / `void garglkSetReversevideoStream(int a, int b)` | `garglk_set_reversevideo`, `garglk_set_reversevideo_stream` |

### 21.13.3 Z-machine Opcodes

On the Z-machine, `bgl.asm` provides the following opcodes:

| Member | Signature | Kind | Meaning |
|---|---|---|---|
| `add` / `sub` / `mul` / `div` / `mod` | `int OP(int a, int b)` | E | Arithmetic |
| `bitor` / `bitand` | `int OP(int a, int b)` | E | Bitwise or, and |
| `bitnot` | `int bitnot(int a)` | E | `~a` |
| `loadw` / `loadb` | `int OP(int arr, int idx)` | S | `@loadw`, `@loadb` |
| `storew` / `storeb` | `void OP(int arr, int idx, int v)` | E | `@storew`, `@storeb` |
| `push` | `void push(int v)` | E | `@push` |
| `pull` | `void pull(var into)` | E | `@pull` into a variable; a routine has its own stack, so `pull` is a statement, not a value |
| `getParent` / `getChild` / `getSibling` | `int OP(int obj)` | S | `@get_parent`, `@get_child`, `@get_sibling` |
| `insertObj` | `void insertObj(int obj, int dst)` | E | `@insert_obj` |
| `removeObj` | `void removeObj(int obj)` | E | `@remove_obj` |
| `printObj` | `void printObj(int obj)` | E | `@print_obj` |
| `getProp` / `getPropAddr` / `getNextProp` | `int OP(int obj, int prop)` | S | `@get_prop`, `@get_prop_addr`, `@get_next_prop` |
| `getPropLen` | `int getPropLen(int propAddr)` | S | `@get_prop_len` |
| `putProp` | `void putProp(int obj, int prop, int v)` | E | `@put_prop` |
| `setAttr` / `clearAttr` | `void OP(int obj, int attr)` | E | `@set_attr`, `@clear_attr` |
| `inc` / `dec` | `void OP(int variable)` | E | `@inc`, `@dec` |
| `printChar` / `printNum` / `printAddr` / `printPaddr` | `void OP(int v)` | E | `@print_char`, `@print_num`, `@print_addr`, `@print_paddr` |
| `newLine` | `void newLine()` | E | `@new_line` |
| `splitWindow` | `void splitWindow(int lines)` | E | `@split_window` |
| `setWindow` | `void setWindow(int win)` | E | `@set_window` |
| `outputStream` | `void outputStream(int n)` / `void outputStream(int n, int table)` | E | `@output_stream` |
| `inputStream` | `void inputStream(int n)` | E | `@input_stream` |
| `random` | `int random(int range)` | S | `@random` |
| `quit` / `restart` | `void OP()` | E | `@quit`, `@restart` |
| `verify` | `int verify()` | E | `@verify` |
| `soundEffect` | `void soundEffect(int num, int effect, int vol)` | E | `@sound_effect` |
| `eraseWindow` | `void eraseWindow(int win)` | E | `@erase_window` |
| `eraseLine` | `void eraseLine(int v)` | E | `@erase_line` |
| `setCursor` | `void setCursor(int line, int col)` | E | `@set_cursor` |
| `getCursor` | `void getCursor(int arr)` | E | `@get_cursor` |
| `setTextStyle` | `void setTextStyle(int style)` | E | `@set_text_style` |
| `bufferMode` | `void bufferMode(int flag)` | E | `@buffer_mode` |
| `readChar` | `int readChar(int dev = 1)` | S | `@read_char` |
| `scanTable` | `int scanTable(int x, int table, int len)` | S | `@scan_table` |
| `call1s` / `call2s` | `int call1s(int routine)` / `int call2s(int routine, int a)` | S | `@call_1s`, `@call_2s` |
| `logShift` / `artShift` | `int OP(int a, int places)` | S | `@log_shift`, `@art_shift` |
| `copyTable` | `void copyTable(int src, int dst, int size)` | E | `@copy_table` |
| `printUnicode` | `void printUnicode(int ch)` | E | `@print_unicode` |
| `setColour` | `void setColour(int fg, int bg)` | E | `@set_colour` |
| `setFont` | `int setFont(int font)` | S | `@set_font` |
| `tokenise` | `void tokenise(int text, int parse)` | E | `@tokenise` |
| `encodeText` | `void encodeText(int zscii, int len, int from, int coded)` | E | `@encode_text` |
| `printTable` | `void printTable(int zscii, int width)` | E | `@print_table` |
| `save` / `restore` | `int OP(int table, int bytes, int name)` | S | `@save`, `@restore` |
| `saveUndo` / `restoreUndo` | `int OP()` | S | `@save_undo`, `@restore_undo` |
| `call1n` / `call2n` | `void call1n(int routine)` / `void call2n(int routine, int a)` | E | `@call_1n`, `@call_2n` |
| `checkUnicode` | `int checkUnicode(int ch)` | S | `@check_unicode` |

**See also** §3.12, §10.2, §22.7.

## 21.14 `bgl.header`

`bgl.header` reads fields of the story file header:  the release number and serial code the game was compiled with (§15.3).  It needs no library.

| Member | Returns | Description |
|---|---|---|
| `bgl.header.release` | `int` | The release number. |
| `bgl.header.serialChar(i)` | `char` | Character `i` (`0..5`) of the six-character serial code. |
| `bgl.header.printSerial()` | `void` | Prints the serial code. |

**Example**

```bgl
print($"Release {bgl.header.release} / Serial number {bgl.header.printSerial()}");
```

## 21.15 `bgl.gc`

`bgl.gc` is the heap from which local arrays and `<linq>` results are allocated, along with its collector (§12.6).  It needs no library.

| Member | Returns | Description |
|---|---|---|
| `bgl.gc.free(a)` | `void` | Frees array `a` now instead of at the next collection.  A later use of it through another reference reads freed memory. |
| `bgl.gc.keep(a)` | `void` | Pins array `a`, for I6 code that holds on to it (§12.6):  it lives until `unpin` or `free`, whatever refers to it. |
| `bgl.gc.unpin(a)` | `void` | Hands a pinned array back to the collector, which frees it once nothing refers to it. |
| `bgl.gc.collect()` | `void` | Frees every heap array that nothing refers to. |
| `bgl.gc.used` | `int` | Words of the heap in blocks in use.  The same arrays can read slightly differently as the heap fragments. |
| `bgl.gc.size` | `int` | Words the heap holds for blocks. |
| `bgl.gc.peak` | `int` | The most words ever in blocks in use:  what `heapSize` must hold. |

The library bindings collect at the prompt (§23); when a program without one calls `collect()`, and what the collector can see, are covered in §12.6.

**Example**

```bgl
#include <array>
array<int> row(int n) {
    array<int> r[4];
    for (int i = 0; i < 4; i++) r += n * i;
    return r;
}

void Main() {
    bglInit();
    for (int n = 1; n <= 20; n++) {
        array<int> r = row(n);
        print(r[3], " ");
        bgl.gc.free(r);         // released now, not at the next prompt
    }
}
```

> ***Note:** Calling `bgl.gc.free` on an array already freed, or on a file-scope or member array (which isn't on the heap), is a run-time error (§19.4).*

---

# 22 Language Extensions

## 22.1 Overview

An *extension* is a file in the `beguiLib` folder which your program enables with `#include <name>` (§14.1.1).  Extensions build on the runtime core (§21) and are library-agnostic:  each works with any IF library binding, or with none.  The exceptions are the library-specific extensions (§22.9), which work only with the library they name.  Nothing in an extension is available until you include it, and a program which doesn't include one carries none of its code.

This chapter covers the following extensions:

| Extension | Include | Adds | Requires `bglInit()` | Also includes | Target |
|---|---|---|---|---|---|
| Tracked character buffers | `#include <buf>` | `array<char>` length tracking, `bgl.util.buf` | yes (length headers of sized arrays) | - | both |
| Strings | `#include <string>` | the `stringObj` type; text methods on `string` | yes | `<buf>` | both |
| Arrays | `#include <array>` | search, mutation, deque, and sort methods on `array<T>`; `copyFrom` | no | - | both |
| LINQ chains | `#include <linq>` | fluent chain operations on `array<T>` | no | `<array>` | both |
| Key input | `#include <ui>` | `bgl.ui.waitForKey()`, `hideCursor()`, `showCursor()` | no | - | both |
| Glulx windows | `#include <glulxWindow>` | window types, splitting, sizing, styles, colors | no | `<glulxImage>` when `generateBlorb` is true | Glulx |
| Glulx images | `#include <glulxImage>` | the `glulxImage` handle; `eImages` metadata | no | - | Glulx |

Including an extension more than once is harmless.  The names `uint`, the `char` utilities, `bgl.util.math`, `bgl.util.random`, `bglAllocated`, and `bgl.world` are part of the core and need no include (§21).

Every entry below has the same shape:  purpose, include line, what it adds, the settings it reads, and notes.

## 22.2 `<buf>`

**Purpose.**  Length-tracked character buffers.  With `<buf>` included, a sized `array<char>` is a *tracked buf*:  it records its capacity and current length, and the buffer operations below apply to it.

**Include**

```bgl
#include <buf>
```

A tracked buf is laid out as a standard I6 hybrid buffer (the length word first, then the characters), so you can pass it directly to an I6 routine which expects one.  `buf[i]` reads and writes character `i` (§12.4), and doesn't change the length.

An untracked `array<char>` (one created without the tracked layout) answers `size` and `length` from the buffer's length word (§12.4), and `isTracked()` returns false.  An array which Inform 6 declares, such as the standard library's input `buffer`, is declared `extern rawArray<char>` (§12.8), not `array<char>`.

A buf, and the `stringObj` built on one (§22.3), holds one byte per character, so it holds only the characters of Latin-1 (codes 0 to 255).  This is true on Glulx too, where string literals and `char` values can be any Unicode character (§D.5):  a character beyond Latin-1 captured or appended into a buf will be stored as `?`.  An `array<char>` initialized from such text is a compile-time error.

**Methods on `array<char>`**

| Method | Returns | Description |
|---|---|---|
| `buf.size` | `int` | Capacity in characters; an untracked buffer's is its length word, and `-1` for `null`. |
| `buf.length` | `int` | Current number of characters. |
| `buf.length = n` | | Set the current length, limited to `size` on a tracked buffer. |
| `buf.isTracked()` | `bool` | True for a tracked buf. |
| `buf.copyFrom(other)` | | Copy `other`'s characters and length, up to `size`; an untracked buffer takes no more than its current length. |

**Buffer operations, `bgl.util.buf`**

Operations which return `array<char>` return the buffer they modified.

| Operation | Returns | Description |
|---|---|---|
| `set(buf, value)` | `array<char>` | Replace the contents with a string literal or another buffer; sets the length. |
| `copy(to, from, n[, toPos[, fromPos]])` | `array<char>` | Copy `n` characters (`-1`: to the end of `from`) from `from[fromPos]` to `to[toPos]`; bounded by both buffers. |
| `append(to, from)` / `prepend(to, from)` | `array<char>` | Add `from`'s contents at the end / start of `to`; `from` is a buffer or a string (a literal included). |
| `insert(to, from, pos[, count])` | `array<char>` | Insert `from` (or its first `count` characters) at `pos`. |
| `delete(buf, pos, count)` | `array<char>` | Remove `count` characters at `pos`, closing the gap. |
| `mid(to, from, fromPos, n)` / `left(to, from, n)` / `right(to, from, n)` | `array<char>` | Copy a substring of `from` into `to`; `n == -1` in `mid` means to the end, and a position or count outside `from` is clamped to it. |
| `indexOf(buf, search[, start])` | `int` | Position of the first occurrence at or after `start`, or `-1`. |
| `indexOfFirstTrue(buf, pred[, start])` / `indexOfFirstFalse` | `int` | Position of the first character for which `pred(c)` is true / false, or `-1`.  `pred` is `func<bool, char>`. |
| `replace(buf, search, repl)` / `replaceAll(buf, search, repl[, start])` | `array<char>` | Replace the first / every occurrence in place. |
| `equals(a, b[, caseInsensitive])` | `bool` | Content equality. |
| `compare(a, b[, caseInsensitive])` | `int` | `-1`, `0`, or `1`, by character code; a shorter buffer which matches sorts first. |
| `startsWith(buf, prefix[, caseInsensitive])` / `endsWith(buf, suffix[, caseInsensitive])` | `bool` | Prefix / suffix test. |
| `toUpper(buf)` / `toLower(buf)` / `reverse(buf)` | `array<char>` | In-place transforms. |
| `trim(buf)` / `trimLeft(buf)` / `trimRight(buf)` | `array<char>` | Remove leading and/or trailing spaces in place. |
| `getChar(buf, pos)` / `setChar(buf, pos, c)` | `int` / `void` | Bounds-checked character access; `getChar` returns `-1` out of range. |
| `print(buf[, len])` | `array<char>` | Print the first `len` characters, or all when `len` is omitted or `-1`. |
| `capture(buf[, maxBytes])` | `void` | Redirect subsequent `print` output into `buf`.  Captures nest to a depth of 16. |
| `release()` | `array<char>` | End the most recent capture, restore the previous output target, and set the buffer's length to what was written. |
| `stackLen()` | `int` | The current capture depth. |
| `isTracked(buf)` / `size(buf)` / `length(buf)` / `setLength(buf, n)` | | The function forms of the methods above. |

**Example**

```bgl
#include <buf>
array<char> line[64];
array<char> word[16];

void Main() {
    bglInit();
    bgl.util.buf.set(line, "hello");
    bgl.util.buf.set(word, " world");
    bgl.util.buf.append(line, word);   // a buffer
    bgl.util.buf.append(line, "!");    // or a string
    bgl.util.buf.toUpper(line);
    bgl.util.buf.print(line);          // -> HELLO WORLD!
    print(line.length);                // -> 12
}
```

**Settings**

`bglStringDefaultSize` (default `500`) is the capacity used for capture into an untracked buffer; `<string>` doesn't use it.  It is an I6 constant; since `<buf>` is processed before your code, set it in an `#emitfirst` block (§14.4.2):

```bgl
#emitfirst { Constant bglStringDefaultSize 800; }
```

> ***Note:** `<buf>` requires `bglInit()` (§21.2).  A local sized buffer is tracked from its declaration until its routine returns.  The registry of tracked buffers has room for the `<string>` pool's buffers plus `bglTrackedBufferReserve` more (default `116`; an I6 constant set before the include, like `bglStringPoolReserve`).*

> ***[Glulx]:** `<buf>` defines `filemode_Write` and `gg_arguments` when no IF library does.*

**See also** §12.4, §22.3.

## 22.3 `<string>`

**Purpose.**  Text you can build and compare:  a second type, `stringObj`, which owns a mutable buffer and compares by content, and text methods and ordering on `string`.

**Include**

```bgl
#include <string>        // includes <buf>
```

The extension has two types, for two different things:

| Type | The slot holds | Mutable | Lifecycle |
|---|---|---|---|
| `string` | a reference to static text (a literal) | no | none; nothing is owned |
| `stringObj` | a buffer of its own, a block in the string heap | yes | allocated on declaration, released at scope exit |

A string literal is a `string`.  The two types mix freely in expressions, and every operation which *produces* text returns a `stringObj`.

Assigning a `stringObj` to a `string` is a compile-time error, and so is passing a `stringObj` where a `string` parameter is expected:  the parameter takes `stringObj`, or `string | stringObj` for either.

`stringObj a = b;` copies `b`'s value; `ref stringObj a := b;` binds a reference (§3.7).  A `stringObj` parameter is a copy too (§8.5); a `ref stringObj` parameter shares the caller's.  Results can be passed straight into a call or an operator, several at once (`join(s.left(3), s.right(4))`, `a.left(1) + b.right(1)`):  each is held until the call returns (§8.5.1).  A `stringObj` global is allocated at startup.

A `stringObj` which a string operation returns is *ephemeral*:  its block has already been released when the caller receives it (§8.5), and its text stays intact only until the next string is allocated.  Used at once, as an operand or an argument, it is held for that call as above.  To keep it any longer, assign it to a `stringObj`, which copies it.

`==` and `!=` between two `string`s (literals included) compare addresses, as they do without the extension (§2.2):  two equal literals are different strings.  `a.equals(b)` compares their text.  When either side is a `stringObj`, `==` and `!=` compare text.

`s == null` and `s != null` remain identity tests on both types (there is no content to compare against `null`).

**Operators (both types unless noted)**

| Operator | Description |
|---|---|
| `s = "text"` / `s = other` | Assign.  On a `string` the slot now refers to the text; on a `stringObj` the text is copied into its buffer.  `other` is a literal, a `string` or a `stringObj`; an `array<char>` is not (append it instead, `s = s.append(buf)`).  `stringObj` also accepts an interpolated string, capturing its output. |
| `s + v` | Concatenation; `s` and `v` are each a literal, a `string`, or a `stringObj`, in either order (a `stringObj` also takes a `char` on either side: `s + '!'`, `'#' + s`).  Returns a new `stringObj`; neither operand is changed. |
| `s += v` | **`stringObj` only.**  Append in place.  `v` can also be an interpolated string, whose output is appended (`s += $" {n} left";`). |
| `s == v` `s != v` | Content comparison when either side is a `stringObj`; between two `string`s, address comparison. |
| `s < v` `s <= v` `s > v` `s >= v` | Lexicographic ordering by character code. |
| `s =~ v` | Content comparison ignoring case. |
| `s <=> v` | Three-way comparison: negative, `0`, or positive as `s` sorts before, with, or after `v` (§4.6). |
| `switch (s) { case "a": … }` | Content comparison. |
| `s[i]` | The character at position `i`. |
| `s[i] = c` | **`stringObj` only.**  Replace the character at position `i`. |
| `s?` | True when the slot is not `null` (§4.10). |

**Methods (both types)**

A method's string argument can be a literal, a `string`, a `stringObj`, or an `array<char>` (`append`, `prepend`, `contains`, `indexOf`, `startsWith`, `endsWith`, `compareTo`).  `append`, `prepend`, `contains`, `indexOf`, `startsWith`, `endsWith`, `insert`, `replace`, `replaceAll` (its search text), `==`, and `!=` also take a `char`, which stands for the one-character string.

| Method | Returns | Description |
|---|---|---|
| `s.print()` | `void` | Print the text. |
| `s.append(v)` | `stringObj` | `s` followed by a string or a `char`. |
| `s.prepend(v)` | `stringObj` | A string or a `char` followed by `s`. |
| `s.toUpper()` / `s.toLower()` | `stringObj` | Case conversion. |
| `s.trim()` / `s.trimLeft()` / `s.trimRight()` | `stringObj` | Without leading and/or trailing spaces. |
| `s.reverse()` | `stringObj` | Characters in reverse order. |
| `s.mid(start, count)` / `s.left(count)` / `s.right(count)` | `stringObj` | Substrings.  A position or count outside the text is clamped to it: `left(-1)` is empty and `left(99)` is the whole text. |
| `s.insert(pos, src)` | `stringObj` | With `src` inserted at `pos` (a position past the end appends). |
| `s.delete(pos, count)` | `stringObj` | With `count` characters removed at `pos`. |
| `s.replace(search, repl)` / `s.replaceAll(search, repl)` | `stringObj` | With the first / every occurrence replaced. |
| `s.format(pattern[, p1[, p2]])` | `stringObj` | `pattern` with `$0` replaced by `s` and `$1`, `$2` by the arguments, each a `string` or `stringObj`. |
| `s.length` | `int` | Number of characters. |
| `s.compareTo(other[, caseInsensitive])` | `int` | `-1`, `0`, or `1`. |
| `s.equals(other)` | `bool` | True when both hold the same text.  Between two `string`s this is the content comparison `==` doesn't make. |
| `s.indexOf(search)` | `int` | Position of the first occurrence, or `-1`. |
| `s.startsWith(prefix)` / `s.endsWith(suffix)` / `s.contains(search)` | `bool` | Substring tests. |
| `s.isEmpty()` | `bool` | True when the length is `0`. |

**Methods (`stringObj` only)**

| Method | Returns | Description |
|---|---|---|
| `s.capture()` | `void` | Redirect subsequent `print` output into `s`. |
| `s.release()` | `void` | End the capture and restore the previous output target. |
| `s.captureOutput(obj, prop)` | `void` | Capture the printing of `obj.prop` (a string or a routine) into `s`. |

**Example**

```bgl
#include <string>

void Main() {
    bglInit();
    string    title = "Cloak";
    stringObj name;
    name = title;                        // copies the text into name's buffer
    name = name + " of Darkness";        // a new stringObj, assigned back
    name += "!";
    if (name.startsWith("Cloak")) print(name.toUpper());   // -> CLOAK OF DARKNESS!
}
```

**Storage**

A `stringObj` always holds a string, so `null` can't be stored in one or passed to a `stringObj` parameter; both are compile-time errors.  A `ref stringObj` can be bound to null (`ref stringObj r := null;`), null can be passed to a `ref stringObj` parameter, and a function returning `stringObj` can return null; for those, `??` gives a fallback:

<!-- doctest: skip -->
```bgl
stringObj s = null;                  // error
ref stringObj r := null;             // fine: r refers to no string
print(r ?? "none");                  // none
```

Each string's characters live in a block of the string heap, a single array of `stringHeapSize` bytes (default `5120`).  A new string starts with room for 16 characters; writing more grows its block by half again.  A block which can't grow in place is copied to a new one, so for that moment the heap holds both.  There is no per-string limit beyond the heap's room.

Text captured by assigning an interpolated string to a `stringObj`, or by `capture()`, is written into the string's block as it is printed.  On the Z-machine the block is first grown to `stringCaptureSize` characters (default `512`), and the room the text didn't use is given back when the capture ends; a block which is already larger keeps its size.  Captured text longer than the room is cut short to it, and the program runs on.  On Glulx the rest is simply not written.  On the Z-machine it has already been written past the block:  where the heap's free end lay there for the whole capture nothing is lost but that text, and anywhere else it has overwritten memory in use, so the program stops with a run-time error naming the setting.  A literal assigned to a `stringObj` can use all of the heap's free room, and text built with `+=` and the other operators has no such limit.

A `string` (a literal) longer than `stringCaptureSize` can be printed and assigned to a `stringObj`; however, other operations on it (`length`, comparisons, `+`) print it into a temporary buffer of that size.  On the Z-machine they will stop with the above run-time error, and on Glulx they will see only its first `stringCaptureSize` characters.  Simply copy it into a `stringObj` first.

Two temporary buffers of `stringCaptureSize` characters are taken from the heap when the program starts, so the heap needs a bit over twice that before any string is stored.

When the heap is full, it is compacted.  If there is still no room, the string takes what room is left and the rest of the text is dropped; the program runs on, and a string made while the heap is full will be empty.  A Z-machine capture is the exception:  it can't be held to less than `stringCaptureSize` characters, so with no room for one the program stops.  Each capture under way keeps `stringCaptureSize` characters in use.

In a debug build (§20.3), text that is cut short prints a run-time warning naming the setting to raise; a release build cuts it short silently.

**Settings**

Both sizes are settings which `<string>` declares (§15.4).  Left unset or at `0`, they take the above defaults:

```bgl
#beguilerSettings { stringHeapSize = 8192; stringCaptureSize = 1024; }
#include <string>
```

In a debug build of a program with a library binding, the `heap` verb also reports the string heap (§20.3).

The string objects themselves come from a pool of `bglStringPoolReserve` (default `10`); every live `stringObj` occupies one, including each `stringObj` member of each object instance.  Some operations also borrow working copies for as long as they run:  `toUpper()`/`toLower()` two, `format()` up to four.  Exhausting the pool stops the program with a runtime error (§19).  The reserve is an I6 constant, set before the include:

```bgl
#i6 { Constant bglStringPoolReserve 32; }
#include <string>
```

> ***Note:** `<string>` requires `bglInit()`.  With it included, `print()` accepts a literal, a `stringObj`, a buffer, or a routine (which it runs).  As an array element type, a `stringObj` slot allocates on first write and is released when the element is dropped (§12.10).*

**See also** §2.2, §12.10, §21.2, §22.2.

## 22.4 `<array>`

**Purpose.**  Searching, mutation, deque, and sort operations on `array<T>`.

**Include**

```bgl
#include <array>
```

Subscripting, `size`, `length`, and assignment need no include (§12.3, §12.5); the methods below do.

Methods which take an element (`indexOf`, `contains`, `append`, …) are type-checked against `T`:  an argument of an incompatible type is a compile-time error.  Where a method needs an operation of `T` (equality, ordering, assignment, or release), it uses the one `T` publishes, or plain word semantics when it publishes none (§12.10).

**Methods**

| Method | Returns | Description |
|---|---|---|
| `isTracked()` | `bool` | True for a Beguile-declared array with length tracking; false for an I6-native `extern` array. |
| `copyFrom(src)` | `void` | Copy `src`'s elements and length into this array's own storage, up to `size`; elements of an owning type are copied through their `operator =`. |
| `indexOf(item)` / `find(item)` | `int` | First index of `item` in `0..length-1`, or `-1`. |
| `contains(item)` | `bool` | True when `item` is present. |
| `clear()` | `void` | Zero every slot up to `size`, releasing owned elements, and set the length to `0`. |
| `swap(pos1, pos2)` | `void` | Exchange two elements.  Indices must be in range. |
| `reverse()` | `void` | Reverse the elements `0..length-1` in place. |
| `append(item)` | `bool` | Add at position `length`.  False when the array is full. |
| `prepend(item)` | `bool` | Insert at position `0`, shifting the rest right.  False when full. |
| `insert(pos, item)` | `bool` | Insert at `pos` (`0..length`), shifting the rest right.  False when full or `pos` is out of range. |
| `remove(pos)` | `void` | Remove the element at `pos`, shifting the rest left.  Out of range: no effect. |
| `removeValue(item)` | `void` | Remove every element equal to `item`. |
| `arr = { a, b }` | `void` | Replace the contents: `clear()`, then append each element of the list. |
| `arr += item` / `arr += { a, b }` | `void` | Append one element, or each element of a list. |
| `arr -= item` / `arr -= { a, b }` | `void` | `removeValue` for one element, or for each element of a list. |
| `push(item)` | `void` | Insert at the front (position `0`). |
| `pop()` | `T` | Remove and return the front element; `0` when empty. |
| `peek()` | `T` | The front element without removing it; `0` when empty. |
| `enqueue(item)` | `void` | Add at the back. |
| `dequeue()` | `T` | Remove and return the front element; `0` when empty. |
| `peekEnd()` | `T` | The back element without removing it; `0` when empty. |
| `popEnd()` | `T` | Remove and return the back element; `0` when empty. |
| `sort()` | `void` | Sort `0..length-1` ascending in place, by `T`'s ordering operator or, when it has none, by signed word value. |
| `sort(compare)` | `void` | Sort with a comparator `func<int, T, T>` returning `-1`, `0`, or `1`. |

The sort is stable.

**Example**

```bgl
#include <array>
array<int> scores[8];

void Main() {
    bglInit();
    scores += { 40, 10, 30 };
    scores.append(20);
    scores.sort();                       // 10 20 30 40
    print(scores.indexOf(30));           // -> 2
    scores.sort((int a, int b) => b - a);   // descending
    int top = scores.pop();              // 40
}
```

**See also** §12.3, §12.10, §22.5.

## 22.5 `<linq>`

**Purpose.**  Chainable, LINQ-style transformations on `array<T>`.  An `array<char>` doesn't take them (a compile-time error).

**Include**

```bgl
#include <linq>          // includes <array>
```

Operations are either *non-terminals*, which return a typed array and can be chained further, or *terminals*, which reduce a chain to one value.  Calling a chain operation without `<linq>` is a compile-time error.

| Method | Returns | Description |
|---|---|---|
| `filter(pred)` | `array<T>` | The elements for which `pred(elem)` is true.  `pred` is `func<bool, T>`. |
| `map(f)` | `array<var>` | `f(elem)` for each element.  `f` is `func<var, T>`.  A mapper returning a struct with fields is a compile-time error. |
| `take(n)` / `skip(n)` | `array<T>` | The first `n` elements / all but the first `n`.  `n` is limited to `0..length`. |
| `takeWhile(pred)` / `skipWhile(pred)` | `array<T>` | Elements up to the first for which `pred` is false / from that element on. |
| `distinct()` | `array<T>` | The first occurrence of each value. |
| `orderBy()` | `array<T>` | A sorted copy, by `T`'s ordering (its `<=>`), or by signed word value when `T` has none; the source is unchanged. |
| `orderBy(compare)` | `array<T>` | A sorted copy using `func<int, T, T>`. |
| `first()` / `last()` | `T` | The first / last element; `0` when empty, or a run-time error for a struct, which has no `0`. |
| `count()` | `int` | The same as `length`. |
| `any(pred)` | `bool` | True when some element satisfies `pred`; false on an empty array. |
| `all(pred)` | `bool` | True when every element satisfies `pred`; true on an empty array. |

**Chain results.**  Each non-terminal returns a new array from the heap (§12.6), with the capacity of the array it was given.  Intermediate results are freed as the chain runs; the final result is freed like any other array once nothing refers to it.

**Struct elements.**  How a result over an array of a struct shares the source's instances, and when they are copied, is covered in §12.10.1.

**Nesting.**  A chain can run inside another chain's predicate or mapper, to any depth:  `arr.filter((Room r) => r.exits.any(isOpen))`.

**Example**

```bgl
#include <linq>
array<int> nums = { 3, -1, 4, -1, 5 };

void Main() {
    bglInit();
    array<int> positives = nums.filter((int x) => x > 0).distinct();   // captured: 3 4 5
    print(nums.map((var y) => y * 2).first());                          // -> 6
}
```

**Settings**

Results come from the heap, sized by `heapSize` (§15.4).

> ***Note:** `orderBy()` with no comparator orders by `T`'s `operator <=>` when it publishes one (a `string` by its text), and otherwise by signed word value; pass `compare` for any other order.  `distinct()` matches elements through `T`'s `operator ==` when it has one.*

**See also** §4.14, §12.6, §15.4, §22.4.

## 22.6 `<ui>`

**Purpose.**  Single-key input and cursor placement, on both targets, through `bgl.ui`.

**Include**

```bgl
#include <ui>
```

The extension adds three members to `bgl.ui`:

| Member | Returns | Description |
|---|---|---|
| `bgl.ui.waitForKey([separator])` | `char` | Wait for a key press and return it.  When `separator` (a `string`) is given, it is printed, followed by a blank line, before waiting. |
| `bgl.ui.hideCursor()` | `int` | Move the text cursor out of the prose ahead of a key read: to the last cell of the status window when `bgl.ui.statusBar.height` is positive.  Returns the window the caller should request input on (Glulx), or `0` when the main window should be used. |
| `bgl.ui.showCursor()` | `void` | Undo `hideCursor()`.  On the Z-machine, returns output to the main window; on Glulx there is nothing to undo. |

`waitForKey()` calls `hideCursor()` and `showCursor()` itself.

**Example**

```bgl
#include <ui>

void Main() {
    char k = bgl.ui.waitForKey("[Press any key]");
    if (k == 'q') return;
}
```

> ***Note:** `<ui>` does not require `bglInit()`.*

> ***[Glulx]:** `waitForKey()` requests a character event on the status window when `statusBar.height` is positive and `statusBar.id` is a real window, and otherwise on `bgl.ui.mainWin.id`; it discards other events until the key arrives.  `mainWin.id` must therefore hold the main window's handle, which a binding provides (§23.3.8).*

**See also** §21.10, §23.3.8.

## 22.7 `<glulxWindow>`

**Purpose.**  Typed access to the Glk window tree:  window types with compile-time subtype safety, splitting, sizing, image drawing, styles, and colors.

**Include**

```bgl
#include <glulxWindow>   // includes <glulxImage> when generateBlorb is true
```

> ***[Glulx]:** This extension is Glulx-only.  Including it in a Z-machine build is a compile-time error; in a source shared between targets, guard the include and all window code with `#if TARGET_GLULX`.*

Glk arranges the screen as a binary tree of windows.  A window is never resized directly; instead, an existing window is *split* to create a child.  The extension models each window as a reference-semantic object (§21.5.8), and a graphics-only or grid-only operation on the wrong kind of window is a compile-time error.

### 22.7.1 Window Types

The extension provides a base `window` type and one type for each kind of window:

| Type | Kind | Members beyond `window` |
|---|---|---|
| `window` | base | `id`, `width`, `height`, `close()`, the split family (§22.7.3), the move family (§22.7.4), `childStyles` (§22.7.7), `measureStyle()`, `styleHonored()`, `checkStyle()`, `distinguishes()` (§22.7.9), `supports` (§22.7.12) |
| `textBufferWindow` | scrolling prose | `drawImage(img, align, …)` (§22.7.5), `setStyle()`, `clearStyle()` (§22.7.7), `print()`, `printLine()`, `clear()` (§22.7.11) |
| `textGridWindow` | fixed character grid | `moveCursor(col, line)` (§22.7.6), `setStyle()`, `clearStyle()`, `print()`, `printLine()`, `clear()` |
| `graphicsWindow` | pixels | `drawImage(img, x, y, …)`, `setBackgroundColor(color)` |

The types are also reachable as `bgl.glulx.window`, `bgl.glulx.textBufferWindow`, `bgl.glulx.textGridWindow`, and `bgl.glulx.graphicsWindow`.  Windows are not world-tree objects:  they have no `parent`, `children`, or attributes.

Child windows are pooled:  at most 8 text-buffer, 8 text-grid, and 8 graphics windows can exist at once (§22.7.3).

### 22.7.2 Roots

The objects `bgl.ui.mainWin` and `bgl.ui.statusBar` (§21.10) are the roots of the tree; the extension adds the window API to those same objects.

| Object | Window kind | Added by this extension |
|---|---|---|
| `bgl.ui.mainWin` | text buffer | `width`, `height`, the split family, `childStyles`, `drawImage()`, `setStyle()`, `clearStyle()`, `print()`, `printLine()`, `clear()`, `supports` |
| `bgl.ui.statusBar` | text grid | `width`, the split family, `childStyles`, `setStyle()`, `clearStyle()`, `print()`, `clear()`, `moveCursor()`, `supports` (`height` is already there, §21.10, §23.3.8) |
| `bgl.ui.screen` | not a window | `setStyle()`, `clearStyle()` for both text window types at once (§22.7.7) |

The roots are objects, not `window` instances:  `close()`, the move family, `measureStyle()`, `styleHonored()`, `checkStyle()`, and `distinguishes()` are not available on them.

### 22.7.3 Splitting

**Syntax**

```syntax
VIEW = WIN.splitDirectionKind( SIZE [ , SCALE [ , BORDER ] ] [ , styles: SHEET ] ) ;
```

`Direction` is `Up`, `Down`, `Left`, or `Right`; `Kind` is `Grid`, `Graphics`, or `Buffer`.  Every combination exists, spelled as one method name (`splitUpGrid`, `splitLeftGraphics`, …).  `SIZE` is a number of lines (grid, buffer) or pixels (graphics) when `SCALE` is `fixed` (the default), or a percentage when it is `proportional`.  `BORDER` is `border` or `noBorder` (the default).  `SHEET` is a `styleSheet` which gives the new window its own look (§22.7.7).  Any window, root or child, can be split.

A split beyond the pool (§22.7.1) fails as `new` does (§4.13), and so does a split the interpreter refuses (e.g., a graphics window where it has no graphics) or one from a window which isn't open (a status bar never given a height).  In each case the result is `null`.

The kind fixes the returned type; the direction fixes its *orientation view*:

| Direction | Returned type |
|---|---|
| `Up`, `Down` | `textGridWindowHorz`, `graphicsWindowHorz` or `textBufferWindowHorz` |
| `Left`, `Right` | `textGridWindowVert`, `graphicsWindowVert` or `textBufferWindowVert` |

An orientation view is an `alias class` of the content type (§8.2.4) which hides the assignment to the axis a window of that orientation can't resize (§22.7.4).  Holding the result with `auto` keeps the view; holding it as the plain content type (`textGridWindow hud = …`), or casting to it, drops to the permissive surface.

**Example**

```bgl
#include <glulxWindow>
#using bgl.glulx;

void Main() {
    auto hud  = bgl.ui.mainWin.splitUpGrid(3);                        // textGridWindowHorz
    auto pic  = bgl.ui.mainWin.splitLeftGraphics(40);                 // graphicsWindowVert
    auto band = bgl.ui.statusBar.splitRightGraphics(20, proportional); // 20% wide
}
```

### 22.7.4 Sizing and Re-arrangement

**Syntax**

```syntax
WIN.width
WIN.height
WIN.width = N ;
WIN.height = N ;
WIN.moveDirection( SIZE [ , SCALE [ , BORDER ] ] ) ;
```

`Direction` is `Up`, `Down`, `Left`, or `Right`, spelled as one method name (`moveUp`, …).

`width` and `height` can be read on every window and root; reading queries the live window.  Writing re-arranges the window within its parent pair.  Only the axis which a window was split *along* can be written (`height` for an `Up`/`Down` split, `width` for a `Left`/`Right` split).  This is enforced at two levels:

- **At compile time**, when the window is held as its orientation view:  a `…Horz` view hides `width.operator =`, a `…Vert` view hides `height.operator =`, and writing the hidden axis is a compile-time error.
- **At run time**, on the permissive surface (a content-typed or `window`-typed value, or a cast):  writing the fixed axis has no effect, and reports it through `bgl.output.log()` (§21.4).

`moveUp`, `moveDown`, `moveLeft`, and `moveRight` re-arrange an *existing* child window within its parent pair, changing its placement and size.  A window split `Up` or `Down` moves only up or down, and one split `Left` or `Right` only left or right.  A `…Horz` view hides `moveLeft` and `moveRight`, a `…Vert` view hides `moveUp` and `moveDown`, and on the permissive surface a move across the axis does nothing and reports a programming error.  They are not available on the roots.

**Example**

<!-- doctest: skip -->
```bgl
auto hud = bgl.ui.mainWin.splitUpGrid(3);
hud.height = 5;                      // the split axis
// hud.width = 40;                   // compile-time error: hidden on textGridWindowHorz
((textGridWindow)hud).width = 40;    // permissive surface: no effect at run time
bgl.ui.statusBar.height = 2;         // roots are writable on both axes
```

### 22.7.5 Images

**Syntax**

```syntax
GRAPHICS-WIN.drawImage( IMAGE [ , X [ , Y [ , WIDTH [ , HEIGHT ] ] ] ] ) ;
TEXT-BUFFER-WIN.drawImage( IMAGE [ , ALIGN [ , WIDTH [ , HEIGHT ] ] ] ) ;
GRAPHICS-WIN.setBackgroundColor( COLOR ) ;
```

These methods exist only when `generateBlorb` is true (§15.6).  `IMAGE` is an `eImages` value; a `glulxImage` (§22.8) converts to one, and a raw resource id is passed as `(eImages)id`.  Every other parameter is optional and can be passed by name, as with the `img` print rule (§21.11).  A graphics window draws at pixel position `(x, y)`, default `(0, 0)`; a text-buffer window (including `bgl.ui.mainWin`) draws inline with an `eGlulxImageAlign` (default `inlineCenter`).  When both `width` and `height` are `0`, the image is drawn at its natural size; when one is given, the other is computed to preserve the aspect ratio; when both are given, they are used as is.

`setBackgroundColor(color)` sets a graphics window's background to an `$RRGGBB` value and clears the window to it.

**Example**

<!-- doctest: skip -->
```bgl
pic.drawImage(eImages.coverArt);                  // natural size, at the top-left
pic.drawImage(eImages.coverArt, width: 100);      // width 100, height to match
bgl.ui.mainWin.drawImage(eImages.icon, eGlulxImageAlign.marginLeft, 48, 48);
```

### 22.7.6 Cursor and Lifecycle

Closing a window returns it to its class's pool (§8.2.6).  The windows split from it stay open:  the window beside it takes over its space.

After a restore, undo or restart, `bgl.ui.recoverWindows()` (§21.10) brings Glk's windows back in line with the program's.  A window the program has open and Glk still has is kept as it is.  A window Glk has which the program has closed, or never opened, is closed.  A window the program has open which Glk no longer has is opened again from the window it was split from, with its split, its size and that window's `childStyles` (not a sheet passed to the split); it comes back empty, since Glk can't restore what a window showed.  Windows split from a closed window are opened again from the window it was split from.

**Methods**

| Member | On | Description |
|---|---|---|
| `win.moveCursor(col, line)` | `textGridWindow`, `bgl.ui.statusBar` | Place the cursor at column `col`, line `line`, both counted from 0. |
| `win.close()` | any child window | Close the window and return it to the pool.  Closing it again has no effect until another window is split, which can reuse its slot. |

**Example**

<!-- doctest: skip -->
```bgl
auto hud = bgl.ui.mainWin.splitUpGrid(3);   // textGridWindowHorz
hud.moveCursor(0, 0);                       // the top-left cell
hud.close();                                // hud is gone; its slot is free for the next split
```

### 22.7.7 Styles

**Syntax**

```syntax
TARGET.setStyle( STYLE-TYPE , style { MEMBER = VALUE ; … } ) ;
TARGET.clearStyle( STYLE-TYPE ) ;
```

Glk styles are hints set per window kind and style type, and a hint affects only windows of that kind created *after* it is set, never those already open.  `TARGET` is `bgl.ui.screen` (both text kinds at once), `bgl.ui.mainWin` or a `textBufferWindow` (the text-buffer kind), or `bgl.ui.statusBar` or a `textGridWindow` (the text-grid kind).  `STYLE-TYPE` is an `eGlulxStyleType` (§22.7.10).

`style` is a struct whose members default to "leave the interpreter's setting".  Build one with the named inline-object form (§11.3.1), giving only the members to change.  `clearStyle()` resets every hint of that style type to the interpreter's default.

| `style` member | Type | Meaning |
|---|---|---|
| `justify` | `int` | An `eGlulxJustify` value, cast to `int`. |
| `indentation` | `int` | Left-margin indent. |
| `paragraphIndentation` | `int` | Additional first-line indent. |
| `sizeAdjustment` | `int` | Text size relative to the base size (`…, -1, 0, 1, …`). |
| `fontWeight` | `int` | `-1` lighter, `0` normal, `1` bold. |
| `italics` | `styleFlag` | Italic or oblique text. |
| `fixedWidth` | `styleFlag` | Fixed-pitch font. |
| `foreColor` | `int` | Text color, `$RRGGBB`. |
| `backColor` | `int` | Background color, `$RRGGBB`. |
| `reverse` | `styleFlag` | Swap foreground and background. |

The members `italics`, `fixedWidth`, and `reverse` are `styleFlag`s:  they take `true` or `false`, and stay unset (`styleUnset`, §22.7.9) until given one.

**Example**

<!-- doctest: skip -->
```bgl
bgl.ui.screen.setStyle(eGlulxStyleType.normal, style { backColor = $111111; foreColor = $cccccc; });
bgl.ui.screen.setStyle(eGlulxStyleType.header, style { fontWeight = 1; justify = (int)eGlulxJustify.centered; });
hud.setStyle(eGlulxStyleType.alert, style { reverse = true; foreColor = $ff0000; });
hud.clearStyle(eGlulxStyleType.alert);
```

**A look for one window**

`setStyle()` changes a style type for every later window of that kind.  To give one window its own look, describe it as a `styleSheet`, a struct with one `style` member per style type (`normal`, `emphasized`, `fixed`, `header`, `subheader`, `alert`, `note`, `blockQuote`, `input`, `user1`, `user2`), and pass it to the split as `styles:`.  No other window is affected.  A style left unset in the sheet leaves that style type as it was.

A window's `childStyles` is a sheet which every window split *directly* from it gets.  It refers to the sheet, so changing the sheet later changes what the next split gets; assigning `null` removes it.  A sheet passed to the split takes precedence over the parent's `childStyles`, style type by style type, and both take precedence over `setStyle()`.

Hints are read only when a window opens:  neither `styles:` nor `childStyles` restyles a window that is already open.

**Example**

<!-- doctest: glulx -->
```bgl
#include <glulxWindow>
#using bgl.glulx;

styleSheet panelLook = styleSheet{
    alert      = style{ fontWeight = 1; foreColor = $C80000; };
    emphasized = style{ italics = true; };
};
styleSheet logLook = styleSheet{ normal = style{ fixedWidth = true; }; };

void Main() {
    bgl.ui.mainWin.childStyles = panelLook;
    auto notes = bgl.ui.mainWin.splitRightBuffer(25);                  // panelLook
    auto log   = bgl.ui.mainWin.splitDownBuffer(5, styles: logLook);   // logLook, then panelLook for the rest
    notes.childStyles = bgl.ui.mainWin.childStyles;                    // notes' children look the same
    print(notes != null && log != null);   // -> true
}
```

### 22.7.8 Colors

`bgl.glulx.color` provides `$RRGGBB` values:  `color.rgb(r, g, b)` composes one from `0..255` channels, and the members `black`, `white`, `red`, `green`, `blue`, `yellow`, `cyan`, `magenta`, `gray`, `lightGray`, and `darkGray` are ready-made values.  All are plain `int`s, accepted wherever a color is.

**Example**

<!-- doctest: skip -->
```bgl
#using bgl.glulx;
pic.setBackgroundColor(color.rgb(20, 30, 40));
bgl.ui.screen.setStyle(eGlulxStyleType.normal, style { foreColor = color.white; });
```

### 22.7.9 Style Validation

Interpreters can ignore style hints, and many can't report what they use.  On a child window, `measureStyle(styleType, hint)` returns the value the interpreter actually uses for that style and hint, or `styleUnset`, a `const int`, when it reports none (`fixedWidth` reads `1` for fixed-pitch, as it is set); `styleHonored(styleType, hint)` is true when the interpreter reports the hint at all.  `hint` is an `eGlulxStyleHint`, whose members are named after the `style` members.

`checkStyle(styleType, s)` compares every hint `s` sets with what the interpreter reports and returns an `eStyleCheck`:  `applied` when every reported hint matches, `differs` when at least one reported hint does not, and `unknown` when the interpreter reports none of them.  Interpreters built on Quixe (Lectrote, Parchment) report no hints, so there the answer is `unknown`.  The enum also reads as a `bool` three ways:  `r.known` (it could report), `r.isApplied`, and `r.isDiffering`.

`distinguishes(a, b)` is true when the interpreter shows style types `a` and `b` differently in that window; it is false when they look the same or it can't tell.

All of these query the live window.

**Example**

<!-- doctest: skip -->
```bgl
if (!hud.styleHonored(eGlulxStyleType.alert, eGlulxStyleHint.reverse)) { /* fall back */ }
int fg = hud.measureStyle(eGlulxStyleType.normal, eGlulxStyleHint.foreColor);

style warn = style{ fontWeight = 1; foreColor = $C80000; };
eStyleCheck r = hud.checkStyle(eGlulxStyleType.alert, warn);
bool useMarkers = r.isDiffering || !hud.distinguishes(eGlulxStyleType.normal, eGlulxStyleType.alert);
```

### 22.7.10 Enums

The following enums come from the Glulx core or from this extension.  Each core enum is also reachable through a short alias under `bgl.glulx` (§21.3); the alias names the type, and members can be written through either name (`bgl.glulx.eWinType.textGrid` or `eGlulxWindowType.textGrid`).

| Enum or bnum | Values | Provided by | `bgl.glulx` alias |
|---|---|---|---|
| `eGlulxWindowType` | `textBuffer`, `textGrid`, `graphics` | core | `eWinType` |
| `bGlulxWindowPlacement` | `left`, `right`, `above`, `below` | core | `bWinPlacement` |
| `bGlulxWindowScale` | `fixed`, `proportional` | core | `bWinScale` |
| `bGlulxWindowBorder` | `border`, `noBorder` | core | `bWinBorder` |
| `eGlulxImageAlign` | `inlineUp`, `inlineDown`, `inlineCenter`, `marginLeft`, `marginRight` | core | `eImgAlign` |
| `eGlulxImageDimension` | `original`, `scaled` | core | `eImgDimension` |
| `eGlulxStyleType` | `normal`, `emphasized`, `fixed`, `header`, `subheader`, `alert`, `note`, `blockQuote`, `input`, `user1`, `user2` | core | `eStyleType` |
| `eGlulxJustify` | `left`, `full`, `centered`, `right` | `<glulxWindow>` | - |
| `eGlulxStyleHint` | `indentation`, `paragraphIndentation`, `justify`, `sizeAdjustment`, `fontWeight`, `italics`, `fixedWidth`, `foreColor`, `backColor`, `reverse` | `<glulxWindow>` | - |
| `eStyleCheck` | `unknown`, `applied`, `differs`; emitter values `known`, `isApplied`, `isDiffering` | `<glulxWindow>` | - |
| `eCharOutput` | `none`, `approximate`, `exact` | `<glulxWindow>` | - |

### 22.7.11 Window Output

**Syntax**

```syntax
WIN.print( VALUE ) ;
WIN.printLine( [ VALUE ] ) ;
WIN.clear() ;
```

`print` and `printLine` write to one window and then return output to wherever it was going.  They print a value exactly as `print()` and `printLine()` do (§21.4).  They are members of the text windows and of `bgl.ui.mainWin` (`bgl.ui.statusBar` has `print`, `clear`, and `moveCursor`); writing to a graphics window is a compile-time error.  `clear()` empties the window.

**Example**

<!-- doctest: glulx -->
```bgl
#include <glulxWindow>

void Main() {
    auto panel = bgl.ui.mainWin.splitRightBuffer(25);
    panel?.printLine("Rooms seen:");
    panel?.printLine($"  {1 + 1} so far");
    panel?.clear();
    print("main");   // -> main
}
```

### 22.7.12 Capabilities

`bgl.glulx.supports` answers what the interpreter can do.  Each member is a property, read without parentheses:  `version` (the Glk version, `0xMMmmrr`), and the `bool`s `timer`, `graphics`, `graphicsTransparency`, `sound`, `soundMusic`, `soundVolume`, `soundNotify`, `hyperlinks`, `unicode`, `dateTime`, `lineTerminators`, and `resourceStream`.  The ones which take an argument are routines:  `charOutput(c)` returns an `eCharOutput` (`none`, `approximate`, or `exact`), `charInput(c)` is true when the player can type `c`, and `images(t)`, `mouse(t)`, and `hyperlinkInput(t)` ask about windows of type `t` (an `eGlulxWindowType`).

Every window, and both roots, also has a `supports` for its own kind:  `win.supports.images`, `win.supports.mouse`, and `win.supports.hyperlinks`.

**Example**

<!-- doctest: glulx -->
```bgl
#include <glulxWindow>

void Main() {
    if (bgl.glulx.supports.timer) print("timer ");
    bool canShow = bgl.glulx.supports.charOutput('e') != eCharOutput.none;
    print(canShow);                                   // -> true
    print(bgl.ui.mainWin.supports.hyperlinks || true);   // -> true
}
```

**See also** §8.2.4, §8.7.4, §11.3.1, §15.6, §21.4, §21.10, §22.8, §23.3.8.

## 22.8 `<glulxImage>`

**Purpose.**  A typed handle to a Glulx image resource which answers "how big is this picture?".

**Include**

```bgl
#include <glulxImage>
```

> ***[Glulx]:** Glulx-only; including it in a Z-machine build is a compile-time error.*

`glulxImage` is a primitive class over the resource id (§8.2.5).  It converts implicitly to `eImages`, is assigned from an `eImages` value or a raw `int` id, and converts to `int` with `(int)img`.

| Member | Returns | Description |
|---|---|---|
| `img.width` / `img.height` | `int` | Natural pixel dimensions. |
| `img.size` | `bglSize` | Both dimensions (§21.12). |

The same three are added to the `eImages` enum (§7.8), so `eImages.logo.width` works without a handle.

**Example**

<!-- doctest: skip -->
```bgl
#include <glulxImage>

void Main() {
    glulxImage cover = eImages.coverArt;
    int w = cover.width;
    pic.drawImage(cover, 0, 0, cover.width / 2);
}
```

> ***Note:** `<glulxImage>` does not require `bglInit()`.  It does not require `generateBlorb` either; however, without packaged assets there are no images to measure.*

**See also** §15.6, §21.12, §22.7.

## 22.9 Library-specific Extensions

**Purpose.**  Lets an extension change what the standard library does inside its own routines without replacing those routines.  Each hook is a call that a patch (§14.1.6) merges into the library.  An object defining the hook's method answers it.

**Include**

```syntax
#include <extensions/stdlib/hooks>               // every group
#include <extensions/stdlib/hooks/GROUP>       // one group
```

The hooks come in seven groups, one per area of the library.  Each group is its own extension, with its own patch.  Each group includes `<bindings/i6StandardLibrary>`, and the groups can be included in any combination and order.

| Group | Routines it hooks | Hooks |
|---|---|---|
| `informLibrary` | `InformLibrary.play` (the play loop) | `ext_afterGamePrologueNotify`, `ext_playLoopRewriteParsedCommands`, `ext_playLoopAssignObjectVariables`, `ext_playLoopPerformSingleObjectAction`, `ext_playLoopPerformMultiObjectAction` |
| `keyboard` | `KeyboardPrimitive`, `Keyboard` | `ext_keyboardPrimitiveNotify`, `ext_keyboardPrimitive`, `ext_keyboardGetInput`, `ext_beforePrompt`, `ext_keyboardPrepInput`, `ext_keyboardHandleComment`, `ext_keyboardHandleUndo`, `ext_keyboardHandleOops` |
| `parser` | `Parser__parse`, `Adjudicate`, `R_Process` | `ext_forceLookAhead`, `ext_suppressParsingImplicitTake`, `ext_adjudicate`, `ext_redirectActionNotify` |
| `standardLibrary` | `Identical`, `NounWord`, `NextWord`, `AfterRoutines`, `DisplayStatus`, `ActionPrimitive` | `ext_identical`, `ext_nounWord`, `ext_nextWord`, `ext_beforeAction`, `ext_afterAction`, `ext_afterRoutines`, `ext_displayStatus` |
| `verbs` | `PlayerTo`, `GoSub`, `LookSub`, `ExamineSub` | `ext_lookSub`, `ext_examineSub`, `ext_preDescription`, `ext_postDescription`, `ext_goSub`, `ext_goSubNoDir`, `ext_playerToNotify` |
| `writeAfterEntry` | `WriteAfterEntry` | `ext_sdPartInvBit`, `ext_sdFullInvBit`, `ext_sdConcealBit`, `ext_sdAlwaysBit`, `ext_sdRecurseBit`, `ext_sdIsOrAre`, `ext_sdNewLineBit`, `ext_sdDoList` |
| `writeList` | `WriteListR` | `ext_forceSingular`, `ext_wlrPluralMany` |

Each hook is an `extern property`, documented where its group declares it.  The `verbs` group also declares `orSuppressNewlineAfterDescription`.  The `writeAfterEntry` group also declares `_WAE_recurse_flag`, `_WAE_parenth_flag`, `_WAE_child_count`, and `_WAE_eldest_child`, which describe the entry being written.

A **library extension** is an object whose parent is the library's `LibraryExtensions` object; it answers a hook by defining a method with the hook's name.  Extensions are asked in the order they are declared:

- A hook which returns a value is asked of each extension in turn until one answers.  False (or 0) lets the library carry on as it would without the hook.  `ext_identical` is the exception:  true leaves it to the library, and false says the two objects differ.
- A hook whose name ends in `Notify`, and `ext_afterAction`, `ext_preDescription`, and `ext_postDescription`, are only told.  Every extension is told, and what it returns is ignored.
- A `keyboard` hook can also return -1, which starts reading the command again.

**Example**

<!-- doctest: compile -->
```bgl
#include <extensions/stdlib/hooks/verbs>

object darkHooks {
    parent = LibraryExtensions;
    bool ext_lookSub(){ print("It's too dark to make anything out.^"); rtrue; }
}
```

> ***Note:** The hooks exist only for the standard library, and the patches are made against version 6.12.8 (§23.2).  A patch which no longer matches the library is a compile-time error (§14.1.6).*

> ***Note:** An included group costs space even when nothing answers it.  On the Z-machine each group adds between 88 and 344 bytes; all seven add about 1.7 KB.*

> ***Note:** A group defines a hook's property only when Inform 6 code has not already defined it, and an Inform 6 object in `LibraryExtensions` answers a hook just as a Beguile one does.*

**See also** §14.1.6, §16.4, §16.6.

---

# 23 IF Library Bindings

## 23.1 What a Binding Is

A *binding* is a Beguile source file in `beguiLib/bindings/` which exposes one external Inform 6 library to Beguile's type system.  It declares the library's attributes, globals, constants, routines, and actions as `extern` (§16.4), so your code can name them with type checking, and it declares the library's additive properties.  It also integrates the library with the runtime core:  it declares the story-identity constants the library expects, runs `bglInit()` before the library's own `main`, and routes the core's `bgl.ui` window objects through the library's window handling.

A binding defines no behavior of its own; the library's I6 source is still included with `#includeI6` (§16.5).  Extensions written for one library, such as the standard library hooks, are covered in §22.9.

Bindings are optional.  Different libraries need different bindings and are not used together.

**Syntax**

```bgl
#include <bindings/i6StandardLibrary>
#include <i6StandardLibrary>            // the folder prefix is optional (§14.1.1)
```

## 23.2 Available Bindings

The following bindings are available:

| File | Library | I6 includes it corresponds to | Target notes |
|---|---|---|---|
| `bindings/i6StandardLibrary.bgl` | The Inform 6 standard library, version 6.12 | `parser`, `verblib`, `grammar` | Z-machine and Glulx.  Declares the Glulx window globals (`gg_mainwin`, `gg_statuswin`, …). |
| `bindings/punyInform.bgl` | PunyInform | `globals`, `puny` | Z-machine.  PunyInform uses no classes; objects are distinguished by attributes alone. |

Each binding is `#once`-guarded.

The standard-library binding requires release 6.12 of that library.  Against an earlier release, such as 6/10, it will stop the build with an error naming the release it needs.

**Include order.**  The library's own includes must sit where Inform 6 expects them (§18.6).  For the standard library:

```bgl
#include <bindings/i6StandardLibrary>
#includeI6 "parser"
#includeI6 "verblib"
// classes, objects, Initialise, and any entry point the program defines (NewRoom, PrintTaskName, …)
#includeI6 "grammar"
```

An entry point your program defines must come before `#includeI6 "grammar"`; a later definition is a duplicate routine.  Verbs and `extend` blocks can go anywhere.

PunyInform's own includes are the reverse:  the binding follows them, and the entry points go before them.

<!-- doctest: skip -->
```bgl
// entry points the program defines (NewRoom, DeathMessage, …)
#includeI6 "globals"
#includeI6 "puny"
#include <bindings/punyInform>
```

An entry point of PunyInform (`NewRoom`, `DeathMessage`, `LibraryMessages`, `BeforeParsing`, `ParseNoun`, `ChooseObjects`, `InScope`, `UnknownVerb`, `PrintVerb`, …) must be defined before `#includeI6 "puny"`.  A definition after the include is never called, and the only sign is the I6 warning `Routine "NewRoom" declared but not used`.  The `punyInform` binding declares none of the entry points.

## 23.3 What a Binding Provides

### 23.3.1 Entry Point and `bglInit()`

Both libraries define `Main` themselves and call the program's `Initialise` routine (§6.7), so a program using either binding defines `Initialise`, not `Main`.

Each binding wraps the library's `main` so that `bglInit()` (§21.2) runs before it; you don't call `bglInit()` yourself.

The wrap is performed only when the `autoInitialize` setting (§15.4) is true, its default.  Set `autoInitialize = false` when another I6 extension already replaces `main`; your program must then call `bglInit()` itself.

### 23.3.2 `story` and `headline`

Both libraries read the I6 constants `Story` and `Headline` for the banner.  Each binding declares them from `#beguilerSettings` (§15.7), ahead of the program's `#includeI6` of the library:

| Constant | Taken from |
|---|---|
| `story` | `#beguilerSettings.title` |
| `headline` | `#beguilerSettings.headline` |

A program using a binding must not declare them again.  The `i6StandardLibrary` binding also declares `story` and `headline` as `extern string`, so Beguile code can read them.

### 23.3.3 Capability Flags

Each binding advertises itself with an order-independent symbol (`#declare`, §14.2.3) which any file can test:

| Binding | Symbol |
|---|---|
| `i6StandardLibrary` | `I6_STANDARD_LIBRARY` |
| `punyInform` | `PUNYINFORM` |

```bgl
#if I6_STANDARD_LIBRARY
    // code that relies on the standard library
#endif
```

### 23.3.4 Attributes, Globals, Constants and Routines

A binding declares the following as `extern`:

- the library's **attributes** (`light`, `container`, `scenery`, `static`, …; §21.5.1);
- its **globals**:  game state (`location`, `player`, `actor`, `score`, `turns`, …) and parser results (`noun`, `second`, `action`, `verb_word`, …).  `action` is declared with type `verb` (§13.3);
- its **constants**:  parser error codes, scope reasons, and color and window constants;
- its **objects**:  `thedark`, `selfobj`, and (standard library only) the compass direction objects `n_obj` … `d_obj`;
- its **routines** (`PlayerTo`, `TestScope`, `StartTimer`, `StatusLineHeight`, …), and, for the standard library, the optional entry points the library calls at defined moments (`AfterLife`, `NewRoom`, `TimePasses`, `InScope`, …) as `extern default` functions (§16.4.1).  PunyInform's entry points are not declared (§23.2).

Beyond the routines it documents, each binding declares every other routine the library defines (in `parser.h`, `verblib.h`, and `english.h`; in PunyInform's core files) as `extern var NAME(var a = 0, …)`, taking the routine's first locals as optional parameters; an argument the call leaves out is `0`.  A member named like any library routine is renamed automatically (§16.9).  A member reached only through `#using` loses to a library routine of the same name, with a warning (§10.4).  A program's own `extern` declaration of a library routine replaces the binding's.

The library's object properties are covered in §23.3.11.

### 23.3.5 Additive Properties

Additive properties belong to the library that defines them, so the binding declares them (§11.7.2).  The `i6StandardLibrary` binding declares `before`, `after`, `life`, `orders`, `describe`, `time_out`, and `each_turn`; the `punyInform` binding declares `before`, `after`, `life`, `describe`, `time_out`, and `each_turn` (`orders` is an ordinary property in PunyInform).  `name` is additive in the Inform 6 compiler itself and needs no binding (§11.7.2).

### 23.3.6 Verbs and Grammar Tokens

A binding declares the library's actions as `extern verb`s with their claimed dictionary words (§13.2.3):

```bgl
extern verb Take { .take|.carry|.hold|.get|.pick|.peel }
extern verb Look { .look|.l }
extern verb Receive { }                 // a fake action: no words or grammar of its own
```

It also declares the `grammarToken` enum (§13.4.1) with the parser's token names (`NOUN`, `HELD`, `CREATURE`, `TOPIC`, `MULTI`, `MULTIHELD`, `MULTIEXCEPT`, `MULTIINSIDE`, `SPECIAL`, `ANYNUMBER`, `NUMBER`, `SCOPE`, `REVERSE`) for use in grammar patterns.

### 23.3.7 Shared Types

Each binding declares `iString`, `iStringOrObject`, and `iStringOrRoutine` (§21.5.10), with `print()` overloads for `iString` and `iStringOrRoutine`.

### 23.3.8 Status Bar and Main Window

Each binding replaces the core's `bgl.ui.statusBar.height` and `bgl.ui.mainWin.id` / `bgl.ui.statusBar.id` (§21.10) with accessors which read and write the library's own status-line state:

| Member | `i6StandardLibrary` | `punyInform` |
|---|---|---|
| `bgl.ui.statusBar.height` read | the library's tracked current height | the library's tracked current height |
| `bgl.ui.statusBar.height` write | the library's status-line-height routine | sets the library's persistent height and splits immediately |
| `bgl.ui.statusBar.id` | the library's status window (Glulx); `0`, read-only (Z-machine) | `0`, read-only |
| `bgl.ui.mainWin.id` | the library's main window | the core's (`0`) |

### 23.3.9 Banner Texts

The `i6StandardLibrary` binding binds the library's banner texts (`INFORMV__TX`, `LIBRARYV__TX`, `LibRelease`) and merges its own banner into the library's `Banner` routine (§14.1.6), one which also names the Beguile version.  A game can change the banner by replacing `Banner` (§6.5).  The `punyInform` binding does the same for PunyInform.

### 23.3.10 Library Options and Debug Builds

Parts of a library exist only when an I6 option constant is set, or only in a debug build.  A binding declares such parts under the symbol which enables them, so a program naming one in a build that lacks it will fail with a Beguile error instead of an Inform 6 one:

| Parts | Declared under |
|---|---|
| Debugging verbs (`tree`, `purloin`, `actions`, …) and debug-only globals and constants | `#if DEBUG` (§20.3) |
| Standard library:  `objects` and `places` | `#if !NO_PLACES` |
| PunyInform routines:  `NumberWord`, `LanguageNumber`, `Achieved` | `#if OPTIONAL_ALLOW_WRITTEN_NUMBERS`, `#if OPTIONAL_LANGUAGE_NUMBER`, `#if OPTIONAL_FULL_SCORE && TASKS_PROVIDED` |
| PunyInform:  `recording`, `replay`, `objects`, and `places` | `#if OPTIONAL_EXTENDED_METAVERBS`; `objects` and `places` also `#if !NO_PLACES` |

You set a library option with `#defineI6` (§14.2.1), before the binding's `#include`:

<!-- doctest: skip -->
```bgl
#defineI6 OPTIONAL_EXTENDED_METAVERBS
#includeI6 @"globals"
#includeI6 "puny"
#include <bindings/punyInform>
```

### 23.3.11 Standard Library Properties

Neither binding declares the library's object properties (`description`, `capacity`, `door_to`, `before`, …) as members of `object`, except `short_name`, which both declare there (§21.4); when an object has both a `short_name` and an `instanceName` (§11.5.4), the library prints `short_name`.  A standard-library property is declared on the class or object providing it, as an ordinary member (§8.3, §11.7.1); declaring one on `extend class object` only gives it a type (§8.7.1).  The member's name is the property's, and its type says which of the library's forms it takes.  Many properties accept either a value or a routine (among them those typed `iString`, `iStringOrObject`, and `iStringOrRoutine`, §21.5.10):  declare the member as a value to give a value, or as a method to give a routine.  A routine the library calls returns what the library expects of it, given below as the method's return type.

| Property | The library uses it to | Declare it as |
|---|---|---|
| `description` | describe the object (`examine`) or room (`look`) | `string description`, `void description()`, or `bool description()`:  true when it printed the whole description (otherwise the library adds its own text) |
| `initial` | describe the object in a room before it is first moved | `string initial` or `void initial()` |
| `when_open`, `when_closed`, `when_on`, `when_off` | describe an openable or switchable object in a room | `string` or a `void` method |
| `inside_description` | describe the inside of an enterable object | `string` or a `void` method |
| `short_name` | print the object's name | `string short_name`, an interpolated string (§2.9.1), or `bool short_name()`:  true when it printed the whole name |
| `article`, `short_name_indef` | print the indefinite article / indefinite name | `string` or a `void` method |
| `plural` | name several identical objects | `string plural` or a `void` method |
| `parse_name` | match the player's words against the object | `int parse_name()`:  the number of words matched, 0 for none, -1 to use `name` |
| `n_to` … `d_to`, `in_to`, `out_to` | the room or door in that direction | `object n_to`, or `var n_to()` returning a room or door to go there, `false` to refuse with the room's `cant_go` (or the library's own message), or `true` after printing its own refusal |
| `cant_go` | refuse movement in a direction with no exit | `string cant_go` or `void cant_go()` |
| `door_to` | the room on the other side of a door | `object door_to` or `object door_to()` |
| `door_dir` | the direction a door leads | `property door_dir` (`door_dir = n_to;`) or `property door_dir()` |
| `with_key` | the key which locks and unlocks the object | `object with_key` |
| `found_in` | the rooms a floating object (a door, scenery) is in | `rawArray<object> found_in`, or `bool found_in()`:  true when it is in `location` |
| `capacity` | how many objects a container or supporter holds | `int capacity` |
| `number`, `time_left` | a general-purpose number; turns left on the object's timer | `int` |
| `daemon` | run every turn after `StartDaemon(obj)` | `void daemon()` |
| `react_before`, `react_after` | intercept any action while the object is in scope | `bool react_before()`:  true to stop the action |
| `invent` | change how the object is listed in an inventory | `bool invent()`:  true when it printed the whole entry |
| `add_to_scope` | bring other objects into scope with this one | `rawArray<object> add_to_scope`, or `void add_to_scope()` calling `AddToScope(obj)` |
| `list_together` | group similar objects in a list | `int list_together` (a group number) or `string list_together`; the library groups objects whose values are the same word, so a string is declared once, on their class, rather than repeated on each object |
| `articles` | articles for a name in a language without inflection | `rawArray<string> articles` |

The binding gives `found_in`, `add_to_scope`, and `articles` their array types, so an object can fill one without naming a type (`found_in = {study, attic};`).

> ***Note:** `null` is the same word as `false`, so a direction routine which returns `null` refuses with `cant_go` too.  A direction property given an interpolated string refuses the move, since the string returns `true` once printed.*

The additive properties (§23.3.5) are written as methods:  `bool before()` returns true to stop the action; `bool describe()`, called when the library lists the object in a room, returns true when it printed the object's paragraph itself, and the library then doesn't list it; the others follow the above table.

**Inventory listings.**  `invent` is called twice for each entry, and `inventory_stage` (1 or 2) says which:  1 before the name is printed, 2 after it, for text following the name.

**Example**

<!-- doctest: compile -->
```bgl
class Room : object { attributes = {light}; string description; }
Room hall {
    "Hall"; description = "A plain hall.";
    object n_to = study;
    var e_to(){ print("A wall.^"); return true; }
    string cant_go = "Not that way.";
}
Room study { "Study"; description = "A study."; object s_to = hall; object u_to = hatch; }
Room attic { "Attic"; description = "An attic."; object d_to = hatch; }
object hatch {
    "hatch"; attributes = {door, openable, static, scenery};
    found_in = {study, attic};
    object door_to(){ if (location == study) return attic; return study; }
    property door_dir = u_to;
}
object ball {
    "ball"; parent = hall;
    bool painted = false;
    bool short_name(){ if (painted) { print("red ball"); return true; } return false; }
    int parse_name(){
        int n = 0;
        while (true){ dictionaryWord w = NextWord(); if (w == .ball || w == .red) n++; else return n; }
    }
    string initial = "A ball lies here.";
    bool invent(){ if (inventory_stage == 2) print(" (bouncy)"); return false; }
}
object guide {
    "guide"; rawArray<dictionaryWord> name = {.guide}; parent = hall; attributes = {animate, proper};
    bool life(){
        if (action == Ask && (dictionaryWord)second == .weather){ print("Rain, later.^"); return true; }
        return false;
    }
}
bool Initialise(){ location = hall; rfalse; }
```

### 23.3.12 Collection at the Prompt

The core keeps one routine for the library to run before it reads the player's command:  the global `_bglPromptHook` (0 until the program first allocates from the heap).  Each binding runs it there, which is where heap arrays are collected (§12.6):

| Binding | Where |
|---|---|
| `i6StandardLibrary` | A `LibraryExtensions` object's `ext_afterprompt` (6.11 and later), so a program's own `AfterPrompt` still runs. |
| `punyInform` | A replacement of `_ReadPlayerInput` which runs the hook, then the library's routine; a read with no prompt (`YesOrNo()`, menus) skips it. |

### 23.3.13 Parser State

A program reads the parser's state through the globals a binding declares (§23.3.4).  A `NUMBER` token's value is in `noun` or `second` (§13.4.2).

**The player's words.**  Inside `parse_name` (and other parsing routines) `NextWord()` returns the next word as a `dictionaryWord`, or `null` for a word not in the dictionary, and advances `wn`; `NextWordStopped()` returns -1 once the words run out.

**Telling objects apart.**  When the parser asks whether two objects sharing a `parse_name` can be told apart, it sets `parser_action` to `TheSame` and the objects in `parser_one` and `parser_two`; the routine answers -1 when they are the same and -2 when they differ (0 leaves it to their `name` words).  A `parse_name` which matched a plural word sets `parser_action = PluralFound;`.  PunyInform has only `parser_action`, for `PluralFound`.

**The topic of Ask, Tell, and Answer.**  For Ask and Tell the parser puts the topic's first word (after a leading "the") in `second`; for Answer (`answer TOPIC to CHARACTER`) it is in `noun`, and `second` is the character.  Both are declared `object`, so a cast reinterprets the word:  `(dictionaryWord)second == .weather` (§4.11).  The whole topic is the words `consult_from` to `consult_from + consult_words - 1`, which you can read with `wn = consult_from;` and `NextWord()`.

## 23.4 Differences Between the Bindings

The two bindings differ in the following ways:

| | `i6StandardLibrary` | `punyInform` |
|---|---|---|
| Directions | Direction *objects* `n_obj` … `d_obj`; a Go action has `noun == n_obj`. | No direction objects.  `selected_direction` (a `property`) holds the direction property (`n_to`, `s_to`, … `in_to`, `out_to`) and `noun` is the shared `Directions` placeholder; `selected_direction_index` is `1..12`.  The `FAKE_*_OBJ` constants are the parser's internal sentinels. |
| Reacting objects | Any object can define `before`/`after`. | An object with `each_turn`, `react_before`, `react_after`, or `add_to_scope` needs the `reactive` attribute, which the library sets for it at startup unless `OPTIONAL_MANUAL_REACTIVE` is defined; with that option the program gives `reactive` itself. |
| Extra attributes | `door`, `absent`, `pluralname`, `male`, `female`, `neuter`, `switchable`, `on`, `scored`, `workflag`, … | The same, plus `reactive`. |
| Pronouns | `itobj`, `himobj`, `herobj` | Also `themobj`. |
| Status line | `StatusLineHeight()`, `gg_statuswin_cursize` | `_StatusLineHeight()`, `statusline_height`, `statusline_current_height` |
| Colors | `CLR_*` constants | `CLR_*` plus the Ozmoo extended colors, and the `clr_on`/`clr_fg`/`clr_bg`/`clr_fgstatus` globals |
| Verb sets | The standard library's actions, meta actions, and debug actions. | PunyInform's; some claimed-word sets differ (`Drop` claims `throw`; `Shout`/`ShoutAt`; `Again`/`Oops`; `LookModeNormal`/`Short`/`Long` in place of `LMode1..3`). |

## 23.5 Writing a Binding

A binding for another library follows these rules:

1. **Guard the file with `#once`** (§14.1.7), and declare the shared types (§23.3.7).
2. **Advertise the library with `#declare`** (§14.2.3):  a bare capability symbol which other files can test regardless of include order.
3. **Declare names, not behavior.**  Use `extern attribute`, `extern object`, `extern int` / `bool` / `string`, `extern const`, `extern property`, `extern additive property`, `extern verb`, and `extern` routines (§16.4).  Give `action` the type `verb`.  Declare each additive property the library defines (§11.7.2).  Don't declare the library's object properties on `object`.
4. **Declare every action as an `extern verb` with its claimed words** (§13.2.3), including fake actions with an empty body, `extern verb Name { }`, which claims no words, so that grammar added by a program extends the library's verbs instead of colliding with them.
5. **Declare the `grammarToken` enum** with the library's parser token names (§13.4.1).
6. **Declare library-required constants in `#emitfirst`** (§14.4.2) using `##beguilerSettings.KEY` substitution (§14.4.5), so that they precede the program's `#includeI6` of the library (§23.3.2).
7. **Run `bglInit()`.**  Wrap the library's `main` with `#emitfirst { replace main _oldmain; }` and `#emitlast { [main; bglInit(); _oldmain(); ]; }`, gated on `#if bglAutoInitialize` so that `autoInitialize = false` disables it (§23.3.1).
8. **Route the status bar** by replacing `bgl.ui.statusBar`'s `id` and `height` accessors, and `bgl.ui.mainWin.id`, with the library's own state (§23.3.8).
9. **Declare the library's optional entry points** as `extern default` functions, so that a program overrides one by defining it (§16.4.1).
10. **Declare optional parts under the symbol that enables them**:  `#if DEBUG` for what exists only in a debug build, and `#if OPTION` / `#if !OPTION` for what a library option constant adds or removes (§23.3.10).
11. **Run `_bglPromptHook` when the turn's prompt is shown** (§23.3.12), when it is not 0, at a point where no Beguile routine is running.

**See also** §11.7, §13.2.3, §14.4, §16.4, §15.7, §21.5.

---

# Appendices

# Appendix A Reserved Words

Here are Beguile's reserved words in alphabetical order, with the section which covers each (§1.5 groups them by role).  Since Beguile is case-insensitive, this applies in any letter case.  Words marked *soft* have meaning only in the position shown and are otherwise ordinary identifiers.

Kinds:  **qualifier** (declaration qualifier), **type** (type name or type-forming word), **control** (statement or control-flow word), **operator** (operator word), **value** (a value or receiver name), **I6** (I6-significant:  the word also appears verbatim in the generated program).

| Word | Kind | Section |
|---|---|---|
| `additive` | qualifier, I6 | §11.7.2 |
| `alias` | qualifier (a second Beguile name for a Beguile declaration) | §8.2.4, §10.2 |
| `array` | type, I6 | §12.2 |
| `asBgl` | qualifier (names an `extern` I6 symbol for Beguile source) | §3.11 |
| `asI6` | qualifier (names the I6 symbol a Beguile declaration emits as) | §3.11 |
| `attribute` | type, I6 | §11.6 |
| `auto` | type | §3.6, §9.9.2 |
| `bnum` | type | §2.7.2 |
| `bool` | type | §2.2 |
| `break` | control | §5.13 |
| `case` | control | §5.12 |
| `catch` | control | §5.16 |
| `char` | type | §2.2 |
| `charLiteral` | type | §2.4 |
| `class` | qualifier, I6 | §8.1 |
| `const` | qualifier | §3.4 |
| `continue` | control | §5.13 |
| `default` | qualifier; control (`switch`) | §5.12, §8.7.3, §16.4.1 |
| `delete` | control | §5.15 |
| `dictionaryWord` | type | §13.1, §21.5.3 |
| `dictionaryWordLiteral` | type | §2.4 |
| `distributor` | declaration (soft:  before a name and its `;`) | §6.8 |
| `do` | control | §5.11 |
| `else` | control | §5.8 |
| `emitter` | qualifier | §7.2 |
| `enum` | type | §2.7.1 |
| `explicit` | qualifier | §9.4 |
| `extend` | qualifier | §8.7.1, §11.10, §12.11, §13.5 |
| `extern` | qualifier | §16.4 |
| `false` | value, I6 (member of `eBool`) | §2.2 |
| `float` | type | §2.3 |
| `for` | control | §5.9 |
| `func` | type | §2.9 |
| `hide` | qualifier (soft:  at the head of a member declaration) | §8.7.4 |
| `if` | control | §5.8 |
| `in` | control (`for … in`) | §5.9.1 |
| `inject` | control (soft:  inside `extend` of an array) | §12.11 |
| `inline` | qualifier | §8.3.5, §11.3.1 |
| `int` | type | §2.2 |
| `interpolatedString` | type | §2.9.1 |
| `interpolatedStringLiteral` | type | §2.4 |
| `intLiteral` | type | §2.4 |
| `literal` | qualifier (soft:  before a type) | §3.13, §6.3 |
| `move` | control (soft:  inside `extend` of an array) | §12.11 |
| `negativeIntLiteral` | type | §2.4 |
| `new` | operator | §4.13 |
| `nothing` | I6 (the Inform 6 constant `null` is emitted as; not a Beguile word) | §2.5 |
| `null` | value | §2.5 |
| `nullLiteral` | type | §2.5 |
| `object` | type, I6 | §11.2 |
| `operator` | qualifier (operator member) | §9 |
| `outer` | value (soft:  inside a property accessor body) | §9.9.3 |
| `primitive` | qualifier (soft:  before `class`) | §8.2.5 |
| `property` | type, I6 | §11.7.1 |
| `rawArray` | type | §12.8 |
| `ref` | qualifier | §3.7 |
| `remove` | control (soft:  inside `extend` of an array) | §12.11 |
| `replace` | qualifier, I6 | §6.5, §8.7.2 |
| `replaced` | operator (call to the replaced routine) | §6.5 |
| `required` | qualifier (soft:  before a member's type) | §8.3.6 |
| `return` | control | §5.14 |
| `rfalse` | control | §5.14 |
| `rtrue` | control | §5.14 |
| `self` | value, I6 | §6.6 |
| `selfType` | type | §2.11 |
| `static` | qualifier | §8.3.3 |
| `string` | type, I6 | §2.2 |
| `stringLiteral` | type | §2.4 |
| `struct` | declaration (soft:  before a name) | §8.2.7 |
| `superposed` | qualifier | §3.12 |
| `switch` | control | §5.12 |
| `synonyms` | qualifier (soft:  inside a verb body or `extend` of a verb) | §13.5.4 |
| `throw` | control | §5.16 |
| `to` | control (`for` / `case` ranges) | §5.9, §5.12 |
| `true` | value, I6 (member of `eBool`) | §2.2 |
| `try` | control | §5.16 |
| `typesealed` | qualifier | §8.3.7 |
| `uint` | type | §2.2, §21.6.1 |
| `union` | type (soft) | §2.8.2 |
| `until` | control | §5.11 |
| `var` | type | §2.6 |
| `verb` | type, I6 | §13.2 |
| `void` | type | §2.2, §6.2 |
| `while` | control | §5.10 |

The following identifiers are not reserved, although they carry meaning:  `grammar`, `meta`, and `priority` are members of the `verb` class, and `handler` and `perform` are its methods (§13.2); `typeof` is a built-in function (§2.8.1); `create` and `destroy` are the lifecycle methods of a pooled class (§8.2.6), and `init` and `deinit` the lifecycle emitters of any class (§8.5); `first`, `last`, `after`, and `before` are the positions of an array `inject` (§12.11); `REVERSE` and `withI6Synonyms` are the pseudo-tokens which can end a grammar line (§13.4.4).  The keyword `for` is reused, outside a loop, in `alias class Foo for Bar` (§8.2.4).

Directive names (`#include`, `#if`, …) are listed in Appendix B.  Inform 6's own reserved words, which constrain any Beguile name that reaches the generated program, are listed in §16.9.

---

# Appendix B Directive Index

Directive names are case-insensitive.  Each entry points to its section in §14; islands and `extern`-related behavior are covered in §16.

| Directive | Purpose | Entry |
|---|---|---|
| `#beguilerSettings { … }` | Configure the compiler and the I6 invocation | §14.7.1 (§15.1) |
| `#beguilerSettings.prop` | Read a setting as a compile-time literal | §14.7.2 (§15.7) |
| `#bgl` | Beguile island in raw I6 (in-routine or file-scope) | §14.5.2 |
| `#bglDecl` | File-scope Beguile island, declarations only | §14.5.2 |
| `#bglStmt` | File-scope Beguile island, statements only | §14.5.2 |
| `#declare` | Define an order-independent, immutable symbol | §14.2.3 |
| `#define` | Define a compilation symbol (linear) | §14.2.1 |
| `#defineI6` | `#define` that also emits the symbol to I6 as a `Constant` | §14.2.1 |
| `#elif` | Alternative condition in an `#if` block | §14.2.5 |
| `#else` | Fallback branch of an `#if` block | §14.2.5 |
| `#emitAfterI6Include` | Emit this file's Inform 6 after a named `#includeI6` | §14.1.8 |
| `#emitfirst` | Raw I6 at the beginning of the generated program | §14.4.2 |
| `#emitlast` | Raw I6 at the end of the generated program | §14.4.3 |
| `#endif` | Close an `#if` block | §14.2.5 |
| `#error` | Halt compilation with a message | §14.3.3 |
| `#exit` | Stop processing the current file | §14.3.4 |
| `#i6` | I6 island in Beguile source (single-line or block) | §14.5.1 (§16.2) |
| `#if` | Conditional compilation | §14.2.5 |
| `#include <name>` | Include a BLR library file | §14.1.1 |
| `#include "path"` | Include a Beguile file by path | §14.1.2 |
| `#include @"path"` | Include by raw-string path | §14.1.3 |
| `#include ?"path"`, `#include ?<name>` | Include, silently skipping a missing file | §14.1.4 |
| `#includeI6` | Include an I6 source file (`?` optional, `@` raw variants) | §14.1.5 |
| `#message` | Print a message during compilation | §14.3.1 |
| `#mergeI6` | Apply a patch to the build's Inform 6 files | §14.1.6 |
| `#once` | Process this file at most once | §14.1.7 |
| `#redef` | Redefine a symbol without error | §14.2.2 |
| `#startup` | Raw I6 run at program startup, inside `bglInit()` | §14.4.1 |
| `#storedEmitFirst` | Named raw-I6 block, emitted at the top only when triggered | §14.4.4 |
| `#storedEmitLast` | Named raw-I6 block, emitted at the end only when triggered | §14.4.4 |
| `#undef` | Remove a symbol | §14.2.2 |
| `#using` | Import a class's or object's members into file scope | §14.6.1 |
| `#warning` | Report a warning and continue | §14.3.2 |

The `##` prefix marks Beguile-level processing inside otherwise raw I6 text.  These markers are not directives and are not valid in ordinary Beguile source:

| Marker | Where | Purpose | Specified in |
|---|---|---|---|
| `##if expr` | emitter bodies | Include the following body text only if the expression is true; same expression syntax as `#if` | §7.4 |
| `##else` | emitter bodies | Alternate branch of `##if` | §7.4 |
| `##endif` | emitter bodies | Close a `##if` block | §7.4 |
| `##beguilerSettings.key` | `#emitfirst`, `#emitlast`, `#storedEmitFirst`, `#storedEmitLast` bodies | Substitute a setting's value as an I6 literal | §14.4.5 |
| `##ifdef`, `##ifndef` | - | Do not exist.  `##if SYMBOL` in an emitter body, `#if SYMBOL` in Beguile source | §7.4, §14.2.5 |
| `##Action` | raw I6 only | Not a Beguile marker:  Inform 6's own action-constant syntax (`##Take`), valid wherever raw I6 is written | §13.3 |

A single-hash directive inside an emitter body (`#ifdef`, `#iftrue`, …) is not a Beguile directive:  it passes through verbatim and becomes an I6 compile-time conditional (§7.4).

---

# Appendix C Operators

## C.1 Precedence

This is the table from §4.3; higher precedence binds more tightly.

| Prec | Operators | Kind | Assoc | Meaning |
|:---:|---|---|---|---|
| 14 | `.` `?.` `[]` `()` `v?` `++` `--` | postfix | left | Member access, optional access, subscript, call, postfix query, postfix increment/decrement |
| 13 | `!` `-` `&` `(Type)` `++` `--` | prefix | right | Logical not, negation, address-of, cast, prefix increment/decrement |
| 11 | `*` `/` `%` | infix | left | Multiplicative |
| 10 | `+` `-` | infix | left | Additive |
| 9 | `<<` `>>` `<=>` | infix | left | Shift, three-way comparison |
| 8 | `<` `<=` `>` `>=` | infix | left | Relational |
| 7 | `==` `!=` `?=` `=~` `:==` | infix | left | Equality, identity |
| 6 | `&` | infix | left | Bitwise and |
| 5 | `^` | infix | left | Bitwise exclusive or |
| 4 | `\|` | infix | left | Bitwise or |
| 3 | `&&` | infix | left | Logical and |
| 2 | `\|\|` | infix | left | Logical or |
| 1 | `? :` | ternary | right | Conditional (§4.9) |
| 1 | `??` | infix | right | Null coalescing (§4.10) |
| 0 | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` `:=` | infix | right | Assignment (§5.5), compound assignment (§5.6), reference binding (§3.7) |

## C.2 Overloadable Operators

The complete set of operators which a class can overload, with the declaration shape of each, is the table in §9.1; the operators which must be declared as emitters are listed in §9.8.  `?.`, `??`, `=>`, `:=`, and `:==` are operator tokens which can't be overloaded.

## C.3 Operator-to-Section Index

The following table points to the sections covering each operator:

| Operator(s) | Role | See |
|---|---|---|
| `+` `-` `*` `/` `%` | Arithmetic | §4.5 |
| `+=` `-=` `*=` `/=` `%=` | Compound arithmetic assignment | §5.6, §9.7 |
| `==` `!=` `<` `>` `<=` `>=` | Comparison (result `eBool`) | §4.6 |
| `?=` | Type-defined comparison; no built-in meaning | §4.6, §9.5 |
| `<=>` | Three-way comparison (result `int`:  negative / 0 / positive) | §4.6, §9.6 |
| `=~` | Content / case-insensitive equality (`char`; `<string>`) | §4.6, §21.7, §22.3 |
| `:==` | Identity:  the same instance; never calls `operator ==` | §4.6 |
| `&&` `\|\|` `!` | Logical (result `eBool`) | §4.7, §9.5 |
| `&` `\|` `^` `<<` `>>` | Bitwise and shift | §4.8, §9.1 |
| `&=` `\|=` `^=` `<<=` `>>=` | Compound bitwise assignment | §5.6 |
| `++` `--` | Increment / decrement | §5.7, §9.7 |
| `=` | Assignment | §5.5, §2.10.1 |
| `:=` | Reference binding (rebinding) | §3.7 |
| `? :` | Ternary conditional | §4.9 |
| `?` (postfix) | Query / null test (result `eBool`) | §4.10, §9.5 |
| `?.` | Optional chaining | §4.10 |
| `??` | Null coalescing | §4.10 |
| `(Type)x` | Type cast and conversion | §4.11, §9.4 |
| `&x` (prefix) | Address-of:  raw machine address as `int` (`(int)x`) | §4.12 |
| `=>` | Lambda literal | §4.14 |
| `[]` `[]=` | Subscript read / write | §9.3, §12.3 |
| `::name` | Global-scope qualifier | §3.9 |
| `Type::operator op` | Operator reference | §4.15 |
| `$opref(op)` | Operator reference inside an emitter body | §7.3.1 |

---

# Appendix D String Escapes and Character Tables

This appendix completes §1.6.3 and §1.6.6.  Every escape below is valid in a string literal, an interpolated string literal, and a character literal; none is processed in a raw string literal, which recognizes only `\"`.

## D.1 Basic Escapes

The basic escapes are the following:

| Escape | Character |
|---|---|
| `\n` | newline |
| `\"` | `"` |
| `\\` | `\` |
| `\^` | `^` (see §D.4) |
| `\~` | `~` (see §D.4) |
| `\@` | `@` |
| `\{` | `{` |
| `\}` | `}` |

In an interpolated string (§1.6.5) `\{` is a brace which opens no span.  A backslash followed by any character this appendix doesn't list is a compile-time error.

<!-- doctest: skip -->
```bgl
"She said, \"well done.\""
"Price: 5\~ off!"
"Press \^ to continue."
```

## D.2 Numeric Escapes

A numeric escape names a character by its code:

| Escape | Meaning |
|---|---|
| `\`*NNN* | the character with decimal code *NNN* |
| `\$`*XX* | the character with hexadecimal code *XX* |

Both forms consume every consecutive digit (or hexadecimal digit) after the prefix.  In a string literal the code is a Unicode code point:  Glulx will render any code point; however, the Z-machine renders only the characters in its character set (§D.5), and any other code is a compile-time error.  In a character literal the two forms differ, as §D.7 describes.

```bgl
"na\239ve"     // -> naïve
"na\$EFve"     // -> naïve
```

## D.3 Diacritical Shorthands

An accent escape is `\` followed by an accent mark and the letter it applies to:

| Escape | Letters | Example | Result |
|---|---|---|---|
| `\'`*X* | a e i o u y A E I O U Y | `"caf\'e"` | café |
| `` \` ``*X* | a e i o u y A E I O U Y | `` "cr\`eme" `` | crème |
| `\^`*X* | a e i o u y A E I O U Y | `"g\^ateau"` | gâteau |
| `\:`*X* | a e i o u y A E I O U Y | `"na\:ive"` | naïve |
| `\~`*X* | a n o A N O | `"se\~nor"` | señor |

A named escape is `\` followed by a two-character name:

| Escape | Result | Escape | Result |
|---|---|---|---|
| `\/o` `\/O` | ø Ø | `\th` `\TH` | þ Þ |
| `\cc` `\cC` | ç Ç | `\et` `\ET` | ð Ð |
| `\oa` `\oA` | å Å | `\ae` `\AE` | æ Æ |
| `\oe` `\OE` | œ Œ | `\ss` | ß |
| `\LL` | £ | `\!!` | ¡ |
| `\??` | ¿ | `\<<` `\>>` | « » |

## D.4 Context-Sensitive `\^` and `\~`

`\^` and `\~` are both a basic escape (§D.1) and an accent mark (§D.3).  When the escape is followed by a letter in its accent set it will produce the accented letter; otherwise it produces a literal `^` or `~`.  To force the literal character before a letter which would otherwise take the accent, double it:  `\^^` or `\~~`.

```bgl
"\^a"      // -> â
"\^^a"     // -> ^a
"\~n"      // -> ñ
"\~~n"     // -> ~n
"\^z"      // -> ^z   (z is not in the accent set)
```

## D.5 Directly Typed Characters

You can type accented characters directly into a string literal, an interpolated string literal, a character literal, or a dictionary word.  The source file can be UTF-8 or Latin-1.  Each character must be one of the ZSCII extended characters (codes 155 to 224):

| Group | Characters |
|---|---|
| diaeresis | ä ö ü Ä Ö Ü ë ï ÿ Ë Ï |
| acute | á é í ó ú ý Á É Í Ó Ú Ý |
| grave | à è ì ò ù À È Ì Ò Ù |
| circumflex | â ê î ô û Â Ê Î Ô Û |
| ring and slash | å Å ø Ø |
| tilde | ã ñ õ Ã Ñ Õ |
| ligatures and letters | æ Æ ç Ç þ Þ ð Ð œ Œ ß |
| punctuation | £ ¡ ¿ « » |

Any other non-ASCII character is a compile-time error, except the typographic quotes of §D.6 and, on Glulx, the characters described below.

<!-- doctest: skip -->
```bgl
"café"
'ñ'
.café
```

> ***[Glulx]:** Glulx text is Unicode, so any character can be typed directly into a string literal, an interpolated string literal, or a character literal:  `"© 2027 ✓"`, `'Ω'`.  A dictionary word still takes only the characters in the above table.*

## D.6 Typographic Quotes and the Backtick

The curly double quotes `“` `”`, the curly single quotes `‘` `’`, and the backtick `` ` `` are accepted in string literals and rendered according to the target.

> ***[Glulx]:** Each curly quote is rendered as its own glyph; the backtick is rendered as a backtick.*

> ***[Z-machine]:** The Z-machine has no typographic glyphs:  curly double quotes fold to a straight double quote, and curly single quotes and the backtick fold to a straight apostrophe `'`.*

The target is the resolved build target (§15); Glulx is the default.  Only directly typed characters are folded:  a numeric escape such as `\$201C` always denotes that code point.

## D.7 Character Literals

A character literal (§1.6.6) holds exactly one character, written directly or with any escape above.  Its value is the character's code:  its ZSCII code on the Z-machine, its Unicode code point on Glulx (`'ä'` is 155 on the Z-machine and 228 on Glulx).

The numeric escapes read differently here than in a string (§D.2).  `\NNN` is a ZSCII code, and names the same character on either target:  `'\155'` is ä, 155 on the Z-machine and 228 on Glulx.  `\$XX` is the character's code as written, on either target, with no translation.

`\'` followed by a letter in the acute-accent set (§D.3) is the acute accent:  `'\'e'` is é.  A `\'` not followed by such a letter is an escaped single quote.

---

# Appendix E Settings Reference

The following table lists every property accepted in a `#beguilerSettings` block (§15.1), alphabetically, including the two which `<string>` declares (§15.9).  *Precedence* is how repeated assignments combine:  **first** (first-writer-wins) or **additive**; a command-line value always counts as written first.  *Readable* marks the properties which `#beguilerSettings.prop` can read (§15.7).

| Property | Type | Default | Precedence | CLI equivalent | Readable | Entry |
| --- | --- | --- | --- | --- | --- | --- |
| `author` | string | `""` | first | - | yes | §15.5 |
| `autoInitialize` | bool | `true` | first | - | yes | §15.4 |
| `beguiLibPath` | string | `"beguiLib"` | first | `-lib=DIR` | yes | §15.2 |
| `blorbAssetPath` | string | `"assets"` | first | - | yes | §15.6 |
| `description` | string | `""` | first | - | yes | §15.5 |
| `economy` | bool | `false` | first | - | yes | §15.3 |
| `errorFormat` | `eErrorFormat` | `E1` | first | `-E1`, `-E2` | yes | §15.3 |
| `filterVerbSplitWarnings` | bool | `true` | first | `-filterVerbSplitWarnings=true\|false` | yes | §15.3 |
| `firstPublished` | string | `""` | first | - | yes | §15.5 |
| `forgiveness` | string | `""` | first | - | yes | §15.5 |
| `framePoolSize` | int | sized from the program, at least `64` | first | - | yes | §15.4 |
| `generateBlorb` | bool | `false` | first | - | yes | §15.6 |
| `genre` | string | `""` | first | - | yes | §15.5 |
| `headline` | string | `""` | first | - | yes | §15.5 |
| `heapSize` | int | `1536` (Z-machine), `16384` (Glulx) (also for 0) | first | - | yes | §15.4 |
| `ifid` | string | `""` (generated) | first | - | yes | §15.5 |
| `includePaths` | string | none | additive | `-includepaths=DIRS` | yes | §15.2 |
| `informName` | string | `"inform"` | first | `-inform=<name>` | yes | §15.2 |
| `informPath` | string | none | first | `-inform=PATH` | yes | §15.2 |
| `language` | string | `""` | first | - | yes | §15.5 |
| `omitUnusedRoutines` | bool | `true` | first | - | yes | §15.3 |
| `outputPath` | string | `"output"` | first | `-o DIR` | yes | §15.3 |
| `release` | int | `0` | first (0 = unset) | - | yes | §15.3 |
| `rewritePaths` | bool | `true` | first | - | yes | §15.3 |
| `serial` | string | `""` | first | - | yes | §15.3 |
| `series` | string | `""` | first | - | yes | §15.5 |
| `seriesNumber` | int | `0` | first (0 = unset) | - | yes | §15.5 |
| `stringCaptureSize` | int | `512` (also for 0); declared by `<string>` | first | - | yes | §22.3 |
| `stringHeapSize` | int | `5120` (also for 0); declared by `<string>` | first | - | yes | §22.3 |
| `target` | `eTarget` | `Glulx` | first | `-G`, `-z5`, `-z8` | yes | §15.3 |
| `title` | string | `""` | first | - | yes | §15.5 |
| `worldBufSize` | int | `128` | first | - | yes | §15.4 |

**Enum values.**  `eTarget`:  `Glulx`, `Z5`, `Z8`.  `eErrorFormat`:  `E1`, `E2`.

**Validation.**  `serial` must be exactly six digits; `framePoolSize` and `worldBufSize` must be at least 1, and `heapSize` 0 or at least 64; `target` must be an `eTarget` member; a regular-string `includePaths` entry must name an existing directory.

---

# Appendix F Pre-defined Symbols

The compiler defines the following symbols before any source file is processed.  You can test and compare them in `#if` expressions exactly like symbols defined with `#define` (§14.2).  Symbol names are case-insensitive, like all Beguile identifiers.

| Symbol | Example value | Meaning |
|--------|---------------|---------|
| `beguiler` | `1023` | Compiler version encoded as `major*1000 + minor*10 + patch`:  1.2.3 is `1023`, 1.1.0 is `1010`.  The encoding fits a 16-bit signed Z-machine word for major versions up to 32. |
| `beguilerMajor` | `1` | Major version component. |
| `beguilerMinor` | `2` | Minor version component. |
| `beguilerPatch` | `3` | Patch version component. |
| `TARGET_GLULX` | (defined, no value) | Defined when the `target` setting is `Glulx`. |
| `TARGET_ZCODE` | (defined, no value) | Defined when the `target` setting is a Z-machine version. |
| `DEBUG` | (defined, no value) | Defined in a debug build (`--debug`, §20.3).  A program can also turn it on itself with `#redef DEBUG` (§14.2.2). |
| `bglAutoInitialize` | `1` | Defined when the `autoInitialize` setting is `true` (§15.4); the bindings test it before running `bglInit()` for you (§23.5). |
| `generateBlorb` | `true` | The `generateBlorb` setting (§15.6).  Always defined; test it by value, `#if (generateBlorb == true)`. |

The version symbols are read-only and derived from the compiler's own version.  The target symbols are set from the `target` setting (§15.3).  Exactly one of `TARGET_GLULX` and `TARGET_ZCODE` is defined.

**The target symbols carry no value.**  Neither can be used in a Beguile expression; `#if TARGET_ZCODE` tests definedness like any other bare symbol (§14.2.5).  To tell Z5 from Z8, test the `target` *setting*, which `#if` reads directly:

<!-- doctest: skip -->
```bgl
#if #beguilerSettings.target == "z8"
```

**Resolution rule.**  Every symbol which carries a value, pre-defined or `#define`d, is resolved as an inline compile-time literal wherever it appears in a Beguile expression:  the name is replaced by its value.  The exception is a symbol defined with `#defineI6`, which is an I6 `Constant` and is emitted by name (§14.2.1).  No other symbol becomes an I6 `Constant`, although the program can assign one to a `const` variable of its own.

```bgl
if(beguiler >= 1010) { … }          // resolved at compile time to if(1010 >= 1010)
const int myVer = beguilerMajor;    // this const is the program's own declaration
```

**Example**

```bgl
#if beguiler >= 1010
    // requires Beguile 1.1.0 or later
#endif

#if #beguilerSettings.target == "z5"
    // Z5 only (excludes Z8): the version lives in the setting, not the symbol
#endif

#if TARGET_GLULX
    // Glulx-specific code
#endif
```

**See also** §14.2.4, §14.2.5, §15.3, Appendix E.

---

# Appendix G Emitter Substitution Tokens

## G.1 Token Reference

Every `$` token is replaced when an emitter body is substituted at a use site; every `##` form is processed at the same time.  `$i6Name` and `$i6Expr` are also recognized in an `#i6` island (§16.2), where they are the only tokens which mean anything.  Anything else in an emitter body, including single-hash `#` directives and unrecognized `##name` text, passes through to the output unchanged.  The full rules are in §7.3 and §7.4.

| Token | Meaning | See |
|---|---|---|
| `$self` | In an operator or assignment emitter, the receiver with its trailing `.member` removed when the receiver is a member access (`obj` for `obj.score + 1`); otherwise, and in a method emitter, the receiver itself (`container.children` for `container.children.length`).  Not meaningful in a global emitter. | §7.3 |
| `$val` | The full receiver expression as written:  `obj.score` for `obj.score + 1`; otherwise the same as `$self`. | §7.3 |
| `$host` | The object a proxy member is accessed on, the owner of the proxy:  the receiver with its trailing `.member` removed, in every kind of emitter (`container` for `container.children.length`); equals `$self` when the receiver is not a member access. | §7.3, §7.3.2 |
| `$name` | The argument supplied for the parameter declared as `name`. | §7.3 |
| `$target` | The assignment target as a full lvalue path, or a compiler-supplied temporary in statement position.  Its presence makes the body responsible for the store. | §7.3.2 |
| `$prop` | In an `array<T>` emitter, the property name of a class or object member array; `0` for a global or local array. | §7.3, §12.7, §12.12 |
| `$opref(op[, T])` | A callable reference to the receiver type's `operator op` (routine name for `static`, property name for an instance operator, `0` for an emitter or none); `T` selects an overload. | §7.3.1 |
| `$oprefReq(op[, T])` | As `$opref`, with a compile-time warning when nothing is found. | §7.3.1 |
| `$i6Expr(expr)` | The I6 a Beguile expression emits, expanded inline.  Binds the body's tokens as typed values; one expression or one void call; cycles and chains deeper than eight are errors. | §7.3.4 |
| `$i6Name(path[(types)])` | The identifier Inform 6 knows a declaration by:  its `asI6` name, the `_bgl_CLASS_METHOD` mangling of a `static` method, or its plain name.  `types` selects an overload.  Naming an emitter is an error. | §7.3.3 |
| `$selfsub` | In a `_bglGlobalDeclaration` body, the instance name with `sub` appended. | §16.8 |
| `##if expr` / `##else` / `##endif` | Conditional inclusion of body text, with the `#if` expression syntax.  `##ifdef`/`##ifndef` are errors. | §7.4 |
| `##beguilerSettings.key` | The compile-time value of a `#beguilerSettings` property, in the raw-I6 bodies of `#emitfirst`, `#emitlast`, `#storedEmitFirst`, and `#storedEmitLast` only. | §14.4.5, §15.7 |
| `##Name` (other) | Not a token:  passes through verbatim as I6 text (an I6 action constant such as `##Take`; `##$v` yields the action name of `$v`). | §7.4 |

---

# Appendix H Glossary

These are the terms of art used throughout this specification, each defined once, with the section which covers it in full.

## H.1 Terms

The following terms have a particular meaning in Beguile:

- **additive property**:  A property whose contributions from a class and its subclasses and instances accumulate into one contiguous run of words instead of overriding one another; declared `additive property name;`.  Every contribution must be a `rawArray<T>` or a routine.  See §11.7.2, §23.3.5.
- **anchor**:  In verb priority, the priority declared in a verb's own body (default 10), against which `extend` contributions sort.  See §13.2.5.
- **`auto { … }` accessor**:  An inline property accessor:  the braces hold a class body (backing members plus the get and set operators).  See §9.9.2.
- **Beguiler**:  The Beguile compiler, which transpiles Beguile source to Inform 6 and then invokes the Inform 6 compiler.  See §17.1.
- **`#bglDecl` / `#bglStmt`**:  The file-scope island forms of precompiler mode that accept only declarations or only statements, respectively; `#bgl` accepts both.  See §14.5.2, §16.3.2.
- **binding**:  A BLR declaration layer that exposes one external Inform 6 library's symbols to Beguile as typed `extern` declarations and integrates it with the runtime core.  See §23.1.
- **BLR (Beguile Language Runtime)**:  The library of Beguile source every program compiles against:  the auto-loaded core, the opt-in extensions, and the IF library bindings.  See §21.1, §22.1, §23.1.
- **bnum**:  A bit-flag enumeration:  its values are powers of two, starting at `1` and doubling.  See §2.7.2.
- **claimed word**:  A dictionary word that a verb, native or `extern`, has declared as one of its trigger words.  See §13.2.3.
- **class form**:  One of the seven kinds of class declaration (normal, `extern`, `emitter`, `alias`, primitive, value, pooled); the form fixes what instances of the class exist and which members it may declare.  See §8.2.
- **declaration**:  A construct that introduces a named type, variable, constant, function, object, verb, or grammar.  See §3.1.
- **default mode**:  The normal compilation mode:  the entry file is a `.bgl` file, and I6 is reached through `#i6` islands.  Contrast **precompiler mode**.  See §16.1.1.
- **directive**:  A `#name` instruction to the compiler (`#include`, `#define`, `#i6`, …); directives are not statements.  See §14, Appendix B.
- **eBool**:  The enum `{ true, false }` which backs `bool`.  See §2.2.
- **emitter**:  A function, method, or operator whose body is raw Inform 6 that the compiler inlines at each call site instead of emitting a routine call.  See §7.1.
- **emitter body**:  The raw I6 text of an emitter, containing substitution tokens.  See §7.1, §16.2.
- **emitter class**:  A class whose instances have no Inform 6 object backing; it serves as a type label and a host for emitter members.  See §8.2.3.
- **emitter namespace**:  A group of emitters declared under one name without `class` (`emitter name { … }`) and called as `name.member(…)`; the name is not a type.  See §7.7.
- **emitter value**:  An emitter declared without a parameter list:  a typed inline expansion used by bare name, without `()`.  See §7.6.
- **ephemeral**:  A `stringObj` returned by a string operation:  its block has already been released when the caller receives it, so its text lasts only until the next string is allocated.  It is held for a call it is passed straight into; assigning it to a `stringObj` copies it to keep.  See §22.3, §8.5.1.
- **eType**:  The enum returned by `typeof(v)`:  the machine category of a value (`unknown`, `int`, `string`, `routine`, `object`, `class`).  See §2.8.1, §21.5.9.
- **evict**:  To remove a dictionary word from an `extern` verb entirely, with the word-level form `extend V { grammar -= { {.w} }; }`.  See §13.5.2.
- **expression**:  Operands joined by operators, resolving to a value with a static type.  See §4.1.
- **extension**:  An opt-in BLR file enabled with `#include <name>`, as opposed to the core, which is always loaded.  See §22.1.
- **frame pool**:  The global pool of word slots which backs local `array<char>` buffers on both targets and, on the Z-machine, the local variables a routine has beyond the machine's per-routine limit; sized from the program, or by the `framePoolSize` setting.  See §15.4, §18.10.
- **function**:  A Beguile-declared callable, which compiles to an I6 routine.  Contrast **routine**.  See §6.1.
- **grammar line**:  One pattern the player may type, declared on a verb or in a grammar object:  a trigger word followed by pattern tokens.  See §13.4.
- **grammar object**:  A standalone `grammar` declaration holding `grammarRule` entries that name their verbs explicitly, as opposed to grammar declared on the verb itself.  See §13.4.5.
- **heap**:  The block of memory which local arrays and `<linq>` results are allocated from.  Sized by `heapSize`.  See §12.6, §21.15.
- **`.i6b` template**:  A built-in Inform 6 template from which the compiler emits constructs which have no fixed I6 form until it knows how they are used.  It is not `#include`d.  See §18.9.
- **`instanceName`**:  An object's name:  a `const string` member of `_bglObject`, fixed at compile time, that becomes the I6 header name `print(obj)` prints.  Given in an object body by name or, as every class's last positional member, by position.  See §11.5.4.
- **island**:  A region of one language embedded in a file written in the other:  an `#i6` island is raw Inform 6 inside a Beguile file; a `#bgl` island is Beguile inside an Inform 6 file.  See §16.2, §16.3.
- **loose identifier mode**:  The relaxed name resolution used inside `#bgl` islands and throughout precompiler mode, where an unresolved identifier passes through to Inform 6 rather than raising an error.  See §16.3.3.
- **materialize**:  To make a `superposed` declaration part of the program, which happens the first time its name is referenced.  See §3.12, §18.9.
- **member**:  Any named part of a class or object:  a variable, method, operator, or emitter.  Beguile makes no property-versus-field distinction.  See §8.3.
- **meta verb**:  A verb with `meta = true`:  an out-of-world action that runs without advancing the turn counter or triggering daemons.  See §13.2.4.
- **named union**:  A `union Name = A | B { … }` declaration:  a union type with a name and a place to hang members.  See §2.8.2.
- **object-backed class**:  A class whose instances are Inform 6 objects with storage of their own (a normal class, including a pooled one), as opposed to an emitter, alias, or primitive class, whose instances are bare words or do not exist.  See §8.2, §11.3.2.
- **outer**:  Inside a property accessor body, the host object the accessor is declared on, as distinct from `self`, the accessor instance itself.  See §9.9.3.
- **owned member**:  A member whose type is a struct with stored members, declared without an initializer; each instance of the enclosing type gets its own backing instance.  See §8.3.4.
- **pass-through conversion**:  A conversion operator declared without a body (`emitter T operator ();`):  the value is left unchanged and merely retyped.  See §2.10.2, §9.4.
- **pooled class**:  A class declared `class Name[N]`, which reserves `N` statically allocated instances that `new` and `delete` hand out and reclaim.  See §8.2.6, §4.13.
- **positional member**:  A member an instance can be given by position rather than by name, in an inline object or a declaration body.  See §8.3.5, §11.3.1.
- **precompiler mode**:  The compilation mode where the entry file is an `.inf` (Inform 6) file and Beguile is reached through `#bgl` islands.  Contrast **default mode**.  See §16.1.2.
- **primary trigger**:  The first dictionary word in a verb's first grammar line; the word an `extend V { grammar += … }` targets.  See §13.2.3.
- **primitive type**:  A type whose variable holds the value itself, one word, with no instance:  `int`, `uint`, `bool`, `char`, `float`, `string`, `dictionaryWord`, `attribute`, `property`, `bglClass`, the literal pseudo-types, `glulxImage`, and every enum.  Copied on assignment; `ref` doesn't apply.  Declared `primitive class`.  See §2.10.3, §8.2.5.
- **program**:  The set of source files compiled together to produce one story file.  See §3.1.
- **property accessor**:  A member that reads and writes like a plain member but runs code on each access:  a struct declaring a getter `operator ()` and a setter `operator =`, used as an owned member of the host class or object.  See §9.9.
- **proxy member**:  A member that represents a relation over its owner rather than storing a value, such as `children` on `object`; it has no slot of its own.  See §7.3, §11.5.
- **pseudo-type**:  The compile-time type of a literal (`intLiteral`, `stringLiteral`, …), inferred by the compiler and never written by the author; it takes part in overload and operator resolution separately from the runtime type it corresponds to.  See §2.4.
- **reference class**:  A class, as opposed to a struct:  a variable declared with no value has an object of its own, one given a value refers to it, and assignment and passing share.  See §2.10.3, §8.2.1.
- **resolved type**:  The static type the compiler assigns to an expression.  See §4.1.
- **re-list**:  `inline name;` in a class body or `extend`:  makes an existing member positional at that point in the class's order, without redeclaring it.  See §8.3.5.
- **required**:  A class-member qualifier:  every object of the class must get a value for the member, from itself or from a class between it and the declaring class.  See §8.3.6.
- **routine**:  An I6 callable.  A Beguile **function** compiles to a routine.  See §6.1.
- **size vs. length**:  For a Beguile array, `size` is the capacity reserved at compile time; `length` is the runtime count of in-use elements.  See §12.3.
- **source file**:  A file the compiler reads:  a `.bgl` file, or an `.inf` file in precompiler mode.  See §3.1, §16.1.
- **statement**:  An executable unit inside a function body, ending in `;` or a `{ }` block.  See §5.1.
- **static instance**:  An instance declared at file scope (`Name m;`, or an object declaration) and allocated once for the program, as opposed to a pooled instance obtained with `new` or an instance local to a routine.  See §8.2.6, §11.2.
- **`#storedEmitFirst` / `#storedEmitLast`**:  A named, deferred raw-I6 block that is emitted only when a built-in I6 template whose `triggers` clause names it is applied.  See §14.4.4, §18.9.
- **story file**:  The executable output of a build (`.z5`, `.z8`, or `.ulx`), run by a Z-machine or Glulx interpreter.  See §20.2.
- **stringObj**:  The `<string>` extension's owning text type:  a slot that owns a block of the string heap and can be changed in place, as opposed to `string`, a slot referring to static text.  Assignment and passing copy the text; `ref` shares the buffer.  See §22.3, §8.5.
- **substitution token**:  A `$`-prefixed placeholder in an emitter body (`$self`, `$val`, `$target`, `$PARAMETER`) replaced at the call site.  See §7.3, Appendix G.
- **superposed**:  A declaration qualifier marking a routine, global, object, or class that is emitted only if something references it.  See §3.12, §18.9.
- **target**:  The virtual machine a story file runs on:  Z-machine or Glulx.  See §15.3.
- **tracked / untracked array**:  A tracked array carries a runtime length word; an untracked array (a raw Inform 6 array, or a `rawArray` view) does not.  See §12.3, §12.8.
- **tracked buf**:  With `<buf>` included, a sized `array<char>` that records its capacity and current length; its value behaves as an Inform 6 hybrid buffer.  See §22.2.
- **trigger word**:  The dictionary word that begins a grammar line and selects its verb; a verb's first trigger word is its primary trigger.  See §13.2.3, §13.4.
- **typesealed**:  A member qualifier that locks the member's type:  a subclass or instance may re-initialize the member but not re-declare it with another type.  See §8.3.7.
- **struct**:  A class declared `struct`:  a global, local, member, or parameter of the type owns an instance of its own and is copied on assignment and passing.  Contrast a reference class, whose variables refer to shared instances.  See §2.10.3, §8.2.7.
- **veneer class**:  A type label declared `extern emitter class` over a word that manages its own storage (`array<T>`).  The one-word value types are primitive classes.  See §8.2.3.
- **word**:  The machine's native integer unit:  16 bits on the Z-machine, 32 on Glulx.  Every scalar value, reference, and `array<T>` element other than an `array<char>` element occupies one word.  See §2.2, Appendix J.

## H.2 Symbols

The following symbols and prefixes are defined here as well:

- **`$self` / `$val` / `$target` / `$host` / `$prop`**:  Substitution tokens used inside emitter bodies:  `$self` is the receiver (in an operator or assignment emitter on a member, the member's owner), `$val` the full receiver expression, `$target` an assignment's left-hand side, `$host` the object a proxy member is accessed on, and `$prop` an array property's name.  See §7.3, Appendix G.
- **`$opref(op)`**:  A lookup, not a substitution:  it yields a reference to the receiver type's operator `op` (the routine for a `static` operator, the property for an instance operator, `0` when the type publishes none).  See §7.3.1, §12.10, Appendix G.
- **`_bglObject`**:  The root of every class whose values are I6 objects; a class declared without a base derives it, unless it is a `primitive` class.  It declares the root assignment and what I6 gives any object:  identity, `provides`, `is`, `print`.  See §2.10.3, §21.5.8.
- **`_bglPrimitive`**:  The root of every primitive class; a `primitive` class declared without a base derives it.  It declares only the root assignment, which copies the word.  See §21.5.8.
- **`_bgl…` / `bgl…`**:  Identifier prefixes reserved for the runtime and for compiler-generated symbols.  See §1.4.

---

# Appendix I Symbol and Method Index

The following table lists every runtime and library name you can write, alphabetically:  functions, methods, members, objects, namespaces, types, constants, and the symbols a library defines.  *Kind* says what the name is; a method or member names the type it belongs to.  *Provided by* is `core` (loaded without an include, §21), an extension (`<name>`, §22), or a binding (§23).  *Section* is where the name is covered.  Overloads share one row; a family of names which differ only in a suffix is listed once with the suffix spelled out.  Names of the `bgl` namespace are listed under `bgl.…`.

`size` and `length` are built in; assigning `length`, `clear()`, and the methods marked `<array>` need `#include <array>`.

| Name | Kind | Provided by | Section |
| --- | --- | --- | --- |
| `+=`, `-=` | operator on `array<T>` | `<array>` | §22.4 |
| `=~` | operator on `char` (case-insensitive equality) | core | §21.7 |
| `a(obj)`, `cA(obj)` | function | core | §21.4 |
| `AfterLife()`, `NewRoom()`, `TimePasses()`, `InScope()`, … | function (`extern default` entry point; author-overridable) | binding (`i6StandardLibrary`) | §23.3.4 |
| `any(pred)`, `all(pred)` | method on `array<T>` | `<linq>` | §22.5 |
| `append(item)` | method on `array<T>` | `<array>` | §22.4 |
| `append(v)`, `prepend(v)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `array<T>` | type | core | §12 |
| `attribute` | type | core | §11.6, §21.5.1 |
| `attributeList` | type | core | §11.5.3, §21.5.1 |
| `attributes` | member on `object` | core | §11.5.3, §21.5.1 |
| `before`, `after`, `life`, `orders`, `describe`, `time_out`, `each_turn` | property (additive) | binding (`i6StandardLibrary`) | §23.3.5 |
| `bgl` | namespace | core | §21.3 |
| `bgl.asm` | namespace (opcode emitters; members differ per target) | core | §21.3 |
| `bgl.glulx` | namespace | core (Glulx) | §21.3 |
| `bgl.glulx.color` | object | `<glulxWindow>` | §22.7.8 |
| `bgl.glulx.eStyleType`, `eWinType`, `eImgAlign`, `eImgDimension`, `bWinBorder`, `bWinPlacement`, `bWinScale` | type (enum aliases) | core (Glulx) | §21.3, §22.7.10 |
| `bgl.glulx.supports` | object (interpreter capabilities) | `<glulxWindow>` | §22.7.12 |
| `bgl.glulx.window`, `bgl.glulx.textBufferWindow`, `bgl.glulx.textGridWindow`, `bgl.glulx.graphicsWindow` | type (path forms of the window types) | `<glulxWindow>` | §21.3, §22.7.1 |
| `bgl.header` | namespace | core | §21.14 |
| `bgl.header.printSerial()` | method on `bgl.header` | core | §21.14 |
| `bgl.header.release` | member on `bgl.header` | core | §21.14 |
| `bgl.header.serialChar(i)` | method on `bgl.header` | core | §21.14 |
| `bgl.printRules` | namespace | core | §21.11 |
| `bgl.printRules.bold`, `italics`, `underline`, `reverse`, `fixed`, `roman` | print rule | core | §21.11 |
| `bgl.printRules.img(image[, align[, width[, height]]])` | print rule | core (Glulx) | §21.11 |
| `bgl.printRules.link(text, target)` | print rule | core (Glulx) | §21.11 |
| `HandleGlkEvent(event, context, buffer)` | function (`extern default` entry point; author-overridable) | core (Glulx) | §6.7.1 |
| `bgl.ui` | namespace | core | §21.10 |
| `bgl.ui.hideCursor()`, `showCursor()` | function | `<ui>` | §22.6 |
| `bgl.ui.mainWin` | object (root text-buffer window) | core | §21.10, §22.7.2 |
| `bgl.ui.screen` | object (style target for both text kinds) | `<glulxWindow>` | §22.7.2 |
| `bgl.ui.statusBar` | object (root text-grid window) | core | §21.10, §22.7.2 |
| `bgl.ui.waitForKey([separator])` | function | `<ui>` | §22.6 |
| `bgl.util` | namespace | core | §21.3 |
| `bgl.util.buf` | namespace | `<buf>` | §22.2 |
| `bgl.util.buf.append(to, from)`, `prepend(to, from)` | function | `<buf>` | §22.2 |
| `bgl.util.buf.capture(buf[, maxBytes])`, `release()`, `stackLen()` | function | `<buf>` | §22.2 |
| `bgl.util.buf.compare(a, b[, caseInsensitive])` | function | `<buf>` | §22.2 |
| `bgl.util.buf.copy(to, from, n[, toPos[, fromPos]])` | function | `<buf>` | §22.2 |
| `bgl.util.buf.delete(buf, pos, count)` | function | `<buf>` | §22.2 |
| `bgl.util.buf.equals(a, b[, caseInsensitive])` | function | `<buf>` | §22.2 |
| `bgl.util.buf.getChar(buf, pos)`, `setChar(buf, pos, c)` | function | `<buf>` | §22.2 |
| `bgl.util.buf.indexOf(buf, search[, start])` | function | `<buf>` | §22.2 |
| `bgl.util.buf.indexOfFirstTrue(buf, pred[, start])`, `indexOfFirstFalse(…)` | function | `<buf>` | §22.2 |
| `bgl.util.buf.insert(to, from, pos[, count])` | function | `<buf>` | §22.2 |
| `bgl.util.buf.isTracked(buf)`, `size(buf)`, `length(buf)`, `setLength(buf, n)` | function | `<buf>` | §22.2 |
| `bgl.util.buf.mid(to, from, fromPos, n)`, `left(to, from, n)`, `right(to, from, n)` | function | `<buf>` | §22.2 |
| `bgl.util.buf.print(buf[, len])` | function | `<buf>` | §22.2 |
| `bgl.util.buf.replace(buf, search, repl)`, `replaceAll(buf, search, repl[, start])` | function | `<buf>` | §22.2 |
| `bgl.util.buf.set(buf, value)` | function | `<buf>` | §22.2 |
| `bgl.util.buf.startsWith(buf, prefix[, ci])`, `endsWith(buf, suffix[, ci])` | function | `<buf>` | §22.2 |
| `bgl.util.buf.toUpper(buf)`, `toLower(buf)`, `reverse(buf)` | function | `<buf>` | §22.2 |
| `bgl.util.buf.trim(buf)`, `trimLeft(buf)`, `trimRight(buf)` | function | `<buf>` | §22.2 |
| `bgl.util.math` | namespace | core | §21.6.2 |
| `bgl.util.math.abs(x)` | function | core | §21.6.2 |
| `bgl.util.math.clamp(v, lo, hi)` | function | core | §21.6.2 |
| `bgl.util.math.min(a, b)`, `max(a, b)` | function | core | §21.6.2 |
| `bgl.util.math.pow(base, exp)` | function | core | §21.6.2 |
| `bgl.util.math.shiftLeft(x, n)`, `shiftRight(x, n)` | function | core | §21.6.2 |
| `bgl.util.math.sign(v)` | function | core | §21.6.2 |
| `bgl.util.math.unsignedCompare(a, b)` | function | core | §21.6.2 |
| `bgl.util.math.unsignedDiv(a, b)`, `unsignedMod(a, b)` | function | core | §21.6.2 |
| `bgl.util.random` | namespace | core | §21.6.3 |
| `bgl.util.random.get(n)`, `get(a, b[, c …])` | function | core | §21.6.3 |
| `bgl.util.random.seed(s)` | function | core | §21.6.3 |
| `bgl.wordsize` | constant | core | §21.3 |
| `bgl.world` | namespace | core | §21.9 |
| `bgl.world.getAll([pred])` | function | core | §21.9 |
| `bgl.world.inParent(parent[, pred])` | function | core | §21.9 |
| `bgl.world.instances(cls[, pred])` | function | core | §21.9 |
| `bgl.zcode` | namespace | core (Z-machine) | §21.3 |
| `bglAllocated` | type (mixin class) | core | §21.8 |
| `bglAutoInitialize` | symbol (mirrors `autoInitialize`) | compiler | §14.2.4, §23.5, Appendix F |
| `bglClass` | type | core | §21.5.6 |
| `_bglGlobalDeclaration()` | emitter hook on a class (one top-level I6 declaration per instance; `$selfsub`) | core | §16.8 |
| `bglInit()` | function | core | §21.2, §18.8 |
| `_bglObject` | type (emitter class) | core | §21.5.8 |
| `_bglPrimitive` | type (emitter class) | core | §21.5.8 |
| `bglSize` | type (struct) | core | §21.12 |
| `bglStringDefaultSize` | constant (I6, set before the include) | `<buf>` | §22.2 |
| `bglStringPoolReserve` | constant (I6, set before the include) | `<string>` | §22.3 |
| `bGlulxWindowBorder` | type (bnum) | core (Glulx) | §22.7.10 |
| `bGlulxWindowPlacement` | type (bnum) | core (Glulx) | §22.7.10 |
| `bGlulxWindowScale` | type (bnum) | core (Glulx) | §22.7.10 |
| `checkStyle(styleType, s)`, `distinguishes(a, b)` | method on `window` | `<glulxWindow>` | §22.7.9 |
| `bool` | type | core | §2.2 |
| `capture()`, `release()` | method on `stringObj` | `<string>` | §22.3 |
| `captureOutput(obj, prop)` | method on `stringObj` | `<string>` | §22.3 |
| `char` | type | core | §2.2, §21.7 |
| `children` | member on `object` | core | §11.5.2, §21.5.7 |
| `childrenProp` | type | core | §21.5.7 |
| `childStyles` | member on `window`, the roots (refers to a `styleSheet`) | `<glulxWindow>` | §22.7.7 |
| `clear()` | method on `array<T>` | `<array>` | §12.3, §22.4 |
| `clear()` | method on the text window types, the roots | `<glulxWindow>` | §22.7.11 |
| `close()` | method on `window` | `<glulxWindow>` | §22.7.6 |
| `CLR_*` | constant (colors) | binding | §23.4 |
| `clr_on`, `clr_fg`, `clr_bg`, `clr_fgstatus` | variable (extern) | binding (`punyInform`) | §23.4 |
| `color.black`, `white`, `red`, `green`, `blue`, `yellow`, `cyan`, `magenta`, `gray`, `lightGray`, `darkGray` | constant (on `bgl.glulx.color`) | `<glulxWindow>` | §22.7.8 |
| `color.rgb(r, g, b)` | method on `bgl.glulx.color` | `<glulxWindow>` | §22.7.8 |
| `compareTo(other[, caseInsensitive])` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `contains(item)` | method on `array<T>` | `<array>` | §22.4 |
| `copy(other)` | method on `bglAllocated` | core | §21.8 |
| `count()` | method on `array<T>` | `<linq>` | §22.5 |
| `create(…)` | method on a pooled class (author-declared; run by `new`) | core | §8.2.6 |
| `DEBUG` | symbol (defined by `--debug`) | compiler | §14.2.4, §20.3, §21.4, Appendix F |
| `deinit()`, `static deinit(T v)` | lifecycle emitter / value-form method on a class | core | §8.5, §12.10 |
| `delete(pos, count)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `destroy()` | method on a pooled class (author-declared; run by `delete`) | core | §8.2.6 |
| `dictionaryWord` | type | core | §13.1, §21.5.3 |
| `Directions` | object (extern; the shared direction placeholder) | binding (`punyInform`) | §23.4 |
| `distinct()` | method on `array<T>` | `<linq>` | §22.5 |
| `door`, `absent`, `pluralname`, `male`, `female`, `neuter` | value (`attribute`) | binding (`i6StandardLibrary`) | §23.4 |
| `drawImage(img, x, y[, width[, height]])` | method on `graphicsWindow` | `<glulxWindow>` | §22.7.5 |
| `drawImage(img[, align[, width[, height]]])` | method on `textBufferWindow`, `bgl.ui.mainWin` | `<glulxWindow>` | §22.7.5 |
| `eAssets` | type (named union) | core | §21.12 |
| `eBool` | type (enum) | core | §2.7.4 |
| `eErrorFormat` | type (enum) | core | §15.3, Appendix E |
| `eGlulxImageAlign` | type (enum) | core (Glulx) | §22.7.10, §21.11 |
| `eGlulxJustify` | type (enum) | `<glulxWindow>` | §22.7.10 |
| `eGlulxStyleHint` | type (enum) | `<glulxWindow>` | §22.7.10 |
| `eGlulxStyleType` | type (enum) | core (Glulx) | §22.7.10 |
| `eGlulxWindowType` | type (enum) | core (Glulx) | §22.7.10 |
| `eCharOutput` | type (enum) | `<glulxWindow>` | §22.7.12 |
| `eStyleCheck` | type (enum) | `<glulxWindow>` | §22.7.9 |
| `eImages` | type (enum) | core | §15.6.1, §21.12 |
| `enqueue(item)`, `dequeue()` | method on `array<T>` | `<array>` | §22.4 |
| `eSounds` | type (enum) | core | §15.6.1, §21.12 |
| `eTarget` | type (enum) | core | §15.3, Appendix E |
| `eType` | type (enum) | core | §2.8.1, §21.5.9 |
| `eUnknownAsset` | type (enum) | core | §15.6.1, §21.12 |
| `FAKE_*_OBJ` | constant (parser sentinels) | binding (`punyInform`) | §23.4 |
| `filter(pred)` | method on `array<T>` | `<linq>` | §22.5 |
| `first()`, `last()` | method on `array<T>` | `<linq>` | §22.5 |
| `float` | type | core (Glulx) | §2.3 |
| `format(pattern[, p1[, p2]])` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `func<…>` | type | core | §2.9 |
| `gg_mainwin`, `gg_statuswin`, … | variable (Glulx window globals) | binding (`i6StandardLibrary`) | §23.2 |
| `give(attr)`, `ungive(attr)` | method on `object`, `attributeList` | core | §21.5.1, §11.5.3 |
| `glulxImage` | type (primitive class) | `<glulxImage>` | §22.8 |
| `grammar` | member on `verb` | core | §13.2, §13.4.5 |
| `grammarRule` | type | core | §13.4.1 |
| `grammarRuleList` | type | core | §13.4.1 |
| `grammarToken` | type (extern enum) | binding | §13.4.1, §23.3.6 |
| `graphicsWindow` | type | `<glulxWindow>` | §22.7.1 |
| `handler()` | method on `verb` | core | §13.2.1 |
| `has(attr)`, `hasnt(attr)` | method on `object`, `attributeList` | core | §21.5.1, §11.5.3 |
| `height` | member on `bgl.ui.statusBar`, `window` | core; `<glulxWindow>` | §21.10, §22.7.4 |
| `I6_STANDARD_LIBRARY` | symbol (`#declare`) | binding (`i6StandardLibrary`) | §23.3.3 |
| `id` | member on `bgl.ui.mainWin`, `bgl.ui.statusBar`, `window` | core | §21.10, §22.7.1 |
| `indexOf(item)`, `find(item)` | method on `array<T>` | `<array>` | §22.4 |
| `indexOf(search)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `INFORMV__TX`, `LIBRARYV__TX`, `LibRelease` | constant | binding (`i6StandardLibrary`) | §23.3.9 |
| `init()` | lifecycle emitter on a class (fires at a local's declaration) | core | §8.5 |
| `Initialise()` | function (entry point the library calls; author-defined) | binding | §23.3.1 |
| `insert(pos, item)` | method on `array<T>` | `<array>` | §22.4 |
| `insert(pos, src)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `int` | type | core | §2.2 |
| `intLiteral`, `negativeIntLiteral`, `stringLiteral`, `charLiteral`, `dictionaryWordLiteral`, `interpolatedStringLiteral` | type (literal pseudo-type) | core | §2.4 |
| `interpolatedString` | type (a routine that prints an interpolated string) | core | §2.9.1 |
| `iString` | type (named union:  `string \| interpolatedString`) | binding | §2.8.2, §21.5.10, §23.3.7 |
| `iStringOrObject` | type (named union:  `iString`'s members and `object`) | binding | §21.5.10, §23.3.7 |
| `iStringOrRoutine` | type (named union:  `iString`'s members and `func<void>`) | binding | §21.5.10, §23.3.7 |
| `nullLiteral` | type (literal pseudo-type:  the type of `null`) | core | §2.5 |
| `is(Class)` | method on `object` | core | §21.5.6 |
| `isEmpty()` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `isLower()`, `isUpper()`, `isAlpha()`, `isNumeric()`, `isAlphaNumeric()`, `isVowel()`, `isConsonant()` | method on `char` | core | §21.7 |
| `isInterpolated()` | method on `iString` | binding | §21.5.10 |
| `isObject()` | method on `iStringOrObject` | binding | §21.5.10 |
| `isRoutine()` | method on `iStringOrRoutine` | binding | §21.5.10 |
| `isTracked()` | method on `array<T>` | `<array>` | §22.4 |
| `itobj`, `himobj`, `herobj` | variable (pronoun objects) | binding | §23.4 |
| `length` | property on `array<T>` (assignable with `<array>`) | core; `<array>` | §12.3, §22.4 |
| `length` | property on `string`, `stringObj` (read-only) | `<string>` | §22.3 |
| `length`, `size` | property on `children` (read-only) | core | §11.5.2, §21.5.7 |
| `light`, `container`, `scenery`, `static`, … (library attributes) | value (`attribute`) | binding | §23.3.4 |
| `location`, `player`, `actor`, `score`, `turns` | variable (extern) | binding | §23.3.4 |
| `bgl.output` | namespace | core | §21.4 |
| `bgl.output.log(v)` | function | core | §21.4 |
| `bgl.output.spaces(n)` | function | core | §21.4 |
| `map(f)` | method on `array<T>` | `<linq>` | §22.5 |
| `measureStyle(styleType, hint)`, `styleHonored(styleType, hint)` | method on `window` | `<glulxWindow>` | §22.7.9 |
| `meta` | member on `verb` | core | §13.2.4 |
| `mid(start, count)`, `left(count)`, `right(count)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `moveCursor(col, line)` | method on `bgl.ui.statusBar`, `textGridWindow` | core; `<glulxWindow>` | §21.10, §22.7.6 |
| `moveUp()`, `moveDown()`, `moveLeft()`, `moveRight()` | method on `window` | `<glulxWindow>` | §22.7.4 |
| `name` | property (additive, on `object`) | core | §11.7.2, §21.5.2 |
| `NO_ATTRIBUTE` | constant | core | §21.5.1 |
| `n_obj` … `d_obj` (compass direction objects) | object (extern) | binding (`i6StandardLibrary`) | §23.3.4, §23.4 |
| `null` | value | core | §2.5 |
| `NOUN`, `HELD`, `CREATURE`, `TOPIC`, `MULTI`, `MULTIHELD`, `MULTIEXCEPT`, `MULTIINSIDE`, `SPECIAL`, `ANYNUMBER`, `NUMBER`, `SCOPE`, `REVERSE` | constant (`grammarToken` values) | binding | §13.4.2, §23.3.6 |
| `noun`, `second`, `action`, `verb_word` | variable (extern; `action` is a `verb`) | binding | §23.3.4, §13.3 |
| `object` | type | core | §2.2, §11 |
| `orderBy([compare])` | method on `array<T>` | `<linq>` | §22.5 |
| `parent` | member on `object` | core | §11.5.1, §21.5.7 |
| `parentProp` | type | core | §21.5.7 |
| `patternElement` | type | core | §13.4.1 |
| `peekEnd()`, `popEnd()` | method on `array<T>` | `<array>` | §22.4 |
| `perform([noun[, second]])` | method on `verb` | core | §13.2.2 |
| `PlayerTo()`, `TestScope()`, `StartTimer()`, `StatusLineHeight()`, … | function (extern library routine) | binding | §23.3.4 |
| `prepend(item)` | method on `array<T>` | `<array>` | §22.4 |
| `print()` | method on `_bglObject` (author-defined override) | core | §21.4 |
| `print()` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `print(v)` | function (overloaded by type; global alias of `bgl.output.print`) | core | §10.5, §21.4 |
| `printLine([v])` | function (`print(v)`, then a newline; global alias of `bgl.output.printLine`) | core | §10.5, §21.4 |
| `print(v)`, `printLine([v])` (window output) | method on the text window types, `bgl.ui.mainWin`; `print` on `bgl.ui.statusBar` | `<glulxWindow>` | §22.7.11 |
| `printName(obj)` | function (global alias of `bgl.output.printName`) | core | §10.5, §21.4 |
| `priority` | member on `verb` | core | §13.2.5 |
| `property` | type | core | §11.7, §21.5.2 |
| `provides(prop)` | method on `object` | core | §11.7.1, §21.5.2 |
| `PUNYINFORM` | symbol (`#declare`) | binding (`punyInform`) | §23.3.3 |
| `push(item)`, `pop()`, `peek()` | method on `array<T>` | `<array>` | §22.4 |
| `rawArray<T>` | type | core | §12.8 |
| `remaining()` | method on `bglAllocated` | core | §21.8 |
| `remove(pos)` | method on `array<T>` | `<array>` | §22.4 |
| `removeValue(item)` | method on `array<T>` | `<array>` | §22.4 |
| `replace(search, repl)`, `replaceAll(search, repl)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `reverse()` | method on `array<T>` | `<array>` | §22.4 |
| `reverse()` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `selected_direction`, `selected_direction_index` | variable (extern) | binding (`punyInform`) | §23.4 |
| `setBackgroundColor(color)` | method on `graphicsWindow` | `<glulxWindow>` | §22.7.5 |
| `setStyle(styleType, style {…})`, `clearStyle(styleType)` | method on the text window types, the roots, `bgl.ui.screen` | `<glulxWindow>` | §22.7.7 |
| `short_name` | member on `object` | library bindings | §21.4, §23.3.11 |
| `instanceName` | member on `_bglObject` (`inline const string`) | core | §8.3.5, §11.5.4 |
| `size` | property on `array<T>` (read-only) | core | §12.3 |
| `size` | property on `glulxImage` (read-only); method `size()` on `eImages` | `<glulxImage>` | §22.8 |
| `size`, `length` | property on `array<char>` (`length` assignable) | `<buf>` | §22.2 |
| `isTracked()` | method on `array<char>` | `<buf>` | §22.2 |
| `sort([compare])` | method on `array<T>` | `<array>` | §22.4 |
| `splitUpGrid()` … `splitRightBuffer()` (12 combinations of direction and kind) | method on `window`, the roots | `<glulxWindow>` | §22.7.3 |
| `supports` | member on `window`, the roots (`images`, `mouse`, `hyperlinks`) | `<glulxWindow>` | §22.7.12 |
| `startsWith(prefix)`, `endsWith(suffix)`, `contains(search)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `story`, `headline` | constant (I6) and `extern string` | binding | §23.3.2 |
| `string` | type | core | §2.2, §22.3 |
| `stringObj` | type | `<string>` | §22.3 |
| `style` | emitter namespace (built-in; called as `style.MEMBER()`; the same word as the struct below) | core | §7.7 |
| `styleFlag` | type (primitive class; `italics`, `fixedWidth`, `reverse`) | `<glulxWindow>` | §22.7.7 |
| `styleSheet` | type (struct; one `style` per style type) | `<glulxWindow>` | §22.7.7 |
| `style` | type (struct; written `style { … }`; the same word as the emitter namespace above) | `<glulxWindow>` | §22.7.7 |
| `styleUnset` | constant | `<glulxWindow>` | §22.7.9 |
| `swap(pos1, pos2)` | method on `array<T>` | `<array>` | §22.4 |
| `switchable`, `on`, `workflag`, `scored` | value (`attribute`) | binding (both) | §23.4 |
| `reactive` | value (`attribute`) | binding (`punyInform`) | §23.4 |
| `take(n)`, `skip(n)` | method on `array<T>` | `<linq>` | §22.5 |
| `Take`, `Look`, `Receive`, … (the library's actions) | object (`extern verb`) | binding | §23.3.6 |
| `takeWhile(pred)`, `skipWhile(pred)` | method on `array<T>` | `<linq>` | §22.5 |
| `textBufferWindow` | type | `<glulxWindow>` | §22.7.1 |
| `textBufferWindowHorz`, `textBufferWindowVert`, `textGridWindowHorz`, `textGridWindowVert`, `graphicsWindowHorz`, `graphicsWindowVert` | type (orientation views) | `<glulxWindow>` | §22.7.3 |
| `textGridWindow` | type | `<glulxWindow>` | §22.7.1 |
| `the(obj)`, `cThe(obj)` | function | core | §21.4 |
| `thedark`, `selfobj` | object (extern) | binding | §23.3.4 |
| `themobj` | variable (pronoun object) | binding (`punyInform`) | §23.4 |
| `toUpper()`, `toLower()` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `toUpper()`, `toLower()` | method on `char` | core | §21.7 |
| `trim()`, `trimLeft()`, `trimRight()` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `typeof(v)` | function | core | §2.8.1, §21.5.9 |
| `uint` | type | core | §21.6.1 |
| `var` | type | core | §2.6 |
| `verb` | type | core | §13.2, §21.5.4 |
| `void` | type | core | §2.2 |
| `width` | member on `bgl.ui.mainWin`, `bgl.ui.statusBar` (read-only), `window` | core; `<glulxWindow>` (writable) | §21.10, §22.7.4 |
| `width`, `height` | property on `glulxImage` (read-only); methods `width()`, `height()` on `eImages` | `<glulxImage>` | §22.8 |
| `window` | type | `<glulxWindow>` | §22.7.1 |

---

# Appendix J Limits

Here is every numeric limit stated in this specification.  *Target* is the target the limit applies to:  `both`, `Z-machine`, or `Glulx`; a paired value is written `Z-machine / Glulx`.  Each limit is covered in the section named in the last column.

| Limit | Value | Target | Section |
| --- | --- | --- | --- |
| Local slots per routine, parameters and locals together | 15 | Z-machine | §18.10 |
| Arguments passed natively on a routine call | 5 | Z-machine | §6.3 |
| Frame pool size, in words (`framePoolSize`) | sized from the program, and at least 64, by default; at least 1 when set | both | §15.4, §18.10 |
| Words per object property (a `rawArray<T>` member's capacity) | 32 | Z-machine | §12.7, §12.8.3 |
| Distinct values in one `bnum` | 16 / 32 | Z-machine / Glulx | §2.7.2 |
| Global variables, shared with the library and the runtime | 233 | Z-machine | §18.12 |
| Attributes, shared with the library | 48 | Z-machine | §18.12 |
| Common properties, shared with the library | 61 | Z-machine | §18.12 |
| Readable memory | 64K | Z-machine | §18.12 |
| Entries in one array | 32,767 | Z-machine | §18.12 |
| Parameters of a pooled class's `create()` | 3 | Z-machine | §8.2.6 |
| `create()` and `destroy()` declarations per pooled class | one of each | both | §8.2.6 |
| Type parameters of `array<T>` and `rawArray<T>`, the only types which take one | 1 | both | §8.1.1 |
| `operator auto()` declarations per class | 1 | both | §9.5.1 |
| Member types in a named union | at least 2 | both | §2.8.2 |
| Integer literal in an `array<char>` initializer or element write | 0..255 | both | §12.4 |
| Assigned array `length` range | 0..32767 / 0..2^31-1 | Z-machine / Glulx | §12.3 |
| Include nesting depth | 255 | both | §18.4 |
| Compile-time errors reported per build | 1 | both | §19.1 |
| Words in the heap for local arrays and `<linq>` results (`heapSize`) | 1536 on the Z-machine, 16384 on Glulx by default (also for 0); otherwise at least 64 | both | §12.6, §15.4 |
| Arguments to `stringObj.format()` after the pattern (`$1`, `$2`) | 2 | both | §22.3 |
| String objects in the `<string>` pool (`bglStringPoolReserve`); running out stops the program | 10 by default | both | §22.3 |
| Tracked `<buf>` buffers beyond the `<string>` pool's (`bglTrackedBufferReserve`) | 116 by default | both | §22.2 |
| The room for a capture into an untracked buffer (`bglStringDefaultSize`) | 500 by default | both | §22.2 |
| Bytes in the `<string>` heap (`stringHeapSize`) | 5120 by default (also for 0) | both | §22.3 |
| Characters captured into a `stringObj` at once, and characters of a literal which other `<string>` operations read (`stringCaptureSize`) | 512 by default (also for 0) | both | §22.3 |
| `<buf>` capture nesting depth | 16 | both | §22.2 |
| Objects per `bgl.world` result buffer (`worldBufSize`); a longer walk stops at the limit | 128 by default; at least 1 | both | §15.4, §21.9 |
| `bgl.world` result buffers, rotating; a result is valid until the fourth subsequent query | 4 | both | §21.9 |
| Values listed in `bgl.util.random.get(a, b, …)` | 2 to 8 | both | §21.6.3 |
| Child windows per kind (`<glulxWindow>`):  text-buffer, text-grid, graphics | 8 of each | Glulx | §22.7.1 |
| Word size (`bgl.wordsize`), in bytes | 2 / 4 | Z-machine / Glulx | §21.3 |
| Width of `int` and `uint`, in bits | 16 / 32 | Z-machine / Glulx | §21.6.1 |
| Digits in the `serial` setting | exactly 6 | both | §15.3 |
| Major version representable in the `beguiler` symbol | 32 | Z-machine | Appendix F |
| ZSCII codes of the extended characters which can be typed directly | 155..224 | Z-machine | Appendix D.5 |

---

# Appendix K Gotchas

This appendix collects behavior which follows from the rules but tends to surprise.  Each entry shows the code, what happens, why, and how to get what you meant.  Every entry is covered in the section it names.

## K.1 A Method Named Like a Library Property Is That Property

<!-- doctest: skip -->
```bgl
class Card : object {
    stringObj Describe() { return "a card"; }
}
Card cardA { "red card"; parent = roomA; }
```

`cardA` will no longer appear in the room's "You can see …" list, and nothing warns you.

Since Beguile names are case-insensitive, `Describe` is `describe`, and a member named like a library property keeps that name (§16.9):  the method becomes the object's `describe` property.  The standard library calls `describe` when it lists an object in a room and treats a non-zero result as "the object described itself; don't list it" (§23.3.11).  The method returns a string, which is never zero.

Give the method a name of its own (`Details()`), or, to take over the room listing on purpose, declare it with the return type the library expects:  `bool describe()`, returning true only when it printed the object's paragraph.

> ***Note:** The library's properties are listed with their binding (§23.3); the compiler doesn't check a program's member against the library's expectations.*

## K.2 An `IdentifyGlkObject` Returning True Stops Window Recovery

<!-- doctest: skip -->
```bgl
int IdentifyGlkObject(int phase, int type, int ref, int rock) {
    if (phase == 2) { redrawPanels(); return 1; }
    return 0;
}
```

After a restore, the `<glulxWindow>` windows aren't reopened or closed, and printing to one which Glk closed is a fatal error.

The standard library calls the program's `IdentifyGlkObject` first, and calls its extensions' only when that returns false; the binding's call to `bgl.ui.recoverWindows()` (§21.10) is one of those extensions.  Returning true in phase 2 tells the library the program has handled everything.

Return `0` from phase 2, or call `bgl.ui.recoverWindows()` yourself before returning true.
