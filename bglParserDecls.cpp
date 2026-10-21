#include "platform.h"
// ===============================================================================
// bglParserDecls.cpp - non-class/object declarations: enum, variable, array,
// routine, grammar.
//
// Covers everything declaration-shaped that doesn't belong in
// bglParserClassObjectDecl.cpp.
//
//   processEnumDeclaration            - enum / bnum / extern enum / extern bnum
//   processArrayDeclaration           - array<T> name[N] = { ... };
//   processVariableDeclaration        - T name; / T name = value;
//   processRoutineDeclaration         - free function declarations
//   processGrammarDeclaration         - grammar foo { ... } at file scope
//   processGrammarObjectDeclaration   - the grammar-object's instance body
//   parseOptionalGrammarRulePriority  - optional priority at end of a grammar rule
//   parseGrammarLineContent           - parse one { trigger, pattern, -> action } line
//   parseGrammarLines                 - parse the outer { line, line } around grammar
// ===============================================================================
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cctype>
#include <tuple>
#include <optional>
#include <string_view>

#include "helpers.h"
#include "settings.h"
#include "typeDef.h"
#include "bglParser.h"
#include "fileLexer.h"
#include "token.h"
#include "bglLanguageService.h"
#include "bglParserHelpers.h"

using namespace std;


// ===============================================================================
// Enum emitter methods (veneer enums)
// ===============================================================================
// An enum/bnum value is already a bare int word — a veneer over int — so an emitter method attached
// to it inlines at the call site with `$self` = the value, exactly like an emitter class member. The
// methods live on an UNREGISTERED companion emitter class hung on the enumDef: it never enters the
// type registry, so it is un-nameable and invisible to the LSP/completions, yet all existing method
// dispatch (getDispatchClass→resolveMethod) works unchanged once getDispatchClass returns it.

// Get (creating on first call) the enum's companion emitter class.
classDef* bglParser::enumCompanion(enumDef& en){
    if(en.companion == nullptr){
        classDef* c = new classDef();
        c->name = "_bglenum_" + en.name;   // internal only; NEVER registered with languageService
        c->displayName = en.dName();
        c->isEmitterClass = true;          // no I6 backing — members inline
        c->src = en.src;
        en.companion = c;
    }
    return en.companion;
}

bool bglParser::isEnumMemberQualifier(token t){
    return t.is("static") || t.is("inline") || t.is("explicit") || t.is("default")
        || t.is("alias")  || t.is("superposed") || t.is("additive")
        || t.is("typesealed") || t.is("ref")
        || t.is(token::constantDeclararion) || t.is(token::replace) || t.is(token::external);
}

// Parse one `emitter <returnType> <name>(params){ body }` method or `emitter <returnType> <name>{ body }`
// value inside an enum body (the `emitter` keyword already consumed) and attach it to the enum's
// companion. Reuses the same leaf parsers as class emitter members (processParameterList /
// getRawTextThroughClosingBrace) so emission is byte-identical. Operators are rejected.
void bglParser::parseEnumEmitterMethod(enumDef& en){
    token returnType = file.getToken({eTokenType::dataType, eTokenType::identifier});
    if(returnType.value == "func") returnType.value = parseFuncType();
    returnType.value = maybeParseUnionTail(returnType.value);
    token name = file.getToken({eTokenType::identifier, eTokenType::dataType});
    if(name.is("operator"))
        parsingError(format("enum '{0}': operators cannot be attached to an enum — only emitter methods "
                            "(`emitter T name(){{ ... }}`) are allowed", en.dName()));
    token paren = file.getToken();
    bool isValue = paren.is(token::braceOpen);
    if(!paren.is(token::parenOpen) && !isValue)
        parsingError(format("enum '{0}': '{1}' must be an emitter method or value — expected '(' or '{{' after the name",
                            en.dName(), (string)name));
    functionDef& fd = *(new functionDef());
    fd.name = (string)name; fd.displayName = name.originalValue;
    fd.src = name.src.line > 0 ? name.src : file.currentLocation();
    fd.returnType = languageService.getType((string)returnType);
    if(fd.returnType.name.empty()) fd.returnType.name = returnType.value; // func<...>/union return types
    fd.isEmitter = true;
    if(!returnType.docComment.empty())   fd.docComment = returnType.docComment;
    else if(!name.docComment.empty())    fd.docComment = name.docComment;
    if(isValue){
        fd.isValueEmitter = true;
        i6Block& b = *(new i6Block());
        b.i6Body = file.getRawTextThroughClosingBrace(/*isI6Content=*/true);
        fd.body = &b;
        classDef* comp = enumCompanion(en);
        if(!replaceStubMember(comp->members, fd))
            comp->members.push_back(&fd);
        return;
    }
    processParameterList(fd);                 // consumes through ')'
    synthesizeParamBackings(fd, en.dName());  // enum name as the backing-context key
    if(file.peekToken().is(token::endStatement)){
        file.getToken();                       // ';' pass-through emitter (returns $self unchanged)
        i6Block& b = *(new i6Block()); b.i6Body = " $self"; fd.body = &b;
    } else {
        file.getToken(token::braceOpen);
        i6Block& b = *(new i6Block());
        b.i6Body = file.getRawTextThroughClosingBrace(/*isI6Content=*/true);
        fd.body = &b;
    }
    classDef* comp = enumCompanion(en);
    // Replace a pre-scan stub of the same name if present (order-independence); else append.
    if(!replaceStubMember(comp->members, fd))
        comp->members.push_back(&fd);
}

// ===============================================================================
// processEnumDeclaration
// ===============================================================================
bool bglParser::processEnumDeclaration(token tok, bool isExternal, token nameOverride, bool isExtend){
    if(getCurrentCompileContext()!=eCompileContext::global) parsingError(format("Enumerations are only allowed in global context:'{0}'", (string) tok));
    bool isBnum=false;
    if(tok.is(token::bnumDeclaration) || tok.value == "bnum") isBnum=true;
    token name = nameOverride.tokenType != eTokenType::unknown ? nameOverride : file.getToken({eTokenType::identifier, eTokenType::dataType}); //enum name
    // `extend enum X { ... }` appends members to an existing enum rather than declaring a new one.
    // The pre-scanner already appended the members (so forward references resolve), so the main
    // pass finds a populated enum and drains the body below — same two-pass shape as a fresh enum.
    enumDef* enumPtr;
    if(isExtend){
        enumPtr = languageService.findEnum(name.value);
        if(enumPtr == nullptr)
            parsingError(format("extend enum '{0}': no previously declared enum with that name", (string)name));
        if(enumPtr->isBnum != isBnum)
            parsingError(format("'{0}' is a {2}; extend it with `extend {2} {0}`, not `extend {1}`.",
                                (string)name, isBnum ? "bnum" : "enum", enumPtr->isBnum ? "bnum" : "enum"));
    } else {
        enumPtr = &languageService.registerEnum((string)name, isExternal, name.originalValue);
    }
    enumDef& newEnum = *enumPtr;
    newEnum.isBnum = isBnum;
    if(!tok.docComment.empty())          newEnum.docComment = tok.docComment;
    else if(!name.docComment.empty())    newEnum.docComment = name.docComment;
    // Optional shared-base clause (mirrors pre-scanner); see bglPreScanner.cpp for semantics.
    // Not valid on `extend` — the base is part of the original declaration.
    if(!isExtend && file.peekToken().is(":")){
        file.getToken();
        token baseTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
        if(!isBnum)
            parsingError(format("enum '{0}': shared-base inheritance is only valid for bnum declarations", newEnum.dName()));
        enumDef* base = languageService.findEnum(baseTok.value);
        if(!base || !base->isBnum)
            parsingError(format("bnum '{0}': base '{1}' is not a declared bnum", newEnum.dName(), baseTok.originalValue));
        newEnum.baseBnum = base;
    }
    bool hasBase = newEnum.baseBnum != nullptr;
    file.getToken(token::braceOpen);
    // Values may already have been populated by the pre-scanner (forward references). If so, drain the
    // value entries rather than re-registering them — but emitter members are ALWAYS parsed in the main
    // pass so their pre-scan stubs are replaced with fully-resolved definitions (mirroring classes).
    bool valuesAlreadyPopulated = !newEnum.namedValues.empty();
    set<string> namedHere;
    int val=1;
    while(true){
        tok=file.getToken();
        if(tok.is(eTokenType::eof)) parsingError("Unexpected end of file inside enum — missing closing '}'");
        if(tok.is(token::braceClose)) break;
        if(tok.is(token::comma)) continue;   // tolerate a separator between entries
        // Emitter method member: `emitter <type> name(params){ body }` (declaration or `extend enum`).
        if(tok.is("emitter")){
            parseEnumEmitterMethod(newEnum);
            if(file.peekToken().is(token::comma)) file.getToken();  // optional trailing comma
            continue;
        }
        // A misplaced member declaration (static/const/… method): only emitter methods may be attached.
        if(isEnumMemberQualifier(tok))
            parsingError(format("enum '{0}': only emitter methods (`emitter T name(){{ ... }}`) may be "
                                "attached to an enum — '{1}' members are not allowed", newEnum.dName(), (string)tok));
        // Otherwise this is a value entry.
        if(!namedHere.insert(tok.value).second)
            parsingError(format("enum '{0}' already has a member '{1}'.", newEnum.dName(),
                                tok.originalValue.empty() ? tok.value : tok.originalValue));
        if(valuesAlreadyPopulated){
            // Drain this single value entry (name [= number]) up to the next separator / close brace.
            while(!file.peekToken().is(token::comma) && !file.peekToken().is(token::braceClose)
                  && !file.peekToken().is(eTokenType::eof))
                file.getToken();
            continue;
        }
        enumValueDef& newVal=*new enumValueDef();
        newVal.name=tok.value;
        newVal.displayName=tok.originalValue;
        newVal.docComment=tok.docComment;  // doc-comment attached to the value's name token
        token sep=file.getToken({token::braceClose, token::comma, token::assignment});
        if(sep.is(token::assignment)){
            bool negate = false;
            if(file.peekToken().is("-")){ file.getToken(); negate = true; }
            token numTok=file.getToken(eTokenType::integer);
            val=stoi(numTok.value);
            if(negate) val = -val;
            if(isBnum && negate)
                parsingError(format("bnum '{0}': negative value {1} is not allowed", newEnum.dName(), val));
            if(isBnum && !hasBase && val != 0 && (val & (val - 1)) != 0)
                parsingError(format("bnum '{0}': explicit value {1} is not a power of 2", newEnum.dName(), val));
            sep=file.getToken({token::braceClose, token::comma});
        }
        // A bnum's values are bits of one word: 16 on the Z-machine, 32 on Glulx.
        newVal.value=val;
        val = nextEnumValue(isBnum, val);
        newEnum.namedValues.push_back(&newVal);
        if(sep.is(token::braceClose)) break;
    }
    // A bnum's values are bits of one word: 16 on the Z-machine, 32 on Glulx.
    if(isBnum)
        for(size_t i = 0; i < newEnum.namedValues.size(); i++){
            unsigned v = (unsigned)newEnum.namedValues[i]->value;
            bool past = targetIsZcode() ? v > 0x8000u
                      : (v == 0 && i > 0 && (unsigned)newEnum.namedValues[i - 1]->value == 0x80000000u);
            if(past)
                parsingError(format("bnum '{0}': '{1}' is one value too many: a {2} word holds {3} bits.",
                                    newEnum.dName(), newEnum.namedValues[i]->name,
                                    targetIsZcode() ? "Z-machine" : "Glulx", targetIsZcode() ? 16 : 32));
        }
    return false;
}


// ===============================================================================
// Array / variable / routine declarations
// ===============================================================================

// Parse a `{ v1, v2, ... }` array initializer (the '{' already consumed) for the given
// elementType, reading through the matching '}'. A `{...}` element is baked as a nested
// aggregate: an inline object for an object-backed element type, or an anonymous inner array
// for an array-of-arrays element type (elementType == "array<...>" / "rawarray<...>"). The
// recursion carries through both, so array<array<array<T>>> nests naturally.
initializerList* bglParser::parseArrayInitializerList(const string& elementType, functionDef* func, statementBlock* body){
    initializerList* list = new initializerList();
    classDef* elemCls = getDispatchClass(elementType);
    bool inferInlineObjects = isInlineConstructible(elemCls) && !isArrayOfArraysElement(elementType);
    bool inferInlineArrays  = isArrayOfArraysElement(elementType);
    string nestedElem       = inferInlineArrays ? arrayInnerType(elementType) : "";
    token t = file.getToken();
    while(!t.is(token::braceClose) && !t.is(eTokenType::eof)) {
        if(inferInlineArrays && t.is(token::braceOpen) && func != nullptr && nestedElem != "char"){
            // A local's inner array is allocated per call (emitLocalArrayAllocs), so the list stays a list.
            initializerList* il = parseArrayInitializerList(nestedElem, func, body);
            if(!nestedElem.empty() && nestedElem != "var")
                for(size_t i = 0; i < il->elements.size(); i++){
                    expression* e = il->elements[i];
                    if(!e->resolvedType.empty() && !zeroFitsBnum(e, nestedElem) && !isArrayElementCompatible(e->resolvedType, nestedElem))
                        parsingError(format("Inner array element {0} has type '{1}', expected '{2}'", i, e->resolvedType, nestedElem));
                }
            il->resolvedType = elementType;
            list->elements.push_back(il);
            token sep = file.getToken({",", token::braceClose});
            if(sep.is(token::braceClose)) break;
            t = file.getToken();
            continue;
        }
        if(inferInlineArrays && t.is(token::braceOpen)){
            // Array-of-arrays element: bake `{...}` as an anonymous inner array and store its address.
            string anonName = format("_bglanon{0}", anonObjectCounter++);
            bakeInlineArrayAggregate(nestedElem, anonName, func, body);
            expression* elem = new expression();
            elem->tokens.push_back(anonName);
            elem->resolvedType = elementType;
            list->elements.push_back(elem);
            token sep = file.getToken({",", token::braceClose});
            if(sep.is(token::braceClose)) break;
            t = file.getToken();
            continue;
        }
        if(inferInlineObjects && t.is(token::braceOpen)){
            // Inferred inline object: a bare `{...}` element of an object-backed array takes the
            // array's element type (§6.2.1). The '{' is already consumed (it is `t`).
            string anonName = format("_bglanon{0}", anonObjectCounter++);
            bakeInlineObjectAggregate(elemCls, elementType, anonName, func, body);
            expression* elem = new expression();
            elem->tokens.push_back(anonName);
            elem->resolvedType = elementType;
            list->elements.push_back(elem);
            token sep = file.getToken({",", token::braceClose});
            if(sep.is(token::braceClose)) break;
            t = file.getToken();
            continue;
        }
        // An enum element type picks its own member over a same-named global (`door`, an attribute).
        string savedExpected = currentExpectedType;
        if(languageService.findEnum(elementType) != nullptr || elementType.rfind("func", 0) == 0) currentExpectedType = elementType;
        expression* elem = parseExpression(t, {",", token::braceClose}, func, body);
        currentExpectedType = savedExpected;
        foldIntLiteralToFloat(elem, elementType);
        list->elements.push_back(elem);
        if(elem->terminator == token::braceClose) break;
        t = file.getToken();
    }
    return list;
}

// Bake an anonymous inner array from a `{...}` literal (the '{' already consumed by the caller)
// whose element type is innerElemType. It becomes a file-scope tracked array named anonName; the
// caller stores a reference to anonName as the outer element (I6 uses the bare array name as its
// address). Inner elements are type-checked against innerElemType, mirroring the outer check.
void bglParser::bakeInlineArrayAggregate(const string& innerElemType, const string& anonName, functionDef* func, statementBlock* body){
    arrayDeclaration& inner = *(new arrayDeclaration());
    inner.src = file.currentLocation();
    inner.name = anonName;
    inner.type = languageService.getType(innerElemType == "char" ? "bytearray" : "array");
    inner.elementType = innerElemType;
    if(innerElemType == "char") inner.isByteArray = true;
    initializerList* il = parseArrayInitializerList(innerElemType, func, body);
    if(!innerElemType.empty() && innerElemType != "var")
        for(size_t i = 0; i < il->elements.size(); i++){
            expression* e = il->elements[i];
            if(!e->resolvedType.empty() && !zeroFitsBnum(e, innerElemType) && !isArrayElementCompatible(e->resolvedType, innerElemType))
                parsingError(format("Inner array element {0} has type '{1}', expected '{2}'", i, e->resolvedType, innerElemType));
            checkByteElementRange(e, innerElemType);
        }
    inner.declaredExpressionValue = il;
    languageService.registerInstance(inner);   // file-scope global; emitted as its own `array` directive
    adoptInitialElements(anonName, innerElemType, il);
}

// A global array whose element type owns storage (publishes a static `operator =`) is compiled holding
// its initial values; bglInit hands each value of another type (a literal) to that operator, so the
// slot owns its own instance.
void bglParser::adoptInitialElements(const string& arrName, const string& elementType, initializerList* list){
    if(list == nullptr || list->elements.empty()) return;
    // A value-class global named as an element is a value: the slot gets an instance of its own with the
    // global's initial fields (a named instance, `Rgb named { … }`, is shared instead — §12.10).
    if(classDef* ec = languageService.findClass(elementType); ec != nullptr && isValueClass(ec))
        for(expression* e : list->elements)
            if(e != nullptr && e->tokens.size() == 1)
                if(auto* gv = languageService.findGlobalAs<variableDeclaration>(e->tokens[0]);
                   gv != nullptr && gv->declaredExpressionValue != nullptr && gv->declaredExpressionValue->tokens.size() == 1
                   && gv->declaredExpressionValue->tokens[0].rfind("_bglanon", 0) == 0)
                    if(auto* made = languageService.findGlobalAs<objectDef>(gv->declaredExpressionValue->tokens[0])){
                        string copyName = format("_bglanon{0}", anonObjectCounter++);
                        objectDef& copy = languageService.registerObject(copyName, false, "");
                        copy.objectClass = made->objectClass;
                        copy.members = made->members;
                        if(std::find(languageService.globals.begin(), languageService.globals.end(), &copy) == languageService.globals.end())
                            languageService.globals.push_back(&copy);
                        e->tokens[0] = copyName;
                        e->resolvedType = elementType;
                    }
    bool isStatic = false;
    string assign = operatorRef(elementType, "=", "", &isStatic);
    if(assign.empty() || assign == "0" || !isStatic) return;
    for(size_t i = 0; i < list->elements.size(); i++){
        expression* e = list->elements[i];
        if(e == nullptr || e->resolvedType == elementType || e->text() == "0") continue;
        // A named instance of the class (`Rgb named { … }`) is shared, not copied.
        if(objectsClassName(e->resolvedType) == elementType) continue;
        string slot = format("{0}-->{1}", arrName, i + 1);
        languageService.globalInits.push_back({arrName, format("{0} = {1}(0, {0});", slot, assign)});
    }
}

// The value class an array's slots own instances of, or null when its elements are plain words.
classDef* bglParser::owningValueElementClass(const string& elementType){
    classDef* cls = elementType.empty() ? nullptr : languageService.findClass(elementType);
    if(!cls || cls->isEmitterClass || cls->isAlias || cls->isExternal || !isValueClass(cls) || !classHasStoredFields(cls))
        return nullptr;
    return cls;
}

// Records an array of a value class, so the emitter can size the pool its slots draw instances from.
void bglParser::noteValueElementArray(arrayDeclaration& arr, classDef* hostClass, bool isLocal){
    classDef* cls = owningValueElementClass(arr.elementType);
    if(cls == nullptr) return;
    if(isLocal && arr.arraySize <= 0 && arr.declaredExpressionValue == nullptr)
        parsingError(format("Array '{0}' holds '{1}' values, each in an instance of its own, so it needs a "
            "capacity: `array<{1}> {0}[n]`.", arr.dName(), typeDisplayName(arr.elementType)));
    // A class's member array is created with every instance, so an initial element would be shared by all of them.
    if(auto* list = dynamic_cast<initializerList*>(arr.declaredExpressionValue); list != nullptr && !isLocal){
        if(hostClass != nullptr && !list->elements.empty())
            parsingError(format("Array '{0}' holds '{1}' values, so every '{2}' needs instances of its own; a class "
                "can't give it initial elements. Give it a capacity and append them in a routine.", arr.dName(), typeDisplayName(arr.elementType),
                hostClass->dName()));
    }
    languageService.valueElementArrays[cls].push_back({&arr, hostClass, isLocal});
    if(functionDef* copyOp = findAssignOperator(cls, arr.elementType, [](functionDef*){ return true; }, /*allowInherited*/true);
       copyOp != nullptr && !copyOp->isEmitter){
        if(copyOp->i6name.empty()) copyOp->i6name = mangleOperatorName(copyOp->name);
        languageService.valueElementCopyOps[cls] = copyOp->i6name;
    }
}

bool bglParser::processArrayDeclaration(token dataType, token name, string elementType, token symbol, abstractObject& contextObj, bool isExternal, bool isSuperposed, bool isConst) {
    arrayDeclaration& arrDecl = *(new arrayDeclaration());
    // An array's name is already a fixed address and assigning to it copies its contents, so `const`
    // has no binding to fix here (§3.4).
    if(isConst)
        parsingError(format("'const' doesn't apply to array '{0}': an array is storage.",
            name.originalValue.empty() ? (string)name : name.originalValue));
    arrDecl.src = file.currentLocation();
    arrDecl.inLoop = loopDepth > 0;
    arrDecl.name = (string)name;
    if(!isExternal && getCurrentCompileContext() == eCompileContext::global && bglLanguageService::isI6StatementKeyword(arrDecl.name))
        parsingError(format("Array '{0}' is named like an Inform 6 keyword, built-in function or built-in property, which Inform 6 won't take "
                            "as an array's name; rename it.", name.originalValue.empty() ? (string)name : name.originalValue));
    // Named like a member's property: one I6 namespace, so the array takes another name (§3.11).
    if(!isExternal && getCurrentCompileContext() == eCompileContext::global && memberPropertyNamed(arrDecl.name))
        arrDecl.i6name = "_g_" + arrDecl.dName();
    arrDecl.type = languageService.getType(elementType == "char" ? "bytearray" : "array");
    arrDecl.elementType = elementType;
    // An array of arrays of arrays isn't supported: rows of rows don't copy or free as rows do.
    if(isArrayOfArraysElement(elementType) && isArrayOfArraysElement(arrayInnerType(elementType)))
        parsingError(format("Array '{0}': arrays nest two deep at most (`array<array<T>>`); keep a third level in a "
            "class member instead.", name.originalValue.empty() ? (string)name : name.originalValue));
    arrDecl.isExternal = isExternal;
    arrDecl.isSuperposed = isSuperposed;
    arrDecl.literalElements = pendingLiteralElements;
    pendingLiteralElements = false;
    if(elementType == "char") arrDecl.isByteArray = true;
    // A file-scope `rawArray<T>` literal is an UNTRACKED `array<T>`: same count-prefixed layout
    // (word0 = count, data at 1..N, subscript `-->(i+1)`) but with NO <len>+<magic> trailer, even
    // when `<array>` is included. So it keeps the `array` type (subscript/size stay array<T>'s) and
    // only sets isRaw, which gates the emitter's trailer off. This is exactly the shape bare I6 array
    // APIs (orArray's single-array form) expect. (NB: a rawArray *parameter* is a different thing —
    // a data-only `-->` view of an external buffer, subscript `-->i`; see §4.7.1.)
    if((string)dataType.value == "rawarray") arrDecl.isRaw = true;
    // A rawArray is a bare I6 word block with NO housekeeping (no count/length/magic) in EVERY
    // form (spec §4.9.1) — it exists only to hand a raw memory block to I6 code (e.g. glk_ records).
    // Carry the full `rawarray<T>` type so the subscript dispatches to rawArray's flat operator[]
    // (`$val-->$i`, indexing from word 0, no count slot) and size()/length()/for-in are rejected —
    // matching the parameter form. (Members are unaffected: they use the property `.&prop-->n`
    // mechanism, already flat.) type is a per-node value copy, so this doesn't touch the shared type.
    if(arrDecl.isRaw)
        arrDecl.type.name = "rawarray<" + elementType + ">";

    functionDef* func = dynamic_cast<functionDef*>(&contextObj);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    if(func != nullptr && !isExternal) checkLocalVariableShadowing(arrDecl, func, body);

    // `[N]` fixes the capacity; an `=` initializer may follow it to seed the
    // leading slots (`array<T> name[N] = { v1, v2 }` — capacity N, length 2, the
    // remaining N-2 slots zeroed). Without `[N]`, an initializer list sets both
    // capacity and length. Both routes converge on the initializer parsing below.
    bool hasInitializer = symbol.is(token::assignment);
    if(symbol.is(token::bracketOpen)) {
        // array<T> name[N];   or   array<T> name[N] = <initializer>;
        arrDecl.arraySize = readCompileTimeInt("an array's capacity");
        if(arrDecl.arraySize < 1)
            parsingError(format("Array '{0}' needs a capacity of at least 1.", arrDecl.dName()));
        if(targetIsZcode() && arrDecl.arraySize > 32767 - 3)
            parsingError(format("Array '{0}' holds {1} elements; a Z-machine array holds at most 32764 (Glulx has "
                                "no such limit).", arrDecl.dName(), arrDecl.arraySize));
        if(!file.peekToken().is(token::bracketClose))
            parsingError(format("Array '{0}''s capacity is one compile-time integer: a literal, a #define, a const int or a "
                                "setting, not an expression; give it a const of its own.", arrDecl.dName()));
        file.getToken(token::bracketClose);
        hasInitializer = file.getToken({token::endStatement, token::assignment}).is(token::assignment);
    }
    if(hasInitializer) {
        // Check for string initializer: array<char> name = "text";
        token firstVal = file.peekToken(1);
        if(firstVal.is(eTokenType::quote) || firstVal.is(eTokenType::rawQuote)){
            if(elementType == "char" || elementType == "charliteral"){
                token strTok = file.getToken();
                // A byte array holds one byte per character: a character beyond Latin-1 doesn't fit.
                for(size_t at = strTok.value.find("@{"); at != string::npos; at = strTok.value.find("@{", at + 2)){
                    size_t close = strTok.value.find('}', at);
                    if(close != string::npos && stoul(strTok.value.substr(at + 2, close - at - 2), nullptr, 16) > 0xFF)
                        parsingError(format("array<char> '{0}' holds one byte per character, so its text can't "
                            "include a character beyond Latin-1 (U+{1}). Print such text from a string instead.",
                            arrDecl.dName(), strTok.value.substr(at + 2, close - at - 2)));
                }
                arrDecl.stringInitializer = strTok.value;
                file.getToken(token::endStatement);
            } else if(elementType.empty() || elementType == "string" || elementType == "stringliteral" || elementType == "var"){
                // One-element shorthand: `array<var>/<string> name = "s"` ≡ `= {"s"}` — a string is a
                // valid var/string element (mirrors `array<int> x = 3` ≡ {3}). Only array<char> fills
                // a char buffer from a string; every other element type takes the string as one element.
                token strTok = file.getToken();
                initializerList* list = new initializerList();
                list->elements.push_back(parseExpression(strTok, {token::endStatement}, func, body));
                if(file.peekToken().is(token::endStatement)) file.getToken();
                arrDecl.declaredExpressionValue = list;
            } else {
                parsingError(format("String initializer is only valid for array<char>; '{0}' cannot "
                    "hold a string element.", typeDisplayName(elementType)));
            }
        } else if(firstVal.is(token::braceOpen)){
            // array<T> name = { v1, v2, ... };  (nested `{...}` elements bake as inline objects or,
            // for an array-of-arrays element type, anonymous inner arrays — see parseArrayInitializerList)
            file.getToken(token::braceOpen);
            // Said before the list is read: there, the object's name would already mean the array.
            if(func == nullptr)
                if(auto* od = languageService.findGlobalAs<objectDef>((string)name); od != nullptr && !od->isPrePassStub)
                    parsingError(format("array '{0}' has the same name as object '{1}' ({2}); Inform 6 keeps these in one "
                        "namespace, ignoring case, so rename one of them or give one an `asI6` name.",
                        name.originalValue.empty() ? (string)name : name.originalValue, od->dName(), languageService.declaredAt(od->name)));
            initializerList* list = parseArrayInitializerList(elementType, func, body);
            file.getToken(token::endStatement);
            // type-check each element against the declared element type
            if(!elementType.empty() && elementType != "var"){
                for(size_t i = 0; i < list->elements.size(); i++){
                    expression* elem = list->elements[i];
                    if(!elem->resolvedType.empty() && !isArrayElementCompatible(elem->resolvedType, elementType))
                        applyAssignOperatorAsValue(elem, elementType);   // a primitive's `$target = $v` operator =
                    if(!elem->resolvedType.empty() && !zeroFitsBnum(elem, elementType) && !isArrayElementCompatible(elem->resolvedType, elementType))
                        parsingError(format("Array element {0} has type '{1}', expected '{2}'", i, typeDisplayName(elem->resolvedType), typeDisplayName(elementType)));
                    checkByteElementRange(elem, elementType);
                }
            }
            arrDecl.declaredExpressionValue = list;
            if(func == nullptr) adoptInitialElements(arrDecl.name, elementType, list);
        } else {
            // Bare (brace-less) RHS. Two meanings, disambiguated by the RHS's resolved type:
            //
            //   (a) One-element shorthand — `array<T> name = 3;` ≡ `array<T> name = {3};`.
            //       When the RHS resolves to a value compatible with the element type T, it is a
            //       single initializer element, wrapped into a one-element list so it flows through
            //       the identical seed/type-check/emit path as the braced form. (You may drop the
            //       braces when declaring a list of one — mirrors the grammar single-line shorthand.)
            //
            //   (b) Pointer-aliasing init — `array<T> name = makeArr();` / `= otherArray`. The RHS is
            //       an arbitrary expression evaluating to an array pointer (typically a function return,
            //       another array global, or a `var`/subscript holding a buffer handle), which is NOT a
            //       concrete element value. No storage is allocated; the slot just holds the pointer,
            //       and arraySize stays 0 so the emitter's local-array allocation path skips it.
            //
            // Disambiguation is by the RHS's resolved type, and is deliberately CONSERVATIVE so it
            // never reinterprets an existing alias:
            //   - an untyped element (`array<var>`) can't be type-matched, so it keeps aliasing;
            //   - a `var`-typed RHS is opaque (it usually holds a pointer/handle, e.g. a subscript of
            //     `array<var>`), and `var` is compatible with every element type, so it would wrap
            //     everything — exclude it and keep aliasing;
            //   - anything else that is element-compatible (a literal, an object of the element class,
            //     an int for `array<char>`) is the one-element shorthand.
            // The caller can always write explicit braces to force a one-element list in the excluded
            // cases.
            bool canReread = !file.hasPendingToken;
            fileLexer::mark_t exprAt = file.mark();
            set<typeDef*> before(languageService.globals.begin(), languageService.globals.end());
            size_t postAt = postInjections.size();
            token first = file.getToken();
            bool freshBefore = currentFunc != nullptr && currentFunc->makesFresh;
            int marksBefore = freshMarks;
            expression* expr = parseExpression(first, {token::endStatement}, func, body);
            bool elementLike = !elementType.empty() && elementType != "var"
                               && !expr->resolvedType.empty() && expr->resolvedType != "var"
                               && isArrayElementCompatible(expr->resolvedType, elementType);
            if(elementLike){
                checkByteElementRange(expr, elementType);
                initializerList* list = new initializerList();
                list->elements.push_back(expr);
                arrDecl.declaredExpressionValue = list;
            } else if(body == nullptr){
                // Pointer-aliasing / expression init on a STATICALLY-allocated array (a file-scope
                // global or a class/object member). Aliasing only makes sense for a LOCAL, whose slot
                // can hold a runtime pointer; a static array is an I6 `Array` directive with no
                // runtime step to copy or alias another array into it. Emitting one is impossible, so
                // reject it here with guidance instead of crashing the emitter ("unable to emit array")
                // or, when the name is later used, failing with a confusing operator[] type error.
                parsingError(format("Array '{0}': a statically-allocated array (file-scope or class/object "
                                    "member) can't be initialized from another array or a runtime expression. "
                                    "Use a '{{ ... }}' element list here, or assign it inside a routine.",
                                    arrDecl.name));
            } else {
                string target = (string)dataType + "<" + elementType + ">";
                // genericValueFits compares template arguments only; a value that isn't an array at
                // all (no `[]`) would alias as one and index wild memory.
                const string& vt = expr->resolvedType;
                if(!vt.empty() && vt != "var" && vt != "nullliteral" && vt.find('<') == string::npos)
                    if(classDef* vc = getDispatchClass(vt);
                       vc && !findMemberInHierarchy(vc, [](typeMember* m){ return m->name == "[]"; })){
                        if(vt == "childrenprop")
                            parsingError(format("Cannot assign '{0}' to array '{1}': `children` is the live object "
                                "tree, not an array. Take a snapshot with `bgl.world.inParent(…)`.",
                                expr->text(), arrDecl.name));
                        parsingError(format("Cannot assign value of type '{0}' to variable of type '{1}'",
                            typeDisplayName(vt), typeDisplayName(target)));
                    }
                if(!elementType.empty() && !genericValueFits(expr, target, func, body))
                    parsingError(format("Cannot assign value of type '{0}' to variable of type '{1}'",
                        typeDisplayName(elementAwareType(expr, func, body)), typeDisplayName(target)));
                // Arrays are references, so the declaration refers to the value's array, a call's result
                // included. A `bgl.world` query's result lives in a buffer the next query reuses, so that one
                // is copied.
                string srcText = expr->text();
                // `array<char> a = word;` in a routine: a copy with the file-scope source's capacity.
                if(body != nullptr && arrDecl.isByteArray && arrDecl.arraySize == 0 && !arrDecl.isRefLocal && !declaringRefArray
                   && first.is(eTokenType::identifier) && expr->tokens.size() == 1
                   && !(func != nullptr && qualifyFromParams(first.value, func)) && !qualifyFromBodyLocals(first.value, body)
                   && !qualifyFromAncestorBlocks(first.value, body))
                    if(auto* g = languageService.findGlobalAs<arrayDeclaration>(first.value); g != nullptr && g->isByteArray){
                        int cap = g->arraySize > 0 ? g->arraySize
                                : !g->stringInitializer.empty() ? i6Emitter::i6StringChars(g->stringInitializer)
                                : dynamic_cast<initializerList*>(g->declaredExpressionValue)
                                  ? (int)dynamic_cast<initializerList*>(g->declaredExpressionValue)->elements.size() : 0;
                        if(cap > 0) arrDecl.arraySize = cap;
                    }
                if(body != nullptr && arrDecl.isByteArray && arrDecl.arraySize == 0 && !arrDecl.isRefLocal && !declaringRefArray
                   && expr->resolvedType == "bytearray"
                   && srcText.find_last_not_of(" \t") != string::npos && srcText[srcText.find_last_not_of(" \t")] != ')')
                    parsingError(format("'{0}' copies an array<char> of unknown capacity: give it one (`array<char> {0}[n] = …;`), "
                                        "or share it with `ref array<char> {0} := …;`.", arrDecl.dName()));
                bool sharedBuffer = srcText.find("_bglWorld") != string::npos;
                bool holdsHandle = arrDecl.name.rfind("_bgl", 0) == 0
                                || arrDecl.isRaw || !sharedBuffer;
                if(canReread && (arrDecl.arraySize > 0 || !holdsHandle)){
                    // The initializer is copied into the array's own storage, as `name = value;` would.
                    // Without a capacity, the storage is sized from the value when the declaration runs.
                    pendingInjections.clear();
                    postInjections.resize(postAt);
                    // The re-read lifts its lambdas again; the first ones would be dead routines.
                    auto& gl = languageService.globals;
                    gl.erase(remove_if(gl.begin(), gl.end(), [&](typeDef* g){
                        return !before.count(g) && dynamic_cast<functionDef*>(g) && g->name.rfind("_bglLambda_", 0) == 0; }), gl.end());
                    file.rewind(exprAt);
                    copyInitSource = file.getRawTextToStatementEnd();
                    if(arrDecl.arraySize == 0){
                        arrDecl.copySource = "_bglsrc_" + arrDecl.name;
                        arrDecl.copyHeld = "_bglheld_" + arrDecl.name;
                    }
                } else {
                    arrDecl.declaredExpressionValue = expr;
                    // Declared from a chain on the routine's own array of structs: it holds a view of those instances.
                    if(expr->viewSource != nullptr && arrDecl.arraySize == 0) arrDecl.viewOf = expr->viewSource;
                    // A local declared from a call or a step's result owns that array (§12.6); one declared from
                    // another array's name owns a copy of it.
                    if(func != nullptr && body != nullptr && !arrDecl.isRaw && !arrDecl.isByteArray && elementType != "char"){
                        // Only a call's or chain's result is taken; a name, element or cast is copied.
                        size_t last = srcText.find_last_not_of(" \t");
                        bool plainName = last == string::npos || srcText[last] != ')';
                        string mOwner, mProp;
                        if(!arrDecl.isRefLocal && !declaringRefArray && memberArraySource(expr, mOwner, mProp)){
                            // A member array is its property's data: the local gets a copy of it.
                            expr->tokens.assign(1, "_bglHeapCopyOfMember(" + mOwner + ", " + escapePropertyName(mProp) + ")");
                            plainName = false;
                        }
                        if(plainName && !arrDecl.isRefLocal && !declaringRefArray) expr->tokens.assign(1, "_bglHeapCopyOf(" + srcText + ")");
                        if(!plainName && isStaticArrayText(srcText, func, body)) arrDecl.staticSource = true;   // nothing to own
                        else if(!plainName || (!arrDecl.isRefLocal && !declaringRefArray)) giveOwner(arrDecl);
                        // The local takes the whole result (`= f(…)`), so the call left nothing for the routine to sweep.
                        if(!plainName && !freshBefore && currentFunc != nullptr && freshMarks == marksBefore + 1
                           && lastFreshCall == stripSpaces(srcText))
                            currentFunc->makesFresh = false;
                    }
                }
            }
        }
    }
    // else symbol is endStatement: extern/forward declaration — no size or initializer

    // Seeded-capacity form (`array<T> name[N] = …`): the seed has to fit the
    // declared capacity, and a string seed has no defined padding for the
    // SizedBuffer layout, so only the brace form is accepted here.
    if(arrDecl.arraySize > 0){
        if(!arrDecl.stringInitializer.empty())
            parsingError(format("Array '{0}': a string initializer cannot be combined with a declared size. "
                                "Use `array<char> {0} = \"...\";` to size it from the literal, or "
                                "`array<char> {0}[{1}];` and assign elements.",
                                arrDecl.name, arrDecl.arraySize));
        if(auto* seed = dynamic_cast<initializerList*>(arrDecl.declaredExpressionValue);
           seed != nullptr && (int)seed->elements.size() > arrDecl.arraySize)
            parsingError(format("Array '{0}' declares capacity {1} but its initializer supplies {2} elements.",
                                arrDecl.name, arrDecl.arraySize, seed->elements.size()));
    }
    if(auto* seed = dynamic_cast<initializerList*>(arrDecl.declaredExpressionValue);
       seed != nullptr && seed->elements.empty() && arrDecl.arraySize <= 0 && !isExternal)
        parsingError(format("Array '{0}' has no storage: give it a capacity ([n]) or at least one element; declare it 'ref' to share another array.", arrDecl.dName()));
    if(!isExternal && !arrDecl.isRefLocal && !declaringRefArray) noteValueElementArray(arrDecl, nullptr, body != nullptr);

    // An initialized local byte array (`= "…"` or `= {…}`) is drawn from the framePool on each
    // call and filled from a hidden file-scope array holding the initializer, laid out alike.
    if(body != nullptr && arrDecl.isByteArray && arrDecl.arraySize == 0
       && (!arrDecl.stringInitializer.empty() || dynamic_cast<initializerList*>(arrDecl.declaredExpressionValue))){
        auto* src = new arrayDeclaration();
        src->name = format("_bglinit_{0}_{1}_{2}", func != nullptr ? func->name : string("x"), arrDecl.name,
                           localByteInitCounter++);
        src->type = arrDecl.type;
        src->elementType = arrDecl.elementType;
        src->isByteArray = true;
        src->isSynthetic = true;
        src->stringInitializer = arrDecl.stringInitializer;
        src->declaredExpressionValue = arrDecl.declaredExpressionValue;
        src->src = arrDecl.src;
        languageService.globals.push_back(src);
        arrDecl.localInitSource = src->name;
        arrDecl.stringInitializer.clear();
        arrDecl.declaredExpressionValue = nullptr;
    }

    if(arrDecl.literalElements)
        if(auto* list = dynamic_cast<initializerList*>(arrDecl.declaredExpressionValue))
            checkLiteralElements(arrDecl, list->elements);
    // A verb named as an element is its action (`{ Take, Drop }` → ##Take ##Drop).
    if(arrDecl.elementType == "verb")
        if(auto* list = dynamic_cast<initializerList*>(arrDecl.declaredExpressionValue))
            for(expression* e : list->elements) if(e != nullptr) applyActionConstant(e);

    if(body != nullptr && !arrDecl.ownerVar.empty()) declareOwner(func, arrDecl);
    if(body != nullptr){
        // Set-up an element needs (a `?.` test, a temp) runs before the elements are stored.
        for(statement* inj : pendingInjections) body->statements.push_back(inj);
        pendingInjections.clear();
        string valueSource = copyInitSource;
        copyInitSource.clear();
        string srcPath = valueSource;
        srcPath.erase(0, srcPath.find_first_not_of(" \t\r\n"));
        srcPath.erase(srcPath.find_last_not_of(" \t\r\n") + 1);
        string srcOwner, srcProp;
        bool plainPath = !srcPath.empty() && srcPath.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.") == string::npos;
        if(!arrDecl.copySource.empty() && plainPath && splitQualifiedMember(srcPath, func, body, srcOwner, srcProp)
           && !memberArrayIsRef(srcOwner, srcProp, func, body)){
            // A member array is property data: size the storage from its length, then copy as `name = value;`.
            file.openText("int " + arrDecl.copyHeld + ";", arrDecl.src.file, arrDecl.src.line);
            try { processStatementDispatch(file.getToken(), *func); } catch(...) { file.close(); throw; }
            file.close();
            file.openText(srcPath + ".length;", arrDecl.src.file, arrDecl.src.line);
            expression* len = nullptr;
            try { len = parseExpression(file.getToken(), {token::endStatement}, func, body); }
            catch(...) { file.close(); throw; }
            file.close();
            arrDecl.copyLength = len->text();
            arrDecl.copySource.clear();
        }
        if(!arrDecl.copySource.empty()){
            string text = "int " + arrDecl.copyHeld + "; " + (string)dataType + "<" + elementType + "> "
                        + arrDecl.copySource + " = " + valueSource + ";";
            file.openText(text, arrDecl.src.file, arrDecl.src.line);
            try { for(int n = 0; n < 2; n++) processStatementDispatch(file.getToken(), *func); }
            catch(...) { file.close(); throw; }
            file.close();
            valueSource = arrDecl.copySource;
        }
        body->statements.push_back(&arrDecl);
        if(!valueSource.empty()){
            string text = arrDecl.name + " = " + valueSource + ";";
            file.openText(text, arrDecl.src.file, arrDecl.src.line);
            try { processStatementDispatch(file.getToken(), *func); } catch(...) { file.close(); throw; }
            file.close();
        }
    } else
        languageService.registerInstance(arrDecl);
    return false;
}

//--process a variable declaration statement and add it to the parse tree
//-- handles both simple declarations and declarations with assignment
//-- also handles parameter declarations within function definitions
//-- e.g.:
//--      int myVar;
//--      int myVar=99;
//--      int myParam)
//--      int myParam=5) 
//--      int myParam=5, ...

// Warns on a local declaration whose name shadows a global, a class/object member or a capturable
// outer variable, and errors on a duplicate in the same scope. No-op at file scope.
void bglParser::checkLocalVariableShadowing(const variableDeclaration& varDecl, functionDef* func, statementBlock* body){
    // Disallow local variable names that shadow a global, a class member, or an object member;
    // also disallow duplicate declarations within the same scope.
    if(func != nullptr){
        // Duplicate in same scope
        if(body != nullptr)
            for(statement* s : body->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == varDecl.name && !vd->scopeEnded)
                        parsingError(format("Variable '{0}' is already declared in this scope", varDecl.name));
        // A parameter is the routine's outermost scope, and it occupies a header slot of its own —
        // re-declaring its name emits `[f z z;`, which I6 rejects as "Local variable defined twice".
        // Same rule as the enclosing-block case below, reported here rather than by Inform 6.
        for(paramDef* pd : func->params)
            if(pd->name == varDecl.name)
                parsingError(format("Variable '{0}' is already declared as a parameter of this "
                    "function, so it must have a different name.", varDecl.name));
        // Re-declaring a local that an ENCLOSING block still has open. I6 has no block scope: every
        // local is hoisted to the routine header, one slot per name, so the inner declaration would
        // not be a second variable — it would write through to the outer one, and the outer would
        // keep the inner's value after the block closed. Sibling blocks may reuse a name freely,
        // their lifetimes not overlapping; it is only the nested case that cannot mean what it says.
        for(statementBlock* blk : activeBlockStack)
            if(blk != nullptr && blk != body)
                for(statement* s : blk->statements)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == varDecl.name && !vd->scopeEnded)
                            parsingError(format("Variable '{0}' is already declared in an enclosing block. "
                                "A nested declaration would share the outer variable's storage, not shadow it, "
                                "so it must have a different name.", varDecl.name));
        if(currentFunc != nullptr && currentFunc->fileScopeReads.count(varDecl.name))
            parsingError(format("'{0}' is read above as the global, then declared here as a local; Inform 6 would "
                "read the local throughout the routine. Rename the local, or declare it before its first use.", varDecl.dName()));
        for(typeDef* g : languageService.globals)
            if(g->name == varDecl.name){
                if(auto* vd = dynamic_cast<variableDeclaration*>(g)){
                    const string& t = vd->type.name;
                    if(t == "grammartoken" || t == "attribute" || t == "property" || t == "verb") continue;
                }
                if(dynamic_cast<verbObjectDef*>(g) != nullptr) continue;   // a verb's name is exempt (§3.10)
                if(auto* cd = dynamic_cast<classDef*>(g))
                    parsingWarning("Local variable '" + varDecl.name + "' has the name of " + (cd->isUnion() ? "union '" : "class '") + g->dName()
                        + "'; in this routine the name means the variable, though the class still declares types.");
                else
                    parsingWarning("Local variable '" + varDecl.name + "' shadows " + shadowedGlobal(g) + "; '::" + varDecl.name + "' reaches it.");
                break;   // one warning, however many overloads the name has
            }
        if(definedSymbols.count(varDecl.name))
            parsingWarning("Local variable '" + varDecl.dName() + "' has the name of a #define symbol; in this "
                           "routine the name means the variable.");
        if(currentClass != nullptr){
            for(typeMember* m : currentClass->members)
                if(m->name == varDecl.name)
                    parsingWarning("Local variable '" + varDecl.dName() + "' shadows a member of class '" + currentClass->dName() + "'.");
            // Walk base class hierarchy for inherited members (vars and functions) — warning only
            function<void(classDef*)> checkBases = [&](classDef* c){
                for(typeMember* m : c->members)
                    if(m->name == varDecl.name)
                        if(dynamic_cast<variableDeclaration*>(m) || dynamic_cast<functionDef*>(m))
                            parsingWarning("Local variable '" + varDecl.name + "' shadows inherited member '" + varDecl.name + "' from class '" + c->dName() + "'.");
                for(classDef* base : c->baseClasses) checkBases(base);
            };
            for(classDef* base : currentClass->baseClasses) checkBases(base);
        }
        if(currentObject != nullptr)
            for(typeMember* m : currentObject->members)
                if(m->name == varDecl.name)
                    warnOnceHere("Local variable '" + varDecl.name + "' shadows a member of object '" + currentObject->name + "'.");
        // Inside a lambda: warn if local shadows a capturable outer variable (param or local
        // from the enclosing function). Without this warning, the outer variable becomes
        // silently unreachable — the lambda-local wins at Tier 1, preventing Tier 7 capture.
        if(lambdaOuterFunc != nullptr){
            for(paramDef* p : lambdaOuterFunc->params)
                if(p->name == varDecl.name){
                    parsingWarning("Local variable '" + varDecl.name + "' shadows capturable parameter '" + varDecl.name + "' from enclosing function.");
                    break;
                }
            statementBlock* rootBody = dynamic_cast<statementBlock*>(lambdaOuterFunc->body);
            if(rootBody != nullptr){
                function<bool(statementBlock*)> findInBlock = [&](statementBlock* blk) -> bool {
                    if(!blk) return false;
                    for(statement* s : blk->statements)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                            if(vd->name == varDecl.name) return true;
                    return false;
                };
                // Check immediate enclosing body and function root body
                bool found = false;
                if(lambdaOuterBody != nullptr) found = findInBlock(lambdaOuterBody);
                if(!found && rootBody != lambdaOuterBody) found = findInBlock(rootBody);
                if(found)
                    parsingWarning("Local variable '" + varDecl.name + "' shadows capturable local '" + varDecl.name + "' from enclosing function.");
            }
        }
    }
}

// `Type name = {…}` / `Type name = Type{…}` at FILE SCOPE on an object-backed class: bakes the
// fields straight into the object `name` (§6.2.1) and consumes the trailing ';'. Returns true when
// it folded — the object then IS the declaration. False leaves the tokens for the initializer paths.
bool bglParser::foldInlineObjectAggregateDeclaration(token dataType, token variableName, token first,
                                                     functionDef* func, statementBlock* body){
        classDef* declCls = getDispatchClass((string)dataType);
        if(declCls != nullptr && isObjectBackedClass(declCls)){
            classDef* rhsCls = nullptr;
            bool bareBrace = first.is(token::braceOpen);
            bool explicitBrace = (first.is(eTokenType::dataType) || first.is(eTokenType::identifier))
                                 && (rhsCls = getDispatchClass(first.value)) != nullptr
                                 && file.peekToken().is(token::braceOpen);
            if(bareBrace || explicitBrace){
                classDef* bakeCls = bareBrace ? declCls : rhsCls;
                // An explicit RHS type must be assignment-compatible (the declared type or a subclass).
                if(explicitBrace && bakeCls != declCls){
                    function<bool(classDef*)> isA = [&](classDef* c)->bool{
                        if(!c) return false;
                        if(c == declCls || c->name == declCls->name) return true;
                        for(classDef* b : c->baseClasses) if(isA(b)) return true;
                        return false;
                    };
                    if(!isA(bakeCls))
                        parsingError(format("'{0} {1} = {2}{{...}}': inline object type '{2}' is not compatible with declared type '{0}'",
                                            (string)dataType, (string)variableName, first.value));
                }
                if(explicitBrace) file.getToken(token::braceOpen);   // consume '{' (bare: `first` is it)
                // The pre-pass saw the `=` and registered `name` as a class-typed variable stub;
                // drop it so the folded object below is the sole declaration of `name` (otherwise
                // both an empty `rule name;` and the baked `rule name with …` would be emitted).
                {
                    string lname = (string)variableName;
                    transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
                    auto& gl = languageService.globals;
                    gl.erase(std::remove_if(gl.begin(), gl.end(), [&](typeDef* g){
                        auto* vd = dynamic_cast<variableDeclaration*>(g);
                        return vd && vd->name == lname && vd->isPrePassStub;
                    }), gl.end());
                }
                bakeInlineObjectAggregate(bakeCls, bareBrace ? (string)dataType : first.value,
                                          (string)variableName, func, body);
                file.getToken(token::endStatement);   // consume trailing ';'
                return true;    // the object IS the declaration — no variable/assignment emitted
            }
        }
    return false;
}

// For an `auto` declaration, resolves the variable's type from the initializer expression (via the
// RHS type's `operator auto()` when it has one) and updates dataType for downstream checks. No-op
// when the declaration is not `auto`.
void bglParser::inferAutoVariableType(variableDeclaration& varDecl, token& dataType, bool isAuto, expression* rhs){
    // auto type inference: resolve type from RHS, checking operator auto() for type promotion
    if(isAuto && rhs != nullptr && !rhs->resolvedType.empty()){
        string inferredType = rhs->resolvedType;
        // Check if the RHS type has operator auto() — use its return type instead
        classDef* rhsCls = languageService.classOf(inferredType);
        if(rhsCls){
            for(typeMember* m : rhsCls->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == "auto"){ inferredType = fd->returnType.name; break; }
        }
        varDecl.type = languageService.getType(inferredType);
        if(varDecl.type.name.empty()) varDecl.type.name = inferredType;
        dataType.value = inferredType;  // update for downstream type checks
    } else if(isAuto){
        parsingError("Cannot infer type from initializer expression");
    }
}

// Verifies the initializer expression is accepted by one of the declared class type's operator=
// signatures, capturing an emitter body (or synthesizing a non-emitter `_opeq` dispatch) into
// varDecl, and falling back to the RHS type's conversion `operator()` emitter.
void bglParser::checkVariableInitializerAssignable(variableDeclaration& varDecl, token dataType,
                                                   expression* rhs, bool isRef){
    //type check: if the declared type is a class, verify the assigned value is accepted by one of its operator= signatures.
    // `ref` locals opt out of all operator= dispatch — they're plain pointer-alias.
    // getDispatchClass (not getType) so a template-typed LHS (`array<int> keep = chain;`)
    // reaches operator= dispatch — this is the primary array copy-on-assign capture form.
    if(!isRef) applyImplicitConversion(rhs, (string)dataType);
    if(!isRef && (string)dataType == "verb") applyActionConstant(rhs);
    // A union holds whichever member's value it is given, so no member's `operator =` applies.
    classDef* classType = isUnionType((string)dataType) ? nullptr : getDispatchClass((string)dataType);
    if((classType == nullptr || ((string)dataType).find('<') != string::npos) && rhs != nullptr && !isRef)
        checkClasslessAssignable(rhs, (string)dataType, "variable");
    if(classType != nullptr && rhs != nullptr && !isRef) rejectUnionInto(rhs, classType);
    if(classType != nullptr && rhs != nullptr && !isRef){
        string valueTypeName = rhs->resolvedType;
        if(!valueTypeName.empty()){
            // The class's `operator =` for this value, most specific parameter first (findAssignOperator).
            functionDef* assignOp = findAssignOperator(classType, valueTypeName,
                [](functionDef*){ return true; }, /*allowInherited*/true, (string)dataType);
            // Template-aware match: param and RHS resolving to the same dispatch class
            // (e.g. operator=(array<T>) for an array<int> RHS). Mirrors the assignment-
            // statement path — backs `array<int> keep = chain;` copy-on-assign.
            if(!assignOp){
                classDef* valCls = getDispatchClass(valueTypeName);
                if(valCls != nullptr) assignOp = dynamic_cast<functionDef*>(findMemberInHierarchy(classType, [&](typeMember* m){
                    auto* fn = dynamic_cast<functionDef*>(m);
                    return fn && fn->name=="=" && fn->params.size()==1
                           && getDispatchClass(fn->params[0]->type.name) == valCls
                           && templateArgsFit(valueTypeName, (string)dataType);
                }));
                // Note: the declaration-init path has no upcast pass, so an array<char>
                // (byteArray) RHS never matches array<T>'s copy operator= here (byteArray's
                // dispatch class != array). It falls through to pointer-alias — safe (no word
                // copy, no corruption), consistent with char arrays being ephemeral for now.
            }
            bool found = assignOp != nullptr || isTypeCompatible(valueTypeName, (string)dataType);
            // If operator= is an emitter, capture its body so the emitter uses it instead of plain assignment
            // Skip if RHS contains $target — the opcode handles its own store
            if(assignOp && assignOp->isEmitter && rhs->text().find("$target") == string::npos){
                if(auto* blk = dynamic_cast<i6Block*>(assignOp->body)){
                    // Pre-substitute $class — the declared LHS type. Mirrors the
                    // assignment-statement path; $self/$target stay deferred to emit time.
                    emitterBindings cb; cb.cls = classType->i6Name();
                    varDecl.initEmitterBody  = expandEmitterBody(blk, cb);
                    varDecl.initEmitterParam = assignOp->params[0]->name;
                }
            }
            // Non-emitter operator=: mirror the assignment-statement dispatch
            // (bglParser.cpp resolveEmitter, non-emitter branch). Synthesize a
            // one-line dispatch body that calls the mangled `_opeq` routine
            // exactly once with the RHS as its argument. Without this, declarations
            // like `Window w = factory();` fell through to plain pointer-assign
            // even when the user had defined a copy operator on the class.
            //
            // Gated on `classHasStoredFields && !isReferenceBacked` to match the
            // backing-synthesis predicate: a reference class's local has no backing
            // object to dispatch operator= on, so the call would target `nothing`.
            // A value class with stored fields has one, so dispatch lands correctly.
            //
            // A pre-scan stub operator= is ACCEPTED here (order-independence): when the
            // class is defined after the use site it's only a stub at emit time, but the
            // stub carries the operator's param types (pre-scan) and its dispatch name is
            // the mangled `_opeq` the real definition will emit — so `$target._opeq(rhs)`
            // resolves at link time. `!params.empty()` directly guards the params[0] access,
            // so a stub need not be excluded wholesale.
            else if(assignOp && !assignOp->isEmitter && !assignOp->params.empty()
                      && classHasStoredFields(classType) && !isReferenceBacked(classType)){
                if(assignOp->i6name.empty()) assignOp->i6name = mangleOperatorName(assignOp->name);
                string paramName = assignOp->params[0]->name;
                // An ancestor's operator (`Gap g = aLength;` takes Length's) is called as that class's own.
                string routine = assignOp->i6name;
                if(classDef* owner = classType->declaringClass(assignOp); owner != nullptr && owner != classType)
                    routine = owner->i6Name() + "::" + routine;
                varDecl.initEmitterBody  = format("$target.{0}(${1});", routine, paramName);
                varDecl.initEmitterParam = paramName;
            }
            // Mirror the assignment-statement check: TypeCompatible fallback with no
            // operator= on a stored-field, non-tree-citizen class would emit silent
            // pointer-assign and surprise the user expecting value-semantics.
            if(found && assignOp == nullptr && classHasStoredFields(classType) && !isReferenceBacked(classType))
                parsingError(format("'{0}' is a struct with no copy operator ('operator =' taking '{0}'), so there "
                                    "is nothing to initialise with. Declare one, or bind a reference ('ref {0} x := …').",
                    typeDisplayName((string)dataType)));
            if(!found){
                // Fallback: check if RHS type has emitter DeclaredType operator(){}
                classDef* rhsCls = languageService.classOf(valueTypeName);
                if(rhsCls != nullptr)
                    if(typeMember* m = findMemberInHierarchy(rhsCls, [&](typeMember* m){
                        auto* opFn = dynamic_cast<functionDef*>(m);
                        return opFn && opFn->name=="operator()" && opFn->params.empty() && opFn->isEmitter && !opFn->isExplicit
                               && opFn->returnType.name==(string)dataType && dynamic_cast<i6Block*>(opFn->body)!=nullptr;
                    })){
                        auto* opFn = dynamic_cast<functionDef*>(m);
                        auto* blk = dynamic_cast<i6Block*>(opFn->body);
                        emitterBindings cv; cv.self = rhs->text();
                        string b = expandEmitterBody(blk, cv);
                        rhs->tokens.clear();
                        rhs->tokens.push_back(b);
                        rhs->resolvedType = (string)dataType;
                        found = true;
                    }
            }
            if(!found) parsingError(format("Cannot assign value of type '{0}' to variable of type '{1}'", typeDisplayName(valueTypeName), typeDisplayName((string)dataType)));
        }
    }
}

// Parses the `= …` / `:= …` initializer onto varDecl: the file-scope inline-aggregate fold, a `{…}`
// initializer list, an interpolated string literal, or a general expression (with `auto` inference
// and operator= checking). Returns true when the declaration was folded away and is fully handled.
bool bglParser::parseVariableInitializer(variableDeclaration& varDecl, token& dataType, token variableName,
                                         token symbol, bool isAuto, bool isRef, functionDef* func, statementBlock* body){
    if(symbol.value==token::assignment || symbol.value==token::bindAssignment){
        token first = file.getToken();
        if(first.is(token::endStatement))
            parsingError(format("'{0}' has '{1}' but no value after it.", varDecl.dName(), symbol.value));
        // Inline-aggregate folding: for a FILE-SCOPE object-backed class variable, `Type name = {...}`
        // and `Type name = Type{...}` are the same declaration as `Type name {...}` — bake the fields
        // straight into the object `name` (§6.2.1) instead of building an intermediate anonymous object
        // and an invalid `Type name = _bglanon`. Value/collection classes are NOT object-backed (they
        // take an init-list operator=), so they fall through to the initializer-list path below.
        if(func == nullptr){
            if(foldInlineObjectAggregateDeclaration(dataType, variableName, first, func, body))
                return true;   // folded into the object declaration — nothing more to build
        }
        classDef* declCls = getDispatchClass((string)dataType);
        if(first.is(token::braceOpen) && declCls != nullptr && (!isObjectBackedClass(declCls) || func != nullptr)
           && isInlineConstructible(declCls) && ((string)dataType).find('<') == string::npos){
            // A class with `inline` members: `Pair q = { 5, 6 };` is `Pair q = Pair{ 5, 6 };`.
            string anonName = format("_bglanon{0}", anonObjectCounter++);
            bakeInlineObjectAggregate(declCls, (string)dataType, anonName, func, body);
            file.getToken(token::endStatement);
            expression* rhs = new expression();
            rhs->tokens.push_back(anonName);
            rhs->resolvedType = (string)dataType;
            varDecl.declaredExpressionValue = rhs;
            checkVariableInitializerAssignable(varDecl, dataType, rhs, isRef);
        } else if(first.is(token::braceOpen)){
            // initializer list: { expr, expr, ... }
            initializerList* list = new initializerList();
            token t = file.getToken();
            while(!t.is(token::braceClose) && !t.is(eTokenType::eof)){
                expression* elem = parseExpression(t, {",", token::braceClose}, func, body);
                list->elements.push_back(elem);
                if(elem->terminator == token::braceClose) break;
                t = file.getToken();
            }
            // consume the trailing semicolon
            file.getToken(token::endStatement);

            // type-check each element against the declared element type
            string dtStr = (string)dataType;
            string expectedElemType;
            // For array<T>, extract T as the expected element type
            if(dtStr.size() > 6 && dtStr.substr(0, 6) == "array<" && dtStr.back() == '>')
                expectedElemType = dtStr.substr(6, dtStr.size() - 7);
            // Fallback: infer from the class's single-param method
            if(expectedElemType.empty()){
                classDef* listClass = languageService.findClass(dtStr);
                if(listClass != nullptr)
                    for(typeMember* m : listClass->members)
                        if(auto* fd = dynamic_cast<functionDef*>(m))
                            if(fd->params.size() == 1){ expectedElemType = fd->params[0]->type.name; break; }
            }
            for(size_t i = 0; i < list->elements.size(); i++){
                expression* elem = list->elements[i];
                if(elem->resolvedType.empty())
                    parsingError(format("Undeclared identifier in initializer list (element {0})", i));
                else if(!expectedElemType.empty() && !zeroFitsBnum(elem, expectedElemType) && !isArrayElementCompatible(elem->resolvedType, expectedElemType))
                    parsingError(format("Element {0} has type '{1}', expected '{2}'", i, elem->resolvedType, expectedElemType));
                checkByteElementRange(elem, expectedElemType);
            }

            varDecl.declaredExpressionValue = list;
        } else if(first.is("$") && file.peekToken(1).is(eTokenType::quote)){
            // Interpolated string literal initializer: string s = $"..."
            varDecl.interpSegments = parseInterpolatedSegments(func, body);
            endInterpolatedStatement();
            expression* rhs = new expression();
            rhs->resolvedType = "interpolatedstringliteral";
            varDecl.declaredExpressionValue = rhs;
            // Emitter lookup for operator=(interpolatedstringliteral)
            classDef* classType=languageService.findClass((string)dataType);
            if(classType != nullptr){
                functionDef* assignOp = dynamic_cast<functionDef*>(findMemberInHierarchy(classType, [&](typeMember* m){
                    auto* fn = dynamic_cast<functionDef*>(m);
                    return fn && fn->name=="=" && fn->params.size()==1 && fn->params[0]->type.name=="interpolatedstringliteral";
                }));
                if(assignOp && assignOp->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(assignOp->body)){
                        varDecl.initEmitterBody  = expandEmitterBody(blk, {});
                        varDecl.initEmitterParam = assignOp->params[0]->name;
                    }
                }
                if(!assignOp)
                    parsingError(format("Type '{0}' has no operator=(interpolatedStringLiteral) emitter", (string)dataType));
            }
        } else {
            // Set expected type for the RHS so name resolution can disambiguate (e.g. an
            // enum value of the declared type wins over a same-name value of an unrelated enum).
            // For `auto`, no expected type is known until we see the RHS — leave it empty.
            string savedExpected = currentExpectedType;
            if(!isAuto) currentExpectedType = (string)dataType;
            bool savedForBind = readingForBind;
            readingForBind = symbol.value == token::bindAssignment;
            expression* rhs = parseExpression(first, {token::endStatement}, func, body);
            readingForBind = savedForBind;
            currentExpectedType = savedExpected;
            varDecl.declaredExpressionValue = rhs;

            inferAutoVariableType(varDecl, dataType, isAuto, rhs);
            if(!isRef && symbol.value == token::assignment) rejectNullIntoValueClass(rhs, languageService.findClass((string)dataType));

            checkVariableInitializerAssignable(varDecl, dataType, rhs, isRef);
        }
    }
    return false;
}

// A class-typed function local needs a global backing instance for value-semantics operator= to
// dispatch against; synthesizes one and points varDecl.i6name at it when the class qualifies.
void bglParser::synthesizeClassLocalBacking(variableDeclaration& varDecl, bool isExternal, bool isConst,
                                            bool isRef, functionDef* func, statementBlock* body, bool hasValue){
    // Class-typed function locals need backing storage for value-semantics assignment to
    // dispatch correctly. A bare int local slot can't be the target of `operator=` —
    // `localA._opeq(rhs)` would dispatch against the slot's int contents (initially 0 =
    // `nothing` in I6), not a real object. Synthesize a global I6 object instance per
    // (routine, local) and route source references to it via i6name. Gated on:
    //   (a) class has own stored fields (otherwise no state to back), AND
    //   (b) class does NOT inherit from `object` (tree citizens use reference semantics
    //       via inherited operator=(object); synthesizing backing for them would
    //       zero-init `parent`/`child`/`sibling`/`attributes` and yank objects out of
    //       the world tree), AND
    //   (c) the local isn't `ref`-qualified (the user opted into pointer-alias).
    // The error path (silent value-semantics-without-operator= gap) already fires upstream
    // for plain classes without operator= — this synthesis covers the happy path where
    // operator= exists and the user expects per-local independent state.
    if(func != nullptr && body != nullptr && !isExternal && !isConst && !isRef){
        classDef* cls = languageService.findClass(varDecl.type.name);
        // A plain local of a reference class with no value owns an object of its own as well.
        bool ownsReference = plainSlotOwnsReferenceInstance(cls) && (!hasValue || hasCopyOperator(cls));
        if(cls && (ownsReference || (!cls->isEmitterClass && !cls->isAlias && !cls->isExternal
                 && classHasStoredFields(cls) && !isReferenceBacked(cls)))){
            // Named like a parameter backing: the class context keeps same-named methods of two
            // classes apart, and an operator goes by its mangled name (`+` is no I6 identifier).
            bool isOperator = func->name == "operator()"
                || (!func->name.empty() && !isalpha((unsigned char)func->name[0]) && func->name[0] != '_');
            string fnPart = isOperator ? mangleOperatorName(func->name) : func->name;
            string context = currentClass != nullptr ? currentClass->name
                           : currentObject != nullptr ? currentObject->name : string();
            string backingName = "_bglLocal_" + (context.empty() ? "" : context + "_") + fnPart + "_" + varDecl.name;
            if(!paramBackingNames.insert(backingName).second){
                int n = 2;
                while(!paramBackingNames.insert(backingName + "_" + to_string(n)).second) n++;
                backingName += "_" + to_string(n);
            }
            variableDeclaration* backing = new variableDeclaration();
            backing->name = backingName;
            backing->displayName = backingName;
            backing->type = varDecl.type;
            backing->src = varDecl.src;
            backing->isInstanceBacking = true;
            languageService.registerInstance(*backing);
            varDecl.backingName = backingName;   // the local itself stays a variable, set to this at entry
            // On the routine itself: a nested block parses against a functionDef of its own.
            (currentFunc != nullptr ? currentFunc : func)->ownBackings.push_back({backingName, varDecl.type.name});
            varDecl.isClassLocalWithBacking = true;
        }
    }
}

// Places the finished declaration into the enclosing body (local) or the global registry, then for a
// local injects the type's init emitter ahead of it and registers its deinit emitter as a cleanup.
void bglParser::registerVariableDeclaration(variableDeclaration& varDecl, bool isConst, functionDef* func, statementBlock* body){
    if(varDecl.isLiteral)
        checkLiteralValue(isUnionType(varDecl.type.name) ? splitUnionType(varDecl.type.name) : vector<string>{varDecl.type.name},
            varDecl.type.name, varDecl.declaredExpressionValue, format("'{0}' is literal: it needs a literal {{KIND}}", varDecl.dName()));
    if(!varDecl.isExternal) rejectValueWordName(varDecl.name);
    if(body != nullptr){
        body->statements.push_back(&varDecl);
        // What the initializer left to do after it runs (a lambda's captured-variable copy-back).
        for(statement* inj : postInjections) body->statements.push_back(inj);
        postInjections.clear();
    }
    else
        languageService.registerInstance(varDecl);

    // init / deinit: if the variable's type is a class with init/deinit emitters, inject them
    // For local variables: inject init as an i6RawNode and register deinit in func->cleanups.
    // For global variables: record init in languageService.globalInits for the synthesised bglInit routine.
    // A `ref` local OWNS NOTHING — it is a pointer alias to storage someone else holds
    // (§ ref locals). Running the type's init would allocate an instance the initializer
    // immediately overwrites (a leak), and running deinit would FREE THE REFERENT, handing
    // another owner's instance back to the pool for the next allocation to reuse.
    if(!isConst && body != nullptr && func != nullptr && !varDecl.isRefLocal){
        classDef* cls = languageService.findClass(varDecl.type.name);
        // A reference-class local refers to an instance it doesn't own, as a parameter does (§8.5).
        if(cls != nullptr && !cls->isPrimitive && !cls->isEmitterClass && !cls->isAlias && !isValueClass(cls))
            cls = nullptr;
        if(cls != nullptr){
            for(typeMember* m : cls->members){
                functionDef* fn = dynamic_cast<functionDef*>(m);
                if(fn == nullptr || !fn->isEmitter || fn->params.size() != 0) continue;
                // Only init/deinit are wanted here. The name test used to come AFTER the body was
                // expanded, so declaring a local expanded every zero-argument emitter the class
                // happened to declare — wasted work while bodies were inert text, and an outright
                // error once a body can contain $i6Expr.
                if(fn->name != "init" && fn->name != "deinit") continue;
                i6Block* blk = dynamic_cast<i6Block*>(fn->body);
                if(blk == nullptr) continue;
                emitterBindings ib; ib.self = varDecl.name; ib.val = varDecl.name; ib.trim = emitterTrim::ws;
                ib.selfType = varDecl.type.name;
                string substituted = expandEmitterBody(blk, ib);
                if(substituted.empty()) continue;
                if(fn->name == "init"){
                    i6RawNode& initNode = *(new i6RawNode());
                    initNode.text = substituted;
                    initNode.cooked = true;   // names the local: it follows the local's rename
                    // Before the initializer's own set-up too: a call it hoisted may hand back a pool slot
                    // that a later allocation would take over.
                    long at = (long)body->statements.size() - 1;
                    if(declInjectionStart >= 0 && declInjectionStart < at) at = declInjectionStart;
                    body->statements.insert(body->statements.begin() + at, &initNode);
                } else if(fn->name == "deinit"){
                    // A local declared in a nested block dies with that block, so its deinit runs
                    // at the block's end rather than the routine's. It could not be registered on
                    // `func` in any case: a nested block parses against a throwaway functionDef
                    // whose cleanups are discarded, which is why a `stringObj` declared inside an
                    // `if` was never freed at all.
                    if(activeBlockStack.size() > 1 && !activeBlockStack.empty() && body == activeBlockStack.back())
                        blockCleanups[body].push_back(substituted);
                    else
                        func->cleanups.push_back({varDecl.name, substituted});
                }
            }
        }
    }
}

// File-scope counterpart: records the type's init emitter — and, when the declaration has a value,
// the operator= emitter that applies it — in languageService.globalInits for the bglInit routine.
// True when a global's initializer text is something Inform 6 will NOT accept in a `Global`
// directive, which takes a compile-time constant. Deliberately conservative: it answers yes only
// for the two shapes that are unmistakably runtime work — a call, and a read of another global's
// storage — so an initializer that compiles today still emits as a directive and costs nothing.
// Everything else (literals, constant arithmetic, an object or routine name, an enum value, a
// `#define`d value) stays where it is.
static bool initializerNeedsRuntime(const string& text){
    auto isIdentChar = [](char c){ return isalnum((unsigned char)c) || c == '_'; };
    for(size_t i = 0; i < text.size(); i++){
        if(!isIdentChar(text[i])) continue;
        size_t start = i;
        while(i < text.size() && isIdentChar(text[i])) i++;
        string word = text.substr(start, i - start);
        if(isdigit((unsigned char)word[0])) continue;          // a number, not a name
        // A call: the name is followed by '(' (possibly across spaces).
        size_t j = text.find_first_not_of(" \t", i);
        if(j != string::npos && text[j] == '(') return true;
        // A member read (`nodea.pri`): an object's property is storage, read when the program runs.
        if(j != string::npos && text[j] == '.' && j + 1 < text.size() && (isalpha((unsigned char)text[j + 1]) || text[j + 1] == '_'))
            return true;
        // A `?.`/`??` temporary: worked out by statements, which run at startup.
        if(word.rfind("_bgl_temp", 0) == 0) return true;
        // A read of another global's storage. An object, class, enum or routine name is a
        // constant in I6, and a `const` global was folded to its value before it got here.
        string lower = word;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        for(typeDef* g : languageService.globals){
            if(g->name != lower) continue;
            auto* vd = dynamic_cast<variableDeclaration*>(g);
            if(vd == nullptr || vd->isConst) break;
            if(dynamic_cast<arrayDeclaration*>(vd)) break;     // an array name is its address
            const string& t = vd->type.name;
            if(t == "attribute" || t == "property" || t == "verb" || t == "grammartoken") break;
            return true;
        }
    }
    return false;
}

// Every global a deferred initializer reads has to be declared ahead of bglInit too — I6 wants a
// variable defined before use, and bglInit is emitted before the program's own declarations.
// Marking them moves their whole declaration, initializer included.
static void markGlobalsReadEarly(const string& text){
    auto isIdentChar = [](char c){ return isalnum((unsigned char)c) || c == '_'; };
    for(size_t i = 0; i < text.size(); i++){
        if(!isIdentChar(text[i])) continue;
        size_t start = i;
        while(i < text.size() && isIdentChar(text[i])) i++;
        string word = text.substr(start, i - start);
        if(isdigit((unsigned char)word[0])) continue;
        transform(word.begin(), word.end(), word.begin(), ::tolower);
        for(typeDef* g : languageService.globals)
            if(g->name == word)
                if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                    if(!vd->isExternal) vd->needsEarlyGlobalDecl = true;
    }
}

void bglParser::recordGlobalVariableInit(variableDeclaration& varDecl, bool isConst, functionDef* func, statementBlock* body){
    const string gref = string(kGlobalEscapeMarker) + varDecl.name;   // mapped to its I6 name at emission
    // Global scope: record init body in globalInits for bglInit
    if(!isConst && body == nullptr && func == nullptr){
        classDef* cls = languageService.findClass(varDecl.type.name);
        if(cls != nullptr){
            for(typeMember* m : cls->members){
                functionDef* fn = dynamic_cast<functionDef*>(m);
                if(fn == nullptr || !fn->isEmitter || fn->params.size() != 0 || fn->name != "init") continue;
                i6Block* blk = dynamic_cast<i6Block*>(fn->body);
                if(blk == nullptr) continue;
                emitterBindings gb; gb.self = gref; gb.val = gref; gb.trim = emitterTrim::ws;
                string subbed = expandEmitterBody(blk, gb);
                if(subbed.empty()) continue;
                languageService.globalInits.push_back({varDecl.name, subbed});
                varDecl.needsEarlyGlobalDecl = true;   // bglInit writes to it, so declare it first
                // The declared value has to be applied through the type's operator= AFTER
                // init has run, not baked into the I6 `global` directive. For a type whose
                // init allocates (stringObj), the slot holds that allocation — writing the
                // literal into the directive both overwrites it and emits a global whose
                // initializer bglInit has already assigned over.
                if(varDecl.declaredExpressionValue != nullptr){
                    string rhsText = varDecl.declaredExpressionValue->text();
                    string rhsType = varDecl.declaredExpressionValue->resolvedType;
                    typeMember* opm = findAssignOperator(cls, rhsType, [](functionDef* f){
                        return f->isEmitter && dynamic_cast<i6Block*>(f->body) != nullptr; }, /*allowInherited*/false);
                    if(opm != nullptr){
                        auto* opFn = dynamic_cast<functionDef*>(opm);
                        emitterBindings ab_; ab_.self = gref; ab_.val = gref;
                        ab_.trim = emitterTrim::ws; ab_.fn = opFn; ab_.args.push_back(rhsText);
                        string ab = expandEmitterBody(dynamic_cast<i6Block*>(opFn->body), ab_);
                        languageService.globalInits.push_back({varDecl.name, ab});
                        varDecl.declaredExpressionValue = nullptr;   // no longer baked into the directive
                        varDecl.needsEarlyGlobalDecl = true;         // declare before bglInit writes to it
                    }
                }
            }
        }
        // An initializer that is not a compile-time constant cannot go in the `Global` directive —
        // I6 answers "Expected constant but found <expression>", naming a line of generated code.
        // It becomes a startup assignment instead, which is what a class-typed global with an
        // `init` already does above. Declaration order is preserved, so an initializer may read a
        // global declared before it.
        if(varDecl.declaredExpressionValue != nullptr
           && initializerNeedsRuntime(varDecl.declaredExpressionValue->text())){
            string rhs = varDecl.declaredExpressionValue->text();
            string store = gref + " = " + rhs + ";";
            // A value class's global owns its instance: the value is copied into it through its operator =.
            if(classDef* vc = getDispatchClass(varDecl.type.name); vc != nullptr && isValueClass(vc) && !vc->isEmitterClass && !varDecl.isRefLocal)
                if(functionDef* op = findAssignOperator(vc, varDecl.declaredExpressionValue->resolvedType,
                                                        [](functionDef* f){ return !f->isEmitter; }, true)){
                    mangleOverloadSetForReceiver(vc->name, "=");
                    if(op->i6name.empty()) op->i6name = mangleOperatorName(op->name);
                    store = gref + "." + op->i6name + "(" + rhs + ");";
                }
            languageService.globalInits.push_back({varDecl.name, globalInitSetUp + store});
            globalInitSetUp.clear();
            markGlobalsReadEarly(rhs);
            varDecl.declaredExpressionValue = nullptr;
            varDecl.needsEarlyGlobalDecl = true;
        }
    }
}

bool bglParser::processVariableDeclaration(token dataType, token variableName, token symbol, abstractObject& contextObj, bool isExternal, bool isConst, string i6alias, bool isRef, bool isSuperposed, bool isAdditive){
    // Ban bare `array` as a type — every array variable must declare its element type.
    // `array<T>` / `array<char>` are handled earlier via processArrayDeclaration and never
    // reach here as a plain variable declaration.
    if(dataType.value == "array")
        parsingError("bare 'array' is not a valid type — declare the element type: array<T>");
    // A plain file-scope declaration of a reference class with no value is that class's object, as with `{ }`.
    if(symbol.is(token::endStatement) && !isRef && !isExternal && !isConst && !isSuperposed && !isAdditive
       && getCurrentCompileContext() == eCompileContext::global && plainSlotOwnsReferenceInstance(getDispatchClass((string)dataType)))
        return processObjectDeclaration(dataType, variableName, false, "", i6alias, false, false);
    // One whose class has a copy operator, given a value, is its own object, the value copied in at startup.
    if(symbol.is(token::assignment) && !isRef && !isExternal && !isConst && !isSuperposed && !isAdditive
       && getCurrentCompileContext() == eCompileContext::global && hasCopyOperator(getDispatchClass((string)dataType))){
        classDef* cls = getDispatchClass((string)dataType);
        expression* rhs = parseExpression(file.getToken(), {token::endStatement}, nullptr, nullptr);
        processObjectDeclaration(dataType, variableName, false, "", i6alias, false, false);
        functionDef* op = findAssignOperator(cls, rhs->resolvedType, [](functionDef* f){ return !f->isEmitter; }, true);
        if(op == nullptr)
            parsingError(format("'{0}' copies through its 'operator =', and none takes a '{1}'.",
                                typeDisplayName(cls->name), typeDisplayName(rhs->resolvedType)));
        mangleOverloadSetForReceiver(cls->name, "=");
        if(op->i6name.empty()) op->i6name = mangleOperatorName(op->name);
        languageService.globalInits.push_back({(string)variableName, objectI6Name((string)variableName) + "." + op->i6name + "(" + rhs->text() + ");"});
        return false;
    }
    variableDeclaration& varDecl = *new variableDeclaration();
    varDecl.src = file.currentLocation();
    varDecl.name=(string) variableName;
    varDecl.displayName = variableName.originalValue;
    varDecl.isLiteral = declLiteral;
    declLiteral = false;
    if(!dataType.docComment.empty())          varDecl.docComment = dataType.docComment;
    else if(!variableName.docComment.empty()) varDecl.docComment = variableName.docComment;
    varDecl.type=languageService.getType((string) dataType);
    if(varDecl.type.name.empty() && isUnionType(dataType.value)) varDecl.type.name = dataType.value;   // `string | Thing u`
    if(!i6alias.empty()) varDecl.i6name = i6alias;
    // A global named like an Inform 6 word (`held`) is renamed here, before any use is emitted.
    else if(!isExternal && getCurrentCompileContext() == eCompileContext::global
            && bglLanguageService::isI6StatementKeyword(varDecl.name)){
        varDecl.i6name = "_" + varDecl.dName();
        varDecl.i6nameAvoidsKeyword = true;
    }
    // A global named like a member's property: Inform 6 keeps both in one namespace, so the global
    // takes another I6 name (§3.11). A `property` declaration is that property.
    else if(!isExternal && getCurrentCompileContext() == eCompileContext::global && varDecl.type.name != "property"
            && memberPropertyNamed(varDecl.name)
            && [&]{ auto* g = languageService.findGlobalAs<variableDeclaration>(varDecl.name);
                    return g == nullptr || g->isPrePassStub || g == &varDecl; }())
        varDecl.i6name = "_g_" + varDecl.dName();
    varDecl.isExternal=isExternal;
    varDecl.isConst=isConst;
    varDecl.isRefLocal=isRef;
    varDecl.isSuperposed=isSuperposed;
    varDecl.isAdditive=isAdditive;
    // `additive` marks an I6 property slot as accumulating across the class hierarchy, so it is
    // meaningful only on a `property` declaration — never on a member or a value type.
    //
    // On an OWNED property it is a directive (`Property additive foo;`). On an `extern` one it is a
    // declaration of fact: I6 already declared the property additive, so nothing is emitted, and the
    // marker exists only so the compiler can warn when a single-word member is bound to one. Which
    // properties are additive is library-specific — `name` from the compiler itself, `before`/`after`
    // /`life` and kin from the standard library — so that knowledge lives in the bindings.
    // A value class's variable owns an instance of its own, so `const` has no binding to fix (§3.4).
    if(isConst && !isExternal)
        if(classDef* vc = languageService.findClass((string)dataType); vc != nullptr && isValueClass(vc))
            parsingError(format("'const' doesn't apply to '{0}': '{1}' is a struct, which is storage.",
                varDecl.dName(), vc->dName()));
    if(isExternal && symbol.is(token::assignment))
        parsingError(format("extern '{0}' can't take a value: Inform 6 declares it, and its value comes from there.",
                            varDecl.dName()));
    if(isConst && !isExternal && symbol.is(token::endStatement))
        parsingError(format("const '{0}' needs a value: `const {1} {0} = …;`.", varDecl.dName(),
                            dataType.originalValue.empty() ? (string)dataType : dataType.originalValue));
    if(isAdditive && varDecl.type.name != "property")
        parsingError("'additive' is only valid on a property declaration (e.g. `additive property foo;`)");
    // For func<...> types, getType returns the base "func" type. Set the full parameterized name.
    {
        string dtLower = (string)dataType;
        transform(dtLower.begin(), dtLower.end(), dtLower.begin(), ::tolower);
        if(dtLower.rfind("func<", 0) == 0) varDecl.type.name = dtLower;
    }

    functionDef* func = dynamic_cast<functionDef*>(&contextObj);
    statementBlock* body = func != nullptr ? dynamic_cast<statementBlock*>(func->body) : nullptr;

    // `ref` is only valid on function-local declarations: it overrides the default
    // value-semantics dispatch for a single variable, opting into reference (pointer-
    // alias) semantics. Globals don't need it — file-scope class-typed variables are
    // already real I6 objects with operator= dispatch. Combining with `const`, `extern`,
    // or other qualifiers doesn't make sense.
    if(isRef){
        if(isConst)
            parsingError("'ref' cannot be combined with 'const'");
        if(isExternal)
            parsingError("'ref' cannot be combined with 'extern'");
        string why = refNotApplicable(getDispatchClass(varDecl.type.name), typeDisplayName(varDecl.type.name));
        if(!why.empty()) parsingError("'ref': " + why);
    }

    checkLocalVariableShadowing(varDecl, func, body);

    bool isAuto = (dataType.value == "auto");
    if(isAuto && symbol.value != token::assignment && symbol.value != token::bindAssignment)
        parsingError("'auto' requires an initializer to infer the type");

    // A `ref` slot is only ever populated by binding — it names an instance owned elsewhere and
    // has no storage of its own to copy into. `=` on one reads as a copy and is rejected so the
    // two never look alike; `:=` is required everywhere, declaration included.
    if(isRef && symbol.value == token::assignment)
        parsingError(format("'ref {0} {1} = …': a 'ref' declaration binds a reference, so it takes "
                            "the reference binding operator. Write 'ref {0} {1} := …' instead.",
                            (string)dataType, (string)variableName));
    if(!isRef && symbol.value == token::bindAssignment)
        parsingError(format("'{0} {1} := …': the reference binding operator is only valid on a 'ref' "
                            "declaration. Write 'ref {0} {1} := …' to bind, or '=' to copy.",
                            (string)dataType, (string)variableName));

    if(func != nullptr) localBeingInitialized = varDecl.name;
    bool folded = parseVariableInitializer(varDecl, dataType, variableName, symbol, isAuto, isRef, func, body);
    localBeingInitialized.clear();
    if(folded) return false;   // folded into an object declaration — no variable emitted
    if(func != nullptr && currentFunc != nullptr && currentFunc->fileScopeReads.count(varDecl.name))
        parsingError(format("'{0}' in its own initializer names the global, but Inform 6 would read the new "
            "local there. Rename the local.", varDecl.dName()));
    if(isRef && symbol.value == token::bindAssignment){
        rejectMemberArrayBind(varDecl.declaredExpressionValue, func, body);
        // As a rebind is checked: the right side is an instance of the class, or of a class derived from it (§3.7).
        expression* rhs = varDecl.declaredExpressionValue;
        classDef* lc = getDispatchClass(varDecl.type.name);
        string rt = rhs != nullptr ? rhs->resolvedType : string();
        if(lc != nullptr && rhs != nullptr && rt != "nullliteral" && !isWordArrayType(varDecl.type.name)){
            classDef* rc = getDispatchClass(rt);
            if(rc == nullptr || rc->isPrimitive)
                parsingError(format("the reference binding operator ':=' binds a reference, so the right side must be an "
                                    "instance of a class; got '{0}'.", typeDisplayName(rt.empty() ? "unknown" : rt)));
            if(rc != lc && !rc->hasAncestor(lc) && !isTypeCompatible(rt, lc->name))
                parsingError(format("cannot bind '{0}' to '{1}': the reference binding operator ':=' requires the same "
                                    "class (or a subclass).", typeDisplayName(rc->name), typeDisplayName(lc->name)));
        }
    }

    // Flush ternary-lowering injections before the variable declaration that uses _bgl_temp
    declInjectionStart = body != nullptr ? (long)body->statements.size() : -1;
    for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
    // At file scope a `?.`/`??` set-up runs at startup, ahead of the global's assignment there.
    globalInitSetUp.clear();
    if(body == nullptr && func == nullptr)
        for(statement* inj : pendingInjections) if(auto* raw = dynamic_cast<i6RawNode*>(inj)) globalInitSetUp += raw->text + " ";
    pendingInjections.clear();

    synthesizeClassLocalBacking(varDecl, isExternal, isConst, isRef, func, body, !symbol.is(token::endStatement));
    if(func != nullptr && !isRef && slotHoldsArray(varDecl.type.name))
        copyMemberArrayValue(varDecl.declaredExpressionValue);
    // A `var` or union local declared from a call's or chain's result takes it, as an array local does.
    if(func != nullptr && body != nullptr && !isRef && slotHoldsArray(varDecl.type.name) && varDecl.declaredExpressionValue != nullptr
       && isWordArrayType(varDecl.declaredExpressionValue->resolvedType)
       && !isStaticArrayText(varDecl.declaredExpressionValue->text(), func, body)){
        string vt = varDecl.declaredExpressionValue->text();
        size_t z = vt.find_last_not_of(" \t");
        if(z != string::npos && vt[z] == ')'){
            string own = claimOwner(varDecl.name);
            auto* claim = new i6RawNode();
            claim->text = own + " = _bglHeapReown(" + own + ", " + varDecl.name + ");";
            claim->cooked = true;
            postInjections.push_back(claim);
            varDecl.claimsAfter = true;
        }
    }

    registerVariableDeclaration(varDecl, isConst, func, body);
    declInjectionStart = -1;

    recordGlobalVariableInit(varDecl, isConst, func, body);

    return false;
}

// Read an integer where the grammar needs a compile-time constant: an integer literal, a `#define`d
// symbol whose value is an integer, or an integer `#beguilerSettings` property. The last two already
// resolve to inline literals everywhere else a value is read (Appendix F), and a declared size is
// read no later — it becomes an I6 array declaration, which I6 sizes at compile time.
int bglParser::readCompileTimeInt(const string& what){
    token t = file.getToken();
    if(t.is(eTokenType::integer)) return stoi(t.value);
    if(t.value == "#beguilersettings"){
        file.getToken(token::period);
        token prop = file.getToken(eTokenType::identifier);
        string sv; int iv = 0; bool bv = false;
        switch(readBeguilerSetting(prop.value, sv, iv, bv)){
            case eSettingKind::integer: return iv;
            case eSettingKind::unknown:
                parsingError(format("#beguilerSettings.{0}: unknown or unsupported property", prop.value));
                return 0;
            default:
                parsingError(format("#beguilerSettings.{0} is not an integer property, so it cannot give {1}.",
                                    prop.value, what));
                return 0;
        }
    }
    if(t.is(eTokenType::identifier) || t.is(eTokenType::name)){
        // A `const int` initialized with an integer literal is as good as a #define.
        if(auto* vd = languageService.findGlobalAs<variableDeclaration>(t.value);
           vd != nullptr && vd->isConst && vd->declaredExpressionValue != nullptr){
            string v = vd->declaredExpressionValue->text();
            v.erase(remove(v.begin(), v.end(), ' '), v.end());
            while(v.size() > 2 && v.front() == '(' && v.back() == ')') v = v.substr(1, v.size() - 2);
            if(!v.empty() && v.find_first_not_of("-0123456789") == string::npos && v.find('-', 1) == string::npos){
                vd->foldedAtCompile = true;
                return stoi(v);
            }
        }
        auto it = definedSymbols.find(t.value);
        if(it != definedSymbols.end()){
            try { return stoi(it->second); } catch(...){ }
            parsingError(format("'{0}' is defined as '{1}', which is not an integer, so it cannot give {2}.",
                                t.originalValue.empty() ? t.value : t.originalValue, it->second, what));
            return 0;
        }
    }
    parsingError(format("{0} must be a compile-time integer — an integer literal, a #define'd integer, a "
                        "`const int` set to an integer literal, or an integer #beguilerSettings property. Got '{1}'.",
                        what, t.value));
    return 0;
}

bool bglParser::processRoutineDeclaration(token returnType, token name, abstractObject& contextObject, bool isExternal, bool isEmitter, bool isReplace, bool isDefault, bool isSuperposed){
    if(declLiteral){
        declLiteral = false;
        parsingError(format("'literal' qualifies the type of a parameter, variable or member, not the return type of '{0}'", (string)name));
    }
    functionDef& funcDef=*(new functionDef());
    funcDef.name=(string) name; funcDef.displayName=name.originalValue;
    funcDef.isDefault=isDefault;
    funcDef.isSuperposed=isSuperposed;
    // Use the name token's source location (preserved from when it was read) — currentLocation()
    // would return where we are NOW (after consuming the name), which may be on a later line.
    funcDef.src = name.src.line > 0 ? name.src : file.currentLocation();
    funcDef.returnType=languageService.getType((string) returnType);
    if(funcDef.returnType.name.empty()) funcDef.returnType.name = (string) returnType;  // func<…> / union return types
    funcDef.isExternal=isExternal;
    funcDef.isEmitter=isEmitter;
    if(!returnType.docComment.empty())   funcDef.docComment = returnType.docComment;
    else if(!name.docComment.empty())    funcDef.docComment = name.docComment;
    // Non-emitter operator routines (name starts with non-identifier char) need a mangled
    // i6name so the emitted I6 routine has a valid property identifier. Call sites use the
    // same mangler to look it up. Emitters are inlined and don't need the mangled name.
    if(!isEmitter && !funcDef.name.empty() && !isalpha((unsigned char)funcDef.name[0]) && funcDef.name[0] != '_')
        funcDef.i6name = mangleOperatorName(funcDef.name);
    // A global routine named like an I6 statement keyword: a call would read as that statement.
    if(!isEmitter && !isExternal && getCurrentCompileContext() == eCompileContext::global
       && funcDef.i6name.empty() && bglLanguageService::isI6StatementKeyword(funcDef.name)){
        funcDef.i6name = "_" + funcDef.dName();
        funcDef.i6nameAvoidsKeyword = true;
    }
    // Named like a member's property: one I6 namespace holds both, and the routine yields.
    if(!isEmitter && !isExternal && getCurrentCompileContext() == eCompileContext::global && funcDef.i6name.empty()
       && memberPropertyNamed(funcDef.name) && !namesFixedGlobal(funcDef.name))   // a replaced I6 routine keeps its name
        funcDef.i6name = "_g_" + funcDef.dName();

    // Register into globals EARLY (replacing any pre-scan stub) so that LSP error recovery
    // preserves partial parse state even if the body parse throws. The full body and params
    // are populated via references into this same object below.
    //
    // In .inf-mode, Pass 1 walks every #bgl island before Pass 2 starts, so all function
    // stubs cluster at the front of `globals` regardless of where their islands sit in the
    // .inf body. Replacing the stub in place would leave the funcDef there too — emitting
    // it ahead of compositeNodes for raw I6 (and `#includeI6` directives) that Pass 2
    // push_backs later. WadeWar3 hit this: `[initialise; ... location = ...]` was emitted
    // before `#include "...#parser.h"` (which declares `location`), and I6 rejected the
    // assignment. So in .inf-mode we erase the stub and push_back the real funcDef,
    // landing it at the current Pass-2 position. Other stubs remain visible for
    // forward-reference resolution during this function's body parse.
    //
    // Redefinition guard: a plain (non-extern) definition whose name is already declared `extern`
    // is only allowed when that extern was marked `default` — a library stub explicitly meant to be
    // overridden. `extern` otherwise asserts "this exists in I6", so silently redefining it is a
    // mistake (a name collision, or a missing `default`). A `default` extern is dropped here so the
    // real definition takes over cleanly. `replace` has its own path (below) and is exempt.
    if(currentClass == nullptr && currentObject == nullptr && !funcDef.isPrePassStub)
        if(classDef* same = languageService.findClass(funcDef.name); same != nullptr && !same->isAlias)
            parsingError(format("'{0}' is already the name of the class declared at {1}; a function can't share it.",
                                funcDef.dName(), languageService.declaredAt(same->name)));
    functionDef* overriddenDefault = nullptr;   // the `extern default` this definition takes over
    // A later `extern` of a routine, from another file, is the signature meant from here on; the overloads
    // one file declares together stand side by side.
    if(isExternal)
        languageService.globals.erase(remove_if(languageService.globals.begin(), languageService.globals.end(),
            [&](typeDef* g){
                auto* ex = dynamic_cast<functionDef*>(g);
                return ex && ex != &funcDef && ex->isExternal && !ex->isPrePassStub && ex->name == funcDef.name
                    && ex->src.file != funcDef.src.file;
            }), languageService.globals.end());
    if(!isExternal && !isReplace){
        // Every declaration of it goes: the program may repeat the binding's `extern default`.
        for(auto it = languageService.globals.begin(); it != languageService.globals.end(); ){
            auto* ex = dynamic_cast<functionDef*>(*it);
            if(ex && ex->isExternal && !ex->isPrePassStub && ex->name == funcDef.name){
                if(!ex->isDefault)
                    parsingError(format("'{0}' is already declared extern (it exists externally); a plain "
                        "definition would silently redefine it. If it's an overridable stub, mark the "
                        "declaration `extern default`; otherwise rename to avoid the collision.", funcDef.name));
                if(overriddenDefault == nullptr) overriddenDefault = ex;
                it = languageService.globals.erase(it);
            } else ++it;
        }
    }

    // In .bgl-mode, Pass 1 walks the source in order, so prescan stubs are already at
    // their source-order positions — claim in place preserves source order naturally.
    bool registeredEarly = false;
    if(languageService.isInfMode){
        for(auto it = languageService.globals.begin(); it != languageService.globals.end(); ++it){
            if(auto* stub = dynamic_cast<functionDef*>(*it)){
                if(stub->name == funcDef.name && stub->isPrePassStub){
                    languageService.globals.erase(it);
                    break;
                }
            }
        }
        languageService.globals.push_back(&funcDef);
        registeredEarly = true;
    } else {
        for(typeDef*& g : languageService.globals){
            if(auto* stub = dynamic_cast<functionDef*>(g)){
                // An extern takes only an extern's place: a definition's stub holds its source position.
                if(stub->name == funcDef.name && stub->isPrePassStub && stub->isExternal == funcDef.isExternal){
                    g = &funcDef;
                    registeredEarly = true;
                    break;
                }
            }
        }
        if(!registeredEarly){
            languageService.globals.push_back(&funcDef);
            registeredEarly = true;
        }
    }
    // A library global of the name: reported here, before a use of the function reads the global instead.
    if(!funcDef.isExternal && !funcDef.isEmitter && funcDef.i6name.empty())
        for(typeDef* g : languageService.globals)
            if(auto* vd = dynamic_cast<variableDeclaration*>(g); vd && vd->isExternal && vd->name == funcDef.name
               && vd->type.name != "property" && vd->i6name.empty())
                parsingError(format("function '{0}' has the same name as the extern {1} '{2}'; Inform 6 keeps these "
                    "in one namespace, ignoring case, so rename the function or give it an `asI6` name.",
                    funcDef.dName(), vd->type.name == "attribute" ? "attribute" : "global", vd->dName()));

    processParameterList(funcDef);

    // Overloading a global function is fine — the call site picks by argument types and each
    // overload emits under its own routine name. Declaring the SAME signature twice is not: there
    // is no call that could tell them apart, and I6 would see one routine name declared twice.
    // Return type is not part of the signature, so `int f(int)` and `string f(int)` collide here.
    if(!isExternal && !isEmitter && !isReplace && !funcDef.isPrePassStub){
        for(typeDef* g : languageService.globals){
            auto* ex = dynamic_cast<functionDef*>(g);
            if(!ex || ex == &funcDef || ex->name != funcDef.name) continue;
            if(ex->isPrePassStub || ex->isExternal) continue;
            if(ex->params.size() != funcDef.params.size()) continue;
            bool same = true;
            for(size_t i = 0; i < ex->params.size(); i++)
                if(ex->params[i]->type.name != funcDef.params[i]->type.name){ same = false; break; }
            if(ex->isEmitter){
                if(same)
                    parsingError(format("'{0}' has the parameter types of the emitter '{1}', so no call can reach it; "
                        "write `replace` to replace the emitter, or give the function another name.",
                        funcDef.dName(), ex->dName()));
                continue;
            }
            if(same && ex->replaceFoundNothing)
                parsingError(format("the replace of '{0}' above comes before '{0}' itself. A replace follows what "
                    "it replaces; move it below this declaration (or below the include that declares it).",
                    funcDef.dName()));
            if(same)
                parsingError(format("'{0}' is already defined with these parameter types. Overloads must "
                    "differ in their parameters — a return type alone cannot tell two calls apart.",
                    funcDef.displayName.empty() ? funcDef.name : funcDef.displayName));
        }
    }

    // The library calls the routine it declared, so an override keeps its parameters.
    if(overriddenDefault != nullptr){
        bool same = overriddenDefault->params.size() == funcDef.params.size();
        for(size_t i = 0; same && i < funcDef.params.size(); i++)
            same = overriddenDefault->params[i]->type.name == funcDef.params[i]->type.name;
        if(!same){
            string sig;
            for(size_t i = 0; i < overriddenDefault->params.size(); i++)
                sig += (i ? ", " : "") + typeDisplayName(overriddenDefault->params[i]->type.name);
            parsingError(format("'{0}' overrides the extern default '{0}({1})', whose parameters it must keep.",
                                funcDef.dName(), sig));
        }
    }
    // Synthesize the instance and copy-in for each param whose class copies, before the body is parsed.
    synthesizeParamBackings(funcDef);

    // ── replace chaining (non-emitter): rename existing before body parse ──
    // For non-emitter replace, we rename the existing function to a mangled name
    // BEFORE parsing the body so that replaced() calls can resolve during parsing.
    // Emitter replace keeps the simpler body-swap path (no chaining).
    if(isReplace && !isEmitter){
        // A name may now carry several overloads, so a replacement has to say WHICH one it
        // replaces: its own parameter types. An exact signature match wins; with no exact match
        // the name still resolves when it names exactly one function, which keeps the ordinary
        // single-definition case working unchanged.
        functionDef* existing = nullptr;
        functionDef* byName = nullptr;
        int byNameCount = 0;
        for(typeDef* g : languageService.globals){
            if(auto* fd = dynamic_cast<functionDef*>(g)){
                if(fd == &funcDef) continue;   // this declaration is already registered — it can't replace itself
                if(fd->name != funcDef.name) continue;
                if(fd->isPrePassStub) continue; // pre-scan stub doesn't count as a real definition
                if(byName == nullptr) byName = fd;
                byNameCount++;
                if(fd->params.size() != funcDef.params.size()) continue;
                bool same = true;
                for(size_t i = 0; i < fd->params.size(); i++)
                    if(fd->params[i]->type.name != funcDef.params[i]->type.name){ same = false; break; }
                if(same){ existing = fd; break; }
            }
        }
        if(existing == nullptr && byNameCount > 1)
            parsingError(format("replace: '{0}' has {1} overloads and none takes these parameter types. "
                "A replacement names the overload it replaces by its parameters.",
                funcDef.displayName.empty() ? funcDef.name : funcDef.displayName, byNameCount));
        if(existing == nullptr) existing = byName;
        if(existing){
            // Determine N for mangled name: count existing _bgl_replaced_NAME_* in globals
            int n = 0;
            string prefix = "_bgl_replaced_" + funcDef.name + "_";
            for(typeDef* g : languageService.globals)
                if(auto* fd = dynamic_cast<functionDef*>(g))
                    if(fd->name.rfind(prefix, 0) == 0) n++;
            string mangledName = prefix + to_string(n);
            existing->name = mangledName;
            existing->i6name = mangledName;   // emitted under this; messages keep the author's name
            funcDef.replacedTarget = mangledName;
            funcDef.replacedFunc = existing;
        } else {
            funcDef.replaceFoundNothing = true;
            parsingWarning(format("replace: no existing global function '{0}' found; treating as new definition", funcDef.name));
        }
    }

    if(isEmitter && file.peekToken().is(token::endStatement)){
        // Semicolon-terminated emitter: pass-through (value unchanged)
        if(funcDef.name != "operator()")
            parsingError(format("emitter '{0}' has no body: an emitter's body is the code it puts at each use. "
                                "Only a conversion `operator()` may end with ';'.", funcDef.dName()));
        file.getToken(); // consume ';'
        i6Block& rawblock=*(new i6Block());
        rawblock.i6Body=" $self";
        funcDef.body=&rawblock;
    } else if(isEmitter){
        file.getToken(token::braceOpen);
        i6Block& rawblock=*(new i6Block());
        rawblock.i6Body = file.getRawTextThroughClosingBrace(/*isI6Content=*/true);
        // A later `replace emitter` of this signature is the body every use sees, wherever it sits.
        if(!isReplace && currentClass == nullptr && currentObject == nullptr)
            if(auto it = forwardReplaceBodies.find(emitterSignatureKey(funcDef)); it != forwardReplaceBodies.end())
                rawblock.i6Body = it->second;
        checkEmitterTokens(funcDef, rawblock.i6Body, currentClass == nullptr && currentObject == nullptr);
        funcDef.body=&rawblock;
    } else if(isExternal){
        // extern non-emitter routine: declared bodyless with ';'. The implementation lives in I6
        // (a library routine) or is supplied by a Beguile `default` override — so the extern itself
        // emits nothing. A body would misleadingly look like an (empty) definition; reject it.
        if(file.peekToken().is(token::braceOpen))
            parsingError(format("extern function '{0}' cannot have a body — declare it with ';'. "
                                "Its implementation comes from I6, or from a `default` override.", funcDef.name));
        // `extern int I6Twice(int x) asBgl twice;`: Beguile calls it `twice`, Inform 6 knows it as I6Twice.
        if(file.peekToken().is("asbgl")){
            file.getToken();
            token bgl = file.getToken({eTokenType::identifier, eTokenType::dataType});
            if(funcDef.i6name.empty()) funcDef.i6name = funcDef.dName();
            funcDef.name = bgl.value;
            funcDef.displayName = bgl.originalValue.empty() ? bgl.value : bgl.originalValue;
        }
        file.getToken(token::endStatement);
        funcDef.body = new statementBlock();
    } else {
        file.getToken(token::braceOpen);
        funcDef.body=new statementBlock();
        functionDef* savedFunc = currentFunc;
        currentFunc = &funcDef;
        openCompileContext(eCompileContext::codeBlock, dynamic_cast<statementBlock*>(funcDef.body));
        while(processNextStatement(funcDef)==false){
        }
        closeCompileContext(eCompileContext::codeBlock);
        currentFunc = savedFunc;
        if(funcDef.returnType.name != "void" && !allPathsReturn(dynamic_cast<statementBlock*>(funcDef.body)))
            parsingError(missingReturnMessage(funcDef));
        saveBackingsAcrossRecursion(funcDef);
    }

    // ── replace: emitter body-swap (existing behavior) ──
    if(isReplace && isEmitter){
        for(typeDef* g : languageService.globals){
            if(auto* existing = dynamic_cast<functionDef*>(g)){
                if(existing->name != funcDef.name) continue;
                if(existing->params.size() != funcDef.params.size()) continue;
                bool paramsMatch = true;
                for(size_t i = 0; i < funcDef.params.size() && paramsMatch; i++)
                    if(funcDef.params[i]->type.name != existing->params[i]->type.name)
                        paramsMatch = false;
                if(!paramsMatch) continue;
                existing->body = funcDef.body;
                existing->isEmitter = funcDef.isEmitter;
                existing->returnType = funcDef.returnType;
                return false;
            }
        }
        parsingWarning(format("replace: no existing emitter '{0}' found with matching signature; treating as new definition", funcDef.name));
    }

    // ── replace over an I6 routine: hand the name over via I6's own Replace ──
    // A Beguile target is simply not emitted, so `replace` needs no help. An `extern` target names
    // a routine I6 supplies, which we cannot drop — so tell I6 to yield the name. The two-operand
    // form keeps the original callable under the mangled name that `replaced()` resolves to; the
    // one-operand form is enough when the body never calls it. Routed through emitFirstBlocks so
    // the directive precedes the library that defines the routine.
    if(isReplace && !isEmitter && funcDef.replacedFunc && funcDef.replacedFunc->isExternal){
        string line = "Replace " + funcDef.name;
        if(funcDef.replacedWasCalled) line += " " + funcDef.replacedTarget;
        languageService.emitFirstBlocks.push_back(line + ";");
    }

    // ── replace chaining: dead code elimination ──
    // If replaced() was never called in the body, the predecessor (and its entire
    // backward chain) will never execute — mark them dead so the emitter skips them.
    if(isReplace && !isEmitter && funcDef.replacedFunc && !funcDef.replacedWasCalled){
        functionDef* f = funcDef.replacedFunc;
        while(f && f != &funcDef && !f->isReplacedDead){   // guard against a self- or cyclic chain
            f->isReplacedDead = true;
            f = f->replacedFunc;
        }
    }

    // funcDef was already registered in globals above (before body parse) so LSP error
    // recovery preserves the partial definition. Nothing more to do here.
    return false;
}


// ===============================================================================
// Grammar declarations
// ===============================================================================
bool bglParser::processGrammarDeclaration(token nameOverride){
    if(getCurrentCompileContext() != eCompileContext::global)
        parsingError("'grammar' declarations are only allowed in global context");

    token name = nameOverride.tokenType != eTokenType::unknown ? nameOverride : file.getToken(eTokenType::identifier);
    string grammarName = name.originalValue.empty() ? name.value : name.originalValue;

    file.getToken(token::braceOpen);
    if(file.peekToken().is(token::braceOpen))
        parsingError(format("'grammar {0} {{ ... }}' bodies hold grammarRule members, not bare grammar lines. To add lines to an existing verb use 'extend {0} {{ grammar += {{ ... }}; }}'; to author a cross-cutting grammar object use 'grammar {0} {{ grammarRule r = {{Verb, {{ ... }}}}; }}'.", grammarName));
    return processGrammarObjectDeclaration(grammarName);
}

// Parse a grammar object body with grammarRule members.
// Assumes outer '{' already consumed. grammarName is the object name.
// Members are: grammarRule name = {VerbRef, {.word, TOKEN, ...}};
// or array<grammarRule> name = {{VerbRef, {.word, ...}}, ...};
// Type can be inferred from the grammarTable class members.

bool bglParser::processGrammarObjectDeclaration(const string& grammarName){
    // Resolve grammarTable class for type inference
    classDef* gtClass = languageService.findClass("grammarrulelist");

    // Create a grammarRuleListDecl to hold all the rules
    grammarRuleListDecl& gtd = *(new grammarRuleListDecl());
    gtd.name = grammarName;
    gtd.type = languageService.getType("grammarrulelist");

    // Helper: resolve a verb reference token, return original-case verb name or error
    auto resolveVerbRef = [this](const token& verbTok) -> string {
        string lower = verbTok.value;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        for(verbObjectDef* v : languageService.verbs)
            if(v->name == lower)
                return verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
        string display = verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
        parsingError(format("'{0}' in grammarRule initializer is not a declared verb", display));
        return "";
    };

    // Helper: parse a single grammarRule initializer {VerbRef, {.word, TOKEN, ...}, priority?}
    // Assumes outer '{' already consumed. Optional 3rd positional element is the line's sort
    // priority (default 10 from BLR `class verb`).
    auto parseGrammarRuleInit = [&](grammarRuleDecl& rd){
        // First element: verb reference
        token verbTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
        rd.targetVerb = resolveVerbRef(verbTok);
        file.getToken(token::comma);
        // Second element: grammar line pattern {.word, TOKEN, ...}
        file.getToken(token::braceOpen);
        rd.line = parseGrammarLineContent();
        // Optional 3rd element: priority
        rd.line.priority   = parseOptionalGrammarRulePriority(languageService.getClassFieldIntDefault("verb", "priority", 10));
        rd.line.isOwnLine  = false;
        // Closing } of the outer initializer
        file.getToken(token::braceClose);
    };

    token tok = file.getToken();
    while(tok.isNot(token::braceClose)){
        if(tok.is(eTokenType::eof)) parsingError("Unexpected end of file inside settings/object body — missing closing '}'");
        // Determine type — explicit or inferred
        string memberType;
        bool isArray = false;
        if(tok.is("array")){
            // explicit array<grammarRule>
            file.getToken("<");
            string elemType = file.getToken({eTokenType::dataType, eTokenType::identifier}).value;
            file.getToken(">");
            memberType = "grammarrule";
            isArray = true;
            tok = file.getToken(eTokenType::identifier); // member name
        } else if(tok.isDataType() && tok.value == "grammarrule"){
            // explicit grammarRule
            memberType = "grammarrule";
            tok = file.getToken(eTokenType::identifier); // member name
        } else if(tok.is(eTokenType::identifier)){
            // inferred type from grammarTable class
            // tok is the member name; infer type from class
            if(gtClass){
                // Check if assignment follows — peek for = or {
                token afterName = file.peekToken();
                if(afterName.is(token::assignment)){
                    // Look at initializer shape to determine type
                    // For now, we'll determine after reading the initializer
                    memberType = "grammarrule"; // default inference
                }
            }
            if(memberType.empty())
                parsingError(format("Cannot infer type for '{0}' in grammar object body", tok.value));
        } else {
            parsingError(format("Unexpected '{0}' in grammar object body", tok.value));
        }

        string memberName = tok.value;
        file.getToken(token::assignment);

        token first = file.getToken(token::braceOpen);

        // Validate shape matches declared type
        token peek2 = file.peekToken();
        if(isArray){
            // array<grammarRule>: must be {{VerbRef, {pattern}}, ...}
            if(!peek2.is(token::braceOpen))
                parsingError(format("array<grammarRule> '{0}' requires nested initializer list of rule pairs", memberName));
            token inner = file.getToken();
            while(inner.is(token::braceOpen)){
                grammarRuleDecl& rd = *(new grammarRuleDecl());
                rd.name = memberName;
                rd.type = languageService.getType("grammarrule");
                parseGrammarRuleInit(rd);
                gtd.rules.push_back(&rd);
                grammarLine gl = rd.line;
                gl.targetVerb = rd.targetVerb;
                gtd.grammarLines.push_back(gl);
                token sep = file.getToken({token::comma, token::braceClose});
                if(sep.is(token::braceClose)) break;
                inner = file.getToken();
            }
        } else {
            // grammarRule: must be {VerbRef, {pattern}} — not nested
            if(peek2.is(token::braceOpen))
                parsingError(format("grammarRule '{0}' takes a single rule initializer; use array<grammarRule> for multiple rules", memberName));
            grammarRuleDecl& rd = *(new grammarRuleDecl());
            rd.name = memberName;
            rd.type = languageService.getType("grammarrule");
            parseGrammarRuleInit(rd);
            gtd.rules.push_back(&rd);
            grammarLine gl = rd.line;
            gl.targetVerb = rd.targetVerb;
            gtd.grammarLines.push_back(gl);
        }

        // Consume optional semicolon after member declaration
        if(file.peekToken().is(token::endStatement)) file.getToken();

        tok = file.getToken();
    }

    languageService.globals.push_back(&gtd);
    return false;
}

// After parseGrammarLineContent has consumed the inner `}` of a `{Verb, {pattern}}` literal,
// peek for an optional third positional element — `, priority` — and return it. Returns
// `defaultPriority` (caller-supplied; typically BLR's `class verb` default of 10) when no
// third element is present.

int bglParser::parseOptionalGrammarRulePriority(int defaultPriority){
    if(!file.peekToken().is(token::comma)) return defaultPriority;
    file.getToken();  // consume the comma
    token prioTok = file.getToken();
    try { return stoi(prioTok.value); }
    catch(...) {
        parsingError(format("grammarRule priority: expected integer literal, got '{0}'",
            prioTok.originalValue.empty() ? prioTok.value : prioTok.originalValue));
    }
    return defaultPriority;
}

// Parse a single grammar line's content: trigger word + pattern tokens.
// Assumes the opening '{' has already been consumed.
// Returns a grammarLine with verbWord and I6-ready patternTokens.

grammarLine bglParser::parseGrammarLineContent(){
    auto escDictWord = [](const string& w) -> string {
        string e; for(char ch : w) e += (ch == '\'') ? '^' : ch; return e;
    };
    auto makeI6Word = [&escDictWord](const token& t) -> string {
        string e = escDictWord(t.value);
        if(t.isPlural) return "'" + e + "//p'";   // I6 plural dictionary flag is '//p' (double slash)
        if(e.size() == 1) return "'" + e + "//'";
        return "'" + e + "'";
    };

    grammarLine line;

    // First token: the verb trigger word — a dict word, with optional |-alternation for
    // multi-trigger lines (e.g. `.type|.enter|.put`). Extras land in additionalVerbWords;
    // they emit as one combined `verb 'w1' 'w2' 'w3' ...` directive when all are first-
    // occurrence, or fan out to per-trigger directives otherwise (see emitGrammarLines).
    // The trigger group may be parenthesized, as any other group: `{ (.chip | .nick), noun }`.
    bool triggerParens = file.peekToken().is("(");
    if(triggerParens) file.getToken();
    token trigger = file.getToken(eTokenType::dictionaryWord);
    line.verbWord = trigger.value;   // raw word, e.g. "put"
    while(file.peekToken().is("|")) {
        file.getToken();  // consume '|'
        token alt = file.getToken(eTokenType::dictionaryWord);
        line.additionalVerbWords.push_back(alt.value);
    }
    if(triggerParens) file.getToken(")");

    // Remaining pattern tokens, comma-separated until }
    token tok = file.getToken({token::comma, token::braceClose});
    while(tok.is(token::comma)){
        // Optional opening paren — syntactic sugar for a dict-word alternatives group
        bool hasParens = file.peekToken().is("(");
        if(hasParens) file.getToken();  // consume '('

        token pt = file.getToken({eTokenType::dictionaryWord, eTokenType::identifier, eTokenType::dataType});

        if(pt.is(eTokenType::dictionaryWord)){
            // Dict word — may be chained with | alternatives: .word1 | .word2 | .word3
            string i6tok = makeI6Word(pt);
            while(file.peekToken().is("|")){
                file.getToken();  // consume '|'
                token alt = file.getToken(eTokenType::dictionaryWord);
                i6tok += "/" + makeI6Word(alt);
            }
            if(hasParens) file.getToken(")");  // consume ')'
            line.patternTokens.push_back(i6tok);
        } else {
            if(hasParens)
                parsingError("Parenthesized grammar group must contain dictionary word alternatives (.word | .word2 | ...)");

            // identifier or dataType in grammar pattern
            string tokenStr = pt.value;
            string display = pt.originalValue.empty() ? pt.value : pt.originalValue;

            // Resolve declared type. Grammar-line context: prefer enum-value matches
            // (e.g. `extern enum grammarToken { noun, held, ... }`) over global decls,
            // because some grammar-token names (`noun`) shadow extern globals of the same
            // name (the I6 runtime `noun` object). In a grammar line position the token
            // meaning always wins; outside grammar lines the global wins via normal
            // ambiguity resolution.
            // grammarToken is searched first, so another enum's same-named member (eParserError.number)
            // can't take the place of a grammar token (NUMBER).
            string resolvedType;
            for(int pass = 0; pass < 2 && resolvedType.empty(); pass++)
            for(typeDef* t : languageService.objectTypes){
                auto* ed = dynamic_cast<enumDef*>(t);
                if(!ed || (pass == 0) != (ed->name == "grammartoken")) continue;
                bool found = false;
                for(enumValueDef* v : ed->namedValues)
                    if(v->name == tokenStr){
                        resolvedType = ed->name;
                        tokenStr = ed->isExternal ? v->name : to_string(v->value);
                        found = true; break;
                    }
                if(found) break;
            }
            if(resolvedType.empty()){
                for(typeDef* g : languageService.globals){
                    if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                        if(vd->name == tokenStr){
                            resolvedType = vd->type.name;
                            if(!vd->i6name.empty()) tokenStr = vd->i6name;
                            break;
                        }
                    if(auto* fd = dynamic_cast<functionDef*>(g))
                        if(fd->name == tokenStr){ resolvedType = "function"; break; }
                }
            }

            // `reverse` and `withI6Synonyms` pseudo-tokens — trailing action-target modifiers, not
            // pattern tokens (neither is pushed into the I6 pattern). Tail grammar (both optional, in
            // this order): pattern… [, reverse] [, withI6Synonyms].
            //   `reverse`        — swaps noun/second when the action receives its args (`-> Act reverse`).
            //   `withI6Synonyms` — opt OUT of the word-precise default: a line whose trigger word is a
            //                      grouped I6 library word is otherwise SPLIT off via `Extend only 'w'`
            //                      (leaving synonyms untouched); this modifier instead spreads it across
            //                      the whole verb group via plain `Extend 'w'`. Meaningless on native/
            //                      solo words (no synonym group) — the emitter ignores it there.
            // `withI6Synonyms`, when present, must be the very last element. A bare `.reverse`/`.withI6Synonyms`
            // dict word (with the dot) is unaffected — this only matches the bare identifiers.
            if(pt.value == "reverse" || pt.value == "withi6synonyms"){
                if(pt.value == "reverse") line.isReverse = true;
                else                      line.withI6Synonyms = true;
                token sep = file.getToken({token::comma, token::braceClose});
                if(sep.is(token::braceClose)) break;                     // end of literal
                if(line.withI6Synonyms)                                  // something follows withI6Synonyms
                    parsingError("'withI6Synonyms' must be the last element in a grammar line literal — no tokens may follow it");
                // A comma after `reverse`: the only element allowed to follow it is `withI6Synonyms`.
                token nxt = file.getToken({eTokenType::identifier, eTokenType::dataType});
                if(nxt.value != "withi6synonyms")
                    parsingError("only the 'withI6Synonyms' modifier may follow 'reverse' in a grammar line literal");
                line.withI6Synonyms = true;
                file.getToken(token::braceClose);                        // consume `}`
                break;
            }

            if(file.peekToken().is("(")){
                // NOUN(Routine) / SCOPE(Routine) — parameterized grammar token
                if(resolvedType != "grammartoken")
                    parsingError(format("'{0}' in grammar pattern: only grammarToken types can be parameterized with (Routine)", display));
                file.getToken();  // consume '('
                token routine = file.getToken({eTokenType::identifier, eTokenType::dataType});
                file.getToken(")");  // consume ')'
                functionDef* rfd = nullptr;
                if(auto* fd = languageService.findGlobalAs<functionDef>(routine.value)) rfd = fd;
                if(!rfd)
                    parsingError(format("'{0}' in '{1}({0})' does not name a declared global function", routine.value, display));
                if(rfd->returnType.name != "bool" && rfd->returnType.name != "ebool")
                    parsingError(format("'{0}' in '{1}({0})' must return bool (returns '{2}')", routine.value, display, rfd->returnType.name));
                if(!rfd->params.empty())
                    parsingError(format("'{0}' in '{1}({0})' must take no parameters: the parser calls it with none{2}",
                        routine.originalValue.empty() ? routine.value : routine.originalValue, display, pt.value == "noun" ? " and leaves the candidate object in 'noun'" : ""));
                tokenStr = pt.value + "=" + routine.value;  // I6 form: noun=Routine
            } else {
                // bare token: must be a declared grammarToken, attribute, or global function
                if(resolvedType != "grammartoken" && resolvedType != "attribute" && resolvedType != "function")
                    parsingError(format("'{0}' in grammar pattern is not a declared grammarToken, attribute, or global function", display));
                if(resolvedType == "function")
                    if(auto* fd = languageService.findGlobalAs<functionDef>(pt.value); fd && !fd->params.empty())
                        parsingError(format("general parsing routine '{0}' must take no parameters: the parser calls it with none, "
                                            "and it reads the input with NextWord()", display));
            }
            line.patternTokens.push_back(tokenStr);
        }
        tok = file.getToken({token::comma, token::braceClose});
    }
    // tok is now } (closing brace of this line)
    return line;
}

// Parse a grammar line list: { {.put, held, .on, noun}, {.hang, held, .on, noun} }
// First token of each line is the verb trigger word (must be a dictionaryWord).
// Remaining tokens are pattern tokens: dict words become I6 'word', identifiers stay as-is.
//
// Single-line shorthand ("pretty lie"): when the first content token after the outer `{`
// is a dictionaryWord, the entire outer braces are treated as the one and only grammar
// line — `grammar = {.give, held, .to, creature}` desugars to
// `grammar = { {.give, held, .to, creature} }`. Mirrors the existing extern-verb body
// shorthand (parseObjectBody) so the same shape works at any grammar-list site.

vector<grammarLine> bglParser::parseGrammarLines(){
    vector<grammarLine> result;

    file.getToken(token::braceOpen);   // outer {

    // Single-line shorthand: outer `{` directly followed by a dictionaryWord means the
    // outer braces ARE the line literal. parseGrammarLineContent reads from the current
    // position (just past `{`) and consumes through the matching `}` — which is the
    // outer close here.
    // A parenthesized first group (`{ ( .a | .b ), noun }`) starts a line too.
    if(file.peekToken().is(eTokenType::dictionaryWord)
       || (file.peekToken().is("(") && file.peekToken(2).is(eTokenType::dictionaryWord))){
        result.push_back(parseGrammarLineContent());
    } else {
        token tok = file.getToken();
        while(tok.isNot(token::braceClose)){
            if(tok.is(eTokenType::eof)) parsingError("Unexpected end of file inside grammar — missing closing '}'");
            tok.assert(token::braceOpen, "Expected '{' to start a grammar line");
            result.push_back(parseGrammarLineContent());

            // After }, expect , (more lines) or } (end of list)
            tok = file.getToken({token::comma, token::braceClose});
            if(tok.is(token::comma))
                tok = file.getToken();   // either { for next line, or } for end
        }
    }

    // Expand alternation trigger words into per-word grammar lines: `{.a|.b, pat}` becomes two
    // lines `{.a, pat}` and `{.b, pat}`. Every grammar line then carries exactly ONE trigger word,
    // so each lowers to its own I6 `Verb` directive (each dictionary word owns one verb — simpler
    // I6 management), and grammar `-=` matches/removes an individual (word + pattern) rather than a
    // whole trigger set (dropping `.a` from a shared line keeps `.b`).
    vector<grammarLine> expanded;
    for(const grammarLine& gl : result){
        expanded.push_back(gl);
        expanded.back().additionalVerbWords.clear();
        for(const string& w : gl.additionalVerbWords){
            grammarLine copy = gl;
            copy.verbWord = w;
            copy.additionalVerbWords.clear();
            expanded.push_back(copy);
        }
    }
    return expanded;
}

// A routine that calls itself shares each parameter's and local's instance with every activation;
// the emitter puts the caller's field values back as it leaves, so a returned value class is copied
// out first, into an instance of the routine's own that nothing restores.
void bglParser::saveBackingsAcrossRecursion(functionDef& fd){
    if(!fd.callsItself || fd.ownBackings.empty()) return;
    classDef* rc = languageService.findClass(fd.returnType.name);
    if(rc == nullptr || rc->isEmitterClass || !isValueClass(rc) || !classHasStoredFields(rc) || isReferenceBacked(rc)) return;
    functionDef* op = findAssignOperator(rc, fd.returnType.name, [](functionDef*){ return true; }, /*allowInherited*/true);
    if(op == nullptr || op->isEmitter)
        parsingError(format("'{0}' calls itself and returns a '{1}', which has no copy operator to return it with; "
            "declare one, or take a `ref` parameter to fill instead.", fd.dName(), typeDisplayName(fd.returnType.name)));
    if(op->i6name.empty()) op->i6name = mangleOperatorName(op->name);
    string context = currentClass != nullptr ? currentClass->name : currentObject != nullptr ? currentObject->name : string();
    string name = "_bglRet_" + (context.empty() ? "" : context + "_") + fd.name;
    int n = 1;
    while(!paramBackingNames.insert(name + "_" + to_string(n)).second) n++;
    name += "_" + to_string(n);
    auto* backing = new variableDeclaration();
    backing->name = name;
    backing->displayName = name;
    backing->type = languageService.getType(fd.returnType.name);
    backing->src = fd.src;
    backing->isInstanceBacking = true;
    languageService.registerInstance(*backing);
    fd.returnCopy = {name, op->i6name};
}

// Every `$word` in an emitter body is one of its parameters or a substitution token (§7.3); anything
// else would reach Inform 6 as text. A `$` before hex digits is I6's hex literal.
void bglParser::checkEmitterTokens(functionDef& fn, const string& body, bool isGlobal){
    static const set<string> tokens = {"action", "class", "host", "i6expr", "i6name", "opref", "oprefreq",
                                       "prop", "self", "selfsub", "target", "val"};
    for(size_t i = 0; i < body.size(); i++){
        char c = body[i];
        if(c == '"'){ size_t e = body.find('"', i + 1); if(e == string::npos) break; i = e; continue; }
        if(c == '!' && (i == 0 || body[i-1] != '~')){ size_t e = body.find('\n', i); if(e == string::npos) break; i = e; continue; }
        if(c != '$' || i + 1 >= body.size() || !(isalpha((unsigned char)body[i+1]) || body[i+1] == '_')) continue;
        size_t e = i + 1;
        while(e < body.size() && (isalnum((unsigned char)body[e]) || body[e] == '_')) e++;
        string word = body.substr(i + 1, e - i - 1), low = word;
        transform(low.begin(), low.end(), low.begin(), ::tolower);
        i = e - 1;
        bool isParam = false;
        for(paramDef* p : fn.params) if(p->name == low) isParam = true;
        if(isParam) continue;
        if(low == "self" && isGlobal)
            parsingError(format("emitter '{0}' uses $self, but a global emitter has no receiver; use a parameter.", fn.dName()));
        if(tokens.count(low)) continue;
        if(all_of(low.begin(), low.end(), [](char h){ return isxdigit((unsigned char)h); })) continue;
        parsingError(format("'${0}' in emitter '{1}' is neither one of its parameters nor a substitution token.",
                            word, fn.dName()));
    }
}

// A union doesn't fit one of its member types (§2.8): `stringObj s = u;` needs the union narrowed
// first, unless the class has an `operator =` that takes the union (or any value) itself.
void bglParser::rejectUnionInto(expression* val, classDef* target){
    if(val == nullptr || target == nullptr || !target->unionMembers.empty() || target->isPrimitive) return;
    string vt = val->resolvedType;
    classDef* vc = languageService.findClass(vt);
    bool isUnion = isUnionType(vt) || (vc != nullptr && !vc->unionMembers.empty());
    if(!isUnion) return;
    // A union whose every member already is the target (`x ? roomA : roomB` into an object) fits as is.
    vector<string> members = isUnionType(vt) ? splitUnionType(vt) : vc->unionMembers;
    bool allDirect = true;
    for(const string& m : members){
        classDef* mc = languageService.classOf(m);
        if(objectDef* od = languageService.findObjectType(m); od != nullptr && od->objectClass) mc = od->objectClass;
        if(mc == nullptr || !(mc == target || mc->hasAncestor(target))) allDirect = false;
    }
    if(allDirect) return;
    bool takesIt = false;
    target->forEachMember([&](typeMember* m){
        auto* f = dynamic_cast<functionDef*>(m);
        if(f && f->name == "=" && !f->isStatic && f->params.size() == 1
           && (f->params[0]->type.name == vt || f->params[0]->type.name == "var")) takesIt = true;
    });
    if(!takesIt){
        string shown;
        if(isUnionType(vt)) for(const string& m : splitUnionType(vt)) shown += (shown.empty() ? "" : " | ") + typeDisplayName(m);
        else shown = typeDisplayName(vt);
        parsingError(format("A '{0}' value doesn't fit a '{1}' directly: narrow the union first with a cast, "
                            "such as `({1})value`.", shown, target->dName()));
    }
}

// What a local of a global's name hides, for the shadowing warning: "the extern function 'Cap'".
string bglParser::shadowedGlobal(typeDef* g){
    bool ext = dynamic_cast<functionDef*>(g) ? dynamic_cast<functionDef*>(g)->isExternal
             : dynamic_cast<objectDef*>(g) ? dynamic_cast<objectDef*>(g)->isExternal
             : dynamic_cast<variableDeclaration*>(g) ? dynamic_cast<variableDeclaration*>(g)->isExternal : false;
    string from = ext ? "extern " : "";
    string kind = dynamic_cast<functionDef*>(g) ? "function" : dynamic_cast<objectDef*>(g) ? "object" : "global variable";
    return "the " + from + kind + " '" + g->dName() + "'";
}

// A local array that owns the array it takes from a call or a step's result: the hidden local holding it.
void bglParser::giveOwner(arrayDeclaration& ad){
    if(!ad.ownerVar.empty()) return;
    ad.ownerVar = "_bglown_" + ad.name;
}
void bglParser::declareOwner(functionDef* func, arrayDeclaration& ad){
    auto* top = func != nullptr ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    if(top == nullptr) return;
    for(statement* s : top->statements)
        if(auto* vd = dynamic_cast<variableDeclaration*>(s); vd && vd->name == ad.ownerVar) return;
    auto* own = new variableDeclaration();
    own->name = own->displayName = ad.ownerVar;
    own->type = languageService.getType("var");
    own->isSynthetic = true;
    top->statements.insert(top->statements.begin(), own);
}

// A hidden local of the routine that owns what `slot` takes from a call or a chain, freed when it takes another and at
// the routine's exit (unless kept elsewhere). Returns the owner's name.
string bglParser::claimOwner(const string& slot){
    functionDef* fn = currentFunc;
    string own = "_bglown_" + slot;
    if(fn == nullptr) return own;
    for(auto& c : fn->cleanups) if(c.first == own) return own;
    if(auto* top = dynamic_cast<statementBlock*>(fn->body)){
        auto* vd = new variableDeclaration();
        vd->name = vd->displayName = own;
        vd->type = languageService.getType("var");
        vd->isSynthetic = true;
        top->statements.insert(top->statements.begin(), vd);
    }
    fn->cleanups.push_back({own, "_bglHeapDropOwned(" + own + ");"});
    return own;
}

// Whether `slot` already has a hidden owner in the routine (claimOwner).
bool bglParser::hasOwner(const string& slot){
    if(currentFunc == nullptr) return false;
    for(auto& c : currentFunc->cleanups) if(c.first == "_bglown_" + slot) return true;
    return false;
}

// `alias print = bgl.output.print;` at file scope: the object's emitters of that name are called as
// globals. Only emitters: they emit no routine of their own, so the same definition serves both.
bool bglParser::processFunctionAlias(token nameTok){
    file.getToken(token::assignment);
    vector<token> path{file.getToken({eTokenType::identifier, eTokenType::dataType})};
    while(file.peekToken().is(token::period)){
        file.getToken();
        path.push_back(file.getToken({eTokenType::identifier, eTokenType::dataType}));
    }
    file.getToken(token::endStatement);
    auto shown = [](const token& t){ return t.originalValue.empty() ? t.value : t.originalValue; };
    string name = shown(nameTok);
    if(path.size() < 2)
        return parsingError(format("alias '{0}': at file scope, an alias names an object's emitter: `alias {0} = obj.{0};`.", name));
    objectDef* at = languageService.findGlobalAs<objectDef>(path[0].value);
    string objPath = shown(path[0]);
    for(size_t i = 1; at != nullptr && i + 1 < path.size(); i++){
        objPath += "." + shown(path[i]);
        variableDeclaration* step = nullptr;
        for(typeMember* m : at->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd && vd->name == path[i].value && vd->isValueAlias) step = vd;
        at = step ? languageService.findGlobalAs<objectDef>(step->type.name) : nullptr;
    }
    if(at == nullptr)
        return parsingError(format("alias '{0}': '{1}' is not an object.", name, objPath));
    if(path.back().value != nameTok.value)
        return parsingError(format("alias '{0}': an alias of a member keeps its name; write `alias {1} = {2}.{1};`.",
                                   name, shown(path.back()), objPath));
    bool found = false;
    for(typeMember* m : at->members)
        if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->name == nameTok.value){
            found = true;
            if(!fd->isEmitter)
                return parsingError(format("alias '{0}': '{1}.{0}' is a method; only an emitter can be aliased at file scope.", name, objPath));
        }
    if(!found)
        return parsingError(format("alias '{0}': '{1}' has no emitter '{0}'.", name, objPath));
    functionAliases.push_back({at, nameTok.value});
    syncFunctionAliases();
    return false;
}

// An aliased object's emitters join the globals, including overloads its extensions add later; one an
// extension has replaced leaves them.
void bglParser::syncFunctionAliases(){
    if(functionAliases.empty()) return;
    set<functionDef*> current;
    for(auto& [obj, name] : functionAliases)
        for(typeMember* m : obj->members)
            if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->name == name && fd->isEmitter && !fd->isPrePassStub)
                current.insert(fd);
    if(current == functionAliasGlobals) return;
    auto& g = languageService.globals;
    g.erase(remove_if(g.begin(), g.end(), [&](typeDef* t){
        auto* fd = dynamic_cast<functionDef*>(t);
        return fd != nullptr && functionAliasGlobals.count(fd) && !current.count(fd); }), g.end());
    for(auto& [obj, name] : functionAliases)   // in declaration order, as overloads are ranked
        for(typeMember* m : obj->members)
            if(auto* fd = dynamic_cast<functionDef*>(m); fd && current.count(fd) && !functionAliasGlobals.count(fd)
               && find(g.begin(), g.end(), (typeDef*)fd) == g.end())
                g.push_back(fd);
    functionAliasGlobals = current;
}
