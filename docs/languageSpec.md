<!-- GENERATED FILE — do not edit.
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
  - [Audiences](#audiences)
  - [What This Specification Is Not](#what-this-specification-is-not)
  - [How to Read It](#how-to-read-it)
  - [Conventions](#conventions)
    - [Typography](#typography)
    - [Syntax Notation](#syntax-notation)
    - [Examples](#examples)
    - [Asides](#asides)
    - [Target Markers](#target-markers)
    - [Cross-References](#cross-references)
    - [Entry Shape](#entry-shape)
- [Introduction to Beguile](#introduction-to-beguile)
  - [What Beguile Is](#what-beguile-is)
  - [Design Goals](#design-goals)
  - [Relationship to Inform 6](#relationship-to-inform-6)

**Part I — The Beguile Language**
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
  - [1.7 Operators and Punctuation](#17-operators-and-punctuation)
  - [1.8 Directive Tokens](#18-directive-tokens)
- [2 Types and Values](#2-types-and-values)
  - [2.1 Overview](#21-overview)
  - [2.2 Primitive Types](#22-primitive-types)
  - [2.3 The `float` Type](#23-the-float-type)
  - [2.4 Literal Pseudo-Types](#24-literal-pseudo-types)
    - [2.4.1 `negativeIntLiteral`](#241-negativeintliteral)
    - [2.4.2 `interpolatedStringLiteral`](#242-interpolatedstringliteral)
  - [2.5 `nothing` and `null`](#25-nothing-and-null)
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
  - [2.10 Class Types as Values](#210-class-types-as-values)
  - [2.11 Type Compatibility](#211-type-compatibility)
  - [2.12 Conversion](#212-conversion)
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
- [7 Emitters](#7-emitters)
  - [7.1 What an Emitter Is](#71-what-an-emitter-is)
  - [7.2 Emitter Functions](#72-emitter-functions)
  - [7.3 Substitution Tokens](#73-substitution-tokens)
    - [7.3.1 `$opref` and `$oprefReq`](#731-opref-and-oprefreq)
    - [7.3.2 Choosing `$self`, `$val` and `$target`](#732-choosing-self-val-and-target)
  - [7.4 Conditional Text: `##if`, `##else`, `##endif`](#74-conditional-text-if-else-endif)
  - [7.5 Global Emitters](#75-global-emitters)
  - [7.6 Emitter Values](#76-emitter-values)
  - [7.7 Emitter Namespaces](#77-emitter-namespaces)
  - [7.8 `operator auto()`](#78-operator-auto)
  - [7.9 Emitter Methods on Enums and Bnums](#79-emitter-methods-on-enums-and-bnums)
  - [7.10 Emitters and Functions Compared](#710-emitters-and-functions-compared)
- [8 Classes](#8-classes)
  - [8.1 Class Declaration](#81-class-declaration)
    - [8.1.1 Type Parameters](#811-type-parameters)
  - [8.2 Class Forms](#82-class-forms)
    - [8.2.1 Normal Classes](#821-normal-classes)
    - [8.2.2 `extern class`](#822-extern-class)
    - [8.2.3 `emitter class`](#823-emitter-class)
    - [8.2.4 `alias class`](#824-alias-class)
    - [8.2.5 Veneer Classes (`extern emitter class`)](#825-veneer-classes-extern-emitter-class)
    - [8.2.6 Pooled Classes](#826-pooled-classes)
    - [8.2.7 `byVal class`](#827-byval-class)
    - [8.2.8 `typesealed` Members](#828-typesealed-members)
  - [8.3 Members](#83-members)
    - [8.3.1 Member Variables](#831-member-variables)
    - [8.3.2 `const` Members](#832-const-members)
    - [8.3.3 `static` Members](#833-static-members)
    - [8.3.4 Owned Members](#834-owned-members)
    - [8.3.5 `inline` Members](#835-inline-members)
  - [8.4 Methods](#84-methods)
  - [8.5 Lifecycle: `init` and `deinit`](#85-lifecycle-init-and-deinit)
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
- [11 Objects](#11-objects)
  - [11.1 Overview](#111-overview)
  - [11.2 Declaring an Object](#112-declaring-an-object)
  - [11.3 Inline Objects — `Type{ … }`](#113-inline-objects-type-)
    - [11.3.1 Positional and Named Members](#1131-positional-and-named-members)
    - [11.3.2 Type Inference from the Target](#1132-type-inference-from-the-target)
    - [11.3.3 Inline Objects as Arguments](#1133-inline-objects-as-arguments)
    - [11.3.4 Constant and Run-time Members](#1134-constant-and-run-time-members)
    - [11.3.5 Nested Aggregates](#1135-nested-aggregates)
    - [11.3.6 Standalone Declarations](#1136-standalone-declarations)
  - [11.4 Members and Type Inference](#114-members-and-type-inference)
  - [11.5 Special Members: `parent`, `children`, `attributes`](#115-special-members-parent-children-attributes)
    - [11.5.1 `parent`](#1151-parent)
    - [11.5.2 `children`](#1152-children)
    - [11.5.3 `attributes`](#1153-attributes)
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
  - [12.4 Byte Arrays — `array<char>`](#124-byte-arrays-arraychar)
  - [12.5 Assignment and Copy Semantics](#125-assignment-and-copy-semantics)
  - [12.6 Local Arrays and Lifetime](#126-local-arrays-and-lifetime)
  - [12.7 Member Arrays](#127-member-arrays)
  - [12.8 `rawArray<T>`](#128-rawarrayt)
    - [12.8.1 Raw Views](#1281-raw-views)
    - [12.8.2 File-scope `rawArray<T>` Literals](#1282-file-scope-rawarrayt-literals)
    - [12.8.3 Member `rawArray<T>`](#1283-member-rawarrayt)
  - [12.9 Arrays of Arrays](#129-arrays-of-arrays)
  - [12.10 Element Type Requirements](#1210-element-type-requirements)
  - [12.11 `extend` for Arrays](#1211-extend-for-arrays)
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
    - [13.4.4 Line Modifiers: `reverse`, `withI6Synonyms`](#1344-line-modifiers-reverse-withi6synonyms)
    - [13.4.5 Grammar on Verbs and Grammar Objects](#1345-grammar-on-verbs-and-grammar-objects)
  - [13.5 Extending Grammar](#135-extending-grammar)
    - [13.5.1 `grammar +=`](#1351-grammar-)
    - [13.5.2 `grammar -=`](#1352-grammar--)
    - [13.5.3 `replace grammar =`](#1353-replace-grammar-)
    - [13.5.4 Synonyms](#1354-synonyms)
- [14 Directives](#14-directives)
  - [14.1 Source Organization](#141-source-organization)
    - [14.1.1 `#include <name>`](#1411-include-name)
    - [14.1.2 `#include "path"`](#1412-include-path)
    - [14.1.3 `#include @"path"`](#1413-include-path)
    - [14.1.4 `#include ?"path"` and `#include ?<name>`](#1414-include-path-and-include-name)
    - [14.1.5 `#includeI6`](#1415-includei6)
    - [14.1.6 `#once`](#1416-once)
  - [14.2 Symbols and Conditional Compilation](#142-symbols-and-conditional-compilation)
    - [14.2.1 `#define`](#1421-define)
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
    - [14.4.5 `##beguilerSettings.<key>` Substitution](#1445-beguilersettingskey-substitution)
  - [14.5 Islands](#145-islands)
    - [14.5.1 `#i6`](#1451-i6)
    - [14.5.2 `#bgl`, `#bglDecl`, `#bglStmt`](#1452-bgl-bgldecl-bglstmt)
  - [14.6 Namespace Import](#146-namespace-import)
    - [14.6.1 `#using`](#1461-using)
  - [14.7 Settings](#147-settings)
    - [14.7.1 `#beguilerSettings { … }`](#1471-beguilersettings-)
    - [14.7.2 `#beguilerSettings.prop`](#1472-beguilersettingsprop)
- [15 Inform 6 Interoperability](#15-inform-6-interoperability)
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

**Part II — The Beguiler Compiler**
- [16 Invoking the Compiler](#16-invoking-the-compiler)
  - [16.1 Synopsis](#161-synopsis)
  - [16.2 Positional Arguments](#162-positional-arguments)
  - [16.3 Options](#163-options)
    - [16.3.1 Pass-through switches](#1631-pass-through-switches)
  - [16.4 Precedence Between the Command Line and `#beguilerSettings`](#164-precedence-between-the-command-line-and-beguilersettings)
  - [16.5 Locating the Toolchain](#165-locating-the-toolchain)
  - [16.6 Language-Server Mode](#166-language-server-mode)
  - [16.7 Exit Status and Console Output](#167-exit-status-and-console-output)
  - [16.8 Entry Mode](#168-entry-mode)
  - [16.9 Examples](#169-examples)
- [17 Settings (`#beguilerSettings`)](#17-settings-beguilersettings)
  - [17.1 The Settings Block](#171-the-settings-block)
  - [17.2 Toolchain Paths](#172-toolchain-paths)
  - [17.3 Compilation Settings](#173-compilation-settings)
  - [17.4 Runtime Settings](#174-runtime-settings)
  - [17.5 Game Metadata](#175-game-metadata)
  - [17.6 Blorb Packaging](#176-blorb-packaging)
    - [17.6.1 Asset Discovery and `_blorbAssets.bgl`](#1761-asset-discovery-and-blorbassetsbgl)
    - [17.6.2 IFID Generation and Persistence](#1762-ifid-generation-and-persistence)
  - [17.7 Reading Settings From Source](#177-reading-settings-from-source)
  - [17.8 Bindings and the Library Banner Constants](#178-bindings-and-the-library-banner-constants)
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

**Part III — The Beguile Language Runtime (BLR)**
- [21 Runtime Core](#21-runtime-core)
  - [21.1 Overview](#211-overview)
  - [21.2 `bglInit()`](#212-bglinit)
  - [21.3 The `bgl` Namespace](#213-the-bgl-namespace)
  - [21.4 `print()`, `log()` and Article Helpers](#214-print-log-and-article-helpers)
  - [21.5 IF-Domain Types](#215-if-domain-types)
    - [21.5.1 `attribute` and `attributeList`](#2151-attribute-and-attributelist)
    - [21.5.2 `property`](#2152-property)
    - [21.5.3 `dictionaryWord`](#2153-dictionaryword)
    - [21.5.4 `verb`](#2154-verb)
    - [21.5.5 Grammar Types](#2155-grammar-types)
    - [21.5.6 `bglClass`](#2156-bglclass)
    - [21.5.7 `parentProp` and `childrenProp`](#2157-parentprop-and-childrenprop)
    - [21.5.8 `_bglObject`](#2158-bglobject)
    - [21.5.9 `eType` and `typeof()`](#2159-etype-and-typeof)
    - [21.5.10 `stringOrRoutine`](#21510-stringorroutine)
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
  - [22.8 `<glulxImage>`](#228-glulximage)
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
    - [23.3.9 `bgl.story`](#2339-bglstory)
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

---

# Front matter

# About This Specification

*Draft, September 2026. Describes the language as implemented by the current Beguiler compiler and
beguiLib.*

## Purpose and Scope

This specification is the reference for Beguile. It is organized in three parts and a set of
appendices:

- **Part I — The Beguile Language** (§1–§15) defines the language.
- **Part II — The Beguiler Compiler** (§16–§20) describes the compiler: how it is invoked, its
  settings, the compilation pipeline from source to story file, its diagnostics and its outputs.
- **Part III — The Beguile Language Runtime** (§21–§23) describes the library every program compiles
  against: the runtime core, the opt-in language extensions and the IF library bindings.
- **Appendices A–J** collect the lookup tables: reserved words, directives, operators, character
  escapes, settings, pre-defined symbols, substitution tokens, the glossary, a symbol index and a table
  of limits.

## Audiences

- **Game authors** writing interactive fiction in Beguile against one of the supplied library bindings
  (Part I; §23).
- **Library and binding authors** exposing an Inform 6 library to Beguile or writing reusable Beguile
  code (§7, §15, Part III).
- **Inform 6 developers** adopting Beguile incrementally inside an existing `.inf` project (§15).
- **Tool developers** building editors, debuggers and build integrations around the compiler's inputs
  and outputs (Part II).

## What This Specification Is Not

It is not an Inform 6 manual. The Inform 6 language, the libraries commonly used with it, and the
Z-machine and Glulx virtual machines are documented elsewhere and are assumed.

It is not a tutorial. Readers new to Beguile should start with the
[Quick Start](../quickStart.md). Readers coming from Inform 6 should start with
[Beguile for the I6 Developer](../Beguile%20for%20the%20I6%20Developer.md), which teaches the language
through a complete, runnable port and maps each construct back to the Inform 6 it replaces. This
specification is the reference to graduate to once the language is familiar.

## How to Read It

Part I is written in dependency order: each chapter assumes only the vocabulary of the chapters before
it, and where a rule needs a later construct the entry gives a one-sentence version and a forward
reference. Part I can therefore be read front to back. Parts II and III and the appendices are
reference material for lookup.

For a runtime or library name, start at Appendix I; for a reserved word, Appendix A; for an operator,
Appendix C; for a numeric limit, Appendix J. The terms of art used throughout — program, target,
directive, emitter, island, binding and the rest — are defined once, in the Glossary (Appendix H).

The conventions below apply to every chapter. The language itself is introduced in the next chapter,
*Introduction to Beguile*.

## Conventions

### Typography

Identifiers, keywords, operators, file names and any other literal source text appear in `code font`.
Keywords are written lowercase; Beguile is case-insensitive (§1.3), so `Print` and `print` are the
same word. A term is set in **bold** where it is defined. Inform 6 is abbreviated I6.

Within an entry, a paragraph that treats one alternative or one aspect of the rule opens with a bold
run-in head ending in a period, such as **Placement.** or **Capture.** The run-in is a label, not a
heading, and the paragraphs under one entry may be read in any order.

### Syntax Notation

Syntax forms are given in fenced blocks tagged `syntax`:

```syntax
[ ⟨qualifier⟩ … ] ⟨type⟩ ⟨name⟩ [ = ⟨initializer⟩ ] ;
```

- `⟨name⟩` — a placeholder, in angle quotes and lowercase, for author-supplied text.
- `[ … ]` — an optional part.
- `…` — repetition of the preceding part.
- `|` — alternation between parts.
- Every other character is literal.

The form above reads: zero or more qualifiers, then a type, then a name, then an optional initializer
introduced by `=`, then a semicolon. Where a form uses `[`, `]`, `|` or `<` as Beguile tokens (array
sizes, union types, type parameters), the sentence under the block says so.

A placeholder may also stand for part of a name. `split⟨direction⟩⟨kind⟩` (§22.7.3) names one method
for every combination of the listed values, spelled as a single identifier (`splitUpGrid`); the
sentence under the block lists the values. In running text and in tables, a form is abbreviated with
capitalized words in place of angle quotes: `Type::operator op` stands for `⟨type⟩::operator ⟨op⟩`
(§4.15), and `obj.member` for `⟨expr⟩.⟨member⟩`.

### Examples

An example is the smallest fragment that shows the rule. A `// →` comment gives the resulting value or
the text printed. Program output appears in blocks tagged `text`. Inform 6 appears in blocks tagged
`i6`, and only in sections about emission (Part II and §15); emitter bodies, which are raw I6 by
definition, are exempt. Examples use real keywords and real library names, and `Main` as the entry
point, so that they compile in principle. An example that uses a name declared by the Inform 6
Standard Library (`location`, `player`, `light`, `selfobj`, …) assumes `#include <i6StandardLibrary>`
(§23.2) and leaves the name undeclared.

### Asides

Material a reader may skip on a first reading — a consequence of the rule, a caution, a limit — is set
apart from the rule as an aside: a block quote that opens with a bold label.

> **Label.** The aside's text.

Target markers are asides of this kind.

### Target Markers

Behavior that depends on the virtual machine is marked `[Z-machine]`, `[Glulx]` or
`[Z-machine/Glulx difference]`, either inline or as an aside:

> **[Glulx]** Applies only when the target is Glulx.

Unmarked text applies to both targets.

### Cross-References

`§N.M` and `§N.M.K` refer to a section; inside a chapter they refer to that chapter's own sections. A
bare chapter number, `§N`, is used only when the whole chapter is meant. `Appendix X` refers to an
appendix.

### Entry Shape

Every language construct (keyword, directive, operator, declaration form, library method) is
documented as an entry with up to five labeled parts, in this order: **Syntax** (the form, in the
notation above), **Description** (the normative rule, including any shorter form that is equivalent to
it), **Example**, **Notes** (target markers, asides, limits) and **See also** (references to sections
that are not neighbors of the entry). A part that is genuinely empty is omitted. Chapters 16, 18, 19,
20 and 23 are narrative and do not use the shape; the extension entries of §22 use their own shape,
announced in §22.1.

---

# Introduction to Beguile

This chapter introduces the language itself: what Beguile is, the goals that shaped it, and how it
relates to Inform 6. The chapters of Part I then define it construct by construct.

## What Beguile Is

Beguile is a statically typed, compiled language for authoring interactive fiction. Its structured
syntax transpiles to Inform 6 and, from there, to a Z-machine or Glulx story file.

Although it borrows features from general-purpose languages, Beguile is not one. Its features are
shaped by the needs of interactive fiction and by the constraints of the virtual machines its programs
ultimately run on.

## Design Goals

- **Familiar syntax.** Beguile's syntax draws on C++, C# and TypeScript. Developers comfortable with
  those languages should find it intuitive.
- **A consistent syntax.** Beguile is designed upon a foundation of core rules — declarations live in
  member bodies, statements end in `;`, types precede names — and every construct follows them. The
  language seeks to minimize targeted micro-grammars for special features, in favor of general-purpose
  constructs that work the same way everywhere. Inform 6's verb grammar shows the alternative:
  `Verb 'take' * noun -> Take;` is a sub-language with its own tokens and punctuation, usable only in
  that directive. In Beguile a verb is an object, its grammar is a member initialized with an ordinary
  list, and changing it later uses the same `extend`, `+=` and `replace` that apply to any object
  (§13).
- **Strong typing.** Every variable, parameter and return value carries a declared type. Type mismatches
  are caught at compile time, not at run time.
- **Transparency.** The generated Inform 6 is readable and maps closely to the Beguile source.
  Developers who know Inform 6 can inspect or supplement the output.
- **Extensibility through emitters.** Performance-sensitive or platform-specific operations can be
  expressed as **emitters**: inline Inform 6 fragments substituted at the call site (§7). Library
  authors get full control over the generated code without giving up type safety at the Beguile level.

## Relationship to Inform 6

Beguile is built on top of Inform 6; it is not a replacement for it. It generates human-readable
Inform 6 source, and several features exist specifically to bridge the two languages:

- `extern` declarations let Beguile use types, functions, attributes and constants defined in Inform 6
  without re-implementing them.
- `#i6` islands place raw Inform 6 inline with Beguile code.
- The emitter subsystem gives authors precise control over the Inform 6 that is generated.

Authors who need capabilities beyond what Beguile exposes can always drop down to Inform 6 through these
mechanisms (§15).

---

# Part I — The Beguile Language

# 1 Lexical Structure

## 1.1 Source Text and Encoding

A source file is plain text encoded as UTF-8 or Latin-1. Outside comments, string literals, character
literals and dictionary words, source text is ASCII. Inside those, characters beyond ASCII may be typed
directly, provided each is one Beguile can represent on the target (Appendix D); any other character is
a compile-time error.

## 1.2 Comments

**Syntax**

```syntax
// ⟨text to end of line⟩
/* ⟨text⟩ */
```

**Description**

Beguile has two comment forms. Comments are discarded and have no effect on compilation.

**Example**

```bgl
// to the end of the line

/* to the closing
   delimiter */
```

## 1.3 Case-Insensitivity

Beguile is case-insensitive for every token except the contents of string literals. Keywords, type
names, identifiers and operator names are normalized to lowercase, so the following are equivalent:

```bgl
if(X == 1) print("yes");
IF(x == 1) Print("yes");
If(x == 1) PRINT("yes");
```

Casing within string literals is preserved as written.

## 1.4 Identifiers

**Syntax**

```syntax
⟨letter or underscore⟩ [ ⟨letter, digit or underscore⟩ … ]
```

**Description**

An identifier is a sequence of letters, digits and underscores whose first character is not a digit.
A reserved word (§1.5) should not be used as a name. A member may share its name with a type
(§3.8.3).

**Example**

```bgl
score   myVar   _internal   room1   velvetCloak
```

> **Reserved prefixes.** Names beginning with `_bgl` or `bgl` are earmarked for the language and its
> runtime: the compiler generates symbols with these prefixes (loop counters, scratch temporaries,
> `bglInit`), and the runtime library declares its own (`bgl`, `_bglObject`,
> `_bglGlobalDeclaration`). The compiler does not reject such a name in user code, but a name of your
> own that coincides with one of the system's can conflict with it, and the behavior is then
> undefined. Where the runtime exposes a prefixed name for authors to use, it is documented with the
> feature (§15.8, §21.5.8).

## 1.5 Reserved Words

The words below have meaning in Beguile and should not be used as names. The compiler recognizes most
of them only in the position where they carry that meaning, so it does not reject every use of one as a
name; the use is nevertheless unsupported. Declaring a type with the name of a built-in type is a
compile-time error, and a global named after a word shared with Inform 6 is rejected by the Inform 6
stage. Beguile is case-insensitive, so all of this applies in any letter case. Appendix A lists the same
words alphabetically, with the section that defines each.

**Declaration words** begin or qualify a declaration.

`alias` `asBgl` `asI6` `byVal` `class` `const` `default` `emitter` `explicit` `extend` `extern` `inline`
`operator` `ref` `replace` `static` `superposed` `typesealed`

**Type-forming words** build a type rather than name one.

`auto` `bnum` `enum` `func` `var` `void`

**Built-in type names** are the types the runtime core declares.

`array` `attribute` `bool` `char` `dictionaryWord` `float` `int` `object` `property` `rawArray`
`string` `uint` `verb`

**Literal pseudo-types** are the types of literal values.

`charLiteral` `dictionaryWordLiteral` `intLiteral` `interpolatedStringLiteral` `negativeIntLiteral`
`stringLiteral`

**Statement words** begin or structure a statement.

`break` `case` `catch` `continue` `delete` `do` `else` `for` `if` `in` `return` `rfalse` `rtrue`
`switch` `throw` `to` `try` `until` `while`

**Expression words** introduce an expression.

`new` `replaced`

**Value words** name a fixed value or the current receiver.

`false` `nothing` `null` `self` `true`

**Contextual words** have meaning in one position only and are ordinary identifiers elsewhere.

`hide` `inject` `move` `outer` `remove` `synonyms` `union`

**Words shared with Inform 6** also appear verbatim in the generated Inform 6, as keywords or
well-known identifiers, so a program that uses one as a name can produce Inform 6 that the Inform 6
compiler rejects.

`additive` `array` `attribute` `class` `false` `nothing` `object` `property` `replace` `self` `string`
`true` `verb`

Inform 6 reserves further words that Beguile does not; §15.9 describes them and the `asI6` clause that
keeps them out of generated names.

## 1.6 Literals

### 1.6.1 Integer Literals

**Syntax**

```syntax
⟨digits⟩
$⟨hex digits⟩
$$⟨binary digits⟩
```

**Description**

An integer literal is decimal, hexadecimal (prefix `$`, digits `0`–`9` `A`–`F` in either case) or
binary (prefix `$$`, digits `0` and `1`). A negative value is formed by prefixing `-`. `0x`
notation is a compile-time error.

An integer literal has the pseudo-type `intLiteral`; a negated one has `negativeIntLiteral` (§2.4).

**Example**

```bgl
42        $FF         $$11111111     // → 42, 255, 255
-1234     $0A         $$11010        // → -1234, 10, 26
```

### 1.6.2 Float Literals

**Syntax**

```syntax
⟨digits⟩.⟨digits⟩
.⟨digits⟩
```

**Description**

A float literal is a decimal number containing a `.` with at least one digit after it. A negative
value is formed by prefixing `-`. `1.` is not a float literal and is a compile-time error; write `1.0`.

**Example**

```bgl
1.0     .3     -1.2
```

**Notes**

> **Member access on a literal.** A `.` followed by anything other than a digit is a member access, so
> `42.someMethod()` calls a method on the integer `42` (§2.4) rather than beginning a float.

> **[Glulx]** Float literals and the `float` type (§2.3) exist only when the target is Glulx.

### 1.6.3 String Literals

**Syntax**

```syntax
"⟨characters⟩"
```

**Description**

A string literal is text enclosed in double quotes. Backslash escapes are recognized; the ones needed
in everyday text are:

| Escape | Character |
|---|---|
| `\n` | newline |
| `\"` | `"` |
| `\\` | `\` |
| `\^` | `^` |
| `\~` | `~` |
| `\@` | `@` |

Beguile preserves Inform 6's conventions for extended characters in strings: an unescaped `^` is a
newline and an unescaped `~` is a double quote, exactly as in I6, and both must be escaped to print
literally. Numeric escapes, diacritical shorthands, directly typed Unicode characters and the folding
of typographic quotes are specified in Appendix D.

A string literal has the pseudo-type `stringLiteral` (§2.4).

**Example**

```bgl
"Hello, world!"
"She said, \"well done.\""
"Line one^Line two"
```

### 1.6.4 Raw String Literals

**Syntax**

```syntax
@"⟨characters⟩"
```

**Description**

A raw string literal disables all escape processing except `\"`. Every other character between the
delimiters is taken literally, including `\`, `^` and `~`; a raw string therefore contains no I6
newline or quote. A raw string may appear anywhere a string literal may, including
`#beguilerSettings` values (§17.1), and has the same pseudo-type, `stringLiteral`.

**Example**

```bgl
string path = @"C:\Users\jim\IF-Games\medusa.bgl";
```

### 1.6.5 Interpolated String Literals

**Syntax**

```syntax
$"⟨characters⟩ { ⟨expression⟩ } ⟨characters⟩ …"
```

**Description**

An interpolated string literal is prefixed with `$` and may contain Beguile expressions inside `{ }`
spans. Escapes are those of a string literal, plus `\{` for a literal `{`. A `{ }` pair nested inside
an expression span is a compile-time error.

An interpolated string has the pseudo-type `interpolatedStringLiteral` (§2.4.2), which may be passed
only to an emitter declaring a parameter of that type; passing it to a non-emitter function is a
compile-time error. `print()` and `log()` accept it in the core runtime (§21.4).

**Example**

```bgl
object lamp { string title = "brass lamp"; int weight = 2; }

print($"The {lamp.title} weighs {lamp.weight} stone.");   // → The brass lamp weighs 2 stone.
print($"Press \{enter} to continue.");                    // → Press {enter} to continue.
```

### 1.6.6 Character Literals

**Syntax**

```syntax
'⟨character⟩'
```

**Description**

A character literal represents exactly one character and is written in single quotes. It accepts the
same escapes as a string literal and the same directly typed Unicode characters (Appendix D), so the
literal itself may be several characters long. A literal representing more than one character is a
compile-time error.

`\'` followed by a vowel in the acute-accent set is the acute accent (`'\'e'` is `é`); a `\'` not
followed by such a vowel is an escaped single quote. A character literal has the pseudo-type
`charLiteral` (§2.4).

**Example**

```bgl
'a'   '\n'   '\\'   'ä'   '\:a'
```

> **Not an I6 dictionary word.** Inform 6 writes a dictionary word in single quotes (`'sword'`).
> Beguile does not: single quotes are the character literal, and a dictionary word is written with a
> leading `.` (§1.6.7).

### 1.6.7 Dictionary Word Literals

**Syntax**

```syntax
.⟨word⟩
..⟨word⟩
```

**Description**

A dictionary word literal names an entry in the I6 dictionary, the tokens the parser matches player
input against. The `.` form is singular; the `..` form is plural. A `-` between two word characters is
part of the word, not the subtraction operator, and an apostrophe is likewise part of the word.

A dictionary word literal has the pseudo-type `dictionaryWordLiteral` and is compatible with
`dictionaryWord` (§2.4, §21.5.3). The meaning of singular and plural words is defined in §13.1.

**Example**

```bgl
.cloak   ..cloaks   .medium-sized   .monkey's
```

## 1.7 Operators and Punctuation

The following multi-character sequences are single tokens. A longer token always wins: `<=>` is the
three-way comparison, never `<=` followed by `>`.

```text
<<=  >>=  <=>
-=  +=  *=  /=  %=  &=  |=  ^=  :=  ?=  ==  !=  <=  >=  =~
&&  ||  ++  --  <<  >>  =>  ?.  ??
```

The single-character operators and punctuation are:

```text
=  +  -  *  /  %  <  >  !  &  |  ^  ?  :  .  ,  ;  #  (  )  {  }  [  ]
```

`::` immediately followed by an identifier forms one token with it, the global-scope qualifier
`::name` (§3.9). No other construct uses `::`.

Where a subscript operator is declared on a class or object, or referred to by name, `[]` and `[]=`
are single tokens naming it (§9.3, §4.15). Everywhere else — an array declaration, a subscript
expression — `[` and `]` are separate tokens.

The role of each operator and the section that specifies it are indexed in Appendix C.3; the
overloadable operators are listed in §9.1 and precedence is tabulated in §4.3.

## 1.8 Directive Tokens

**Syntax**

```syntax
#⟨identifier⟩
```

**Description**

A `#` immediately followed by an identifier, with no intervening whitespace, is a directive token:
`#include`, `#define`, `#if`, `#i6`. Directives are specified in §14 and indexed in Appendix B.

---

# 2 Types and Values

## 2.1 Overview

Beguile is statically typed: every variable, parameter and return value has a type known at compile
time. Types fall into four categories: primitive types (§2.2), literal pseudo-types (§2.4),
user-defined types (enumerations §2.7, unions §2.8, classes §8) and the `var` escape type (§2.6).
Arrays are covered in §12.

## 2.2 Primitive Types

**Description**

The primitive types are declared by the runtime core (§21) and need no `#include`.

| Type | Description |
|---|---|
| `int` | Signed integer, one native word. |
| `uint` | Unsigned integer, the same width as `int`. A non-negative integer literal converts to it implicitly; any other conversion between `int` and `uint` is an explicit cast (§2.4.1). Its operators are specified in §21.6.1. |
| `float` | IEEE 754 single-precision floating point. `[Glulx]` See §2.3. |
| `bool` | Boolean value, `true` or `false`. The comparison and logical operators and `operator ?()` yield `eBool`, the enumeration `{ true, false }` (§2.7.4); `eBool` and `bool` interoperate, so a comparison may be stored in a `bool` and a `bool` tested where an `eBool` is expected. Tables elsewhere in this specification write `bool` for either. |
| `char` | A single ZSCII character value. The runtime core (§21.7) adds case-conversion and inspection methods. |
| `string` | A reference to static text. The core provides printing, equality and literal assignment; the `<string>` extension (§22.3) compares content and adds `stringObj` for text that is built or changed. |
| `object` | The base class of every world object in the IF model (§11.1). |
| `verb` | The class from which verbs are declared (§13.2). |
| `void` | Not a value type: the return type of a function that returns nothing. |

`object`, `string`, `array` and `verb` share their names with I6 constructs and compile to them, but
are used with Beguile syntax and typing.

**Notes**

Beyond the primitive types, the library provides `array<T>` and `rawArray<T>` (§12.2, §12.8) and,
with `#include <string>`, `stringObj` for text that is built or changed (§22.3).

## 2.3 The `float` Type

**Description**

A `float` occupies one native word, the same width as `int`, holding an IEEE 754 single-precision
value. It supports the arithmetic operators `+` `-` `*` `/` `%` and their compound-assignment forms,
the comparisons `==` `!=` `<` `<=` `>` `>=`, and `print()`.

A `float` is initialized from a float literal (§1.6.2) or by assignment from an `int`, which converts
the numeric value (`5` becomes `5.0`). Conversion in either direction by cast is explicit:
`(float)n` and `(int)f` (the latter truncates). An `int` is never reinterpreted bit-for-bit as a
`float`.

**Example**

```bgl
#beguilerSettings { target = Glulx; }

void Main() {
    float a = 2.5;
    float b = 5;                    // → 5.0
    float c = (float)314 / (float)100;   // → 3.14
    print(b / a);                   // → 2
    if(b > a) print("^bigger^");
    int n = (int)c;                 // → 3
}
```

**Notes**

> **[Glulx]** `float` is available only when the target is Glulx; the Z-machine has no floating-point
> support. It is part of the core and needs no `#include`.

## 2.4 Literal Pseudo-Types

**Description**

A literal has a **pseudo-type** that is inferred by the compiler and never written by the author.
Pseudo-types take part in operator and overload resolution independently of the runtime types they
correspond to.

| Pseudo-type | Written | Corresponding type |
|---|---|---|
| `intLiteral` | `42`, `$FF`, `$$1010` | `int` |
| `negativeIntLiteral` | `-1` | `int` |
| `stringLiteral` | `"hello"`, `@"raw"` | `string` |
| `charLiteral` | `'a'` | `char` |
| `dictionaryWordLiteral` | `.cloak`, `..cloaks` | `dictionaryWord` (§21.5.3) |
| `interpolatedStringLiteral` | `$"hello {x}"` | none; see §2.4.2 |

A pseudo-type is compatible with its corresponding type only through an `operator =` declared on that
type (§2.11); there is no built-in rule. Pseudo-types are first-class types, declared as
`extern class` (§8.2.2) in the core and extensible with `extend` (§8.7.1), so a method defined
against one may be called directly on a literal.

**Example**

```bgl
"hello".print();       // a method on stringLiteral
42.someMethod();       // a method on intLiteral
```

### 2.4.1 `negativeIntLiteral`

**Description**

A negated integer literal such as `-1` has the pseudo-type `negativeIntLiteral` rather than
`intLiteral`. It converts to `int` implicitly but is not compatible with `uint` (§2.2); assigning
one to a `uint` requires an explicit cast.

**Example**

```bgl
int  x = -5;         // → -5
uint u = -1;         // compile-time error
uint v = (uint)-1;   // the largest unsigned value
```

### 2.4.2 `interpolatedStringLiteral`

**Description**

An interpolated string (§1.6.5) contains several segments that cannot be reduced to a single value, so
it has no corresponding runtime type. It may be passed only to an emitter that declares an
`interpolatedStringLiteral` parameter, where it expands into a block of statements; passing it to a
non-emitter function is a compile-time error. The core `print()` and `log()` accept it (§21.4); with the
`<string>` extension a `stringObj` may be assigned one, `stringObj s = $"…";` (§22.3).

## 2.5 `nothing` and `null`

**Description**

`nothing` is the absent or unset value. It is the value I6 uses to mean "no object", numerically `0`,
and Beguile exposes it under that name throughout the language. Its resolved type (§4.1) is `object`,
but it is compatible with every type. `null` is a synonym; the two are interchangeable.

For a reference, `nothing` is the absent state (a failed `new` on a pooled class, §8.2.6; an unset
member; a missing parent); for an integer it is `0`.

**Example**

```bgl
object o;
if(o == nothing) print("not yet set");

marbleClass m = new marbleClass();
if(m == null) print("pool exhausted");
```

## 2.6 The `var` Type

**Description**

`var` is a universal escape type that bypasses static type checking in both directions: any value may
be assigned to a `var`, and a `var` may be assigned to any type. The author is responsible for the
underlying value being meaningful; the compiler catches no mismatch involving `var`.

In overload resolution an overload with `var` parameters is a fallback, selected only when no typed
overload matches (§6.4).

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
enum ⟨name⟩ { ⟨member⟩ [ = ⟨integer⟩ ] [ , ⟨member⟩ [ = ⟨integer⟩ ] … ] }
```

**Description**

An `enum` declares a named set of integer constants. Values start at 1 and increment by 1. A member
may be given an explicit value, including a negative one; numbering resumes from the last assigned
value. Two members may hold the same value, but they are then indistinguishable at run time.

**Example**

```bgl
enum direction { north, south, east, west }   // → 1, 2, 3, 4

enum myPhase {
    setup  = 0,
    play   = 10,
    ending           // → 11
}
```

### 2.7.2 `bnum`

**Syntax**

```syntax
bnum ⟨name⟩ [ : ⟨base bnum⟩ ] { ⟨member⟩ [ = ⟨integer⟩ ] [ , ⟨member⟩ [ = ⟨integer⟩ ] … ] }
```

**Description**

A `bnum` is a bit-flag enumeration. Values start at 1 and double with each member, so members are
non-overlapping powers of two suitable for combination as flags. An explicit value must be a
non-negative power of two, and numbering resumes by doubling from it. `0` is always a legal value.

**Shared base.** A `bnum` may name a base `bnum`, declaring that it occupies a sub-field of the same
packed integer. The base must itself be a `bnum`; naming a plain `enum` as the base is a compile-time
error. Within a shared base the power-of-two rule is relaxed, since a sub-field's members need not be
single bits.

**Combining values.** Bitwise `|`, `&` and `^` between two `bnum` values are permitted only when the
operands share a common `bnum` ancestor (one may be the other's ancestor, or both the same type). The
result has the shared ancestor's type. Combining values with no common base is a compile-time error.

**Example**

```bgl
bnum itemFlag { portable, fragile, lit, locked }   // → 1, 2, 4, 8

bnum winMethodFlags { }                          // empty base: defines the bit-field space
bnum windowPlacement : winMethodFlags { left = 0, right = 1, above = 2, below = 3 }
bnum windowScale     : winMethodFlags { fixed = 16, proportional = 32 }
bnum windowBorder    : winMethodFlags { border = 0, noBorder = 256 }

int winMethod = left | fixed | noBorder;    // all share winMethodFlags
int bad       = left | itemFlag.portable;   // compile-time error
```

**Notes**

> **[Z-machine/Glulx difference]** A `bnum` may have at most 16 distinct values on the Z-machine and
> 32 on Glulx.

### 2.7.3 Widening to `int`

**Description**

A `bnum` value converts implicitly to `int`. The reverse, `int` to a `bnum`, requires an explicit
cast. A plain `enum` does not widen in either direction; converting between an `enum` and `int`
requires an explicit cast.

**Example**

```bgl
bnum itemFlag { portable, fragile }
enum direction { north, south }

int a = fragile;               // → 2: a bnum widens to int
itemFlag f = (itemFlag)2;      // int to bnum: explicit cast
int b = (int)south;            // → 2: enum to int: explicit cast
direction d = north;
int c = d;                     // compile-time error: an enum does not widen
```

### 2.7.4 `extern enum` and `extern bnum`

**Syntax**

```syntax
extern enum ⟨name⟩ { ⟨member⟩ [ , ⟨member⟩ … ] }
extern bnum ⟨name⟩ { ⟨member⟩ [ , ⟨member⟩ … ] }
```

**Description**

An `extern` enumeration names values that are defined in I6, not by Beguile. The declaration registers
the names for type checking and produces no output. `eBool`, Beguile's boolean-result type (§2.2), is
declared this way by the runtime core.

**Example**

```bgl
extern enum eBool { true, false }
extern enum eErrorFormat { E1, E2 }
```

### 2.7.5 Naming Members

**Description**

An enumeration member may be referenced by bare name (`north`, `true`) or qualified by its type
(`direction.north`, `eBool.true`). The qualified form is required when two enumeration types declare
a member of the same name.

Enumerations may carry emitter methods, declared in the body or added with `extend enum`; the form is
given in §7.9.

## 2.8 Union Types

**Syntax**

```syntax
⟨type⟩ | ⟨type⟩ [ | ⟨type⟩ … ]
```

The `|` here is the union type operator, a literal token.

**Description**

A union type declares that a value is one of several types, distinguished at run time. It is valid at
every type position: parameter, local, return type, class or object member, and array element type.

A union complements overloading rather than replacing it: an overload dispatches on the caller's
static type at compile time, whereas a union carries a value whose type is known only at run time (one
read from a member or an array) and is discriminated with `typeof` (§2.8.1).

A union type is also *inferred* in one place: a ternary whose two branches have unrelated types
resolves to their union (§4.9).

**Canonical form.** Members are order-independent and de-duplicated: `string | func<void>` and
`func<void> | string` name the same type, and a union of a type with itself is that type. Members may be listed in any order.

**Operations.** A union value may be assigned, passed, returned and compared with `==` and `!=`. It
may not be called, printed, member-accessed or used in arithmetic while it is still a union; doing so
is a compile-time error. The value is first **narrowed** with an ordinary cast `(⟨member⟩)x`. Because
every member shares one machine word, the cast retypes the value without converting it; it is an
assertion, and the author is responsible for having discriminated correctly.

**Compatibility.** A value of type `T` is assignable to a union `U` when `T` is compatible with some
member of `U`. A union `U₁` is assignable to a union `U₂` when every member of `U₁` is compatible with
some member of `U₂`. A union is not assignable to a plain member type without a narrowing cast. In
overload resolution a union parameter is the widest candidate: an exact or member-typed overload always
wins, and the union catches only arguments whose static type is itself a union.

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
typeof( ⟨expression⟩ )
```

**Description**

`typeof` returns the run-time machine category of any value, including a `var`, as an `eType`. It is
a function declared in the core, needs no `#include`, and works on both targets.

`typeof` reports machine categories, not source types:

- `bool`, `char` and enumeration values are represented as `int` and report `eType.int`. Two union
  members that share a representation, such as `int | bool`, cannot be told apart by `typeof`.
- A scalar that happens to equal a valid object number, or a string or routine address, is reported as
  that reference category. A union mixing a scalar with a reference type, such as `int | string`, must
  be discriminated by the author's own test, then narrowed with a cast.
- `nothing` and `null` report `eType.unknown`, as does any value whose category cannot be determined.

Objects and classes report `eType.object` and `eType.class`; a value's class is tested with
`x.is(SomeClass)` (§21.5.6).

**Example**

The core declares `eType` as follows; it is shown for reference, and a program does not declare it.

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
union ⟨name⟩ = ⟨type⟩ | ⟨type⟩ [ | ⟨type⟩ … ] ;
union ⟨name⟩ = ⟨type⟩ | ⟨type⟩ [ | ⟨type⟩ … ] { ⟨member⟩ … }
extend ⟨name⟩ { ⟨member⟩ … }
```

The `|` here is the union type operator, a literal token.

**Description**

A named union gives a union a name and a place for members. It must have at least two distinct member
types and takes no qualifiers. The bodyless form may be given members later with `extend`.

Compatibility is hybrid: for assignment and passing, a named union is transparent (a
`stringOrRoutine` is a `string | func<void>`, and satisfies a parameter of that anonymous type); for
member lookup it is nominal (only a value statically typed as the named union sees its members).

Members are emitters (§7.2), inlined by static type, or `static` methods. Printing a named union is
provided by a global `print(⟨name⟩)` overload rather than a member, because `print` dispatches on the
argument's static type (§21.4).

**Example**

```bgl
union stringOrRoutine = string | func<void> ;

void show(stringOrRoutine v) {              // also accepts a string | func<void>
    if(typeof(v) == eType.routine) { func<void> f = (func<void>)v; f(); }
    else print((string)v);
}
```

**Notes**

The library bindings ship `stringOrRoutine`, with its `print` overload and an `isRoutine` member, for
the I6 "string-or-routine" properties such as `description` (§21.5.10, §23.3.7).

## 2.9 Function Types

**Syntax**

```syntax
func< ⟨return type⟩ [ , ⟨parameter type⟩ … ] >
```

The `<` and `>` here are literal tokens enclosing the type arguments.

**Description**

`func<>` is the type of a value that refers to a function. The first type argument is the return
type; the remaining arguments are the parameter types in order. A function with no parameters has type
`func<⟨return type⟩>`; one returning nothing has `func<void, …>`.

`func<>` is valid as a variable, parameter, return and member type, and as the element type of a
generic collection, including nested forms such as `array<func<T>>`. A value of `func<>` type is
called with ordinary call syntax, including through a member (`obj.handler(3)`) and through a
`for…in` loop variable. The bare name `func` is compatible with every `func<…>` type (§2.11).

Function values are written as named functions (§6.1) or as lambda literals; lambda syntax and
variable capture are in §4.14.

**Example**

```bgl
func<void, int> printer;    // takes one int, returns nothing
func<int, int>  doubler;    // takes one int, returns int
func<void>      callback;   // takes nothing, returns nothing

array<func<eVerdict>> rulebook = { ruleA, ruleB };
for(func<eVerdict> r in rulebook) { eVerdict v = r(); … }
```

## 2.10 Class Types as Values

**Description**

Value and reference semantics concern classes with stored members: what a variable of the type holds,
and what assignment copies. A class that does not derive from `object` (or otherwise from the
runtime's root class `_bglObject`, §21.5.8) is a **value class**. A variable whose type is a class
with stored members holds either the members themselves or a reference to an instance owned elsewhere:

- A local of a value class type has **value semantics**: its members are zero-initialized at routine
  entry, and assignment dispatches `operator =` on the class, copying members rather than aliasing. If
  such a class has stored members and declares no `operator =`, assigning into a local of that type is
  a compile-time error.
- A class derived from `_bglObject`, including every class derived from `object`, has **reference
  semantics**: the variable holds the instance, and assignment makes the variable refer to the
  right-hand instance.
- A class with no stored members has nothing to copy, and neither semantics applies. The veneer
  classes `int`, `bool`, `char` and `string` (§8.2.5) derive from `_bglObject` but store nothing: a
  variable of such a type holds the bare word, and assignment copies the word.
- A local declared `ref` opts into reference semantics regardless of its class, and is bound with
  `:=` (§3.7).

Class parameters follow the same model; a `byVal class` opts a whole class into value semantics for
parameter passing (§8.2.7).

**Example**

```bgl
class Vec2 {
    int x = 0; int y = 0;
    void operator = (Vec2 v) { x = v.x; y = v.y; }
}

Vec2 unit;

void doMath() {
    Vec2 v;              // x = 0, y = 0 on entry
    unit.x = 1;
    v = unit;            // operator = copies the members
}
```

## 2.11 Type Compatibility

Compatibility is checked at every assignment, declaration initializer and function-call argument. A
value of type `A` is compatible with a target of type `B` when any of the following holds, tested in
order:

1. **`var`** — either side is `var` (§2.6).
2. **`null`** — the value is `nothing` or `null` (§2.5).
3. **Assignment operator** — `B` declares an `operator =` that accepts `A`. Candidates are tried in
   this order, and the first found is used:
   1. an emitter whose parameter type is exactly `A`;
   2. an emitter whose parameter type is `var`;
   3. an emitter whose parameter type is the same generic type as `A` with different type arguments
      (`operator = (array<T>)` accepts an `array<int>`);
   4. a non-emitter whose parameter type is exactly `A`;
   5. a non-emitter whose parameter type is `var`;
   6. an emitter whose parameter type is a base class of `A`;
   7. only when `A` is not otherwise compatible with `B`: an emitter, then a non-emitter, whose
      parameter type `A` is compatible with under these rules (this is how a literal reaches an
      `operator = (int)`).
4. **Exact match** — `A` and `B` are the same type.
5. **Class hierarchy** — `A` inherits from `B`, directly or through a chain of base classes and
   aliases. The reverse is not compatible: an `object` cannot be assigned to a `Room`.
6. **Conversion operator** — `A` declares an implicit `operator()` returning `B` (§2.12); an emitter
   conversion is preferred to a non-emitter one.
7. **Generics** — `func` is compatible with every `func<…>`; `array` is compatible with every
   `array<T>`.
8. **`bnum` widening** — `A` is a `bnum` and `B` is `int` (§2.7.3).
9. **Unions** — `B` is a union with a member compatible with `A`, or both are unions and every member
   of `A` is compatible with some member of `B` (§2.8).

If none holds, the assignment or call is a compile-time error.

**Example**

```bgl
class Animal : object { }
class Dog : Animal { }
class Celsius { int degrees = 0; int operator () { return degrees * 9 / 5 + 32; } }
bnum itemFlag { portable, fragile }
Dog rex { }
void run(func f) { … }

var any = 5;         int a = any;             // 1: var on either side
object o = nothing;                            // 2: nothing is compatible with every type
int n = 42;                                    // 3: int declares operator = (intLiteral)
itemFlag f = fragile; itemFlag g = f;          // 4: exact match
Animal pet = rex;                              // 5: class hierarchy; `Dog d = pet;` is an error
Celsius t;           int degrees = t;          // 6: conversion operator
func<int, int> fn;   run(fn);                  // 7: func accepts every func<…>
int bits = portable | fragile;                 // 8: a bnum widens to int
string | func<void> u = "hello";               // 9: a member of the union
```

## 2.12 Conversion

**Description**

A type may declare a **conversion operator**, `operator()`, returning another type; the declaration
syntax is in §9.4. A conversion is **implicit** by default: the compiler applies it during
assignment, argument matching and operator resolution. A conversion qualified `explicit` is applied
only at a cast site, `(⟨type⟩)expr`.

A **pass-through conversion**, declared without a body, leaves the value unchanged and merely retypes it.
A conversion written as a regular method rather than an emitter also fires on a bare read of a member
of that type, which is the basis of property accessors (§9.9).

Beyond conversion operators, a cast is required to narrow a union (§2.8), to convert `int` to a
`bnum` or an `enum` to `int` (§2.7.3), and to convert between `int` and `float` (§2.3). The full cast
syntax is in §4.11.

**Priority.** The order in which an assignment operator, a conversion operator and the other
compatibility rules are tried is stated once, in §2.11. Among overloads, an exact match wins over a
conversion match, which wins over a `var` fallback (§6.4).

**Example**

```bgl
class Celsius {
    int degrees = 0;
    int operator () { return degrees * 9 / 5 + 32; }          // implicit: Celsius → int
    explicit string operator () { return "a temperature"; }   // explicit: only under a cast
}

Celsius t;
int n = t;              // implicit conversion
string s = t;           // compile-time error
string u = (string)t;   // explicit cast
```

---

# 3 Declarations, Variables and Scope

## 3.1 Program Structure

A program is one or more source files. The declarations at the outermost level of a file (types,
classes, enums, variables, functions, objects, verbs and grammar) constitute the **global scope** and
are visible throughout the entire compilation. Declarations may appear in any order, and a name may
be used before it is declared; see §18.3 for the pre-scan.

Every program has a `Main` function as its entry point (§6.7). General-purpose libraries such as
the Inform 6 Standard Library and PunyInform define `Main` themselves and expect a library-specific
entry point, such as `Initialise`, instead (§23.3.1).

A global name must be unique across every kind of global declaration: declaring a variable, function,
class, object or enum with the name of an existing global of any kind is a compile-time error (§19.2).

## 3.2 Declaration Qualifiers

**Syntax**

```syntax
[ ⟨qualifier⟩ … ] ⟨declaration⟩
```

**Description**

A declaration may be preceded by one or more qualifiers, in any order: `emitter replace void foo()`
and `replace emitter void foo()` are equivalent. Each qualifier is specified in the chapter that owns
the construct it modifies.

| Qualifier | Meaning | See |
|---|---|---|
| `const` | Read-only variable or member. | §3.4 |
| `static` | Member belongs to the type rather than to an instance. | §8.3.3 |
| `extern` | Declared in Inform 6; registered for type-checking only, produces no output. | §15.4 |
| `emitter` | The body is an I6 template expanded at each use. | §7.2 |
| `extend` | Adds members to an existing class, object, enum or array. | §8.7.1, §11.10, §12.11 |
| `alias` | Another name for an existing type or value. | §8.2.4, §10.2 |
| `replace` | Replaces an already-declared function or member. | §6.5, §8.7.2 |
| `default` | A base-class member that a derived declaration may override without warning. | §8.7.3 |
| `explicit` | A conversion operator that fires only under a cast. | §9.4 |
| `superposed` | A routine, global, object or class that is emitted only if it is used. | §3.12 |
| `typesealed` | A member whose type a derived class may not change. | §8.2.8 |
| `byVal` | A class whose parameters are passed by value. | §8.2.7 |
| `inline` | A member variable that is a positional slot for inline object construction. | §8.3.5, §11.3.1 |
| `ref` | A local or member that references an instance owned elsewhere. | §3.7 |
| `additive` | A property whose values accumulate along the class chain. | §11.7.2 |

The following combinations are compile-time errors: `explicit` on anything but `operator()`; `const`
with `static`; `static` with `emitter`; `explicit` with `const` or `static`; `alias` with `extern`;
`alias` with `emitter`; `default` in an object or verb body.

`global` is not a qualifier. A variable is global by being declared at file scope (§3.3); the compiler
does not recognize `global` before a declaration.

## 3.3 Global Variables

**Syntax**

```syntax
⟨type⟩ ⟨name⟩ [ = ⟨initializer⟩ ] ;
auto ⟨name⟩ = ⟨initializer⟩ ;
```

**Description**

A variable declared at file scope is a global. An initializer that is a constant expression — a
literal, constant arithmetic, an object or routine name, or a `#define` value — is part of the
declaration. **Any other initializer is applied at startup**, in `bglInit()`, in declaration order:
it may call a routine or read a global declared before it, and the variable holds `0` until
`bglInit()` runs (§21.2). A class-typed global whose type declares a parameterless `init` is applied
at startup for the same reason. `auto` infers the type from the initializer (§3.6). A global name must be unique (§3.1), and a local may not
share a name with a global (§3.10).

**Example**

```bgl
bool isGood = true;
int score = 5 + 3;
string playerName;
```

## 3.4 Constants

**Syntax**

```syntax
const ⟨type⟩ ⟨name⟩ = ⟨value⟩ ;
extern const ⟨type⟩ ⟨name⟩ ;
```

**Description**

`const` marks a variable as read-only. Assigning to it, including `++`, `--` and compound assignment,
is a compile-time error. `extern const` declares a constant that is defined in Inform 6: it is
registered for type-checking, produces no output, and takes no initializer.

**Example**

```bgl
const int MAX_SCORE = 2;
extern const int STUCK_PE;
```

## 3.5 Extern Variables

**Syntax**

```syntax
extern ⟨type⟩ ⟨name⟩ ;
```

**Description**

An `extern` variable is declared in Inform 6 and registered for type-checking only. It produces no
output and cannot be initialized. It may be read and assigned; `extern const` (§3.4) is read-only.
Other `extern` declarations are specified in §15.4.

**Example**

```bgl
extern int score;
extern object location;
```

## 3.6 Local Variables

**Syntax**

```syntax
⟨type⟩ ⟨name⟩ [ = ⟨initializer⟩ ] ;
auto ⟨name⟩ = ⟨initializer⟩ ;
```

**Description**

A local variable is visible from its declaration to the end of the enclosing block (§5.2); reading it
after that block has closed is a compile-time error. Two blocks that do not enclose one another may
each declare the same name (§3.10). `auto`
infers the type from the initializer and requires one; `auto x;` is a compile-time error. The inferred
type is fixed at the declaration and later assignments are checked against it. `auto` is accepted in
local, global and member declarations.

If the variable's type declares an `init` emitter, it fires immediately after the declaration and
before the initializer is assigned (§8.5). Locals beyond the Z-machine's per-routine limit are
spilled to the frame pool by the compiler (§18.10). Shadowing rules are in §3.10.

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
ref ⟨type⟩ ⟨name⟩ := ⟨expression⟩ ;    // local: bound at declaration
ref ⟨type⟩ ⟨name⟩ ;                    // member: starts empty
⟨slot⟩ := ⟨expression⟩ ;               // rebind
```

**Description**

A class-typed slot is either an **owning slot** or a **reference slot**.

**Owning slots.** A local or member of a class type normally owns an instance: a local's members are
zero-initialized at routine entry, and a class-typed member is created with its host (§8.3.4). `=`
copies into an owning slot by dispatching the type's `operator =`. A class that has stored members,
does not inherit from `object`, and declares no `operator =` has no copy semantics, so assigning into
a slot of that type is a compile-time error; the remedies are to declare `operator =`, mark the slot
`ref`, or inherit from `object`. Classes derived from `object` use reference semantics, and classes
with no stored members have nothing to copy.

**Reference slots.** A slot declared `ref` owns nothing; it names an instance owned elsewhere. It is
empty (`nothing`) until bound, so `if (!slot)` distinguishes an unbound slot from a bound one. `ref` is
valid on local variable declarations and on class and object members; on a parameter or on an `extern`
or `const` declaration it is a compile-time error. A member whose type is its own class must be `ref`.

**Binding and assignment.** `:=` binds a reference: it stores the reference and never dispatches
`operator =`. Both sides must be the same class, or the right side a subclass; binding an unrelated
class or a non-instance value is a compile-time error. `:=` is not overloadable. A plain `=` on a
bound reference assigns *through* it, dispatching `operator =` into the referent exactly as on an
owning slot, and so requires the type to have copy semantics. Reads and member writes through a
reference chain normally (`node.next.id`).

**Declaration pairing.** A `ref` declaration binds with `:=`; `=` on a `ref` declaration is a
compile-time error, and so is `:=` on a slot that is not `ref`.

**Example**

```bgl
class Node { int id; ref Node next; }

Node first;   first.id = 1;
Node second;  second.id = 2;
ref Node r := first;          // bind
r.id = 9;                     // through the reference: first.id is now 9
r := second;                  // rebind: first keeps its value
first.next := second;         // a ref member is bound the same way
```

**Notes**

A `ref` slot may be bound to a pooled-class instance created with `new`
(`holder.slot := new pooled();`); pooled classes are specified in §8.2.6. Parameters of class type are
passed by reference unless the class is declared `byVal` (§8.2.7).

**See also** §4.13, §5.15.

## 3.8 Identifier Resolution

An identifier is resolved by searching three tiers in order; the first match wins and later tiers are
not searched. An identifier that matches no tier is undeclared, a compile-time error.

### 3.8.1 Local Scope

1. Parameters of the enclosing function.
2. Local variables of the current block.
3. Local variables of enclosing blocks of the same function.

### 3.8.2 Class and Object Scope

Inside a method body, members of the enclosing class or object, including members inherited through
the base chain. A bare member name resolves as `self.name`; `self` is the receiver (§6.6).
Members declared later in the same body resolve normally.

### 3.8.3 Global Scope

1. Enum values, which share one flat global namespace.
2. Global variables, constants, `extern` declarations and verb names.
3. Members imported with `#using`, which rank below every global; the directive is specified in §10.4.

**Verb names.** A verb is an object and follows the same rules as any other identifier; a local or
parameter with the same name as a verb takes priority.

**Ambiguity.** Inside an object method body, a bare identifier that resolves at this tier and is also
a property of the enclosing object (own or inherited) is resolved to the global candidate and the
compiler issues a warning; `self.X` selects the property and `::X` (§3.9) the global. Inherited *methods* are
not included in this check.

**Members named after types.** A member or method may share a name with a type, including a built-in
type keyword such as `object`; a name following `.` is unambiguously a member. Type names remain
reserved for top-level identifiers (§1.5).

## 3.9 The Global-Scope Qualifier `::`

**Syntax**

```syntax
::⟨name⟩
::⟨name⟩.⟨member⟩
```

**Description**

A leading `::` resolves `⟨name⟩` at global scope, skipping §3.8.1 and §3.8.2. It is the counterpart of
`self.name`: where `self.name` selects the member, `::name` selects the global. It is valid as an
lvalue and as an rvalue and applies to the head of a dotted path. `::name` suppresses the ambiguity
warning of §3.8.3. If no such global exists it is an undeclared-identifier error; it never falls back
to a member.

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

**Description**

Local variables, parameters and `for`-loop variables are checked against the enclosing scopes.

**Errors.**
- Shadowing a global variable. Globals of the symbolic-constant kinds `attribute`, `property`, `verb`
  and `grammarToken` are exempt: they name compile-time constants, not runtime storage.
- Shadowing a registered type name (a class or an enum).
- Re-declaring a local that an enclosing block still has open, or a parameter of the same function.
  A nested declaration does not shadow the outer name — it shares its storage — so the two must have
  different names. Blocks that do not enclose one another may reuse a name freely; their lifetimes
  do not overlap, and a name is not visible after its block closes.

**Warnings.**
- Shadowing a direct member of the enclosing class or object, or a member inherited from a base
  class; `self.name` reaches the member.
- A lambda-local variable shadowing a capturable outer local or parameter (§4.14).
- A member overriding a base-class member; `replace` or `default` suppresses the warning (§8.7.3).

**Two file-scope declarations of one name.** A function and a variable or object may share a
file-scope name, which happens whenever an author declares one of the unprefixed names the library
publishes (`print`, `log`, and the article rules before they moved to `bgl.printRules` — §21.11).
They are told apart by **use**, not by declaration order:

| Use | Resolves to |
|---|---|
| `name(args)` | the function |
| `name.member`, `name = v` | the variable or object |
| a bare `name`, with no variable of that name declared | the function, as a `func<>` reference |

A function reference supports neither a member access nor assignment, so the two readings never
compete. Shadowing a name you then call unqualified resolves to the variable, as ordinary shadowing
does — so do not shadow something you still mean to call.

**Example**

```bgl
int score = 0;
class Counter { int n = 0; }
void foo() {
    int score = 5;       // error: shadows global
    int Counter = 0;     // error: shadows class
}
```

## 3.11 The `asI6` and `asBgl` Clauses

**Syntax**

```syntax
⟨type⟩ ⟨name⟩ asI6 ⟨i6 name⟩ ;
object ⟨name⟩ asI6 ⟨i6 name⟩ { … }
⟨type⟩ ⟨member⟩ asI6 ⟨i6 name⟩ ;              // class or object member
⟨type⟩ ⟨method⟩ ( … ) asI6 ⟨i6 name⟩ { … }
extern ⟨type⟩ ⟨i6 name⟩ asBgl ⟨name⟩ ;
```

**Description**

A declaration carries two names: the one Beguile source uses and the one that reaches the Inform 6
output. They are the same unless a clause says otherwise, and which clause applies follows from
where the thing is defined.

**The declared name is always the name in the language that defines the thing; the clause names it
in the other one.** A plain declaration is defined in Beguile, so the declared name is the Beguile
one and `asI6` *creates* the name Inform 6 will get. An `extern` declaration adopts a symbol Inform 6
already defines, so the declared name is the I6 one and `asBgl` *names* it for Beguile.

```bgl
attribute heightened asI6 excited;   // a new attribute: Beguile says heightened, I6 says excited
extern attribute light asBgl lit;    // I6 already has light; Beguile says lit
```

Each clause is therefore tied to one side of `extern`. `asI6` on an `extern` is a compile-time error
— there is no name to create, because the symbol already exists — and `asBgl` without `extern` is an
error for the mirror reason. That is what keeps the reading unambiguous: **`asI6` never names an
existing symbol, and `asBgl` never invents one.** The pair reads as one mechanism with two
directions: each clause names the side its keyword ends in.

`asI6` is valid on any typed instance declaration, on a named object definition (including instances
of subclasses such as `room Name asI6 place { }`), and on class and object members, where it follows
the member name. It is ignored on operator methods; on a type declaration (`extern class`,
`alias class`) or on a free function it is a compile-time error. The usual reason to reach for either
clause is that the name required on one side is a keyword or reserved word on the other (§15.9).

Both clauses cross the language boundary. `alias` never does: `alias class Foo for Bar` (§8.2.4),
`alias name for Type;` and `alias name = Target;` (§10.2) each introduce a second *Beguile* name for
something already named in Beguile, and leave the emitted I6 untouched.

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
superposed ⟨declaration⟩
```

**Description**

`superposed` is a declaration qualifier (§3.2). A declaration qualified `superposed` is part of the
program only if something references it; a superposed declaration that nothing names is absent from
the story file and costs nothing. It may qualify a global function, a file-scope global variable or
array, a whole object declaration, or a whole class declaration.

- **Reference.** A declaration is **materialized**, made part of the program, the first time its name
  is used. For a class, a use is a static instance, a subclass, `new` on a pooled class, an `is`
  test, or any typed use. Reference matching is case-insensitive, like all Beguile identity.
- **Transitive.** A materialized declaration's own references materialize in turn; a superposed
  declaration may freely reference other superposed declarations.
- **Whole declarations, plus `static` methods.** `superposed` applies to a function, global, object or
  class. Inside a class body it applies only to a `static` method (`static superposed ⟨type⟩ ⟨name⟩(…)`),
  which then materializes only when referenced (§8.4); on a non-`static` method it has no effect and
  the compiler issues a warning.
- **Rejected on** `extern`, `emitter` and `alias` classes, which have no definition to withhold, and
  on `extend class`, where it belongs to the original declaration.
- `superposed` may appear in any position among the qualifiers.

The `omitUnusedRoutines` setting (§17.3) is complementary: `superposed` withholds a declaration that
is never referenced, while `omitUnusedRoutines` asks the I6 compiler to drop routines that were
emitted and remain unreferenced.

**Example**

```bgl
superposed array<char> vowels = "aeiou";

superposed bool charIsVowel(char c){
    for(char v in vowels) if(v == c) rtrue;   // materializes vowels as well
    rfalse;
}

superposed object worldHelpers {
    array<object> getAll() { … }
}
extend bgl { alias world = worldHelpers; }   // bgl.world.getAll() materializes worldHelpers
```

**Notes**

An `alias` value member (§10.2) references its target only where the alias is used, so an alias to
a superposed object keeps the object absent until the alias is used; an `auto` member references its
target unconditionally.

**See also** §10.2, §17.3, §18.7 (placement of a materialized class), §18.9 (emission).

---

# 4 Expressions and Operators

## 4.1 Evaluation

Every expression has a **resolved type**, which drives operator resolution, type-checking and emitter
dispatch. Operators of equal precedence group left to right, and each binary operator resolves in
turn using the resolved type of the expression to its left. `&&` and `||` evaluate the right operand
only when the left operand does not decide the result; `??` evaluates its right operand only when the
left is null (§4.10); a ternary evaluates only the selected branch (§4.9).

## 4.2 Operands

An expression is one or more operands joined by operators. An operand is one of:

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

A literal may have emitter methods called on it directly: `"hello".print()`, `42.someMethod()`.

## 4.3 Operator Precedence

Higher levels bind more tightly. `a + b * c` is `a + (b * c)`; `a > 0 && b < 10` is
`(a > 0) && (b < 10)`; `a <=> b < 0` is `(a <=> b) < 0`.

| Prec | Operators | Kind | Assoc | Meaning |
|:---:|---|---|---|---|
| 14 | `.` `?.` `[]` `()` `v?` `++` `--` | postfix | left | Member access, optional access, subscript, call, postfix query, postfix increment/decrement |
| 13 | `!` `-` `&` `(Type)` `++` `--` | prefix | right | Logical not, negation, address-of, cast, prefix increment/decrement |
| 11 | `*` `/` `%` | infix | left | Multiplicative |
| 10 | `+` `-` | infix | left | Additive |
| 9 | `<<` `>>` `<=>` | infix | left | Shift, three-way comparison |
| 8 | `<` `<=` `>` `>=` | infix | left | Relational |
| 7 | `==` `!=` `?=` `=~` | infix | left | Equality |
| 6 | `&` | infix | left | Bitwise and |
| 5 | `^` | infix | left | Bitwise exclusive or |
| 4 | `\|` | infix | left | Bitwise or |
| 3 | `&&` | infix | left | Logical and |
| 2 | `\|\|` | infix | left | Logical or |
| 1 | `? :` | ternary | — | Conditional (one per statement, §4.9) |
| 1 | `??` | infix | — | Null coalescing (§4.10) |
| 0 | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` `:=` | infix | right | Assignment (§5.5), compound assignment (§5.6), reference binding (§3.7) |

There is no unary bitwise-not operator.

## 4.4 Binary Operator Resolution

A binary operator is resolved against the resolved type of its left operand:

1. Look for `operator op` (emitter or method) on the left type whose parameter accepts the right type.
2. Otherwise, if the left type has a conversion operator (`operator()`) to the right type, fall back
   to the built-in operator.
3. Otherwise, if the right type has a conversion operator to a type for which the left type does
   declare `operator op`, apply that conversion and use the operator.
4. Otherwise, if the left type is known, it is a compile-time error. A `var` operand falls back to the
   built-in operator.

Assignment and arithmetic operators keep the left operand's type. The overloadable operators are
listed in §9.1; the declaration forms are specified in §9.

**Example**

```bgl
class Money {
    int cents = 0;
    emitter bool operator == (Money v){ $self.cents == $v.cents }
    int operator () { return cents; }          // implicit conversion to int
}
Money a; Money b; int five = 5;
bool same = a == b;       // 1: Money declares operator == (Money)
int  sum  = a + five;     // 2: no operator + on Money; a converts to int, built-in + applies
```

## 4.5 Arithmetic Operators

The built-in operators are declared by the runtime core, need no `#include`, and are ordinary
overloadable operators (§9.1) that a type may declare for itself. The table lists every built-in
binary and prefix operator by the types that define it; the compound-assignment forms follow the
binary operator they are built from (§5.6), and the postfix query, optional chaining and null
coalescing are in §4.10.

| Operators | Built in for | Result | Example |
|---|---|---|---|
| `+` `-` `*` `/` `%` | `int`, `uint` (§21.6.1), `float` (§2.3); `+` and `-` also for `char` with a `char` or `int` right operand (§21.7) | The left operand's type | `7 / 2` → `3`; `7 % 2` → `1`; `'a' + 1` → `'b'` |
| `==` `!=` `<` `>` `<=` `>=` | `int`, `uint`, `float`, `char`; `==` and `!=` also for `bool`, `string` (identity of the text) and `object` | `eBool` | `score >= 50`; `noun == lamp` |
| `?=` | No built-in type; it exists so that a type may give it a meaning (§9.5) | `eBool` | — |
| `=~` | `char`: case-insensitive equality (§21.7); `string` with `<string>` (§22.3) | `eBool` | `'A' =~ 'a'` → `true` |
| `<=>` | No built-in type; `<string>` declares it for `string` (§22.3). A type provides it as `operator <=>`, `static` with both operands as parameters or as an instance operator (§9.6); generic containers use it to obtain an ordering | `int`: negative, `0` or positive | `(a <=> b) < 0` |
| `&&` `\|\|` `!` | `bool` and `eBool` (§2.2); `!` on a type that declares `operator !()` uses that emitter (§9.5) | `eBool` | `a > 0 && b < 10` |
| `&` `\|` `^` `<<` `>>` | `int`, `uint`; `&`, `\|` and `^` also combine `bnum` values that share a base (§2.7.2) | The left operand's type | `flags & lit`; `1 << 4` → `16` |

A leading `-` on an integer literal forms a negative literal, whose pseudo-type is
`negativeIntLiteral` (§2.4.1). `<=>` binds more tightly than the relational operators (§4.3).

## 4.6 Comparison Operators

`== != < > <= >= ?= =~ <=>` are tabulated in §4.5.

## 4.7 Logical Operators

`&&`, `||` and prefix `!` are tabulated in §4.5.

## 4.8 Bitwise and Shift Operators

`& | ^ << >>` are tabulated in §4.5; the compound forms `&= |= ^= <<= >>=` are in §5.6.

## 4.9 Ternary Operator

**Syntax**

```syntax
⟨condition⟩ ? ⟨trueExpr⟩ : ⟨falseExpr⟩
```

**Description**

Selects one of two values. A ternary may appear as a call argument, on the right-hand side of an
assignment, in a parenthesized sub-expression, and in the condition and increment parts of a `for`
loop. At most one ternary may appear per statement (a call argument that is a ternary counts), and a
ternary may not be nested in another ternary's condition or branches.

**Type of the result.** When the branches have the same type, that is the type. When one branch's
type is assignable to the other's — a derived class and its base, for instance — the result takes the
more general of the two, whichever side it is written on. A literal counts as the type it denotes,
so `-1` and `0` are both `int`. A branch typed `var` imposes nothing.

When the branches have unrelated types, the result is the **union** of the two (§2.8): a ternary over
a string and a routine is what `string | func<void>` describes, and a union value occupies one word
either way. The destination then decides whether the expression is legal — a union-typed target
accepts it and discriminates with `typeof` (§2.8.1), while a target of one branch's type rejects it
as the mistyping it is.

**Example**

```bgl
print(x > 0 ? "positive" : "non-positive");
int result = (cond ? a : b) + extra;

string | func<void> action = useText ? "nothing happens" : doSomething;  // union of both branches
int wrong = cond ? 1 : "text";   // error: 'int|string' is not assignable to 'int'
```

## 4.10 Optional Chaining, Null Coalescing and Postfix Query

**Syntax**

```syntax
⟨expr⟩?.⟨member⟩
⟨expr⟩?.⟨method⟩(⟨args⟩)
⟨expr⟩ ?? ⟨fallback⟩
⟨expr⟩?
```

**Description**

All three operators are type-driven: the operand's type must declare an `operator ?()` emitter, whose
result is the null test (the declaration is specified in §9.5; the BLR defines it for `object` as
"not `nothing`" and for `string` as "non-zero handle"). Using them on a type without `operator ?()`
is a compile-time error.

**`?.`** accesses a member or calls a method only if the left operand is non-null. In an expression,
each `?.` step tests the value so far; if it is null the whole chain yields `nothing`, otherwise the
step proceeds. As a statement (`x?.remove();`) the operation runs only if the target is non-null. A
plain `.` after a `?.` adds no guard: if the preceding `?.` yielded null, the member access runs on
`null`; the result is undefined.

**`??`** yields the left operand if it is non-null, otherwise the right operand, which is evaluated only
in that case.

**Postfix `?`** yields the null test as an `eBool`; `noun?` is `noun != null` for objects, and `!noun?`
negates it. Where a `?` could begin a ternary, as the right operand of a binary operator, it is the
ternary; the postfix query applies only when the `?` ends the expression.

**Example**

```bgl
object dest = actor?.destination ?? location;
string desc = noun?.parent.description;   // parent guarded, .description not
if (noun?) print("something is here");
```

## 4.11 Casts

**Syntax**

```syntax
(⟨type⟩)⟨expr⟩
(⟨instanceName⟩)⟨expr⟩
```

**Description**

A cast sets the resolved type of the immediately following identifier or call; it does not propagate
through a chain. It has three uses.

**Ancestor-qualified dispatch.** Method dispatch is dynamic: `myDog.speak()` runs the most-derived
override. Casting the receiver to a strict ancestor of its static type selects that ancestor's version:
`(Animal)myDog.speak()`. On `self` inside an override this calls the overridden method without
recursion. An identity cast, a downcast or a cast to an unrelated type keeps dynamic dispatch. The
receiver must be a class or object with real methods; an ancestor cast on an `emitter` method is a
compile-time error. The cast qualifies method dispatch only; it does not apply to member access or
`operator =` (`(Base)obj.field = x`). A base-typed *variable* stays dynamic: `Animal a = myDog;
a.speak();` runs `Dog`'s override.

**Explicit conversion.** A conversion operator declared `explicit` fires only under a cast:
`string s = (string)myValue;` (§9.4). A cast also forces resolution through a specific type when the
inferred type would resolve differently.

**Class vs. instance.** The target may be a class or a named object. Casting to a class exposes the
class's members; casting to an instance also exposes members declared on that object alone. This is
the way to reach members through a dynamically-typed value such as `.parent`, which is statically
`object`. The cast is an unchecked downcast: the member name is checked against the target at compile
time, but the runtime object is assumed to be of that type. Reaching a member that `object` does not
have through an `object`-typed value without a cast is a compile-time error. `(var)` gives an
untyped read with no member check.

**Example**

```bgl
class Room : object { int lit; }
Room library { int shelves; }
object obj;                                // statically object
int a = ((library)obj.parent).shelves;     // instance member
int b = ((Room)obj.parent).lit;            // class member
```

**See also** §2.12 (conversion operators), §8.6 (inheritance and overriding).

## 4.12 Address-of `&`

**Syntax**

```syntax
&⟨expr⟩
```

**Description**

Prefix `&⟨expr⟩` yields the raw machine address of its operand as an `int`; it is exactly
`(int)⟨expr⟩`. On an array, buffer or object it gives the base address; on a scalar it yields the
value itself. It does not compute the storage address of a variable or property; Beguile has no
pointer types. A `&` with a left operand is the bitwise and (§4.8).

**Example**

```bgl
extern void FillBuffer(int address, int length);   // an I6 routine
array<char> buf[32];
FillBuffer(&buf, 32);                              // the base address of buf
int n = 7;
int m = &n;                                        // → 7: a scalar yields its value
```

**See also** §15.4.1 (`extern` functions).

## 4.13 `new`

**Syntax**

```syntax
new ⟨type⟩(⟨args⟩)
```

**Description**

Allocates an instance of a pooled class and yields a reference to it, or `nothing` if the pool is
exhausted; the result must be checked before use. The arguments are passed to the class's `create()`
method; if the class declares no `create`, `new ⟨type⟩()` is the only valid form. `new` on a class
that is not pooled is a compile-time error. Pooled classes, pool size and the `create`/`destroy`
lifecycle are specified in §8.2.6; the `delete` statement in §5.15.

**Example**

```bgl
class Marble[10] : object { }
Marble m = new Marble();
if (m == nothing) print("The pool is exhausted.");
```

**Notes**

`create()` argument limit: see §8.2.6.

## 4.14 Lambdas

**Syntax**

```syntax
(⟨type⟩ ⟨param⟩, …) => { ⟨body⟩ }
(⟨type⟩ ⟨param⟩, …) => ⟨expr⟩
() => { ⟨body⟩ }
```

**Description**

A lambda is an anonymous function literal. It may be assigned to a `func<>` variable or member,
passed as an argument, or stored in a collection (`func<>` is specified in §2.9). The return type is
inferred: from the expression of a `return expr;` in the body, or `void` if the body has no `return`.
A lambda in argument position is passed by reference, like a named `func<>` value.

The expression-bodied form `(⟨params⟩) => ⟨expr⟩` is equivalent to
`(⟨params⟩) => { return ⟨expr⟩; }`; the return type is inferred from the expression. In both forms
every parameter is typed; `x => …` is not a lambda.

**Capture.** A lambda body may use locals and parameters of the enclosing function, wherever in that
function the lambda appears, including inside loop and `if` bodies. Each captured variable's value is
copied when the lambda is created. For a lambda passed directly as an argument, changes the body makes
to a captured variable are visible in the enclosing scope after the call returns. Captures are
intended for immediate callbacks: for a lambda stored and invoked later, the behavior is undefined. A
lambda that captures nothing costs nothing extra.

**Constraints.** A lambda literal may not be invoked immediately (`((int n) => { … })(42)`); assign it
or pass it first.

**Example**

```bgl
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
⟨type⟩::operator ⟨op⟩
⟨type⟩::operator ⟨op⟩(⟨type⟩)
```

**Description**

Names an operator that `⟨type⟩` declares, yielding its address for use wherever a `func<>` is
expected. Only a `static` operator is referenceable; referencing an instance operator is a
compile-time error. When a type declares several static overloads of the operator, the parenthesized
operand type selects one. The rules for declaring static operators are in §9.6.

**Example**

```bgl
#include <string>
class Money {
    int cents = 0;
    static bool operator == (Money a, int b) { return a.cents == b; }
}
array<string> names = {"cherry", "apple"};

names.sort(string::operator <=>);
func<bool, Money, int> byCents = Money::operator ==(int);
```

**See also** §7.3.1 (`$opref`, the same lookup inside an emitter body).

---

# 5 Statements and Control Flow

## 5.1 Statements

The body of a function, method or emitter is a sequence of statements executed in order. A statement
ends with a semicolon unless it ends with a closing brace. A stray `;` is an empty statement and is
discarded, so a trailing semicolon after a directive, a declaration, a class body or a block is
accepted at every statement boundary and at file scope.

## 5.2 Block Statement

**Syntax**

```syntax
{ ⟨statement⟩ … }
```

**Description**

A block groups statements and introduces a scope: a variable declared in the block is visible from
its declaration to the block's closing brace (§3.6). A block may stand wherever a statement may.

## 5.3 Expression Statement

**Syntax**

```syntax
⟨expression⟩ ;
```

**Description**

An expression followed by a semicolon is a statement. The expressions that may stand alone are
function and method calls, including chained calls (`str.trim().print();`), optional-chained calls
(`x?.remove();`), increment and decrement (§5.7), and assignment (§5.5, §5.6). Arguments and arity
are checked at compile time, and a method call is resolved against the declared type of its receiver;
each call in a chain is resolved against the return type of the previous call.

## 5.4 Declaration Statement

**Syntax**

```syntax
⟨type⟩ ⟨name⟩ [ = ⟨expression⟩ ] ;
```

**Description**

A local variable declaration is a statement. The forms, including `auto`, are specified in §3.6;
`ref` and `:=` in §3.7.

## 5.5 Assignment

**Syntax**

```syntax
⟨lvalue⟩ = ⟨expression⟩ ;
```

**Description**

The left-hand side is a declared variable or a dotted member path. The assignment is permitted when
the right-hand type is compatible with the left-hand type; the compatibility rules, tested in order,
are in §2.11. Reference binding (`:=`) is specified in §3.7.

**Example**

```bgl
score = score + 10;
lamp.parent = library;
```

## 5.6 Compound Assignment

**Syntax**

```syntax
⟨lvalue⟩ ⟨op⟩= ⟨expression⟩ ;
```

`⟨op⟩` is one of `+ - * / % & | ^ << >>`.

**Description**

`+= -= *= /= %= &= |= ^= <<= >>=` modify the variable in place. The left type must declare the
corresponding compound operator; if it does not and the type is known (not `var`), it is a
compile-time error. The BLR defines all of them for `int` (§2.2) and `uint` (§21.6.1).

`n += 2` is equivalent to `n = n + 2` when the type declares no `operator +=`; the
fallback is specified in §9.7.

**Example**

```bgl
int n = 5;
n += 2;      // → 7
n <<= 1;     // → 14
```

## 5.7 Increment and Decrement

**Syntax**

```syntax
⟨variable⟩++ ;   ⟨variable⟩-- ;   ++⟨variable⟩ ;   --⟨variable⟩ ;
```

**Description**

The variable's type must declare `operator ++` / `operator --`. A prefix form uses
`operator prefix++` / `operator prefix--` if the type declares it and otherwise falls back to the
postfix operator, so a type that declares only `operator ++` supports both `n++` and `++n`. If no
operator is found and the type is known, it is a compile-time error.

**Example**

```bgl
int n = 5;
n++;         // → 6
--n;         // → 5
```

## 5.8 `if` / `else`

**Syntax**

```syntax
if (⟨condition⟩) ⟨statement⟩
if (⟨condition⟩) { … } else { … }
if (⟨condition⟩) { … } else if (⟨condition⟩) { … } else { … }
```

**Description**

The body may be a single statement or a block. `else` is optional.

`else if (⟨condition⟩) { … }` is equivalent to `else { if (⟨condition⟩) { … } }`:
an `else if` chain is an `if` nested in the `else`.

**Example**

```bgl
if (score < 0)       print("Invalid score.");
else if (score < 50) print("Keep going.");
else                 print("Well done!");
```

## 5.9 `for`

**Syntax**

```syntax
for (⟨initializer⟩; ⟨condition⟩; ⟨increment⟩) ⟨statement⟩
```

**Description**

All three parts are required. The initializer may declare a new variable or assign to an existing
one; it may not redeclare a variable that already exists, including one declared in the initializer
of an earlier `for`. A ternary is permitted in the condition and increment parts (§4.9).

**Example**

```bgl
for (int i = 0; i < 3; i++) print(i);    // → 012
```

### 5.9.1 `for-in`

**Syntax**

```syntax
for (⟨type⟩ ⟨v⟩ in ⟨source⟩) ⟨statement⟩
for (⟨v⟩ in ⟨source⟩) ⟨statement⟩
for (⟨type⟩ ⟨v⟩ in ⟨first⟩ to ⟨last⟩) ⟨statement⟩
```

In the second form `⟨v⟩` is a variable declared earlier.

**Description**

Iterates over every element of `⟨source⟩`, which is one of:

| Source | Element type |
|---|---|
| A declared array variable | The array's element type |
| An inline list `{a, b, …}` | The type of the first element; each element is checked against `v` |
| A call expression returning an array | `var`: any declared type for `v` is accepted |
| A range `first to last` | Integer; inclusive at both ends; the bounds are any expressions |

The loop variable may be declared in the loop head or beforehand. Its type must be compatible with
the element type or it is a compile-time error; `var` matches any element type, and `auto` infers the
element type from the source, including through `operator auto()` (§7.8). Any other source expression
is a compile-time error. Loops nest; each keeps its own iteration state.

**Example**

```bgl
array<int> primes = {2, 3, 5, 7};
int start = 10;
int count = 3;

for (auto p in primes) print(p);                        // → 2357
for (object o in bgl.world.getAll()) print(o);
for (int i in start to start + count - 1) print(i);     // → 101112
```

**See also** §21.9 (`bgl.world`).

## 5.10 `while`

**Syntax**

```syntax
while (⟨condition⟩) ⟨statement⟩
```

**Description**

The condition is evaluated before each iteration; if it is false on entry the body does not run.

**Example**

```bgl
int n = 3;
while (n > 0) { print(n); n--; }    // → 321
```

## 5.11 `do` / `while` and `do` / `until`

**Syntax**

```syntax
do { … } while (⟨condition⟩) ;
do { … } until (⟨condition⟩) ;
```

**Description**

The body runs at least once and the condition is evaluated after each iteration. `do`/`while`
repeats while the condition is true; `do`/`until` repeats until it becomes true.

**Example**

```bgl
int n = 0;
do { n++; } while (n < 3);    // n → 3
do { n--; } until (n == 0);   // n → 0
```

## 5.12 `switch`

**Syntax**

```syntax
switch (⟨expression⟩) {
    case ⟨value⟩ [, ⟨value⟩ …] :   ⟨statements⟩
    case ⟨low⟩ to ⟨high⟩ :           ⟨statements⟩
    case ⟨op⟩ ⟨value⟩ :              ⟨statements⟩
    default :                        ⟨statements⟩
}
```

`⟨op⟩` is one of `>`, `>=`, `<`, `<=`.

**Description**

A case lists one or more values separated by commas. Cases do not fall through. A `break` in a case
body leaves the `switch` — at the top of the body or nested in a block within it, and whichever
lowering the switch takes. Because cases do not fall through, a `break` as the last statement of a
case body does nothing; a `break` before the end of the body ends the case there, leaving the rest of
it unreachable. A `break` inside a loop within a case body belongs to that loop. Case values
are type-checked against the switch expression (§2.11): integer literals match an `int`, and an enum
value must be of the switch expression's enum type. When the switch expression is a `verb`, case
values are verb names (§13.2).

**Ranges.** `low to high` matches the inclusive range and may be mixed with single values in one case:
`case 1, 3, 5 to 10:`.

**Comparison guards.** `case >= 50:` tests the switch value against a comparison. When any case in a
switch is a guard, the switch expression is evaluated once and the cases are tested in order; values
and ranges in the same switch become equality and range tests.

**Type-driven comparison.** When the switch expression's type declares `operator switch()`, that
operator performs each case comparison; `string` declares one so that `case "north":` compares
content. Overloads for different case-value types may coexist. The declaration form is in §9.5.

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

**Description**

`break` exits the innermost enclosing `for`, `while` or `do` loop, or the innermost enclosing
`switch` when that is nearer (§5.12). A `break` in neither is reported by the Inform 6 stage.
`continue` skips the rest of the current iteration of the innermost `for`, `while` or `do` and
re-evaluates the loop condition; a `switch` inside the loop does not intercept it. `continue` outside
a loop is a compile-time error.

**Example**

```bgl
for (int i = 0; i < 10; i++) {
    if (i == 3) break;            // leaves the loop
    if (i % 2 == 0) continue;     // skips the even values
    print(i);
}                                 // → 1
```

## 5.14 `return`, `rtrue` and `rfalse`

**Syntax**

```syntax
return ;
return ⟨expression⟩ ;
rtrue ;   rfalse ;
rtrue(⟨expression⟩) ;   rfalse(⟨expression⟩) ;
```

**Description**

`return` exits the function, with a value if given. The value may be any expression and is
type-checked against the declared return type. In a `void` function `return expr;` is a compile-time
error unless `expr` is itself of type `void`. In loose mode (`#bgl` islands and precompiler mode,
§15.3.3) an expression of type `var` is also accepted there.

In a `void` function, `return expr;` where `expr` is of type `void` is equivalent to
`expr; return;`.

In a function whose return type is `bool`, `rtrue;` is equivalent to `return true;`
and `rfalse;` to `return false;`.

`rtrue(expr)` and `rfalse(expr)` print `expr`, with full `print()` overload dispatch including `$"…"`
strings (§21.4), and then return.

**Return-path analysis.** A non-`void` function must return on every path; a path that can reach the
end of the body without returning is a compile-time error. A path is satisfied by an unconditional
`return` at the top level of the body; by an `if`/`else` whose two branches are both satisfied; by a
`switch` with a `default` whose every case body is satisfied (a `switch` without `default` is not); or
by an `#i6` island that returns (§15.2). A loop body alone does not satisfy it.

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
delete ⟨identifier⟩ ;
```

**Description**

Returns a pooled-class instance to its pool, calling the class's `destroy()` method first if one is
declared. `delete` on a variable whose type is not a pooled class is a compile-time error. Pooled
classes are specified in §8.2.6; allocation with `new` in §4.13.

**Example**

```bgl
Marble m = new Marble();
if (m != nothing) delete m;    // destroy() runs; the slot returns to the pool
```

## 5.16 `try` / `catch` / `throw`

**Syntax**

```syntax
try { … } catch (⟨type⟩ ⟨name⟩) { … }
throw ⟨expression⟩ ;
```

**Description**

If `throw` executes anywhere during the `try` block, including inside called functions at any depth,
execution unwinds to the nearest enclosing `catch` and the thrown value is assigned to the catch
variable. The thrown value is one word: an integer, an object reference or any other word-sized value.
`try` blocks nest; a `throw` inside a `catch` re-throws to the next enclosing `catch`. If `throw`
executes with no active `try` on the call stack, the program prints an error and halts.

**Example**

```bgl
void deep() { throw 7; }
try { deep(); } catch (int e) { print(e); }    // → 7
```

**Notes**

> **[Z-machine]** Requires version 5 or later.

---

# 6 Functions

## 6.1 Function Declarations

**Syntax**

```syntax
⟨returnType⟩ ⟨name⟩(⟨type⟩ ⟨param⟩, …) { ⟨body⟩ }
```

**Description**

A function has a return type, a name, a parenthesized parameter list and a block body (§5.2). A
function declared at global scope is a **global function**; its name must be unique among non-emitter
functions, and among all globals (§3.8.3). Functions may also be declared as members of a class (§8.4)
or object (§11.9). A function declared `extern` has no body and is defined in Inform 6 (§15.4.1); a
function declared `emitter` has an I6 template body (§7.2).

**Example**

```bgl
object foyer { }

void DeathMessage() { print("You have lost"); }
bool Initialise() { location = foyer; rtrue; }
```

## 6.2 Return Types

**Description**

| Return type | Meaning |
|---|---|
| `void` | No value. `return;` is permitted; `return expr;` is an error unless `expr` is `void` (§5.14). |
| Any other type | Every path must end in `return expr;` with `expr` compatible with the type (§5.14). |
| `array<T>` | Returns a typed array. A returned *local* array is ephemeral (§12.6). |

`rtrue`, `rfalse` and the return-path rules are specified in §5.14.

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
(⟨type⟩ ⟨name⟩ [ = ⟨default⟩ ], …)
()
```

**Description**

Parameters are local to the body. A parameter with a default value is optional; required parameters
precede optional ones. A call must supply between the required count and the total count of
arguments, or it is a compile-time error.

**Named arguments.** An argument may be passed as `name: value`. Named arguments may appear in any
order and may be mixed with positional arguments: positional arguments fill the first unfilled
parameters in order, and named arguments fill their target parameter. It is an error to name a
parameter the function does not have, to supply a parameter both positionally and by name, or to omit
a required parameter.

Parameter names may be omitted in non-emitter declarations inside an `extern class` (§15.4.3).

**Example**

```bgl
void spawn(string name, int x, int y, bool hostile = false) { … }
spawn(x: 10, y: 20, name: "goblin", hostile: true);   // spawn("goblin", 10, 20, true)
```

## 6.4 Overload Resolution

**Description**

Functions and methods with the same name and different parameter-type signatures may coexist:
functions at global scope, emitter or not, and emitter and non-emitter methods on classes and
objects, including `operator()`, `operator[]` and `operator[]=`.

Two declarations of the same name must differ in their **parameters** — the return type is not part
of the signature, since a call is resolved before its result is used. Declaring the same name twice
with the same parameter types is a compile-time error.

Because Inform 6 has no overloading, each member of a set emits as its own routine or property under
a mangled name (`add_2_int_int`); a name with a single definition keeps the name the author wrote.
The mangled name is not part of the language: from raw I6, reach an overload through
`$i6Name(add(int,int))` (§7.3.3) rather than by spelling it out.

A call is resolved in two steps:

1. **Arity.** A candidate matches only if the argument count lies between its required and total
   parameter counts.
2. **Types.** Among candidates that match by arity, an exact type match for every argument wins over a
   match through an implicit conversion (`operator()`, §9.4), which wins over a match through `var`.

Type compatibility itself is specified in §2.11.

**A reference is not a call.** A bare function name used as a value (`func<int,int> f = twice;`)
must name exactly one routine. An overloaded name names the set, and nothing in a reference says
which member is meant, so it is a compile-time error; wrap the overload you want in a function of
its own and reference that.

## 6.5 `replace` and `replaced()`

**Syntax**

```syntax
replace ⟨returnType⟩ ⟨name⟩(⟨params⟩) { ⟨body⟩ }
replaced(⟨args⟩)
```

**Description**

`replace` replaces an already-declared global function. If no matching function exists, the compiler
issues a warning and the declaration is treated as a new function.

**Emitter functions.** The body is swapped. Matching is by name, return type and full parameter-type
signature, since emitters overload. `replaced()` is not available in an emitter.

**Non-emitter functions.** A replacement names the overload it replaces by its own parameter types;
with a name that has one definition, the parameters need not match it. Replacements chain. Inside the replacement body, `replaced(args)` calls the
immediately preceding definition; the arguments are type-checked against that predecessor's signature,
which may differ from the replacement's own. Successive `replace` declarations form a chain in which
each `replaced()` calls the version it directly replaced. A predecessor that no replacement calls is
not part of the program.

Replacing an `extern` routine defined by an I6 library works the same way in the source; the
I6-side rules are in §15.6.

**Example**

```bgl
int step(int n) { return n; }
replace int step(int n) { return replaced(n) + 100; }   // calls the original
replace int step(int n) { return replaced(n) + 200; }   // calls the first replacement
// step(5) → 305
```

**See also** §8.7.2 (`replace` for class and object members).

## 6.6 `self`

**Syntax**

```syntax
self
self.⟨member⟩
```

**Description**

Inside a class method or an object method, `self` is the receiver: the instance the method was called
on. A bare member name in a method body resolves as `self.member` (§3.8.2), so `self.` is optional and
has the same effect. `self` is not valid outside a method body.

`self` differs from Inform 6's `self`, which is the object that owns the running routine. In an
emitter body, `$self` is the receiver (§7.3).

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

**Description**

`Main` is the program's entry point. The compiler does not check for it: a program built without an
IF library binding defines `Main` itself, and a missing or duplicated `Main` is reported by the
Inform 6 stage. A library such as the Inform 6 Standard Library or PunyInform defines `Main` itself
and calls a library-specific entry point, such as `Initialise`, that the program supplies instead
(§23.3.1).

**Example**

```bgl
void Main() {
    print("Hello, world!^");
}
```

**See also** §23.3.1 (entry point under a binding).

---

# 7 Emitters

## 7.1 What an Emitter Is

An **emitter** is a function-like declaration whose body is an Inform 6 template rather than Beguile
statements. No routine exists for it: at every use site the body text is substituted in place, with
its **substitution tokens** (`$self`, `$paramName`, …) replaced by the expressions at that site. The
use site itself is ordinary, type-checked Beguile: an emitter has a return type and typed parameters,
and its result participates in expressions like any other value.

Emitters are the mechanism by which library code gives a Beguile type precise control over the
generated I6 without giving up type safety. They may be declared at global scope (§7.5), as members
of any class, as members of an enum or bnum (§7.9), or grouped in an emitter namespace (§7.7).

Unlike functions, emitters may be **overloaded**: several emitters with the same name and different
parameter types may coexist, and the best match is chosen from the argument types at each use site.

Class-member emitters that have their own rules — operator emitters, `init`/`deinit`, conversion and
accessor operators — are specified in §8.5 and §9; this chapter covers the emitter mechanism itself.

## 7.2 Emitter Functions

**Syntax**

```syntax
emitter ⟨returnType⟩ ⟨name⟩( [ ⟨type⟩ ⟨param⟩ [, …] ] ) { ⟨i6-template⟩ }
```

**Description**

`emitter` is a declaration qualifier (§3.2) and may appear in any order with the other qualifiers. The
body between the braces is raw I6 text: it is not parsed as Beguile, and only substitution tokens
(§7.3) and `##` directives (§7.4) are recognized inside it. Everything else, including single-hash
directives such as `#ifdef`, passes through unchanged to the output.

- The return type is the type of the substituted expression at the use site. An `emitter void`
  function may be used only as a statement.
- Parameters are referenced in the body as `$name`. The bare name is *not* substituted, so a body may
  freely mention an I6 identifier that happens to share a parameter's name.
- An emitter always has a body. The one exception is the bodiless conversion operator
  `emitter T operator();`, which is a pass-through (§9.4).
- `static` and `emitter` cannot be combined: an emitter has no routine to make static.
- Recursion is meaningless; an emitter body cannot refer to itself as a routine. A body may
  reach another emitter through `$i6Expr` (§7.3.4), and a chain that comes back round to a body
  already being expanded is a compile-time error rather than an unbounded expansion.

**Example**

```bgl
class Counter {
    int value = 0;
    emitter void increment(){ $self.value++ }
}

Counter c;
c.increment();      // the body is substituted here with $self = c
```

**See also** §8.2 (which class forms require, permit, or imply `emitter` on members).

## 7.3 Substitution Tokens

Every substitution token begins with `$`, which keeps it distinct from any raw I6 identifier. The
tokens below are recognized in every emitter body; a feature may add a feature-local token, which is
documented with that feature (`$selfsub`, §15.8). Appendix G is the one-page index.

| Token | Replaced with |
|---|---|
| `$self` | In an operator or assignment emitter, the receiver expression with its trailing `.member` removed when the receiver is a member access (`obj` for `obj.score + 1`); otherwise, and in a method emitter, the receiver itself (`x` for `x + 1`, `container.children` for `container.children.length()`); a method emitter on `parent` or `attributes` (§11.5) is the exception and receives the owner. Not meaningful in a global emitter. |
| `$val` | The full receiver expression as written: `obj.score` for `obj.score + 1`; otherwise identical to `$self`. |
| `$host` | The object a proxy member (§7.3.2) is accessed on, the owner of the proxy: the receiver with its trailing `.member` removed, in every kind of emitter. For a receiver that is not a member access, `$host` equals `$self`. |
| `$paramName` | The argument expression supplied for the parameter of that name. |
| `$target` | The assignment target as a full lvalue path (`obj.prop`, or `x`); see below. |
| `$prop` | In an `array<T>` emitter, the property name when the array is an object member; `0` for a global array. |
| `$opref(op[, T])` | A callable reference to the receiver type's `operator op` (§7.3.1). |
| `$oprefReq(op[, T])` | As `$opref`, but a missing operator is a compile-time warning (§7.3.1). |
| `$i6Name(path)` | The identifier Inform 6 knows the named declaration by (§7.3.3). |
| `$i6Expr(expr)` | The I6 that a Beguile expression emits, expanded inline (§7.3.4). |

**`$self` and `$host`.** The two differ in which emitters strip the trailing member: `$self` strips
it only in an operator or assignment emitter, whereas `$host` strips it in a method emitter as well,
which is how a method on a proxy member reaches the owner. For `container.children.length()`, `$self`
and `$val` are `container.children` and `$host` is `container`.

**`$target`.** When the emitter's result is assigned (`int r = f();`), `$target` is the left-hand
side; when the emitter is used as a bare statement it is a compiler-supplied temporary. A body that
mentions `$target` performs the store itself: the usual `lhs = body` assignment is suppressed.

### 7.3.1 `$opref` and `$oprefReq`

**Syntax**

```syntax
$opref( ⟨op⟩ [, ⟨operandType⟩] )
$oprefReq( ⟨op⟩ [, ⟨operandType⟩] )
```

**Description**

`$opref` is a lookup, not a value: it substitutes a reference to one of a type's operators so that a
shared runtime routine can be handed a type-aware operation. The type searched is the receiver's
**element type** inside an `array<T>` emitter and the receiver's own type elsewhere. What is
substituted depends on how the operator was declared:

| The operator is | `$opref` substitutes |
|---|---|
| `static` | The name of the free routine; call it as `op(a, b)` |
| an instance member | The property name; call it as `a.(op)(b)` |
| an emitter, or absent | `0` — an emitter has no address and is never referenceable |

The receiving routine must tell the two callable forms apart (`metaclass()` distinguishes a routine
from a property). Publishing an operator is opt-in: a type that declares none yields `0` and the
runtime uses its default.

When a type publishes more than one referenceable overload of the operator, the reference is
ambiguous and is a compile-time error; name the operand type to select one:

```bgl
class Money {
    static bool operator == (Money a, Money b) { return a.cents == b.cents; }
    static bool operator == (Money a, int b)   { return a.cents == b; }
    emitter int refMoney(){ $opref(==, Money) }     // $opref(==) alone is ambiguous
}
```

`$oprefReq` resolves exactly as `$opref` does and substitutes `0` in the same cases, but additionally
reports a **compile-time warning** when the lookup is empty. A library uses it where the default is
not a sound fallback: absence of `==` still finds an element by identity, but absence of `<=>` orders a
class by object address, so `sort()` uses `$oprefReq(<=>)`. No warning is raised for the built-in word
types (`int`, `char`, `object`, …), for which word semantics are the correct answer, nor for a call
that supplies its own comparator.

**Example**

```bgl
emitter int  indexOf(T item) { _bglArray.indexOf($self, $prop, $item, $opref(==)) }
emitter void sort()          { _bglArray.sortDefault($self, $prop, $oprefReq(<=>)) }
```

**See also** §4.15 (`Type::operator op`, the same lookup in ordinary code), §9.6.

### 7.3.2 Choosing `$self`, `$val` and `$target`

**Description**

`$self` and `$val` differ only when the receiver is a property access.

- A **value type** (`int`, `bool`, `string`, …) uses `$val`: the body acts on the value the property
  holds, so `obj.score + 1` must substitute `obj.score`, not `obj`.
- A **proxy member** (a storageless type such as `parentProp` whose operators act on the owning
  object) uses `$self`: `o.parent == bar` must act on `o`, not on `o.parent`.
- An `operator =` that performs a literal store uses `$target`, so `obj.prop = v` stores through the
  dotted lvalue. A proxy `operator =` that redirects to a non-store I6 statement uses `$self`.

**Example**

```bgl
extern class int : _bglObject {
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
$i6Name( ⟨path⟩ )
$i6Name( ⟨path⟩( ⟨type⟩ [, …] ) )
```

**Description**

Substitutes the identifier Inform 6 knows a declaration by. A body is raw I6, so calling a Beguile
routine from one means writing that routine's *emitted* name — which is not the author's to know: an
`asI6` clause renames it (§3.11), a `static` method emits as `_bgl_⟨class⟩_⟨method⟩`, and that mangling
grows a parameter-type discriminator as soon as a second overload of the name is declared. A body
that spells the name itself is therefore wrong the moment an unrelated overload appears. `$i6Name`
asks the compiler for it instead.

`⟨path⟩` is a Beguile path, resolved as an ordinary name would be, including through namespace
aliases (`bgl.asm.readChar`). What is substituted depends on what the path names:

| The path names | `$i6Name` substitutes |
|---|---|
| A global function | Its routine name, or its `asI6` name |
| A `static` method | The free routine's name, `_bgl_⟨class⟩_⟨method⟩`, with the overload discriminator when the class declares more than one |
| An instance method | Its property name — call it as `receiver.(⟨name⟩)(…)` |
| A data member | Its property name, or its `asI6` name |
| An object, class or enum | Its emitted name, or its `asI6` name |
| An enum value | The value's bare word |

When the path names an overloaded method, the parenthesized type list selects one; omitting it where
more than one candidate exists is a compile-time error naming the candidates. The same selection
form appears in `$opref(op, T)` (§7.3.1) and `Type::operator op(T)` (§4.15).

Two cases are compile-time errors rather than a substituted `0`:

- **An emitter.** No routine and no constant is emitted for it, so there is nothing to name. Use
  `$i6Expr` to expand it inline instead. (`$opref` yields `0` for the same situation only because a
  missing operator has a sound fallback in word semantics; a missing *name* has none.)
- **An unresolved path.** An empty substitution would reach Inform 6 as malformed text with nothing
  pointing back at the cause.

A body is expanded only where it is used, so a `$i6Name` in an emitter that is never called is never
resolved and never reports.

**Example**

```bgl
extern attribute lit asI6 light;
object myHook asI6 hook;

emitter bool isLit(object o)      { ($o has $i6Name(lit)) }          // → ($o has light)
emitter int  readKey()            { $i6Name(bgl.asm.readChar)(1) }   // → _bgl_bglOpCodes_readChar(1)
emitter int  scale(int a, int b)  { $i6Name(Money.scale(int, int))($a, $b) }
```

> **When to reach for it.** A global function with no `asI6` clause emits under its own name, so a body
> may simply write that name; `$i6Name` buys nothing there. Use it where the emitted identifier is
> *not* the Beguile one: anything carrying an `asI6` clause, a member or method (which emit as property
> names), a promoted member array, and above all a `static` method — its `_bgl_⟨class⟩_⟨method⟩`
> mangling gains a parameter-type discriminator as soon as a second overload of that name is
> declared, so a spelling that is correct today is silently wrong after an unrelated edit elsewhere.

**See also** §3.11 (`asI6`), §7.3.1 (`$opref`, the same lookup for operators), §4.15, §15.2
(the same token in an `#i6` island).

### 7.3.4 `$i6Expr`

**Syntax**

```syntax
$i6Expr( ⟨expression⟩ )
```

**Description**

Substitutes the I6 that a Beguile expression emits. This is how one emitter reaches another: an
emitter has no routine, so a body cannot call one by name (§7.3.3) — it has to be *expanded*, and
`$i6Expr` is what expands it.

The payload is ordinary Beguile, parsed and type-checked at the use site, so it gets overload
resolution, and a callee's own `##if` gating (§7.4) comes with it rather than being re-written by
every caller.

**Binding.** The body's tokens stand for **typed values** inside the payload: `$v` denotes a value of
`v`'s declared type whose emitted text is the argument at that use site, and `$self`/`$val`/`$host`
likewise at the receiver's type. The payload may therefore mention only this emitter's own
parameters and receiver; any other `$name` is a compile-time error. It may not mention a bare I6
identifier either, since that is not a Beguile symbol — `$i6Expr(__glkHook(33, $a))` resolves only
because `__glkHook` *is* a Beguile function.

**Extent.** Exactly one expression: either one that produces a value, or a single call that returns
nothing, which is what lets an `emitter void` body chain. A `;`-separated list is not accepted —
without locals, control flow or `return` it would look like a function body without being one.
Multi-statement logic still belongs in a routine, which an emitter body can then reach by name
(§7.3.3); a `superposed` routine (§3.12) costs nothing when no body references it.

**Termination.** Because a body is substituted rather than called, a `$i6Expr` chain that reaches a
body already being expanded has no base case. Such a cycle is a compile-time error, as is a chain
deeper than eight expansions.

> **Substituted text is duplicated text.** A body that names `$v` twice already duplicates the
> argument expression, evaluating its side effects twice. Chaining multiplies this: an outer body
> passing `$self` into an inner one that names `$val` twice yields four copies of the receiver.
> Prefer a routine where the argument is expensive or effectful.

**Example**

```bgl
emitter int twice(int v){ ($v * 2) }
emitter int quad (int v){ $i6Expr(twice(twice($v))) }        // → ((v * 2) * 2)

// bgl.asm.add is an emitter — no routine exists for it, so only expansion can reach it
emitter int sum(int a, int b){ $i6Expr(bgl.asm.add($a, $b)) }

// the callee owns the target gating; this caller inherits it
emitter void reseed(){ $i6Expr(bgl.util.random.seed(0)); }   // z-code → random(-0)
                                                             // Glulx  → @setrandom 0
class Counter : object {
    int value;
    emitter int doubled(){ $i6Expr(bgl.asm.add($self.value, $self.value)) }
}
```

**See also** §7.3.3 (`$i6Name`, for a name where no expression is legal), §3.12 (`superposed`),
§7.4, §15.2 (the same token in an `#i6` island).

## 7.4 Conditional Text: `##if`, `##else`, `##endif`

**Syntax**

```syntax
##if ⟨expression⟩
    ⟨i6-text⟩
[ ##else
    ⟨i6-text⟩ ]
##endif
```

**Description**

Inside an emitter body, the double-hash directives select which body text is substituted.
`⟨expression⟩` accepts the same forms as `#if` (§14.2.5): symbols, comparisons, `&&`, `||`, `!` and
parentheses, with the same definedness-versus-value rule. They are evaluated when the emitter is
substituted, and are not valid in ordinary Beguile source, where the conditional is `#if` (§14.2.5).
`##ifdef` and `##ifndef` do not exist at all: use `##if SYMBOL` here, `#if SYMBOL` there.

Single-hash directives are raw I6 and pass through to the output, where they act as I6 compile-time
conditionals. Any other `##name` (for instance an I6 action constant such as `##Take`) also passes
through unchanged, so `$self == ##$v` substitutes the I6 action name of `$v`.

One further double-hash form, `##beguilerSettings.key`, is recognized only in the raw-I6 directive
bodies of §14.4.5; it is not substituted in emitter bodies.

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
emitter ⟨returnType⟩ ⟨name⟩( [ ⟨params⟩ ] ) { ⟨i6-template⟩ }
```

The declaration is written at file scope.

**Description**

A global emitter is declared at file scope and is used like a global function, with its body
substituted at each use site. `$self` has no meaning in a global emitter. Global emitters may be
overloaded; the overload is selected by argument type. `print()` and `log()` are global emitters
(§21.4).

**Example**

```bgl
emitter void print(stringLiteral str){ print (string)str; }
emitter void print(string str)       { print (string)str; }
emitter void print(var val)          { print val; }
```

## 7.6 Emitter Values

**Syntax**

```syntax
emitter ⟨type⟩ ⟨name⟩ { ⟨i6-template⟩ }
```

**Description**

An emitter without a parameter list is an **emitter value**: a typed inline expansion used by bare
name, without `()`. A typed value (`emitter int`, `emitter string`, …) may appear wherever an
expression of that type is accepted; an `emitter void` value may be used only as a statement,
terminated by `;`. Emitter values may be declared at global scope, in class bodies, and in object
bodies; on a member, `$self` substitutes as for an emitter function.

An emitter value and an emitter function are distinct declarations with distinct use:

| Declaration | Use |
|---|---|
| `emitter int foo() { … }` | `foo()` |
| `emitter int foo { … }` | `foo` |

Using a value with parentheses is a compile-time error, whether the name is bare, imported with
`#using`, or qualified. A function is used only with parentheses. A type may therefore expose a value
and a same-named zero-argument function as different members.

**Example**

```bgl
emitter int  wordSize   { WORDSIZE }
emitter void setBold    { style bold }

int ws = wordSize;          // → WORDSIZE
setBold;                    // statement form
```

## 7.7 Emitter Namespaces

**Syntax**

```syntax
emitter ⟨name⟩ {
    ⟨returnType⟩ ⟨member⟩( [ ⟨params⟩ ] ) { ⟨i6-template⟩ }
    …
}
```

**Description**

An emitter namespace groups emitters under one name without declaring a class. It is written as
`emitter` followed directly by the name (no `class` keyword) and a body. Members are called as
`name.member(…)`.

- Every member is an emitter; the `emitter` keyword on a member is optional.
- The namespace name is not a type and cannot declare a variable.
- An emitter namespace cannot be inherited from or extended; either is a compile-time error.

An `emitter class` (§8.2.3) differs in that it *is* a type — its purpose is to declare variables of
it — whereas an emitter namespace exists only to be called by name. `style` is a built-in emitter
namespace; it coexists with the `style` value class of `<glulxWindow>` (§22.7.7), because a call
`style.member(…)` reaches the namespace while the inline-object form `style { … }` builds a value of
the class.

**Example**

```bgl
emitter style {
    void italics() { style underline; }
    void roman()   { style roman; }
}

print($"{style.italics()}Italic text{style.roman()}");
```

**See also** §10 — namespaces in general (objects and emitter classes as containers, `alias`, `#using`);
§10.3 — alias members on emitter classes, for composing namespaces hierarchically.

## 7.8 `operator auto()`

**Syntax**

```syntax
⟨type⟩ operator auto() ;
```

**Description**

When a variable is declared with `auto` (§3.6), its type is normally the resolved type of its
initializer. A class that declares `operator auto()` overrides this: the declared return type is the
type `auto` infers for a value of that class.

- Takes no parameters and has no body; only the return type matters.
- At most one per class; a duplicate is a compile-time error.
- A type without `operator auto()` infers as itself.

The literal pseudo-types declare it so that `auto x = 5;` infers `int` rather than `intLiteral`:

| Literal type | `operator auto()` returns |
|---|---|
| `intLiteral` | `int` |
| `charLiteral` | `char` |
| `dictionaryWordLiteral` | `dictionaryWord` |

**Example**

```bgl
extern class intLiteral : _bglObject {
    emitter int operator();     // implicit conversion to int
    int operator auto();        // auto infers int
}
```

**See also** §2.4 (literal pseudo-types), §5.9.1 (`auto` in `for-in`).

## 7.9 Emitter Methods on Enums and Bnums

**Syntax**

```syntax
enum ⟨name⟩ { ⟨value⟩, …, emitter ⟨type⟩ ⟨method⟩( [ ⟨params⟩ ] ) { ⟨i6-template⟩ } }
extend enum ⟨name⟩ { emitter ⟨type⟩ ⟨method⟩( [ ⟨params⟩ ] ) { ⟨i6-template⟩ } }
```

**Description**

An enum or bnum value is a bare word, so an enum can host emitter methods: members substituted at the
call site with `$self` bound to the value and `$paramName` to each argument. They are declared in the
enum body, or added later with `extend enum`, using the same member form as an emitter class. A
method may be called on a bare value or on an enum-typed variable, and may be used before the
declaration that adds it.

Only emitter methods may be attached. `operator` overloads, `static` members, plain members, and
emitter values are compile-time errors in an enum body. A value with methods costs exactly what a
plain value costs, and the methods are not reachable as a nameable type.

**Example**

```bgl
enum eDirection {
    north, south, east, west,
    emitter int bump()      { ($self + 100) }
    emitter int plus(int n) { ($self + $n)  }
}
extend enum eDirection {
    emitter int tenfold()   { ($self * 10) }
}

int a = north.bump();       // → 101
eDirection d = south;
int b = d.bump();           // → 102
int c = east.plus(10);      // → 13
```

**See also** §2.7 (enumerations).

## 7.10 Emitters and Functions Compared

| | Function | Emitter |
|---|---|---|
| Body | Beguile statements, compiled to a routine | I6 template, substituted at each use site |
| Parameters in the body | Bare name (`myParam`) | `$myParam` |
| Receiver in the body | `self` | `$self` / `$val` / `$host` |
| Overloading | Global functions: not supported (one routine per name); class and object methods: by parameter signature (§8.4) | Supported, by parameter types |
| Recursion | Supported | Not meaningful — a `$i6Expr` chain that revisits a body is an error (§7.3.4) |
| In an `extern class` body | Declaration only, no body | Must have a body (or `;` for a pass-through conversion) |
| At global scope | Yes | Yes |

An emitter body is the primary route to I6 capabilities that have no Beguile syntax; for a raw-I6
block inside a function body see `#i6` (§15.2).

---

# 8 Classes

A class declares a type: the members its instances hold, the methods and emitters that act on them,
and the parents it inherits from. This chapter specifies the declaration and its forms (normal,
`extern`, `emitter`, `alias`, veneer, pooled and `byVal`), the kinds of member, methods, the `init`
and `deinit` lifecycle, inheritance, and extending and replacing members. Two further topics have
chapters of their own: overloaded operators and property accessors, which are class members, are
specified in §9; the use of objects and emitter classes as namespaces is specified in §10. Objects,
the instances declared at file scope, are specified in §11.

## 8.1 Class Declaration

**Syntax**

```syntax
[ ⟨qualifier⟩ … ] class ⟨name⟩ [ <⟨T⟩> ] [ : ⟨parent⟩ [ , ⟨parent⟩ … ] ] {
    ⟨member⟩ …
}
[ ⟨qualifier⟩ … ] class ⟨name⟩[⟨n⟩] [ : ⟨parent⟩ [ , ⟨parent⟩ … ] ] {
    ⟨member⟩ …
}
```

In the first form `<` and `>` are literal: `<⟨T⟩>` declares a type parameter (§8.1.1). In the second
form `[` and `]` are literal: `[⟨n⟩]` declares a pool size (§8.2.6).

**Description**

A class declares a new type. Its members are member variables, methods, emitters, and operators (§9).
The class name must be unique among types; it is the type name used to declare instances. Member
variables follow the same `⟨type⟩ ⟨name⟩ [ = ⟨value⟩ ]` form as global variables (§3.3). Qualifiers
(`extern`, `emitter`, `alias`, `byVal`, `extend`, `replace`, `superposed`, …) are the declaration
qualifiers of §3.2 and may appear in any order.

**Example**

```bgl
class Point {
    int x = 0;
    int y = 0;
    void describe(){ print(x); print(y); }
}
```

### 8.1.1 Type Parameters

**Syntax**

```syntax
class ⟨name⟩<⟨T⟩> [ : ⟨parent⟩ ] { ⟨member⟩ … }
⟨name⟩<⟨type⟩> ⟨variable⟩ ;
```

`<` and `>` are literal.

**Description**

A class may declare one type parameter after its name. The parameter is a name scoped to the class
body and may be used wherever a type is expected in a member declaration: return types, parameter
types, member-variable types. At a use site the binding is supplied, and every `T` in the relevant
member's signature is replaced by it, so `Box<Room> b;` gives `b.payload` the type `Room` and rejects
incompatible writes at compile time. The substitution is purely static.

- Only the first type parameter binds; `<K, V>` parses but only `K` is used.
- The parameter is not a global type; it exists only inside its declaring class.
- `extend class Name<…>` and `alias class Name<…>` are compile-time errors; type parameters belong
  to the original declaration.
- A binding may be supplied in a declaration but not in inheritance position, where the class-name
  form is used (`class byteArray : array<char>` is written as a class name).

**Example**

```bgl
class Box<T> : object {
    T   payload;
    int weight;
}
Box<Room> roomBox;      // T = Room
Box<int>  scoreBox;     // T = int
```

**See also** §12.1 — `array<T>` is the principal client.

## 8.2 Class Forms

| Form | Syntax | Instances | Members permitted |
|---|---|---|---|
| Normal | `class Foo` | Objects with their own storage | Variables (with or without initializers), methods, emitters |
| Extern | `extern class Foo` | Defined outside Beguile (I6) | Variable declarations, emitters, `static` methods |
| Emitter | `emitter class Foo` | None (type label only) | Emitters (`emitter` implied), `static` methods, alias members |
| Alias | `alias class Foo for Parent` | Same as `Parent` | Variable declarations (no initializer), emitters, `static` methods |
| Veneer | `extern emitter class Foo : Base` | A bare word | As emitter class |
| Pooled | `class Foo[N]` | `N` preallocated slots | As normal class, plus `create()`/`destroy()` |
| By value | `byVal class Foo` | As normal; parameters copy | As normal class; `operator =` required |

Normal, `extern`, and `emitter` classes may inherit with `: Parent` (§8.6). An alias class names
exactly one `Parent` after `for`; this is a type-aliasing relationship, not inheritance. Every form
may be extended with `extend class` (§8.7.1). `alias` and `extern` are mutually exclusive.

### 8.2.1 Normal Classes

**Syntax**

```syntax
class ⟨name⟩ [ : ⟨parent⟩ [ , ⟨parent⟩ … ] ] { ⟨member⟩ … }
```

**Description**

A class with no form qualifier supports the full member set: variables with initializers, methods
with statement bodies, and emitters. Instances are objects with their own storage.

A normal class that derives from neither `object` nor `_bglObject` (§21.5.8) is a **value class**:
an instance holds its members directly, a local or member of the type is an instance in its own
right, and assignment copies the members through the class's `operator =` (§2.10). A class derived
from `object` or `_bglObject` has reference semantics: a variable of the type holds a reference to an
instance owned elsewhere. The term *value class* is used throughout this specification for the
former.

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
extern class ⟨name⟩ [ : ⟨parent⟩ ] { ⟨member⟩ … }
extern class ⟨name⟩[] ;
```

In the second form `[]` is literal: it is the marker form described below.

**Description**

An `extern class` describes a type that is implemented outside Beguile. The declaration is used for
type checking and emitter dispatch only.

- Emitters are permitted and require the `emitter` keyword.
- Non-`static` methods are a compile-time error.
- Variable declarations (type and name) are permitted and drive type inference on instances.
- Variable definitions (with `=`) are accepted, but the value is metadata only: no code is generated
  for it and it is not observable from Beguile.

The **marker form** `extern class Name[];` declares that the type is pooled externally, with a pool
size Beguile does not know. It permits `new Name(…)` and `delete` on the type (§8.2.6). The body may
be present or omitted. `extern class Name[N]` with an explicit size is a compile-time error.

**Example**

```bgl
extern class object {
    parentProp    parent;
    attributeList attributes;
    emitter void  give(attribute attr){ give $val $attr }
    emitter eBool has(attribute attr){ $val has $attr }
}
```

**See also** §15.4.3 — the interoperability rules for extern classes.

### 8.2.3 `emitter class`

**Syntax**

```syntax
emitter class ⟨name⟩ [ : ⟨parent⟩ ] { ⟨member⟩ … }
```

**Description**

An `emitter class` is a type with no instance storage: it exists to give a name to a set of emitters
and operators. Every method is an emitter; the `emitter` keyword on a member is optional. A member
variable is a compile-time error, except an **alias member** (§10.3). An emitter class is used as
a type — variables may be declared of it — which distinguishes it from an emitter namespace (§7.7),
which is only called by name.

**Example**

```bgl
emitter class celsius {
    fahrenheit operator(){ $val * 9 / 5 + 32 }
    celsius operator = (celsius v){ $target = $v; }
}
```

### 8.2.4 `alias class`

**Syntax**

```syntax
alias class ⟨name⟩ for ⟨parent⟩ { ⟨member⟩ … }
```

**Description**

An `alias class` is the same type as its parent under another name; it adds typed member
declarations that let instances omit a type keyword when initializing those members. An instance of
an alias class is an instance of the root non-alias type reached by following `for` through any
chain of aliases, and its body resolves members against the alias and every class along that chain.

- Emitters are permitted and require the `emitter` keyword.
- Non-`static`, non-emitter methods are a compile-time error.
- Variable declarations (type and name) are permitted; a definition with `=` is a compile-time error.
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

### 8.2.5 Veneer Classes (`extern emitter class`)

**Syntax**

```syntax
extern emitter class ⟨name⟩ : ⟨base⟩ { ⟨member⟩ … }
```

**Description**

A class declared `extern emitter` is a **veneer class**: a distinct type with no representation of
its own. Its value *is* the word it wraps; the class adds a type and behavior (emitters, operators)
but no storage. An instance is a bare variable, not a world-tree object. The primitive types (`int`,
`bool`, `char`, `string`, the literal pseudo-types) are declared this way, naming `_bglObject` as
their base to take the shared member surface (§21.5.8).

Because a veneer has nothing to initialize beyond the word it wraps, a value of the base type is a
complete instance: given a matching `operator =`, a veneer over `int` accepts an `int` (or any
int-compatible value such as an enum member) directly.

A veneer is a *newtype*, not an alias: `glulxImage` and `int` are different types that share a
representation, and conversion between them is explicit (`operator()` to the base, `operator =(base)`
from it). An `alias class` (§8.2.4) is the *same* type under another name.

**Example**

```bgl
extern emitter class int : _bglObject {
    emitter int operator + (int v){ $val + $v }
}

glulxImage cover = eAssets.coverArt;    // a veneer over int accepts the int
int w = cover.width();                  // behavior without storage
```

### 8.2.6 Pooled Classes

**Syntax**

```syntax
class ⟨name⟩[⟨n⟩] [ : ⟨parent⟩ ] { ⟨member⟩ … }
extern class ⟨name⟩[] ;
```

`[` and `]` are literal: `⟨n⟩` is a positive integer literal or the name of a compile-time integer
constant, and the empty `[]` is the extern marker form (§8.2.2).

**Description**

A normal class may reserve a fixed number of instances by adding `[N]` after its name. Instances are
then obtained and released with `new` (§4.13) and `delete` (§5.15). There is no dynamic allocation:
`new` returns one of the `N` preallocated slots, or `nothing` when the pool is exhausted, and the pool
never grows; the result of `new` must be tested before use.

**Pool size.**
- `[N]`, a positive integer literal.
- `[IDENT]`, an identifier naming a compile-time integer constant (`const int IDENT = …;`). Owned
  members (§8.3.4) are not permitted on an identifier-sized pool.
- `[]` is valid only on `extern class` (§8.2.2).
- `[N]`/`[IDENT]` are compile-time errors on `emitter class` and `alias class`.
- `extend class Name[…]` is a compile-time error; the pool size belongs to the original declaration.

**Inheritance.** Subclasses share the parent's pool; there is no per-subclass size.

**`create()` and `destroy()`.** A pooled class may declare a `create()` method, run when a slot is
allocated, and a `destroy()` method, run before a slot is returned to the pool. Both are optional.

- `create()` returns `void` and may declare parameters; the arguments of `new Name(args)` are passed
  to it. If `create()` is not declared, `new Name()` with no arguments is the only valid form.
- **[Z-machine]** `create()` may declare at most three parameters; a fourth is a compile-time
  error. I6's class-message veneer enumerates the arguments it forwards on the Z-machine and
  raises a run-time error past three. Glulx forwards them all, so the limit does not apply there.
- `destroy()` returns `void` and takes no parameters.
- One `create` and one `destroy` per class; overloads are compile-time errors.

**Owned members in a pool.** Each slot has its own backing for every owned member (§8.3.4). `new`
attaches a backing and resets its members to their declared defaults, so a reused slot always starts
fresh; `delete` releases it. Any `create()`/`destroy()` the class declares runs after this reset.

**Example**

```bgl
class marbleClass[10] : object {
    int weight = 0;
    void create(int w){ weight = w; }
    void destroy(){ }
}

void Main(){
    marbleClass m = new marbleClass(5);     // create(5) runs; m.weight → 5
    if(m == nothing) return;                // pool exhausted
    delete m;                               // destroy() runs, slot returns to the pool
}
```

**Notes**

A file-scope instance of a pooled type (`Name m;` at file scope, not obtained from `new`) is not part
of the pool. Passing such an instance to `delete`, or otherwise mixing file-scope and pooled instances
of one type, is not detected at compile time and the behavior is undefined.

### 8.2.7 `byVal class`

**Syntax**

```syntax
byVal class ⟨name⟩ { ⟨member⟩ … }
```

**Description**

By default a class-typed parameter is passed by reference. A `byVal class` parameter is passed by
**value**: at each call the argument is copied into the parameter through the class's `operator =`,
so mutations inside the callee do not affect the caller's instance.

- `operator =` accepting the class (or `var`) is required; declaring a `byVal class` without one is a
  compile-time error.
- A `byVal class` cannot inherit from `object`.
- `byVal` cannot be combined with `extern`, `emitter`, `extend`, or `alias`.
- The marker is not inherited; a subclass must declare `byVal` itself.

**Example**

```bgl
byVal class Temperature {
    int degrees = 0;
    emitter Temperature operator = (Temperature v){ $target = $v; }
}
void heatUp(Temperature t){ t.degrees = t.degrees + 10; }   // local copy only

Temperature room;
heatUp(room);       // room.degrees unchanged
```

### 8.2.8 `typesealed` Members

**Syntax**

```syntax
typesealed ⟨type⟩ ⟨name⟩ ;
```

**Description**

A member declared `typesealed` in a base class has its type locked. A subclass or object instance may
re-initialize the member; if it re-declares it with a different type keyword, the sealed type is
kept, the written type is ignored, and a warning is reported. The initializer is still validated
against the written type. The canonical sealed member is `object.parent` (`parentProp`), so no
`object`-derived class can repurpose `parent` as a differently-typed member.

**Example**

```bgl
class place : object { }
object southOfRockWall { }

place cliffEdge {
    parent = southOfRockWall;               // idiomatic: no type keyword
    // object parent = southOfRockWall;     // accepted, with a warning; stays parentProp
}
```

## 8.3 Members

### 8.3.1 Member Variables

**Syntax**

```syntax
⟨type⟩ ⟨name⟩ [ = ⟨value⟩ ] ;
array<⟨type⟩> ⟨name⟩[⟨n⟩] ;
array<⟨type⟩> ⟨name⟩ = { ⟨value⟩ , … } ;
```

`<`, `>`, `[` and `]` are literal in the array forms, which are those of §12.2.

**Description**

Member variables declare the per-instance state of a class. An `array<T>` member accepts the same
forms as at file scope and in object bodies (§12.2, §12.7): a sized declaration or an initializer
list, and each instance gets its own storage for it. Class-typed members hold a reference unless they
are owned (§8.3.4).

**Example**

```bgl
class Inventory : object {
    array<int>    slots[6];
    array<object> heldRefs = { lamp, key };
    array<char>   nameBuf[16];
}
```

### 8.3.2 `const` Members

**Syntax**

```syntax
const ⟨type⟩ ⟨name⟩ [ = ⟨value⟩ ] ;
```

**Description**

A `const` member may be initialized in the class or in an object declaration and cannot be assigned
afterwards; assignment is a compile-time error. `const` and `static` are mutually exclusive.

**Example**

```bgl
class Config {
    const int    maxScore = 100;
    const string title    = "My Game";
}
Config config;
config.maxScore = 200;      // compile-time error
```

### 8.3.3 `static` Members

**Syntax**

```syntax
static ⟨type⟩ ⟨name⟩ [ = ⟨value⟩ ] ;
```

**Description**

A `static` member is class-level state shared by all instances, read and written as
`ClassName.member`. Inside a method the class name is required; a bare name resolves to an instance
member. Beguile's `static` is unrelated to Inform 6's `static` (an immovable object).

A class name reaches **only** what belongs to the class: its `static` members, its `static` methods,
its emitter values and its alias members. Naming a per-instance member through the type
(`Counter.someField`, or through a type alias — §10.2) is a compile-time error; a per-instance
member exists only on an instance.

**Example**

```bgl
class Counter {
    static int instanceCount = 0;
    void increment(){ Counter.instanceCount = Counter.instanceCount + 1; }
}
```

**See also** §9.6 — `static` methods and operators.

### 8.3.4 Owned Members

**Syntax**

```syntax
⟨value-class⟩ ⟨name⟩ ;
```

**Description**

A class-typed member is an **owned member** when all three hold: its type is a value class (§8.2.1),
that type has stored members, and the member is declared without an initializer. An owned member is
a live instance of its own — every instance of the enclosing class has an independent backing —
rather than a bare reference slot, so methods and operators may be called on it. Ownership is
detected structurally; there is no keyword. To keep reference semantics instead, derive the member's
type from `object` or initialize the member to an existing instance.

Owned members are what make property accessors work (§9.9). In a pooled class each slot has its own
backing, reset on `new` (§8.2.6). Owned members are not permitted on an identifier-sized pool.

**Example**

```bgl
class Box { int _val = 0; void set(int v){ _val = v; } }
class thing : object { Box b; }         // b is owned: each thing has its own Box
thing t1 {}
thing t2 {}                              // t1.b and t2.b are distinct instances
```

### 8.3.5 `inline` Members

**Syntax**

```syntax
inline ⟨type⟩ ⟨name⟩ [ = ⟨value⟩ ] ;
```

**Description**

An `inline` member is a positional slot for inline object construction: when an instance is written
as `Type{ a, b, … }`, the positional values fill the class's `inline` members in declaration order,
base class first. A member that is not `inline` can be set only by name. `inline` may be declared on
an `object`-derived class or on a value class; an `array<T>` member may be `inline`. The positional
and named forms, their separators and the error cases are specified in §11.3.1.

**Example**

```bgl
class point : object { inline int x; inline int y; string label; }
point origin = point{ 0, 0 };            // x, y positional; label by name only
```

## 8.4 Methods

**Syntax**

```syntax
[ ⟨qualifier⟩ … ] ⟨type⟩ ⟨name⟩ ( [ ⟨parameter⟩ , … ] ) { ⟨statement⟩ … }
```

**Description**

A method is a function declared in a class body. Within it, a bare member name resolves to
`self.name`; the explicit `self.name` form is equivalent. Methods may be overloaded by parameter-type
signature; two methods with the same name and the same signature are a compile-time error (§11.9.2).
A method may be declared `static` (§9.6), `default`, `replace` (§8.7.2, §8.7.3), or
`static superposed` (§3.12; `superposed` on a non-`static` method has no effect and is a warning).
An operator member follows the same declaration form with an operator symbol in place of the name;
operators are specified in §9.

**Example**

```bgl
class Counter {
    int count = 0;
    void incrementBy(int amount){ count = count + amount; }    // count → self.count
    bool isAbove(int threshold){ return count > threshold; }
}
```

## 8.5 Lifecycle: `init` and `deinit`

**Syntax**

```syntax
emitter void init() { ⟨i6-template⟩ }
emitter void deinit() { ⟨i6-template⟩ }
static ⟨type⟩ deinit( ⟨type⟩ ⟨value⟩ ) { ⟨statement⟩ … }
```

**Description**

A class of any form may declare `init` and `deinit`, which run automatically for a local variable of
that type.

- `init` fires immediately after the variable is declared, before any initializer assignment.
- `deinit` fires at the end of the block the variable was declared in, and before any `return` that
  leaves that block. For a variable declared at the routine's top level these are the same thing:
  the routine's end and every `return` in it. Releases run in reverse declaration order.
- In these instance forms both must be emitters and declare no parameters; either violation is a
  compile-time error. `init` has no other form and cannot be `static`.

A `static deinit` with exactly one parameter is the **value form**: it releases a value a container
holds without a receiver, and is what `array<T>` calls for dropped elements (§12.10). A type that
owns storage generally declares both.

**Example**

```bgl
extend extern class string {
    emitter void init()   { $self = GetNewString(); }
    emitter void deinit() { FreeString($self); }
}

void doSomething(){
    string s;           // init fires
    s = "hello";
    return;             // deinit fires first
}                       // deinit also fires on fall-through
```

**See also** §9.8 — `init` and `deinit` among the members that must be emitters; §12.10 — the value
form of `deinit` as `array<T>` uses it.

## 8.6 Inheritance

**Syntax**

```syntax
class ⟨name⟩ : ⟨parent⟩ [ , ⟨parent⟩ … ] { ⟨member⟩ … }
```

**Description**

A class inherits every member of the parents listed after the colon. Multiple inheritance is
permitted. Member lookup walks the hierarchy depth-first, left to right, and the first match wins;
when two parents declare the same member name, the first-listed parent's member is used. Inside a
method, bare identifiers resolve inherited **variable** members from all bases by this search;
inherited methods resolve through method dispatch.

To dispatch to a specific ancestor's version of a member, cast the receiver: `(Animal)myDog.speak()`
(§4.11).

**Example**

```bgl
class Flyer   : object { int altitude = 0; }
class Swimmer : object { int depth = 0; }
class FlyingFish : Flyer, Swimmer {
    void status(){ print(altitude); print(depth); }     // from Flyer, from Swimmer
}
```

## 8.7 Extending and Replacing Members

### 8.7.1 `extend class`

**Syntax**

```syntax
extend [ extern ] class ⟨name⟩ { ⟨member⟩ … }
```

**Description**

`extend class` adds members to an already-declared class; the name must already be a type. An
`extern` class accepts only emitters and `static` members; a class declared in Beguile accepts any
member. `extend class` may not change a type parameter (§8.1.1) or a pool size (§8.2.6).

**Example**

```bgl
extend class Counter {
    emitter bool isZero(){ $self.value == 0 }
}
extend extern class int {
    emitter string asHex(){ $self.toHexString() }
}
```

**See also** §11.10 — `extend` for objects; §12.11 — `extend` for arrays.

### 8.7.2 `replace`

**Syntax**

```syntax
replace ⟨member-declaration⟩
```

**Description**

Inside `extend class`, the `replace` qualifier replaces an existing member instead of adding one.
It is required when the new member would duplicate an existing one; a duplicate without `replace` is
a compile-time error. `replace` on a member that does not exist is a warning, and the member is added.

`replace` also applies to global functions (§6.5) and to `extend` on objects (§11.10).

**Example**

```bgl
extend extern class string {
    replace emitter string operator = (stringLiteral v){ $self.set(v); }
}
```

### 8.7.3 Shadowing and `default`

**Syntax**

```syntax
default ⟨method-declaration⟩
```

**Description**

When a derived class or an object body declares a method that already exists in a base class, a
warning is reported unless the declaration carries `replace`, which states that the override is
intentional.

A base-class method marked `default` is expected to be overridden: overriding it needs no `replace`
and produces no warning. `default` is valid only in a class declaration; in an object or verb body
it is a compile-time error. An override that is not itself marked `default` ends the chain — further
descendants again need `replace` — so mark each override `default` to keep a method freely
overridable. The `verb` class declares `handler()` this way (§13.2.1).

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
hide ⟨member⟩ ;
hide ⟨member⟩ . operator ⟨op⟩ [ ( ⟨type⟩ , … ) ] ;
hide ⟨method⟩ [ ( ⟨type⟩ , … ) ] ;
```

**Description**

`hide` removes an **inherited** member, or one operator of it, from a subtype's static surface, so
that accessing it *through that type* is a compile-time error. It is permitted in a subclass body and
in `extend class`.

- `hide member;` hides the whole member, read and write.
- `hide member.operator op;` hides only that operator; `hide height.operator =;` blocks writes and
  leaves reads intact.
- A parenthesized operand list narrows the hide to one overload; without it, all overloads are
  hidden.
- Only inherited members may be hidden; a `hide` naming a member that is not inherited, or an
  operator its type does not have, is a warning with no effect.

Hiding changes only what resolves through the subtype; the member still exists. A value of a base type
that does not hide it reaches it, whether by upcast or by passing the value to a base-typed parameter.

**Example**

```bgl
class dim {                           // a value class with a getter and a setter (§9.9.1)
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
side.height = 5;                      // compile-time error
(baseWin)side.height = 5;             // OK: the base surface still has the write
```

**See also** §9.9 — property accessors, the usual target of `hide member.operator =;`.

### 8.7.5 Matching Rules

**Description**

How `replace` and shadowing decide that two declarations name the same member:

- In `extend class`: `replace` replaces a member of the same class. In `extend` on an object
  (§11.10): the same rules, applied to the object's members.
- In a derived class or object body: `replace` suppresses the shadowing warning for a method found
  anywhere in the base hierarchy.
- **Emitters** match by name and full parameter-type signature, because emitters may be overloaded.
- **Methods** and **member variables** match by name alone.

**Example**

```bgl
class Animal : object {
    int  legs = 4;
    void speak(){ print("..."); }
    emitter bool fits(int size){ $self.legs <= $size }
}

extend class Animal {
    replace int  legs = 2;                          // member variable: matched by name
    replace void speak(){ print("Hello."); }        // method: matched by name
    emitter bool fits(string label){ $self.legs }   // emitter, new signature: added, no replace
    replace emitter bool fits(int size){ $self.legs == $size }   // emitter, same signature: replaced
}

class Dog : Animal {
    replace void speak(){ print("Woof!"); }         // found in the base hierarchy: no warning
}
```

---

# 9 Operators and Accessors

Operator overloading and property accessors are the two ways a class gives its values behavior at
sites that are not method calls. An overloaded operator supplies the meaning of `a + b`, `x == y`,
`buf[i]` or a conversion for values of the class; a property accessor runs code on every plain read
or write of a member. Both are class members and follow the member rules of §8: they are declared in
a class body, inherited (§8.6), extended, replaced and hidden (§8.7) like any other member, and each
comes in an emitter form (§7) and a regular-method form. This chapter assumes §7 and §8.

§9.1 through §9.8 specify operators: the overloadable set and the general declaration form, the
emitter and non-emitter forms, subscript, conversion, the special operators `?`, `switch`, `?=`, `<=>`
and `!`, `static` operators, the fallbacks for compound assignment and increment, and the operators
that must be emitters. §9.9 specifies property accessors, which are built from a conversion operator
and an assignment operator declared on a value class.

## 9.1 Overloadable Operators

**Syntax**

```syntax
[ emitter | static ] ⟨type⟩ operator ⟨op⟩ ( [ ⟨parameter⟩ , … ] ) { ⟨body⟩ }
```

**Description**

An operator may be overloaded on any class as an **emitter** (§7.2) or as a **regular method**. The
operator symbol takes the place of the method name; the left operand is the receiver and the right
operand, if any, is the parameter. When `a op b` is compiled, `operator op` is looked up on the type
of `a` with a parameter matching the type of `b`, walking the inheritance hierarchy (§8.6). The
symbols that may follow `operator` are:

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
| Auto inference | `auto` | `T operator auto();` | §7.8 |

`prefix++` and `prefix--` name the prefix forms (`++n`); `++` and `--` name the postfix forms, so a
class may give each its own behavior. Declaring `operator` with any symbol outside this set is a
compile-time error. The valid operator tokens `?.`, `??`, `=>`, and the reference-binding operator
`:=` (§3.7) are not overloadable; `:=` exists precisely to bypass operator dispatch.

The `==` row's `static` shape is the form a generic container calls (§9.6, §12.10).

**See also** §4.4 — binary operator resolution; Appendix C — the same table as a quick reference.

## 9.2 Emitter and Non-Emitter Operators

**Description**

An emitter operator substitutes its body at each use site, with `$self`/`$val` bound to the left
operand and `$paramName` to the right (§7.3).

A non-emitter operator is a method called at each use site, with the right operand as its argument.
Use it when the body is complex, when it should be visible to the debugger, or when code size matters.

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

> **Which overload wins.** When a type declares several overloads of one operator, the right-hand
> operand selects between them in four passes: the operand's **exact** type; the **base type of a
> literal** (`intLiteral` → `int`); a type the base is **convertible** to; and finally a parameter
> declared `var`. `var` accepts anything, so it is the last resort rather than an exact match — which
> is what lets a subclass override an operator it inherits. `object` declares
> `emitter eBool operator == (var v)`, and were `var` treated as exact, a class deriving from
> `object` could never give `==` its own meaning. An operand whose type is itself `var`, or whose
> type is unknown, still matches in the first pass; there is nothing more specific to prefer.

## 9.3 Subscript: `operator []` and `operator []=`

**Syntax**

```syntax
⟨element-type⟩ operator [] ( int ⟨index⟩ ) { ⟨body⟩ }
⟨type⟩ operator []= ( int ⟨index⟩ , ⟨element-type⟩ ⟨value⟩ ) { ⟨body⟩ }
```

`[` and `]` are literal: they are part of the operator name.

**Description**

`operator []` takes the index and returns the element type; `operator []=` takes the index and then
the value, and returns a type of the author's choice — commonly the assigned type, so the assignment
can be used as an expression. Any class may declare them, and a subclass inherits them. The result of
a subscript read supports member access, resolved against the element type.

**Example**

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

**See also** §12.3 — subscripts on `array<T>`.

## 9.4 Conversion: `operator ()`

**Syntax**

```syntax
[ explicit ] emitter ⟨type⟩ operator ( ) ;
[ explicit ] emitter ⟨type⟩ operator ( ) { ⟨i6-template⟩ }
[ explicit ] ⟨type⟩ operator ( ) { ⟨statement⟩ … }
```

**Description**

A zero-parameter `operator ()` declares that a value of this class converts to `⟨type⟩`. The
compatibility rules that decide *when* a conversion is applied — assignment, argument matching,
operator resolution, casts — are in §2.11 and §2.12.

- By default a conversion is **implicit** and is applied automatically wherever the rules of §2.11
  allow. An `explicit` conversion applies only at an explicit cast `(T)expr` (§4.11). `explicit` is
  valid only on `operator ()` and cannot be combined with `const` or `static`.
- The emitter form substitutes its body with `$self`/`$val` bound to the source value.
- The regular-method form runs at runtime and also fires on a **bare read** of a member of this type
  — in an initializer, an argument, or an expression — not only at cast sites. This is the getter
  half of a property accessor (§9.9.1).

**Example**

```bgl
class MyType : _bglObject {
    emitter int operator ();                   // implicit
    explicit emitter string operator ();       // only via (string)x
}
MyType t;
int    n = t;              // OK
string s = (string)t;      // OK; `string s = t;` is a compile-time error

class heightProxy {
    int _val = 0;
    int operator (){ return _val; }            // regular method: fires on bare read
}
```

**Notes**

`emitter T operator ();` (semicolon, no body) is the pass-through conversion; it is
equivalent to `emitter T operator () { $val }`.

## 9.5 Special Operators

**Description**

| Operator | Use site | Description |
|---|---|---|
| `operator ? ()` | `x?`, `x?.m`, `x ?? y` | Defines what "present" (non-null) means for the type, returning `eBool`. Evaluated at each step of `?.`, to decide whether `??` needs its fallback, and directly by postfix `?`. A type without it cannot use any of the three; doing so is a compile-time error. Must be an emitter. |
| `operator switch (U v)` | `switch(x){ case v: }` | The comparison applied to each `case` value. Must be an emitter. Statement semantics: §5.12. |
| `operator ?= (U v)` | `x ?= y` | A binary comparison at equality precedence with result `eBool`. No built-in type defines it; a class gives it a meaning. |
| `operator <=> (U v)` | `x <=> y` | Three-way comparison, result `int`: negative when the left operand orders first, `0` when equivalent, positive otherwise. Declarable `static` or as an instance operator (§9.6). |
| `operator ! ()` | `!x` | Prefix logical not. |

`string` defines `operator ? ()` as a non-zero handle test. The language has no built-in notion of
null; it is entirely type-defined. Expression semantics of `?`, `?.` and `??` are in §4.10.

**Example**

```bgl
extern class object {
    emitter eBool operator ? () { $self ~= nothing }
}
```

## 9.6 `static` Operators and Three-Way Comparison

**Syntax**

```syntax
static ⟨type⟩ operator ⟨op⟩ ( ⟨type⟩ ⟨left⟩ , ⟨type⟩ ⟨right⟩ ) { ⟨statement⟩ … }
static ⟨type⟩ ⟨name⟩ ( [ ⟨parameter⟩ , … ] ) { ⟨statement⟩ … }
```

**Description**

A `static` operator takes **both** operands as parameters and has no receiver. It is a free routine
rather than a member of an instance, which is why it may be declared on an `extern`, `emitter`, or
`alias` class, none of which permit other non-emitter methods. `static` cannot be combined with
`emitter` or `const`.

Generic containers cannot see their element type at runtime. `array<T>`'s search and sort members
compare **words** unless the element type publishes `operator ==` and `operator <=>`, which they
obtain through `$opref` (§7.3.1). Two forms exist because an element may or may not be an object:

| Element at runtime | Form used |
|---|---|
| An object (class instance) | A non-emitter instance operator, sent to the element |
| A bare word (`string`, `float`) | A `static` operator, called with both operands |

A bare-word type can publish only the static form. A type that declares neither keeps word comparison.
Where a type declares both an instance and a static form of one operator, the instance form is used at
ordinary use sites and the static form where generic code needs an address. Which members of `array<T>`
consult which operator is tabulated in §12.10.

`<=>` follows the same rule: `static`, or an instance operator with the left operand as receiver. Either
form is usable in an expression or a comparator lambda.

**Example**

```bgl
extern int compareText(string a, string b);     // an I6 routine: negative, 0 or positive

extend class string {
    static bool operator ==  (string a, string b) { return compareText(a, b) == 0; }
    static int  operator <=> (string a, string b) { return compareText(a, b); }
}

array<string> names = { "cherry", "apple", "banana" };
names.indexOf("apple");                             // → 1, via string's ==
names.sort();                                       // apple, banana, cherry, via <=>

string a = "pear";
string b = "plum";
int r = a <=> b;                                    // -1, 0, or +1
names.sort((string a, string b) => a <=> b);
```

**See also** §4.15 — `Type::operator op`; only a `static` operator is referenceable from ordinary code; §3.12 — `static superposed` methods.

## 9.7 Compound Assignment and Increment Fallback

**Description**

When no `operator op=` is declared for a compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`, `&=`,
`|=`, `^=`, `<<=`, `>>=`), the statement is expanded to the equivalent simple assignment. When no
`operator ++`/`--` (or `prefix++`/`prefix--`) is declared, the increment or decrement is applied
directly to the variable.

**Example**

```bgl
n += 2;         // → n = n + 2;  when n's type declares no operator +=
```

**Notes**

`n op= v` on a type that declares no `operator op=` is equivalent to `n = n op v`;
`n++` and `n--` on a type that declares no increment operator are equivalent to `n = n + 1` and
`n = n - 1`.

**See also** §5.6 — the compound-assignment statement; §5.7 — increment and decrement.

## 9.8 Emitter-Required Operators

**Description**

The following must be declared with `emitter` (implied inside an `emitter class`); declaring any of
them as a regular method is a compile-time error:

| Declaration | Reason |
|---|---|
| `operator ? ()` | Substituted as the null test in `?.`, `??`, and postfix `?` |
| `operator switch (U v)` | Substituted as the comparison of each `case` |
| `init()` | Substituted at the declaration site; not callable as a method |
| `deinit()` | Substituted at scope exit and `return`; not callable as a method |

## 9.9 Property Accessors

A **property accessor** is a member that reads and writes like a plain member but runs code on each
access. It is built from a regular-method `operator ()` (the getter, §9.4) and an `operator =` (the
setter) declared on a value class (§8.2.1), and the value class is then used as an owned member
(§8.3.4) of the host class or object. A read of the member dispatches the getter; a write dispatches
the setter, never the getter.

Accessors work on single objects, on every instance of a class, and on pooled instances obtained with
`new`. An accessor is declared either as a named class that then types the member (§9.9.1) or in place
with `auto { … }` (§9.9.2); only the in-place form, declared directly on an object, may reach its host
through `outer` (§9.9.3).

### 9.9.1 Getters and Setters

**Syntax**

```syntax
class ⟨accessor⟩ {
    [ ⟨type⟩ ⟨name⟩ [ = ⟨value⟩ ] ; … ]
    ⟨type⟩ operator ( ) { ⟨statement⟩ … }
    ⟨type⟩ operator = ( ⟨type⟩ ⟨value⟩ ) { ⟨statement⟩ … }
}
⟨accessor⟩ ⟨member⟩ ;
```

The first form declares the accessor class: optional backing members, a getter and a setter. The
second form declares the accessor as an owned member of a host class or object.

**Description**

- The getter is a regular-method `operator ()` with no parameters. It runs on every bare read of the
  member — in an initializer, an argument, or an expression — and its return type is the type the
  member reads as.
- The setter is an `operator =` taking one parameter. It runs on every assignment to the member; the
  assigned value is its argument.
- The accessor class must be a value class with at least one stored member, and the member must be
  declared without an initializer, so that it is owned (§8.3.4); otherwise the member is an ordinary
  reference slot and no accessor dispatch occurs.
- Inside either operator, `self` is the accessor instance and its backing members. A named accessor
  class has no access to its host; `outer` (§9.9.3) is available only to an inline accessor.

**Example**

```bgl
class heightProxy {
    int _val = 0;
    int  operator ()        { return _val; }
    void operator = (int v) { _val = v * 2; }
}
class thing : object { heightProxy height; }
thing t {}
t.height = 5;               // setter: _val → 10
int a = t.height;           // getter: a → 10
```

**See also** §8.7.4 — `hide member.operator =;` makes an accessor read-only on a subtype.

### 9.9.2 Inline Accessors: `auto { … }`

**Syntax**

```syntax
auto ⟨member⟩ = { ⟨accessor-body⟩ } ;
```

`⟨accessor-body⟩` is the class body of §9.9.1: backing members, a getter and a setter.

**Description**

`auto name = { … }` declares the accessor in place, inside a class or object body: the braces hold a
class body (backing members and the two operators) and the member's type is the synthesized class.
`auto`, not `var`, is required, because the type is inferred from the body. The braces are read as a
class body, rather than a `{ v1, v2 }` initializer list, when they begin with a member declaration or
an `operator`.

**Example**

```bgl
object gadget {
    auto level = {
        int _raw = 0;
        int  operator ()        { return _raw; }
        void operator = (int v) { _raw = v * 3; }
    }
}
gadget.level = 4;           // _raw → 12
int n = gadget.level;       // n → 12
```

**Notes**

`auto level = { int _raw = 0; int operator (){ … } void operator = (int v){ … } }`
is equivalent to declaring a hidden class with that body and writing `HiddenClass level;`.

### 9.9.3 `outer`

**Syntax**

```syntax
outer . ⟨member⟩
```

**Description**

Inside an accessor body two receivers are available: `self`, the accessor instance and its own
backing members; and `outer`, the host object the accessor is declared on. `outer` is resolved at
compile time and may read and write the host's other members. It is available only inside an inline
`auto { … }` accessor (§9.9.2) declared directly in an object body (§11.2); it is a compile-time error
in a named accessor class (§9.9.1) and in an inline accessor declared in a class body.

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
gadget.level = 4;           // _raw → 12
```

---

# 10 Namespaces

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
  `alias asm = bglOpCodes` is equivalent to the alias-member form `emitter auto asm = bglOpCodes;`, a
  compile-time redirect with no I6 backing.

Only these forms make a path step a namespace step. An ordinary member whose declared value happens
to name an object — `object door { room target = hallway; }` — is storage: `door.target` reads the
member at run time and may be assigned.

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

---

# 11 Objects

## 11.1 Overview

An *object* is a named, globally visible instance that exists as a concrete entity in the story file.
A class (§8.1) is a type; an object is a single instance of a type. **Any** class may be instantiated
as a named object, including utility classes and data tables; objects are commonly used for
world-model entities (rooms, things, characters), and those inherit from `object` to get the
world-model members. The declaration says which class is instantiated, not whether the instance is an
`object`: a base that does not derive from `object` gives an instance that is not one, whichever of
the two declaration forms is used (§11.2).

## 11.2 Declaring an Object

**Syntax**

```syntax
object ⟨name⟩ [ asI6 ⟨i6name⟩ ] { ⟨member⟩ … }
object ⟨name⟩ [ asI6 ⟨i6name⟩ ] : ⟨base⟩ [ , ⟨base⟩ … ] { ⟨member⟩ … }
⟨class⟩ ⟨name⟩ [ asI6 ⟨i6name⟩ ] { ⟨member⟩ … }
```

**Description**

An object is declared at global scope. The name becomes a globally visible identifier usable wherever
an `object`-typed value is expected. The body holds member declarations in the same form as a class
body (§8.3.1): a member is written `Type name [= value];` and members are `;`-separated. A member
with no initializer defaults to `0`, `false` or `nothing` according to its type.

The class an object instantiates is given in one of two equivalent ways. Using the class name as the
type keyword (`ClassName Name { … }`) is the usual form. The inheritance form (`object Name : Base`)
is required when the object inherits from more than one base. An object declared with neither
(`object Name { … }`) is an instance of `object`.

An object declared with no base, or with a base that derives from `object`, is an instance of
`object` and carries `parent`, `children` and `attributes` (§11.5). `object` is **not** added
implicitly: an object whose bases do not derive from `object` has the static type of its first base,
is not assignable to an `object` variable, and lacks the `object` members. Give such a declaration
`object` as an explicit base when the world-model members are wanted.

The optional `as i6name` clause names the object differently in the emitted I6 (§3.11).

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

**Notes**

`ClassName Name { … }` is equivalent to `object Name : ClassName { … }`.

**See also** §11.4 — members and type inference; §11.11 — `extern object`.

## 11.3 Inline Objects — `Type{ … }`

**Syntax**

```syntax
⟨type⟩{ ⟨value⟩ , … }
⟨type⟩{ ⟨name⟩ = ⟨value⟩ ; … }
⟨type⟩{ ⟨value⟩ , … ; ⟨name⟩ = ⟨value⟩ ; … }
{ … }
```

The last form omits `⟨type⟩`; it is permitted only where the target type is known (§11.3.2).

**Description**

An object declaration written in expression position without a name declares an *anonymous* object
and evaluates to a reference to it. It is the same declaration as the named form
(`point p { x = 1; y = 2; }`) with the name omitted, and it is a compile-time declaration: nothing runs
at startup, and it is unrelated to `new` (§4.13), which allocates at run time.

The form is available for any class that can be declared as a named object. It has no meaning for
namespace types (`emitter class`, `alias class`) or value classes. It may appear anywhere an
expression may: an array-literal element, an `inject` element in `extend` for arrays (§12.11), a
variable initializer, a call argument, or standing alone as a statement.

### 11.3.1 Positional and Named Members

**Description**

A member is given *positionally* or *by name*, and the separators carry meaning:

| Form | Separator | Fills |
|---|---|---|
| Positional: `{ 3, 4 }` | `,` | The next `inline` member (§8.3.5), in declaration order, base class first. Only `inline` members participate. |
| Named: `{ x = 1; y = 2; }` | `;` | The member named, `inline` or not. The `=` is required. |
| Combined: `{ 5, 6; label = "p"; }` | `,` then a single `;` | Positional values first; the first `;` ends the positional section and begins the named section. |

Supplying more positional values than there are `inline` members is a compile-time error. After the
`;` that ends the positional section only named members may follow; a `,` before or among named
members is a compile-time error, as is a named member before a positional one. Ordinary object and
class bodies remain `;`-only; `,` is never a member separator there.

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

**Description**

Where the target type is already known, the leading `Type` may be omitted and a bare `{ … }` takes the
target's type. Inference applies in four positions:

- an `array<T>` element;
- an object-backed variable initializer (`Type name = { … }`);
- the element of a declarative `inject` (§12.11);
- a call argument whose parameter is an object-backed class.

A bare `{ … }` produces an object only when the target type is an object-backed class. If the target
is an `array<…>`, or a value class or collection type with an `operator =(initializerList)`, a bare
`{ … }` is a braced list. An explicit `Type{ … }` is always available and also distinguishes a single
element from a list.

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

**Notes**

`Type name = { … };` is equivalent to the named object declaration
`Type name { … }`.

### 11.3.3 Inline Objects as Arguments

**Description**

A `Type{ … }` or bare `{ … }` may be passed directly as a call argument, positionally or as a named
argument (§6.3), and on a method call.

For the bare form the parameter type must resolve unambiguously: the type is inferred only when every
viable overload of that name expects the same aggregate-constructible class at that argument position
(an object-backed class, or a value class declaring `inline` members). If the overloads disagree, or
the parameter cannot take an aggregate, it is a compile-time error; write an explicit `Type{ … }`.

**Example**

```bgl
void place(point p) { … }
place(point{3, 4});
place({3, 4});
place(p: {3, 4});
obj.method({ x = 1; y = 2; });
```

### 11.3.4 Constant and Run-time Members

**Description**

When every member value is a compile-time constant, the expression denotes one *constant instance*,
so repeated evaluations yield a reference to the *same* object. When a member value is a run-time
expression (a local or parameter, as in `foo({ width, height })`), the expression denotes a per-site
instance whose run-time members are populated immediately before each evaluation, so each evaluation
sees freshly populated values. A recursive call that re-enters the same site repopulates that site's
single instance, so after the inner call returns the outer activation sees the inner call's values.

**Example**

```bgl
class dims : object { inline int width; inline int height; }
void report(dims d) { print(d.width); print(d.height); }

void Main() {
    report({ 3, 4 });            // constant: one constant instance
    int w = 5;
    int h = 6;
    report({ w, h });            // run-time: populated before each evaluation
}
```

### 11.3.5 Nested Aggregates

**Description**

A member value may itself be a `{ … }` aggregate, whose shape is taken from the member's type: an
`array<T>` member takes a braced array literal, and an object-backed member takes a nested inline
object. An `array<T>` member may be declared `inline`, making it a positional slot like any other.

**Example**

```bgl
class menu : object {
    inline object linkTo;
    inline string title;
    inline array<dictionaryWord> words;
}

menu child = { root, "a child", {.foo, .bar, .baz} };
```

### 11.3.6 Standalone Declarations

**Description**

An inline object may stand alone as a statement, with no name and no assignment. The reference is
discarded, so the object is reachable only if it links itself, for example by setting a positional
`parent` or `linkTo` that places it in the object tree. Without such a link it is an unreferenced
object, reachable only by an object-tree walk (§21.9).

**Example**

```bgl
menu{ root, "text to display", {.type, .kind} };
```

## 11.4 Members and Type Inference

**Description**

Members of an object body are declared with a type, as in a class body. When the object is an instance
of a class, a member already declared on that class may be set without repeating the type. The
compiler searches the object's declared class first, then its base classes, and finally the base
`object` class. A typeless member name that is found on no class in the hierarchy is a compile-time
error.

**Object references.** A member may hold a reference to another object. The assigned object must be
type-compatible with the declared member type, and a declared object instance is stored as a direct
reference; no `init()` is called.

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

**See also** §8.3.4 — owned members; §3.7 — `ref` members; §11.8 — array members.

## 11.5 Special Members: `parent`, `children`, `attributes`

Three members declared on the base `object` class have compiler-level support tied to the world model:
`parent` and `children` place objects in the object tree, and `attributes` sets the object's attribute
flags. They are available on every object.

### 11.5.1 `parent`

**Syntax**

```syntax
object parent = ⟨container⟩ ;
⟨object⟩ . parent = ⟨container⟩ ;
```

The first form appears in an object body; the second is a run-time statement.

**Description**

In an object body, `parent` places the object inside another object at game start. At run time,
assigning to `obj.parent` moves the object.

**Example**

```bgl
object cloak {
    object parent = selfobj;     // the player carries it at game start
}
```

### 11.5.2 `children`

**Syntax**

```syntax
children = { ⟨object⟩ , … } ;
⟨object⟩ . children += { ⟨object⟩ , … } ;
for ( object ⟨name⟩ in ⟨object⟩ . children ) ⟨statement⟩
⟨object⟩ . children . length ( )
```

The first form appears in an object body; the others are run-time expressions and statements.

**Description**

`children` is the inverse of `parent`: the object's child collection in the object tree.

**Placement.** In an object body, `children = { a, b, c }` places each listed object inside this one
at game start; it is equivalent to setting `parent` on each child, and the container may be declared
before or after its contents. An object has exactly one parent, so conflicting placement is a
compile-time error: listing an object in two containers' `children`, or listing it in one container's
`children` while it sets `parent` to a different object. Declaring the same link both ways
(`kitchen.children = { table }` and `table.parent = kitchen`) is accepted.

**Reading.** `obj.children` is a collection: it is iterated with `for … in`, and `.length()` (or its
synonym `.size()`) returns the number of direct children.

**Run-time placement.** `obj.children += { … }` moves each listed object into `obj`. `=` on
`children` is permitted only in an object body, and `-=` is not permitted; to remove an object, move
it by assigning its `parent`.

**Example**

```bgl
object table {}
object chair {}
object kitchen {
    children = { table, chair };
}
object bowl {}
object apple {}
object pear {}

for (object o in kitchen.children) { o.give(seen); }
int n = kitchen.children.length();
bowl.children += { apple, pear };
```

### 11.5.3 `attributes`

**Syntax**

```syntax
attributes = { [ ! ] ⟨attribute⟩ , … } ;
```

**Description**

`attributes` is an `attributeList` member (§21.5.1) that declares the object's initial attributes.
Each entry names an attribute; a `!` prefix explicitly clears an attribute the object would otherwise
inherit from its class. The list is additive relative to the class: attributes the class gives are
kept unless negated, and `attributes = {}` clears nothing. In an `extend` block, `attributes =` is
permitted only when the object's own declaration has no `attributes` member; otherwise it is a
compile-time error, even with `replace`.

`attributeList` accepts `=` only; `+=` and `-=` are not permitted. To change attributes at run time
use `give(attr)` and `ungive(attr)`, and test them with `has(attr)` (§21.5.1).

**Example**

```bgl
object foyer {
    attributes = {light};
}

class post : object { attributeList attributes = {scenery}; }
post lampPost {}

extend lampPost {
    attributes = {light, !scenery};
}
```

## 11.6 Attribute Declarations

**Syntax**

```syntax
attribute ⟨name⟩ ;
extern attribute ⟨name⟩ [ asI6 ⟨i6name⟩ ] ;
```

**Description**

An `attribute` declaration introduces a named flag that can be given to objects. An attribute must be
declared before use. The `extern` form refers to an attribute defined in I6 (typically by the IF
library binding, §23.3.4). Once declared, the name is an identifier of type `attribute` and may be
used in `attributes` lists and passed to `give`, `ungive` and `has`.

**Example**

```bgl
attribute myNewAttr;
extern attribute light;
```

## 11.7 Property Declarations

### 11.7.1 `property` and `extern property`

**Syntax**

```syntax
property ⟨name⟩ ;
extern property ⟨name⟩ ;
```

**Description**

Every member name that appears in any class or object declaration is a property name, and
`obj.provides(name)` (§21.5.2) tests at run time whether `obj` carries that property. No declaration
is needed for a member of a declared class.

A `property` declaration introduces a property name that is not a member of any Beguile class,
typically because it lives in I6 code, or because it is a run-time flag with no compile-time owner.
The plain form defines the property; the `extern` form refers to one defined in I6. Both make the name
available to `obj.provides(name)`.

A property declaration carries no type. The type lives at each use site: every class or object member
that contributes to the property declares its own type there. A non-additive property is
unconstrained; because an object's value overrides its class's, the contributions never share
storage, and each may be whatever type it needs. The same property name may therefore be a member of
two unrelated classes with different types.

A `property` identifier has type `property` and is accepted wherever a `property` parameter is
expected. A free-standing declaration does not grant `obj.name` access; declare the name as a class
member to read or write it.

In default mode, `obj.provides(unknownName)` on an undeclared name is a compile-time error. In loose
mode (`#bgl` islands and precompiler mode, §15.3.3) the name passes through unchecked.

**Example**

```bgl
property hidden_flag;
extern property libDefinedProp;

class Box : object { int weight; }
Box g_box;

void Main() {
    if (g_box.provides(weight))         { … }   // class member
    if (g_box.provides(hidden_flag))    { … }   // free-standing declaration
    if (g_box.provides(libDefinedProp)) { … }   // extern declaration
}
```

### 11.7.2 `additive` Properties

**Syntax**

```syntax
additive property ⟨name⟩ ;
extern additive property ⟨name⟩ ;
```

**Description**

A property is normally overriding: when an object and one of its ancestor classes both supply it, the
object's value replaces the class's. An `additive` property instead accumulates: the object's
contribution and all of its ancestors' are gathered into one contiguous run of words. `additive` is
meaningful only on a `property` declaration, not on a class or object member and not on a type
declaration. On an `extern property` it records that the I6 declaration is already additive.

`name` is additive in I6 itself. The core BLR declares it, and the core is always loaded, so the rules
below apply to `name` in every program; a program that repeats `extern property name;` is in error.
Every other additive property is declared by the IF library binding that defines it (§23.3.5).

**Contributions are raw arrays.** An additive property has no length word, which is the
`rawArray<T>` layout (§12.8.1), so every member that contributes to it must be a `rawArray<T>` or a
routine. The element type is fixed by the highest ancestor that declares the member, walking the
class hierarchy root-first; every other contribution in that hierarchy must use the same element type.
Unrelated hierarchies may each fix their own. An inference-typed override (`name = {.wooden};`) takes
the ancestor's type and is always consistent. A member declared `array<T>` or as a scalar
(`int name`, `stringObj name`) on an additive property is a compile-time error, and so is an element
type that disagrees with the hierarchy's.

**Extent and operations.** A contributing member's extent is the total accumulated across every
layer, so `size()` and `length()` on `r1.name` in the example both answer 3. `size()`, `length()`
and subscripting work; every operation that needs a length word (`setLength`, `clear`,
`append`, `insert`, `prepend`, `remove`, `removeValue`, `push`, `pop`, `dequeue`, `enqueue`,
`popEnd`, `peek`, `peekEnd`, `indexOf`, `reverse`, `sort`) is a compile-time error on
such a member.

**Example**

```bgl
class Room       { rawArray<dictionaryWord> name = {.box, .crate}; }   // fixes the element type
object r1 : Room { name = {.wooden}; }                                 // matches all three words
```

**See also** §12.8.3 — member `rawArray<T>`.

### 11.7.3 Computed Property Access and `property` Parameters

**Syntax**

```syntax
⟨object⟩ . ⟨property-variable⟩
⟨object⟩ . ⟨property-variable⟩ ( [ ⟨argument⟩ , … ] )
```

**Description**

A local or parameter declared `property` (or `var`) may be dereferenced against a receiver: `obj.p`
reads the property `p` names, and `obj.m(2)` sends the message, binding `self` to `obj`. The property
is resolved at run time. The name after the dot is treated this way only when it is such a local or
parameter *and* it is not a real member of the receiver's type; a genuine member always wins. A
file-scope `property foo;` does not qualify.

When a function or emitter parameter is typed `property`, a bare property-name argument is taken as
the property identifier (an implicit `(property)` cast, §4.11) rather than as a value read. This
holds inside an object method body, where a bare member name would otherwise mean `self.name`. Any
known property name is accepted: a member of any class or object, or a free-standing `property` or
`extern property` declaration. `obj.provides(property)` is the canonical consumer.

**Example**

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
array<⟨type⟩> ⟨name⟩ = { ⟨value⟩ , … } ;
array<⟨type⟩> ⟨name⟩[⟨n⟩] ;
array<char> ⟨name⟩ = "⟨text⟩" ;
rawArray<⟨type⟩> ⟨name⟩ = { ⟨value⟩ , … } ;
```

`<`, `>`, `[` and `]` are literal.

**Description**

A member may be an array. It has the same semantics as any other array: subscripting, `for … in`,
`length()` and the `<array>` methods (§22.4) behave identically, and element type checking follows
the rules for global arrays (§12.2). A byte-array member (`array<char>`) accepts a string initializer
or a brace initializer. Storage rules for member arrays, including the Z-machine property-size limit
and `ref` members, are given in §12.7; the `rawArray<T>` member form is covered in §12.8.3 and its
use for additive properties in §11.7.2.

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
⟨type⟩ ⟨name⟩ ( [ ⟨parameter⟩ , … ] ) { ⟨statement⟩ … }
```

**Description**

An object may define methods: functions that are members of the object rather than free functions.

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

**Description**

When a method is called on an object, including `self.method()` from inside the object's own method
body, the method is looked up in this order:

1. the object's own members (per-instance overrides);
2. the object's class, then its base classes recursively.

Every object that derives from `object` (§11.2) reaches the methods declared there
(`give`, `ungive`, `has`, `provides`, `is`, …; §21.5) through the same walk. Per-instance methods
shadow inherited ones. There is no special case for the `object` base
class; it is an ordinary class reached through the ordinary hierarchy walk.

**Example**

```bgl
class Animal : object {
    void speak() { print("..."); }
}
Animal cat {
    replace void speak() { print("Meow."); }   // per-instance override
}
Animal cow { }

cat.speak();            // Meow.        — the object's own member
cow.speak();            // ...          — the class's method
cat.give(light);        // from object, through the hierarchy walk
```

### 11.9.2 Overloads

**Description**

A class or object body may declare several methods with the same name and different parameter
signatures. Overloads coexist and are dispatched by signature under the general resolution rule
(§6.4). Overloads of `operator()`, `operator[]` and `operator[]=` follow the
same rule; operator emitters may likewise be overloaded by parameter type.

**Example**

```bgl
class Logger {
    void log(int n)        { print(n); }
    void log(string s)     { print(s); }
    void log(int n, int m) { print(n); print(":"); print(m); }
}

Logger lg;
lg.log(5);          // log(int)
lg.log("hi");       // log(string)
lg.log(3, 7);       // log(int, int)
```

## 11.10 `extend` for Objects

**Syntax**

```syntax
extend ⟨object⟩ {
    ⟨type⟩ ⟨name⟩ = ⟨value⟩ ;
    replace ⟨type⟩ ⟨name⟩ = ⟨value⟩ ;
    replace ⟨type⟩ ⟨name⟩ ( [ ⟨parameter⟩ , … ] ) { ⟨statement⟩ … }
    ⟨name⟩ += { ⟨value⟩ , … } ;
    ⟨name⟩ -= { ⟨value⟩ , … } ;
}
```

**Description**

Any previously declared object may be extended. Inside the body:

- A new member is declared with `=`, as in an object body.
- `replace` replaces an existing method or member value. Adding a duplicate member without `replace`
  is a compile-time error; `replace` on a member that does not exist is a warning.
- `+=` appends to, and `-=` removes from, an existing collection member (`grammarRuleList` or
  `array<T>`). The member must exist and be a collection type; otherwise it is a compile-time error.
  `attributeList` accepts `=` only (§11.5.3). The grammar forms are specified in §13.5.

Extending an `extern` object (§11.11) is restricted to its `grammar` member: `grammar += { … }`,
`replace grammar = { … }` and `grammar -= { … }` are permitted (§13.5). Adding a member or method, or
using `-=` on any other member of an extern object, is a compile-time error.

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

**See also** §8.7.1 — `extend class`; §8.7.5 — `replace` matching rules; §12.11 — `extend` for arrays.

## 11.11 `extern object`

**Syntax**

```syntax
extern object ⟨name⟩ ;
extern object ⟨name⟩ {
    ⟨type⟩ ⟨method⟩ ( [ ⟨parameter⟩ , … ] ) ;
    emitter ⟨type⟩ ⟨method⟩ ( [ ⟨parameter⟩ , … ] ) { ⟨i6-template⟩ }
    ⟨type⟩ ⟨member⟩ ;
}
```

**Description**

An object defined in I6 rather than Beguile is declared `extern`. The bare form records only the name.
The body form also declares the types of the object's members, so calls and member reads against it
type-check. The object is a referenceable file-scope name like any global object, but no object is
defined for it in the story file.

Rules for the body:

- Methods are bodyless signatures; the implementation is in I6. A brace body on a non-emitter method
  is a compile-time error. `emitter` members with a body are permitted.
- Members are typed and carry no initializer; an initializer is a compile-time error.
- Default parameter values are honored for arity and overload resolution at call sites, as for
  ordinary methods (§6.3).

**Example**

```bgl
extern object playerCommands {
    void pushCommand(string cmd, bool isMeta = false, bool isSilent = false);
    object interrupt;
}

playerCommands.pushCommand("say hello");
```

**See also** §15.4.4 — extern objects at the I6 boundary; §8.2.2 — `extern class`.

---

# 12 Arrays

## 12.1 Overview

`array<T>` is a typed word array with a capacity fixed at declaration and a run-time tracked length.
The element type `T` is mandatory; bare `array` is not a type. `T` may be any base type (`int`,
`bool`, `string`, `object`, `char`, `dictionaryWord`), any user-defined class, or another array type
(§12.9). `rawArray<T>` (§12.8) is the untracked form used at the I6 boundary.

The `<array>` extension is loaded by the runtime core, so every array operation is available
without an explicit `#include <array>`. Subscripting, `size()`, `length()` and `for … in` are built
in; `setLength()`, `clear()`, value-semantic assignment and the remaining methods (`append`,
`indexOf`, `sort`, …) are provided by `<array>` and are cataloged in §22.4.

## 12.2 Declaring Arrays

**Syntax**

```syntax
array<⟨type⟩> ⟨name⟩[⟨n⟩] ;
array<⟨type⟩> ⟨name⟩ = { ⟨value⟩ , … } ;
array<⟨type⟩> ⟨name⟩ ;
```

`<`, `>`, `[` and `]` are literal. The first form is a sized array: capacity `⟨n⟩`, zero-initialized,
length 0. `⟨n⟩` is any compile-time integer: an integer literal, a `#define`d symbol whose value is
an integer (§14.2.1), or an integer `#beguilerSettings` property (§17.7). The second is an initialized array: capacity and length equal to the number of values. The
third is declared without capacity.

**Description**

Arrays may be declared at file scope, as locals inside a function body (§12.6), and as class or object
members (§12.7). A global array has no element-count limit. Element type checking is enforced at every
subscript site and in initializers: reading an element yields a `T`, writing requires a value
compatible with `T`, and a cross-type assignment (a `string` into an `array<int>`) is a compile-time
error.

**Example**

```bgl
array<int> scores[5];
array<int> primes = {2, 3, 5, 7, 11};
array<Room> visited;
```

## 12.3 Subscripts, Size and Length

**Syntax**

```syntax
⟨array⟩[⟨index⟩]
⟨array⟩[⟨index⟩] = ⟨value⟩
⟨array⟩ . size ( )
⟨array⟩ . length ( )
⟨array⟩ . setLength ( ⟨n⟩ )
⟨array⟩ . clear ( )
```

`[` and `]` are literal.

**Description**

Subscripts are zero-based. `size()` returns the number of elements allocated for the array, whether or
not those slots hold meaningful values.

An array also carries an explicit *length*, the count of in-use entries. Length is set at allocation
(N for an initializer list, 0 for a sized array) and changes only through explicit operations:
`setLength()`, `clear()`, and the mutators of `<array>` (§22.4). A slot write (`arr[i] = v`) does not
change length; the array behaves as a buffer with a cursor. `setLength(n)` is range-checked to the
signed word range of the target; `clear()` zeroes every slot up to `size()` and resets length to 0.

Every traversal in `<array>` (`indexOf()`, `contains()`, `removeValue()`, `sort()`, `first()`,
`last()`, …) walks the in-use range only; slots beyond `length()` are not searched, sorted or matched.
`clear()` is the one capacity-wide operation. A sized array filled only by slot writes therefore has
`length() == 0` and reads as empty; use `+=`, `insert()` or `setLength()` to make the slots live.

On an `array<char>` (§12.4), `size()` and `length()` both read the buffer's length word, which starts
at the declared capacity. On a `rawArray<T>` (§12.8) `size()`, `length()` and `for … in` are
compile-time errors, except that a raw member array reports its property length from both. `isTracked()`
(§22.4) tells a tracked array from an untracked one.

**Example**

```bgl
array<int> scores[5];

int x = scores[2];
scores[0] = 99;
int n = scores.size();          // → 5
int used = scores.length();     // → 0: a slot write does not change length
scores.setLength(1);            // → length() is now 1
scores.clear();                 // every slot 0, length 0
```

**Notes**

> **[Z-machine]** `setLength(n)` accepts 0..32767.

> **[Glulx]** `setLength(n)` accepts 0..2^31-1.

## 12.4 Byte Arrays — `array<char>`

**Syntax**

```syntax
array<char> ⟨name⟩[⟨n⟩] ;
array<char> ⟨name⟩ = { ⟨value⟩ , … } ;
array<char> ⟨name⟩ = "⟨text⟩" ;
```

`<`, `>`, `[` and `]` are literal.

**Description**

`array<char>` is a byte array: elements are stored and accessed as bytes. All other element types are
word arrays. A byte array holds character or small-integer data; its initializer and element writes
accept both character literals and integers (`{'H', 'i'}`, `{5, 10, 15}`, `bytes[i] = 99`). An
integer literal outside 0..255 is a compile-time error; a non-literal `int` that exceeds a byte wraps
to its low byte at run time. A string initializer fills the array with the characters of the text.

Reading an element yields a `char`, which widens to `int` freely, so `int a = bytes[i]`,
`bytes[i] + bytes[j]` and `bytes[i] > threshold` all work without casts. A `char` result is
byte-wide; accumulate into an `int`. Printing a `char` prints a character; to print its numeric value,
read it into an `int` first.

**Example**

```bgl
array<char> bytes = {'H', 'i', 5, 10};
array<char> word  = "hello";

bytes[2] = 99;                  // an integer into a byte slot
int big = 300;
bytes[3] = big;                 // → 44: wraps to the low byte at run time
int total = bytes[2] + bytes[3];
print(word[0]);                 // → h
int code = word[0];
print(code);                    // → 104
```

## 12.5 Assignment and Copy Semantics

**Syntax**

```syntax
⟨destination⟩ = ⟨source⟩ ;
```

**Description**

Assigning one array to another (`dst = src`) copies the elements; the two arrays are independent
afterwards. This is the mechanism that captures an ephemeral array
result (§12.6). Copying into an element of an owning element type goes through that type's
`static operator =` (§12.10).

**Example**

```bgl
array<int> src = {1, 2, 3};
array<int> dst[3];

dst = src;                      // copies the elements
dst[0] = 99;                    // src[0] is still 1
```

## 12.6 Local Arrays and Lifetime

**Description**

A local array (declared inside a function body, sized or initialized) is allocated per call at
function entry and freed at function exit, so each call, including a recursive one, has its own
storage (§18.10). File-scope arrays live in permanent storage and may be returned freely.

Because a local array's storage is reclaimed on return, a returned local array is an *ephemeral*
reference, not an owned value, exactly like an ephemeral string (§22.3): the storage it names has been
freed by the time the caller sees it. To keep the result, assign it to a typed local, which copies it
(§12.5). If the reference is consumed in an expression, or passed straight into another call, without
first being assigned, the behavior is undefined. The same applies to results of `<array>` methods and
chains on a local array (§22.4, §22.5).

**Example**

```bgl
array<int> build() {
    array<int> tmp[3];
    tmp[0] = 1; tmp[1] = 2; tmp[2] = 3;
    return tmp;                 // ephemeral
}

array<int> keep = build();      // copies into stable storage
int n = keep[0];                // safe
```

## 12.7 Member Arrays

**Syntax**

```syntax
array<⟨type⟩> ⟨name⟩[⟨n⟩] ;
array<⟨type⟩> ⟨name⟩ = { ⟨value⟩ , … } ;
ref array<⟨type⟩> ⟨name⟩ ;
```

`<`, `>`, `[` and `]` are literal. These forms appear in a class or object body.

**Description**

An `array<T>` declared as a class or object member (§8.3.1, §11.8) has the same semantics as any other
array: `length()`, `append()`, `pop()` and the rest behave identically. Storage is per instance: every
instance of a class has its own copy of a member array, with the declared capacity and initializer.

| Declared capacity | Storage |
|---|---|
| fits in one property | inline in the object's property, with the length in a trailing slot |
| too large for one property | separate storage owned by that instance; the property refers to it |

Two member kinds always keep the bare I6 layout and are never moved to separate storage: a
`rawArray<T>` member (§12.8.3), where exceeding the property limit is an error, and a member bound to
an `additive` property, which must be declared `rawArray<T>` (§11.7.2).

A member may also be declared `ref`, in which case it holds a reference to an array owned elsewhere,
is bound with `:=` (§3.7), and owns no storage of its own.

**Example**

```bgl
class Inventory : object {
    array<object> held[8];                  // per-instance storage
}

array<int> shared[8];
object w { ref array<int> log; }

w.log := shared;      // bind
w.log += 5;           // appends to shared
```

**Notes**

> **[Z-machine]** A property holds at most 32 words, counting the length slot, so a member array
> larger than that uses separate storage. Byte-array members (`array<char>`) are not subject to the
> property limit.

> **[Glulx]** Properties have no practical size limit; member arrays are always stored inline.

## 12.8 `rawArray<T>`

### 12.8.1 Raw Views

**Syntax**

```syntax
rawArray<⟨type⟩> ⟨name⟩
```

`<` and `>` are literal. The form appears as a parameter, an `extern` declaration, or a member.

**Description**

`rawArray<T>` is a typed view over a bare I6 word array: no length header and no tracking. It is
declarable at file scope, as an `extern`, as a parameter type, and as a class or object member. Its
purpose is interoperability: an I6 buffer handed to Beguile (the `results` array of a `parse_error`
entry point, a library table, an array declared in an `#i6` island, §15.2) has no count word, and
receiving it as a `rawArray<T>` parameter allows ordinary subscript syntax on it.

| | `array<T>` | `rawArray<T>` parameter |
|---|---|---|
| Layout | count word, then elements | elements only |
| `size()` / `length()` | available | unavailable; the length is passed explicitly |
| Length tracking | yes | none |

Because a `rawArray<T>` carries no length, `for … in` over a `rawArray<T>` parameter is a compile-time
error; iteration uses an indexed loop bounded by a length supplied separately. Elements are
type-checked at every subscript and may be cast like any other value.

**Example**

```bgl
bool ext_parsererror(int etype, rawArray<var> results) {      // NOTHING_PE, PutOn, Insert: library names
    if (etype == NOTHING_PE && ((verb)results[0] == PutOn || (verb)results[0] == Insert))
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
rawArray<⟨type⟩> ⟨name⟩ = { ⟨value⟩ , … } ;
```

`<` and `>` are literal.

**Description**

A file-scope `rawArray<T>` declared with an initializer is an *untracked* `array<T>`: it has the
count-word-then-elements layout of `array<T>`, and its count word holds the true element count, but it
carries no length-tracking trailer even though `<array>` is loaded. This is the form to use
when a bare I6 array API reads the array by its count word (for example the single-array form of
orLibrary's `util.orArray`, §15.10); a tracked `array<T>` would over-count there.

A file-scope `rawArray<T>` literal and a `rawArray<T>` parameter are not interchangeable: the literal
is count-prefixed and the parameter is elements-only. The type system keeps them apart; a literal has
the `array` element-covariant type and is passed where an `array<var>` is expected.

**Example**

```bgl
array<string>    trk = { "a", "b", "c" };   // tracked
rawArray<string> raw = { "a", "b", "c" };   // untracked; count word = 3
```

### 12.8.3 Member `rawArray<T>`

**Syntax**

```syntax
rawArray<⟨type⟩> ⟨name⟩ = { ⟨value⟩ , … } ;
```

`<` and `>` are literal. The form appears in a class or object body.

**Description**

A `rawArray<T>` member is a bare property array. It is required for members that contribute to an
`additive` property, and its `size()`, `length()` and permitted operations on such members are
specified in §11.7.2.

**Example**

```bgl
class Room { rawArray<dictionaryWord> name = {.box, .crate}; }
```

## 12.9 Arrays of Arrays

**Syntax**

```syntax
array<array<⟨type⟩>> ⟨name⟩ = { { ⟨value⟩ , … } , … } ;
⟨name⟩[⟨i⟩]
⟨name⟩[⟨i⟩][⟨j⟩]
```

`<`, `>`, `[` and `]` are literal. `⟨name⟩[⟨i⟩]` is an `array<⟨type⟩>`; `⟨name⟩[⟨i⟩][⟨j⟩]` is an
element of `⟨type⟩`.

**Description**

The element type of an array may itself be an array type, to any depth. A nested initializer supplies
one braced list per inner array, and the inner arrays may differ in length. `name[i]` is an
`array<T>` and supports `length()` and the array surface; `name[i][j]` reads or writes an element,
and `name[i][j].member = v` writes through an object element. `for (array<T> row in name)` iterates
the outer array with `row` typed `array<T>`. A nested array may be declared as a local. A wrong
element type or a wrong nesting depth in an initializer is a compile-time error.

**Example**

```bgl
array<array<int>> grid = { {1,2,3}, {4,5} };

int v = grid[0][1];                    // 2
int rows = grid.length();              // 2
int cols = grid[1].length();           // 2
grid[0][1] = 99;
int total = 0;
for (array<int> row in grid) {
    for (int i = 0; i < row.length(); i++) { total += row[i]; }
}
```

## 12.10 Element Type Requirements

**Description**

`array<T>` cannot see the element type at run time. It asks `T` for four operations, each supplied as
an operator reference (§7.3.1), and every one is optional. A type that publishes nothing gets plain
word semantics and pays nothing.

| `T` publishes | Used by | Absent |
|---|---|---|
| `operator ==` (static or instance) | `indexOf`, `find`, `contains`, `removeValue`, `-=` | word comparison (identity) |
| `operator <=>` (static or instance) | `sort()` | signed word ordering, and a compile-time warning for a class (ordering by object address) |
| `static operator =` | `[i] =`, `append`, `prepend`, `insert` | raw word store |
| `static deinit(T)` | `remove`, `removeValue`, `-=`, `clear`, local scope exit | nothing is released |

For `int`, `char`, `bool` and `object` the defaults are correct. The default is silently wrong for a
type whose value semantics differ from its word: a content-comparing type without `operator ==`
matches on address (`string` and `stringObj` publish one for this reason); `float` is sign-magnitude,
so it publishes `operator <=>` to sort negatives correctly; a type that owns storage without
`static operator =` receives a raw word store and the slot holds a value the array does not own; a
type that owns storage without `static deinit` leaks every dropped element, and a local array of it
leaks all of them at scope exit.

**Static versus instance.** `==` and `<=>` may be either form; the instance form is sent to the left
operand. A bare-word element type (`string`, `float`) has no object to receive a message and must
publish the static form; only an object-backed element type may use the instance form. `operator =`
must be `static`: on a first write the slot holds 0, so there is no receiver. An instance
`operator =` is ignored and the store is a word write. `deinit` has both forms and they do different
jobs: `emitter deinit()` releases a receiver at scope exit, `static deinit(T v)` releases a slot the
container has no receiver for. A type that owns storage generally wants both (§8.5).

**Assignment copies.** Every store into an owning element type goes through `static operator =`,
which copies the incoming value into the buffer the slot owns. Passing an already-allocated value
allocates a second time; the original is not released.

**Example**

```bgl
array<stringObj> slots[4];

slots += "alpha";                    // the slot allocates and owns
slots[0] = "replaced";               // reuses slot 0's buffer

stringObj tmp = "beta";              // allocated on declaration (§22.3)
slots += tmp;                        // copies into a new slot allocation; tmp keeps its own buffer,
                                     // released at scope exit, not by the array
```

**See also** §9.6 — `static` operators; §8.5 — the value form of `deinit`; §22.4 — the `<array>` methods that.
consult these operators.

## 12.11 `extend` for Arrays

**Syntax**

```syntax
extend ⟨array⟩ {
    inject ⟨element⟩ [ after ⟨ref⟩ | before ⟨ref⟩ | first | last ] ;
    remove ⟨ref⟩ ;
    move   ⟨ref⟩ [ after ⟨ref⟩ | before ⟨ref⟩ | first | last ] ;
}
```

`⟨ref⟩` is an element name or a zero-based index written `[⟨n⟩]`, where `[` and `]` are literal.

**Description**

`extend arrayName { … }` edits the initializer of a previously declared array at compile time, so a
later file (or a later point in the same file) may add to, remove from or reorder the array without
touching the original declaration. It is build-time only; nothing runs at startup. Run-time mutation
uses the array's methods (§22.4). The body holds *array extension statements*: `inject`, `remove`
and `move`.

- `inject element [position]` splices an element in. The element is a named object, a literal, or an
  inline object `Type{ … }` (§11.3); when the element type is an object-backed class the type may be
  omitted (`inject { … } last;`). With no position clause the element is appended.
- `remove X` removes an element.
- `move X [position]` repositions an existing element (remove, then inject at the new position).

`after X` and `before X` place the element immediately next to an existing element; `first` and
`last` name the two ends, and no clause means `last`. A reference `X` or `Y` is either an element
name (the identifier of the object an element refers to) or a zero-based index `[N]`
(`after [0]`, `remove [2]`).

The statements apply in source order, so a later `move` or `remove` sees the effect of earlier ones, and
`[N]` indices are relative to the array's state at that point. An unknown name or an out-of-range
`[N]` is a compile-time error.

**Example**

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

**See also** §11.10 — `extend` for objects; §13.5 — `extend` for verb grammar; §8.7.1 — `extend class`.

---

# 13 Dictionary Words, Verbs and Grammar

## 13.1 Dictionary Words

**Syntax**

```syntax
.⟨word⟩
..⟨word⟩
dictionaryWord ⟨name⟩ = .⟨word⟩ ;
```

**Description**

`dictionaryWord` is the type of a dictionary entry, a token the parser matches against player input.
A singular word is written with a leading `.` and a plural word with a leading `..`; both forms have
type `dictionaryWord`. The token form, including internal hyphens and apostrophes, is specified in
§1.6.7.

Dictionary words are compared with `==` and `!=`, and are the trigger and literal tokens of grammar
patterns (§13.4.2). `print()` on a dictionary word prints the word's text; the `dictionaryWord`
runtime type and its printing behavior are specified in §21.5.3.

**Example**

```bgl
dictionaryWord w = .lamp;
if (w == .lamp) print(w);      // → lamp
```

**See also** §1.6.7, §21.5.3.

## 13.2 Verb Declarations

**Syntax**

```syntax
verb ⟨name⟩ {
    grammar = { { ⟨pattern⟩ } , … } ;
    void handler() { … }
    [ meta = true ; ]
    [ priority = ⟨n⟩ ; ]
    [ ⟨member⟩ … ]
}
```

**Description**

`verb` is an alias class for `object` (`alias class verb for object { … }`, declared by the core BLR,
§21.5.4) whose members are `grammarRuleList grammar`, `bool meta`, `int priority`, `handler()` and
`perform()`. A `verb` declaration creates a named object that is an instance of that
class. The body uses ordinary object member syntax (§11.4): the `grammar` member's type is inferred
from the class, so `grammar = { … }` needs no type, and further members and methods may be declared
as on any object.

A verb name follows the same resolution rules as any identifier: a local variable with the same name
shadows the verb (§3.10).

**Example**

```bgl
verb Examine {
    grammar = {
        {.examine, noun},
        {.x, noun},
    };
    void handler() {
        print("You examine it closely.");
    }
}
```

**Notes**

When the first token inside `grammar = { … }` is a dictionary-word literal, the
outer braces are read as the braces of a single grammar line, so the inner braces may be omitted:
`grammar = {.whistle, noun};` is equivalent to
`grammar = { {.whistle, noun} };`. `|`-alternation is allowed in this form
(`grammar = {.hum|.murmur, noun};`). It applies only when one line is being declared; several lines
use the full form with one pair of braces per line. The single-line form is recognized wherever a
grammar line list is accepted: verb declarations, `extend` blocks (`grammar += { … }`,
`grammar -= { … }`, `replace grammar = { … }`), grammar objects, and extern verb bodies.

**See also** §11.2, §21.5.4.

### 13.2.1 `handler()`

**Syntax**

```syntax
void handler() { … }
```

**Description**

`handler()` is the verb's action body: it runs when the player enters a command matching the verb's
grammar. The `verb` class declares it as a `default` emitter method (§8.7.3), so a verb object overrides it
with an ordinary `void handler()` and needs no `replace`. A non-extern verb must define `handler()`; omitting it is a
compile-time error. Extern
verbs (§13.2.3) are exempt.

**Example**

```bgl
verb Jump {
    grammar = { {.jump} };
    void handler() { print("You jump on the spot."); }
}
```

### 13.2.2 `perform()`

**Syntax**

```syntax
⟨verb⟩.perform() ;
⟨verb⟩.perform( ⟨noun⟩ ) ;
⟨verb⟩.perform( ⟨noun⟩ , ⟨second⟩ ) ;
```

**Description**

`perform()` runs the verb's action from code. The active IF library's action runner is used: it saves
and restores the current actor, action, noun and second, and runs the surrounding before/after rules.
`perform()` works on any verb, native or `extern`, whether or not it defines a `handler()`. To run an
action and then return true from the enclosing function, follow the call with an explicit `rtrue;`.

**Example**

```bgl
Enter.perform(door);
Take.perform(coin, pouch);
Take.perform(coin); rtrue;
```

### 13.2.3 Extern Verbs and Claimed Words

**Syntax**

```syntax
extern verb ⟨name⟩ ;
extern verb ⟨name⟩ { .⟨word⟩ [ | .⟨word⟩ ] … }
extern verb ⟨name⟩ { grammar = { { .⟨word⟩ [ | .⟨word⟩ ] … } , … } ; }
```

The `|` in these forms is the literal alternation token (§13.4.3), not notation.

**Description**

A verb whose behavior is defined by the I6 library is declared `extern verb`. The declaration
registers the name for `switch (action)` comparisons, grammar lines, `perform()` and method calls. An
extern verb needs no `handler()`.

An I6 verb may claim several trigger words (`inventory`, `inv`, `i`). Declaring them in the extern
verb's body makes the claims visible, so that a grammar line elsewhere that uses one of those words
extends the library verb rather than defining a new one. The body uses the same `grammar` syntax as a
native verb, and these rules apply:

- Several words are listed with `|`-alternation (§13.4.3). Splitting the words over several lines or
  listing them all in one line yields the same claimed-word set.
- Pattern tokens after the trigger word are ignored, and writing one is a warning. No grammar is
  defined for an extern verb from Beguile; only the words in first position contribute.
- A bare `extern verb Name;` claims a single word, the lowercased verb name. The body form is needed
  only when the verb claims additional words or its primary word differs from its name.
- The first word of the first grammar line is the verb's *primary trigger*, the word used as the
  target when the verb is extended (§13.5). All listed words are equally claimed for collision
  detection.

Extern verbs cannot be marked `meta` and cannot carry a `priority`.

When an extern verb's body begins with a dictionary-word literal, the body is read
as the trigger-word section of a single grammar line: `extern verb Inv { .inventory|.inv|.i }` is
equivalent to `extern verb Inv { grammar = { {.inventory|.inv|.i} }; }`. This form accepts
trigger words only; a body with several lines or other members uses the full form.

**Example**

```bgl
extern verb Take;
extern verb Inv  { .inventory|.inv|.i }
extern verb Quit { .q|.quit|.die }
```

**See also** §15.4.2, §23.3.6.

### 13.2.4 Meta Verbs

**Syntax**

```syntax
meta = true ;
```

**Description**

Setting the `verb` member `meta` to `true` marks the verb as out-of-world: it runs without advancing
the turn and without triggering daemons or timers. `meta` is recognized only on `verb` instances and
is not a run-time property. Extern verbs cannot be marked meta; the I6 declaration already carries
the marking or does not.

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
priority = ⟨n⟩ ;
extend ⟨verb⟩ { priority = ⟨n⟩ ; grammar += { … } ; }
grammarRule ⟨name⟩ = { ⟨verb⟩ , { ⟨pattern⟩ } , ⟨n⟩ } ;
```

**Description**

Priority orders grammar lines that come from several sources and target the same trigger word. A
lower number is tried earlier by the parser. The `verb` member `priority` defaults to `10`; it is
recognized only on `verb` instances and is not a run-time property.

A verb's *anchor* is the priority declared in its own `verb` body, or `10` when none is declared
(an extern verb always has the default anchor). Every other contribution to that verb's trigger
words sorts relative to the anchor: a contribution with a lower priority is tried before the verb's
own lines, a higher one after. The verb that a `synonyms` list attaches to (§13.5.4) is not an anchor
in this sense.

| Source | Syntax | Scope | Default |
|---|---|---|---|
| Verb body (the anchor) | `priority = N;` inside `verb V { … }` | The verb's own lines; stored on the verb | `10` |
| `extend` block | `priority = N;` inside `extend V { … }` | Every line added by that block's `grammar +=`; not stored on the verb, so several `extend` blocks at different priorities coexist on one verb | `10` |
| Grammar-object rule | Third positional element of a `grammarRule` initializer, `{V, {pattern}, N}` | That one rule, sorted against its target verb's anchor; a grammar object has no block-level default, so the value is repeated per rule | `10` |
| Inferred-verb line | none | A line written `{pattern}` takes the owning verb's anchor, or the enclosing `extend` block's priority | — |

Combining `priority = N;` with `replace grammar = { … }` in the same `extend` block is a compile-time
error (§13.5.3). An extern verb cannot carry a `priority` in its declaration (§13.2.3).

**Ordering.** For each trigger word of a verb, lines with `priority < anchor` are tried before the
verb's own lines; the verb's own lines come next; lines with `priority ≥ anchor` from other sources
come last. Per-rule and block-local priorities participate in the same ordering.

**Example**

```bgl
verb Take {
    priority = 5;
    grammar = { {.take, noun}, {.grab, noun} };
    void handler() { … }
}

extend Look {
    priority = 5;                              // before Look's own lines
    grammar += { {.peek, noun} };
}

extend Look {
    priority = 12;                             // after Look's own lines
    grammar += { {.look, .carefully, noun} };
}

grammar additions {
    grammarRule r1 = {Take, {.nab, noun}};             // 10
    grammarRule r2 = {Drop, {.toss, held}, 5};         // 5
}
```

**See also** §13.4.5, §13.5.

## 13.3 Action Comparisons

**Syntax**

```syntax
action == ⟨verb⟩
action != ⟨verb⟩
switch (action) { case ⟨verb⟩ : … }
```

**Description**

The library variable `action` has type `verb`. It is compared against a verb name with `==` and `!=`,
and a `switch` on `action` takes verb names as case values (§5.12).

**Example**

```bgl
if (action == Take) { … }

switch (action) {
    case Take: print("Taken.");
    case Drop: print("Dropped.");
}
```

**See also** §5.12, §23.3.4.

## 13.4 Grammar

Grammar lines define what the player may type and which verb they trigger. A line is declared either
on a verb (§13.4.5) or in a grammar object, and both forms have the same effect.

### 13.4.1 Grammar Types

**Description**

| Type | Purpose |
|---|---|
| `grammarToken` | An extern enum declared by the IF library binding (§23.3.6); its values are the parser tokens `noun`, `held`, `creature`, …. Both the bare value (`held`) and the qualified form (`grammarToken.held`) are valid in pattern position. `noun(Routine)` and `scope(Routine)` are its parameterized forms. |
| `patternElement` | One element of a pattern: a dictionary word or a grammar token. A pattern is an `array<patternElement>`, written `{.examine, noun}`. |
| `grammarRule` | One verb-targeted pattern, with an optional priority: `{Examine, {.examine, noun}}` or `{Examine, {.examine, noun}, 5}`. |
| `grammarRuleList` | A list of grammar rules; the type of the `grammar` member on `verb` and of a grammar object. |

A `grammarRule` has two initializer forms:

- **Explicit verb**: `{Verb, {pattern}[, priority]}`. Valid in any context.
- **Inferred verb**: `{pattern}`. The verb is the owning object, which should be a `verb` or a
  subclass of `verb`; otherwise a warning is issued. The priority is the owning verb's anchor or the
  enclosing `extend` block's (§13.2.5).

A `grammarRule` member takes exactly one `{verb, {pattern}}` pair; an `array<grammarRule>` member
takes a list of them. Any other shape is a compile-time error.

**See also** §21.5.5.

### 13.4.2 Pattern Tokens

**Description**

Each element of a pattern is one of the following.

| Token | Matches |
|---|---|
| `.word` | the player typing that word |
| `..words` | the plural form of the word |
| `.w1 \| .w2` | any one of the listed words; may be parenthesized |
| `noun` | any in-scope object |
| `held` | a held object |
| `creature` | a creature or actor |
| `topic` | a topic phrase |
| `multi` | one or more in-scope objects |
| `multiheld` | one or more held objects |
| `multiexcept` | one or more in-scope objects, excluding one already matched (used after a preposition: `multiexcept, .in, noun`) |
| `multiinside` | one or more objects inside a specific container (used after a preposition: `multiinside, .from, noun`) |
| `number` | a number typed by the player, range-checked |
| `anynumber` | any number, no range check |
| `special` | a number or a dictionary word |
| *attributeName* | an object that has that attribute (`container`, `animate`) |
| *RoutineName* | a general parsing routine: a global `bool` function the parser calls to consume input words and report a match |
| `noun(Routine)` | a noun filter: the parser matches nouns normally, then calls the global `bool` routine with each candidate object, which accepts or rejects it |
| `scope(Routine)` | a scope setter: the global `bool` routine decides which objects are in scope for this line, using the library's scope routines (`PlaceInScope()`, `ScopeWithin()`) |

A bare identifier in a pattern must be declared as a `grammarToken`, an `attribute`, or a global
function; any other declaration, or an undeclared name, is a compile-time error. In pattern position
a bare `noun` resolves to the grammar token even when an `extern object noun` exists at file scope;
outside a pattern the global wins, and `grammarToken.noun` selects the token explicitly.

**Example**

```bgl
bool isEdible(object obj) { return obj.has(edible); }

verb Taste {
    grammar = { {.taste, noun(isEdible)} };
    void handler() { … }
}

bool parseColor(int context) { … }

verb Paint {
    grammar = { {.paint, noun, parseColor} };      // "paint <object> <color>"
    void handler() { … }
}

verb Chat {
    grammar = { {.chat, animate, .about, topic} };
    void handler() { … }
}
```

**See also** §11.6, §23.3.6.

### 13.4.3 Alternation and Multi-trigger Lines

**Description**

Dictionary words separated by `|` match any one of them, and the group may be parenthesized.
Alternation is allowed in any position, including the first.

When alternation appears in the first position, the line declares one pattern that fires on any of
the listed trigger words. The line behaves as one line per word: each trigger word is treated
independently, so `grammar -=` (§13.5.2) can remove a single word's line, and each word is routed on
its own. A new word defines that word's grammar; a word already claimed by another verb (a library
verb through its claimed words, or an earlier Beguile verb) adds the line to that word's grammar. The
same applies to multi-trigger lines inside `extend V { grammar += { … } }`.

**Example**

```bgl
verb Stow {
    grammar = {
        {.stow, held, .on | .onto | .upon, noun},
        {.stow, held, (.in | .into | .inside), noun},
    };
    void handler() { … }
}

verb TypeNum {                                     // three trigger words: one line per word
    grammar = { {.type | .enter | .put, number, .into | .in | .on | .onto, noun} };
    void handler() { print("You can't type anything there."); }
}
```

### 13.4.4 Line Modifiers: `reverse`, `withI6Synonyms`

**Syntax**

```syntax
{ ⟨pattern⟩ [ , reverse ] [ , withI6Synonyms ] }
```

**Description**

Two pseudo-tokens may end a grammar line, in this order. Neither is matched against input.
`withI6Synonyms`, when present, must be last. The dotted forms `.reverse` and `.withI6Synonyms` are
ordinary dictionary words and are unaffected.

- `reverse` swaps `noun` and `second` when the action receives its parsed arguments:
  `{.give, creature, held, reverse}`.
- `withI6Synonyms` widens the line's effect on a library verb. A library verb may group several
  synonym words under one grammar table; when a line's trigger word belongs to such a group, the line
  affects only that word and its synonyms are untouched. With `withI6Synonyms` the line applies to
  every word in the group. On a native word, or a library word with no synonyms, the modifier has no
  effect.

In I6 terms: a line whose trigger word belongs to a library verb's synonym group is emitted as
`Extend only 'w'`, which splits `w` off the group, keeps the library grammar it inherits and adds the
line to `w` alone; with `withI6Synonyms` the line is emitted as `Extend 'w'`, which I6 applies to
every word of the group. A library word with no synonyms is emitted as a plain `Extend`, and a new
word as a fresh `Verb`. Background, non-normative: [Verbs and Grammar](../Verbs-Grammar.md).

**Example**

```bgl
verb Keypad {
    grammar = { {.enter, number, .into, noun} };    // "enter 5 into keypad"; "cross" unaffected
    void handler() { … }
}

extend Take { grammar += { {.take, .all, .from, noun, withI6Synonyms} }; }   // take/get/carry/…
```

### 13.4.5 Grammar on Verbs and Grammar Objects

**Syntax**

```syntax
verb ⟨name⟩ { grammar = { { ⟨pattern⟩ } , … } ; … }

grammar ⟨name⟩ {
    grammarRule ⟨rule⟩ = { ⟨verb⟩ , { ⟨pattern⟩ } [ , ⟨priority⟩ ] } ;
    array<grammarRule> ⟨rules⟩ = { { ⟨verb⟩ , { ⟨pattern⟩ } [ , ⟨priority⟩ ] } , … } ;
}
```

**Description**

The `grammar` member of a verb is a `grammarRuleList` whose lines use the inferred-verb form; the verb
is the owning object.

A `grammar` declaration declares an object of class `grammarRuleList`. A grammar object is
cross-cutting: one object may carry rules targeting many verbs, each rule pairing a pattern with an
explicit verb. Member types may be inferred, and an `array<grammarRule>` member holds several rules.
Per-rule priority is the optional third element (§13.2.5).

**Example**

```bgl
grammar customPatterns {
    rule1 = {PutOn, {.hang, held, .on, noun}};
    array<grammarRule> rules = {
        {PutOn,  {.put, held, .on, noun}},
        {Insert, {.put, held, .in, noun}, 5},
    };
}
```

**See also** §13.2.5, §21.5.5.

## 13.5 Extending Grammar

**Syntax**

```syntax
extend ⟨verb⟩ {
    [ priority = ⟨n⟩ ; ]
    grammar += { { ⟨pattern⟩ } , … } ;
    grammar -= { { .⟨word⟩ , ⟨pattern⟩ } , … } ;
    grammar -= { { .⟨word⟩ } , … } ;
    replace grammar = { { ⟨pattern⟩ } , … } ;
    synonyms = { .⟨word⟩ , … } ;
}
```

The two `grammar -=` forms are the line-level and word-level shapes of §13.5.2; entries of both shapes
may be mixed in one list.

**Description**

Grammar is added to or removed from an existing verb, including an `extern verb`, inside an
`extend V { … }` body (§11.10). An operator is required: bare `grammar = { … }` is valid only in the
original `verb` or `extern verb` declaration and is a compile-time error inside an `extend`. A bare
`priority = N;` in the block applies to the lines it appends (§13.2.5).

### 13.5.1 `grammar +=`

**Description**

`grammar += { … }` appends lines to the verb's grammar. Multi-trigger lines (§13.4.3) and line
modifiers (§13.4.4) are accepted.

**Example**

```bgl
extern verb PutOn;
extend PutOn {
    grammar += { {.hang, held, .on, noun} };
}
```

### 13.5.2 `grammar -=`

**Description**

`grammar -= { … }` removes grammar. The grain of the removal is set by how much of the line is named:

- **Line-level** — `{.w, pattern…}` removes the one line that matches exactly: same trigger word,
  same tokens, same `reverse` flag.
- **Word-level** — `{.w}` removes all of that word's grammar.

A `-=` entry must match a line exactly; a partial (prefix) pattern matches nothing:
`-= { {.give, noun} }` removes only the line whose pattern is exactly `noun`, never
`{.give, noun, .to, noun}`. An alternation in a `-=` entry (`.a|.b|.c`) is one removal per word, each
matched and warned separately. Removal is source-ordered: a `-=` sees only lines declared before it,
so a later `+=` of the same line is unaffected. A `-=` that matches nothing is a warning.

**Example**

```bgl
verb TypeNum { grammar = { {.type|.dial, .into, noun}, {.dial, .to, noun} }; void handler() { … } }
extend TypeNum { grammar -= { {.dial, .into, noun} }; }   // that one dial line
extend TypeNum { grammar -= { {.dial} }; }                // every dial line
```

**Extern verbs.** For a library verb Beguile knows the words it claims and the lines Beguile itself
added with `+=`, but the library's own patterns are opaque. A line-level `-=` therefore matches only
Beguile-added lines; naming a claimed word with a pattern that matches none of them is a warning. A
word-level `-=` on a claimed word *evicts* the word from the library verb: if a native verb also
declares that word, its lines take the word over; otherwise the word is disabled. The word must be
genuinely claimed by the library verb.

In I6 terms, eviction is emitted as `Extend only 'w' replace`: the one directive that reaches a
library verb's opaque grammar, since it names no patterns. It detaches `w` from the library verb and
discards the library's grammar for it; the lines a native verb declares for `w` are folded into that
directive and become the word's complete grammar, and with no such lines the word matches nothing.
Background, non-normative: [Verbs and Grammar](../Verbs-Grammar.md).

```bgl
extend Enter  { grammar -= { {.enter} }; }      // evict 'enter' from the library verb …
verb  Keypad  { grammar = { {.enter, number, .into, noun} }; void handler() { … } }   // … and reclaim it
extend Disturb { grammar -= { {.xyzzy} }; }     // no reclaimer: 'xyzzy' is disabled
```

**See also** §13.2.3, §13.4.3.

### 13.5.3 `replace grammar =`

**Description**

`replace grammar = { … }` discards all of the verb's grammar, every trigger word and every prior `+=`
and `-=`, whatever their source order, and the listed lines become its complete grammar. Trigger
words not named in the replacement are dropped. Only this verb is affected; a word that another verb
also uses keeps its grammar there. On an extern verb the replacement overrides the library's grammar
for the named words. Combining `replace grammar =` with `priority = N;` in the same block is a
compile-time error.

**Example**

```bgl
verb TypeNum { grammar = { {.type|.dial, .into, noun} }; void handler() { … } }
extend TypeNum {
    replace grammar = { {.dial, .to, noun} };     // 'type' is dropped
}

extend Take {
    replace grammar = { {.take, .firmly, noun} };  // overrides the library's 'take'
}
```

**See also** §13.2.5.

### 13.5.4 Synonyms

**Description**

`synonyms = { .word, … }` inside an `extend` makes the listed words true aliases of the extended verb:
they share the verb's grammar rather than copying it, so a later `extend` of the verb reaches every
synonym. By contrast, adding the words as separate trigger lines with `grammar +=` copies the pattern
set at that point, and later extensions do not reach them.

The alias is attached to the verb's primary trigger word (its first claimed word, or its name if it
declares none). `synonyms` works on both extern and native verbs. One-letter and plural word
conventions apply (§1.6.7). An empty `synonyms = { }` is a compile-time error.

There is no Beguile form for I6's `Extend 'w' only` directive; if needed, write it in an `#i6` island
(§15.2).

**Example**

```bgl
extend Take { synonyms = {.steal, .grab, .pilfer}; }
extend Take { grammar += { {.take, .quietly, noun} }; }   // also "steal quietly", "grab quietly"
```

**See also** §13.2.3, §15.2.

---

# 14 Directives

A directive is a compile-time instruction introduced by `#`. Directive names are case-insensitive, like
every Beguile identifier (§1.3). Unless an entry says otherwise, a directive is written at global scope
and is consumed by the compiler: nothing of it reaches the generated I6 except the raw I6 bodies of the
placement directives in §14.4 and the island directives in §14.5. An unrecognized directive is a
compile-time error.

The entries are grouped by purpose; Appendix B lists every directive alphabetically.

## 14.1 Source Organization

### 14.1.1 `#include <name>`

**Syntax**

```syntax
#include <⟨name⟩>
#include <⟨sub⟩/⟨name⟩>
#include <⟨a⟩/⟨b⟩/⟨name⟩>
```

The angle brackets are literal.

**Description**

Includes a Beguile source file from the Beguile Language Runtime library tree (`beguiLib`, located by
the `beguiLibPath` setting, §17.2). The tree is searched recursively for `name.bgl`; the `.bgl`
extension is supplied by the compiler and must not be written.

- `<name>` matches `name.bgl` anywhere in the tree, root or any subfolder.
- `<sub/name>` matches `name.bgl` only when its immediate parent folder is `sub`, at any depth.
- `<a/b/name>` requires the resolved path to end with `a/b/name.bgl`.

The search is deterministic: the root folder's files are checked first, then each subfolder in
alphabetical order, depth first, and the first match wins. Matching is case-insensitive
(`<String>` and `<string>` are the same include) regardless of the file system. A file that is not
found is a compile-time error.

A folder prefix is optional: `#include <⟨name⟩>` is equivalent to the
canonical `#include <⟨folder⟩/…/⟨name⟩>` naming the folder in which the search finds `name.bgl`,
so `<i6StandardLibrary>` and `<bindings/i6StandardLibrary>` are the same include.

**Example**

```bgl
#include <string>                  // beguiLib/string.bgl
#include <i6StandardLibrary>       // same as <bindings/i6StandardLibrary>
#include <bindings/punyInform>     // beguiLib/bindings/punyInform.bgl
```

**Notes**

The extensions that ship with the BLR and what each provides are listed in §22.

**See also** §18.5, §22.

### 14.1.2 `#include "path"`

**Syntax**

```syntax
#include "⟨path⟩"
```

**Description**

Includes a Beguile source file by path. The compiler searches the directory of the current source
file, then each `includePaths` directory (§17.3), trying *path*`.bgl` first and then *path* exactly as
written. Subdirectory paths are permitted and resolve relative to each directory searched. Either `/`
or `\` may be used as a separator. Matching is case-insensitive. A file that is not found is a
compile-time error.

A file may be included more than once; a file that must be processed only once guards itself with
`#once` (§14.1.6).

**Example**

```bgl
#include "myLibrary"
#include "utils/helpers"
```

**Notes**

Beguile has no equivalent of Inform 6's `>filename` prefix; the current file's directory is always
searched first.

**See also** §18.5.

### 14.1.3 `#include @"path"`

**Syntax**

```syntax
#include @"⟨path⟩"
#include ?@"⟨path⟩"
```

**Description**

As `#include "path"`, except that the path is a raw string (§1.6.4): no escape processing is applied,
so backslashes and escape-like sequences pass through verbatim. Path search and the file-not-found
error are those of §14.1.2. The optional and raw modifiers combine as `?@"path"`. An interpolated
string (`$"…"`) is not accepted as an include path.

**Example**

```bgl
#include @"vendor\legacy\helpers"
```

**See also** §1.6.4.

### 14.1.4 `#include ?"path"` and `#include ?<name>`

**Syntax**

```syntax
#include ?"⟨path⟩"
#include ?<⟨name⟩>
```

The angle brackets are literal.

**Description**

As the corresponding unmodified form, except that a file that is not found is silently skipped
instead of being reported as an error.

**Example**

```bgl
#include ?"optionalExtension"
#include ?<glulxImage>
```

### 14.1.5 `#includeI6`

**Syntax**

```syntax
#includeI6 "⟨name⟩"
#includeI6 ?"⟨name⟩"
#includeI6 @"⟨name⟩"
```

**Description**

Includes an Inform 6 source file in the generated program. The file is resolved like
`#include "path"` (§14.1.2): the current source file's directory, then each `includePaths` directory,
trying *name* as written and then *name*`.h`. Subdirectory paths are permitted. The I6 compiler
receives the resolved file. A file that is not found is a compile-time error.

`?"name"` silently skips a file that is not found. `@"name"` hands the string to the I6 compiler
verbatim: no search, no existence check and no separator rewriting are performed.

Every `includePaths` directory is also made available to the I6 compiler, so an included I6 file can
resolve its own internal includes.

**Example**

```bgl
#includeI6 "parser"
```

**Notes**

The I6 include is placed in the generated output at the directive's source position; a Beguile
declaration that depends on something the I6 file defines must follow it (§15.4, §18.7).

An `extern` declaration for a name the included file defines may precede or follow the include; the
exception is a class that uses an `extern attribute`, which must follow that attribute's declaration
(§18.7).

**See also** §15.5, §18.5.

### 14.1.6 `#once`

**Syntax**

```syntax
#once
```

**Description**

Placed at the top of a source file, marks the file so that any later `#include` of the same file (by
any path that resolves to the same location) is silently ignored. Without `#once` a file is
processed every time it is included.

Include nesting is limited to 255 levels; exceeding the limit, for example through circular includes
of files without `#once`, is a compile-time error.

**Example**

```bgl
#once
// rest of myLibrary.bgl …
```

**Notes**

Every file intended to be included as a library should begin with `#once`; the BLR extension files
all do.

**See also** §14.4 (per-file deduplication that does not depend on `#once`).

## 14.2 Symbols and Conditional Compilation

### 14.2.1 `#define`

**Syntax**

```syntax
#define ⟨name⟩
#define ⟨name⟩ ⟨value⟩
```

**Description**

Defines a compilation symbol. Without a value the symbol has the boolean value `true`. A symbol with
a value is also usable in a Beguile expression, where the name is replaced by its value at compile
time: a numeric value is an integer literal, any other value a string literal. Symbols are tested with
`#if` (§14.2.5).

`#define` is linear: it affects only the source that follows it, in the order files are processed. An
`#if` sees a symbol as defined only if a `#define` for it appears earlier and no intervening `#undef`
removed it. Defining a symbol that is already defined is a compile-time error; use `#redef`
(§14.2.2). Defining a symbol that was `#declare`d is a compile-time error (§14.2.3).

**Example**

```bgl
#define DEBUG          // DEBUG is true
#define MAX_SCORE 100

if(score >= MAX_SCORE) print("You win!");   // compiles as: if(score >= 100)
const int maxScore = MAX_SCORE;
```

### 14.2.2 `#redef` and `#undef`

**Syntax**

```syntax
#redef ⟨name⟩
#redef ⟨name⟩ ⟨value⟩
#undef ⟨name⟩
```

**Description**

`#redef` behaves exactly like `#define` except that an already-defined symbol is overwritten rather
than reported. `#undef` removes a symbol so that a later `#if` treats it as undefined. Both are
linear, like `#define`. Applying either to a `#declare`d symbol is a compile-time error.

**Example**

```bgl
#define LEVEL 1
#redef  LEVEL 2      // now 2
#undef  LEVEL        // now undefined
```

### 14.2.3 `#declare`

**Syntax**

```syntax
#declare ⟨name⟩
#declare ⟨name⟩ ⟨value⟩
```

**Description**

Defines a symbol like `#define`, with two differences:

- **Order-independent.** A declared symbol is visible to every `#if` in the program, including
  `#if`s in files processed before the file containing the `#declare`.
- **Immutable.** A declared symbol cannot be `#undef`d, `#redef`d or shadowed by a `#define`; each is
  a compile-time error, as is a second `#declare` of the same name with a different value.

In every other respect it matches `#define`: a bare `#declare NAME` is a boolean flag, a value is
usable inline as a literal, and the symbol is tested with the same `#if` expressions.

| | `#define` / `#redef` | `#declare` |
|---|---|---|
| Visibility | Linear: source after the directive | Global: every `#if`, before or after |
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

**Description**

The compiler defines these symbols before any source file is processed: `beguiler`,
`beguilerMajor`, `beguilerMinor`, `beguilerPatch` (the compiler version), and exactly one of
`TARGET_GLULX` or `TARGET_ZCODE` (the compilation target, from the `target` setting). They behave as
`#define`d symbols in `#if` expressions and as inline literals in Beguile expressions. Their values
and the resolution rule are given in Appendix F.

The two target symbols are flags with no value: they say which machine is being compiled for, and
nothing more. To distinguish `Z5` from `Z8`, read the setting instead — `#if #beguilerSettings.target
== "z8"` (§14.2.5, §17.7).

**See also** Appendix F, §17.3.

### 14.2.5 `#if`, `#elif`, `#else`, `#endif`

**Syntax**

```syntax
#if ⟨expression⟩
    …
[ #elif ⟨expression⟩
    … ] …
[ #else
    … ]
#endif
```

**Description**

Includes or excludes the enclosed source text according to a compile-time expression over the
currently defined symbols. Source in an excluded branch is skipped without being parsed. Conditional
blocks nest. They may enclose declarations as well as statements; an excluded declaration does not
exist.

The expression may contain: symbol names; integer literals; string literals;
`#beguilerSettings.⟨property⟩` references (§17.7); `true` and `false` (the values `1` and `0`); the
comparisons `==`, `!=`, `<`, `>`, `<=`, `>=`; `&&`, `||`, `!`; and parentheses.

A `#beguilerSettings.⟨property⟩` reference resolves to the property's value, the same value the
reference has in a Beguile expression (§14.7.2): a string property compares against a string
literal, an int property compares numerically, and a bool property tests on its own. String
comparison ignores case, as everywhere else in Beguile. The property must be written with no space
around the `.`; an undeclared property is a compile-time error, not a false condition.

A bare symbol name is true when the symbol is *defined*, whatever its value, so `#if V` is true even
when `V` was defined as `0` or `false`. In a comparison the name resolves to the symbol's *value*; an
undefined symbol compares as `0`. Thus after `#define V false`, `#if V` and `#if V == false` are both
true and `#if V == true` is false.

There is no `#ifdef` or `#ifndef`, in either the single- or double-hash form: `#if SYMBOL` and
`#if !SYMBOL` test definedness. `#if` tests *Beguile* symbols; to emit a conditional for the Inform 6
compiler to evaluate — over one of its own constants, say — write it in an `#i6` island, where a
single-hash directive is raw I6 and passes through (§7.4):

```bgl
#i6 { #Ifndef NO_SCORE; print "^"; #Endif; }
```

**Example**

```bgl
#define DEBUG
#define FEATURE_LEVEL 3

#if DEBUG && beguiler >= 1010
    print("debug mode on Beguile 1.1+");
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
    // Z8 only — the target symbols say which machine, the setting says which version
#endif
```

**Notes**

The `##if` / `##else` / `##endif` forms provide the same conditional logic inside emitter bodies and
are not valid in ordinary Beguile source; see §7.4.

**See also** §7.4.

## 14.3 Diagnostics and Control

### 14.3.1 `#message`

**Syntax**

```syntax
#message "⟨text⟩"
```

**Description**

Writes the string to the compiler's standard output during compilation. It does not affect the
generated output or the result of compilation.

**Example**

```bgl
#message "Loading custom library..."
```

**See also** §20.5.

### 14.3.2 `#warning`

**Syntax**

```syntax
#warning "⟨text⟩"
```

**Description**

Reports a warning with the given text, in the compiler's standard warning format with file name and
line number, and continues compilation.

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
#error "⟨text⟩"
```

**Description**

Halts compilation with an error carrying the given text, reported in the compiler's standard error
format with file name and line number.

**Example**

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

**Description**

Stops processing the current file immediately, as though its end had been reached. Any conditional
nesting (§14.2.5) still open in that file is discarded. Open code blocks are not closed, so `#exit`
is used only at the top level of a file.

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

The directives in this section carry a body of raw I6 text between braces. The body is not parsed as
Beguile; only the substitution in §14.4.5 is applied. All of them share two rules:

- **Deduplication.** Each source file contributes its blocks at most once, however many times the
  file is included. `#once` is not required for this.
- **Ordering.** Blocks from different files are placed in file-inclusion order, the order in which
  the compiler first encounters each file.

### 14.4.1 `#startup`

**Syntax**

```syntax
#startup {
    ⟨raw I6 statements⟩
}
```

**Description**

Registers I6 statements to run at program startup. The statements run inside the runtime's
`bglInit()` routine, before any global variable initializers, and before startup blocks from files
included later. The IF library bindings (§23.3.1) call `bglInit()`; a program built without a
binding must call it from its own starting routine.

**Example**

```bgl
#startup {
    _initializeStringBuffers();
}
```

**Notes**

Intended for library code that must initialize runtime infrastructure before any object is
constructed. Program code normally uses object initializers or the game's starting routine instead.

**See also** §18.8, §21.2, §23.3.1.

### 14.4.2 `#emitfirst`

**Syntax**

```syntax
#emitfirst {
    ⟨raw I6⟩
}
```

**Description**

Places the body at the beginning of the generated I6 program, before the runtime and before every
declaration, so that it precedes any I6 library included later. This is where an I6 directive that
must appear early belongs: a `Replace`, a banner constant, or conditional-compilation setup that an
I6 library reads while it is being included. The body may use `##beguilerSettings.<key>` (§14.4.5).

**Example**

```bgl
#emitfirst {
    Replace DrawStatusLine;
    Constant story    = ##beguilerSettings.title;
    Constant headline = ##beguilerSettings.headline;
}
```

**Notes**

The IF library bindings use this directive to declare the constants their I6 library requires
(§23.3.2).

**See also** §15.6, §18.6, §23.3.2.

### 14.4.3 `#emitlast`

**Syntax**

```syntax
#emitlast {
    ⟨raw I6⟩
}
```

**Description**

Places the body at the very end of the generated I6 program, after every declaration, grammar
directive and object definition. The body may use `##beguilerSettings.<key>` (§14.4.5).

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
#storedEmitFirst ⟨name⟩ {
    ⟨raw I6⟩
}
#storedEmitLast ⟨name⟩ {
    ⟨raw I6⟩
}
```

**Description**

Registers a named, deferred raw-I6 block. Unlike `#emitfirst` and `#emitlast`, a stored block is not
placed in the output by default: it is emitted, once, only when a built-in I6 template that needs it
is applied (§18.9). When emitted, the block takes the position of the corresponding non-stored form:
the top of the program for `#storedEmitFirst`, the end for `#storedEmitLast`. A program in which no
template needs the block pays nothing for it.

Several stored blocks may share one name, and one template may need several names. Registering a
name that is already registered replaces the earlier block (latest wins); `#once` on the declaring
file is the normal guard. The body may use `##beguilerSettings.<key>` (§14.4.5).

**Example**

```bgl
#storedEmitFirst scratchSupport {
    Array  scratchStack --> 33;
    Global scratchTop = 0;
    [ scratchPush v; … ];
}
```

**Notes**

Intended for BLR authors: helper routines that some runtime feature needs but most programs do not.

**See also** §18.9, Appendix G.

### 14.4.5 `##beguilerSettings.<key>` Substitution

**Syntax**

```syntax
##beguilerSettings.⟨key⟩
```

**Description**

Inside the raw I6 body of `#emitfirst`, `#emitlast`, `#storedEmitFirst` or `#storedEmitLast`, this
marker is replaced by the compile-time value of the named `#beguilerSettings` property. A string
property becomes an I6 quoted string (`"…"`); an integer property becomes a decimal literal. The
property names are those accepted by the `#beguilerSettings.prop` expression form (§14.7.2, §17.7).
The substitution is applied only in the bodies of `#emitfirst`, `#emitlast`, `#storedEmitFirst` and
`#storedEmitLast`; it is not applied in `#startup`, `#i6` or emitter bodies.

**Example**

```bgl
#emitfirst {
    Constant story = ##beguilerSettings.title;
}
```

**See also** §14.7.2, §17.7, Appendix G.

## 14.5 Islands

An island is a region of one language embedded in a stream of the other. This section gives the
syntax; the two compilation modes are in §15.1, the semantics of I6 islands in §15.2, of Beguile
islands in §15.3, and loose identifier resolution in §15.3.3.

### 14.5.1 `#i6`

Syntax: `#i6 ⟨raw I6 to end of line⟩` or `#i6 { ⟨raw I6⟩ }`. See §15.2.

### 14.5.2 `#bgl`, `#bglDecl`, `#bglStmt`

Syntax: `#bgl ⟨statement⟩ ;`, `#bgl { … }`, `#bglDecl { ⟨declarations⟩ }`, `#bglStmt { ⟨statements⟩ }`. See §15.3.

## 14.6 Namespace Import

### 14.6.1 `#using`

**Syntax**

```syntax
#using ⟨name⟩ [ .⟨name⟩ ] … [ ; ]
```

**Description**

Imports the members of a class or object into the current file's scope so they may be used
unqualified. Every rule of the directive — its file scope, the priority of imported names, what each
kind of target contributes and alias imports — is specified in §10.4.

**See also** §10.4.

## 14.7 Settings

### 14.7.1 `#beguilerSettings { … }`

Syntax: `#beguilerSettings { ⟨property⟩ = ⟨value⟩ ; … }`. See §17.1 and Appendix E.

### 14.7.2 `#beguilerSettings.prop`

Syntax: `#beguilerSettings.⟨property⟩`. See §17.7.

---

# 15 Inform 6 Interoperability

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

Injects raw I6 at the directive's source position in the generated program. The body is not parsed
or type-checked: the compiler tracks only braces, string literals (`"…"`) and word or character
literals (`'…'`), far enough to find the closing brace of the block form. The single-line form takes
everything to the end of the line.

Two substitution tokens are recognized in the body, the same two an emitter body uses to reach a
Beguile declaration: `$i6Name(⟨path⟩)` (§7.3.3) for the identifier Inform 6 knows a declaration by,
and `$i6Expr(⟨expression⟩)` (§7.3.4) for the I6 a Beguile expression emits. An island has no
receiver and no parameters, so no other `$` token means anything in one; inside a routine the
payload of `$i6Expr` resolves against that routine's scope and may name its locals. Everything else
passes through untouched, including I6's own `$` hexadecimal and `$$` binary literals — only the
exact `$i6Name(` and `$i6Expr(` forms are claimed.

Those two cover naming a declaration and inlining one expression. To run Beguile *statements* in an
island, use `#bgl` (§15.3.1), which is also the only one of the three that needs the block form.

An I6 island is placed in source order relative to the surrounding declarations; the ordering
guarantees around classes and instances are in §18.7.

**Example**

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

**See also** §7.3.3, §7.3.4, §15.3.1 (`#bgl`), §14.5.1, §18.7.

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

An `extern` declaration may carry an `asI6 <i6name>` clause naming the I6 identifier (§3.11).

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
`asI6` clause (§3.11). The Inform 6 Designer's Manual holds the authoritative, version-current list of
reserved words.

## 15.10 Third-party I6 Libraries and Raw Arrays

See §12.8.2.

---

# Part II — The Beguiler Compiler

# 16 Invoking the Compiler

## 16.1 Synopsis

```text
beguiler [options] <source> [<output>]
beguiler --lsp [-lib=<dir>] [-includepaths=<dir>[,<dir>…]]
```

`beguiler` is a single executable that transpiles a Beguile program to Inform 6, hands the result to
the Inform 6 compiler, and (optionally) packages the story file into a blorb. Run with no arguments it
prints a usage summary and exits with status 1.

The first line of console output is always a banner naming the compiler version and build date; the
compiler's version is also visible to the program as the pre-defined symbol `beguiler` (Appendix F).

## 16.2 Positional Arguments

**Source.** The first argument that does not begin with `-` is the source file. Its extension selects
the entry mode (§16.8): a `.bgl` file is compiled in default mode, a `.inf` file in precompiler mode.

**Output.** An optional second bare argument is the story file to write. When it is given it is used
exactly as written; when it is omitted the story file is placed in the output directory
(§20.1) as `<stem>.<ext>`, where `<stem>` is the source file name without its
extension and `<ext>` is chosen by the target: `ulx` for Glulx, `z5` or `z8` for the Z-machine.

A third bare argument is an error.

## 16.3 Options

Options are recognized only when they appear exactly as listed (upper- and lower-case variants are
listed where both are accepted). Any other argument beginning with `-` is not an error: it is passed
through, unchanged, to the Inform 6 command line (§16.3.1).

| Option | Effect | `#beguilerSettings` equivalent |
| --- | --- | --- |
| `-o <dir>` | Output directory for the story file and all intermediate files. `-o` without a following argument is an error. | `outputPath` |
| `-G`, `-g` | Target Glulx. | `target = Glulx` |
| `-z5`, `-Z5` | Target the Z-machine, version 5. | `target = Z5` |
| `-z8`, `-Z8` | Target the Z-machine, version 8. | `target = Z8` |
| `-E<n>` | Inform 6 error-message format; `<n>` is a single digit. `E1` is the Microsoft style, `E2` the Macintosh style. Emitted into the generated source as `!% -E<n>`. | `errorFormat` |
| `-inform=<name>` | The Inform 6 compiler to run. An absolute path is used verbatim; anything else is resolved relative to the directory containing the `beguiler` binary. The name `none` skips the Inform 6 hand-off: the transpiled `.inf` is written and compilation stops there. | `informName` / `informPath` |
| `-includepaths=<dir>[,<dir>…]` | Adds one or more directories to the include search path (§18.4). Comma-separated; whitespace around each entry is trimmed; duplicates are ignored. Entries are taken as written: no separator rewriting, no relative-path resolution, no existence check. | `includePaths` |
| `-lib=<dir>` | Location of the Beguile system library (the `beguiLib` tree). An absolute path is used verbatim; a relative path is anchored to the directory containing the `beguiler` binary. Surrounding `"` or `'` quotes are stripped. | `beguiLibPath` |
| `--debug` | Debug build: Inform 6 is run with `-k`, and the debug bundle is written (§20.3). | none |
| `--lsp` | Language-server mode (§16.6). No compilation takes place. | none |

### 16.3.1 Pass-through switches

Any argument beginning with `-` that is not one of the options above is appended verbatim to the
Inform 6 command line, after the compiler's own switches. This is the way to hand Inform 6 a switch
Beguile does not model, for example `-s` (statistics) or `-~S` (strict mode off). No validation is
performed; an unknown switch surfaces as an Inform 6 error.

The `-E<n>` option is the one exception to "exactly as listed": any `-E` followed by a digit is
consumed by Beguile rather than passed through.

## 16.4 Precedence Between the Command Line and `#beguilerSettings`

Most options can also be set from the source with a `#beguilerSettings` block (§17.1). Where both are
given, the rule is per property:

| Property | Rule |
| --- | --- |
| `target` | Command line wins. |
| `errorFormat` | Command line wins. |
| `outputPath` | Command line (`-o`) wins. |
| `informName` / `informPath` | Command line (`-inform=`) wins over both; between the two block properties, `informPath` wins over `informName` (§16.5). |
| `includePaths` | Additive. Command-line entries are added first, then block entries in the order they are parsed. |
| `beguiLibPath` | The command line (`-lib=`, or the default) locates the library for the core files loaded before the program's own source is read; a block value replaces it for every `#include` processed after the block. |

`--debug`, `--lsp` and pass-through switches have no settings equivalent; `generateBlorb`,
`blorbAssetPath`, `economy`, `omitUnusedRoutines`, `autoInitialize`, `release`, `serial`, the runtime
sizes and the game-metadata properties have no command-line equivalent.

## 16.5 Locating the Toolchain

The compiler never searches the system `PATH`. Both of its dependencies are located relative to the
`beguiler` binary unless told otherwise.

**The Beguile system library** (`beguiLib`). `-lib=<dir>` if given; otherwise the `beguiLib`
subdirectory of the directory containing the binary. This is where the auto-loaded core files, the
built-in templates (§18.9) and every `#include <…>` extension live.

**The Inform 6 compiler**, in decreasing order of precedence:

1. `-inform=<name>` on the command line.
2. `informPath = "…"` in `#beguilerSettings`: a full path to the binary, used verbatim.
3. `informName = "…"` in `#beguilerSettings`: a file name, resolved relative to the directory
   containing the `beguiler` binary.
4. The default: a file named `inform` next to the `beguiler` binary. If nothing was configured and no
   such file exists, `../inform6/inform6` relative to the binary is tried as a last resort.

Rules 1 and 3 accept `none` to skip the Inform 6 stage entirely.

## 16.6 Language-Server Mode

`--lsp` anywhere on the command line switches the binary into Language Server Protocol mode: it reads
JSON-RPC messages from standard input and writes responses to standard output, using the LSP
`Content-Length` framing, until its input closes. No source file is compiled and nothing is written to
disk.

In this mode only `-lib=` and `-includepaths=` are honored (with the meanings in §16.3); every other
argument is ignored. The VS Code extension starts the compiler this way to provide diagnostics,
completion and navigation while editing.

## 16.7 Exit Status and Console Output

The process exits with status 0 when the requested work completed, and 1 when it did not: a usage
error, a missing source file, a compile-time error (§19.2), a failure to write the transpiled file, or a
non-zero exit from Inform 6. Only the first compile-time error is reported; compilation stops there.

On success the console shows the transpile confirmation, the exact Inform 6 command line that was run,
and Inform 6's own output with its diagnostics rewritten to point at Beguile source lines (§19.1.1).
When the Inform 6 stage is skipped (`none`), the message says so.

## 16.8 Entry Mode

The source file's extension, and nothing else, selects the compilation mode: `.bgl` is default mode
and `.inf` is precompiler mode, as specified in §15.1.

In precompiler mode the compiler synthesizes no ICL header: the `!%` lines at the top of the `.inf`
file are the sole authority, and the target is read from them (`-G` selects Glulx; `-z` selects the
Z-machine, version 5; `-v3`, `-v5` or `-v8` select an explicit Z-machine version). A `-G` or
`-z5`/`-z8` option on the command line, or a `target` in a `#beguilerSettings` block, is still
accepted but the `!%` value is what Inform 6 sees.

## 16.9 Examples

```text
beguiler game.bgl
beguiler -z5 -o build game.bgl
beguiler -G --debug -includepaths=../lib,../shared game.bgl
beguiler -inform=none game.bgl            # transpile only; leaves build/game.bgl.transpiled.inf
beguiler -lib=/opt/beguile/beguiLib -inform=/opt/inform6/inform6 game.bgl
```

---

# 17 Settings (`#beguilerSettings`)

## 17.1 The Settings Block

**Syntax**

```syntax
#beguilerSettings {
    [ ⟨type⟩ ] ⟨property⟩ = ⟨value⟩ ;
    …
}
```

⟨value⟩ is an integer literal, a string literal (`"…"` or raw `@"…"`), `true`/`false`, or an enum
member written bare (`Z5`) or qualified by its enum (`eTarget.Z5`).

**Description**

A `#beguilerSettings` block configures the compiler and the Inform 6 invocation that follows. It is
permitted only at global scope (§14.7.1 lists the directive among the others). Any number of blocks
may appear, in the entry file or in any included file, and a block may be empty.

Each entry names one property from the fixed set in §17.2–§17.6 (Appendix E lists them all). The
optional leading ⟨type⟩ is documentation only; it must be a type name or a data-type keyword and is
otherwise ignored. Property names are case-insensitive, like every identifier. Naming a property that
does not exist, giving a value of the wrong type, or qualifying an enum value with the wrong enum
name, is a compile-time error.

**Precedence.** Properties follow **first-writer-wins**: the first block, in parse order, that sets a
property fixes it, and later assignments to the same property are ignored. A value given on the
command line counts as written before any block (§16.4). Two exceptions:

- `includePaths` is **additive**: every occurrence appends to the search path (duplicates are
  dropped).
- `release` and `seriesNumber` treat `0` as "not set", so a block that assigns `0` does not fix them.

**Entry-file properties.** Five properties are read from the entry source file before parsing begins,
by a textual scan of its `#beguilerSettings` blocks:

- `target`
- `includePaths`
- `generateBlorb`
- `blorbAssetPath`
- `autoInitialize`
- `worldBufSize`

The asset scan, the compile-time target symbols used by `#if` during the pre-scan, and the include
search path used to resolve forward references all come from this early read. These properties must
therefore be written in the entry file, as literal values, not in an included file.

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

**See also** §16.4, §18.4, Appendix E.

## 17.2 Toolchain Paths

These properties locate external tools. They are never written to the generated output.

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `informPath` | string | none | Full path to the Inform 6 compiler binary. Wins over `informName`. |
| `informName` | string | `"inform"` | File name of the Inform 6 binary, looked up next to the `beguiler` binary. `"none"` skips the Inform 6 hand-off. |
| `beguiLibPath` | string | `"beguiLib"` | Directory of the Beguile system library, replacing the binary-adjacent default for includes processed after the block. |
| `includePaths` | string | none | One or more directories, comma-separated, added to the search path for both `#include "…"` and `#includeI6` (§18.4). Additive across blocks and with `-includepaths=`. |

A regular `"…"` `includePaths` entry is normalized: separators are rewritten (§18.5), a relative path
is resolved against the directory of the file containing the block, and the result is canonicalized.
Each such entry must name an existing directory or the block is a compile-time error.
A raw `@"…"` entry is taken verbatim, with no rewriting, resolution or check. Every entry is also
emitted to Inform 6 as a `!% ++include_path=` line, one per directory, so that Inform 6 resolves its
own `Include` directives against the same set.

## 17.3 Compilation Settings

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `target` | `eTarget` | `Glulx` | `Glulx`, `Z5` or `Z8`. Emitted as `!% -G`, `!% -v5` or `!% -v8`, and exposed to `#if` as the valueless `TARGET_GLULX` / `TARGET_ZCODE` flags (Appendix F). The flags say which machine; for the Z-machine version, read the setting — `#if #beguilerSettings.target == "z8"` (§17.7). |
| `outputPath` | string | `"output"` | Directory for the story file and every intermediate file; relative to the source file's directory (§20.1). |
| `errorFormat` | `eErrorFormat` | `E1` | Inform 6 diagnostic style, emitted as `!% -E1` or `!% -E2`. Only `E1` and `E2` are accepted in a block. |
| `release` | int | `0` | Story release number, emitted as an I6 `Release` directive when non-zero. |
| `serial` | string | `""` | Story serial number, emitted as an I6 `Serial` directive when set. Must be exactly six digits. |
| `omitUnusedRoutines` | bool | `true` | Emits `!% $OMIT_UNUSED_ROUTINES=1`, so Inform 6 drops routines nothing references (§18.11). |
| `economy` | bool | `false` | Opt-in automatic text abbreviation (§18.11). |

## 17.4 Runtime Settings

These properties size runtime structures in the generated code. Each of the three sizes must be at
least 1.

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `framePoolSize` | int | `64` | Word slots in the frame pool, which backs local arrays on both targets and, on the Z-machine, local-variable overflow (§18.10). Emitted only when some routine needs it. |
| `linqScratchSize` | int | `32` | Elements per scratch buffer for `<linq>` query chains (§22.5). Emitted only when `<linq>` is included. |
| `worldBufSize` | int | `128` | Objects per scratch buffer for `bgl.world` queries (§21.9); a walk that would exceed it stops there. Sizes a runtime-library declaration, so it must be set in the entry file (§17.1). |
| `forInScratchSize` | int | `31` | Maximum elements in a literal-list `for (x in {…})` (§5.9.1). Emitted only when that form is used. |
| `rewritePaths` | bool | `true` | Rewrite `/` and `\` in every path (settings paths, `#include`, `#includeI6`) to the platform separator (§18.5). |
| `autoInitialize` | bool | `true` | When `true`, the library binding wraps the program's entry point so that `bglInit()` runs first (§21.2, §23.3.1). Set `false` when another library already replaces `Main`; the program then calls `bglInit()` itself. A program built on no binding at all always calls it itself, whatever this is set to — there is nothing to do the wrapping. |

The string pool used by `<string>` is not a setting: its size is the I6 constant
`bglStringPoolReserve`, declared in an `#i6` island before the extension is included (§22.3).

## 17.5 Game Metadata

These properties describe the work. They feed the iFiction record written into a blorb (§17.6) and
are readable from source (§17.7); apart from `release`, `serial` and `ifid` they are **not** emitted
into the story file by the compiler.

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `title` | string | `""` | Title (iFiction `<title>`). `"Untitled"` in the blorb record if unset. |
| `author` | string | `""` | Author (iFiction `<author>`). `"Anonymous"` in the blorb record if unset. |
| `headline` | string | `""` | Subtitle or tagline (iFiction `<headline>`). |
| `genre` | string | `""` | Genre, e.g. `"Mystery"` (iFiction `<genre>`). |
| `description` | string | `""` | Blurb (iFiction `<description>`). |
| `language` | string | `""` | ISO-639 code, e.g. `"en"` (iFiction `<language>`). |
| `series` | string | `""` | Series name (iFiction `<series>`). |
| `seriesNumber` | int | `0` | Position in the series (iFiction `<seriesnumber>`); written only when greater than 0. |
| `firstPublished` | string | `""` | `"YYYY"` or `"YYYY-MM-DD"` (iFiction `<firstpublished>`). |
| `forgiveness` | string | `""` | `"Merciful"`, `"Polite"`, `"Tough"`, `"Nasty"` or `"Cruel"` (iFiction `<forgiveness>`). |
| `ifid` | string | `""` | IFID in UUID form. Auto-generated when blorb packaging is on (§17.6.2). |

**What reaches the story file.** `release` and `serial` become I6 `Release` and `Serial` directives.
When `ifid` is set (explicitly or by generation) the compiler embeds it in the story file in the
Treaty of Babel form that cataloging tools search for:

```i6
Array UUID_ARRAY string "UUID://A0B1C2D3-E4F5-6789-ABCD-EF0123456789//";
```

Everything else is available only through `#beguilerSettings.prop` (§17.7). The IF library bindings
use that mechanism to declare the library's `story` and `headline` constants from `title` and
`headline` (§23.3.2).

## 17.6 Blorb Packaging

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `generateBlorb` | bool | `false` | Master switch for the asset scan and the blorb build. |
| `blorbAssetPath` | string | `"assets"` | Directory scanned for assets. Relative to the source file's directory. |

When `generateBlorb` is `true`, two extra stages run (§18.2):

1. **Before parsing**, the asset directory is scanned and `_blorbAssets.bgl` is written next to the
   source file.
2. **After Inform 6 succeeds**, the story file and every discovered asset are assembled into a blorb
   next to the story file: `<stem>.gblorb` for Glulx, `<stem>.zblorb` for the Z-machine
   (§20.2). The blorb carries an iFiction record built from §17.5.

A missing asset directory is reported and packaging proceeds with no assets.

### 17.6.1 Asset Discovery and `_blorbAssets.bgl`

The asset directory is scanned **non-recursively**; files whose name begins with `.` are ignored.
Each remaining file becomes one resource, classified by its extension (case-insensitively):

| Extension | Resource | Enum |
| --- | --- | --- |
| `png`, `jpg`, `jpeg` | picture | `eImages` |
| `aiff`, `aif` | sound | `eSounds` |
| anything else | generic data | `eUnknownAsset` |

Unrecognized files are packaged rather than dropped, and a console note lists them.

The enum member name is the file's stem in camelCase (`-`, `_` and space are word breaks) followed by
the capitalized extension: `priest.png` → `priestPng`, `title-theme.aiff` → `titleThemeAiff`.
Resource numbers are assigned from 1, pictures first, then sounds, then generic data, alphabetically
within each group; the number is the member's value.

The core library declares three empty enums, `eImages`, `eSounds` and `eUnknownAsset`, and the
union `eAssets = eImages | eSounds | eUnknownAsset` (§21.12). The generated `_blorbAssets.bgl` extends
each of them:

```bgl
// Auto-generated by beguiler blorbifier — do not edit
extend enum eImages {
    priestPng = 1
}
extend enum eSounds {
    titleThemeAiff = 2
}
extend enum eUnknownAsset {
}
```

The file is regenerated on every build with packaging on. Include it once from the source
(`#include "_blorbAssets.bgl"`) so the members are in scope; the union lets one routine accept any
asset while `eImages`-typed operations reject a sound id.

`generateBlorb` is also visible to `#if` as the symbol `generateBlorb`, carrying the value `true` or
`false`; test it by value, `#if (generateBlorb == true)`, because the symbol is always declared
(Appendix F).

### 17.6.2 IFID Generation and Persistence

When packaging is on and no `ifid` is set, the compiler supplies one:

1. **Deterministic.** The IFID is a name-based UUID derived from the source file name, `author`
   and `title`; the same three inputs always yield the same IFID.
2. **Persisted.** It is written to the head of `_blorbAssets.bgl` as
   `#beguilerSettings { ifid = "…"; }`, from where later builds read it back like any other
   setting.
3. **Explicit wins.** An `ifid` in the program's own source takes precedence under
   first-writer-wins; the persisted value is a fallback.
4. **Stable.** An IFID must never change once a work is published; rules 1–3 keep it constant across
   rebuilds even if the generated file is deleted.

## 17.7 Reading Settings From Source

**Syntax**

```syntax
#beguilerSettings.⟨property⟩
```

**Description**

In an expression, `#beguilerSettings.property` is replaced at parse time by a literal holding the
property's value: a string literal for string and enum properties (an enum value is its name, e.g.
`"z5"`), an integer literal for integer properties, and `true`/`false` for boolean ones. **Every
declared property may be read** — a property the author can write is one they can read back. A name
that is not a declared property is a compile-time error.

| Result | Properties |
| --- | --- |
| string | `title`, `author`, `headline`, `genre`, `description`, `language`, `series`, `firstPublished`, `forgiveness`, `ifid`, `target`, `outputPath`, `blorbAssetPath`, `informName`, `informPath`, `beguiLibPath`, `errorFormat`, `serial`, `includePaths` |
| int | `release`, `seriesNumber`, `framePoolSize`, `linqScratchSize`, `worldBufSize`, `forInScratchSize` |
| bool | `generateBlorb`, `autoInitialize`, `economy`, `omitUnusedRoutines`, `rewritePaths` |

`includePaths` is a list, which has no literal form, so it reads back as the `;`-joined search path.

The value is whatever has been fixed when the reference is parsed. A string property not yet set
reads as `""` and an integer property as `0`, except the three runtime sizes, which read as their
defaults; so the block that sets a property must precede, in parse order, any reference to it.

Inside the raw-I6 body of `#emitfirst`, `#emitlast`, `#storedEmitFirst` and `#storedEmitLast`, the
same properties are available as `##beguilerSettings.property` (§14.4.5).

**In a `#if` condition.** The same reference reads the same value in a compile-time condition
(§14.2.5), which is how a program asks a question the target flags cannot answer — `TARGET_ZCODE`
and `TARGET_GLULX` say which machine, and nothing more. A string property compares against a string
literal, ignoring case; an int property compares numerically; a bool property tests on its own. The
property must be written with no space around the `.`.

**Example**

```bgl
#beguilerSettings { title = "Cloak of Darkness"; release = 3; }

const string story      = #beguilerSettings.title;     // → "Cloak of Darkness"
const int    gameRelease = #beguilerSettings.release;  // → 3

#if #beguilerSettings.target == "z8"
    // this build has the larger Z-machine address space
#endif
```

**See also** §14.2.5, §14.4.2, §14.4.5, §17.5.

## 17.8 Bindings and the Library Banner Constants

The IF library bindings declare the library's `story` and `headline` constants from the `title` and `headline` settings; see §23.3.2.

---

# 18 Compilation Model

## 18.1 Overview

Beguile is a transpiler: a program is translated to a single Inform 6 source file, which the Inform 6
compiler then turns into a story file. This chapter describes the pipeline as an author can observe
it: which passes run, what may be referenced before it is declared, how files are found, in what order
the generated file is laid out, and which parts of the output are present only when used.

The same pipeline serves both entry modes (§15.1). In precompiler mode the Beguile passes operate on
the embedded `#bgl` islands and the surrounding I6 passes through; an `.inf` file with no islands at
all is copied through unchanged (§15.1.2).

## 18.2 Phases

A build runs these stages in order. Stages 1 and 8 run only when blorb packaging is enabled
(§17.6); stages 6–8 run only when the Inform 6 hand-off is not disabled (§16.3, `-inform=none`).

1. **Asset scan.** The asset directory is scanned and `_blorbAssets.bgl` is (re)written next to the
   source file.
2. **Pre-scan (pass 1).** The source and everything it includes are read once to register the names
   of every global declaration (§18.3).
3. **Parse (pass 2).** The source is lexed and parsed in full; types are resolved and checked.
4. **Whole-program checks.** Checks that need the complete program, such as duplicate member
   detection across `extend` blocks and the hidden-member rules (§8.7.4), run here; unset settings take
   their defaults.
5. **Emission.** The transpiled file `<source>.transpiled.inf` is written to the output directory
   (§20.2); in a debug build the debug bundle is written beside it.
6. **Abbreviation pass** (only with `economy = true`, §18.11).
7. **Inform 6.** The Inform 6 compiler is run on the transpiled file; its diagnostics are rewritten
   to Beguile locations (§19.1.1).
8. **Blorb assembly.** The story file and the assets are packaged into a `.gblorb` or `.zblorb`.

The first compile-time error ends the build.

## 18.3 Passes and Forward References

Declarations at global scope may appear in any order. The pre-scan registers a stub for every global
name before the main parse begins, so a name may be used before the file position at which it is
declared, and an included file may refer to declarations in a file included after it. The pre-scan
registers:

- classes (including `extern` and generic ones) and their members;
- enums, bnums and unions, with their members;
- objects, verbs and global variables, with the members declared in an object body;
- global functions;
- members added by `extend` bodies, whether the target is declared before or after the `extend`;
- `#declare` symbols, which are consequently visible to every `#if` in the program regardless of
  position (§14.2.3).

Conditional directives (`#if`, `#elif`, `#else`, `#endif`) and `#define`/`#undef` are honored
during **both** passes, so a declaration excluded by `#if` is not registered either. `#define` is
positional: it affects only conditionals parsed after it (§14.2.1).

Some things are not order-independent, because their value is fixed at the point of parsing:

- `#beguilerSettings.prop` reads the value fixed so far (§17.7).
- The emission-ordering rule for `extern attribute` (§18.7, rule 3) depends on source order.
- A `#using` naming a class or object declared later is accepted but ignored, with a warning (§19.3).

## 18.4 Include Resolution

`#include`, `#includeI6` and `#beguilerSettings` are specified in §14.1 and §14.7.1; this section
states how the file they name is found.

**Library includes, `#include <name>`.** The `beguiLib` tree (§16.5) is
walked recursively, files in each directory before its subdirectories, subdirectories in alphabetical
order; the first file whose path ends with the requested name is taken.

**Relative includes, `#include "path"` and `#includeI6 "name"`.** The directory of the including file
is searched first, then each `includePaths` entry in order (command-line entries before block
entries). A `#includeI6` that resolves is emitted with its full absolute path, so Inform 6 never
searches for it; the raw form `#includeI6 @"…"` bypasses resolution and is emitted verbatim.

**Case-insensitive matching.** Every component of an include path, directory or file, matches
case-insensitively; an exact-case match is preferred, and if two entries in one directory differ only
in case, which one is chosen is unspecified.

**Nesting.** Includes may nest to a depth of 255; exceeding it is a compile-time error. A file
guarded by `#once` is processed at most once however often it is included.

## 18.5 Path Resolution

Every path written in Beguile source — include paths and the path-valued settings — is normalized
when parsed:

- **Separator rewriting.** Each `/` and `\` becomes the platform separator, so a path written with
  either separator works on any platform. `rewritePaths = false` disables this for the program.
- **Relative anchoring.** `includePaths` and `blorbAssetPath` are relative to the directory of the
  file that sets them; `outputPath` is relative to the entry source file's directory
  (§20.1).

## 18.6 Layout of the Generated File

The transpiled `.inf` file is laid out in this order, regardless of the order of the source. Items
marked *(when used)* are absent from programs that do not need them.

1. The ICL header: `!% -G` / `!% -v5` / `!% -v8`, `!% -E<n>`, `!% $OMIT_UNUSED_ROUTINES=1` and one
   `!% ++include_path=` line per `includePaths` entry. In precompiler mode the entry file's own `!%`
   lines appear here instead.
2. The `beguiler` version constant, then `Serial` and `Release` when set, the runtime size constants
   *(when used)*, and the `UUID_ARRAY` IFID string *(when set)*.
3. Compiler scratch globals *(when used)*.
4. The frame pool *(when used, §18.10)* and Z-machine excess-parameter globals *(when used)*.
5. Every `#emitfirst` block, in file-inclusion order, each file's block at most once; then the fired
   `#storedEmitFirst` blocks *(when triggered, §18.9)*.
6. The `bglInit` routine (§18.8).
7. The program's declarations in source order, subject to the ordering rules of §18.7: classes,
   objects, globals, arrays, functions, verbs and grammar, and the contents of `#i6` islands.
8. Static class routines, then any grammar evictions.
9. Every `#emitlast` block, in file-inclusion order; then the fired `#storedEmitLast` blocks
   *(when triggered)*.
10. Referenced `superposed` declarations, appended in the order they were observed (§18.9).
11. In precompiler mode, the entry file's trailing `end;` and anything after it. It comes after the
    `superposed` declarations because Inform 6 stops reading at `end;`.

## 18.7 Emission Ordering

Inform 6 requires a class to be defined before any instance of it or class derived from it, and an
attribute to be declared before any `has` clause that names it. The emitter keeps source order except
where these three rules require otherwise:

1. **Lazy class emission.** A class is normally emitted at its source position. If an earlier
   declaration is an instance of it (`ClassName var;`, or an object of that class), the class is
   emitted immediately before that first instance instead.
2. **Base before derived.** Before a class is emitted, at its own position or by rule 1, its base
   classes are emitted first, transitively.
3. **`extern attribute` as a gate.** A class whose attribute list names an `extern attribute` cannot
   be emitted before the `extern attribute` declaration has been passed in source order. That
   declaration stands in for the `#includeI6` that defines the attribute in I6, so a bindings file
   must be included before any class, or first instance of a class, that uses one of its attributes.
   Violating this is a compile-time error (§19.2).

Circular inheritance (`class A : B` where `B` derives, directly or transitively, from `A`) is a
compile-time error detected when the inheritance clause is parsed, independent of emission.

Member types, method bodies and value references impose no ordering constraint: a class may declare a
member of a class declared later, and a routine may call a routine or name an object declared later.

## 18.8 `bglInit()`

Every program contains a routine `bglInit`, emitted at position 6 of the generated file (§18.6),
before the program's own declarations. This section states only its place in the output; what it
does, in what order, and who calls it are specified in §21.2.

In precompiler mode the routine is declared but is never called unless the `.inf` file arranges it
(§15.1.2).

## 18.9 Pay-Only-If-Used Emission

Three mechanisms let the library declare code and data that cost nothing in a program that does not
use them. The `superposed` qualifier itself is specified in §3.2 and §3.12; this section states what
the emitter does with it.

**`superposed` declarations.** A `superposed` routine, global, array, object or class is withheld
from the transpiled file. After everything else has been emitted, the output is searched for the
declaration's name as a whole word, case-insensitively; if it occurs, the declaration is **appended at
the end** of the file, and the search repeats until nothing more is pulled in. A `superposed` class
that is used in a declaration position — a static instance or a subclass — is instead emitted in place
ahead of that use, with its `superposed` bases first, because an I6 `Class` must precede its
instances and subclasses. A `superposed` declaration nothing references does not appear in the
transpiled file at all, and is likewise absent from the debug bundle (§20.3).

Because the test is textual, a name that also occurs in a string literal or comment materializes its
declaration (a harmless dead entry); a genuinely referenced declaration is never dropped.

**Stored emit blocks.** `#storedEmitFirst name` and `#storedEmitLast name` (§14.4.4) register a raw-I6
block that is emitted, at the position of the corresponding unstored form, only when a built-in
template that needs it is applied. Each block is emitted at most once.

**Built-in templates.** A few constructs have no fixed I6 form until the compiler knows how the
program uses them: the frame pool is sized to the program, and the several `for … in` forms each expand
to a different loop. The compiler holds these as built-in I6 templates, which are part of the system
library but are not Beguile source and are never `#include`d; authors do not write or edit them. A
template names the stored blocks it needs, so that a helper such as the literal-list `for … in`
scratch buffer is emitted only in programs that use the construct.

## 18.10 The Frame Pool

The Z-machine gives a routine 15 local slots for parameters and locals together; one slot is
reserved for the frame pointer, so a routine needing more than 14 has its excess locals spilled into
a global **frame pool**. The mechanism is recursion-safe and invisible to the author. The pool is also the backing store for local arrays
(§12.6) on both targets. It is emitted only when some routine needs it, and its size in words is
`framePoolSize` (§17.4). A program that exhausts it halts with a runtime error (§19.4).

> **[Glulx]** Glulx has no practical local-variable limit; spilling never occurs, and the pool is
> emitted only for local arrays.

> **[Z-machine]** A routine call passes at most five arguments natively; parameters beyond the fifth
> travel through compiler-emitted globals, which appear in the header when any routine declares more
> than five.

## 18.11 Dead Code and Economy

Three independent controls reduce story-file size.

**`superposed`** (§18.9) withholds a declaration from the transpiled file unless it is referenced.

**`omitUnusedRoutines`** (default `true`) emits `!% $OMIT_UNUSED_ROUTINES=1`, asking Inform 6 to
drop every routine that is emitted but never referenced. Inform 6 sees the whole compilation,
including `#i6` islands and included I6 libraries, and keeps any routine whose address is taken, so
the setting is safe; set it `false` for a debug build in which every routine should be present.

**`economy`** (default `false`) enables automatic text abbreviation. After the transpiled file is
written, the compiler runs Inform 6 once with `-u` to compute the optimal `Abbreviate` set, inserts
those directives at the top of the transpiled file, and compiles with `-e`. If the program already
contains any `Abbreviate` directive of its own, the pass is skipped and a console note says so. The
pass is best-effort: if the pre-pass fails, the build proceeds without abbreviations.

---

# 19 Diagnostics

## 19.1 Message Format

Every diagnostic the compiler produces about the source is one line of the form

```text
<file>:<line>:<column>: ERROR: <message>
<file>:<line>:<column>: warning: <message>
```

`<file>` is the path of the Beguile source file containing the construct, as the compiler resolved
it. When a diagnostic is anchored to a statement the column is `1`; otherwise it is the position at
which the lexer stopped. Diagnostics are written to standard error.

**Errors stop the build.** The first error ends compilation with exit status 1 (§16.7).
A program therefore never receives more than one error per build.

**Warnings continue.** A warning is advisory; the build proceeds. Warnings whose location lies inside
the system library (the `beguiLib` tree) are not shown: they concern the library's own internals and
an author cannot act on them.

**Location-free messages.** A few checks run after parsing, when no source position is current; their
messages carry no `file:line:column` prefix.

### 19.1.1 Inform 6 Diagnostics

Inform 6's own output is passed through the console with each diagnostic rewritten to Beguile
conventions:

```text
<file>.transpiled.inf:<line>:1: ERROR: <message>
  ↳ <file>.bgl:<line>:1
```

The first line names the transpiled file; the indented second line names the Beguile source line from
which that I6 line was generated, and is omitted when no Beguile line corresponds (for example, in a
compiler-generated header line). Inform 6 "Fatal error" and "Error" both appear as `ERROR`;
"Warning" appears as `warning`. Inform 6 warnings that map into the system library, or that name a
`_bgl`-prefixed symbol, are hidden, and Inform 6's closing "Compiled with N warnings" count is
adjusted to the number actually shown. Any Inform 6 error fails the build even when Inform 6 itself
exits successfully.

## 19.2 Compile-Time Errors

The categories below are those an author is most likely to meet. Messages are quoted as
representatives; `'x'` stands for the offending name.

**Global name collisions.** Every global declaration — variable, function, class, object, enum, verb —
must be unique. The message always cites the original declaration:

```text
'score' is already defined (originally declared at myLibrary.bgl:17)
'score' is already defined as a type (originally declared at myLibrary.bgl:17)
```

Collisions between Beguile names and symbols defined only in raw I6 (`#i6`, `#includeI6`) are not
visible to Beguile and surface as Inform 6 errors instead; declare such symbols `extern` (§15.4) to
bring them into the Beguile namespace. In precompiler mode the compiler does scan the surrounding I6
for declarations and reports overlaps as warnings (§19.3).

**Member collisions.** A member declared twice in one class or object, or added by `extend` when a
member of that name exists, is an error unless `replace` is used (§8.7):

```text
class 'Room': member 'light' is already defined
extend class 'Room': member 'describe' is already defined; use 'replace' to override
```

**Ambiguous names.** A bare identifier that could refer to more than one declaration, for instance
members of two `#using` imports, or an operator reference with more than one candidate:

```text
'val' is ambiguous: matches #using-imported member 'libA.val' and #using-imported member 'libB.val'. Qualify the use explicitly to disambiguate.
```

**Inheritance and emission order** (§18.7):

```text
class 'A': circular inheritance — 'B' transitively inherits from 'A'
class 'Room' uses `has light` but its bindings-file declaration `extern attribute light;` comes later in source (triggered by: …). Move the bindings file before the class or its first instance.
```

The second is a location-free message.

**Includes and directives** (§14):

```text
#include: file 'x' not found
Maximum include nesting depth (255) exceeded while including 'x'
'#define x' redefines a symbol that is already defined; use '#redef x' to intentionally redefine it
```

`#error "text"` raises an error whose message is the given text.

**Settings** (§17.1):

```text
Unknown beguilerSettings property 'x'
beguilerSettings property 'x' expects an int, got 'y'
beguilerSettings property 'serial' must be exactly 6 digits (e.g. "250328")
beguilerSettings property 'framePoolSize' must be at least 1
beguilerSettings includePaths entry 'x' does not resolve to an existing directory ('…'). Use @"..." to emit a literal path without filesystem validation.
#beguilerSettings.x: unknown or unsupported property
```

**Qualifier misuse.** Combining qualifiers that exclude one another, or applying one where it has no
effect, is an error; for example `superposed` on an `extern`, `emitter` or `alias` class or on
`extend class` (§3.2, §3.12).

**Post-emission check.** After the transpiled file is written, the compiler verifies that no
property-class member (§9.9) was emitted as a raw property access. A failure names the transpiled file
and line and suggests reading the value into a local first.

## 19.3 Warnings

**`#using`** (§10.4). The directive is ignored, with a warning, when its target is not a declared class
or object, when a named member does not exist, or when the member is not of an importable type:

```text
#using 'myLib': not a declared class or object; directive ignored
#using 'myLib.x': member not found; directive ignored
```

**Shadowing.** A local variable, parameter or loop variable that hides a global, a member of the
enclosing class or object, or a capturable name from an enclosing function:

```text
Local variable 'score' shadows global of the same name; the global is unreachable from this scope.
Parameter 'name' shadows a member of class 'Thing'.
```

**Object-member ambiguity.** Inside an object body, a bare identifier that resolves at file scope but
is also a property of the enclosing object (§3.8):

```text
Bare 'light' resolves as global variable 'light', but 'light' is also a property of the enclosing object. Beguile cannot tell which you mean. Write 'self.light' for the object's property, or '::light' to force the global and silence this warning.
```

**Method overriding without `replace`** (§8.7.3):

```text
class 'Door': method 'describe' shadows definition in base class 'Thing'; use 'replace' to suppress this warning
replace: no existing global function 'x' found; treating as new definition
```

**Verbs and grammar** (§13.2.3, §13.5.2). Pattern tokens after the trigger word in an `extern verb`'s grammar are
ignored; a `grammar -=` that matches nothing removes nothing:

```text
extern verb 'PutOn': pattern tokens after the trigger word(s) in `grammar = {...}` are ignored — …
grammar -= on 'Take': no grammar for word `'grab'` exists, so nothing was removed. …
```

**Precompiler mode.** A routine, global, constant, object or other symbol declared in the surrounding
I6 whose name is also a Beguile global:

```text
<file>: warning: I6 routine 'Score' collides with Beguile-declared global of the same name
```

**Unreachable `bglInit()`** (§21.2). The program has runtime initialization to do — a sized tracked
array or byte array to stamp, a `#startup` block, or a deferred global initializer — and nothing in
the transpiled file calls `bglInit()`. The program still builds and still runs; it runs on
uninitialized data.

```text
WARNING: nothing calls bglInit(), so this program starts with the BLR uninitialized.
```

**Other.** `hide` naming a member that is not inherited; re-typing a `typesealed` member (the retype
is ignored); `superposed` on a non-`static` method (no effect); `#warning "text"` reports the given
text.

## 19.4 Runtime Failures

The generated code and the BLR detect a small number of conditions at run time. Each prints a bracketed
message and halts the story with `quit`, except where stated.

| Condition | Message | Recovery |
| --- | --- | --- |
| Frame pool full (§18.10) | `[Beguile runtime error: frame pool exhausted]` | Raise `framePoolSize`. |
| Literal-list `for … in` scratch full (§5.9.1) | `[Beguile runtime error: for-in literal-list scratch exhausted]` | Raise `forInScratchSize`. |
| `<linq>` query step exceeds its buffer (§22.5) | `[Beguile runtime error: filter() output exceeds linqScratchSize. Increase via #beguilerSettings.linqScratchSize.]` (the method name varies) | Raise `linqScratchSize`. |
| `<linq>` query chains nested more than two deep (§22.5) | `[Beguile runtime error: LINQ chain nesting exceeds _BGL_LINQ_MAXDEPTH. …]` | Capture the inner result in a local first. |
| `setLength` beyond the word range (§12.3) | `[Beguile runtime error: setLength value exceeds signed range (max 32767 on Z, 2^31-1 on Glulx)]` | — |
| `<string>` pool full (§22.3) | `[ERROR: Unable to allocate a new instance.]` | Raise `bglStringPoolReserve`. |
| Pooled class full (§8.2.6) | none: `new` returns `nothing` | Test the result of `new`, or size the pool larger. |
| `bgl.world` result buffer full (§21.9) | none: the walk stops at 128 objects | Narrow the query. |
| `throw` with no enclosing `try` (§5.16) | the interpreter's own error; the story halts | — |

---

# 20 Build Outputs and Debugging

## 20.1 Output Directory

Every file a build produces, other than `_blorbAssets.bgl`, is written to one **output directory**.
It is named by `-o` on the command line, else by `outputPath` in `#beguilerSettings`, else it is
`output`. A relative name is resolved against the directory of the entry source file, never against
the working directory; the directory is created if it does not exist. An explicit output-file
argument on the command line (§16.2) is the one exception: it is written exactly
where named.

## 20.2 Files Produced

For an entry file `game.bgl` targeting Glulx, a full build with blorb packaging and `--debug` leaves:

| File | Location | Produced |
| --- | --- | --- |
| `game.bgl.transpiled.inf` | output directory | always: the generated Inform 6 source, retained after the build |
| `game.ulx` | output directory | on Inform 6 success (`game.z5` / `game.z8` for the Z-machine targets) |
| `game.gblorb` | output directory | with `generateBlorb` (`game.zblorb` for the Z-machine) |
| `_blorbAssets.bgl` | beside `game.bgl` | with `generateBlorb`, before parsing (§17.6) |
| `game.bgl.bgldbg` | output directory | with `--debug` (§20.3) |
| `game.bgl.transpiled.inf.dbg` | output directory | with `--debug`: Inform 6's debug file (§20.4) |

The transpiled file is always `<source file name>.transpiled.inf`, including the source extension, so
a precompiler-mode `story.inf` yields `story.inf.transpiled.inf`. It is the file Inform 6 compiles
and the file its diagnostics refer to (§19.1.1); with `-inform=none` it is the
build's final product.

**Current-file-relative I6 includes.** Inform 6 resolves `Include ">name"` relative to the file
containing it, which after transpilation is the output directory rather than the source directory.
The compiler rewrites every such directive in the transpiled file so that it still resolves against
the source directory; an author's `#i6 { Include ">lib.h"; }` therefore behaves as it would in a
hand-written `.inf` beside the source.

## 20.3 Debug Builds

`--debug` (§16.3) changes two things: Inform 6 is run with `-k`, so it writes its own debug
file, and the compiler writes its **debug bundle**, `<source file name>.bgldbg`, beside the transpiled
file. Everything else about the build is unchanged; in particular `omitUnusedRoutines` still applies
unless the program sets it `false` (§18.11).

The bundle is plain text in three sections, each introduced by a bracketed header:

| Section | Contents |
| --- | --- |
| `[map]` | One line per generated I6 line that came from Beguile source: the I6 line number, the Beguile file and the Beguile line. This is the table the compiler itself uses to rewrite Inform 6 diagnostics, and the debugger uses to step through Beguile source while the story runs. |
| `[sym]` | The program's globals, objects (with their properties), functions and extern objects, each with its I6 identifier and kind. A `superposed` declaration that was never referenced is omitted, since it has no I6 counterpart. |
| `[types]` | Type information: each enum with its values; each class and each object with its own properties, giving every property's Beguile type; and the declared type of every routine local and global. This is what lets the debugger show a `dictionaryWord` or an `array<object>` as such rather than as a number. |

The bundle describes only what the compiler knows; VM addresses come from the Inform 6 file.

## 20.4 The Inform 6 Debug File

With `-k`, Inform 6 writes `gameinfo.dbg`, an XML database mapping VM addresses to I6 source lines and
describing routines, their local-variable frames, globals and properties. The compiler moves it into
the output directory as `<source file name>.transpiled.inf.dbg`. Its format is Inform 6's and is not
specified here.

Together the two files give a debugger the chain *VM address → I6 line → Beguile file and line*, plus
typed variables; the VS Code extension consumes both to provide source-level debugging of a running
story.

## 20.5 Console Output

A build reports, in order: the compiler banner; blorb scan and IFID notes when packaging is on; any
compile-time diagnostics (§19.1); on a successful transpile, the exact Inform 6 command line that is
about to run; Inform 6's own output, rewritten; and blorb assembly notes. Inform 6's command line
shows the switches that were passed through unchanged (§16.3.1), the `-k` added
by `--debug`, and the `-e` added by `economy`.

---

# Part III — The Beguile Language Runtime (BLR)

# 21 Runtime Core

## 21.1 Overview

The Beguile Language Runtime (BLR) is the library of Beguile source that every program compiles against. Its *core* is loaded automatically: no `#include` is needed, in either default mode or precompiler mode (§15.1). Everything in this chapter is part of the core. The extensions, each enabled with `#include <…>` except `<array>`, which the core includes itself (§22.1), are in §22; the IF library bindings are in §23.

The core provides:

- the `bgl` namespace and its target-specific branches (§21.3);
- the output routines `print()` and `log()` and the article helpers (§21.4);
- the IF-domain types: `attribute`, `property`, `dictionaryWord`, `verb`, the grammar types, `bglClass`, `parentProp`, `childrenProp`, `_bglObject`, `eType` (§21.5);
- the numeric, character and allocation utilities: `uint`, `bgl.util.math`, `bgl.util.random`, the `char` methods, `bglAllocated` (§21.6–§21.8);
- object-tree queries, `bgl.world` (§21.9);
- the user-interface roots `bgl.ui.mainWin` / `bgl.ui.statusBar` and the print rules (§21.10–§21.11);
- the utility types `bglSize` and the blorb asset enums (§21.12).

The primitive types (`int`, `bool`, `char`, `string`, `float`, `object`, `var`) and their literals are documented in §2.2; they are also core.

In precompiler mode the core is loaded but the `bgl` namespace is not imported: a `#bgl` island must declare `#using bgl;` before using `bgl.…` (§15.1.2).

## 21.2 `bglInit()`

**Syntax**

```syntax
bglInit();
```

**Description**

`bglInit()` is a routine the compiler synthesizes for every program; its position in the generated file is given in §18.8. It runs at most once however often it is called and, in order:

1. the length headers of sized tracked arrays and byte arrays (§12.3, §22.2, §22.4);
2. every `#startup` block in the program, in file-inclusion order (§14.4.1);
3. the deferred initializers of globals, in declaration order (§3.6): for a class-typed global whose type declares a parameterless `init` (§8.5), the `init` body and then the `operator =` that applies a declared value; for any global whose initializer is not a constant expression, that initializer.

A program built on an IF library binding does not call `bglInit()` itself: the binding wraps the library's `main` so that `bglInit()` runs first (§23.3.1). A program built without a binding, or with `autoInitialize = false` (§17.4), must call `bglInit()` from its entry point before using anything that depends on it. The call is always available and is harmless when nothing has registered work.

Omitting it does not stop the build and does not stop the program: it runs on uninitialized data, where a sized tracked array reports its raw header word as its length and a sized byte array is still a null pointer. The compiler therefore warns when the program has initialization to do and nothing in the transpiled file calls `bglInit()` (§19.3).

Extensions that need `bglInit()` say so in their entry in §22: `<string>` and `<linq>` do, and so do `<array>` and `<buf>`, whose sized, uninitialized tracked arrays have no length header until it runs: before `bglInit()`, such an array reports its raw header word from `size()` and `length()` and `append` fails; arrays declared with an initializer list are complete at compile time. `<ui>`, `<glulxWindow>` and `<glulxImage>` do not need it.

**Example**

```bgl
void Main() {
    bglInit();
    // …
}
```

**See also** §14.4.1, §15.1.2, §18.8, §23.3.1.

## 21.3 The `bgl` Namespace

`bgl` is the root namespace object of the runtime. Its members are reached by dotted path (`bgl.asm.add(a, b)`, `bgl.util.math.pow(2, 8)`); `#using bgl.glulx;` and the other `#using` forms import a branch so its members are reachable bare (§10.4).

| Path | Contents | Target |
|---|---|---|
| `bgl.wordsize` | The word size of the active target: `2` on the Z-machine, `4` on Glulx. | both |
| `bgl.asm.*` | Direct opcode emitters for the active virtual machine; the set of members differs per target (§21.13). | both |
| `bgl.glulx.*` | Glulx-specific names: the enum aliases `eStyleType`, `eWinType`, `eImgAlign`, `eImgDimension`, `bWinBorder`, `bWinPlacement`, `bWinScale` (§22.7.10); the window types and `color` when `<glulxWindow>` is included (§22.7). | Glulx |
| `bgl.zcode.*` | Z-machine-specific names. Empty in the current core. | Z-machine |
| `bgl.util.*` | Utilities: `math` (§21.6.2), `random` (§21.6.3), and `buf` when `<buf>` is included (§22.2). | both |
| `bgl.world.*` | Object-tree queries (§21.9). | both |
| `bgl.ui.*` | `mainWin`, `statusBar` (§21.10); `screen` with `<glulxWindow>` (§22.7.2); `hideCursor`, `showCursor`, `waitForKey` with `<ui>` (§22.6). | both |
| `bgl.printRules.*` | Text-style print rules for interpolated strings (§21.11). | both |
| `bgl.story.*` | Story-file identity values. Provided by the `i6StandardLibrary` binding (§23.3.9). | both |

Only the branch for the active target is loaded: a Glulx build loads `bgl.glulx` and the Glulx `bgl.asm`; a Z-machine build loads `bgl.zcode` and the Z-machine `bgl.asm`. Referencing a member of the other target's branch is a compile-time error.

**Example**

```bgl
int bits = bgl.wordsize * 8;             // 16 on Z-machine, 32 on Glulx
int roll = bgl.util.random.get(6);       // 1..6
```

**See also** §10, §10.4, §22.

## 21.4 `print()`, `log()` and Article Helpers

**Syntax**

```syntax
print( ⟨value⟩ ) ;
print( $"⟨text⟩ {⟨expr⟩} ⟨text⟩" ) ;
log( ⟨value⟩ ) ;
printName( ⟨obj⟩ ) ;
bgl.printRules.a( ⟨obj⟩ ) ;   bgl.printRules.cA( ⟨obj⟩ ) ;
bgl.printRules.the( ⟨obj⟩ ) ; bgl.printRules.cThe( ⟨obj⟩ ) ;
```

**Description**

`print()` writes a value to the current output stream immediately. It is overloaded on the argument's type; the core supplies overloads for every primitive type and the IF-domain types, and extensions and bindings add or replace overloads for the types they introduce (`<string>` replaces `print(string)`; a binding adds `print(stringOrRoutine)`). An interpolated string (§1.6.5) prints each segment with the overload for that segment's type.

`print(obj)` on a value whose type derives from `_bglObject` (§21.5.8) calls the value's own `print()` method when it defines one; otherwise it prints the object's short name. A class therefore customizes how its instances print by defining `void print()`.

The core declares `short_name` as a `string` member of `object`: the text (or routine) the article helpers and `printName()` print as the object's name. No binding redeclares it (§23.3.4).

`log()` accepts the same arguments as `print()` and is a debug-only output: it produces output only when the symbol `DEBUG` is defined (§14.2.1). Its arguments are parsed and type-checked in every build, so a release build still diagnoses errors inside a `log()` call.

The article helpers print a world-tree object with an article: `a(obj)` → "a lamp", `cA(obj)` → "A lamp", `the(obj)` → "the lamp", `cThe(obj)` → "The lamp". They are members of `bgl.printRules` (§21.11), so they are written qualified, or `#using bgl.printRules;` (§10.4) brings the short spelling into a file — which is how they read best inside interpolated text. `printName(obj)` → "lamp" prints the bare short name with no article and is a file-scope function.

> **Why they are namespaced.** In Inform 6 `a`, `an`, `the`, `A` and `The` are context-sensitive keywords, meaningful only in the `print (rule) value` slot; they are not I6 identifiers, and an I6 program may freely name a variable or object `the`. Beguile models them as ordinary emitters, which would otherwise put four very common words at file scope in every program.

**Example**

```bgl
extern attribute light;
object lamp { short_name = "brass lamp"; }

void Main() {
    print($"You see {bgl.printRules.a(lamp)}.");   // → You see a brass lamp.
    bgl.printRules.cThe(lamp); print(" glows.");   // → The brass lamp glows.
    log("reached Main");                           // output only with #define DEBUG
}
```

**Notes**

The article forms depend on the active IF library's article and naming conventions (for example the `proper` attribute), which the library, not the core, defines.

**See also** §1.6.5, §14.2.1, §22.3.

## 21.5 IF-Domain Types

### 21.5.1 `attribute` and `attributeList`

**Syntax**

```syntax
attribute ⟨name⟩ ;
extern attribute ⟨name⟩ ;
attributeList attributes = { ⟨attr⟩ [ , !⟨attr⟩ ] … } ;
⟨obj⟩.give( ⟨attr⟩ )   ⟨obj⟩.ungive( ⟨attr⟩ )   ⟨obj⟩.has( ⟨attr⟩ )   ⟨obj⟩.hasnt( ⟨attr⟩ )
```

The `attributeList` form appears in a class or object body; the `!` is literal.

**Description**

`attribute` is the type of a single Inform 6 attribute (`light`, `container`, `static`, …). Attributes are usually bound with `extern attribute` by a library binding (§23.3.4); a program may also declare its own (§11.6).

`attributeList` is the type of a class's or object's `attributes` member: the set of attributes the object initially has. It takes an initializer list. A `!` prefix on an entry negates an inherited attribute, so the object starts *without* it even though its class has it. Only `=` is accepted on `attributes`; `+=` and `-=` are compile-time errors. `attributes = {…}` in an `extend` block is permitted only when the object's declaration has no `attributes` member; the list is additive relative to the class (§11.5.3).

At runtime an object's attributes change through `give()` and `ungive()` and are tested with `has()` and `hasnt()`; all four are defined on `object` and on `attributeList`.

`NO_ATTRIBUTE` is an `attribute`-typed constant meaning "no attribute". It is the only value other than a real attribute that may be assigned to an attribute-valued member; a bare `0` is a type error.

**Example**

```bgl
extern attribute light;
extern attribute scenery;
extern attribute static;

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
property ⟨name⟩ ;
extern property ⟨name⟩ ;
[ extern ] additive property ⟨name⟩ ;
⟨obj⟩.provides( ⟨name⟩ )
```

**Description**

`property` is the type of a free-standing Inform 6 property name: a name in the global property table that is not a member of any Beguile class. Class and object members register as property names automatically, so a `property` declaration is needed only when a name has no class to live on (typical for I6 interop) but must still be usable with `obj.provides(name)` or as a `property`-typed value.

A `property` value is the property identifier, a word-sized value. It supports `=`, `==` and `!=` only; arithmetic on a property identifier is a compile-time error.

The core declares one property itself: `name`, which is `additive` in the Inform 6 compiler. A program must not redeclare it. Every other additive property belongs to a library and is declared by its binding (§23.3.5).

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
dictionaryWord ⟨name⟩ = .⟨word⟩ ;
dictionaryWord ⟨name⟩ = ..⟨word⟩ ;
```

**Description**

`dictionaryWord` is the type of an Inform 6 dictionary word. Literals are written `.word` (singular) or `..word` (plural); the literal syntax is in §1.6.7 and the use of dictionary words in grammar is in §13.4.2.

`print()` on a dictionary word prints the word's text. This applies to a literal, a `dictionaryWord` variable, and an element read from a `rawArray<dictionaryWord>` member alike. Dictionary words compare with `==` and `!=`.

**Example**

```bgl
object sword { rawArray<dictionaryWord> name = { .blade, .sword }; }

void Main() {
    print(sword.name[1]);      // → sword
}
```

**See also** §1.6.7, §13.1.

### 21.5.4 `verb`

`verb` is the core class from which verbs are declared; its members and the whole verb model are specified in §13.2.

### 21.5.5 Grammar Types

| Type | Purpose |
|---|---|
| `patternElement` | The base type of one element of a grammar pattern. A pattern element is a dictionary word, a grammar token, an attribute, or a parser-hook function. |
| `grammarToken` | An `extern enum` of the parser's token names (`noun`, `held`, `creature`, …). It is declared by the library binding, not by the core (§23.3.6). |
| `grammarRule` | One verb-targeted pattern with a priority: `{verb, {pattern}[, priority]}`. |
| `grammarRuleList` | A list of grammar rules; the type of a verb's `grammar` member and of a grammar object. |

These types are the receivers of the grammar operators (`=`, `+=`, `-=`, `replace … =`). Programs rarely name them; the grammar declaration syntax and its rules are in §13.4.

**Example**

```bgl
#include <i6StandardLibrary>

grammar extraLines {
    grammarRule hang = { PutOn, {.hang, held, .on, noun} };   // a verb and a pattern of pattern elements
}
```

### 21.5.6 `bglClass`

**Syntax**

```syntax
⟨obj⟩.is( ⟨class⟩ )
```

**Description**

`bglClass` is the parameter type of `object.is()`, the runtime class test (true when `obj` is an instance of the class or of any subclass). Every registered class — declared with `class Name {…}` or `extern class Name : object {…}` — is type-compatible with `bglClass`, so any class name is accepted as the argument. In default mode the class must be declared; in loose identifier mode (§15.3.3) the name passes through unchecked.

**Example**

```bgl
class Container : object { }
class Box : Container { int weight; }
Box crate;

void Main() {
    if (crate.is(Container)) print("a container");   // true: Box inherits Container
}
```

**See also** §8.6, §15.3.3.

### 21.5.7 `parentProp` and `childrenProp`

**Syntax**

```syntax
⟨obj⟩.parent = ⟨newParent⟩ ;
⟨obj⟩.parent
for ( object ⟨name⟩ in ⟨obj⟩.children ) ⟨statement⟩
⟨obj⟩.children.length()
⟨obj⟩.children.size()
⟨obj⟩.children += { ⟨obj⟩ , … } ;
```

**Description**

`parentProp` is the type of the `parent` member that every `object` has. Assigning to `obj.parent` *moves* the object in the world tree; reading it yields the parent object; `==` and `!=` compare against an object. The member is `typesealed`: an object body may re-initialize `parent` but not change its type (§8.2.8).

`childrenProp` is the type of the `children` member: the collection of an object's direct children. It is iterable with `for … in`, reports its count with `length()` or `size()` (synonyms here: a world-tree collection has no capacity), is populated in an object body with `children = { … }`, and grows at runtime with `+=`. It is a storageless member: it has no slot of its own and reads the world tree through its owner. The placement rules are in §11.5.

**Example**

```bgl
object cave { }
object lamp { parent = cave; }

void Main() {
    lamp.parent = player;                    // move lamp to player
    int n = cave.children.length();          // 0
}
```

**See also** §11.5.

### 21.5.8 `_bglObject`

**Syntax**

```syntax
class ⟨name⟩ : _bglObject { … }
```

**Description**

`_bglObject` is the root base class of the runtime: an empty `emitter class` from which `object`, the primitive wrappers (`int`, `char`, `string`, …), the IF-domain types above and the runtime's own namespace objects derive.

Deriving from `_bglObject` gives a class with stored members **reference semantics**: locals and members of the type hold an identity, not a copy. `object` adds world-tree citizenship (`parent`, `children`, attributes) on top of that. A class with no base is a value class and is copied on assignment. The veneer classes `int`, `bool`, `char` and `string` (§8.2.5) also derive from `_bglObject` but have no stored members, so there is nothing to share and they behave as values. `_bglObject` is never inherited implicitly; a program names it as a base only to obtain reference semantics without the world tree — the window types of `<glulxWindow>` are an example (§22.7.1).

`print(x)` dispatches on `_bglObject` as described in §21.4.

**See also** §2.10, §8.2.

### 21.5.9 `eType` and `typeof()`

**Syntax**

```syntax
typeof( ⟨expr⟩ )
```

**Description**

`typeof(v)` returns the machine category of a value as an `eType`: `unknown` (`0`: `null`/`nothing`, or undeterminable), `int` (any scalar word — `bool`, `char` and enum values report as `int`), `string`, `routine`, `object`, `class`. It is the discriminator for union-typed values; the type semantics, casts and limits are in §2.8.1.

**Example**

```bgl
object lamp { }

void Main() {
    var x = lamp;
    if (typeof(x) == eType.object) print("an object");   // → an object
    bool b = typeof(true) == eType.int;                  // → true: bool reports as int
}
```

**See also** §2.8.

### 21.5.10 `stringOrRoutine`

**Syntax**

```syntax
stringOrRoutine ⟨name⟩ ;
⟨value⟩.isRoutine()
print( ⟨value⟩ ) ;
```

**Description**

`stringOrRoutine` is the named union `string | func<void>`: a value that is either printable text or a routine to run, the classic Inform "string-or-routine" property (`description`, `cant_go`, …). It is provided by the IF library bindings, not by the core: it is in scope whenever a binding is included (§23.3.7). `isRoutine()` reports which the value currently holds. `print(x)` prints the string or runs the routine; the overload carries no library dependency.

**Example**

```bgl
#include <i6StandardLibrary>
void describeLamp() { print("It flickers."); }
object lamp { stringOrRoutine description; }

void Main() {
    lamp.description = "a brass lamp";   // prints the text
    lamp.description = describeLamp;     // runs the routine
    print(lamp.description);
}
```

**See also** §2.8.2, §23.3.7.

## 21.6 Numeric Utilities

### 21.6.1 `uint`

**Syntax**

```syntax
uint ⟨name⟩ [ = ⟨expr⟩ ] ;
(uint) ⟨expr⟩
(int) ⟨expr⟩
```

**Description**

`uint` is an unsigned integer with the same bit pattern and width as `int` (16 bits on the Z-machine, 32 on Glulx). There is no implicit conversion between `int` and `uint` in either direction; a non-negative integer literal converts implicitly, a negative literal only with an explicit `(uint)` cast, so `uint x = -1;` is a compile-time error.

| Operators | Behavior |
|---|---|
| `+` `-` `*` `++` `--` `+=` `-=` `*=` | As for `int`; overflow wraps. |
| `/` `%` `/=` `%=` | Unsigned division and modulo. |
| `==` `!=` | As for `int`. |
| `<` `<=` `>` `>=` | Unsigned ordering: a value with the high bit set is large, not negative. |
| `&` `\|` `^` `&=` `\|=` `^=` `<<` `<<=` | As for `int`. |
| `>>` `>>=` | Logical (zero-fill) right shift. |
| unary `-` | Not defined on `uint`. |

`print(uint)` prints the full unsigned value: `print((uint)-1)` prints `65535` on the Z-machine and `4294967295` on Glulx.

**Example**

```bgl
uint x = 12;          // literal
uint y = (uint)n;     // explicit cast from int
int  z = (int)y;      // explicit cast back
uint w = (uint)-1;    // a negative literal requires the cast
```

**See also** §2.2, §2.4.1.

### 21.6.2 `bgl.util.math`

**Description**

`bgl.util.math` holds the integer helpers that have no operator form.

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

The unsigned helpers are what the `uint` operators use; calling them directly is only needed for unsigned arithmetic on plain `int` values.

**Example**

```bgl
int p = bgl.util.math.pow(2, 8);              // → 256
int c = bgl.util.math.clamp(120, 0, 100);     // → 100
int s = bgl.util.math.sign(-7);               // → -1
```

### 21.6.3 `bgl.util.random`

**Description**

`bgl.util.random` is the runtime's random-number source: a uniform integer in a range, a uniform choice from a list, or a seed for a reproducible sequence.

| Function | Returns | Description |
|---|---|---|
| `bgl.util.random.get(n)` | `int` | A uniformly random value in `1..n`. |
| `bgl.util.random.get(a, b[, c …])` | `var` | One of the listed values, chosen uniformly. Two to eight values may be listed. |
| `bgl.util.random.seed(s)` | `void` | Seed the generator: `s == 0` re-seeds from the environment; `s > 0` selects a deterministic sequence. |

**Example**

```bgl
int d6 = bgl.util.random.get(6);
object prize = bgl.util.random.get(coin, gem, key);
```

**Notes**

> **[Z-machine/Glulx difference]** Both targets accept the same `seed(s)` calls; the underlying mechanism differs but the contract above holds on both.

## 21.7 Character Utilities

**Description**

The `char` type carries classification, case conversion and case-insensitive comparison in the core. All handle the ZSCII extended characters (accented letters, ligatures).

| Member | Returns | Description |
|---|---|---|
| `c.isLower()` / `c.isUpper()` | `bool` | Lowercase / uppercase letter, including accented letters. |
| `c.isAlpha()` | `bool` | Any letter. |
| `c.isNumeric()` | `bool` | A digit `0`..`9`. |
| `c.isAlphaNumeric()` | `bool` | A letter or digit. |
| `c.isVowel()` / `c.isConsonant()` | `bool` | Vowel (including accented vowels) / consonant. |
| `c.toUpper()` / `c.toLower()` | `char` | Case conversion; a character with no case maps to itself. |
| `c =~ d` | `bool` | Case-insensitive equality: `'A' =~ 'a'` is true. |

The `char` comparison and arithmetic operators are in §4.5 and §4.6.

**Example**

```bgl
char c = 'a';
bool v = c.isVowel();        // → true
char u = c.toUpper();        // → 'A'
bool same = c =~ 'A';        // → true
```

## 21.8 `bglAllocated`

**Syntax**

```syntax
class ⟨name⟩ [ ⟨n⟩ ] : object, bglAllocated { … }
⟨instance⟩.copy( ⟨other⟩ ) ;
⟨instance⟩.remaining()
```

The `[ ⟨n⟩ ]` brackets are literal: the pool size of §8.2.6.

**Description**

`bglAllocated` is a mixin for pooled classes (§8.2.6). A pooled class that inherits it gains two operations:

| Method | Returns | Description |
|---|---|---|
| `instance.copy(other)` | `void` | Copies every property of `other` into `instance`. Both must be live pool instances of the same or a compatible pooled class. |
| `instance.remaining()` | `int` | The number of free slots remaining in the class's pool. |

Copying is explicit; assigning one pooled instance to another does not copy.

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
bgl.world.getAll( [ ⟨pred⟩ ] )
bgl.world.inParent( ⟨parent⟩ [ , ⟨pred⟩ ] )
bgl.world.instances( ⟨class⟩ [ , ⟨pred⟩ ] )
```

**Description**

`bgl.world` queries the object tree and returns the matching objects as a tracked `array<object>`, so the `<array>` operations (and `<linq>` chains, when `<linq>` is included) apply to the result.

| Method | Returns |
|---|---|
| `bgl.world.getAll()` | Every object in the program. |
| `bgl.world.inParent(parent)` | The direct children of `parent`. |
| `bgl.world.instances(cls)` | Every object of class `cls` or a subclass. |

Each method takes an optional predicate `func<bool, object>`; only objects for which it returns true are returned. The predicate runs inside the walk, so a selective predicate is preferable to filtering the full result afterwards.

Results live in a shared rotating set of four scratch buffers, each holding `worldBufSize` objects (default 128, §17.4). The number of buffers is fixed. A result is valid until the fourth subsequent query; nested queries inside a `for (o in bgl.world.…)` loop are therefore safe to a depth of three. A result must not be stored across turns; a result that is needed later is copied into a program-declared `array<object>` (assignment copies, §22.4). A walk that would exceed a buffer stops silently at its capacity.

**Example**

```bgl
extern attribute light;
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
bgl.ui.statusBar.height [ = ⟨lines⟩ ]
```

**Description**

The core defines the two root windows of the display as objects under `bgl.ui`. Their members are the same on both targets; an IF library binding replaces the members that the library itself controls (§23.3.8), and `<glulxWindow>` adds the window API to the same objects (§22.7.2).

| Member | Type | Description |
|---|---|---|
| `bgl.ui.mainWin.id` | `int` | The main window handle. `0` until a binding or the program assigns it. |
| `bgl.ui.statusBar.id` | `int` | The status window handle. `0` until assigned; `null` under a binding on the Z-machine, which has no window handles. |
| `bgl.ui.statusBar.height` | `int` | The status window's height in lines. Writing it splits the status window to that height. |

**Example**

```bgl
bgl.ui.statusBar.height = 2;
```

> **[Glulx]** Without a binding, writing `statusBar.height` opens a text-grid window above the root window on first write and re-arranges it thereafter.

> **[Z-machine]** Without a binding, writing `statusBar.height` splits the upper window to that height.

**See also** §22.6, §22.7, §23.3.8.

## 21.11 `bgl.printRules`

**Syntax**

```syntax
$"… {bgl.printRules.⟨rule⟩} …"
$"… {bgl.printRules.img( ⟨image⟩ [ , ⟨align⟩ [ , ⟨width⟩ [ , ⟨height⟩ ] ] ] )} …"
```

**Description**

`bgl.printRules` holds print rules for use inside interpolated strings (§1.6.5). The style rules are value-less emitters that switch the output style at that point in the text; the article rules take the object to print. With `#using bgl.printRules;` the rules are reachable bare, which is how the articles read best in interpolated text.

| Rule | Effect |
|---|---|
| `a(obj)` | The object's short name with an indefinite article — "a lamp". |
| `cA(obj)` | As `a`, capitalized — "A lamp". |
| `the(obj)` | The object's short name with the definite article — "the lamp". |
| `cThe(obj)` | As `the`, capitalized — "The lamp". |
| `bold` | Bold text. |
| `italics` | Italic text (rendered as underline where the target has no italics). |
| `underline` | Underlined text. |
| `reverse` | Reverse video. |
| `fixed` | Fixed-pitch text. |
| `roman` | Return to plain text. |
| `img(image[, align[, width[, height]]])` | Draw an image inline in the main window. **[Glulx]**, and only when `generateBlorb` is true (§17.6). `image` is an `eImages` value; `align` is an `eGlulxImageAlign` (default `inlineCenter`); a `0` dimension is computed from the other, preserving aspect ratio, and `0, 0` draws at natural size. |

**Example**

```bgl
print($"The troll {bgl.printRules.italics}hit{bgl.printRules.roman} the table.");
#using bgl.printRules;
print($"{bold}Warning{roman}");
print($"You cannot open {the(noun)}.");
```

**See also** §1.6.5, §17.6, §22.8.

## 21.12 Utility Types

**Description**

**`bglSize`.** A value class with `int width` and `int height` members and a copying `operator =`. It is the return type of the image-metadata calls in `<glulxImage>` (§22.8) and the argument type of the runtime's scaling helpers.

**Blorb asset enums.** The core declares three enums, empty unless blorb packaging is enabled, and one union over them:

| Type | Contents |
|---|---|
| `eImages` | Picture resource ids. |
| `eSounds` | Sound resource ids. |
| `eUnknownAsset` | Data resource ids: any packaged file that is neither an image nor a sound. |
| `eAssets` | The union `eImages \| eSounds \| eUnknownAsset`, for APIs that accept any resource. |

When `generateBlorb` is true the compiler extends `eImages` and `eSounds` with one member per asset file found (§17.6.1). A value of `eImages` is the raw resource id; with `<glulxImage>` it also answers `width()`, `height()` and `size()` (§22.8).

**See also** §2.7, §2.8, §17.6.1, §22.8.

## 21.13 `bgl.asm`

**Description**

`bgl.asm` is the namespace of direct opcode emitters for the active target. It is an alias
(§10.2) for a target-specific class: on Glulx the Glulx opcode class, on the Z-machine the
Z-machine opcode class; each is available only on its own target. Members are either emitters,
which inline the opcode at the call site, or `static superposed` functions (§3.12), which wrap a
value-returning opcode in a free routine that exists only in programs that call it. Kind **E** below
is an emitter; **S** is a `static superposed` function.

**Example**

```bgl
#using bgl;
int n = asm.random(6);        // Glulx: @random; Z-machine: @random
asm.streamchar('!');          // Glulx only
```

### 21.13.1 Glulx Opcodes

| Member | Signature | Kind | Meaning |
|---|---|---|---|
| `add` / `sub` / `mul` / `div` / `mod` | `int ⟨op⟩(int a, int b)` | E | Arithmetic |
| `neg` | `int neg(int a)` | E | `-a` |
| `bitand` / `bitor` | `int ⟨op⟩(int a, int b)` | E | Bitwise and, or |
| `bitxor` | `int bitxor(int a, int b)` | S | `@bitxor` |
| `bitnot` | `int bitnot(int a)` | E | `~a` |
| `shiftl` | `int shiftl(int a, int b)` | S | Logical shift left |
| `sshiftr` | `int sshiftr(int a, int b)` | S | Arithmetic shift right |
| `ushiftr` | `int ushiftr(int a, int b)` | S | Logical shift right |
| `aload` / `aloads` / `aloadb` / `aloadbit` | `int ⟨op⟩(int addr, int idx)` | S | Load word, short, byte, bit |
| `astore` / `astores` / `astoreb` / `astorebit` | `void ⟨op⟩(int addr, int idx, int v)` | E | Store word, short, byte, bit |
| `copy` | `int copy(int v)` | E | `@copy` |
| `sexs` / `sexb` | `int ⟨op⟩(int v)` | S | Sign-extend short, byte |
| `stkcount` | `int stkcount()` | S | `@stkcount` |
| `stkpeek` | `int stkpeek(int idx)` | S | `@stkpeek` |
| `stkswap` | `void stkswap()` | E | `@stkswap` |
| `stkroll` | `void stkroll(int count, int dir)` | E | `@stkroll` |
| `stkcopy` | `void stkcopy(int count)` | E | `@stkcopy` |
| `callf` / `callfi` / `callfii` / `callfiii` | `int ⟨op⟩(int addr, …)` | S | Call with 0–3 arguments |
| `streamchar` / `streamnum` / `streamstr` / `streamunichar` | `void ⟨op⟩(int v)` | E | Stream output |
| `getstringtbl` | `int getstringtbl()` | S | `@getstringtbl` |
| `setstringtbl` | `void setstringtbl(int addr)` | E | `@setstringtbl` |
| `getmemsize` | `int getmemsize()` | S | `@getmemsize` |
| `setmemsize` | `int setmemsize(int size)` | S | `@setmemsize` |
| `malloc` | `int malloc(int size)` | S | Heap allocate |
| `mfree` | `void mfree(int addr)` | E | `@mfree` |
| `mzero` | `void mzero(int count, int addr)` | E | `@mzero` |
| `mcopy` | `void mcopy(int count, int src, int dst)` | E | `@mcopy` |
| `linearsearch` / `binarysearch` | `int ⟨op⟩(int key, int keySize, int start, int structSize, int numStructs, int keyOffset, int options)` | S | Table search |
| `linkedsearch` | `int linkedsearch(int key, int keySize, int start, int keyOffset, int nextOffset, int options)` | S | `@linkedsearch` |
| `random` | `int random(int range)` | S | `@random` |
| `setrandom` | `void setrandom(int seed)` | E | `@setrandom` |
| `quit` / `restart` | `void ⟨op⟩()` | E | `@quit`, `@restart` |
| `save` / `restore` | `int ⟨op⟩(int stream)` | S | `@save`, `@restore` |
| `saveundo` / `restoreundo` | `int ⟨op⟩()` | S | `@saveundo`, `@restoreundo` |
| `protect` | `void protect(int addr, int len)` | E | `@protect` |
| `verify` | `int verify()` | S | `@verify` |
| `debugtrap` | `void debugtrap(int val)` | E | `@debugtrap` |
| `glk` | `int glk(int sel, int argc)` | E | Raw Glk dispatch |
| `setiosys` | `void setiosys(int mode, int rock)` | E | `@setiosys` |
| `numtof` | `int numtof(int n)` | S | Integer to float bits |
| `ftonumz` / `ftonumn` | `int ⟨op⟩(int f)` | S | Float to integer, truncating / rounding |
| `fadd` / `fsub` / `fmul` / `fdiv` | `int ⟨op⟩(int a, int b)` | S | Float arithmetic |
| `ceil` / `floor` / `sqrt` / `exp` / `log` | `int ⟨op⟩(int f)` | S | Float unary functions |
| `pow` | `int pow(int a, int b)` | S | `@pow` |
| `sin` / `cos` / `tan` / `asin` / `acos` / `atan` | `int ⟨op⟩(int f)` | S | Float trigonometry |
| `atan2` | `int atan2(int a, int b)` | S | `@atan2` |
| `accelfunc` | `void accelfunc(int idx, int addr)` | E | `@accelfunc` |
| `accelparam` | `void accelparam(int idx, int val)` | E | `@accelparam` |

### 21.13.2 Glk Calls

Each of these is an emitter that performs the named Glk call; the member name is the Glk function
name in camel case without the `glk_` prefix (`windowOpen` is `glk_window_open`). Parameters are
`int` and follow the Glk argument order unless noted.

| Member | Signature | Glk call |
|---|---|---|
| `exit` | `void exit()` | `glk_exit` |
| `tick` | `void tick()` | `glk_tick` |
| `gestalt` | `int gestalt(int a, int b)` | `glk_gestalt` |
| `gestaltExt` | `int gestaltExt(int a, int b, int c, int d)` | `glk_gestalt_ext` |
| `windowIterate` | `int windowIterate(int a, int b)` | `glk_window_iterate` |
| `windowGetRock` | `int windowGetRock(int a)` | `glk_window_get_rock` |
| `windowGetRoot` | `int windowGetRoot()` | `glk_window_get_root` |
| `windowOpen` | `int windowOpen(int splitWinId, eGlulxWindowType type, int size, bGlulxWindowMethodFlags winMethod = noBorder, int winRock = 0)` | `glk_window_open` (arguments reordered to Glk order) |
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
| `charToLower` / `charToUpper` | `int ⟨op⟩(int a)` | `glk_char_to_lower`, `glk_char_to_upper` |
| `stylehintSet` | `void stylehintSet(int a, int b, int c, int d)` | `glk_stylehint_set` |
| `stylehintClear` | `void stylehintClear(int a, int b, int c)` | `glk_stylehint_clear` |
| `styleDistinguish` | `int styleDistinguish(int a, int b, int c)` | `glk_style_distinguish` |
| `styleMeasure` | `int styleMeasure(int a, int b, int c, int d)` | `glk_style_measure` |
| `select` / `selectPoll` | `void ⟨op⟩(int a)` | `glk_select`, `glk_select_poll` |
| `requestLineEvent` | `void requestLineEvent(int a, int b, int c, int d)` | `glk_request_line_event` |
| `cancelLineEvent` | `void cancelLineEvent(int a, int b)` | `glk_cancel_line_event` |
| `requestCharEvent` / `cancelCharEvent` | `void ⟨op⟩(int a)` | `glk_request_char_event`, `glk_cancel_char_event` |
| `requestMouseEvent` / `cancelMouseEvent` | `void ⟨op⟩(int a)` | `glk_request_mouse_event`, `glk_cancel_mouse_event` |
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
| `schannelStop` / `schannelPause` / `schannelUnpause` | `void ⟨op⟩(int a)` | `glk_schannel_stop`, `glk_schannel_pause`, `glk_schannel_unpause` |
| `schannelSetVolume` / `schannelSetVolumeExt` | `void schannelSetVolume(int a, int b)` / `void schannelSetVolumeExt(int a, int b, int c, int d)` | `glk_schannel_set_volume`, `glk_schannel_set_volume_ext` |
| `soundLoadHint` | `void soundLoadHint(int a, int b)` | `glk_sound_load_hint` |
| `setHyperlink` / `setHyperlinkStream` | `void setHyperlink(int a)` / `void setHyperlinkStream(int a, int b)` | `glk_set_hyperlink`, `glk_set_hyperlink_stream` |
| `requestHyperlinkEvent` / `cancelHyperlinkEvent` | `void ⟨op⟩(int a)` | `glk_request_hyperlink_event`, `glk_cancel_hyperlink_event` |
| `bufferToLowerCaseUni` / `bufferToUpperCaseUni` | `int ⟨op⟩(int a, int b, int c)` | `glk_buffer_to_lower_case_uni`, `glk_buffer_to_upper_case_uni` |
| `bufferToTitleCaseUni` | `int bufferToTitleCaseUni(int a, int b, int c, int d)` | `glk_buffer_to_title_case_uni` |
| `bufferCanonDecomposeUni` / `bufferCanonNormalizeUni` | `int ⟨op⟩(int a, int b, int c)` | `glk_buffer_canon_decompose_uni`, `glk_buffer_canon_normalize_uni` |
| `putCharUni` / `putStringUni` | `void ⟨op⟩(int a)` | `glk_put_char_uni`, `glk_put_string_uni` |
| `putBufferUni` | `void putBufferUni(int a, int b)` | `glk_put_buffer_uni` |
| `putCharStreamUni` / `putStringStreamUni` | `void ⟨op⟩(int a, int b)` | `glk_put_char_stream_uni`, `glk_put_string_stream_uni` |
| `putBufferStreamUni` | `void putBufferStreamUni(int a, int b, int c)` | `glk_put_buffer_stream_uni` |
| `getCharStreamUni` | `int getCharStreamUni(int a)` | `glk_get_char_stream_uni` |
| `getBufferStreamUni` / `getLineStreamUni` | `int ⟨op⟩(int a, int b, int c)` | `glk_get_buffer_stream_uni`, `glk_get_line_stream_uni` |
| `streamOpenFileUni` | `int streamOpenFileUni(int a, int b, int c)` | `glk_stream_open_file_uni` |
| `streamOpenMemoryUni` | `int streamOpenMemoryUni(int a, int b, int c, int d)` | `glk_stream_open_memory_uni` |
| `streamOpenResourceUni` | `int streamOpenResourceUni(int a, int b)` | `glk_stream_open_resource_uni` |
| `requestCharEventUni` | `void requestCharEventUni(int a)` | `glk_request_char_event_uni` |
| `requestLineEventUni` | `void requestLineEventUni(int a, int b, int c, int d)` | `glk_request_line_event_uni` |
| `setEchoLineEvent` | `void setEchoLineEvent(int a, int b)` | `glk_set_echo_line_event` |
| `setTerminatorsLineEvent` | `void setTerminatorsLineEvent(int a, int b, int c)` | `glk_set_terminators_line_event` |
| `currentTime` | `void currentTime(int a)` | `glk_current_time` |
| `currentSimpleTime` | `int currentSimpleTime(int a)` | `glk_current_simple_time` |
| `timeToDateUtc` / `timeToDateLocal` | `void ⟨op⟩(int a, int b)` | `glk_time_to_date_utc`, `glk_time_to_date_local` |
| `simpleTimeToDateUtc` / `simpleTimeToDateLocal` | `void ⟨op⟩(int a, int b, int c)` | `glk_simple_time_to_date_utc`, `glk_simple_time_to_date_local` |
| `dateToTimeUtc` / `dateToTimeLocal` | `void ⟨op⟩(int a, int b)` | `glk_date_to_time_utc`, `glk_date_to_time_local` |
| `dateToSimpleTimeUtc` / `dateToSimpleTimeLocal` | `int ⟨op⟩(int a, int b)` | `glk_date_to_simple_time_utc`, `glk_date_to_simple_time_local` |
| `garglkSetZcolors` / `garglkSetZcolorsStream` | `void garglkSetZcolors(int a, int b)` / `void garglkSetZcolorsStream(int a, int b, int c)` | `garglk_set_zcolors`, `garglk_set_zcolors_stream` |
| `garglkSetReversevideo` / `garglkSetReversevideoStream` | `void garglkSetReversevideo(int a)` / `void garglkSetReversevideoStream(int a, int b)` | `garglk_set_reversevideo`, `garglk_set_reversevideo_stream` |

### 21.13.3 Z-machine Opcodes

| Member | Signature | Kind | Meaning |
|---|---|---|---|
| `add` / `sub` / `mul` / `div` / `mod` | `int ⟨op⟩(int a, int b)` | E | Arithmetic |
| `bitor` / `bitand` | `int ⟨op⟩(int a, int b)` | E | Bitwise or, and |
| `bitnot` | `int bitnot(int a)` | E | `~a` |
| `loadw` / `loadb` | `int ⟨op⟩(int arr, int idx)` | S | `@loadw`, `@loadb` |
| `storew` / `storeb` | `void ⟨op⟩(int arr, int idx, int v)` | E | `@storew`, `@storeb` |
| `push` | `void push(int v)` | E | `@push` |
| `pull` | `int pull()` | S | `@pull` |
| `getParent` / `getChild` / `getSibling` | `int ⟨op⟩(int obj)` | S | `@get_parent`, `@get_child`, `@get_sibling` |
| `insertObj` | `void insertObj(int obj, int dst)` | E | `@insert_obj` |
| `removeObj` | `void removeObj(int obj)` | E | `@remove_obj` |
| `printObj` | `void printObj(int obj)` | E | `@print_obj` |
| `getProp` / `getPropAddr` / `getNextProp` | `int ⟨op⟩(int obj, int prop)` | S | `@get_prop`, `@get_prop_addr`, `@get_next_prop` |
| `getPropLen` | `int getPropLen(int propAddr)` | S | `@get_prop_len` |
| `putProp` | `void putProp(int obj, int prop, int v)` | E | `@put_prop` |
| `setAttr` / `clearAttr` | `void ⟨op⟩(int obj, int attr)` | E | `@set_attr`, `@clear_attr` |
| `inc` / `dec` | `void ⟨op⟩(int variable)` | E | `@inc`, `@dec` |
| `printChar` / `printNum` / `printAddr` / `printPaddr` | `void ⟨op⟩(int v)` | E | `@print_char`, `@print_num`, `@print_addr`, `@print_paddr` |
| `newLine` | `void newLine()` | E | `@new_line` |
| `splitWindow` | `void splitWindow(int lines)` | E | `@split_window` |
| `setWindow` | `void setWindow(int win)` | E | `@set_window` |
| `outputStream` | `void outputStream(int n)` / `void outputStream(int n, int table)` | E | `@output_stream` |
| `inputStream` | `void inputStream(int n)` | E | `@input_stream` |
| `random` | `int random(int range)` | S | `@random` |
| `quit` / `restart` | `void ⟨op⟩()` | E | `@quit`, `@restart` |
| `verify` | `int verify()` | E | `@verify` |
| `soundEffect` | `void soundEffect(int num, int effect, int vol)` | E | `@sound_effect` |

The following members do not exist on a Z3 target — that is, when the `target` setting is anything
but `z3` (§17.7).

| Member | Signature | Kind | Meaning |
|---|---|---|---|
| `eraseWindow` | `void eraseWindow(int win)` | E | `@erase_window` |
| `eraseLine` | `void eraseLine(int v)` | E | `@erase_line` |
| `setCursor` | `void setCursor(int line, int col)` | E | `@set_cursor` |
| `getCursor` | `void getCursor(int arr)` | E | `@get_cursor` |
| `setTextStyle` | `void setTextStyle(int style)` | E | `@set_text_style` |
| `bufferMode` | `void bufferMode(int flag)` | E | `@buffer_mode` |
| `readChar` | `int readChar(int dev = 1)` | S | `@read_char` |
| `scanTable` | `int scanTable(int x, int table, int len)` | S | `@scan_table` |
| `call1s` / `call2s` | `int call1s(int routine)` / `int call2s(int routine, int a)` | S | `@call_1s`, `@call_2s` |
| `logShift` / `artShift` | `int ⟨op⟩(int a, int places)` | S | `@log_shift`, `@art_shift` |
| `copyTable` | `void copyTable(int src, int dst, int size)` | E | `@copy_table` |
| `printUnicode` | `void printUnicode(int ch)` | E | `@print_unicode` |
| `setColour` | `void setColour(int fg, int bg)` | E | `@set_colour` |
| `setFont` | `int setFont(int font)` | S | `@set_font` |
| `tokenise` | `void tokenise(int text, int parse)` | E | `@tokenise` |
| `encodeText` | `void encodeText(int zscii, int len, int from, int coded)` | E | `@encode_text` |
| `printTable` | `void printTable(int zscii, int width)` | E | `@print_table` |
| `save` / `restore` | `int ⟨op⟩(int table, int bytes, int name)` | S | `@save`, `@restore` |
| `saveUndo` / `restoreUndo` | `int ⟨op⟩()` | S | `@save_undo`, `@restore_undo` |
| `call1n` / `call2n` | `void call1n(int routine)` / `void call2n(int routine, int a)` | E | `@call_1n`, `@call_2n` |
| `checkUnicode` | `int checkUnicode(int ch)` | S | `@check_unicode` |

**See also** §3.12; §10.2; §22.7.

---

# 22 Language Extensions

## 22.1 Overview

An *extension* is a file in the `beguiLib` folder that a program enables with `#include <name>` (§14.1.1). Extensions build on the runtime core (§21) and are library-agnostic: each works with any IF library binding or with none. Nothing in an extension is available until it is included; `<array>` is the exception because the core includes it itself (§12.1).

| Extension | Include | Adds | Requires `bglInit()` | Also includes | Target |
|---|---|---|---|---|---|
| Tracked character buffers | `#include <buf>` | `array<char>` length tracking, `bgl.util.buf` | yes (length headers of sized arrays) | — | both |
| Strings | `#include <string>` | content comparison on `string`; the `stringObj` type | yes | `<buf>` | both |
| Arrays | loaded by the core | search, mutation, deque and sort methods on `array<T>`; copy-on-assign | yes (length headers of sized arrays) | — | both |
| LINQ chains | `#include <linq>` | fluent chain operations on `array<T>` | yes | `<array>` | both |
| Key input | `#include <ui>` | `bgl.ui.waitForKey()`, `hideCursor()`, `showCursor()` | no | — | both |
| Glulx windows | `#include <glulxWindow>` | window types, splitting, sizing, styles, colors | no | `<glulxImage>` when `generateBlorb` is true | Glulx |
| Glulx images | `#include <glulxImage>` | the `glulxImage` handle; `eImages` metadata | no | — | Glulx |

Including an extension more than once is harmless. The names `uint`, the `char` utilities, `bgl.util.math`, `bgl.util.random`, `bglAllocated` and `bgl.world` are part of the core and need no include (§21).

Every entry below has the same shape: purpose, include line, what it adds, the settings it reads, and notes.

## 22.2 `<buf>`

**Purpose.** Length-tracked character buffers. With `<buf>` included, a sized `array<char>` is a *tracked buf*: it records its capacity and its current length, and the buffer operations below apply to it.

**Include**

```bgl
#include <buf>
```

**Description**

A tracked buf's value behaves as a standard I6 hybrid buffer — the length word first, then the characters — so it can be passed directly to I6 library routines that expect one (`print_to_array`, `glk_put_buffer`, …). `buf[i]` reads and writes character `i` (§12.4). Length and capacity are read and written through the methods below. Writing `buf[i]` does not change the length.

An `array<char>` that is not tracked (an `extern` I6 array, or one created without the tracked layout) answers `size()` and `length()` from the buffer's length word (§12.4), and `isTracked()` returns false.

**Methods on `array<char>`**

| Method | Returns | Description |
|---|---|---|
| `buf.size()` | `int` | Capacity in characters; `-1` for an untracked buffer. |
| `buf.length()` | `int` | Current number of characters. |
| `buf.setLength(n)` | `void` | Set the current length, limited to `size()` on a tracked buffer. |
| `buf.isTracked()` | `bool` | True for a tracked buf. |

**Buffer operations, `bgl.util.buf`**

Every operation takes the buffer as its first argument. Operations that return `array<char>` return the buffer they modified.

| Operation | Returns | Description |
|---|---|---|
| `set(buf, value)` | `array<char>` | Replace the contents with a string literal or another buffer; sets the length. |
| `copy(to, from, n[, toPos[, fromPos]])` | `array<char>` | Copy `n` characters (`-1`: to the end of `from`) from `from[fromPos]` to `to[toPos]`; bounded by both buffers. |
| `append(to, from)` / `prepend(to, from)` | `array<char>` | Add `from`'s contents at the end / start of `to`. |
| `insert(to, from, pos[, count])` | `array<char>` | Insert `from` (or its first `count` characters) at `pos`. |
| `delete(buf, pos, count)` | `array<char>` | Remove `count` characters at `pos`, closing the gap. |
| `mid(to, from, fromPos, n)` / `left(to, from, n)` / `right(to, from, n)` | `array<char>` | Copy a substring of `from` into `to`; `n == -1` in `mid` means to the end. |
| `indexOf(buf, search[, start])` | `int` | Position of the first occurrence at or after `start`, or `-1`. |
| `indexOfFirstTrue(buf, pred[, start])` / `indexOfFirstFalse` | `int` | Position of the first character for which `pred(c)` is true / false, or `-1`. `pred` is `func<bool, char>`. |
| `replace(buf, search, repl)` / `replaceAll(buf, search, repl[, start])` | `array<char>` | Replace the first / every occurrence in place. |
| `equals(a, b[, caseInsensitive])` | `bool` | Content equality. |
| `compare(a, b[, caseInsensitive])` | `int` | `-1`, `0` or `1`, by character code; a shorter buffer that matches sorts first. |
| `startsWith(buf, prefix[, caseInsensitive])` / `endsWith(buf, suffix[, caseInsensitive])` | `bool` | Prefix / suffix test. |
| `toUpper(buf)` / `toLower(buf)` / `reverse(buf)` | `array<char>` | In-place transforms. |
| `trim(buf)` / `trimLeft(buf)` / `trimRight(buf)` | `array<char>` | Remove leading and/or trailing spaces in place. |
| `getChar(buf, pos)` / `setChar(buf, pos, c)` | `int` / `void` | Bounds-checked character access; `getChar` returns `-1` out of range. |
| `print(buf[, len])` | `array<char>` | Print the first `len` characters, or all when `len` is omitted or `-1`. |
| `capture(buf[, maxBytes])` | `void` | Redirect subsequent `print` output into `buf`. Captures nest to a depth of 16. |
| `release()` | `array<char>` | End the most recent capture, restore the previous output target and set the buffer's length to what was written. |
| `stackLen()` | `int` | The current capture depth. |
| `isTracked(buf)` / `size(buf)` / `length(buf)` / `setLength(buf, n)` | | The function forms of the methods above. |

**Example**

```bgl
#include <buf>
array<char> line[64];

void Main() {
    bglInit();
    bgl.util.buf.set(line, "hello");
    bgl.util.buf.append(line, " world");
    bgl.util.buf.toUpper(line);
    bgl.util.buf.print(line);        // → HELLO WORLD
    print(line.length());            // → 11
}
```

**Settings**

`bglStringDefaultSize` (default `500`) is the capacity used for capture into an untracked buffer and for the pool buffers of `<string>`. It is an I6 constant, set before the include:

```bgl
#i6 { Constant bglStringDefaultSize 800; }
#include <buf>
```

**Notes**

Requires `bglInit()`, which writes the length headers of sized tracked buffers (§21.2). `compare()` orders by character code, not by locale: on the Z-machine every uppercase letter sorts before every lowercase one; pass `caseInsensitive` for dictionary order.

> **[Glulx]** Capture uses a Glk memory stream. `<buf>` supplies the constants it needs (`filemode_Write`, `gg_arguments`) when no IF library defines them.

**See also** §12.4, §22.3.

## 22.3 `<string>`

**Purpose.** Content semantics for text: comparison and ordering by content on `string`, and a second type, `stringObj`, that owns a mutable buffer.

**Include**

```bgl
#include <string>        // includes <buf>
```

**Description**

Two types, for two different things:

| Type | The slot holds | Mutable | Lifecycle |
|---|---|---|---|
| `string` | a reference to static text (a literal) | no | none; nothing is owned |
| `stringObj` | a buffer of its own, taken from the string pool | yes | allocated on declaration, released at scope exit |

A string literal is a `string`. Use `string` for text that is only read and `stringObj` for text that is built or changed. Both compare, order, `switch` and print by content, and they mix freely in expressions. Every operation that *produces* text returns a `stringObj`, so a `stringObj` is what must receive it. Assigning a `string` or a literal to a `stringObj` copies the text into the object's own buffer; assigning a `stringObj` to a `string` is a compile-time error, because the `string` would alias a buffer it does not own. `stringObj a = b;` copies `b`'s value; `ref stringObj a := b;` binds a reference (§3.7).

`s == null` and `s != null` remain identity tests on both types (there is no content to compare against `null`).

**Operators (both types unless noted)**

| Operator | Description |
|---|---|
| `s = "text"` / `s = other` | Assign. On a `string` the slot now refers to the text; on a `stringObj` the text is copied into its buffer. `stringObj` also accepts an interpolated string, capturing its output. |
| `s + v` | Concatenation; `v` is a literal or a string of the same type. Returns a new `stringObj`; `s` is unchanged. |
| `s += v` | **`stringObj` only.** Append in place. |
| `s == v` `s != v` `s < v` `s <= v` `s > v` `s >= v` | Content comparison and lexicographic ordering by character code. |
| `switch (s) { case "a": … }` | Content comparison. |
| `s[i]` | The character at position `i`. |
| `s[i] = c` | **`stringObj` only.** Replace the character at position `i`. |
| `s?` | True when the slot is not `null` (§4.10). |

**Methods (both types)**

| Method | Returns | Description |
|---|---|---|
| `s.print()` | `void` | Print the text. |
| `s.append(v)` | `stringObj` | `s` followed by a string or a `char`. |
| `s.prepend(v)` | `stringObj` | `v` followed by `s`. |
| `s.toUpper()` / `s.toLower()` | `stringObj` | Case conversion. |
| `s.trim()` / `s.trimLeft()` / `s.trimRight()` | `stringObj` | Without leading and/or trailing spaces. |
| `s.reverse()` | `stringObj` | Characters in reverse order. |
| `s.mid(start, count)` / `s.left(count)` / `s.right(count)` | `stringObj` | Substrings. |
| `s.insert(pos, src)` | `stringObj` | With `src` inserted at `pos`. |
| `s.delete(pos, count)` | `stringObj` | With `count` characters removed at `pos`. |
| `s.replace(search, repl)` / `s.replaceAll(search, repl)` | `stringObj` | With the first / every occurrence replaced. |
| `s.format(pattern[, p1[, p2]])` | `stringObj` | `pattern` with `$0` replaced by `s` and `$1`, `$2` by the arguments. |
| `s.getLength()` | `int` | Number of characters. |
| `s.compareTo(other[, caseInsensitive])` | `int` | `-1`, `0` or `1`; the form a sort comparator needs. |
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
    if (name.startsWith("Cloak")) print(name.toUpper());   // → CLOAK OF DARKNESS!
}
```

**Settings**

The pool holds `bglStringPoolReserve` string objects (default `10`); every live `stringObj` occupies one, including each `stringObj` member of each object instance. Exhausting the pool is a runtime error. Each buffer holds `bglStringDefaultSize` characters (default `500`, §22.2). Both are I6 constants, set before the include:

```bgl
#i6 { Constant bglStringPoolReserve 64; }
#include <string>
```

`bglStringPoolReserve` is unrelated to `framePoolSize` (§17.4), which sizes the Z-machine local-variable overflow pool.

**Notes**

Requires `bglInit()`, which initializes the pool. `print(string)` is replaced by a printer that accepts a literal, a `stringObj`, a buffer or a routine (which it runs). For use of `stringObj` as an array element type — the slot allocates on first write and is released when the element is dropped — see §12.10.

**See also** §2.2, §12.10, §21.2, §22.2.

## 22.4 `<array>`

**Purpose.** Searching, mutation, deque and sort operations on `array<T>`, and value-semantic assignment.

**Include**

```bgl
#include <array>         // optional: the core includes it (§12.1)
```

**Description**

The runtime core loads `<array>` automatically, so an explicit `#include <array>` is never required; the built-in part of the surface is subscripting, `size()` and `length()` (§12.3). `<array>` adds the methods below and makes `dst = src` copy the elements of `src` into `dst` (clamped to `dst`'s capacity) and set `dst`'s length, rather than alias the array. Copy-on-assign is the capture mechanism for a returned local array and for a chain result (§22.5).

Methods that take an element (`indexOf`, `contains`, `append`, …) are type-checked against `T`: an argument of an incompatible type is a compile-time error. Where a method needs an operation of `T` — equality, ordering, assignment, release — it uses the one `T` publishes, or the plain word semantics when `T` publishes none; the contract is specified in §12.10.

**Methods**

| Method | Returns | Description |
|---|---|---|
| `length()` | `int` | The number of elements in use. It is set at allocation (the element count for a list initializer, `0` for a sized declaration) and changed only by the operations below. On an untracked `extern` array it returns `size()`. |
| `setLength(n)` | `void` | Set the length. A negative `n` is a runtime error. No effect on an untracked array. |
| `isTracked()` | `bool` | True for a Beguile-declared array with length tracking; false for an I6-native `extern` array. |
| `indexOf(item)` / `find(item)` | `int` | First index of `item` in `0..length()-1`, or `-1`. |
| `contains(item)` | `bool` | True when `item` is present. |
| `clear()` | `void` | Zero every slot up to `size()`, releasing owned elements, and set the length to `0`. |
| `swap(pos1, pos2)` | `void` | Exchange two elements. Indices must be in range. |
| `reverse()` | `void` | Reverse the elements `0..length()-1` in place. |
| `append(item)` | `bool` | Add at position `length()`. False when the array is full. |
| `prepend(item)` | `bool` | Insert at position `0`, shifting the rest right. False when full. |
| `insert(pos, item)` | `bool` | Insert at `pos` (`0..length()`), shifting the rest right. False when full or `pos` is out of range. |
| `remove(pos)` | `void` | Remove the element at `pos`, shifting the rest left. Out of range: no effect. |
| `removeValue(item)` | `void` | Remove every element equal to `item`. |
| `arr += item` / `arr += { a, b }` | `void` | Append one element, or each element of a list. |
| `arr -= item` / `arr -= { a, b }` | `void` | `removeValue` for one element, or for each element of a list. |
| `push(item)` | `void` | Insert at the front (position `0`). |
| `pop()` | `T` | Remove and return the front element; `0` when empty. |
| `peek()` | `T` | The front element without removing it; `0` when empty. |
| `enqueue(item)` | `void` | Add at the back. |
| `dequeue()` | `T` | Remove and return the front element; `0` when empty. |
| `peekEnd()` | `T` | The back element without removing it; `0` when empty. |
| `popEnd()` | `T` | Remove and return the back element; `0` when empty. |
| `sort()` | `void` | Sort `0..length()-1` ascending in place, by `T`'s ordering operator or, when it has none, by signed word value. |
| `sort(compare)` | `void` | Sort with a comparator `func<int, T, T>` returning `-1`, `0` or `1`. |

`push`, `peek` and `pop` operate at the front of the array; `enqueue`, `peekEnd` and `popEnd` at the back. The sort is stable.

**Example**

```bgl
array<int> scores[8];

void Main() {
    bglInit();
    scores += { 40, 10, 30 };
    scores.append(20);
    scores.sort();                       // 10 20 30 40
    print(scores.indexOf(30));           // → 2
    scores.sort((int a, int b) => b - a);   // descending
    int top = scores.pop();              // 40
}
```

**Notes**

Requires `bglInit()`, which writes the length headers of sized tracked arrays (§21.2). Writing `arr[i] = v` never changes the length.

**See also** §12.3, §12.10, §22.5.

## 22.5 `<linq>`

**Purpose.** Chainable, LINQ-style transformations on `array<T>`.

**Include**

```bgl
#include <linq>          // includes <array>
```

**Description**

Operations are *non-terminals*, which return a typed array and may be chained further, and *terminals*, which reduce a chain to one value. Calling a chain operation without `<linq>` is a compile-time error.

| Method | Returns | Description |
|---|---|---|
| `filter(pred)` | `array<T>` | The elements for which `pred(elem)` is true. `pred` is `func<bool, T>`. |
| `map(f)` | `array<var>` | `f(elem)` for each element. `f` is `func<var, T>`; the result is `array<var>` because the mapper's result type is not tracked. |
| `take(n)` / `skip(n)` | `array<T>` | The first `n` elements / all but the first `n`. `n` is limited to `0..length()`. |
| `takeWhile(pred)` / `skipWhile(pred)` | `array<T>` | Elements up to the first for which `pred` is false / from that element on. |
| `distinct()` | `array<T>` | The first occurrence of each value. |
| `orderBy()` | `array<T>` | A sorted copy, by signed word value; the source is unchanged. |
| `orderBy(compare)` | `array<T>` | A sorted copy using `func<int, T, T>`. |
| `first()` / `last()` | `T` | The first / last element; `0` when empty. |
| `count()` | `int` | The same as `length()`. |
| `any(pred)` | `bool` | True when some element satisfies `pred`; false on an empty array. |
| `all(pred)` | `bool` | True when every element satisfies `pred`; true on an empty array. |

**Chain results.** A non-terminal's result lives in a scratch buffer that the next chain reuses. Consume it in the same statement, reduce it with a terminal, or capture it by assigning it to a typed array, which copies it (§22.4). Do not return a chain result from a function whose source is a local array (§12.6).

**Nesting.** A chain may run inside another chain's predicate or mapper — `arr.filter((Room r) => r.exits.any(isOpen))` — to a depth of two (a chain inside a chain). Deeper nesting is a runtime error that ends the program.

**Example**

```bgl
#include <linq>
array<int> nums = { 3, -1, 4, -1, 5 };

void Main() {
    bglInit();
    array<int> positives = nums.filter((int x) => x > 0).distinct();   // captured: 3 4 5
    print(nums.map((var y) => y * 2).first());                          // → 6
}
```

**Settings**

`linqScratchSize` (§17.4, default `32`) is the capacity of each scratch buffer. A chain step whose result would exceed it is a runtime error that ends the program.

**Notes**

Requires `bglInit()`, which prepares the scratch buffers. `orderBy()` with no comparator orders by signed word value even when `T` publishes an ordering operator; pass `compare` for content-ordered element types such as `string`.

**See also** §4.14, §12.6, §17.4, §22.4.

## 22.6 `<ui>`

**Purpose.** Single-key input and cursor placement, on both targets, through `bgl.ui`.

**Include**

```bgl
#include <ui>
```

**Description**

| Member | Returns | Description |
|---|---|---|
| `bgl.ui.waitForKey([separator])` | `char` | Wait for a key press and return it. When `separator` (a `string`) is given, it is printed, followed by a blank line, before waiting. |
| `bgl.ui.hideCursor()` | `int` | Move the text cursor out of the prose ahead of a key read: to the last cell of the status window when `bgl.ui.statusBar.height` is positive. Returns the window the caller should request input on (Glulx), or `0` when the main window should be used. |
| `bgl.ui.showCursor()` | `void` | Undo `hideCursor()`. On the Z-machine, returns output to the main window; on Glulx there is nothing to undo. |

`waitForKey()` calls `hideCursor()` and `showCursor()` itself.

**Example**

```bgl
#include <ui>

void Main() {
    char k = bgl.ui.waitForKey("[Press any key]");
    if (k == 'q') return;
}
```

**Notes**

Does not require `bglInit()`.

> **[Glulx]** `waitForKey()` requests a character event on the status window when `statusBar.height` is positive and `statusBar.id` is a real window, otherwise on `bgl.ui.mainWin.id`; it discards other events until the key arrives. `mainWin.id` must therefore hold the main window's handle, which a binding provides (§23.3.8).

> **[Z-machine]** `hideCursor()` switches to the upper window and positions the cursor at its last row and column; `waitForKey()` reads with the character-input opcode and then switches back.

**See also** §21.10, §23.3.8.

## 22.7 `<glulxWindow>`

**Purpose.** Typed access to the Glk window tree: window types with compile-time subtype safety, splitting, sizing, image drawing, styles and colors.

**Include**

```bgl
#include <glulxWindow>   // includes <glulxImage> when generateBlorb is true
```

> **[Glulx]** This extension is Glulx-only. Including it in a Z-machine build is a compile-time error; guard the include and all window code with `#if TARGET_GLULX` in a source shared between targets.

**Description**

Glk arranges the screen as a binary tree of windows: a window is never resized directly; instead an existing window is *split* to create a child. The extension models each window as a reference-semantic object (§21.5.8) and gives a graphics-only or grid-only operation on the wrong kind of window a compile-time error.

### 22.7.1 Window Types

**Description**

| Type | Kind | Members beyond `window` |
|---|---|---|
| `window` | base | `id`, `width`, `height`, `close()`, the split family (§22.7.3), the move family (§22.7.4), `measureStyle()`, `styleHonored()` (§22.7.9) |
| `textBufferWindow` | scrolling prose | `drawImage(img, align, …)` (§22.7.5), `setStyle()`, `clearStyle()` (§22.7.7) |
| `textGridWindow` | fixed character grid | `moveCursor(col, line)` (§22.7.6), `setStyle()`, `clearStyle()` |
| `graphicsWindow` | pixels | `drawImage(img, x, y, …)`, `setBackgroundColor(color)` |

The types are also reachable as `bgl.glulx.window`, `bgl.glulx.textBufferWindow`, `bgl.glulx.textGridWindow` and `bgl.glulx.graphicsWindow`. Windows derive from `_bglObject`, not from `object`: they are not world-tree objects and have no `parent`, `children` or attributes.

Child windows are pooled: at most 8 text-buffer, 8 text-grid and 8 graphics windows may exist at once. A split beyond the pool fails as `new` does (§4.13).

### 22.7.2 Roots

The core objects `bgl.ui.mainWin` and `bgl.ui.statusBar` (§21.10) are the roots of the tree; the extension adds the window API to those same objects.

| Object | Window kind | Added by this extension |
|---|---|---|
| `bgl.ui.mainWin` | text buffer | `width`, `height`, the split family, `drawImage()`, `setStyle()`, `clearStyle()` |
| `bgl.ui.statusBar` | text grid | `width`, the split family, `setStyle()`, `clearStyle()` (`height` is the core's, or the binding's, §23.3.8) |
| `bgl.ui.screen` | not a window | `setStyle()`, `clearStyle()` for both text window types at once (§22.7.7) |

The roots are objects, not `window` instances: `close()`, the move family, `moveCursor()`, `measureStyle()` and `styleHonored()` are not available on them.

### 22.7.3 Splitting

**Syntax**

```syntax
⟨view⟩ = ⟨win⟩.split⟨direction⟩⟨kind⟩( ⟨size⟩ [ , ⟨scale⟩ [ , ⟨border⟩ ] ] ) ;
```

⟨direction⟩ is `Up`, `Down`, `Left` or `Right`; ⟨kind⟩ is `Grid`, `Graphics` or `Buffer`; every combination exists, spelled as one method name (`splitUpGrid`, `splitLeftGraphics`, …). ⟨size⟩ is a number of lines (grid, buffer) or pixels (graphics) when ⟨scale⟩ is `fixed` (the default), or a percentage when it is `proportional`. ⟨border⟩ is `border` or `noBorder` (the default). Any window, root or child, can be split.

**Description**

The kind fixes the returned type; the direction fixes its *orientation view*:

| Direction | Returned type |
|---|---|
| `Up`, `Down` | `textGridWindowHorz`, `graphicsWindowHorz` or `textBufferWindowHorz` |
| `Left`, `Right` | `textGridWindowVert`, `graphicsWindowVert` or `textBufferWindowVert` |

An orientation view is an `alias class` of the content type (§8.2.4) that hides the assignment to the axis a window of that orientation cannot resize (§22.7.4). Holding the result with `auto` keeps the view; holding it as the plain content type (`textGridWindow hud = …`) or casting to it drops to the permissive surface.

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
⟨win⟩.width
⟨win⟩.height
⟨win⟩.width = ⟨n⟩ ;
⟨win⟩.height = ⟨n⟩ ;
⟨win⟩.move⟨direction⟩( ⟨size⟩ [ , ⟨scale⟩ [ , ⟨border⟩ ] ] ) ;
```

⟨direction⟩ is `Up`, `Down`, `Left` or `Right`, spelled as one method name (`moveUp`, …).

**Description**

`width` and `height` are readable on every window and root; reading queries the live window. Writing re-arranges the window within its parent pair. Only the axis a window was split *along* can be written — `height` for an `Up`/`Down` split, `width` for a `Left`/`Right` split — because the other dimension is dictated by the sibling window. This is enforced at two levels:

- **At compile time**, when the window is held as its orientation view: a `…Horz` view hides `width.operator =`, a `…Vert` view hides `height.operator =`, and writing the hidden axis is a compile-time error.
- **At run time**, on the permissive surface (a content-typed or `window`-typed value, or a cast): writing the fixed axis has no effect, and reports it through `log()` (§21.4).

`moveUp`, `moveDown`, `moveLeft` and `moveRight` re-arrange an *existing* child window within its parent pair, changing its placement and size and, with it, which axis is subsequently writable. They are not available on the roots.

**Example**

```bgl
auto hud = bgl.ui.mainWin.splitUpGrid(3);
hud.height = 5;                      // the split axis
// hud.width = 40;                   // compile-time error: hidden on textGridWindowHorz
(textGridWindow)hud.width = 40;      // permissive surface: no effect at run time
bgl.ui.statusBar.height = 2;         // roots are writable on both axes
```

### 22.7.5 Images

**Syntax**

```syntax
⟨graphicsWin⟩.drawImage( ⟨img⟩ , ⟨x⟩ , ⟨y⟩ [ , ⟨width⟩ [ , ⟨height⟩ ] ] ) ;
⟨textBufferWin⟩.drawImage( ⟨img⟩ [ , ⟨align⟩ [ , ⟨width⟩ [ , ⟨height⟩ ] ] ] ) ;
⟨graphicsWin⟩.setBackgroundColor( ⟨color⟩ ) ;
```

**Description**

Image drawing needs blorb assets, so these methods exist only when `generateBlorb` is true (§17.6). ⟨img⟩ is a `var`: an `eImages` value, a `glulxImage` (§22.8) or a raw resource id. A graphics window draws at pixel position `(x, y)`; a text-buffer window (including `bgl.ui.mainWin`) draws inline with an `eGlulxImageAlign` (default `inlineCenter`). When both `width` and `height` are `0` the image is drawn at its natural size; when one is given the other is computed to preserve the aspect ratio; when both are given they are used as is.

`setBackgroundColor(color)` sets a graphics window's background to an `$RRGGBB` value and clears the window to it.

**Example**

```bgl
pic.drawImage(eImages.coverArt, 0, 0);            // natural size
pic.drawImage(eImages.coverArt, 0, 0, 100);       // width 100, height to match
bgl.ui.mainWin.drawImage(eImages.icon, eGlulxImageAlign.marginLeft, 48, 48);
```

### 22.7.6 Cursor and Lifecycle

**Description**

A text-grid window positions its cursor explicitly; any child window can be closed, which also closes every window split from it.

**Methods**

| Member | On | Description |
|---|---|---|
| `win.moveCursor(col, line)` | `textGridWindow` | Place the cursor at column `col`, line `line`. |
| `win.close()` | any child window | Close the window and its subtree. Closing an already-closed window has no effect. |

**Example**

```bgl
auto hud = bgl.ui.mainWin.splitUpGrid(3);   // textGridWindowHorz
hud.moveCursor(0, 0);                       // the top-left cell
hud.close();                                // hud and any window split from it are gone
```

### 22.7.7 Styles

**Syntax**

```syntax
⟨target⟩.setStyle( ⟨styleType⟩ , style { ⟨member⟩ = ⟨value⟩ ; … } ) ;
⟨target⟩.clearStyle( ⟨styleType⟩ ) ;
```

**Description**

Glk styles are hints set per window kind and style type; a hint affects windows of that kind created *after* it is set. A style applied after a split does not affect windows already created. ⟨target⟩ is `bgl.ui.screen` (both text kinds at once), `bgl.ui.mainWin` or a `textBufferWindow` (the text-buffer kind), or `bgl.ui.statusBar` or a `textGridWindow` (the text-grid kind). ⟨styleType⟩ is an `eGlulxStyleType` (§22.7.10).

`style` is a value class whose members default to "leave the interpreter's setting". Build one with the named inline-object form (§11.3.1) giving only the members to change; `clearStyle()` resets every hint of that style type to the interpreter's default.

| `style` member | Type | Meaning |
|---|---|---|
| `justify` | `int` | An `eGlulxJustify` value, cast to `int`. |
| `indentation` | `int` | Left-margin indent. |
| `paragraphIndentation` | `int` | Additional first-line indent. |
| `sizeAdjustment` | `int` | Text size relative to the base size (`…, -1, 0, 1, …`). |
| `fontWeight` | `int` | `-1` lighter, `0` normal, `1` bold. |
| `italics` | `bool` | Italic or oblique text. |
| `fixedWidth` | `bool` | Fixed-pitch font. |
| `foreColor` | `int` | Text color, `$RRGGBB`. |
| `backColor` | `int` | Background color, `$RRGGBB`. |
| `reverse` | `bool` | Swap foreground and background. |

**Example**

```bgl
bgl.ui.screen.setStyle(eGlulxStyleType.normal, style { backColor = $111111; foreColor = $cccccc; });
bgl.ui.screen.setStyle(eGlulxStyleType.header, style { fontWeight = 1; justify = (int)eGlulxJustify.centered; });
hud.setStyle(eGlulxStyleType.alert, style { reverse = true; foreColor = $ff0000; });
hud.clearStyle(eGlulxStyleType.alert);
```

### 22.7.8 Colors

**Description**

`bgl.glulx.color` provides `$RRGGBB` values: `color.rgb(r, g, b)` composes one from `0..255` channels, and the members `black`, `white`, `red`, `green`, `blue`, `yellow`, `cyan`, `magenta`, `gray`, `lightGray` and `darkGray` are ready-made values. All are plain `int`s and are accepted wherever a color is.

**Example**

```bgl
#using bgl.glulx;
pic.setBackgroundColor(color.rgb(20, 30, 40));
bgl.ui.screen.setStyle(eGlulxStyleType.normal, style { foreColor = color.white; });
```

### 22.7.9 Style Validation

**Description**

Interpreters may ignore style hints. On a child window, `measureStyle(styleType, hint)` returns the value the interpreter actually uses for that style and hint, or `styleUnset` when it reports none; `styleHonored(styleType, hint)` is true when the interpreter reports the hint at all. `hint` is an `eGlulxStyleHint`, whose members are named after the `style` members. Both query the live window, so they are called after the window exists.

**Example**

```bgl
if (!hud.styleHonored(eGlulxStyleType.alert, eGlulxStyleHint.reverse)) { /* fall back */ }
int fg = hud.measureStyle(eGlulxStyleType.normal, eGlulxStyleHint.foreColor);
```

### 22.7.10 Enums

**Description**

Provided by the Glulx core or by this extension. Each core enum is also reachable through a short alias under `bgl.glulx` (§21.3); the alias names the type, and members may be written through either name (`bgl.glulx.eWinType.textGrid` or `eGlulxWindowType.textGrid`).

| Enum or bnum | Values | Provided by | `bgl.glulx` alias |
|---|---|---|---|
| `eGlulxWindowType` | `textBuffer`, `textGrid`, `graphics` | core | `eWinType` |
| `bGlulxWindowPlacement` | `left`, `right`, `above`, `below` | core | `bWinPlacement` |
| `bGlulxWindowScale` | `fixed`, `proportional` | core | `bWinScale` |
| `bGlulxWindowBorder` | `border`, `noBorder` | core | `bWinBorder` |
| `eGlulxImageAlign` | `inlineUp`, `inlineDown`, `inlineCenter`, `marginLeft`, `marginRight` | core | `eImgAlign` |
| `eGlulxImageDimension` | `original`, `scaled` | core | `eImgDimension` |
| `eGlulxStyleType` | `normal`, `emphasized`, `fixed`, `header`, `subheader`, `alert`, `note`, `blockQuote`, `input`, `user1`, `user2` | core | `eStyleType` |
| `eGlulxJustify` | `left`, `full`, `centered`, `right` | `<glulxWindow>` | — |
| `eGlulxStyleHint` | `indentation`, `paragraphIndentation`, `justify`, `sizeAdjustment`, `fontWeight`, `italics`, `fixedWidth`, `foreColor`, `backColor`, `reverse` | `<glulxWindow>` | — |

`styleUnset` is the `const int` returned by `measureStyle()` for an unreported hint.

**See also** §8.2.4, §8.7.4, §11.3.1, §17.6, §21.10, §22.8, §23.3.8.

## 22.8 `<glulxImage>`

**Purpose.** A typed handle to a Glulx image resource that answers "how big is this picture?".

**Include**

```bgl
#include <glulxImage>
```

> **[Glulx]** Glulx-only; including it in a Z-machine build is a compile-time error.

**Description**

`glulxImage` is a veneer class over the resource id (§8.2.5): it adds a type and methods but no storage, so a `glulxImage` is accepted wherever an image id is (`drawImage`, `bgl.printRules.img`). It is assigned from an `eImages` value or a raw `int` id, and converts back to `int` with `(int)img`.

| Member | Returns | Description |
|---|---|---|
| `img.width()` / `img.height()` | `int` | Natural pixel dimensions. |
| `img.size()` | `bglSize` | Both dimensions (§21.12). |

The same three members are added to the `eImages` enum, so `eImages.logo.width()` works without a handle.

**Example**

```bgl
#include <glulxImage>

void Main() {
    glulxImage cover = eImages.coverArt;
    int w = cover.width();
    pic.drawImage(cover, 0, 0, cover.width() / 2);
}
```

**Notes**

Does not require `bglInit()`. Does not require `generateBlorb`, but without packaged assets there are no images to measure.

**See also** §17.6, §21.12, §22.7.

---

# 23 IF Library Bindings

## 23.1 What a Binding Is

A *binding* is a Beguile source file in `beguiLib/bindings/` that exposes one external Inform 6 library to Beguile's type system. It declares the library's attributes, globals, constants, routines and actions with `extern` declarations (§15.4), so that Beguile code can name them with type checking; it declares the library's additive properties; and it integrates the library with the runtime core: it declares the story-identity constants the library expects, runs `bglInit()` before the library's own `main`, and routes the core's `bgl.ui` window objects through the library's window handling.

A binding defines no behavior of its own. The library's I6 source is still included with `#includeI6` (§15.5); the binding only makes its names visible.

Bindings are optional. A program that manages its own `extern` declarations, or that uses an IF library without a binding, does not include one. Different libraries need different bindings and are not used together.

**Syntax**

```bgl
#include <bindings/i6StandardLibrary>
#include <i6StandardLibrary>            // the folder prefix is optional (§14.1.1)
```

## 23.2 Available Bindings

| File | Library | I6 includes it corresponds to | Target notes |
|---|---|---|---|
| `bindings/i6StandardLibrary.bgl` | The Inform 6 standard library | `parser`, `verblib`, `grammar` | Z-machine and Glulx. Declares the Glulx window globals (`gg_mainwin`, `gg_statuswin`, …). |
| `bindings/punyInform.bgl` | PunyInform | `globals`, `puny` | Z-machine. PunyInform uses no classes; objects are distinguished by attributes alone. |
| `bindings/_commonBindings.bgl` | — | — | Shared declarations both bindings include (§23.3.7). Not included directly. |

Each binding is `#once`-guarded.

## 23.3 What a Binding Provides

### 23.3.1 Entry Point and `bglInit()`

Both libraries define `Main` themselves and call the program's `Initialise` routine (§6.7). A program using either binding therefore defines `Initialise`, not `Main`.

Each binding wraps the library's `main` so that `bglInit()` (§21.2) runs before it. The program does not call `bglInit()` itself.

The `i6StandardLibrary` binding performs the wrap only when the `autoInitialize` setting (§17.4) is true, which is its default. Set `autoInitialize = false` when another I6 extension already replaces `main` (Inform 6 forbids two replacements); the program is then responsible for calling `bglInit()`.

### 23.3.2 `story` and `headline`

Both libraries read the I6 constants `Story` and `Headline` while their own source is being included, for the banner. Each binding declares them from `#beguilerSettings` (§17.7) in an `#emitfirst` block, so that they precede the program's `#includeI6` of the library:

| Constant | Taken from |
|---|---|
| `story` | `#beguilerSettings.title` |
| `headline` | `#beguilerSettings.headline` |

A program using a binding must not declare them again. The `release` and `serial` settings are emitted by the compiler as the I6 `Release` and `Serial` directives, independently of any binding (§17.3). The `i6StandardLibrary` binding also declares `story` and `headline` as `extern string`, so Beguile code can read them.

### 23.3.3 Capability Flags

Each binding advertises itself with an order-independent symbol (`#declare`, §14.2.3), which any file — including a core file parsed before the binding — can test:

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

A binding declares, as `extern`:

- the library's **attributes** (`light`, `container`, `scenery`, `static`, …; §21.5.1);
- its **globals**: game state (`location`, `player`, `actor`, `score`, `turns`, …) and parser results (`noun`, `second`, `action`, `verb_word`, …). `action` is declared with type `verb`, so it compares directly with verb names (§13.3);
- its **constants**: parser error codes, scope reasons, color and window constants;
- its **objects**: `thedark`, `selfobj`, and (standard library only) the compass direction objects `n_obj` … `d_obj`;
- its **routines** (`PlayerTo`, `TestScope`, `StartTimer`, `StatusLineHeight`, …), and, for the standard library, the optional entry points the library calls at defined moments (`AfterLife`, `NewRoom`, `TimePasses`, `InScope`, …) as `extern default` functions: a program overrides one by defining a function of that name (§15.4.1).

Neither binding declares the library's object *properties* (`description`, `capacity`, `door_to`, `before`, …) as members of `object`; a class or object declares the properties it provides (§11.7.1). The one exception is `short_name`, which the core declares on `object` (§21.4).

### 23.3.5 Additive Properties

Additive properties belong to the library that defines them, so the binding declares them (§11.7.2). The `i6StandardLibrary` binding declares `before`, `after`, `life`, `orders`, `describe`, `time_out` and `each_turn`. `name` is additive in the Inform 6 compiler itself and is declared by the core (§21.5.2), not by a binding.

### 23.3.6 Verbs and Grammar Tokens

A binding declares the library's actions as `extern verb`s with their claimed dictionary words, so that a program can compare `action` with them, launch them with `perform()`, and extend their grammar without colliding with the library's own `Verb` directives (§13.2.3):

```bgl
extern verb Take { .take|.carry|.hold|.get|.pick|.peel }
extern verb Look { .look|.l }
extern verb Receive;                    // a fake action: no grammar of its own
```

It also declares the `grammarToken` enum (§21.5.5) with the parser's token names — `noun`, `held`, `creature`, `topic`, `multi`, `multiheld`, `multiexcept`, `multiinside`, `special`, `anynumber`, `number`, `scope`, `reverse` — for use in grammar patterns.

### 23.3.7 Shared Types

`bindings/_commonBindings.bgl`, included by both bindings, declares `stringOrRoutine` (§21.5.10) with its `print()` overload. Its behavior does not depend on either library.

### 23.3.8 Status Bar and Main Window

The core's `bgl.ui.statusBar.height` and `bgl.ui.mainWin.id` / `bgl.ui.statusBar.id` (§21.10) are replaced by each binding with accessors that read and write the library's own status-line state, so that `bgl.ui.statusBar.height = 2` and the library's own status-line redraw agree:

| Member | `i6StandardLibrary` | `punyInform` |
|---|---|---|
| `bgl.ui.statusBar.height` read | the library's tracked current height | the library's tracked current height |
| `bgl.ui.statusBar.height` write | the library's status-line-height routine | sets the library's persistent height and splits immediately |
| `bgl.ui.statusBar.id` | the library's status window (Glulx); `null` (Z-machine) | `null` |
| `bgl.ui.mainWin.id` | the library's main window | the core's (`0`) |

### 23.3.9 `bgl.story`

The `i6StandardLibrary` binding adds `bgl.story`, the story file's identity values read from the story header:

| Member | Returns | Description |
|---|---|---|
| `bgl.story.release` | `int` | The release number. |
| `bgl.story.serialChar(i)` | `char` | Character `i` (`0..5`) of the six-character serial. |
| `bgl.story.printSerial()` | `void` | Print the serial. |

It also binds the library's banner texts (`INFORMV__TX`, `LIBRARYV__TX`, `LibRelease`) and provides `informVersion`, a value-less emitter that prints the Inform 6 compiler version, usable inside an interpolated string like a print rule (§21.11).

> **[Z-machine/Glulx difference]** The header addresses differ per target; `bgl.story` hides the difference.

## 23.4 Differences Between the Bindings

| | `i6StandardLibrary` | `punyInform` |
|---|---|---|
| Directions | Direction *objects* `n_obj` … `d_obj`; a Go action has `noun == n_obj`. | No direction objects. `selected_direction` (a `property`) holds the direction property (`n_to`, `s_to`, … `in_to`, `out_to`) and `noun` is the shared `Directions` placeholder; `selected_direction_index` is `1..12`. The `FAKE_*_OBJ` constants are the parser's internal sentinels. |
| Reacting objects | Any object may define `before`/`after`. | An object with `before`, `after`, `each_turn`, `react_before` or `react_after` must have the `reactive` attribute. |
| Extra attributes | `door`, `absent`, `pluralname`, `male`, `female`, `neuter`, … | Also `switchable`, `on`, `workflag`, `reactive`, `scored`. |
| Pronouns | `itobj`, `himobj`, `herobj` | Also `themobj`. |
| Status line | `StatusLineHeight()`, `gg_statuswin_cursize` | `_StatusLineHeight()`, `statusline_height`, `statusline_current_height` |
| Colors | `CLR_*` constants | `CLR_*` plus the Ozmoo extended colors, and the `clr_on`/`clr_fg`/`clr_bg`/`clr_fgstatus` globals |
| Verb sets | The standard library's actions, meta actions and debug actions. | PunyInform's; some claimed-word sets differ (`Drop` claims `throw`; `Shout`/`ShoutAt`; `Again`/`Oops`; `LookModeNormal`/`Short`/`Long` in place of `LMode1..3`). |
| `autoInitialize` | Honored. | Not consulted; `main` is always wrapped. |

## 23.5 Writing a Binding

A binding for another library follows these rules:

1. **Guard the file with `#once`** (§14.1.6), and include `<bindings/_commonBindings>` for the shared types.
2. **Advertise the library with `#declare`** (§14.2.3): a bare capability symbol that other files can test regardless of include order.
3. **Declare names, not behavior.** Use `extern attribute`, `extern object`, `extern int` / `bool` / `string`, `extern const`, `extern property`, `extern additive property`, `extern verb` and `extern` routines (§15.4). Give `action` the type `verb`. Declare each additive property the library defines (§11.7.2). Do not declare the library's object properties on `object`.
4. **Declare every action as an `extern verb` with its claimed words** (§13.2.3), including fake actions as bodiless `extern verb Name;`, so that grammar added by a program extends the library's verbs instead of colliding with them.
5. **Declare the `grammarToken` enum** with the library's parser token names (§21.5.5).
6. **Declare library-required constants in `#emitfirst`** (§14.4.2) using `##beguilerSettings.<key>` substitution (§14.4.5), so that they precede the program's `#includeI6` of the library (§23.3.2).
7. **Run `bglInit()`.** Wrap the library's `main` with `#emitfirst { replace main _oldmain; }` and `#emitlast { [main; bglInit(); _oldmain(); ]; }`, gated on `#if bglAutoInitialize` so that `autoInitialize = false` disables it (§23.3.1).
8. **Route the status bar** by replacing `bgl.ui.statusBar`'s `id` and `height` accessors, and `bgl.ui.mainWin.id`, with the library's own state (§23.3.8), so that the core, the `<ui>` extension and `<glulxWindow>` operate on the library's windows.
9. **Declare the library's optional entry points** as `extern default` functions, so that a program overrides one by defining it (§15.4.1).

**See also** §11.7, §13.2.3, §14.4, §15.4, §17.7, §21.5.

---

# Appendices

# Appendix A Reserved Words

This is the alphabetical table of Beguile's reserved words, with the section that defines each; §1.5
groups the same words by role and explains why they should not be used as names. Beguile is
case-insensitive, so this applies in any letter case. Words marked *soft* have meaning only in the
position shown and are otherwise ordinary identifiers.

Kinds: **qualifier** (declaration qualifier), **type** (type name or type-forming word),
**control** (statement or control-flow word), **operator** (operator word), **value** (a value or
receiver name), **I6** (I6-significant: the word also appears verbatim in the generated program, so
reusing it as a name can produce Inform 6 that the Inform 6 compiler rejects).

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
| `byVal` | qualifier | §8.2.7 |
| `case` | control | §5.12 |
| `catch` | control | §5.16 |
| `char` | type | §2.2 |
| `charLiteral` | type | §2.4 |
| `class` | qualifier, I6 | §8.1 |
| `const` | qualifier | §3.4 |
| `continue` | control | §5.13 |
| `default` | qualifier; control (`switch`) | §5.12, §8.7.3, §15.4.1 |
| `delete` | control | §5.15 |
| `dictionaryWord` | type | §13.1, §21.5.3 |
| `dictionaryWordLiteral` | type | §2.4 |
| `do` | control | §5.11 |
| `else` | control | §5.8 |
| `emitter` | qualifier | §7.2 |
| `enum` | type | §2.7.1 |
| `explicit` | qualifier | §9.4 |
| `extend` | qualifier | §8.7.1, §11.10, §12.11, §13.5 |
| `extern` | qualifier | §15.4 |
| `false` | value, I6 (member of `eBool`) | §2.2 |
| `float` | type | §2.3 |
| `for` | control | §5.9 |
| `func` | type | §2.9 |
| `hide` | qualifier (soft: at the head of a member declaration) | §8.7.4 |
| `if` | control | §5.8 |
| `in` | control (`for … in`) | §5.9.1 |
| `inject` | control (soft: inside `extend` of an array) | §12.11 |
| `inline` | qualifier | §8.3.5, §11.3.1 |
| `int` | type | §2.2 |
| `interpolatedStringLiteral` | type | §2.4 |
| `intLiteral` | type | §2.4 |
| `move` | control (soft: inside `extend` of an array) | §12.11 |
| `negativeIntLiteral` | type | §2.4 |
| `new` | operator | §4.13 |
| `nothing` | value, I6 | §2.5 |
| `null` | value | §2.5 |
| `object` | type, I6 | §11.2 |
| `operator` | qualifier (operator member) | §9 |
| `outer` | value (soft: inside a property accessor body) | §9.9.3 |
| `property` | type, I6 | §11.7.1 |
| `rawArray` | type | §12.8 |
| `ref` | qualifier | §3.7 |
| `remove` | control (soft: inside `extend` of an array) | §12.11 |
| `replace` | qualifier, I6 | §6.5, §8.7.2 |
| `replaced` | operator (call to the replaced routine) | §6.5 |
| `return` | control | §5.14 |
| `rfalse` | control | §5.14 |
| `rtrue` | control | §5.14 |
| `self` | value, I6 | §6.6 |
| `static` | qualifier | §8.3.3 |
| `string` | type, I6 | §2.2 |
| `stringLiteral` | type | §2.4 |
| `superposed` | qualifier | §3.12 |
| `switch` | control | §5.12 |
| `synonyms` | qualifier (soft: inside a verb body or `extend` of a verb) | §13.5.4 |
| `throw` | control | §5.16 |
| `to` | control (`for` / `case` ranges) | §5.9, §5.12 |
| `true` | value, I6 (member of `eBool`) | §2.2 |
| `try` | control | §5.16 |
| `typesealed` | qualifier | §8.2.8 |
| `uint` | type | §2.2, §21.6.1 |
| `union` | type (soft) | §2.8.2 |
| `until` | control | §5.11 |
| `var` | type | §2.6 |
| `verb` | type, I6 | §13.2 |
| `void` | type | §2.2, §6.2 |
| `while` | control | §5.10 |

The following identifiers are not reserved, although they carry meaning: `grammar`, `meta` and
`priority` are members of the `verb` class, and `handler` and `perform` are its methods (§13.2);
`typeof` is a function of the runtime core (§2.8.1); `create` and `destroy` are the lifecycle methods of a pooled class (§8.2.6)
and `init` and `deinit` the lifecycle emitters of any class (§8.5); `first`, `last`, `after` and
`before` are the positions of an array `inject` (§12.11); `reverse` and `withI6Synonyms` are the
pseudo-tokens that may end a grammar line (§13.4.4). The keyword `for` is reused, outside a loop, in
`alias class Foo for Bar` (§8.2.4).

Directive names (`#include`, `#if`, …) are listed in Appendix B. Inform 6's own reserved words,
which constrain any Beguile name that reaches the generated program, are listed in §15.9.

---

# Appendix B Directive Index

Directive names are case-insensitive. Each entry points to its specification in §14; the semantics
of islands and `extern`-related behavior are in §15.

| Directive | Purpose | Entry |
|---|---|---|
| `#beguilerSettings { … }` | Configure the compiler and the I6 invocation | §14.7.1 (§17.1) |
| `#beguilerSettings.prop` | Read a setting as a compile-time literal | §14.7.2 (§17.7) |
| `#bgl` | Beguile island in raw I6 (in-routine or file-scope) | §14.5.2 |
| `#bglDecl` | File-scope Beguile island, declarations only | §14.5.2 |
| `#bglStmt` | File-scope Beguile island, statements only | §14.5.2 |
| `#declare` | Define an order-independent, immutable symbol | §14.2.3 |
| `#define` | Define a compilation symbol (linear) | §14.2.1 |
| `#elif` | Alternative condition in an `#if` block | §14.2.5 |
| `#else` | Fallback branch of an `#if` block | §14.2.5 |
| `#emitfirst` | Raw I6 at the beginning of the generated program | §14.4.2 |
| `#emitlast` | Raw I6 at the end of the generated program | §14.4.3 |
| `#endif` | Close an `#if` block | §14.2.5 |
| `#error` | Halt compilation with a message | §14.3.3 |
| `#exit` | Stop processing the current file | §14.3.4 |
| `#i6` | I6 island in Beguile source (single-line or block) | §14.5.1 (§15.2) |
| `#if` | Conditional compilation | §14.2.5 |
| `#include <name>` | Include a BLR library file | §14.1.1 |
| `#include "path"` | Include a Beguile file by path | §14.1.2 |
| `#include @"path"` | Include by raw-string path | §14.1.3 |
| `#include ?"path"`, `#include ?<name>` | Include, silently skipping a missing file | §14.1.4 |
| `#includeI6` | Include an I6 source file (`?` optional, `@` raw variants) | §14.1.5 |
| `#message` | Print a message during compilation | §14.3.1 |
| `#once` | Process this file at most once | §14.1.6 |
| `#redef` | Redefine a symbol without error | §14.2.2 |
| `#startup` | Raw I6 run at program startup, inside `bglInit()` | §14.4.1 |
| `#storedEmitFirst` | Named raw-I6 block, emitted at the top only when triggered | §14.4.4 |
| `#storedEmitLast` | Named raw-I6 block, emitted at the end only when triggered | §14.4.4 |
| `#undef` | Remove a symbol | §14.2.2 |
| `#using` | Import a class's or object's members into file scope | §10.4 |
| `#warning` | Report a warning and continue | §14.3.2 |

The `##` prefix marks Beguile-level processing inside otherwise raw I6 text. These markers are not
directives and are not valid in ordinary Beguile source:

| Marker | Where | Purpose | Specified in |
|---|---|---|---|
| `##if expr` | emitter bodies | Include the following body text only if the expression is true; same expression syntax as `#if` | §7.4 |
| `##else` | emitter bodies | Alternate branch of `##if` | §7.4 |
| `##endif` | emitter bodies | Close a `##if` block | §7.4 |
| `##beguilerSettings.key` | `#emitfirst`, `#emitlast`, `#storedEmitFirst`, `#storedEmitLast` bodies | Substitute a setting's value as an I6 literal | §14.4.5 |
| `##ifdef`, `##ifndef` | — | Do not exist. `##if SYMBOL` in an emitter body, `#if SYMBOL` in Beguile source | §7.4, §14.2.5 |
| `##Action` | raw I6 only | Not a Beguile marker: Inform 6's own action-constant syntax (`##Take`), valid wherever raw I6 is written. In Beguile source, compare `action` with the verb name instead | §13.3 |

A single-hash directive inside an emitter body (`#ifdef`, `#iftrue`, …) is not a Beguile directive:
it is passed through verbatim and becomes an I6 compile-time conditional (§7.4).

---

# Appendix C Operators

## C.1 Precedence

The table is the one in §4.3; higher precedence binds more tightly.

| Prec | Operators | Kind | Assoc | Meaning |
|:---:|---|---|---|---|
| 14 | `.` `?.` `[]` `()` `v?` `++` `--` | postfix | left | Member access, optional access, subscript, call, postfix query, postfix increment/decrement |
| 13 | `!` `-` `&` `(Type)` `++` `--` | prefix | right | Logical not, negation, address-of, cast, prefix increment/decrement |
| 11 | `*` `/` `%` | infix | left | Multiplicative |
| 10 | `+` `-` | infix | left | Additive |
| 9 | `<<` `>>` `<=>` | infix | left | Shift, three-way comparison |
| 8 | `<` `<=` `>` `>=` | infix | left | Relational |
| 7 | `==` `!=` `?=` `=~` | infix | left | Equality |
| 6 | `&` | infix | left | Bitwise and |
| 5 | `^` | infix | left | Bitwise exclusive or |
| 4 | `\|` | infix | left | Bitwise or |
| 3 | `&&` | infix | left | Logical and |
| 2 | `\|\|` | infix | left | Logical or |
| 1 | `? :` | ternary | — | Conditional (one per statement, §4.9) |
| 1 | `??` | infix | — | Null coalescing (§4.10) |
| 0 | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` `:=` | infix | right | Assignment (§5.5), compound assignment (§5.6), reference binding (§3.7) |

## C.2 Overloadable Operators

The complete set of operators a class may overload, with the declaration shape of each, is the table
in §9.1; the operators that must be declared as emitters are listed in §9.8. `?.`, `??`, `=>` and
`:=` are operator tokens that are not overloadable.

## C.3 Operator-to-Section Index

| Operator(s) | Role | See |
|---|---|---|
| `+` `-` `*` `/` `%` | Arithmetic | §4.5 |
| `+=` `-=` `*=` `/=` `%=` | Compound arithmetic assignment | §5.6, §9.7 |
| `==` `!=` `<` `>` `<=` `>=` | Comparison (result `eBool`) | §4.5 |
| `?=` | Type-defined comparison; no built-in meaning | §4.5, §9.5 |
| `<=>` | Three-way comparison (result `int`: negative / 0 / positive) | §4.5, §9.6 |
| `=~` | Content / case-insensitive equality (core `char`; `<string>`) | §4.5, §21.7, §22.3 |
| `&&` `\|\|` `!` | Logical (result `eBool`) | §4.5, §9.5 |
| `&` `\|` `^` `<<` `>>` | Bitwise and shift | §4.5, §9.1 |
| `&=` `\|=` `^=` `<<=` `>>=` | Compound bitwise assignment | §5.6 |
| `++` `--` | Increment / decrement | §5.7, §9.7 |
| `=` | Assignment | §5.5, §2.11 |
| `:=` | Reference binding (rebinding) | §3.7 |
| `? :` | Ternary conditional | §4.9 |
| `?` (postfix) | Query / null test (result `eBool`) | §4.10, §9.5 |
| `?.` | Optional chaining | §4.10 |
| `??` | Null coalescing | §4.10 |
| `(Type)x` | Type cast and conversion | §4.11, §9.4 |
| `&x` (prefix) | Address-of: raw machine address as `int` (`(int)x`) | §4.12 |
| `=>` | Lambda literal | §4.14 |
| `[]` `[]=` | Subscript read / write | §9.3, §12.3 |
| `::name` | Global-scope qualifier | §3.9 |
| `Type::operator op` | Operator reference | §4.15 |
| `$opref(op)` | Operator reference inside an emitter body | §7.3.1 |

---

# Appendix D String Escapes and Character Tables

This appendix completes §1.6.3 and §1.6.6. Every escape below is
valid in a string literal, an interpolated string literal and a character literal; none is processed
in a raw string literal, which recognizes only `\"`.

## D.1 Basic Escapes

| Escape | Character |
|---|---|
| `\n` | newline |
| `\"` | `"` |
| `\\` | `\` |
| `\^` | `^` (see §D.4) |
| `\~` | `~` (see §D.4) |
| `\@` | `@` |
| `\{` | `{` (interpolated strings only, §1.6.5) |

An unescaped `^` in a string is a newline and an unescaped `~` is a double quote, as in I6.

```bgl
"She said, \"well done.\""
"Price: 5\~ off!"
"Press \^ to continue."
```

## D.2 Numeric Escapes

| Escape | Meaning |
|---|---|
| `\`*NNN* | the character with decimal code *NNN* |
| `\$`*XX* | the character with hexadecimal code *XX* |

Both forms consume every consecutive digit (or hexadecimal digit) after the prefix. The code is a
Unicode code point and is rendered on both targets.

```bgl
"na\239ve"     // → naïve
"na\$EFve"     // → naïve
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

`\^` and `\~` are both a basic escape (§D.1) and an accent mark (§D.3). When the escape is followed
by a letter in its accent set it produces the accented letter; otherwise it produces a literal `^` or
`~`. To force the literal character before a letter that would otherwise take the accent, double it:
`\^^` or `\~~`.

```bgl
"\^a"      // → â
"\^^a"     // → ^a
"\~n"      // → ñ
"\~~n"     // → ~n
"\^z"      // → ^z   (z is not in the accent set)
```

## D.5 Directly Typed Characters

Accented characters may be typed directly into a string literal, an interpolated string literal, a
character literal or a dictionary word. The source file may be UTF-8 or Latin-1. Each character must
be one of the ZSCII extended characters (codes 155 to 224):

| Group | Characters |
|---|---|
| diaeresis | ä ö ü Ä Ö Ü ë ï ÿ Ë Ï |
| acute | á é í ó ú ý Á É Í Ó Ú Ý |
| grave | à è ì ò ù À È Ì Ò Ù |
| circumflex | â ê î ô û Â Ê Î Ô Û |
| ring and slash | å Å ø Ø |
| tilde | ã ñ õ Ã Ñ Õ |
| ligatures and letters | æ Æ ç Ç þ Þ ð Ð œ Œ ß |
| punctuation | £ ¡ ¿ |

Any other non-ASCII character is a compile-time error, except the typographic quotes of §D.6.

```bgl
"café"
'ñ'
.café
```

## D.6 Typographic Quotes and the Backtick

The curly double quotes `“` `”`, the curly single quotes `‘` `’` and the backtick `` ` `` are accepted
in string literals and rendered according to the target.

> **[Glulx]** Each curly quote is rendered as its own glyph; the backtick is rendered as a backtick.

> **[Z-machine]** The Z-machine has no typographic glyphs: curly double quotes fold to a straight
> double quote, and curly single quotes and the backtick fold to a straight apostrophe `'`.

The target is the resolved build target (§17); Glulx is the default. Only directly typed characters
are folded: a numeric escape such as `\$201C` always denotes that code point.

## D.7 Character Literals

A character literal (§1.6.6) holds exactly one character, written directly or with any escape above. Its
value is the character's ZSCII code, so a character literal may be compared numerically: `c >= 'ä'`
compares against the code of ä.

`\'` followed by a letter in the acute-accent set (§D.3) is the acute accent: `'\'e'` is é. A `\'`
not followed by such a letter is an escaped single quote.

---

# Appendix E Settings Reference

Every property accepted in a `#beguilerSettings` block (§17.1), alphabetically. *Precedence* is how
repeated assignments combine: **first** (first-writer-wins), **last** (last-writer-wins) or
**additive**; a command-line value always counts as written first. *Readable* marks the properties
that `#beguilerSettings.prop` may read (§17.7).

| Property | Type | Default | Precedence | CLI equivalent | Readable | Entry |
| --- | --- | --- | --- | --- | --- | --- |
| `author` | string | `""` | first | — | yes | §17.5 |
| `autoInitialize` | bool | `true` | first | — | yes | §17.4 |
| `beguiLibPath` | string | `"beguiLib"` | first | `-lib=<dir>` | yes | §17.2 |
| `blorbAssetPath` | string | `"assets"` | first | — | yes | §17.6 |
| `description` | string | `""` | first | — | yes | §17.5 |
| `economy` | bool | `false` | first | — | yes | §17.3 |
| `errorFormat` | `eErrorFormat` | `E1` | first | `-E1`, `-E2` | yes | §17.3 |
| `firstPublished` | string | `""` | first | — | yes | §17.5 |
| `forgiveness` | string | `""` | first | — | yes | §17.5 |
| `forInScratchSize` | int | `31` | first | — | yes | §17.4 |
| `framePoolSize` | int | `64` | first | — | yes | §17.4 |
| `generateBlorb` | bool | `false` | first | — | yes | §17.6 |
| `genre` | string | `""` | first | — | yes | §17.5 |
| `headline` | string | `""` | first | — | yes | §17.5 |
| `ifid` | string | `""` (generated) | first | — | yes | §17.5 |
| `includePaths` | string | none | additive | `-includepaths=<dirs>` | yes | §17.2 |
| `informName` | string | `"inform"` | first | `-inform=<name>` | yes | §17.2 |
| `informPath` | string | none | first | `-inform=<path>` | yes | §17.2 |
| `language` | string | `""` | first | — | yes | §17.5 |
| `linqScratchSize` | int | `32` | first | — | yes | §17.4 |
| `omitUnusedRoutines` | bool | `true` | first | — | yes | §17.3 |
| `outputPath` | string | `"output"` | first | `-o <dir>` | yes | §17.3 |
| `release` | int | `0` | first (0 = unset) | — | yes | §17.3 |
| `rewritePaths` | bool | `true` | first | — | yes | §17.4 |
| `serial` | string | `""` | first | — | yes | §17.3 |
| `series` | string | `""` | first | — | yes | §17.5 |
| `seriesNumber` | int | `0` | first (0 = unset) | — | yes | §17.5 |
| `target` | `eTarget` | `Glulx` | first | `-G`, `-z5`, `-z8` | yes | §17.3 |
| `title` | string | `""` | first | — | yes | §17.5 |
| `worldBufSize` | int | `128` | first | — | yes | §17.4 |

**Enum values.** `eTarget`: `Glulx`, `Z5`, `Z8`. `eErrorFormat`: `E1`, `E2`.

**Validation.** `serial` must be exactly six digits; the three `int` sizes must be at least 1;
`target` must be an `eTarget` member; a regular-string `includePaths` entry must name an existing
directory.

---

# Appendix F Pre-defined Symbols

The compiler defines the following symbols before any source file is processed. They are tested and
compared in `#if` expressions exactly like symbols defined with `#define` (§14.2). Symbol names are
case-insensitive, like all Beguile identifiers.

| Symbol | Example value | Meaning |
|--------|---------------|---------|
| `beguiler` | `1023` | Compiler version encoded as `major*1000 + minor*10 + patch`: 1.2.3 is `1023`, 1.1.0 is `1010`. The encoding fits a 16-bit signed Z-machine word for major versions up to 32. |
| `beguilerMajor` | `1` | Major version component. |
| `beguilerMinor` | `2` | Minor version component. |
| `beguilerPatch` | `3` | Patch version component. |
| `TARGET_GLULX` | (defined, no value) | Defined when the `target` setting is `Glulx`. |
| `TARGET_ZCODE` | (defined, no value) | Defined when the `target` setting is a Z-machine version. |

The version symbols are read-only and derived from the compiler's own version. The target symbols are
set from the `target` setting (§17.3) before any source is read, so they are available to every `#if`
in the program. Exactly one of `TARGET_GLULX` and `TARGET_ZCODE` is defined.

**The target symbols carry no value.** Both are flags, and they answer one question: which machine.
Having no value, neither can be used in a Beguile expression — `#if TARGET_ZCODE` tests definedness
like any other bare symbol (§14.2.5). A finer question, such as Z5 versus Z8, is a question about the
`target` *setting*, which `#if` reads directly:

```bgl
#if #beguilerSettings.target == "z8"
```

**Resolution rule.** Every symbol, pre-defined or `#define`d, that carries a value is resolved as an
inline compile-time literal wherever it appears in a Beguile expression: the name is replaced by its
value. A symbol never becomes an I6 `Constant` unless the program assigns it to a `const` variable.

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
    // Z5 only (excludes Z8) — the version lives in the setting, not the symbol
#endif

#if TARGET_GLULX
    // Glulx-specific code
#endif
```

**See also** §14.2.4, §14.2.5, §17.3, Appendix E.

---

# Appendix G Emitter Substitution Tokens

## G.1 Token Reference

Every `$` token is replaced when an emitter body is substituted at a use site; every `##` form is
processed at the same time. `$i6Name` and `$i6Expr` are also recognized in an `#i6` island (§15.2),
where they are the only tokens that mean anything — an island has no receiver and no parameters. Anything else in an emitter body, including single-hash `#` directives
and unrecognized `##name` text, passes through to the output unchanged. Full rules: §7.3 and §7.4.

| Token | Meaning | See |
|---|---|---|
| `$self` | In an operator or assignment emitter, the receiver with its trailing `.member` removed when the receiver is a member access (`obj` for `obj.score + 1`); otherwise, and in a method emitter, the receiver itself (`container.children` for `container.children.length()`). Not meaningful in a global emitter. | §7.3 |
| `$val` | The full receiver expression as written: `obj.score` for `obj.score + 1`; otherwise the same as `$self`. | §7.3 |
| `$host` | The object a proxy member is accessed on, the owner of the proxy: the receiver with its trailing `.member` removed, in every kind of emitter (`container` for `container.children.length()`); equals `$self` when the receiver is not a member access. | §7.3, §7.3.2 |
| `$name` | The argument supplied for the parameter declared as `name`. | §7.3 |
| `$target` | The assignment target as a full lvalue path, or a compiler-supplied temporary in statement position. Its presence makes the body responsible for the store. | §7.3.2 |
| `$prop` | In an `array<T>` emitter, the property name of an object-member array; `0` for a global array. | §7.3, §12.7 |
| `$opref(op[, T])` | A callable reference to the receiver type's `operator op` (routine name for `static`, property name for an instance operator, `0` for an emitter or none); `T` selects an overload. | §7.3.1 |
| `$oprefReq(op[, T])` | As `$opref`, with a compile-time warning when nothing is found. | §7.3.1 |
| `$i6Expr(expr)` | The I6 a Beguile expression emits, expanded inline — how one emitter reaches another. Binds the body's tokens as typed values; one expression or one void call; cycles and chains deeper than eight are errors. | §7.3.4 |
| `$i6Name(path[(types)])` | The identifier Inform 6 knows a declaration by: its `asI6` name, the `_bgl_<class>_<method>` mangling of a `static` method, or its plain name. `types` selects an overload. Naming an emitter is an error. | §7.3.3 |
| `$selfsub` | In a `_bglGlobalDeclaration` body, the instance name with `sub` appended. | §15.8 |
| `##if expr` / `##else` / `##endif` | Conditional inclusion of body text, with the `#if` expression syntax. `##ifdef`/`##ifndef` are errors. | §7.4 |
| `##beguilerSettings.key` | The compile-time value of a `#beguilerSettings` property, in the raw-I6 bodies of `#emitfirst`, `#emitlast`, `#storedEmitFirst` and `#storedEmitLast` only. | §14.4.5, §17.7 |
| `##Name` (other) | Not a token: passes through verbatim as I6 text (an I6 action constant such as `##Take`; `##$v` yields the action name of `$v`). | §7.4 |

---

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

---

# Appendix I Symbol and Method Index

Every runtime and library name an author can write, alphabetically: functions, methods, members,
objects, namespaces, types, constants and the symbols a library defines. *Kind* says what the name
is; a method or member names the type it belongs to. *Provided by* is `core` (loaded without an
include, §21), an extension (`<name>`, §22) or a binding (§23). *Section* is where the name is
specified. Overloads share one row; a family of names that differ only in a suffix is listed once
with the suffix spelled out. Names of the `bgl` namespace are listed under `bgl.…`.

`<array>` is loaded by the core, so its methods need no include; `length()` is built in, `setLength()`
and `clear()` come from `<array>`.

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
| `bgl.glulx.window`, `bgl.glulx.textBufferWindow`, `bgl.glulx.textGridWindow`, `bgl.glulx.graphicsWindow` | type (path forms of the window types) | `<glulxWindow>` | §21.3, §22.7.1 |
| `bgl.printRules` | namespace | core | §21.11 |
| `bgl.printRules.bold`, `italics`, `underline`, `reverse`, `fixed`, `roman` | print rule | core | §21.11 |
| `bgl.printRules.img(image[, align[, width[, height]]])` | print rule | core (Glulx) | §21.11 |
| `bgl.story` | namespace | binding (`i6StandardLibrary`) | §23.3.9 |
| `bgl.story.printSerial()` | method on `bgl.story` | binding (`i6StandardLibrary`) | §23.3.9 |
| `bgl.story.release` | member on `bgl.story` | binding (`i6StandardLibrary`) | §23.3.9 |
| `bgl.story.serialChar(i)` | method on `bgl.story` | binding (`i6StandardLibrary`) | §23.3.9 |
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
| `bglAutoInitialize` | symbol (mirrors `autoInitialize`) | binding | §23.5 |
| `bglClass` | type | core | §21.5.6 |
| `_bglGlobalDeclaration()` | emitter hook on a class (one top-level I6 declaration per instance; `$selfsub`) | core | §15.8 |
| `bglInit()` | function | core | §21.2, §18.8 |
| `_bglObject` | type (emitter class) | core | §21.5.8 |
| `bglSize` | type (value class) | core | §21.12 |
| `bglStringDefaultSize` | constant (I6, set before the include) | `<buf>` | §22.2 |
| `bglStringPoolReserve` | constant (I6, set before the include) | `<string>` | §22.3 |
| `bGlulxWindowBorder` | type (bnum) | core (Glulx) | §22.7.10 |
| `bGlulxWindowPlacement` | type (bnum) | core (Glulx) | §22.7.10 |
| `bGlulxWindowScale` | type (bnum) | core (Glulx) | §22.7.10 |
| `bool` | type | core | §2.2 |
| `capture()`, `release()` | method on `stringObj` | `<string>` | §22.3 |
| `captureOutput(obj, prop)` | method on `stringObj` | `<string>` | §22.3 |
| `char` | type | core | §2.2, §21.7 |
| `children` | member on `object` | core | §11.5.2, §21.5.7 |
| `childrenProp` | type | core | §21.5.7 |
| `clear()` | method on `array<T>` | core; `<array>` | §12.3, §22.4 |
| `close()` | method on `window` | `<glulxWindow>` | §22.7.6 |
| `CLR_*` | constant (colors) | binding | §23.4 |
| `clr_on`, `clr_fg`, `clr_bg`, `clr_fgstatus` | variable (extern) | binding (`punyInform`) | §23.4 |
| `color.black`, `white`, `red`, `green`, `blue`, `yellow`, `cyan`, `magenta`, `gray`, `lightGray`, `darkGray` | constant (on `bgl.glulx.color`) | `<glulxWindow>` | §22.7.8 |
| `color.rgb(r, g, b)` | method on `bgl.glulx.color` | `<glulxWindow>` | §22.7.8 |
| `compareTo(other[, caseInsensitive])` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `contains(item)` | method on `array<T>` | `<array>` | §22.4 |
| `copy(other)` | method on `bglAllocated` | core | §21.8 |
| `count()` | method on `array<T>` | `<linq>` | §22.5 |
| `create([p1[, p2[, p3]]])` | method on a pooled class (author-declared; run by `new`) | core | §8.2.6 |
| `DEBUG` | symbol (`#define`; enables `log()`) | core | §21.4 |
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
| `eErrorFormat` | type (enum) | core | §17.3, Appendix E |
| `eGlulxImageAlign` | type (enum) | core (Glulx) | §22.7.10, §21.11 |
| `eGlulxJustify` | type (enum) | `<glulxWindow>` | §22.7.10 |
| `eGlulxStyleHint` | type (enum) | `<glulxWindow>` | §22.7.10 |
| `eGlulxStyleType` | type (enum) | core (Glulx) | §22.7.10 |
| `eGlulxWindowType` | type (enum) | core (Glulx) | §22.7.10 |
| `eImages` | type (enum) | core | §17.6.1, §21.12 |
| `enqueue(item)`, `dequeue()` | method on `array<T>` | `<array>` | §22.4 |
| `eSounds` | type (enum) | core | §17.6.1, §21.12 |
| `eTarget` | type (enum) | core | §17.3, Appendix E |
| `eType` | type (enum) | core | §2.8.1, §21.5.9 |
| `eUnknownAsset` | type (enum) | core | §17.6.1, §21.12 |
| `FAKE_*_OBJ` | constant (parser sentinels) | binding (`punyInform`) | §23.4 |
| `filter(pred)` | method on `array<T>` | `<linq>` | §22.5 |
| `first()`, `last()` | method on `array<T>` | `<linq>` | §22.5 |
| `float` | type | core (Glulx) | §2.3 |
| `format(pattern[, p1[, p2]])` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `func<…>` | type | core | §2.9 |
| `getLength()` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `gg_mainwin`, `gg_statuswin`, … | variable (Glulx window globals) | binding (`i6StandardLibrary`) | §23.2 |
| `give(attr)`, `ungive(attr)` | method on `object`, `attributeList` | core | §21.5.1, §11.5.3 |
| `glulxImage` | type (veneer class) | `<glulxImage>` | §22.8 |
| `grammar` | member on `verb` | core | §13.2, §13.4.5 |
| `grammarRule` | type | core | §13.4.1, §21.5.5 |
| `grammarRuleList` | type | core | §13.4.1, §21.5.5 |
| `grammarToken` | type (extern enum) | binding | §13.4.1, §23.3.6 |
| `graphicsWindow` | type | `<glulxWindow>` | §22.7.1 |
| `handler()` | method on `verb` | core | §13.2.1 |
| `has(attr)`, `hasnt(attr)` | method on `object`, `attributeList` | core | §21.5.1, §11.5.3 |
| `height` | member on `bgl.ui.statusBar`, `window` | core; `<glulxWindow>` | §21.10, §22.7.4 |
| `I6_STANDARD_LIBRARY` | symbol (`#declare`) | binding (`i6StandardLibrary`) | §23.3.3 |
| `id` | member on `bgl.ui.mainWin`, `bgl.ui.statusBar`, `window` | core | §21.10, §22.7.1 |
| `indexOf(item)`, `find(item)` | method on `array<T>` | `<array>` | §22.4 |
| `indexOf(search)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `informVersion` | print rule | binding (`i6StandardLibrary`) | §23.3.9 |
| `INFORMV__TX`, `LIBRARYV__TX`, `LibRelease` | constant | binding (`i6StandardLibrary`) | §23.3.9 |
| `init()` | lifecycle emitter on a class (fires at a local's declaration) | core | §8.5 |
| `Initialise()` | function (entry point the library calls; author-defined) | binding | §23.3.1 |
| `insert(pos, item)` | method on `array<T>` | `<array>` | §22.4 |
| `insert(pos, src)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `int` | type | core | §2.2 |
| `intLiteral`, `negativeIntLiteral`, `stringLiteral`, `charLiteral`, `dictionaryWordLiteral`, `interpolatedStringLiteral` | type (literal pseudo-type) | core | §2.4 |
| `is(Class)` | method on `object` | core | §21.5.6 |
| `isEmpty()` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `isLower()`, `isUpper()`, `isAlpha()`, `isNumeric()`, `isAlphaNumeric()`, `isVowel()`, `isConsonant()` | method on `char` | core | §21.7 |
| `isRoutine()` | method on `stringOrRoutine` | binding | §21.5.10 |
| `isTracked()` | method on `array<T>` | `<array>` | §22.4 |
| `itobj`, `himobj`, `herobj` | variable (pronoun objects) | binding | §23.4 |
| `length()` | method on `array<T>` | core; `<array>` | §12.3, §22.4 |
| `length()`, `size()` | method on `children` | core | §11.5.2, §21.5.7 |
| `light`, `container`, `scenery`, `static`, … (library attributes) | value (`attribute`) | binding | §23.3.4 |
| `location`, `player`, `actor`, `score`, `turns` | variable (extern) | binding | §23.3.4 |
| `log(v)` | function | core | §21.4 |
| `map(f)` | method on `array<T>` | `<linq>` | §22.5 |
| `measureStyle(styleType, hint)`, `styleHonored(styleType, hint)` | method on `window` | `<glulxWindow>` | §22.7.9 |
| `meta` | member on `verb` | core | §13.2.4 |
| `mid(start, count)`, `left(count)`, `right(count)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `moveCursor(col, line)` | method on `textGridWindow` | `<glulxWindow>` | §22.7.6 |
| `moveUp()`, `moveDown()`, `moveLeft()`, `moveRight()` | method on `window` | `<glulxWindow>` | §22.7.4 |
| `name` | property (additive, on `object`) | core | §11.7.2, §21.5.2 |
| `NO_ATTRIBUTE` | constant | core | §21.5.1 |
| `n_obj` … `d_obj` (compass direction objects) | object (extern) | binding (`i6StandardLibrary`) | §23.3.4, §23.4 |
| `nothing`, `null` | value | core | §2.5 |
| `noun`, `held`, `creature`, `topic`, `multi`, `multiheld`, `multiexcept`, `multiinside`, `special`, `anynumber`, `number`, `scope`, `reverse` | constant (`grammarToken` values) | binding | §13.4.2, §23.3.6 |
| `noun`, `second`, `action`, `verb_word` | variable (extern; `action` is a `verb`) | binding | §23.3.4, §13.3 |
| `object` | type | core | §2.2, §11 |
| `orderBy([compare])` | method on `array<T>` | `<linq>` | §22.5 |
| `parent` | member on `object` | core | §11.5.1, §21.5.7 |
| `parentProp` | type | core | §21.5.7 |
| `patternElement` | type | core | §13.4.1, §21.5.5 |
| `peekEnd()`, `popEnd()` | method on `array<T>` | `<array>` | §22.4 |
| `perform([noun[, second]])` | method on `verb` | core | §13.2.2 |
| `PlayerTo()`, `TestScope()`, `StartTimer()`, `StatusLineHeight()`, … | function (extern library routine) | binding | §23.3.4 |
| `prepend(item)` | method on `array<T>` | `<array>` | §22.4 |
| `print()` | method on `_bglObject` (author-defined override) | core | §21.4 |
| `print()` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `print(v)` | function (overloaded by type) | core | §21.4 |
| `printName(obj)` | function | core | §21.4 |
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
| `setLength(n)` | method on `array<T>` | core; `<array>` | §12.3, §22.4 |
| `setStyle(styleType, style {…})`, `clearStyle(styleType)` | method on the text window types, the roots, `bgl.ui.screen` | `<glulxWindow>` | §22.7.7 |
| `short_name` | member on `object` | core | §21.4 |
| `size()` | method on `array<T>` | core | §12.3 |
| `size()` | method on `glulxImage`, `eImages` | `<glulxImage>` | §22.8 |
| `size()`, `length()`, `setLength(n)`, `isTracked()` | method on `array<char>` | `<buf>` | §22.2 |
| `sort([compare])` | method on `array<T>` | `<array>` | §22.4 |
| `splitUpGrid()` … `splitRightBuffer()` (12 combinations of direction and kind) | method on `window`, the roots | `<glulxWindow>` | §22.7.3 |
| `startsWith(prefix)`, `endsWith(suffix)`, `contains(search)` | method on `string`, `stringObj` | `<string>` | §22.3 |
| `story`, `headline` | constant (I6) and `extern string` | binding | §23.3.2 |
| `string` | type | core | §2.2, §22.3 |
| `stringObj` | type | `<string>` | §22.3 |
| `stringOrRoutine` | type (named union) | binding | §2.8.2, §21.5.10, §23.3.7 |
| `style` | emitter namespace (built-in; called as `style.⟨member⟩()`; the same word as the value class below) | core | §7.7 |
| `style` | type (value class; written `style { … }`; the same word as the emitter namespace above) | `<glulxWindow>` | §22.7.7 |
| `styleUnset` | constant | `<glulxWindow>` | §22.7.10 |
| `swap(pos1, pos2)` | method on `array<T>` | `<array>` | §22.4 |
| `switchable`, `on`, `workflag`, `reactive`, `scored` | value (`attribute`) | binding (`punyInform`) | §23.4 |
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
| `width` | member on `window`, the roots | `<glulxWindow>` | §22.7.4 |
| `width()`, `height()` | method on `glulxImage`, `eImages` | `<glulxImage>` | §22.8 |
| `window` | type | `<glulxWindow>` | §22.7.1 |

---

# Appendix J Limits

Every numeric limit stated in this specification, in one table. *Target* is the target the limit
applies to: `both`, `Z-machine` or `Glulx`; a paired value is written `Z-machine / Glulx`. Each limit
is specified in the section named in the last column; this table does not add limits of its own.

| Limit | Value | Target | Section |
| --- | --- | --- | --- |
| Local slots per routine, parameters and locals together; one slot is reserved for the frame pointer, so a routine needing more than 14 spills to the frame pool | 15 | Z-machine | §18.10 |
| Arguments passed natively on a routine call; the rest travel through compiler-emitted globals | 5 | Z-machine | §18.10 |
| Frame pool size, in words (`framePoolSize`) | 64 by default; at least 1 | both | §17.4, §18.10 |
| Words per object property, counting the length slot; a larger member array uses separate storage | 32 | Z-machine | §12.7 |
| Distinct values in one `bnum` | 16 / 32 | Z-machine / Glulx | §2.7.2 |
| Parameters of a pooled class's `create()` | 3 | both | §8.2.6 |
| `create()` and `destroy()` declarations per pooled class | one of each | both | §8.2.6 |
| Type parameters per class; only the first binds | 1 | both | §8.1.1 |
| `operator auto()` declarations per class | 1 | both | §7.8 |
| Ternary operators per statement | 1 | both | §4.9 |
| Member types in a named union | at least 2 | both | §2.8.2 |
| Integer literal in an `array<char>` initializer or element write | 0..255 | both | §12.4 |
| `setLength(n)` range | 0..32767 / 0..2^31−1 | Z-machine / Glulx | §12.3 |
| Include nesting depth | 255 | both | §14.1.6, §18.4 |
| Compile-time errors reported per build; the first ends the build | 1 | both | §19.1, §16.7 |
| Elements in a literal-list `for (x in {…})` (`forInScratchSize`) | 31 by default; at least 1 | both | §17.4, §5.9.1 |
| Elements per `<linq>` scratch buffer (`linqScratchSize`) | 32 by default; at least 1 | both | §17.4, §22.5 |
| `<linq>` chain nesting depth (a chain inside a chain's predicate or mapper) | 2 | both | §22.5 |
| Arguments to `stringObj.format()` after the pattern (`$1`, `$2`) | 2 | both | §22.3 |
| String objects in the `<string>` pool (`bglStringPoolReserve`) | 10 by default | both | §22.3 |
| Characters per `<buf>` / `<string>` buffer (`bglStringDefaultSize`) | 500 by default | both | §22.2 |
| `<buf>` capture nesting depth | 16 | both | §22.2 |
| Objects per `bgl.world` result buffer; a longer walk stops at the limit | 128 | both | §21.9 |
| `bgl.world` result buffers, rotating; a result is valid until the fourth subsequent query | 4 | both | §21.9 |
| Values listed in `bgl.util.random.get(a, b, …)` | 2 to 8 | both | §21.6.3 |
| Child windows per kind (`<glulxWindow>`): text-buffer, text-grid, graphics | 8 of each | Glulx | §22.7.1 |
| Word size (`bgl.wordsize`), in bytes | 2 / 4 | Z-machine / Glulx | §21.3 |
| Width of `int` and `uint`, in bits | 16 / 32 | Z-machine / Glulx | §21.6.1 |
| Digits in the `serial` setting | exactly 6 | both | §17.3 |
| Major version representable in the `beguiler` symbol | 32 | Z-machine | Appendix F |
| ZSCII codes of the extended characters that may be typed directly | 155..224 | Z-machine | Appendix D.5 |
