// ===============================================================================
// bglParserClassObjectDecl.cpp - class and object declaration processing.
//
// Extracted from bglParser.cpp (Phase 3 of the refactor). Covers the top-level
// `class`/`object` declarations plus all the member-parsing helpers that walk
// their bodies.
//
// Top-level declarations:
//   processClassDeclaration         - class Foo { ... }, extern/extend/emitter/alias/byVal
//   processObjectDeclaration        - object Foo { ... }, including typed instances
//   processObjectExtension          - extend object Foo { ... } adds members to an existing object
//   processExtendCompoundAssignment - handle `attributes += {...}` etc. on extended objects
//   processEmitterValueDeclaration  - emitter T name {...} (no parens)
//   processArrayDeclarationFromGeneric  - array<T> name[N] declarations
//   processTypedObjectDeclaration   - Type instance { ... } shorthand
//   processAliasedDeclaration       - Type instance = otherInstance shorthand
//
// Object body member parsers:
//   parsePropertyValue          - parse RHS of `T name = value;` in object body
//   processI6InlineMember       - #i6{...} block as a member
//   processArrayMember          - array<T> name in object body
//   processMemberMethod         - non-emitter method body
//   processMemberVariable       - typed variable member with no inferred type
//   processTypedMember          - variable / method member with explicit type
//   processInheritedMember      - bare-name member that uses an inherited type
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
// Top-level: processClassDeclaration
// ===============================================================================
bool bglParser::processClassDeclaration(token tok, bool isExternal, bool isExtend, bool isEmitterClass, bool isAlias, token nameOverride, bool isByVal){
    if(getCurrentCompileContext()!=eCompileContext::global) parsingError(format("Class declarations are only allowed in global context:'{0}'", (string) tok));

    token nameTok = nameOverride.tokenType != eTokenType::unknown ? nameOverride : file.getToken({eTokenType::identifier, eTokenType::dataType});
    classDef* classPtr = nullptr;
    if(isExtend){
        classPtr = dynamic_cast<classDef*>(&languageService.getType((string)nameTok));
        if(classPtr == nullptr) parsingError(format("extend class '{0}': no previously defined class with that name", (string)nameTok));
    } else {
        //create an empty class definition object and register it immediately, so that we can refer to this type within its own definition (e.g. comparison operators)
        classPtr = &languageService.registerClass((string)nameTok, isExternal, nameTok.originalValue);
    }
    classDef& newClass = *classPtr;
    // Doc-comment: prefer the doc on the leading `class` keyword (or the first qualifier
    // token forwarded through parseQualifiers); fall back to anything attached to the name token.
    if(!tok.docComment.empty())            newClass.docComment = tok.docComment;
    else if(!nameTok.docComment.empty())   newClass.docComment = nameTok.docComment;

    if(!isExtend){
        if(isEmitterClass) newClass.isEmitterClass = true;
        if(isAlias)        newClass.isAlias = true;
        if(isByVal)        newClass.isByVal = true;
    }
    // `byVal` validation: rejects combinations that can't sensibly support a class-
    // controlled copy-in path. `extend byVal class` is rejected because byVal is a
    // class-identity decision that belongs on the original declaration; an extend
    // should pick up the existing class's identity, not re-declare it.
    if(isByVal){
        if(isExtend)       parsingError(format("'extend class {0}': 'byVal' belongs on the original class declaration, not on extend.", (string)nameTok));
        if(isExternal)     parsingError(format("'extern byVal class {0}': extern classes are I6-defined and Beguile can't insert copy-in code at their use sites.", (string)nameTok));
        if(isEmitterClass) parsingError(format("'emitter byVal class {0}': emitter classes have no stored fields; value semantics has nothing to copy.", (string)nameTok));
        if(isAlias)        parsingError(format("'alias byVal class {0}': alias classes dissolve to their parent for emission; the marker has no effect.", (string)nameTok));
    }

    classDef* savedClass = currentClass;
    currentClass = &newClass;
    openCompileContext(eCompileContext::objectDef);

    // Type parameter clause (optional, before pool/inheritance):
    //   `class Foo<T> { … }` — declares T as a type parameter scoped to the class body.
    //   Parameters are stored on the classDef but NOT registered globally — collisions
    //   with same-named instances (e.g. `Temperature t;`) would otherwise occur. Member
    //   signatures parse T as an identifier-typed token; substitution at method-lookup
    //   time replaces T with the use-site binding before any type query runs.
    //   Skipped on `extend class Foo<…>` — type parameters are part of the original decl.
    if(file.peekToken().is("<")){
        if(isExtend) parsingError(format("extend class '{0}': type parameters cannot be added by extend — they are part of the original declaration", (string)nameTok));
        if(isAlias)  parsingError(format("alias class '{0}': type parameters are not supported on alias classes", (string)nameTok));
        file.getToken(); // consume '<'
        while(true){
            token paramTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
            // Don't double-add when claiming a pre-pass stub that already populated this list.
            bool already = false;
            for(const string& tp : newClass.typeParameters) if(tp == paramTok.value){ already = true; break; }
            if(!already) newClass.typeParameters.push_back(paramTok.value);
            token sep = file.getToken({token::comma, ">"});
            if(sep.value == ">") break;
        }
    }

    // Pool size clause (optional, before any inheritance):
    //   `class Foo[N]`              — sized pool, N statically-allocated instances (emit `Class Foo(N)`)
    //   `extern class Foo[]`        — marker: I6-pooled type, size opaque to Beguile
    //   `class Foo[N]` on emitter/alias — error (no I6 backing / type aliasing)
    //   `extern class Foo[N]`       — error (size belongs to I6)
    //   `extend class Foo[...]`     — error (pool size is part of original declaration)
    tok = file.getToken();
    if(tok.is(token::bracketOpen)){
        token inner = file.getToken();
        if(inner.is(token::bracketClose)){
            // `[]` empty marker form
            if(!isExternal)
                parsingError(format("class '{0}': empty pool brackets '[]' are only valid on `extern class` (marker for an I6-defined pooled type)", (string)nameTok));
            if(isExtend)
                parsingError(format("extend class '{0}': pool brackets cannot be added by extend — pool status is part of the original declaration", (string)nameTok));
            newClass.poolSize = -1; // extern marker
        } else if(inner.is(eTokenType::integer) || inner.is(eTokenType::identifier)){
            // `[N]` sized form — N may be an integer literal or an identifier referring to a
            // `Default`/`Constant`-declared I6 constant. For identifiers, the Beguile parser
            // can't resolve the numeric value (I6 owns the constant table), so we capture the
            // identifier verbatim and let the I6 link step substitute. The numeric poolSize
            // field is set to a positive sentinel so all "is this pooled?" checks downstream
            // continue to work.
            if(isExternal)
                parsingError(format("extern class '{0}': pool size cannot be specified — extern declarations describe I6-defined types, and the I6 declaration owns the pool size. Use 'extern class {0}[]' as a marker that the type is pooled, or omit the brackets.", (string)nameTok));
            if(isExtend)
                parsingError(format("extend class '{0}': pool size cannot be modified — pool size is part of the original declaration's contract", (string)nameTok));
            if(isEmitterClass)
                parsingError(format("emitter class '{0}': pool size is not valid (emitter classes have no I6 backing for instances)", (string)nameTok));
            if(isAlias)
                parsingError(format("alias class '{0}': pool size is not valid (alias classes dissolve to another type)", (string)nameTok));
            if(inner.is(eTokenType::integer)){
                int n = stoi(inner.value);
                if(n <= 0)
                    parsingError(format("class '{0}': pool size must be a positive integer (got {1})", (string)nameTok, n));
                newClass.poolSize = n;
            } else {
                newClass.poolSize    = 1;  // positive sentinel — "is pooled" downstream checks
                newClass.poolSizeExpr = inner.value;
            }
            file.getToken(token::bracketClose);
        } else {
            parsingError(format("class '{0}': expected integer, identifier, or ']' after '[' in pool clause, got '{1}'", (string)nameTok, (string)inner));
        }
        tok = file.getToken();
    }

    // Inheritance clause:
    //   alias class requires 'for Parent'
    //   other classes use optional ': Parent [, Parent2 ...]'
    if(isAlias && !tok.is("for"))
        parsingError(format("'alias {0}' requires a parent class: use 'alias {0} for ParentClass'", (string)nameTok));
    // Detect circular inheritance: walking `parent`'s ancestry must not reach newClass.
    auto checkInheritanceCycle = [&](classDef* parent, const string& parentDisplay){
        function<bool(classDef*, set<classDef*>&)> reachesSelf = [&](classDef* c, set<classDef*>& visited) -> bool {
            if(!c || !visited.insert(c).second) return false;
            if(c == &newClass) return true;
            for(classDef* b : c->baseClasses) if(reachesSelf(b, visited)) return true;
            return false;
        };
        set<classDef*> visited;
        if(reachesSelf(parent, visited))
            parsingError(format("class '{0}': circular inheritance — '{1}' transitively inherits from '{0}'",
                                newClass.dName(), parentDisplay));
    };
    if(tok.is("for")){
        // alias class single-parent clause
        token parentTok = file.getToken({eTokenType::dataType, eTokenType::identifier});
        classDef* parent = dynamic_cast<classDef*>(&languageService.getType(parentTok.value));
        if(!parent) parsingError(format("Unknown base class '{0}'", parentTok.value));
        else { checkInheritanceCycle(parent, parentTok.originalValue); newClass.baseClasses.push_back(parent); }
        tok = file.getToken();
    } else if(tok.is(":")){
        do {
            token parentTok = file.getToken({eTokenType::dataType, eTokenType::identifier});
            classDef* parent = dynamic_cast<classDef*>(&languageService.getType(parentTok.value));
            if(!parent) parsingError(format("Unknown base class '{0}'", parentTok.value));
            else { checkInheritanceCycle(parent, parentTok.originalValue); newClass.baseClasses.push_back(parent); }
            tok = file.getToken();
        } while(tok.is(","));
    }
    // `byVal class Foo : object` — contradictory: object-derived classes are tree
    // citizens (reference semantics via the world tree), and byVal is value semantics.
    // Walk bases transitively for the check.
    if(newClass.isByVal){
        function<bool(classDef*)> inheritsObj = [&](classDef* c) -> bool {
            if(!c) return false;
            if(c->name == "object") return true;
            for(classDef* base : c->baseClasses) if(inheritsObj(base)) return true;
            return false;
        };
        for(classDef* base : newClass.baseClasses)
            if(inheritsObj(base))
                parsingError(format("'byVal class {0}': value-semantic classes cannot inherit from 'object' (tree-citizen reference semantics conflicts with value semantics).", newClass.dName()));
    }
    // Marker-form extern pool class (`extern class Foo[];`) allows no body — it's purely a
    // declaration that the type exists and is pooled in I6. Skip body parsing and return.
    if(isExternal && newClass.poolSize == -1 && tok.is(token::endStatement)){
        closeCompileContext(eCompileContext::objectDef);
        currentClass = savedClass;
        return false;
    }
    tok.assert(token::braceOpen);
    tok=file.getToken();
    while(tok.isNot(token::braceClose)){
        if(tok.is(eTokenType::eof))
            parsingError(format("Unexpected end of file inside class '{0}' — missing closing '}}'", newClass.dName()));
        Qualifiers q = parseQualifiers(tok);
        bool isEmitter = q.isEmitter;
        bool isReplace = q.isReplace;
        bool isExplicitConversion = q.isExplicit;
        bool isMemberConst = q.isConst;
        bool isMemberStatic = q.isStatic;
        bool isOperator = false;
        token returnType;
        token name;
        // emitter class: 'emitter' keyword is optional — all members are implicitly emitters
        if(newClass.isEmitterClass && !isEmitter) isEmitter = true;
        // Context-specific validation
        if(q.isExtern && !isExtend)
            parsingError("'extern' is not valid inside a class body (use on the class declaration itself)");
        if(q.isExtend)
            parsingError("'extend' is not valid inside a class body");
        if(q.isAlias)
            parsingError("'alias' is not valid inside a class body");
        // array<T> member: same handler as object bodies (refactored to take members vector).
        // Class is never verb-derived, so no grammarRule downcast applies.
        if(tok.is("array") && file.peekToken().is("<")){
            processArrayMember(newClass.members, newClass.dName(), nullptr, &newClass, &q);
            tok = file.getToken();
            continue;
        }
        // Check for inherited member type inference: if the token is an identifier (not a data type)
        // followed by '=' or ';', look up the member name in base classes to infer the type.
        if(tok.is(eTokenType::identifier) && !tok.isDataType()){
            token peek = file.peekToken(1);
            if(peek.is(token::assignment) || peek.is(token::endStatement)){
                string memberName = tok.value;
                string inferredType;
                // Search base classes for a member with this name. For arrayDeclaration members,
                // reconstruct the full templated typeName (`array<dictionaryWord>`) so downstream
                // element-type validation has the T to check against — `vd->type.name` alone is
                // just `array` and loses the elementType.
                std::function<void(classDef*)> searchBases = [&](classDef* c){
                    for(typeMember* m : c->members)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                            if(vd->name == memberName){
                                if(auto* arr = dynamic_cast<arrayDeclaration*>(m))
                                    inferredType = "array<" + arr->elementType + ">";
                                else
                                    inferredType = vd->type.name;
                                return;
                            }
                    for(classDef* base : c->baseClasses){ searchBases(base); if(!inferredType.empty()) return; }
                };
                for(classDef* base : newClass.baseClasses){ searchBases(base); if(!inferredType.empty()) break; }
                if(!inferredType.empty()){
                    // Inferred: treat tok as the member name, use the inherited type
                    returnType.value = inferredType;
                    returnType.tokenType = eTokenType::dataType;
                    name = tok;
                } else {
                    tok.assertDataType(); // will error with a meaningful message
                    returnType = tok;
                    name = file.getToken({eTokenType::identifier, eTokenType::dataType});
                }
            } else {
                tok.assertDataType();
                returnType = tok;
                name = file.getToken({eTokenType::identifier, eTokenType::dataType});
            }
        } else {
            returnType=tok.assertDataType();
            name=file.getToken({eTokenType::identifier, eTokenType::dataType});
        }
        if(name.is("operator")){
            isOperator=true;
            token opTok = file.getToken();
            if(opTok.is(token::parenOpen)){
                // conversion operator: emitter <type> operator()
                name.value = "operator()";
                tok = opTok; // the ( is already consumed; reuse it
            } else if(opTok.is(token::bracketOpen)){
                // subscript operator: operator[] (read) or operator[]= (write)
                file.getToken(token::bracketClose);
                token maybeAssign = file.getToken();
                if(maybeAssign.is(token::assignment)){
                    name.value = "[]=";
                    tok = file.getToken(eTokenType::symbol);
                } else {
                    name.value = "[]";
                    tok = maybeAssign;  // already consumed the next symbol
                }
            } else if(opTok.is("switch")){
                // switch comparison operator: operator switch(type v)
                name.value = "switch";
                tok = file.getToken(eTokenType::symbol);
            } else if(opTok.is("auto")){
                // auto inference operator: operator auto() — return type only, no body
                name.value = "auto";
                tok = file.getToken(eTokenType::symbol);  // should be ( or ;
            } else if(opTok.is(eTokenType::identifier)){
                // qualified operator: e.g. "prefix++" — read qualifier then oper symbol
                token opSym = file.getToken(eTokenType::oper);
                name.value = opTok.value + opSym.value;  // e.g. "prefix++"
                tok = file.getToken(eTokenType::symbol);
            } else if(opTok.is("?")){
                // unary query operator: operator ?()
                name.value = "?";
                tok = file.getToken(eTokenType::symbol);
            } else {
                opTok.assert(eTokenType::oper);
                name = opTok;
                tok=file.getToken(eTokenType::symbol);
            }
        } else {
            tok=file.getToken({eTokenType::symbol, eTokenType::oper});
        }

        if(tok.is(token::parenOpen))  { //this is a function
            functionDef& funcDef=*(new functionDef());
            funcDef.name=(string) name; funcDef.displayName=name.originalValue;
            funcDef.src = name.src.line > 0 ? name.src : file.currentLocation();
            funcDef.returnType=languageService.getType((string) returnType);
            funcDef.isEmitter=isEmitter;
            funcDef.isExplicit=isExplicitConversion;
            funcDef.isDefault=q.isDefault;
            if(!returnType.docComment.empty())   funcDef.docComment = returnType.docComment;
            else if(!name.docComment.empty())    funcDef.docComment = name.docComment;
            if(isExplicitConversion && funcDef.name != "operator()")
                parsingError("'explicit' is only valid on conversion operators (operator())");
            processParameterList(funcDef);
            // Synthesize per-(class, method, param) backing globals for byVal-class params.
            // Same machinery as the top-level call from processRoutineDeclaration; pass the
            // enclosing class name as context so backings on same-named methods across
            // classes don't collide.
            synthesizeParamBackings(funcDef, newClass.dName());
            // operator auto: no params, no body, max one per class
            if(funcDef.name == "auto"){
                if(!funcDef.params.empty())
                    parsingError("operator auto() cannot have parameters");
                // Check for duplicate (ignore pre-pass stubs, which the full pass replaces)
                for(typeMember* m : newClass.members)
                    if(auto* fd = dynamic_cast<functionDef*>(m))
                        if(fd->name == "auto" && !fd->isPrePassStub)
                            parsingError(format("class '{0}' already has an operator auto()", newClass.dName()));
                // Must be followed by ; (no body)
                token afterAuto = file.getToken();
                if(!afterAuto.is(token::endStatement))
                    parsingError("operator auto() cannot have a body — declare return type only");
                newClass.members.push_back(&funcDef);
                tok = file.getToken();
                continue;
            }
            if(isEmitter && !funcDef.params.empty() && (funcDef.name == "init" || funcDef.name == "deinit"))
                parsingError(format("Emitter '{0}' cannot accept parameters", funcDef.name));
            if(!isEmitter && (funcDef.name == "switch" || funcDef.name == "?"))
                parsingError(format("operator {0}() must be declared as an emitter", funcDef.name));
            if(!isEmitter && (funcDef.name == "init" || funcDef.name == "deinit"))
                parsingError(format("'{0}' must be declared as an emitter", funcDef.name));
            if((isExternal || newClass.isExternal || newClass.isAlias) && !isEmitter){
                // extern/alias class non-emitter methods not allowed
                parsingError(format("Non-emitter function '{0}' is not allowed in an extern or alias class", funcDef.name));
            } else if(funcDef.isEmitter && file.peekToken().is(token::endStatement)){
                // Semicolon-terminated emitter: pass-through (value unchanged)
                file.getToken(); // consume ';'
                i6Block& rawblock=*(new i6Block());
                rawblock.i6Body=" $self";
                funcDef.body=&rawblock;
            } else {
                file.getToken(token::braceOpen); //consume the open brace;
                if(funcDef.isEmitter){
                    i6Block& rawblock=*(new i6Block());
                    rawblock.i6Body = file.getRawTextThroughClosingBrace(/*isI6Content=*/true);
                    funcDef.body=&rawblock;
                } else {
                    funcDef.body = new statementBlock();
                    functionDef* savedFunc = currentFunc;
                    currentFunc = &funcDef;
                    openCompileContext(eCompileContext::codeBlock, dynamic_cast<statementBlock*>(funcDef.body));
                    while(processNextStatement(funcDef) == false){}
                    closeCompileContext(eCompileContext::codeBlock);
                    currentFunc = savedFunc;
                    if(funcDef.returnType.name != "void" && !allPathsReturn(dynamic_cast<statementBlock*>(funcDef.body)))
                        parsingError(format("Non-void routine '{0}' has no return statement", funcDef.name));
                }
            }
            if(isExtend){
                // find an existing member with the same name and parameter signature
                // Pre-scan stubs match by name only (they have no params)
                typeMember* existing = nullptr;
                bool alreadyReplaced = false;
                for(typeMember* m : newClass.members){
                    functionDef* fd = dynamic_cast<functionDef*>(m);
                    if(!fd || fd->name != funcDef.name) continue;
                    if(fd->isPrePassStub){ existing = m; break; }
                    if(fd->params.size() == funcDef.params.size()){
                        bool match = true;
                        for(size_t i=0; i<funcDef.params.size(); i++){
                            if(fd->params[i]->type.name != funcDef.params[i]->type.name){ match=false; break; }
                        }
                        // operator(), operator[], and operator[]= can have multiple overloads with
                        // different return types; mirror the rule from the primary-class path so
                        // extend blocks can add conversion operators alongside existing ones.
                        if(match && fd->returnType.name != funcDef.returnType.name &&
                               (funcDef.name == "operator()" || funcDef.name == "[]" || funcDef.name == "[]="))
                            match = false;
                        if(match){ existing = m; break; }
                    }
                }
                // If the existing member is a pre-scan stub, replace it silently
                if(existing){
                    auto* existFd = dynamic_cast<functionDef*>(existing);
                    if(existFd && existFd->isPrePassStub){
                        for(auto it=newClass.members.begin(); it!=newClass.members.end(); ++it)
                            if(*it==existing){ *it = &funcDef; break; }
                        alreadyReplaced = true;
                    }
                }
                if(existing && !alreadyReplaced && !isReplace)
                    parsingError(format("extend class '{0}': member '{1}' is already defined; use 'replace' to override", newClass.dName(), funcDef.dName()));
                if(!existing && isReplace)
                    parsingWarning(format("extend class '{0}': 'replace' specified but no existing member '{1}' found; treating as new definition", newClass.dName(), funcDef.dName()));
                if(existing && !alreadyReplaced){
                    for(auto it=newClass.members.begin(); it!=newClass.members.end(); ++it)
                        if(*it==existing){ *it = &funcDef; break; }
                    alreadyReplaced = true;
                }
                if(alreadyReplaced){ tok = file.getToken(); continue; }
            }
            if(funcDef.isEmitter && funcDef.name == "_bglglobaldeclaration"){
                // declaration emitter — store raw body on the class, not as a member
                i6Block* body = dynamic_cast<i6Block*>(funcDef.body);
                if(body) newClass.globalDeclarationBody = body->i6Body;
            } else {
                if(!isExtend){
                    // Silently replace pre-scan stubs (may have been seeded by an 'extend class' that
                    // appears later in the file — pre-scan pushes stubs into this class's member list).
                    if(replaceStubMember(newClass.members, funcDef)) { tok = file.getToken(); continue; }
                    bool replacedExisting = false;
                    for(size_t i = 0; i < newClass.members.size(); i++){
                        functionDef* fd = dynamic_cast<functionDef*>(newClass.members[i]);
                        if(fd && fd->name == funcDef.name && fd->params.size() == funcDef.params.size()){
                            bool match = true;
                            for(size_t j=0; j<funcDef.params.size(); j++)
                                if(fd->params[j]->type.name != funcDef.params[j]->type.name){ match=false; break; }
                            // operator(), operator[], and operator[]= can have multiple overloads with
                            // different return types. operator() uses them for conversion operators;
                            // operator[] / operator[]= use them for element-type-aware array subscripts
                            // so the library can declare one overload per supported element type.
                            if(match && fd->returnType.name != funcDef.returnType.name &&
                                   (funcDef.name == "operator()" || funcDef.name == "[]" || funcDef.name == "[]="))
                                match = false;
                            if(match){
                                if(isReplace){
                                    newClass.members[i] = &funcDef;
                                    replacedExisting = true;
                                    break;
                                }
                                parsingError(format("class '{0}': method '{1}' with the same signature is already defined (originally at {2}:{3}); use 'replace' to override",
                                    newClass.dName(), funcDef.dName(), fd->src.file, fd->src.line));
                            }
                        }
                    }
                    if(replacedExisting) { tok = file.getToken(); continue; }
                    // Check base class hierarchy for shadowed methods — warn if 'replace' not specified.
                    // A "shadow" requires matching signature (arity + param types). Same name but
                    // different arity or param types is an overload, not a shadow — no warning.
                    if(!isReplace && !funcDef.name.empty()){
                        string shadowedFrom;
                        bool shadowedIsDefault = false;
                        auto sigMatches = [&](functionDef* fd){
                            if(fd->params.size() != funcDef.params.size()) return false;
                            for(size_t j = 0; j < funcDef.params.size(); j++)
                                if(fd->params[j]->type.name != funcDef.params[j]->type.name) return false;
                            return true;
                        };
                        function<void(classDef*)> searchBases = [&](classDef* c){
                            if(!shadowedFrom.empty()) return;
                            for(typeMember* m : c->members)
                                if(auto* fd = dynamic_cast<functionDef*>(m))
                                    if(fd->name == funcDef.name && sigMatches(fd)){
                                        shadowedFrom = c->dName(); shadowedIsDefault = fd->isDefault; return;
                                    }
                            for(classDef* base : c->baseClasses) searchBases(base);
                        };
                        for(classDef* base : newClass.baseClasses) searchBases(base);
                        if(!shadowedFrom.empty() && !shadowedIsDefault)
                            parsingWarning(format("class '{0}': method '{1}' shadows definition in base class '{2}'; use 'replace' to suppress this warning",
                                newClass.dName(), funcDef.dName(), shadowedFrom));
                    }
                }
                newClass.members.push_back(&funcDef);
            }
        }
        else if(tok.is(token::braceOpen) && isEmitter && !isOperator){
            // Emitter value in class body: emitter Type name { body }
            functionDef& funcDef = *(new functionDef());
            funcDef.name = (string)name; funcDef.displayName = name.originalValue;
            funcDef.src = name.src.line > 0 ? name.src : file.currentLocation();
            funcDef.returnType = languageService.getType((string)returnType);
            funcDef.isEmitter = true;
            funcDef.isValueEmitter = true;
            funcDef.isDefault = q.isDefault;
            funcDef.src = file.currentLocation();
            i6Block& rawblock = *(new i6Block());
            rawblock.i6Body = file.getRawTextThroughClosingBrace(/*isI6Content=*/true);
            funcDef.body = &rawblock;
            if(!replaceStubMember(newClass.members, funcDef))
                newClass.members.push_back(&funcDef);
        }
        else{
            if(isOperator==true) parsingError("Operators must be functions.");
            // Emitter class: allow alias members (typed reference to another class) but not variable declarations
            if(isEmitter || newClass.isEmitterClass){
                string aliasTypeName = (string)returnType;
                // auto inference: require = classReference to infer type
                if(aliasTypeName == "auto"){
                    if(!tok.is(token::assignment))
                        parsingError(format("'auto' alias member '{0}' requires an initializer to infer the type", (string)name));
                    token rhs = file.getToken({eTokenType::identifier, eTokenType::dataType});
                    classDef* rhsCls = dynamic_cast<classDef*>(&languageService.getType(rhs.value));
                    if(!rhsCls)
                        parsingError(format("'auto' alias member '{0}': '{1}' is not a declared class",
                            (string)name, rhs.originalValue.empty() ? rhs.value : rhs.originalValue));
                    aliasTypeName = rhs.value;
                    tok = file.getToken();  // consume ;
                }
                classDef* aliasCls = dynamic_cast<classDef*>(&languageService.getType(aliasTypeName));
                if(!aliasCls)
                    parsingError(format("Emitter class '{0}' only supports emitter functions, emitter values, and class alias members; '{1}' is not a class",
                        newClass.dName(), aliasTypeName));
                // Alias member: register as a variableDeclaration with the class type
                // No I6 backing — resolved at compile time for dot-access
                variableDeclaration& aliasDef = *(new variableDeclaration());
                aliasDef.name = (string)name;
                aliasDef.type = languageService.getType(aliasTypeName);
                aliasDef.isExternal = true;  // no I6 emission
                if(tok.is(token::assignment) && aliasTypeName != "auto")
                    parsingError(format("Alias member '{0}' on emitter class cannot have an initializer; use 'auto' to infer type", (string)name));
                // Consume ; if present
                if(tok.isNot(token::endStatement))
                    parsingError(format("Expected ';' after alias member '{0}'", (string)name));
                newClass.members.push_back(&aliasDef);
                tok = file.getToken();
                continue;
            }
            if(tok.isNot(token::endStatement) && tok.isNot(token::assignment))
                parsingError(format("Expected '=' or ';' after member '{0}'", (string)name));
            // Note: alias-class members may carry a default value (e.g. `int priority = 10;` on
            // `class verb`). The value is a compile-time default consulted by the emitter when
            // lifting the field from an instance body; it is not emitted as an I6 property.
            variableDeclaration& varDef=*(new variableDeclaration());
            varDef.name=(string) name;
            varDef.displayName = name.originalValue;
            varDef.src = name.src.line > 0 ? name.src : file.currentLocation();
            varDef.type=languageService.getType((string) returnType);
            if(isMemberConst) varDef.isConst = true;
            varDef.isStatic = isMemberStatic;
            if(!returnType.docComment.empty())   varDef.docComment = returnType.docComment;
            else if(!name.docComment.empty())    varDef.docComment = name.docComment;
            if(tok.is(token::assignment)){
                token first = file.getToken();
                if(first.is(token::braceOpen)){
                    // initializer list: { expr, expr, ... } with optional nesting
                    initializerList* list = new initializerList();
                    token t2 = file.getToken();
                    while(!t2.is(token::braceClose) && !t2.is(eTokenType::eof)){
                        if(t2.is(token::braceOpen)){
                            initializerList* inner = new initializerList();
                            token t3 = file.getToken();
                            while(!t3.is(token::braceClose) && !t3.is(eTokenType::eof)){
                                expression* elem = parseExpression(t3, {",", token::braceClose}, nullptr, nullptr);
                                inner->elements.push_back(elem);
                                if(elem->terminator == token::braceClose) break;
                                t3 = file.getToken();
                            }
                            list->elements.push_back(inner);
                            t2 = file.getToken({token::comma, token::braceClose});
                            if(t2.is(token::braceClose)) break;
                            t2 = file.getToken();
                            continue;
                        }
                        expression* elem = parseExpression(t2, {",", token::braceClose}, nullptr, nullptr);
                        list->elements.push_back(elem);
                        if(elem->terminator == token::braceClose) break;
                        t2 = file.getToken();
                    }
                    file.getToken(token::endStatement);
                    // Element-type validation for inherited array members reassigned in this
                    // class body. Mirrors parsePropertyValue's check — `returnType` here is the
                    // full templated form (`array<dictionaryWord>`) when the inferred base member
                    // is an arrayDeclaration, so we can extract T and reject sibling/supertype
                    // values (e.g. `name = {.gadget, noun}` where `noun` is a grammarToken).
                    {
                        string rtStr = (string)returnType;
                        string expectedElemType;
                        if(rtStr.size() > 6 && rtStr.substr(0, 6) == "array<" && rtStr.back() == '>')
                            expectedElemType = rtStr.substr(6, rtStr.size() - 7);
                        if(!expectedElemType.empty())
                            for(size_t i = 0; i < list->elements.size(); i++){
                                expression* elem = list->elements[i];
                                if(elem->resolvedType.empty())
                                    parsingError(format("Undeclared identifier in initializer list (element {0})", i));
                                else if(!isTypeCompatible(elem->resolvedType, expectedElemType))
                                    parsingError(format("Element {0} has type '{1}', expected '{2}'", i, elem->resolvedType, expectedElemType));
                            }
                    }
                    varDef.declaredExpressionValue = list;
                } else {
                    varDef.declaredExpressionValue = parseExpression(first, {token::endStatement}, nullptr, nullptr);
                }
            }
            if(isExtend){
                typeMember* existing = nullptr;
                for(typeMember* m : newClass.members){
                    variableDeclaration* vd = dynamic_cast<variableDeclaration*>(m);
                    if(vd && vd->name == varDef.name){ existing = m; break; }
                }
                if(existing && !isReplace)
                    parsingError(format("extend class '{0}': member '{1}' is already defined; use 'replace' to override", newClass.dName(), varDef.dName()));
                if(!existing && isReplace)
                if(existing)
                    for(auto it=newClass.members.begin(); it!=newClass.members.end(); ++it)
                        if(*it==existing){ newClass.members.erase(it); break; }
            } else {
                // Check for an existing stub from pre-scan (static member variables) and replace it.
                bool replacedStub = false;
                for(size_t i = 0; i < newClass.members.size(); i++){
                    if(auto* vd = dynamic_cast<variableDeclaration*>(newClass.members[i])){
                        if(vd->name == varDef.name){
                            if(vd->isPrePassStub){
                                newClass.members[i] = (typeMember*)&varDef;
                                replacedStub = true;
                            } else {
                                parsingError(format("class '{0}': member '{1}' is already defined", newClass.dName(), varDef.dName()));
                            }
                            break;
                        }
                    }
                }
                if(!replacedStub) newClass.members.push_back((typeMember*)&varDef);
                tok=file.getToken();
                continue;
            }
            newClass.members.push_back((typeMember*)&varDef);
        }
        tok=file.getToken();
    }   
    //if...
    //file.getToken("emitter");
    //token retval=file.getToken(eTokenType::dataType);

    // Pool class validation — enforce single create/destroy and signature rules.
    // I6's create/destroy mechanism invokes a SINGLE property of each name; multiple
    // overloads would collide in I6. v1 restricts pool classes to one create and one destroy.
    if(newClass.poolSize != 0){
        functionDef* createFn = nullptr;
        functionDef* destroyFn = nullptr;
        for(typeMember* m : newClass.members){
            auto* fd = dynamic_cast<functionDef*>(m);
            if(!fd) continue;
            if(fd->name == "create"){
                if(createFn != nullptr)
                    parsingError(format("class '{0}': only one 'create' method is allowed on a pooled class (I6's create veneer dispatches to a single property)", newClass.dName()));
                createFn = fd;
            } else if(fd->name == "destroy"){
                if(destroyFn != nullptr)
                    parsingError(format("class '{0}': only one 'destroy' method is allowed on a pooled class", newClass.dName()));
                destroyFn = fd;
            }
        }
        if(createFn != nullptr){
            if(createFn->returnType.name != "void")
                parsingError(format("class '{0}': 'create' must have void return type — I6 ignores the return value", newClass.dName()));
            if(createFn->params.size() > 3)
                parsingError(format("class '{0}': 'create' is limited to 3 parameters on Z-machine (I6's class-message veneer caps it). Got {1}.", newClass.dName(), createFn->params.size()));
        }
        if(destroyFn != nullptr){
            if(destroyFn->returnType.name != "void")
                parsingError(format("class '{0}': 'destroy' must have void return type — I6 ignores the return value", newClass.dName()));
            if(!destroyFn->params.empty())
                parsingError(format("class '{0}': 'destroy' must take no parameters", newClass.dName()));
        }
    }

    currentClass = savedClass;
    closeCompileContext(eCompileContext::objectDef);
    return false;
}
// Reads <ReturnType, ParamType, ...> from stream and returns the full "func<...>" type string.
// Called after the "func" token has been consumed.

// ===============================================================================
// Object body member parsers
// ===============================================================================
void bglParser::parsePropertyValue(variableDeclaration& prop, string typeName){
    token first = file.getToken();
    if(first.is(token::braceOpen)){
        initializerList* list = new initializerList();
        token t = file.getToken();
        while(!t.is(token::braceClose) && !t.is(eTokenType::eof)){
            if(t.is(token::braceOpen)){
                // nested initializer list: { {a, b}, {c, d} }
                initializerList* inner = new initializerList();
                token t2 = file.getToken();
                while(!t2.is(token::braceClose) && !t2.is(eTokenType::eof)){
                    expression* elem = parseExpression(t2, {",", token::braceClose}, nullptr, nullptr);
                    inner->elements.push_back(elem);
                    if(elem->terminator == token::braceClose) break;
                    t2 = file.getToken();
                }
                list->elements.push_back(inner);
                // after inner }, expect , or outer }
                t = file.getToken({token::comma, token::braceClose});
                if(t.is(token::braceClose)) break;
                t = file.getToken();
                continue;
            }
            expression* elem = parseExpression(t, {",", token::braceClose}, nullptr, nullptr);
            list->elements.push_back(elem);
            if(elem->terminator == token::braceClose) break;
            t = file.getToken();
        }
        if(file.peekToken().is(token::endStatement)) file.getToken();
        string expectedElemType;
        // For array<T>, extract T as the expected element type
        if(typeName.size() > 6 && typeName.substr(0, 6) == "array<" && typeName.back() == '>')
            expectedElemType = typeName.substr(6, typeName.size() - 7);
        // Fallback: infer from the class's single-param method
        if(expectedElemType.empty()){
            classDef* listClass = dynamic_cast<classDef*>(&languageService.getType(typeName));
            if(listClass != nullptr)
                for(typeMember* m : listClass->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m))
                        if(fd->params.size() == 1){ expectedElemType = fd->params[0]->type.name; break; }
        }
        for(size_t i = 0; i < list->elements.size(); i++){
            expression* elem = list->elements[i];
            if(elem->resolvedType.empty())
                parsingError(format("Undeclared identifier in initializer list (element {0})", i));
            else if(!expectedElemType.empty() && !isTypeCompatible(elem->resolvedType, expectedElemType))
                parsingError(format("Element {0} has type '{1}', expected '{2}'", i, elem->resolvedType, expectedElemType));
        }
        prop.declaredExpressionValue = list;
    } else {
        expression* expr = parseExpression(first, {token::endStatement}, nullptr, nullptr);
        // auto inference: if the property type is 'auto', adopt the expression's resolved type
        if(typeName == "auto" && !expr->resolvedType.empty()){
            typeName = expr->resolvedType;
            prop.type = languageService.getType(typeName);
        } else if(!typeName.empty() && typeName != "var" && !expr->resolvedType.empty()
           && !isTypeCompatible(expr->resolvedType, typeName))
            parsingError(format("Cannot assign value of type '{0}' to property '{1}' of type '{2}'",
                typeDisplayName(expr->resolvedType), prop.dName(), typeDisplayName(typeName)));
        prop.declaredExpressionValue = expr;
    }
}

void bglParser::processI6InlineMember(objectDef& obj){
    token t = file.getToken();
    i6RawNode& node = *(new i6RawNode());
    if(t.is(token::braceOpen)){
        deque<char> closeChar;
        closeChar.push_back('}');
        t = file.getBasicToken(true);
        while(closeChar.size() > 0){
            if(t.value[0] == closeChar.back()){
                closeChar.pop_back();
                if(closeChar.size() == 0) break;
            }
            node.text = node.text + t.value;
            if(closeChar.back() != '\n'){
                if(t.value[0] == '{') closeChar.push_back('}');
                if(t.value[0] == '!') closeChar.push_back('\n');
            }
            t = file.getBasicToken(true);
        }
    } else {
        while(t.isNot("\n")){
            node.text = node.text + t.value;
            t = file.getBasicToken(true);
        }
    }
    obj.members.push_back((typeMember*)&node);
}

bool bglParser::processArrayMember(vector<typeMember*>& members, const string& ownerDName, verbObjectDef* vodForGrammarRules,
                                   abstractObject* ctx, Qualifiers* q){
    file.getToken("<");
    string elemType = file.getToken({eTokenType::dataType, eTokenType::identifier}).value;
    file.getToken(">");
    token propName = file.getToken(eTokenType::identifier);

    // Routine form: `array<T> name(...)` — class/object member function returning a
    // generic array type (e.g. LINQ chain primitives like
    //   `array<var> filter(array<var> src, func<bool, var> pred) { ... }`).
    // We inline the routine-creation flow here instead of calling processRoutineDeclaration,
    // because that path registers into languageService.globals and we need member-vector
    // registration. This mirrors the relevant slice of processMemberMethod / class-body
    // routine creation, scoped to the array<T> return-type case.
    if(ctx && q && file.peekToken().is(token::parenOpen)){
        file.getToken(token::parenOpen);
        functionDef& funcDef = *(new functionDef());
        funcDef.name = (string)propName;
        funcDef.displayName = propName.originalValue;
        funcDef.src = propName.src.line > 0 ? propName.src : file.currentLocation();
        funcDef.returnType.name = "array<" + elemType + ">";
        funcDef.isEmitter  = q->isEmitter;
        funcDef.isExternal = q->isExtern;

        processParameterList(funcDef);

        if(funcDef.isEmitter){
            file.getToken(token::braceOpen);
            i6Block& rawblock = *(new i6Block());
            rawblock.i6Body = file.getRawTextThroughClosingBrace(/*isI6Content=*/true);
            funcDef.body = &rawblock;
        } else {
            file.getToken(token::braceOpen);
            funcDef.body = new statementBlock();
            functionDef* savedFunc = currentFunc;
            currentFunc = &funcDef;
            openCompileContext(eCompileContext::codeBlock, dynamic_cast<statementBlock*>(funcDef.body));
            while(processNextStatement(funcDef) == false){}
            closeCompileContext(eCompileContext::codeBlock);
            currentFunc = savedFunc;
            if(funcDef.returnType.name != "void" && !allPathsReturn(dynamic_cast<statementBlock*>(funcDef.body)))
                parsingError(format("Non-void routine '{0}' has no return statement", funcDef.name));
        }

        if(!replaceStubMember(members, funcDef))
            members.push_back(&funcDef);
        return true;
    }

    // array<grammarRule>: parse {{Verb, {pattern}}, ...} initializer
    if(elemType == "grammarrule"){
        file.getToken(token::assignment);
        file.getToken(token::braceOpen);
        token inner = file.getToken();
        while(inner.is(token::braceOpen)){
            grammarRuleDecl& rd = *(new grammarRuleDecl());
            rd.name = propName.value;
            rd.type = languageService.getType("grammarrule");
            // Parse verb reference
            token verbTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
            string lower = verbTok.value;
            transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            bool verbFound = false;
            for(verbObjectDef* v : languageService.verbs)
                if(v->name == lower){ verbFound = true; break; }
            if(!verbFound){
                string display = verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
                parsingError(format("'{0}' in grammarRule initializer is not a declared verb", display));
            }
            rd.targetVerb = verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
            file.getToken(token::comma);
            file.getToken(token::braceOpen);
            rd.line = parseGrammarLineContent();
            // Optional `, priority` third element (default 10 from BLR `class verb`).
            rd.line.priority   = parseOptionalGrammarRulePriority(languageService.getClassFieldIntDefault("verb", "priority", 10));
            rd.line.isOwnLine  = false;
            file.getToken(token::braceClose);  // closing } of this rule
            // Add to verb's grammarLines if applicable
            if(vodForGrammarRules){
                grammarLine gl = rd.line;
                gl.targetVerb = rd.targetVerb;
                vodForGrammarRules->grammarLines.push_back(gl);
            }
            members.push_back(&rd);
            token sep = file.getToken({token::comma, token::braceClose});
            if(sep.is(token::braceClose)) break;
            inner = file.getToken();
        }
        if(file.peekToken().is(token::endStatement)) file.getToken();
        return false;
    }

    token sym = file.getToken({token::bracketOpen, token::assignment, token::endStatement});
    arrayDeclaration& arrDecl = *(new arrayDeclaration());
    arrDecl.name = (string)propName;
    arrDecl.type = languageService.getType("array");
    arrDecl.elementType = elemType;
    if(sym.is(token::bracketOpen)){
        token sizeTok = file.getToken(eTokenType::integer);
        arrDecl.arraySize = stoi(sizeTok.value);
        file.getToken(token::bracketClose);
        file.getToken(token::endStatement);
    } else if(sym.is(token::assignment)){
        // Check for string initializer: array<char> name = "text";
        token peek = file.peekToken(1);
        if(peek.is(eTokenType::quote) || peek.is(eTokenType::rawQuote)){
            if(elemType != "char" && elemType != "charliteral")
                parsingError("String initializer is only valid for array<char>");
            token strTok = file.getToken();
            arrDecl.stringInitializer = strTok.value;
            arrDecl.isByteArray = true;
            arrDecl.type = languageService.getType("bytearray");
            if(file.peekToken().is(token::endStatement)) file.getToken();
        } else {
            file.getToken(token::braceOpen);
            initializerList* list = new initializerList();
            token t = file.getToken();
            while(!t.is(token::braceClose) && !t.is(eTokenType::eof)){
                expression* elem = parseExpression(t, {",", token::braceClose}, nullptr, nullptr);
                list->elements.push_back(elem);
                if(elem->terminator == token::braceClose) break;
                t = file.getToken();
            }
            if(file.peekToken().is(token::endStatement)) file.getToken();
            arrDecl.declaredExpressionValue = list;
        }
    }
    // Set byte array type for char arrays
    if(elemType == "char"){
        arrDecl.isByteArray = true;
        arrDecl.type = languageService.getType("bytearray");
    }
    // Replace pre-pass stub if present (pre-scan registers a variableDeclaration stub for
    // every property/array declaration so forward references resolve in sibling methods).
    bool replacedStub = false;
    for(size_t i = 0; i < members.size(); i++){
        if(members[i]->name == arrDecl.name){
            if(auto* existingVd = dynamic_cast<variableDeclaration*>(members[i])){
                if(existingVd->isPrePassStub){
                    members[i] = (typeMember*)&arrDecl;
                    replacedStub = true;
                    break;
                }
            }
            parsingError(format("'{0}': member '{1}' is already defined", ownerDName, arrDecl.dName()));
        }
    }
    if(!replacedStub) members.push_back((typeMember*)&arrDecl);
    return false;
}


void bglParser::processMemberMethod(objectDef& obj, token returnType, token name, bool isReplace){
    functionDef& funcDef = *(new functionDef());
    funcDef.name = (string)name;
    funcDef.displayName = name.originalValue;
    funcDef.src = name.src.line > 0 ? name.src : file.currentLocation();
    funcDef.returnType = languageService.getType((string)returnType);
    processParameterList(funcDef);
    file.getToken(token::braceOpen);
    funcDef.body = new statementBlock();
    functionDef* savedFunc = currentFunc;
    currentFunc = &funcDef;
    openCompileContext(eCompileContext::codeBlock, dynamic_cast<statementBlock*>(funcDef.body));
    while(processNextStatement(funcDef) == false){}
    closeCompileContext(eCompileContext::codeBlock);
    currentFunc = savedFunc;
    if(funcDef.returnType.name != "void" && !allPathsReturn(dynamic_cast<statementBlock*>(funcDef.body)))
        parsingError(format("Non-void routine '{0}' has no return statement", funcDef.name));
    // Check class hierarchy for shadowed methods — warn if 'replace' not specified.
    // A "shadow" requires matching signature (arity + param types). Same name but
    // different arity or param types is an overload, not a shadow — no warning.
    if(!isReplace && !funcDef.name.empty()){
        string shadowedFrom;
        bool shadowedIsDefault = false;
        auto sigMatches = [&](functionDef* fd){
            if(fd->params.size() != funcDef.params.size()) return false;
            for(size_t j = 0; j < funcDef.params.size(); j++)
                if(fd->params[j]->type.name != funcDef.params[j]->type.name) return false;
            return true;
        };
        function<void(classDef*)> searchClass = [&](classDef* c){
            if(!shadowedFrom.empty() || !c) return;
            for(typeMember* m : c->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == funcDef.name && sigMatches(fd)){
                        shadowedFrom = c->dName(); shadowedIsDefault = fd->isDefault; return;
                    }
            for(classDef* base : c->baseClasses) searchClass(base);
        };
        // Search objectClass hierarchy
        if(obj.objectClass) searchClass(obj.objectClass);
        // Search objectDef base classes (from ': Parent' clause)
        for(typeDef* base : obj.baseClasses)
            if(auto* cd = dynamic_cast<classDef*>(base)) searchClass(cd);
        if(!shadowedFrom.empty() && !shadowedIsDefault)
            parsingWarning(format("object '{0}': method '{1}' shadows definition in class '{2}'; use 'replace' to suppress this warning",
                obj.dName(), funcDef.dName(), shadowedFrom));
    }
    if(!replaceStubMember(obj.members, funcDef)){
        // Signature-aware duplicate check: same name + same arity + same param types.
        // Same-arity-different-types is an overload, not a duplicate (mirrors the class-
        // body rule). The emitter mangles overload sets so each emits as a distinct I6
        // property.
        auto sigEq = [&](functionDef* fd) -> bool {
            if(fd->name != funcDef.name) return false;
            if(fd->params.size() != funcDef.params.size()) return false;
            for(size_t i = 0; i < funcDef.params.size(); i++)
                if(fd->params[i]->type.name != funcDef.params[i]->type.name) return false;
            return true;
        };
        bool replaced = false;
        if(isReplace){
            for(size_t i = 0; i < obj.members.size(); i++)
                if(auto* fd = dynamic_cast<functionDef*>(obj.members[i]))
                    if(sigEq(fd)){
                        obj.members[i] = &funcDef;
                        replaced = true;
                        break;
                    }
            if(!replaced)
                parsingWarning(format("object '{0}': 'replace' specified but no existing method '{1}' to replace", obj.dName(), funcDef.dName()));
        }
        if(!replaced){
            for(typeMember* m : obj.members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(sigEq(fd))
                        parsingError(format("object '{0}': method '{1}' with the same signature is already defined (originally at {2}:{3}); use 'replace' to override",
                            obj.dName(), funcDef.dName(), fd->src.file, fd->src.line));
        }
        if(!replaced)
            obj.members.push_back((typeMember*)&funcDef);
    }
}


void bglParser::processMemberVariable(objectDef& obj, string typeName, string name, bool hasValue, bool isReplace){
    variableDeclaration& prop = *(new variableDeclaration());
    prop.name = name;
    prop.src = file.currentLocation();
    prop.type = languageService.getType(typeName);
    if(hasValue) parsePropertyValue(prop, typeName);
    bool replaced = false;
    if(isReplace){
        for(size_t i = 0; i < obj.members.size(); i++)
            if(auto* vd = dynamic_cast<variableDeclaration*>(obj.members[i]))
                if(vd->name == prop.name){ obj.members[i] = &prop; replaced = true; break; }
        if(!replaced)
            parsingWarning(format("object '{0}': 'replace' specified but no existing member '{1}' to replace", obj.dName(), prop.dName()));
    }
    if(!replaced){
        // Replace any pre-pass stub registered for this property — pre-scan registers
        // a variableDeclaration stub for every property declaration so forward references
        // from sibling methods resolve. The full pass replaces the stub with the real
        // typed declaration here.
        for(size_t i = 0; i < obj.members.size(); i++){
            if(obj.members[i]->name == prop.name){
                if(auto* existingVd = dynamic_cast<variableDeclaration*>(obj.members[i])){
                    if(existingVd->isPrePassStub){
                        obj.members[i] = &prop;
                        replaced = true;
                        break;
                    }
                }
                parsingError(format("object '{0}': member '{1}' is already defined", obj.dName(), prop.dName()));
            }
        }
        if(!replaced) obj.members.push_back((typeMember*)&prop);
    }
}


void bglParser::processTypedMember(objectDef& obj, token typeTok, bool isReplace){
    token propName = file.getToken(eTokenType::identifier);
    if(propName.is("operator")){
        token opTok = file.getToken();
        if(opTok.is(token::parenOpen))
            propName.value = "operator()";
        else {
            opTok.assert(eTokenType::oper);
            propName.value = "operator " + opTok.value;
        }
    }
    // grammarRule member: parse {Verb, {pattern}} or array<grammarRule> {{Verb, {pattern}}, ...}
    if(typeTok.value == "grammarrule"){
        file.getToken(token::assignment);
        file.getToken(token::braceOpen);
        token peek = file.peekToken();
        if(peek.is(token::braceOpen)){
            // array form: single grammarRule can't have nested braces
            parsingError(format("grammarRule '{0}' takes a single rule initializer; use array<grammarRule> for multiple rules", propName.value));
        }
        // Single rule: {VerbRef, {.word, TOKEN, ...}}
        grammarRuleDecl& rd = *(new grammarRuleDecl());
        rd.name = propName.value;
        rd.type = languageService.getType("grammarrule");
        token verbTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
        // Resolve verb reference
        string lower = verbTok.value;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        bool verbFound = false;
        for(verbObjectDef* v : languageService.verbs)
            if(v->name == lower){ verbFound = true; break; }
        if(!verbFound){
            string display = verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
            parsingError(format("'{0}' in grammarRule initializer is not a declared verb", display));
        }
        rd.targetVerb = verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
        file.getToken(token::comma);
        file.getToken(token::braceOpen);
        rd.line = parseGrammarLineContent();
        // Optional `, priority` third element (default 10 from BLR `class verb`).
        rd.line.priority   = parseOptionalGrammarRulePriority(languageService.getClassFieldIntDefault("verb", "priority", 10));
        rd.line.isOwnLine  = false;
        file.getToken(token::braceClose);  // closing } of outer initializer
        if(file.peekToken().is(token::endStatement)) file.getToken();
        // If on a grammarRuleList-bearing object, also add to its grammarLines
        if(auto* vod = dynamic_cast<verbObjectDef*>(&obj)){
            grammarLine gl = rd.line;
            gl.targetVerb = rd.targetVerb;
            vod->grammarLines.push_back(gl);
        }
        obj.members.push_back(&rd);
        return;
    }
    // grammarRuleList member: parse grammar lines instead of standard property value
    if(typeTok.value == "grammarrulelist"){
        // Resolve verb context for inferred-verb grammarRule entries
        auto* vod = dynamic_cast<verbObjectDef*>(&obj);
        bool isVerbContext = (vod != nullptr);
        if(!isVerbContext){
            bool isVerbDerived = false;
            function<void(classDef*)> searchBases = [&](classDef* cls){
                if(!cls || isVerbDerived) return;
                if(cls->name == "verb"){ isVerbDerived = true; return; }
                for(classDef* base : cls->baseClasses) searchBases(base);
            };
            if(obj.objectClass) searchBases(obj.objectClass);
            isVerbContext = isVerbDerived;
        }
        string inferredVerb;
        if(vod) inferredVerb = vod->displayName.empty() ? vod->name : vod->displayName;

        file.getToken(token::assignment);
        grammarRuleListDecl& gtd = *(new grammarRuleListDecl());
        gtd.name = propName.value;
        gtd.type = languageService.getType("grammarrulelist");
        gtd.verbName = inferredVerb;

        // Parse grammar lines and create a grammarRuleDecl for each
        vector<grammarLine> lines = parseGrammarLines();
        for(grammarLine& gl : lines){
            grammarRuleDecl& rd = *(new grammarRuleDecl());
            rd.name = propName.value;
            rd.type = languageService.getType("grammarrule");
            rd.line = gl;
            rd.targetVerb = inferredVerb;
            if(!isVerbContext)
                parsingWarning(format("grammarRule in '{0}' uses inferred verb form but the owning object is not a verb; use explicit form: grammarRule r = {{Verb, {{pattern}}}}", propName.value));
            gtd.rules.push_back(&rd);
            gl.targetVerb = inferredVerb;
            gtd.grammarLines.push_back(gl);
        }

        if(file.peekToken().is(token::endStatement)) file.getToken();
        if(vod){
            vod->grammarLines.insert(vod->grammarLines.end(), gtd.grammarLines.begin(), gtd.grammarLines.end());
            if(vod->isExternal) languageService.globals.push_back(&gtd);
        }
        obj.members.push_back(&gtd);
        return;
    }
    token sym = file.getToken({token::assignment, token::endStatement, token::parenOpen});
    if(sym.is(token::parenOpen))
        processMemberMethod(obj, typeTok, propName, isReplace);
    else
        processMemberVariable(obj, typeTok.value, propName.value, sym.is(token::assignment), isReplace);
}


void bglParser::processInheritedMember(objectDef& obj, token nameTok){
    string propTypeName;
    // Search the object's declared class first (if any), then fall back to the base 'object' class.
    // For arrayDeclaration members, reconstruct the full templated typeName (e.g. `array<dictionaryWord>`)
    // so parsePropertyValue can extract the element type — otherwise `vd->type.name` returns just `array`
    // and parsePropertyValue's fallback misidentifies the element type as int (from operator[]'s index param).
    std::function<void(classDef*)> searchClass = [&](classDef* cls){
        if(!cls || !propTypeName.empty()) return;
        for(typeMember* m : cls->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == nameTok.value){
                    if(auto* arr = dynamic_cast<arrayDeclaration*>(m))
                        propTypeName = "array<" + arr->elementType + ">";
                    else
                        propTypeName = vd->type.name;
                    return;
                }
        for(classDef* base : cls->baseClasses)
            searchClass(base);
    };
    searchClass(obj.objectClass);
    if(propTypeName.empty()){
        classDef* baseObj = dynamic_cast<classDef*>(&languageService.getType("object"));
        searchClass(baseObj);
    }
    if(propTypeName.empty())
        parsingError(format("'{0}' is not a property defined on the object class or its bases; add a type specifier (e.g. 'object {0} = ...')", nameTok.value));
    // grammarRule/grammarRuleList with inferred type: route to grammar-specific parsing
    if(propTypeName == "grammarrule"){
        file.getToken(token::assignment);
        file.getToken(token::braceOpen);
        grammarRuleDecl& rd = *(new grammarRuleDecl());
        rd.name = nameTok.value;
        rd.type = languageService.getType("grammarrule");
        token verbTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
        string lower = verbTok.value;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        bool verbFound = false;
        for(verbObjectDef* v : languageService.verbs)
            if(v->name == lower){ verbFound = true; break; }
        if(!verbFound){
            string display = verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
            parsingError(format("'{0}' in grammarRule initializer is not a declared verb", display));
        }
        rd.targetVerb = verbTok.originalValue.empty() ? verbTok.value : verbTok.originalValue;
        file.getToken(token::comma);
        file.getToken(token::braceOpen);
        rd.line = parseGrammarLineContent();
        // Optional `, priority` third element (default 10 from BLR `class verb`).
        rd.line.priority   = parseOptionalGrammarRulePriority(languageService.getClassFieldIntDefault("verb", "priority", 10));
        rd.line.isOwnLine  = false;
        file.getToken(token::braceClose);
        if(file.peekToken().is(token::endStatement)) file.getToken();
        if(auto* vod = dynamic_cast<verbObjectDef*>(&obj)){
            grammarLine gl = rd.line;
            gl.targetVerb = rd.targetVerb;
            vod->grammarLines.push_back(gl);
        }
        obj.members.push_back(&rd);
        return;
    }
    if(propTypeName == "grammarrulelist"){
        auto* vod = dynamic_cast<verbObjectDef*>(&obj);
        bool isVerbContext = (vod != nullptr);
        if(!isVerbContext){
            bool isVerbDerived = false;
            function<void(classDef*)> searchBases = [&](classDef* cls){
                if(!cls || isVerbDerived) return;
                if(cls->name == "verb"){ isVerbDerived = true; return; }
                for(classDef* base : cls->baseClasses) searchBases(base);
            };
            if(obj.objectClass) searchBases(obj.objectClass);
            isVerbContext = isVerbDerived;
        }
        string inferredVerb;
        if(vod) inferredVerb = vod->displayName.empty() ? vod->name : vod->displayName;

        file.getToken(token::assignment);
        grammarRuleListDecl& gtd = *(new grammarRuleListDecl());
        gtd.name = nameTok.value;
        gtd.type = languageService.getType("grammarrulelist");
        gtd.verbName = inferredVerb;

        vector<grammarLine> lines = parseGrammarLines();
        for(grammarLine& gl : lines){
            grammarRuleDecl& rd = *(new grammarRuleDecl());
            rd.name = nameTok.value;
            rd.type = languageService.getType("grammarrule");
            rd.line = gl;
            rd.targetVerb = inferredVerb;
            if(!isVerbContext)
                parsingWarning(format("grammarRule in '{0}' uses inferred verb form but the owning object is not a verb; use explicit form: grammarRule r = {{Verb, {{pattern}}}}", nameTok.value));
            gtd.rules.push_back(&rd);
            gl.targetVerb = inferredVerb;
            gtd.grammarLines.push_back(gl);
        }

        if(file.peekToken().is(token::endStatement)) file.getToken();
        if(vod){
            vod->grammarLines.insert(vod->grammarLines.end(), gtd.grammarLines.begin(), gtd.grammarLines.end());
            if(vod->isExternal) languageService.globals.push_back(&gtd);
        }
        obj.members.push_back(&gtd);
        return;
    }
    bool hasValue = file.getToken({token::assignment, token::endStatement}).is(token::assignment);
    processMemberVariable(obj, propTypeName, nameTok.value, hasValue);
}


// ===============================================================================
// Misc small declaration helpers + processObjectDeclaration
// ===============================================================================
bool bglParser::processEmitterValueDeclaration(token typeTok, token nameTok){
    // emitter Type name { body } — no parens, expands inline as a value emitter
    functionDef& funcDef = *(new functionDef());
    funcDef.name = nameTok.value;
    funcDef.displayName = nameTok.originalValue;
    funcDef.returnType = languageService.getType(typeTok.value);
    funcDef.isEmitter = true;
    funcDef.isValueEmitter = true;
    funcDef.src = file.currentLocation();
    i6Block& rawblock = *(new i6Block());
    rawblock.i6Body = file.getRawTextThroughClosingBrace(/*isI6Content=*/true);
    funcDef.body = &rawblock;
    for(typeDef*& g : languageService.globals)
        if(auto* stub = dynamic_cast<functionDef*>(g))
            if(stub->name == funcDef.name && stub->isPrePassStub){ g = &funcDef; return false; }
    languageService.globals.push_back(&funcDef);
    return false;
}


bool bglParser::processArrayDeclarationFromGeneric(token arrayTok, Qualifiers& q, abstractObject& ctx){
    // Entered after "array" "<" have been consumed. Reads: elementType > name symbol
    string elemType = file.getToken({eTokenType::dataType, eTokenType::identifier}).value;
    file.getToken(">");
    // Build the full generic type token (e.g. "array<int>") so processRoutineDeclaration sees it
    token typeTok = arrayTok;
    typeTok.value = "array<" + elemType + ">";
    token name = file.getToken({eTokenType::identifier, eTokenType::dataType});
    token symbol = file.getToken({token::bracketOpen, token::assignment, token::endStatement, token::parenOpen});
    if(symbol.is(token::parenOpen))
        return processRoutineDeclaration(typeTok, name, ctx, q.isExtern, q.isEmitter, q.isReplace);
    processArrayDeclaration(arrayTok, name, elemType, symbol, ctx, q.isExtern);
    return false;
}


bool bglParser::processTypedObjectDeclaration(token typeTok, token nameTok, token classNameTok, Qualifiers& q, abstractObject& ctx){
    // Entered after "Type name : ClassName" have been consumed. Reads optional "as alias" then symbol.
    string objectClassName = classNameTok.value;
    string i6alias;
    if(file.peekToken().is("as")){
        file.getToken();
        token aliasTok = file.getToken(eTokenType::identifier);
        i6alias = aliasTok.originalValue.empty() ? aliasTok.value : aliasTok.originalValue;
    }
    token symbol = file.getToken({token::assignment, token::parenOpen, token::endStatement, token::braceOpen});
    if(symbol.is(token::parenOpen))
        return processRoutineDeclaration(typeTok, nameTok, ctx, q.isExtern, q.isEmitter, q.isReplace);
    else if(symbol.is(token::braceOpen))
        return processObjectDeclaration(typeTok, nameTok, q.isExtern, objectClassName, i6alias, true, q.isEmitter);
    else
        return processVariableDeclaration(typeTok, nameTok, symbol, ctx, q.isExtern, q.isConst, i6alias, q.isRef);
}


bool bglParser::processAliasedDeclaration(token typeTok, token nameTok, token aliasTok, Qualifiers& q, abstractObject& ctx){
    // Entered after "Type name as alias" have been consumed. Reads symbol.
    string i6alias = aliasTok.originalValue.empty() ? aliasTok.value : aliasTok.originalValue;
    token symbol = file.getToken({token::assignment, token::parenOpen, token::endStatement, token::braceOpen});
    if(symbol.is(token::parenOpen))
        return processRoutineDeclaration(typeTok, nameTok, ctx, q.isExtern, q.isEmitter, q.isReplace);
    else if(symbol.is(token::braceOpen))
        return processObjectDeclaration(typeTok, nameTok, q.isExtern, "", i6alias, true, q.isEmitter);
    else
        return processVariableDeclaration(typeTok, nameTok, symbol, ctx, q.isExtern, q.isConst, i6alias, q.isRef);
}


bool bglParser::processObjectDeclaration(token objectType, token name, bool isExternal, string className, string i6alias, bool hasBody, bool isEmitter){
    // If emitter qualifier is set and body is present, this is an emitter value declaration
    if(isEmitter && hasBody) return processEmitterValueDeclaration(objectType, name);


    // Resolve the class to determine if this is a verb-derived object
    string resolvedClassName = !className.empty() ? className : objectType.value;
    bool isVerbDerived = false;
    if(!resolvedClassName.empty()){
        function<bool(classDef*)> checkVerbBase = [&](classDef* cls) -> bool {
            if(!cls) return false;
            if(cls->name == "verb") return true;
            for(classDef* base : cls->baseClasses)
                if(checkVerbBase(base)) return true;
            return false;
        };
        if(classDef* cls = dynamic_cast<classDef*>(&languageService.getType(resolvedClassName)))
            isVerbDerived = checkVerbBase(cls);
    }

    // Create verbObjectDef for verb-derived objects, objectDef otherwise
    objectDef* objPtr;
    verbObjectDef* vod = nullptr;
    string origName = name.originalValue.empty() ? (string)name : name.originalValue;
    if(isVerbDerived){
        verbObjectDef& vd = languageService.registerVerbObject(origName, isExternal);
        objPtr = &vd;
        vod = &vd;
    } else {
        objectDef& od = languageService.registerObject((string)name, isExternal, name.originalValue);
        objPtr = &od;
    }
    objectDef& newObj = *objPtr;
    if(!i6alias.empty()) newObj.i6name = i6alias;
    // Doc-comment attached to the object's declaration (leading type or name token).
    if(!objectType.docComment.empty())   newObj.docComment = objectType.docComment;
    else if(!name.docComment.empty())    newObj.docComment = name.docComment;
    if(!resolvedClassName.empty()){
        if(classDef* cls = dynamic_cast<classDef*>(&languageService.getType(resolvedClassName)))
            newObj.objectClass = cls;
        else if(!className.empty())
            parsingError(format("Unknown class '{0}' in object declaration", className));
    }

    // Extern objects: parse body for type registration but validate members.
    // Only type declarations (no initializer) and emitters are allowed.
    if(isExternal){
        if(hasBody){
            objectDef* savedObject = currentObject;
            currentObject = &newObj;
            openCompileContext(eCompileContext::objectDef);

            // Syntax sweetener ("pretty lie") for extern verb claimed-words: when the body
            // begins with a dictionary-word literal, the entire body is interpreted as the
            // trigger-words section of a single grammar line. Desugars to the canonical form
            // `extern verb V { grammar = { {.w1|.w2|.w3} }; }`. Triggers may be combined with
            // `|`-alternation; no pattern tokens are allowed in this form (extern verbs don't
            // emit grammar). The canonical body parsing path below handles all other cases.
            if(file.peekToken().is(eTokenType::dictionaryWord)){
                auto* vod = dynamic_cast<verbObjectDef*>(&newObj);
                if(!vod)
                    parsingError(format("extern object '{0}': dictionary-word body shorthand is only valid on extern verbs", newObj.dName()));
                grammarLine line;
                token trigger = file.getToken(eTokenType::dictionaryWord);
                line.verbWord = trigger.value;
                while(file.peekToken().is("|")){
                    file.getToken();  // consume |
                    token alt = file.getToken(eTokenType::dictionaryWord);
                    line.additionalVerbWords.push_back(alt.value);
                }
                file.getToken(token::braceClose);
                line.targetVerb = newObj.displayName.empty() ? newObj.name : newObj.displayName;
                line.isOwnLine = true;
                vod->grammarLines.push_back(line);
                closeCompileContext(eCompileContext::objectDef);
                currentObject = savedObject;
                return false;
            }

            token tok = file.getToken();
            while(tok.isNot(token::braceClose) && tok.isNot(eTokenType::eof)){
                Qualifiers q = parseQualifiers(tok);
                if(q.isEmitter){
                    // Emitter methods and emitter values are allowed on extern objects
                    token retType = tok;
                    token propName = file.getToken(eTokenType::identifier);
                    if(propName.is("operator")){
                        token opTok = file.getToken();
                        if(opTok.is(token::parenOpen)) propName.value = "operator()";
                        else if(opTok.is("?")) propName.value = "?";
                        else if(opTok.is("switch")) propName.value = "switch";
                        else { opTok.assert(eTokenType::oper); propName.value = opTok.value; }
                    }
                    functionDef& funcDef = *(new functionDef());
                    funcDef.name = (string)propName;
                    funcDef.returnType = languageService.getType((string)retType);
                    funcDef.isEmitter = true;
                    funcDef.isExplicit = q.isExplicit;
                    funcDef.isDefault = q.isDefault;
                    token sym = file.getToken();
                    bool funcHasParens = false;
                    if(sym.is(token::parenOpen)){ funcHasParens = true; processParameterList(funcDef); sym = file.getToken(); }
                    i6Block& rawblock = *(new i6Block());
                    if(sym.is(token::endStatement)) rawblock.i6Body = " $self";
                    else { rawblock.i6Body = file.getRawTextThroughClosingBrace(/*isI6Content=*/true); if(!funcHasParens) funcDef.isValueEmitter = true; }
                    funcDef.body = &rawblock;
                    newObj.members.push_back((typeMember*)&funcDef);
                } else if(tok.isDataType()){
                    // Type declaration or method stub on extern object
                    token propName = file.getToken({eTokenType::identifier, eTokenType::dataType});
                    token sym = file.getToken({token::endStatement, token::assignment, token::parenOpen});
                    if(sym.is(token::assignment)){
                        // Property with initializer — skip the value, issue error
                        token v = file.getToken();
                        while(!v.is(token::endStatement) && !v.is(token::braceClose) && !v.is(eTokenType::eof)) v = file.getToken();
                        parsingError(format("extern object '{0}': property '{1}' cannot have an initializer; extern objects are defined in I6",
                            newObj.dName(), propName.value));
                    }
                    if(sym.is(token::parenOpen)){
                        // Method declaration — register as functionDef for type checking
                        functionDef& funcDef = *(new functionDef());
                        funcDef.name = propName.value;
                        funcDef.returnType = languageService.getType(tok.value);
                        funcDef.isExternal = true;
                        processParameterList(funcDef);
                        // Body is not allowed on non-emitter methods in extern objects
                        token after = file.getToken();
                        if(after.is(token::braceOpen)){
                            file.getRawTextThroughClosingBrace();
                            parsingError(format("extern object '{0}': non-emitter method '{1}' cannot have a body; use 'emitter' or declare without a body",
                                newObj.dName(), propName.value));
                        }
                        // else: after is ; — declaration stub, valid
                        newObj.members.push_back((typeMember*)&funcDef);
                        tok = file.getToken();
                        continue;
                    }
                    // Property without initializer — register for type checking
                    variableDeclaration& prop = *(new variableDeclaration());
                    prop.name = propName.value;
                    prop.type = languageService.getType(tok.value);
                    prop.isExternal = true;
                    newObj.members.push_back((typeMember*)&prop);
                } else if(tok.is(eTokenType::identifier) && tok.value == "grammar"){
                    // Extern verb body: `grammar = { {.w1|.w2|.w3, ...}, ... };` declares the dict
                    // words this verb claims at the I6 level. Identical syntax to non-extern verbs;
                    // the difference is purely emission: extern verbs never emit grammar (their I6
                    // grammar is defined externally), so only the trigger words (first dict word
                    // of each line plus |-alternation extras) are meaningful here. Pattern tokens
                    // after the trigger position are parsed (for syntax validation) but ignored;
                    // a warning is issued so the author knows they have no effect.
                    auto* vod = dynamic_cast<verbObjectDef*>(&newObj);
                    if(!vod)
                        parsingError(format("'grammar' is only valid inside a verb body (on '{0}')", newObj.dName()));
                    file.getToken(token::assignment);
                    vector<grammarLine> lines = parseGrammarLines();
                    string verbDisplayName = newObj.displayName.empty() ? newObj.name : newObj.displayName;
                    for(grammarLine& gl : lines){
                        if(!gl.patternTokens.empty())
                            parsingWarning(format("extern verb '{0}': pattern tokens after the trigger word(s) in `grammar = {{...}}` are ignored — extern verbs don't emit I6 grammar; only the dict words in the first position contribute to the verb's claimed-words list",
                                verbDisplayName));
                        gl.targetVerb = verbDisplayName;
                        gl.isOwnLine = true;
                        vod->grammarLines.push_back(gl);
                    }
                    if(file.peekToken().is(token::endStatement)) file.getToken();
                } else {
                    parsingError(format("Unexpected '{0}' in extern object body", tok.value));
                }
                tok = file.getToken();
            }
            closeCompileContext(eCompileContext::objectDef);
            currentObject = savedObject;
        }
        return false;
    }

    objectDef* savedObject = currentObject;
    currentObject = &newObj;
    openCompileContext(eCompileContext::objectDef);

    token tok = file.getToken();
    while(tok.isNot(token::braceClose)){
        if(tok.is(eTokenType::eof)) parsingError(format("Unexpected end of file inside object '{0}' — missing closing '}}'", newObj.dName()));
        if(tok.is(eTokenType::directive)){
            if(tok.is("#i6")) processI6InlineMember(newObj);
            else parsingError(format("Unsupported directive in object body: '{0}'", tok.value));
            tok = file.getToken();
            continue;
        }
        Qualifiers q = parseQualifiers(tok);
        bool memberIsReplace = q.isReplace;
        // Context-specific validation
        if(q.isExtern)  parsingError("'extern' is not valid inside an object body");
        if(q.isExtend)  parsingError("'extend' is not valid inside an object body");
        if(q.isAlias){
            // Type alias member: `alias memberName for TypeName;`
            // Creates a compile-time type reference — used by namespace-scoped type resolution.
            token aliasName = tok;  // tok is already the first token after qualifiers
            token forKw = file.getToken();
            if(!forKw.is("for"))
                parsingError(format("'alias {0}' in object body requires 'for': alias {0} for TypeName;", aliasName.value));
            token typeTok = file.getToken({eTokenType::dataType, eTokenType::identifier});
            if(!languageService.isClassType(typeTok.value))
                parsingError(format("alias member '{0}': '{1}' is not a declared type (class or enum)", aliasName.value,
                    typeTok.originalValue.empty() ? typeTok.value : typeTok.originalValue));
            file.getToken(token::endStatement);
            variableDeclaration& aliasDef = *(new variableDeclaration());
            aliasDef.name = (string)aliasName;
            aliasDef.displayName = aliasName.originalValue;
            aliasDef.type = languageService.getType(typeTok.value);
            aliasDef.isExternal = true;  // no I6 emission
            aliasDef.isAlias = true;     // marks as type alias, not instance
            newObj.members.push_back(&aliasDef);
            tok = file.getToken();
            continue;
        }
        if(q.isConst)   parsingError("'const' is not valid inside an object body (use on the property type)");
        if(q.isStatic)  parsingError("'static' is not valid inside an object body");
        if(q.isDefault) parsingError("'default' is only valid in class declarations, not object instances");
        if(q.isEmitter){
            // `emitter array<T> name(...)` — route to processArrayMember which already
            // handles emitter routines (via q->isEmitter). Without this dispatch, the
            // emitter branch below would try to consume `<` as the propName identifier
            // and fail. Mirrors the same routing done for non-emitter `array<T>` at the
            // bottom of this loop.
            if(tok.value == "array" && file.peekToken().is("<")){
                processArrayMember(newObj.members, newObj.dName(), dynamic_cast<verbObjectDef*>(&newObj), &newObj, &q);
                tok = file.getToken();
                continue;
            }
            // Alias member: `emitter auto name = ClassRef;` — compile-time indirection to another class.
            // Supported on objects so namespace-like objects can expose emitter classes alongside real state.
            if(tok.value == "auto"){
                token aliasName = file.getToken({eTokenType::identifier, eTokenType::dataType});
                file.getToken(token::assignment);
                token rhs = file.getToken({eTokenType::identifier, eTokenType::dataType});
                classDef* rhsCls = dynamic_cast<classDef*>(&languageService.getType(rhs.value));
                if(!rhsCls)
                    parsingError(format("'auto' alias member '{0}': '{1}' is not a declared class",
                        (string)aliasName, rhs.originalValue.empty() ? rhs.value : rhs.originalValue));
                file.getToken(token::endStatement);
                variableDeclaration& aliasDef = *(new variableDeclaration());
                aliasDef.name = (string)aliasName;
                aliasDef.type = languageService.getType(rhs.value);
                aliasDef.isExternal = true;  // no I6 emission
                newObj.members.push_back(&aliasDef);
                tok = file.getToken();
                continue;
            }
            // emitter method inside object body — parse as raw I6 body
            token retType = tok;
            token propName = file.getToken(eTokenType::identifier);
            if(propName.is("operator")){
                token opTok = file.getToken();
                if(opTok.is(token::parenOpen)) propName.value = "operator()";
                else if(opTok.is("?")) propName.value = "?";
                else if(opTok.is("switch")) propName.value = "switch";
                else { opTok.assert(eTokenType::oper); propName.value = opTok.value; }
            }
            functionDef& funcDef = *(new functionDef());
            funcDef.name = (string)propName;
            funcDef.returnType = languageService.getType((string)retType);
            funcDef.isEmitter = true;
            funcDef.isExplicit = q.isExplicit;
            funcDef.isDefault = q.isDefault;
            if(q.isExplicit && funcDef.name != "operator()")
                parsingError("'explicit' is only valid on conversion operators (operator())");
            token sym = file.getToken();
            bool hasParens = false;
            if(sym.is(token::parenOpen)){
                hasParens = true;
                processParameterList(funcDef);
                sym = file.getToken();
            }
            i6Block& rawblock = *(new i6Block());
            if(sym.is(token::endStatement)){
                // Semicolon-terminated emitter: pass-through
                rawblock.i6Body = " $self";
            } else {
                // sym should be braceOpen
                rawblock.i6Body = file.getRawTextThroughClosingBrace(/*isI6Content=*/true);
                if(!hasParens) funcDef.isValueEmitter = true;
            }
            funcDef.body = &rawblock;
            if(!replaceStubMember(newObj.members, funcDef))
                newObj.members.push_back((typeMember*)&funcDef);
        } else if(tok.value == "array")
            processArrayMember(newObj.members, newObj.dName(), dynamic_cast<verbObjectDef*>(&newObj), &newObj, &q);
        else if(tok.isDataType())
            processTypedMember(newObj, tok, memberIsReplace);
        else if(tok.is(eTokenType::identifier))
            processInheritedMember(newObj, tok);
        else
            parsingError(format("Unexpected token '{0}' in object body", tok.value));
        tok = file.getToken();
    }

    closeCompileContext(eCompileContext::objectDef);
    currentObject = savedObject;

    // Verb-specific post-processing: link perform() and warn if missing
    if(vod && !isExternal){
        for(typeMember* m : vod->members)
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == "perform"){ vod->doFunc = fd; break; }
        if(!vod->doFunc)
            parsingWarning(format("verb '{0}' does not define perform()", origName));
    }

    return false;
}

// ===============================================================================
// extend object { ... } - add members to an existing object
// ===============================================================================
bool bglParser::processObjectExtension(token nameTok){
    if(getCurrentCompileContext() != eCompileContext::global)
        parsingError("'extend' declarations are only allowed in global context");

    string lower = nameTok.value;
    // Look up as verb first, then as regular object
    objectDef* obj = nullptr;
    verbObjectDef* vod = nullptr;
    for(verbObjectDef* v : languageService.verbs)
        if(v->name == lower){ obj = v; vod = v; break; }
    if(!obj){
        for(typeDef* g : languageService.globals)
            if(auto* od = dynamic_cast<objectDef*>(g))
                if(od->name == lower){ obj = od; break; }
    }
    if(!obj)
        parsingError(format("extend '{0}': no previously declared object with that name",
            nameTok.originalValue.empty() ? nameTok.value : nameTok.originalValue));

    bool isExternalObj = obj->isExternal || (vod && vod->isExternal);

    // Parse body using standard object body loop
    file.getToken(token::braceOpen);
    objectDef* savedObject = currentObject;
    currentObject = obj;
    openCompileContext(eCompileContext::objectDef);

    // For verb extend blocks, track the block's grammar priority. `priority = N;` inside this
    // extend body acts as a block-local directive (it stamps the priority onto grammar lines
    // added by `grammar +=` here) rather than mutating a persistent member on the verb. The
    // default comes from `class verb`'s BLR-declared `int priority = N;` default so the value
    // lives in Beguile, not buried in C++.
    int verbPriorityDefault = (vod != nullptr) ? languageService.getClassFieldIntDefault("verb", "priority", 10) : 0;
    int extendBlockPriority = verbPriorityDefault;
    bool extendHadReplaceGrammar = false;  // tracks `grammar = { ... }` (replace) — used to error
                                           // out a later `priority = N;` set in the same block

    token tok = file.getToken();
    while(tok.isNot(token::braceClose)){
        if(tok.is(eTokenType::eof)) parsingError("Unexpected end of file inside object — missing closing '}'");
        if(tok.is(eTokenType::directive)){
            if(tok.is("#i6")) processI6InlineMember(*obj);
            else parsingError(format("Unsupported directive in extend body: '{0}'", tok.value));
            tok = file.getToken();
            continue;
        }
        Qualifiers q = parseQualifiers(tok);
        bool memberIsReplace = q.isReplace;
        if(q.isExtern)  parsingError("'extern' is not valid inside an extend body");
        if(q.isExtend)  parsingError("'extend' is not valid inside an extend body");
        if(q.isAlias){
            // Type alias member: `alias memberName for TypeName;`
            token aliasName = tok;
            token forKw = file.getToken();
            if(!forKw.is("for"))
                parsingError(format("'alias {0}' in extend body requires 'for': alias {0} for TypeName;", aliasName.value));
            token typeTok = file.getToken({eTokenType::dataType, eTokenType::identifier});
            if(!languageService.isClassType(typeTok.value))
                parsingError(format("alias member '{0}': '{1}' is not a declared type (class or enum)", aliasName.value,
                    typeTok.originalValue.empty() ? typeTok.value : typeTok.originalValue));
            file.getToken(token::endStatement);
            variableDeclaration& aliasDef = *(new variableDeclaration());
            aliasDef.name = (string)aliasName;
            aliasDef.displayName = aliasName.originalValue;
            aliasDef.type = languageService.getType(typeTok.value);
            aliasDef.isExternal = true;
            aliasDef.isAlias = true;
            obj->members.push_back(&aliasDef);
            tok = file.getToken();
            continue;
        }
        if(q.isDefault) parsingError("'default' is only valid in class declarations, not object instances");
        if(isExternalObj && !q.isEmitter){
            // For extern objects, only compound assignment on collection members is allowed,
            // plus a few block-local directives that don't actually mutate the extern object
            // (e.g. verb extend's `priority = N;` is captured into extendBlockPriority and
            // stamped onto added grammar lines — it never becomes a member on the extern verb).
            bool isAllowed = false;
            if(tok.isDataType()){
                token peekName = file.peekToken();
                token peekOp = file.peekToken(2);
                if(peekOp.is("+=") || peekOp.is("-=")) isAllowed = true;
            } else if(tok.is(eTokenType::identifier)){
                token peekOp = file.peekToken();
                if(peekOp.is("+=") || peekOp.is("-=")) isAllowed = true;
                else if(vod != nullptr && tok.value == "priority" && peekOp.is(token::assignment))
                    isAllowed = true;  // block-local priority directive (see identifier branch below)
                else if(vod != nullptr && tok.value == "grammar" && peekOp.is(token::assignment))
                    isAllowed = true;  // replace semantics: `grammar = { ... }` (see identifier branch below)
            }
            if(!isAllowed)
                parsingError(format("Cannot add members to extern object '{0}'; only compound assignment (+=) on collection members is allowed",
                    nameTok.originalValue.empty() ? nameTok.value : nameTok.originalValue));
        }
        if(q.isEmitter){
            if(isExternalObj)
                parsingError(format("Cannot add emitter methods to extern object '{0}'",
                    nameTok.originalValue.empty() ? nameTok.value : nameTok.originalValue));
            // Alias member: `emitter auto name = ClassRef;` — compile-time indirection to
            // another class. Mirrors the same case in the primary object-body parser so
            // `extend bgl { emitter auto asm = bglOpCodes; }` works the same as declaring
            // it inside the original `object bgl { ... }`.
            if(tok.value == "auto"){
                token aliasName = file.getToken({eTokenType::identifier, eTokenType::dataType});
                file.getToken(token::assignment);
                token rhs = file.getToken({eTokenType::identifier, eTokenType::dataType});
                classDef* rhsCls = dynamic_cast<classDef*>(&languageService.getType(rhs.value));
                if(!rhsCls)
                    parsingError(format("'auto' alias member '{0}': '{1}' is not a declared class",
                        (string)aliasName, rhs.originalValue.empty() ? rhs.value : rhs.originalValue));
                file.getToken(token::endStatement);
                variableDeclaration& aliasDef = *(new variableDeclaration());
                aliasDef.name = (string)aliasName;
                aliasDef.type = languageService.getType(rhs.value);
                aliasDef.isExternal = true;  // no I6 emission
                obj->members.push_back(&aliasDef);
                tok = file.getToken();
                continue;
            }
            token retType = tok;
            token propName = file.getToken(eTokenType::identifier);
            if(propName.is("operator")){
                token opTok = file.getToken();
                if(opTok.is(token::parenOpen)) propName.value = "operator()";
                else if(opTok.is("?")) propName.value = "?";
                else if(opTok.is("switch")) propName.value = "switch";
                else { opTok.assert(eTokenType::oper); propName.value = opTok.value; }
            }
            functionDef& funcDef = *(new functionDef());
            funcDef.name = (string)propName;
            funcDef.returnType = languageService.getType((string)retType);
            funcDef.isEmitter = true;
            funcDef.isExplicit = q.isExplicit;
            funcDef.isDefault = q.isDefault;
            token sym = file.getToken();
            bool hasParens = false;
            if(sym.is(token::parenOpen)){ hasParens = true; processParameterList(funcDef); sym = file.getToken(); }
            i6Block& rawblock = *(new i6Block());
            if(sym.is(token::endStatement)) rawblock.i6Body = " $self";
            else { rawblock.i6Body = file.getRawTextThroughClosingBrace(/*isI6Content=*/true); if(!hasParens) funcDef.isValueEmitter = true; }
            funcDef.body = &rawblock;
            if(!replaceStubMember(obj->members, funcDef)){
                bool replaced = false;
                if(memberIsReplace){
                    for(size_t i = 0; i < obj->members.size(); i++)
                        if(auto* fd = dynamic_cast<functionDef*>(obj->members[i]))
                            if(fd->name == funcDef.name){ obj->members[i] = &funcDef; replaced = true; break; }
                }
                if(!replaced) obj->members.push_back((typeMember*)&funcDef);
            }
        } else if(tok.value == "array")
            processArrayMember(obj->members, obj->dName(), dynamic_cast<verbObjectDef*>(obj), obj, &q);
        else if(tok.isDataType()){
            // Check for += / -= compound assignment on a typed member
            token peekName = file.peekToken();
            token peekOp = file.peekToken(2);
            if((peekOp.is("+=") || peekOp.is("-=")) && peekName.is(eTokenType::identifier)){
                token memberName = file.getToken();
                token op = file.getToken();
                processExtendCompoundAssignment(*obj, memberName, op.value, vod, extendBlockPriority);
            } else {
                processTypedMember(*obj, tok, memberIsReplace);
            }
        }
        else if(tok.is(eTokenType::identifier)){
            // Check for += / -= compound assignment (inferred type)
            token peekOp = file.peekToken();
            if(peekOp.is("+=") || peekOp.is("-=")){
                if(vod != nullptr && tok.value == "grammar"){
                    // `grammar +=` / `grammar -=` are not part of the grammar surface — the
                    // visual closeness to `=` made silent typo-flips between append and replace
                    // too easy. In `extend`, plain `grammar = { ... }` appends (default safe);
                    // destructive replacement requires the explicit `replace` qualifier.
                    parsingError(format("'grammar {0}' is not supported. In an `extend` body, `grammar = {{ ... }}` appends (default); use `replace grammar = {{ ... }}` for destructive replacement.", peekOp.value));
                }
                token op = file.getToken();
                processExtendCompoundAssignment(*obj, tok, op.value, vod, extendBlockPriority);
            } else if(vod != nullptr && tok.value == "priority" && peekOp.is(token::assignment)){
                // Verb extend block: `priority = N;` is a block-local directive that sets the
                // priority stamped onto subsequent `grammar +=` contributions. It is NOT a
                // persistent member on the verb (which would conflict with multiple extend
                // blocks at different priorities, all merging into the same verbObjectDef).
                file.getToken(token::assignment);
                token valTok = file.getToken();
                int newPriority = extendBlockPriority;
                try { newPriority = stoi(valTok.value); }
                catch(...) { parsingError(format("priority: expected integer literal, got '{0}'", valTok.value)); }
                if(extendHadReplaceGrammar && newPriority != verbPriorityDefault)
                    parsingError("priority is meaningless with `grammar = { ... }` (replace); remove one or set priority to the default");
                extendBlockPriority = newPriority;
                file.getToken(token::endStatement);
            } else if(vod != nullptr && tok.value == "grammar" && peekOp.is(token::assignment)){
                // Verb extend block: `grammar = { ... }` is APPEND by default (additive,
                // priority-stamped from the surrounding extend block). `replace grammar = { ... }`
                // is destructive REPLACE — emits I6 `Extend 'v' replace …` directives, wiping
                // the verb's previous rules for each trigger word. The `replace` qualifier is
                // captured upstream into `memberIsReplace`.
                //
                // Priority is meaningless under replace; if the user also set a non-default
                // `priority = N;` in the same extend block, that's a contradiction.
                bool isReplace = memberIsReplace;
                if(isReplace && extendBlockPriority != verbPriorityDefault)
                    parsingError("`replace grammar = { ... }` cannot be combined with a non-default `priority = N;` in the same extend block — priority is meaningless under replace");
                file.getToken(token::assignment);
                vector<grammarLine> lines = parseGrammarLines();
                if(file.peekToken().is(token::endStatement)) file.getToken();
                string inferredVerb = vod->displayName.empty() ? vod->name : vod->displayName;

                // Find or create the verb's "grammar" rule-list. Extern verbs are emitted via
                // the globals-level grammarRuleListDecl (since emitVerbObject early-returns
                // for extern); non-extern verbs pick the same lines up via vod->grammarLines.
                grammarRuleListDecl* gtd = nullptr;
                for(typeMember* m : obj->members)
                    if(auto* g = dynamic_cast<grammarRuleListDecl*>(m))
                        if(g->name == "grammar"){ gtd = g; break; }
                if(!gtd){
                    gtd = new grammarRuleListDecl();
                    gtd->name = "grammar";
                    gtd->type = languageService.getType("grammarrulelist");
                    gtd->verbName = inferredVerb;
                    obj->members.push_back(gtd);
                    if(vod->isExternal) languageService.globals.push_back(gtd);
                }
                for(grammarLine& gl : lines){
                    gl.targetVerb    = inferredVerb;
                    gl.isOwnLine     = false;
                    gl.isReplaceMode = isReplace;
                    if(!isReplace){
                        // Append path: stamp the extend block's priority and create per-rule
                        // grammarRuleDecl tracking (mirrors what `+=` used to do).
                        gl.priority = extendBlockPriority;
                        grammarRuleDecl& rd = *(new grammarRuleDecl());
                        rd.name = "grammar";
                        rd.type = languageService.getType("grammarrule");
                        rd.line = gl;
                        rd.targetVerb = inferredVerb;
                        gtd->rules.push_back(&rd);
                    }
                    gtd->grammarLines.push_back(gl);
                    vod->grammarLines.push_back(gl);
                }
                if(isReplace) extendHadReplaceGrammar = true;
            } else {
                processInheritedMember(*obj, tok);
            }
        }
        else
            parsingError(format("Unexpected token '{0}' in extend body", tok.value));
        tok = file.getToken();
    }

    closeCompileContext(eCompileContext::objectDef);
    currentObject = savedObject;

    // For verb objects: link perform() and add grammar to globals if external
    if(vod){
        for(typeMember* m : vod->members)
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == "perform"){ vod->doFunc = fd; break; }
    }

    return false;
}

// Compound assignment (+= / -=) on an existing collection member inside an extend body.

void bglParser::processExtendCompoundAssignment(objectDef& obj, token memberName, const string& op, verbObjectDef* vod, int blockPriority){
    string memberNameStr = memberName.value;
    string display = memberName.originalValue.empty() ? memberName.value : memberName.originalValue;

    // Find the existing member on the object
    string memberType;
    typeMember* existingMember = nullptr;
    // Search object's own members
    for(typeMember* m : obj.members){
        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
            if(vd->name == memberNameStr){ memberType = vd->type.name; existingMember = m; break; }
    }
    // Search class hierarchy if not found on object
    if(memberType.empty()){
        function<void(classDef*)> searchClass = [&](classDef* cls){
            if(!cls || !memberType.empty()) return;
            for(typeMember* m : cls->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->name == memberNameStr){ memberType = vd->type.name; break; }
            for(classDef* base : cls->baseClasses) searchClass(base);
        };
        if(obj.objectClass) searchClass(obj.objectClass);
    }
    if(memberType.empty())
        parsingError(format("'{0}' is not a member of this object; cannot use {1}", display, op));

    // -= on extern objects is not allowed — we don't control their original data
    bool isExternalObj = obj.isExternal || (vod && vod->isExternal);
    if(isExternalObj && op == "-=")
        parsingError(format("Cannot use -= on extern object '{0}'; the original data is defined externally",
            obj.displayName.empty() ? obj.name : obj.displayName));

    // grammarRuleList += / -=
    if(memberType == "grammarrulelist"){
        if(op == "+="){
            // Parse grammar lines and append
            vector<grammarLine> lines = parseGrammarLines();
            string inferredVerb;
            if(vod) inferredVerb = vod->displayName.empty() ? vod->name : vod->displayName;

            // Find or create the grammarRuleListDecl on the object
            grammarRuleListDecl* gtd = nullptr;
            for(typeMember* m : obj.members)
                if(auto* g = dynamic_cast<grammarRuleListDecl*>(m))
                    if(g->name == memberNameStr){ gtd = g; break; }
            if(!gtd){
                gtd = new grammarRuleListDecl();
                gtd->name = memberNameStr;
                gtd->type = languageService.getType("grammarrulelist");
                gtd->verbName = inferredVerb;
                obj.members.push_back(gtd);
                if(vod && vod->isExternal) languageService.globals.push_back(gtd);
            }
            for(grammarLine& gl : lines){
                gl.priority = blockPriority;
                gl.isOwnLine = false;        // extend-block contribution, sorts against verb's anchor
                grammarRuleDecl& rd = *(new grammarRuleDecl());
                rd.name = memberNameStr;
                rd.type = languageService.getType("grammarrule");
                rd.line = gl;
                rd.targetVerb = inferredVerb;
                gtd->rules.push_back(&rd);
                gl.targetVerb = inferredVerb;
                gtd->grammarLines.push_back(gl);
            }
            if(vod)
                vod->grammarLines.insert(vod->grammarLines.end(), gtd->grammarLines.end() - lines.size(), gtd->grammarLines.end());
        } else {
            // -= : remove matching grammar lines
            vector<grammarLine> toRemove = parseGrammarLines();
            for(typeMember* m : obj.members){
                if(auto* g = dynamic_cast<grammarRuleListDecl*>(m)){
                    if(g->name != memberNameStr) continue;
                    for(const grammarLine& rem : toRemove){
                        g->grammarLines.erase(
                            remove_if(g->grammarLines.begin(), g->grammarLines.end(),
                                [&](const grammarLine& gl){ return gl.verbWord == rem.verbWord && gl.additionalVerbWords == rem.additionalVerbWords && gl.patternTokens == rem.patternTokens; }),
                            g->grammarLines.end());
                    }
                }
            }
            if(vod){
                for(const grammarLine& rem : toRemove){
                    vod->grammarLines.erase(
                        remove_if(vod->grammarLines.begin(), vod->grammarLines.end(),
                            [&](const grammarLine& gl){ return gl.verbWord == rem.verbWord && gl.patternTokens == rem.patternTokens; }),
                        vod->grammarLines.end());
                }
            }
        }
    }
    // attributeList: only `=` is supported. Use `attributes = {a, b, !c}` to
    // set/replace and `!attr` to negate an inherited attribute (maps directly to
    // I6's `has ~attr`). The legacy `+=`/`-=` forms were removed because they
    // didn't compose with extend/class semantics — see spec §… for the new model.
    else if(memberType == "attributelist"){
        parsingError(format(
            "'{0} {1}': '{2}' is an attributeList, which only accepts `= {{a, b, !c}}`. "
            "To negate an inherited attribute use prefix `!`, e.g. `attributes = {{light, !scenery}}`. "
            "For runtime changes use give()/ungive().",
            display, op, display));
    }
    // array<T> += / -=
    else if(memberType == "array"){
        file.getToken(token::braceOpen);
        initializerList* newElements = new initializerList();
        token t = file.getToken();
        while(!t.is(token::braceClose) && !t.is(eTokenType::eof)){
            expression* elem = parseExpression(t, {",", token::braceClose}, nullptr, nullptr);
            newElements->elements.push_back(elem);
            if(elem->terminator == token::braceClose) break;
            t = file.getToken();
        }
        // Find existing array member
        arrayDeclaration* arrMember = nullptr;
        for(typeMember* m : obj.members)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(m))
                if(ad->name == memberNameStr){ arrMember = ad; break; }
        if(op == "+="){
            if(arrMember){
                auto* list = dynamic_cast<initializerList*>(arrMember->declaredExpressionValue);
                if(!list){ list = new initializerList(); arrMember->declaredExpressionValue = list; }
                for(expression* e : newElements->elements)
                    list->elements.push_back(e);
            }
        } else {
            // -= : remove matching elements by text value
            if(arrMember){
                if(auto* list = dynamic_cast<initializerList*>(arrMember->declaredExpressionValue)){
                    for(expression* rem : newElements->elements){
                        string remText = rem->text();
                        list->elements.erase(
                            remove_if(list->elements.begin(), list->elements.end(),
                                [&](expression* e){ return e->text() == remText; }),
                            list->elements.end());
                    }
                }
            }
        }
    }
    else {
        parsingError(format("Type '{0}' does not support {1} in extend body", memberType, op));
    }

    if(file.peekToken().is(token::endStatement)) file.getToken();
}

//===============================================================================================================================
// Grammar declarations
//===============================================================================================================================
