// ===============================================================================
// bglParserTypes.cpp - type/name resolution + method/call binding.
//
// Extracted from bglParser.cpp (Phase 2 of the refactor). All entry points are
// member methods of bglParser; this file holds their definitions only.
//
// Type / name resolution (cluster E):
//   resolveIdentifierType       - bare identifier -> type lookup (lexical scope)
//   resolvePathType             - dotted path -> type, multi-hop hierarchy walk
//   qualifyIdentifier           - identifier -> I6-qualified emission form
//   resolveNamespacedType       - bgl.glulx.X-style type path resolution
//   isNamespacedTypePath        - non-consuming peek for namespace type paths
//   consumeTypeToken            - consume dot path, return flat type token
//   tryConsumeNamespacedEnumValue - bgl.glulx.winPlacement.above bnum access
//   resolveArrayElementType     - array<T>'s T from a variable name
//   resolveArrayElementTypeDotted - same for dotted owner.member
//   findArraySubscriptOp        - locate operator[] for a given element type
//   addCapture                  - register a lambda outer-scope capture
//   getDispatchClass            - resolve a type name to its dispatch classDef
//   typeHasMember               - cheap "does this type have a member named X"
//
// Method/call binding (cluster F):
//   resolveMethod               - method lookup on a type (with hierarchy walk)
//   resolveMethodWithConversion - same plus operator() conversion fallback
//   bindMethodCall              - method call binding + arg fixup + emission prep
//   parseCallArgList            - parse (arg, arg, named: arg, $"...")
//   resolveGlobalCall           - free function call lookup
//   validateGlobalCall          - report arity/type errors for global calls
//   bindGlobalCall              - unified resolve + validate + finalize
//   applyArgConversions         - per-arg operator() emission
//   finalizeCallArgs            - reorder named args + apply conversions
//   isTypeCompatible            - type compatibility check w/ fallbacks
//
// File-local statics:
//   substituteMethodForBindings - substitute T-> concrete on a generic method
//   formatSignature             - format function signature for error messages
//   reorderNamedArgsImpl        - shared named-arg reorder implementation
// ===============================================================================
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cctype>
#include <tuple>
#include <optional>
#include <string_view>
#include <unordered_map>

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
// File-local statics shared by the methods below
// ===============================================================================
static functionDef* substituteMethodForBindings(functionDef* fd, const unordered_map<string,string>& bindings){
    if(!fd || bindings.empty()) return fd;
    auto sub = [&](const string& t) -> string {
        // Direct match: the entire type name IS a type parameter (e.g., return type T).
        auto it = bindings.find(t);
        if(it != bindings.end()) return it->second;
        // Wrapped form: substitute each comma-separated piece inside the angle brackets
        // so `array<T>`, `func<bool, T>`, `func<var, T>`, etc. all rewrite when T is bound.
        size_t lt = t.find('<');
        if(lt != string::npos && !t.empty() && t.back() == '>'){
            string base = t.substr(0, lt);
            string body = t.substr(lt + 1, t.size() - lt - 2);
            string out;
            size_t i = 0;
            bool anyChange = false;
            while(i < body.size()){
                size_t comma = body.find(',', i);
                size_t end = (comma == string::npos) ? body.size() : comma;
                // Trim leading whitespace; keep trailing-whitespace preservation simple.
                size_t s = i;
                while(s < end && (body[s] == ' ' || body[s] == '\t')) s++;
                string piece = body.substr(s, end - s);
                auto pit = bindings.find(piece);
                if(pit != bindings.end()){ piece = pit->second; anyChange = true; }
                if(!out.empty()) out += ",";
                out += piece;
                if(comma == string::npos) break;
                i = comma + 1;
            }
            if(anyChange) return base + "<" + out + ">";
        }
        return t;
    };
    string newRet = sub(fd->returnType.name);
    bool anyChange = (newRet != fd->returnType.name);
    vector<string> newParamTypes;
    newParamTypes.reserve(fd->params.size());
    for(paramDef* p : fd->params){
        string nt = sub(p->type.name);
        if(nt != p->type.name) anyChange = true;
        newParamTypes.push_back(nt);
    }
    if(!anyChange) return fd;
    functionDef* clone = new functionDef();
    clone->name = fd->name;
    clone->displayName = fd->displayName;
    clone->src = fd->src;
    clone->isEmitter = fd->isEmitter;
    clone->isExplicit = fd->isExplicit;
    clone->isDefault = fd->isDefault;
    clone->isValueEmitter = fd->isValueEmitter;
    clone->returnType.name = newRet;
    clone->body = fd->body; // shared
    clone->cleanups = fd->cleanups;
    clone->replacedTarget = fd->replacedTarget;
    clone->replacedFunc = fd->replacedFunc;
    clone->captures = fd->captures;
    clone->isPrePassStub = fd->isPrePassStub;
    for(size_t i = 0; i < fd->params.size(); i++){
        paramDef* pc = new paramDef();
        pc->name = fd->params[i]->name;
        pc->displayName = fd->params[i]->displayName;
        pc->type.name = newParamTypes[i];
        pc->defaultValue = fd->params[i]->defaultValue;
        clone->params.push_back(pc);
    }
    return clone;
}


static string formatSignature(functionDef* fd){
    string sig = fd->dName() + "(";
    for(size_t i = 0; i < fd->params.size(); i++){
        if(i > 0) sig += ", ";
        sig += fd->params[i]->type.dName() + " " + fd->params[i]->dName();
        if(!fd->params[i]->defaultValue.empty())
            sig += " = " + fd->params[i]->defaultValue;
    }
    sig += ")";
    if(!fd->returnType.name.empty() && fd->returnType.name != "void")
        sig += " -> " + fd->returnType.name;
    return sig;
}

// ── Shared: parse a function call's argument list ────────────────────────────
// Assumes '(' has already been consumed.  Reads comma-separated expressions
// (including named arguments and interpolated strings) until ')'.

static bool reorderNamedArgsImpl(vector<expression*>& args, vector<string>& namedArgNames,
                                  vector<vector<interpolatedSegment>>& interpSegmentsPerArg,
                                  functionDef* fd, function<bool(string)> errorFn){
    bool hasNamed = false;
    for(auto& n : namedArgNames) if(!n.empty()){ hasNamed = true; break; }
    if(!hasNamed) return true;
    vector<expression*> reordered(fd->params.size(), nullptr);
    vector<vector<interpolatedSegment>> reorderedInterp(fd->params.size());
    size_t positionalIdx = 0;
    for(size_t i = 0; i < args.size(); i++){
        if(i < namedArgNames.size() && namedArgNames[i].empty()){
            while(positionalIdx < reordered.size() && reordered[positionalIdx] != nullptr) positionalIdx++;
            if(positionalIdx >= fd->params.size()) { errorFn("Too many positional arguments"); return false; }
            reordered[positionalIdx] = args[i];
            if(i < interpSegmentsPerArg.size()) reorderedInterp[positionalIdx] = interpSegmentsPerArg[i];
            positionalIdx++;
        }
    }
    for(size_t i = 0; i < args.size(); i++){
        if(i < namedArgNames.size() && !namedArgNames[i].empty()){
            bool found = false;
            for(size_t p = 0; p < fd->params.size(); p++){
                if(fd->params[p]->name == namedArgNames[i]){
                    if(reordered[p] != nullptr){ errorFn(format("Parameter '{0}' specified more than once", namedArgNames[i])); return false; }
                    reordered[p] = args[i];
                    if(i < interpSegmentsPerArg.size()) reorderedInterp[p] = interpSegmentsPerArg[i];
                    found = true; break;
                }
            }
            if(!found){ errorFn(format("No parameter named '{0}'", namedArgNames[i])); return false; }
        }
    }
    args.clear();
    interpSegmentsPerArg.clear();
    for(size_t i = 0; i < reordered.size(); i++){
        if(reordered[i] != nullptr){
            args.push_back(reordered[i]);
            interpSegmentsPerArg.push_back(reorderedInterp[i]);
        } else if(!fd->params[i]->defaultValue.empty()){
            expression* defExpr = new expression();
            defExpr->tokens.push_back(fd->params[i]->defaultValue);
            args.push_back(defExpr);
            interpSegmentsPerArg.push_back({});
        } else {
            errorFn(format("Required parameter '{0}' not provided", fd->params[i]->name)); return false;
        }
    }
    namedArgNames.clear();
    return true;
}

// Back-compat wrapper for statement-path callers that hold a functionCallStatement.

// ===============================================================================
// Method lookup and binding
// ===============================================================================
bglParser::MethodMatch bglParser::resolveMethod(const string& typeName, const string& objPath, const string& methodNameIn, const vector<expression*>& args, const string& elementType){
    MethodMatch result;
    functionDef* varFallback = nullptr;
    // Beguile is case-insensitive: stored names are canonical (lowercased), but the
    // methodName arriving here may be a display name (split from a qualified path like
    // "self.doStuff"). Normalize to canonical so direct string compares against fdIn->name
    // succeed regardless of source-side case.
    string methodName = methodNameIn;
    transform(methodName.begin(), methodName.end(), methodName.begin(), ::tolower);

    // Build type-parameter bindings if the receiver class is generic and the caller supplied
    // an element-type binding. Single-parameter case only — sufficient for array<T>; multi-
    // param classes (map<K,V> etc.) will need the parameter list extended.
    // Use getDispatchClass so templated receiver types (`array<int>` from a parametric param)
    // strip down to the generic class.
    classDef* cls = getDispatchClass(typeName);
    unordered_map<string,string> bindings;
    if(cls && !cls->typeParameters.empty() && !elementType.empty())
        bindings[cls->typeParameters[0]] = elementType;

    // Helper: check one function definition against the method name and args.
    // If type-parameter bindings apply, substitute on a clone before signature checking; the
    // caller's MethodMatch carries the substituted clone so downstream type queries (return
    // type, param types, isTypeCompatible, emission) all see concrete types.
    auto checkMethod = [&](functionDef* fdIn) {
        if(fdIn->name != methodName) return;
        functionDef* fd = substituteMethodForBindings(fdIn, bindings);
        if(!result.nameMatch) result.nameMatch = fd;
        result.nameFound = true;

        // Pre-scan stubs match by name only (params may be absent or partial); treat as arity-match too
        if(fd->isPrePassStub){ result.method = fd; if(!result.arityMatch) result.arityMatch = fd; return; }

        // Check arity (accounting for default params)
        size_t req = 0;
        for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
        if(args.size() < req || args.size() > fd->params.size()) return;
        if(!result.arityMatch) result.arityMatch = fd;

        // Check arg types
        bool argsOk = true, usesVar = false;
        for(size_t i = 0; i < args.size() && argsOk; i++){
            string argType = args[i]->resolvedType;
            string paramType = fd->params[i]->type.name;
            if(paramType == "var") usesVar = true;
            else if(!argType.empty() && !isTypeCompatible(argType, paramType)) argsOk = false;
        }
        if(!argsOk) return;
        if(usesVar){ if(!varFallback) varFallback = fd; }
        else if(!result.method) result.method = fd;
    };

    // Step 1: search class hierarchy
    if(cls != nullptr){
        std::function<void(classDef*)> searchHierarchy = [&](classDef* c){
            for(typeMember* m : c->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    checkMethod(fd);
            if(!result.method)
                for(classDef* base : c->baseClasses){ searchHierarchy(base); if(result.method) return; }
        };
        searchHierarchy(cls);
    }
    if(!result.method) result.method = varFallback;

    // Step 2: fallback — search objectDef's own members, then walk its objectClass hierarchy.
    // An objectDef's identity is itself, so its own member overrides win over inherited ones.
    // After local members, walk objectClass + baseClasses so inherited methods (give/has/provides
    // on `object`, plus any explicit class members) are reachable. Mirrors Step 1's hierarchy walk.
    // Three lookup strategies for finding the target objectDef:
    //   a) typeName itself resolves to an objectDef.
    //   b) objPath names a global objectDef (covers receivers like `_glulx.method()`).
    //   c) self / current-object short-circuits.
    if(!result.method){
        string lowerPath = objPath;
        transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
        objectDef* targetObj = nullptr;
        // (a) Type-name is itself an objectDef.
        if(auto* od = dynamic_cast<objectDef*>(&languageService.getType(typeName)))
            targetObj = od;
        // (c) self / "object" inside an object body
        else if(lowerPath == "self" && currentObject != nullptr)
            targetObj = currentObject;
        else if(typeName == "object" && currentObject != nullptr)
            targetObj = currentObject;
        // (b) objPath names a global objectDef
        else
            for(typeDef* g : languageService.globals)
                if(g->name == lowerPath)
                    if(auto* od = dynamic_cast<objectDef*>(g)){ targetObj = od; break; }
        if(targetObj != nullptr){
            varFallback = nullptr;
            // Local members first — overrides win over inherited.
            for(typeMember* m : targetObj->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    checkMethod(fd);
            // Then walk the objectClass hierarchy.
            if(!result.method && targetObj->objectClass){
                std::function<void(classDef*)> searchHierarchy = [&](classDef* c){
                    for(typeMember* m : c->members)
                        if(auto* fd = dynamic_cast<functionDef*>(m))
                            checkMethod(fd);
                    if(!result.method)
                        for(classDef* base : c->baseClasses){ searchHierarchy(base); if(result.method) return; }
                };
                searchHierarchy(targetObj->objectClass);
            }
            if(!result.method) result.method = varFallback;
        }
    }

    return result;
}

// Method resolution with non-explicit operator() conversion fallback. If resolveMethod fails to
// find a method on the requested type, walks that type's members for each non-explicit
// `operator()` emitter, retries resolveMethod on the converted return type, and returns the first
// successful match. On success, updates typeName in-place so the caller's type variable reflects
// the conversion target. On failure, returns the original (failed) match so the caller can use
// nameFound/nameMatch/arityMatch for error reporting.

bglParser::MethodMatch bglParser::resolveMethodWithConversion(string& typeName, const string& objPath, const string& methodName, const vector<expression*>& args, const string& elementType){
    MethodMatch mm = resolveMethod(typeName, objPath, methodName, args, elementType);
    if(mm.method) return mm;
    classDef* srcCls = dynamic_cast<classDef*>(&languageService.getType(typeName));
    if(!srcCls) return mm;
    for(typeMember* m : srcCls->members){
        auto* convOp = dynamic_cast<functionDef*>(m);
        if(!convOp || convOp->name != "operator()" || !convOp->isEmitter || convOp->isExplicit) continue;
        MethodMatch mm2 = resolveMethod(convOp->returnType.name, objPath, methodName, args, elementType);
        if(mm2.method){
            typeName = convOp->returnType.name;
            return mm2;
        }
    }
    return mm;
}


// ===============================================================================
// Type / name resolution
// ===============================================================================
bool bglParser::typeHasMember(const string& typeName, const string& memberName){
    if(typeName.empty() || memberName.empty()) return false;
    typeDef& td = languageService.getType(typeName);
    if(auto* cls = dynamic_cast<classDef*>(&td)){
        return findMemberInHierarchy(cls, [&](typeMember* m){ return m->name == memberName; }) != nullptr;
    }
    if(auto* obj = dynamic_cast<objectDef*>(&td)){
        for(typeMember* m : obj->members) if(m->name == memberName) return true;
        return false;
    }
    return false;
}


std::string bglParser::resolveIdentifierType(std::string name, functionDef* func, statementBlock* body, const string& memberHint){
    if(name == "null") return "object";
    if(name == "self" && lambdaOuterFunc == nullptr){
        // `self` inside an objectDef is the objectDef's own identity, regardless of base class.
        // resolveMethod walks `objectClass`'s hierarchy for inherited methods, so this works
        // whether the base is `object`, a user class, or anything else — no special-casing.
        if(currentObject != nullptr) return currentObject->name;
        if(currentClass != nullptr) return currentClass->name;
        return "object";
    }

    // ── LEXICAL SCOPE — first match wins absolutely ────────────────────────────
    // Locals, params, and members of the enclosing object/class form the lexical scope of
    // the current code. Inner scope shadows outer by design, so first match wins without
    // any ambiguity check. memberHint is NOT applied here: if a local exists with this
    // name, it's the receiver — let dispatch produce a "no method on type X" error if the
    // local's type doesn't have the member, rather than silently looking past the shadow.
    if(func != nullptr)
        for(paramDef* p : func->params)
            if(p->name == name) return p->type.name;
    if(body != nullptr)
        for(statement* s : body->statements)
            if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                if(vd->name == name) return vd->type.name;
    for(statementBlock* blk : activeBlockStack)
        if(blk != nullptr && blk != body)
            for(statement* s : blk->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name) return vd->type.name;
    if(currentObject != nullptr){
        for(typeMember* m : currentObject->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == name) return vd->type.name;
        // Walk the object's class hierarchy so inherited members (e.g. `attributeList
        // attributes` from the base `object` class) resolve via bare-name reference
        // inside the object's method bodies — mirrors the currentClass branch below.
        // Without this, bare `attributes` inside a `room` method body falls past
        // lexical scope and hits the property-name fallback, typing as `property`
        // and producing "no method 'X' on type 'property'" downstream.
        function<string(classDef*)> findVarInBases = [&](classDef* c) -> string {
            if(!c) return "";
            for(typeMember* m : c->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->name == name) return vd->type.name;
            for(classDef* base : c->baseClasses){
                string t = findVarInBases(base);
                if(!t.empty()) return t;
            }
            return "";
        };
        string t = findVarInBases(currentObject->objectClass);
        if(!t.empty()) return t;
    }
    if(currentClass != nullptr){
        for(typeMember* m : currentClass->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == name) return vd->type.name;
        function<string(classDef*)> findVarInBases = [&](classDef* c) -> string {
            for(typeMember* m : c->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->name == name) return vd->type.name;
            for(classDef* base : c->baseClasses){
                string t = findVarInBases(base);
                if(!t.empty()) return t;
            }
            return "";
        };
        for(classDef* base : currentClass->baseClasses){
            string t = findVarInBases(base);
            if(!t.empty()) return t;
        }
    }

    // Closure capture: check outer function's scope for type. This is a lexical-scope walk
    // (the lambda's enclosing function), so it runs BEFORE file-scope candidates — standard
    // scoping rule that captures shadow same-named globals.
    if(lambdaOuterFunc != nullptr){
        for(paramDef* p : lambdaOuterFunc->params)
            if(p->name == name) return p->type.name;
        if(lambdaOuterBody != nullptr)
            for(statement* s : lambdaOuterBody->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name) return vd->type.name;
        statementBlock* rootBody = dynamic_cast<statementBlock*>(lambdaOuterFunc->body);
        if(rootBody != nullptr && rootBody != lambdaOuterBody){
            function<string(statementBlock*)> findInBlock = [&](statementBlock* blk) -> string {
                if(!blk) return "";
                for(statement* s : blk->statements){
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == name) return vd->type.name;
                    if(auto* ifs = dynamic_cast<ifStatement*>(s)){
                        string t = findInBlock(ifs->thenBlock); if(!t.empty()) return t;
                        t = findInBlock(ifs->elseBlock); if(!t.empty()) return t;
                    } else if(auto* fors = dynamic_cast<forStatement*>(s)){
                        string t = findInBlock(fors->body); if(!t.empty()) return t;
                    } else if(auto* fis = dynamic_cast<forInStatement*>(s)){
                        string t = findInBlock(fis->body); if(!t.empty()) return t;
                    } else if(auto* ws = dynamic_cast<whileStatement*>(s)){
                        string t = findInBlock(ws->body); if(!t.empty()) return t;
                    } else if(auto* ds = dynamic_cast<doStatement*>(s)){
                        string t = findInBlock(ds->body); if(!t.empty()) return t;
                    } else if(auto* tc = dynamic_cast<tryCatchStatement*>(s)){
                        string t = findInBlock(tc->tryBody); if(!t.empty()) return t;
                        t = findInBlock(tc->catchBody); if(!t.empty()) return t;
                    }
                }
                return "";
            };
            string t = findInBlock(rootBody);
            if(!t.empty()) return t;
        }
        // Capture chaining: walk the full lambda nesting stack
        for(int si = (int)lambdaOuterFuncStack.size() - 1; si >= 0; si--){
            functionDef* ancestor = lambdaOuterFuncStack[si];
            if(ancestor == lambdaOuterFunc) continue;
            for(paramDef* p : ancestor->params)
                if(p->name == name) return p->type.name;
            statementBlock* ancestorBody = dynamic_cast<statementBlock*>(ancestor->body);
            if(ancestorBody != nullptr)
                for(statement* s : ancestorBody->statements)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == name) return vd->type.name;
            for(auto& cap : ancestor->captures)
                if(cap.outerName == name) return cap.typeName;
        }
        // `self` type resolution for capture
        if(name == "self" && (currentClass != nullptr || currentObject != nullptr))
            return currentClass ? currentClass->name : "object";
    }

    // ── FILE SCOPE — collect all matches, then apply ambiguity rule ───────────
    // Enum values, globals, verbs, emitter objects, #defines, and #using imports all live
    // at file/module scope. They share one conceptual namespace — collisions there are
    // accidents, not deliberate shadowing. Collect every match and let the ambiguity logic
    // below decide.
    struct Candidate { string type; string origin; bool isEnum; bool isObject = false; };
    vector<Candidate> candidates;

    // Enum values: walk EVERY enum that contains this value name. Multiple enums may share
    // a value name (e.g. eColor.Red and eFlavor.Red); each is a distinct candidate that the
    // expected-type tie-breaker can disambiguate.
    for(typeDef* t : languageService.objectTypes){
        auto* ed = dynamic_cast<enumDef*>(t);
        if(!ed) continue;
        for(enumValueDef* v : ed->namedValues)
            if(v->name == name){
                candidates.push_back({ed->name, format("enum value of '{0}'", ed->name), true});
                break;
            }
    }
    // Globals (variable, function, objectDef, classDef, enumDef)
    for(typeDef* g : languageService.globals){
        if(g->name == name){
            string ct, origin;
            bool isObj = false;
            if(auto* vd = dynamic_cast<variableDeclaration*>(g)){ ct = vd->type.name; origin = format("global variable '{0}'", g->name); }
            else if(auto* fd = dynamic_cast<functionDef*>(g)){ ct = fd->returnType.name; origin = format("global function '{0}'", g->name); }
            else if(auto* od = dynamic_cast<objectDef*>(g)){
                isObj = true;
                // Type-identity rule: an objectDef IS its own type unless it explicitly
                // inherits from a non-`object` class. The implicit `object` parent doesn't
                // count — it's the universal supertype, not a specific identity.
                // (TODO: this `name != "object"` check is asymmetric and undermines the
                // "object is not special" principle. Removing it requires teaching the ~80
                // dynamic_cast<classDef*>(getType(...)) operator-dispatch sites to also walk
                // an objectDef's objectClass — out of scope for now; resolveMethod already
                // does this so unclassed `object Foo{}` reaches inherited methods.)
                bool explicitNonObjectClass = od->objectClass && od->objectClass->name != "object";
                ct = explicitNonObjectClass ? od->objectClass->name : od->name;
                origin = format("global object '{0}'", g->name);
            }
            else if(dynamic_cast<classDef*>(g) || dynamic_cast<enumDef*>(g)){
                // Class/enum type-name reference (e.g. ClassName.staticMember). The type
                // identifies itself so dot-path code can locate the member.
                ct = name;
                origin = format("type '{0}'", g->name);
            }
            if(!ct.empty()) candidates.push_back({ct, origin, false, isObj});
            break;
        }
    }
    // Action constants (extern verb Take etc.)
    for(verbObjectDef* vd : languageService.verbs){
        string lower = vd->name;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if(lower == name){
            candidates.push_back({"verb", format("verb '{0}'", vd->name), false, /*isObject=*/true});
            break;
        }
    }
    // Global emitter objects
    {
        typeDef& td = languageService.getType(name);
        if(auto* cd = dynamic_cast<classDef*>(&td))
            if(cd->isGlobalEmitterObject || cd->isEmitterClass)
                candidates.push_back({name, format("emitter object '{0}'", name), false});
    }
    // #define symbols
    {
        auto it = definedSymbols.find(name);
        if(it != definedSymbols.end() && !it->second.empty()){
            bool isNumeric = !it->second.empty() && (isdigit(it->second[0]) || (it->second[0] == '-' && it->second.size() > 1));
            string ct = isNumeric ? "intliteral" : "stringliteral";
            candidates.push_back({ct, format("#define symbol '{0}'", name), false});
        }
    }
    // #using class imports
    for(classDef* imp : usingImports){
        bool added = false;
        for(typeMember* m : imp->members){
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == name){ candidates.push_back({fd->returnType.name, format("#using-imported method '{0}.{1}'", imp->dName(), name), false}); added = true; break; }
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == name){ candidates.push_back({vd->type.name, format("#using-imported variable '{0}.{1}'", imp->dName(), name), false}); added = true; break; }
        }
        if(added) continue;
    }
    // #using object imports
    for(objectDef* imp : usingObjectImports){
        bool added = false;
        for(typeMember* m : imp->members){
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == name){ candidates.push_back({fd->returnType.name, format("#using-imported method '{0}.{1}'", imp->name, name), false}); added = true; break; }
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == name){ candidates.push_back({vd->type.name, format("#using-imported member '{0}.{1}'", imp->name, name), false}); added = true; break; }
        }
        if(added) continue;
    }

    // Dedupe: candidates resolving to the same type are the same thing seen via multiple lookup
    // paths (e.g. a verb registered both as a verbObjectDef and as a global object stub).
    {
        vector<Candidate> deduped;
        for(auto& c : candidates){
            bool dup = false;
            for(auto& d : deduped) if(d.type == c.type){ dup = true; break; }
            if(!dup) deduped.push_back(c);
        }
        candidates = deduped;
    }
    // Globals beat enum values: when a non-enum candidate exists alongside enum-value
    // candidates, drop the enum candidates. Lets a global like `extern object noun` win
    // over an enum value `grammarToken.noun` for bare `noun` references; the enum value
    // is still reachable via `grammarToken.noun` qualified access.
    {
        bool hasNonEnum = false;
        for(auto& c : candidates) if(!c.isEnum){ hasNonEnum = true; break; }
        if(hasNonEnum){
            vector<Candidate> filtered;
            for(auto& c : candidates) if(!c.isEnum) filtered.push_back(c);
            candidates = filtered;
        }
    }
    // Resolve the candidate set. Cases:
    //   • 1 candidate         → use it (let dispatch produce type-mismatch errors if any).
    //   • 2+ candidates       → try memberHint, expected-type, then non-verb-beats-verb.
    //                           If a single candidate emerges, use it; otherwise error.
    if(candidates.size() == 1) return candidates[0].type;
    if(candidates.size() >= 2){
        if(!memberHint.empty()){
            vector<Candidate*> satisfying;
            for(auto& c : candidates)
                if(typeHasMember(c.type, memberHint)) satisfying.push_back(&c);
            if(satisfying.size() == 1) return satisfying[0]->type;
            // 0 or 2+ satisfy — fall through to expected-type filter, then ambiguity error.
        }
        if(!currentExpectedType.empty()){
            vector<Candidate*> compatible;
            for(auto& c : candidates)
                if(isTypeCompatible(c.type, currentExpectedType)) compatible.push_back(&c);
            if(compatible.size() == 1) return compatible[0]->type;
        }
        // Non-object beats object as a last-resort tiebreaker. Objects (and verbs,
        // which are objects via `alias class verb for object`) are reachable in their
        // natural usage contexts: `action == V` (expected-type filter above), or
        // `V.method()` (memberHint filter above). A bare reference like `score++`
        // overwhelmingly means the variable. Mirrors the non-enum-beats-enum precedent.
        {
            bool hasNonObject = false;
            for(auto& c : candidates) if(!c.isObject){ hasNonObject = true; break; }
            if(hasNonObject){
                vector<Candidate> filtered;
                for(auto& c : candidates) if(!c.isObject) filtered.push_back(c);
                if(filtered.size() == 1) return filtered[0].type;
                candidates = filtered;  // narrowed but still ambiguous — fall through to error
            }
        }
        string msg = format("'{0}' is ambiguous: matches ", name);
        for(size_t i = 0; i < candidates.size(); i++){
            if(i > 0) msg += (i == candidates.size() - 1 ? " and " : ", ");
            msg += candidates[i].origin;
        }
        msg += ". Qualify the use explicitly to disambiguate.";
        parsingError(msg);
    }

    // Loose mode: see qualifyIdentifier for rationale. Unknown identifiers inside #bgl{}
    // blocks are typed `var` so the type checker passes them through.
    if(looseIdentifierMode) return "var";
    // Property-name fallback: a bare identifier that matches any class/object member or
    // free-standing `property foo;` decl resolves to type `property`. Lets `obj.provides(name)`
    // accept names that aren't otherwise in scope, including class members.
    if(languageService.isKnownPropertyName(name)) return "property";
    // Class-name fallback: a bare identifier that matches a registered class resolves to
    // type `bglclass`. Picks up `extern class Foo;` (which never enters `globals`) so
    // `obj.is(Foo)` accepts bare class names. Non-extern classes resolve earlier as their
    // own type and reach `bglclass` via the isTypeCompatible rule.
    if(languageService.isKnownClassName(name)) return "bglclass";
    return "";
}

// True iff `name` resolves as an inherited variableDeclaration member of currentObject's class
// hierarchy AND is NOT shadowed by a local, param, or own member. Used to decide whether bare
// identifiers in method-call contexts should map their `$self` substitution to `self` (the
// implicit owner) rather than to the bare receiver name. Mirrors the lexical-scope walk in
// resolveIdentifierType but returns just yes/no.
bool bglParser::isInheritedObjectMember(const string& name, functionDef* func, statementBlock* body){
    if(currentObject == nullptr || currentObject->objectClass == nullptr) return false;
    // Shadowing checks first — locals/params/own members take precedence over inheritance.
    if(func != nullptr)
        for(paramDef* p : func->params)
            if(p->name == name) return false;
    if(body != nullptr)
        for(statement* s : body->statements)
            if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                if(vd->name == name) return false;
    for(statementBlock* blk : activeBlockStack)
        if(blk != nullptr && blk != body)
            for(statement* s : blk->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name) return false;
    for(typeMember* m : currentObject->members)
        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
            if(vd->name == name) return false;
    // Now walk the class hierarchy.
    function<bool(classDef*)> walk = [&](classDef* c) -> bool {
        if(!c) return false;
        for(typeMember* m : c->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == name) return true;
        for(classDef* base : c->baseClasses)
            if(walk(base)) return true;
        return false;
    };
    return walk(currentObject->objectClass);
}

// Return the declared element type of an array variable, or "" if `name` isn't an arrayDeclaration
// in any reachable scope. Walks the same scopes as resolveIdentifierType but looks specifically
// for arrayDeclaration subclass instances so the elementType field can be read.

string bglParser::resolveArrayElementType(const string& name, functionDef* func, statementBlock* body){
    if(func != nullptr)
        for(paramDef* p : func->params)
            if(p->name == name) {
                // Parameter type is stored as "array<T>" for typed array params
                const string& tn = p->type.name;
                if(tn.size() > 6 && tn.substr(0,6) == "array<" && tn.back() == '>')
                    return tn.substr(6, tn.size() - 7);
                if(tn == "bytearray") return "char";
                return "";
            }
    if(body != nullptr)
        for(statement* s : body->statements)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(s))
                if(ad->name == name) return ad->elementType;
    if(currentFunc != nullptr && currentFunc->body != nullptr){
        statementBlock* outerBody = dynamic_cast<statementBlock*>(currentFunc->body);
        if(outerBody != nullptr && outerBody != body)
            for(statement* s : outerBody->statements)
                if(auto* ad = dynamic_cast<arrayDeclaration*>(s))
                    if(ad->name == name) return ad->elementType;
    }
    if(currentObject != nullptr)
        for(typeMember* m : currentObject->members)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(m))
                if(ad->name == name) return ad->elementType;
    if(currentClass != nullptr)
        for(typeMember* m : currentClass->members)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(m))
                if(ad->name == name) return ad->elementType;
    for(typeDef* g : languageService.globals)
        if(auto* ad = dynamic_cast<arrayDeclaration*>(g))
            if(ad->name == name) return ad->elementType;
    return "";
}

// Resolve the element type of `objName.propName` where propName is an array member of obj's class.

string bglParser::resolveArrayElementTypeDotted(const string& objName, const string& propName,
                                                  functionDef* func, statementBlock* body){
    // Find objName's class — walk objectInstances + globals
    classDef* cls = nullptr;
    for(typeDef* t : languageService.objectInstances)
        if(auto* od = dynamic_cast<objectDef*>(t))
            if(od->name == objName) {
                for(typeMember* m : od->members)
                    if(auto* ad = dynamic_cast<arrayDeclaration*>(m))
                        if(ad->name == propName) return ad->elementType;
                cls = od->objectClass;
                break;
            }
    if(!cls) {
        string objType = resolveIdentifierType(objName, func, body);
        cls = dynamic_cast<classDef*>(&languageService.getType(objType));
    }
    function<string(classDef*)> walk = [&](classDef* c) -> string {
        if(!c) return "";
        for(typeMember* m : c->members)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(m))
                if(ad->name == propName) return ad->elementType;
        for(classDef* base : c->baseClasses) { string r = walk(base); if(!r.empty()) return r; }
        return "";
    };
    return walk(cls);
}

// Find operator[] or operator[]= on an array class hierarchy matching a specific element type.
// For read (isWrite=false), matches the operator's return type against elemType.
// For write (isWrite=true), matches the operator's second parameter type against elemType.
//
// When the receiving class declares a type parameter (e.g. `class array<T>`), the search applies
// type-parameter→element substitution so `T operator[](int)` resolves as if it were
// `<elemType> operator[](int)`. Subclasses that override with a concrete element type
// (e.g. `byteArray.operator[](int) → char`) win via Pass 1's exact match.

functionDef* bglParser::findArraySubscriptOp(classDef* arrCls, const string& elemType, bool isWrite){
    if(!arrCls) return nullptr;
    const string opName = isWrite ? "[]=" : "[]";
    const size_t expectedParams = isWrite ? 2u : 1u;
    auto matches = [&](functionDef* fd, const string& t) {
        if(!fd || fd->name != opName || fd->params.size() != expectedParams) return false;
        if(isWrite) return fd->params[1]->type.name == t;
        return fd->returnType.name == t;
    };
    // Walk the hierarchy. For each class along the way, build bindings if it declares a type
    // parameter (single-param case for now — array<T>), and apply substitution before matching.
    functionDef* found = nullptr;
    function<void(classDef*)> search = [&](classDef* c) {
        if(!c || found) return;
        unordered_map<string,string> bindings;
        if(!c->typeParameters.empty() && !elemType.empty())
            bindings[c->typeParameters[0]] = elemType;
        for(typeMember* m : c->members)
            if(auto* fd = dynamic_cast<functionDef*>(m)){
                functionDef* effective = substituteMethodForBindings(fd, bindings);
                if(matches(effective, elemType)) { found = effective; return; }
            }
        for(classDef* base : c->baseClasses) { if(found) return; search(base); }
    };
    search(arrCls);
    return found;
}

// Returns the I6-qualified form of an identifier based on scope:
//   - found in local params/vars → name (unqualified)
//   - found in current object's members → "self.name"
//   - found in enum values or globals → name (unqualified)
//   - not found → "" (caller should report an error)

// Check if a variable is declared const (local, global, or class member).

// ===============================================================================
// Argument parsing and global-call binding
// ===============================================================================
bglParser::ParsedArgList bglParser::parseCallArgList(functionDef* func, statementBlock* body){
    ParsedArgList result;
    // Function arguments are a fresh expression context — clear the outer expected-type so it
    // doesn't bleed into arg resolution. (Per-arg expected types from the callee's signature
    // would be ideal but require flipping overload-resolution order; deferred for now.)
    string savedExpectedArgs = currentExpectedType;
    currentExpectedType = "";
    token firstArgTok = file.getToken();
    while(firstArgTok.isNot(token::parenClose)){
        // Named argument detection: identifier followed by ':'
        string namedArgName;
        if(firstArgTok.is(eTokenType::name) && file.peekToken(1).is(":")){
            namedArgName = firstArgTok.value;
            file.getToken(); // consume ':'
            firstArgTok = file.getToken(); // read the value expression's first token
        }
        if(firstArgTok.is("$") && file.peekToken(1).is(eTokenType::quote)){
            // Interpolated string literal argument: func($"...")
            vector<interpolatedSegment> segs = parseInterpolatedSegments(func, body);
            expression* arg = new expression();
            arg->resolvedType = "interpolatedstringliteral";
            token sep = file.getToken();
            arg->terminator = sep.value;
            result.args.push_back(arg);
            result.namedArgNames.push_back(namedArgName);
            while(result.interpSegmentsPerArg.size() < result.args.size() - 1)
                result.interpSegmentsPerArg.push_back({});
            result.interpSegmentsPerArg.push_back(segs);
            if(sep.is(token::parenClose)) break;
            firstArgTok = file.getToken();
        } else {
            expression* arg = parseExpression(firstArgTok, {token::comma, token::parenClose}, func, body);
            result.args.push_back(arg);
            result.namedArgNames.push_back(namedArgName);
            if(arg->terminator == token::parenClose) { currentExpectedType = savedExpectedArgs; return result; }
            firstArgTok = file.getToken();
        }
    }
    currentExpectedType = savedExpectedArgs;
    return result;
}

// ── Shared: resolve a global function call ───────────────────────────────────
// Given a function name and parsed argument expressions, finds the best matching
// global function definition.  Priority: exact type > conversion > var fallback.

bglParser::GlobalCallMatch bglParser::resolveGlobalCall(const string& name, const vector<expression*>& args,
                                                         functionDef* func, statementBlock* body){
    GlobalCallMatch result;
    functionDef* conversionMatch = nullptr;
    functionDef* varFallback = nullptr;
    for(typeDef* g : languageService.globals){
        if(auto* fd = dynamic_cast<functionDef*>(g)){
            if(fd->name != name) continue;
            if(result.nameMatch == nullptr) result.nameMatch = fd;
            size_t req = 0;
            for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
            if(args.size() >= req && args.size() <= fd->params.size()){
                if(result.arityMatch == nullptr) result.arityMatch = fd;
                bool argsOk = true;
                bool usesVar = false;
                bool needsConversion = false;
                for(size_t i = 0; i < args.size() && argsOk; i++){
                    string argType = args[i]->resolvedType;
                    string paramType = fd->params[i]->type.name;
                    if(paramType == "var") usesVar = true;
                    else if(argType.empty() || argType == paramType) {} // exact or unknown
                    else if(isTypeCompatible(argType, paramType)) needsConversion = true;
                    else argsOk = false;
                }
                if(argsOk){
                    if(usesVar){ if(varFallback == nullptr) varFallback = fd; }
                    else if(!needsConversion){ result.match = fd; return result; }
                    else if(conversionMatch == nullptr) conversionMatch = fd;
                }
            }
        }
    }
    if(result.match == nullptr) result.match = conversionMatch;
    if(result.match == nullptr) result.match = varFallback;
    // If no global found, check #using imports for a method with the matching name.
    // This lets `#using bgl.asm` make `get_prop_len(x)` work as a bare call.
    auto checkImportMember = [&](typeMember* m){
        auto* fd = dynamic_cast<functionDef*>(m);
        if(!fd || fd->name != name) return;
        if(result.nameMatch == nullptr) result.nameMatch = fd;
        size_t req = 0;
        for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
        if(args.size() >= req && args.size() <= fd->params.size()){
            if(result.arityMatch == nullptr) result.arityMatch = fd;
            if(result.match == nullptr) result.match = fd;
        }
    };
    if(result.match == nullptr){
        for(classDef* imp : usingImports)
            for(typeMember* m : imp->members) checkImportMember(m);
    }
    if(result.match == nullptr){
        for(objectDef* imp : usingObjectImports)
            for(typeMember* m : imp->members) checkImportMember(m);
    }
    // If no global found, check for func<> variable
    if(result.nameMatch == nullptr){
        string varType = resolveIdentifierType(name, func, body);
        if(varType.rfind("func<", 0) == 0){
            size_t lt = varType.find('<');
            size_t sep = varType.find(',', lt);
            size_t gt = varType.rfind('>');
            result.funcVarReturnType = (sep != string::npos && sep < gt)
                ? varType.substr(lt+1, sep-lt-1)
                : varType.substr(lt+1, gt-lt-1);
        }
    }
    return result;
}

// ── Shared: validate a global function call and report arity/type errors ─────
// Called after resolveGlobalCall; issues parsingError if validation fails.
// Returns the resolved function's return type name.

string bglParser::validateGlobalCall(GlobalCallMatch& gcm, const string& funcName, size_t argCount){
    if(!gcm.funcVarReturnType.empty()) return gcm.funcVarReturnType;
    if(gcm.nameMatch == nullptr){
        // Loose mode (#bgl{} content): unresolved function names are assumed to be I6
        // routines defined in the surrounding stream. Return "var" as the inferred type;
        // emission falls through to a literal `name(args)` call which I6 will resolve.
        if(looseIdentifierMode) return "var";
        parsingError(format("Undeclared function '{0}'", funcName));
    }
    string dispName = gcm.nameMatch ? gcm.nameMatch->dName() : funcName;
    if(gcm.arityMatch == nullptr){
        size_t req = 0; for(paramDef* p : gcm.nameMatch->params) if(p->defaultValue.empty()) req++;
        size_t tot = gcm.nameMatch->params.size();
        parsingError(format("Function '{0}' expects {1} argument(s), but {2} were supplied.\n  Expected: {3}",
            dispName,
            (req == tot) ? to_string(tot) : to_string(req) + "-" + to_string(tot),
            argCount, formatSignature(gcm.nameMatch)));
    }
    if(gcm.match == nullptr){
        // Loose mode (#bgl{}): bind to nameMatch when arity matches but types don't —
        // arguments pass through to I6 unchecked. See spec §15.6.
        if(looseIdentifierMode && gcm.nameMatch != nullptr){
            gcm.match = gcm.nameMatch;
            return gcm.match->returnType.name;
        }
        parsingError(format("No overload of function '{0}' accepts these argument types", dispName));
    }
    return gcm.match->returnType.name;
}


// ===============================================================================
// Identifier qualification + small lookup utilities
// ===============================================================================
std::string bglParser::qualifyIdentifier(std::string name, functionDef* func, statementBlock* body, const string& memberHint){
    if(name == "null") return "nothing";
    if(name == "self" && lambdaOuterFunc == nullptr) return "self";
    // Handle dot-path: qualify the head, then check for value emitter on the tail
    size_t dot = name.find('.');
    if(dot != string::npos){
        string head = name.substr(0, dot);
        string tail = name.substr(dot + 1);
        string qualifiedHead = qualifyIdentifier(head, func, body);
        if(qualifiedHead.empty()) return "";
        // Check if head is a class/emitter-namespace with a value emitter or alias member
        string lowerTail = tail;
        transform(lowerTail.begin(), lowerTail.end(), lowerTail.begin(), ::tolower);
        // Split tail at first dot for multi-level paths: bgl.strings.init → head=bgl, firstSeg=strings, rest=init
        string firstSeg = lowerTail;
        string rest;
        size_t tailDot = lowerTail.find('.');
        if(tailDot != string::npos){
            firstSeg = lowerTail.substr(0, tailDot);
            rest = lowerTail.substr(tailDot + 1);
        }
        classDef* cls = dynamic_cast<classDef*>(&languageService.getType(head));
        if(cls){
            for(typeMember* m : cls->members){
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == firstSeg && fd->isValueEmitter && fd->isEmitter && rest.empty())
                        if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                            string b = processBglConditionals(blk->i6Body);
                            b = i6Emitter::replaceWord(b, "$self", qualifiedHead);
                            b = i6Emitter::replaceWord(b, "$val",  qualifiedHead);
                            size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                            size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                            return b;
                        }
                // Alias member: resolve through the alias type for the remaining path
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->name == firstSeg && !rest.empty()){
                        // Recurse: qualify "aliasType.rest"
                        return qualifyIdentifier(vd->type.name + "." + rest, func, body);
                    }
            }
        }
        return qualifiedHead + "." + tail;
    }
    // Tier 1a: params of current (possibly nested) context. byVal-class params have
    // a synthesized backing (isClassParamWithBacking); substitute the backing's i6name
    // so source references resolve to the local copy instead of the bare routine local
    // (which still holds the caller's object reference for the copy-in to read).
    if(func != nullptr)
        for(paramDef* p : func->params)
            if(p->name == name) return p->isClassParamWithBacking && !p->i6name.empty() ? p->i6name : name;
    // Tier 1b: locals in current block. For class-typed locals with synthesized static
    // backing (isClassLocalWithBacking), substitute the i6name so source references like
    // `w.width` emit against the global backing object instead of the bare-int local slot.
    if(body != nullptr)
        for(statement* s : body->statements)
            if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                if(vd->name == name) return vd->isClassLocalWithBacking && !vd->i6name.empty() ? vd->i6name : name;
    // Tier 1c: locals in ancestor blocks (when inside nested if/for/while/etc.).
    // Walks the activeBlockStack — each block was pushed when its compile context opened.
    // This handles the case where the AST is still being built (parent statements haven't
    // been pushed to their body yet) by checking each in-progress block directly.
    for(statementBlock* blk : activeBlockStack)
        if(blk != nullptr && blk != body)
            for(statement* s : blk->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name) return vd->isClassLocalWithBacking && !vd->i6name.empty() ? vd->i6name : name;
    // Tier 2: current object/class members (variables and methods) → qualify with self
    // Walks the full class hierarchy (depth-first through baseClasses) so inherited members
    // from multiple bases are reachable as bare identifiers inside method bodies.
    // Uses the member's displayName (via dName()) so user case is preserved in I6 emission.
    if(currentObject != nullptr)
        for(typeMember* m : currentObject->members)
            if(m->name == name)
                if(dynamic_cast<variableDeclaration*>(m) || dynamic_cast<functionDef*>(m))
                    return "self." + m->dName();
    if(currentClass != nullptr){
        // Direct members: match both variables and functions (same as before).
        // Static variables resolve to their mangled global name, not `self.name` — they
        // live outside any instance's property table.
        for(typeMember* m : currentClass->members)
            if(m->name == name)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                    if(vd->isStatic) return "_bgl_" + currentClass->dName() + "_" + vd->dName();
                    return "self." + vd->dName();
                }
                else if(dynamic_cast<functionDef*>(m))
                    return "self." + m->dName();
        // Base class hierarchy: only match VARIABLES, not functions. Function/method resolution
        // has its own hierarchy-aware path (resolveMethod) that checks arity and types correctly.
        // Walking functions here would shadow global functions of the same name (e.g. `print`
        // on _bglObject shadowing the global `print(string)` emitter).
        // Returns the member's displayName when found in a base.
        std::function<string(classDef*)> findVarDisplayInBases = [&](classDef* c) -> string {
            for(typeMember* m : c->members)
                if(m->name == name)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m)) return vd->dName();
            for(classDef* base : c->baseClasses){
                string r = findVarDisplayInBases(base);
                if(!r.empty()) return r;
            }
            return "";
        };
        for(classDef* base : currentClass->baseClasses){
            string disp = findVarDisplayInBases(base);
            if(!disp.empty()) return "self." + disp;
        }
    }

    // Tier 7 (hoisted): closure capture — inside a lambda, check the outer function's scope
    // BEFORE file-scope candidates. Captures shadow same-named globals, matching standard
    // lexical-scoping semantics in any closure-bearing language.
    if(lambdaOuterFunc != nullptr && currentFunc != nullptr){
        // Check outer function's params
        for(paramDef* p : lambdaOuterFunc->params)
            if(p->name == name)
                return addCapture(name, p->type.name);
        // Check immediate enclosing body (e.g. for-loop body)
        if(lambdaOuterBody != nullptr)
            for(statement* s : lambdaOuterBody->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name)
                        return addCapture(name, vd->type.name);
        // Check function root body recursively — covers locals declared before/outside the
        // enclosing block (e.g. `int base = 10;` declared before a for-loop).
        statementBlock* rootBody = dynamic_cast<statementBlock*>(lambdaOuterFunc->body);
        if(rootBody != nullptr && rootBody != lambdaOuterBody){
            function<variableDeclaration*(statementBlock*)> findInBlock = [&](statementBlock* blk) -> variableDeclaration* {
                if(!blk) return nullptr;
                for(statement* s : blk->statements){
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == name) return vd;
                    if(auto* ifs = dynamic_cast<ifStatement*>(s)){
                        if(auto* r = findInBlock(ifs->thenBlock)) return r;
                        if(auto* r = findInBlock(ifs->elseBlock)) return r;
                    } else if(auto* fors = dynamic_cast<forStatement*>(s)){
                        if(auto* r = findInBlock(fors->body)) return r;
                    } else if(auto* fis = dynamic_cast<forInStatement*>(s)){
                        if(auto* r = findInBlock(fis->body)) return r;
                    } else if(auto* ws = dynamic_cast<whileStatement*>(s)){
                        if(auto* r = findInBlock(ws->body)) return r;
                    } else if(auto* ds = dynamic_cast<doStatement*>(s)){
                        if(auto* r = findInBlock(ds->body)) return r;
                    } else if(auto* tc = dynamic_cast<tryCatchStatement*>(s)){
                        if(auto* r = findInBlock(tc->tryBody)) return r;
                        if(auto* r = findInBlock(tc->catchBody)) return r;
                    }
                }
                return nullptr;
            };
            if(auto* vd = findInBlock(rootBody))
                return addCapture(name, vd->type.name);
        }
        // Capture chaining: walk the full lambda nesting stack.
        for(int si = (int)lambdaOuterFuncStack.size() - 1; si >= 0; si--){
            functionDef* ancestor = lambdaOuterFuncStack[si];
            if(ancestor == lambdaOuterFunc) continue;
            for(paramDef* p : ancestor->params)
                if(p->name == name)
                    return addCapture(name, p->type.name);
            statementBlock* ancestorBody = dynamic_cast<statementBlock*>(ancestor->body);
            if(ancestorBody != nullptr)
                for(statement* s : ancestorBody->statements)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == name)
                            return addCapture(name, vd->type.name);
            for(auto& cap : ancestor->captures)
                if(cap.outerName == name)
                    return addCapture(cap.globalName, cap.typeName);
        }
        // `self` capture: when a lambda is inside an object method, `self` refers to the
        // enclosing object. Capture it to a global.
        if(name == "self" && (currentClass != nullptr || currentObject != nullptr)){
            string selfType = currentClass ? currentClass->name : "object";
            return addCapture("self", selfType);
        }
    }

    // ── FILE SCOPE — collect all matches, then apply ambiguity rule ───────────
    // Same principle as resolveIdentifierType: enum values, globals, verbs, emitter objects,
    // #defines, and #using imports share one conceptual namespace. Multiple matches across
    // these tiers indicates a real ambiguity, not legitimate scope shadowing.
    struct Candidate { string qualified; string type; string origin; bool isEnum; bool isObject = false; };
    vector<Candidate> candidates;

    // Enum values: walk EVERY enum containing this name so multiple enums sharing a value
    // each become a candidate (the expected-type filter below disambiguates).
    for(typeDef* t : languageService.objectTypes){
        auto* ed = dynamic_cast<enumDef*>(t);
        if(!ed) continue;
        bool match = false; int matchedValue = 0;
        for(enumValueDef* ev : ed->namedValues)
            if(ev->name == name){ match = true; matchedValue = ev->value; break; }
        if(!match) continue;
        string enumQualified;
        if(ed->isExternal) enumQualified = name;
        else                enumQualified = to_string(matchedValue);
        candidates.push_back({enumQualified, ed->name, format("enum value of '{0}'", ed->name), true});
    }
    // Globals (variable, function, objectDef, classDef, enumDef)
    for(typeDef* g : languageService.globals){
        if(g->name == name){
            string qual, ct, origin;
            bool isObj = false;
            if(auto* fd = dynamic_cast<functionDef*>(g)){
                ct = fd->returnType.name;
                origin = format("global function '{0}'", g->name);
                if(fd->isValueEmitter && fd->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                        string b = processBglConditionals(blk->i6Body);
                        size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                        size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                        qual = b;
                    } else qual = g->i6name.empty() ? name : g->i6name;
                } else qual = g->i6name.empty() ? name : g->i6name;
            }
            else if(auto* vd = dynamic_cast<variableDeclaration*>(g)){
                ct = vd->type.name;
                qual = g->i6name.empty() ? name : g->i6name;
                origin = format("global variable '{0}'", g->name);
            }
            else if(auto* od = dynamic_cast<objectDef*>(g)){
                // Same type-identity rule as resolveIdentifierType: implicit `object`
                // parent is not a distinguishing class. (Same TODO applies — see above.)
                bool explicitNonObjectClass = od->objectClass && od->objectClass->name != "object";
                ct = explicitNonObjectClass ? od->objectClass->name : od->name;
                qual = g->i6name.empty() ? name : g->i6name;
                origin = format("global object '{0}'", g->name);
                isObj = true;
            }
            else if(dynamic_cast<classDef*>(g) || dynamic_cast<enumDef*>(g)){
                // Class/enum type-name reference (e.g. for ClassName.staticMember dot-paths).
                // The dot-path code in qualifyIdentifier consults this and resolves the tail.
                qual = g->i6name.empty() ? name : g->i6name;
                ct = name;  // type identifies itself
                origin = format("type '{0}'", g->name);
            }
            if(!qual.empty()) candidates.push_back({qual, ct, origin, false, isObj});
            break;
        }
    }
    // #using class imports
    for(classDef* imp : usingImports){
        for(typeMember* m : imp->members){
            string qual, ct, origin;
            bool matched = false;
            if(auto* fd = dynamic_cast<functionDef*>(m)){
                if(fd->name != name) continue;
                ct = fd->returnType.name;
                origin = format("#using-imported method '{0}.{1}'", imp->dName(), name);
                if(fd->isValueEmitter && fd->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                        string b = processBglConditionals(blk->i6Body);
                        b = i6Emitter::replaceWord(b, "$self", imp->name);
                        b = i6Emitter::replaceWord(b, "$val",  imp->name);
                        size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                        size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                        qual = b;
                    } else qual = imp->name + "." + name;
                } else qual = imp->name + "." + name;
                matched = true;
            }
            else if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                if(vd->name != name) continue;
                ct = vd->type.name;
                qual = imp->name + "." + name;
                origin = format("#using-imported variable '{0}.{1}'", imp->dName(), name);
                matched = true;
            }
            if(matched){ candidates.push_back({qual, ct, origin, false}); break; }
        }
    }
    // #using object imports
    for(objectDef* imp : usingObjectImports){
        for(typeMember* m : imp->members){
            string qual, ct, origin;
            bool matched = false;
            if(auto* fd = dynamic_cast<functionDef*>(m)){
                if(fd->name != name) continue;
                ct = fd->returnType.name;
                origin = format("#using-imported method '{0}.{1}'", imp->name, name);
                if(fd->isValueEmitter && fd->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                        string b = processBglConditionals(blk->i6Body);
                        b = i6Emitter::replaceWord(b, "$self", imp->name);
                        b = i6Emitter::replaceWord(b, "$val",  imp->name);
                        size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                        size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                        qual = b;
                    } else qual = imp->name + "." + name;
                } else qual = imp->name + "." + name;
                matched = true;
            }
            else if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                if(vd->name != name) continue;
                ct = vd->type.name;
                qual = imp->name + "." + name;
                origin = format("#using-imported member '{0}.{1}'", imp->name, name);
                matched = true;
            }
            if(matched){ candidates.push_back({qual, ct, origin, false}); break; }
        }
    }
    // Verb names (action constants)
    for(verbObjectDef* vd : languageService.verbs){
        string lower = vd->name;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if(lower == name){
            candidates.push_back({vd->name, "verb", format("verb '{0}'", vd->name), false, /*isObject=*/true});
            break;
        }
    }
    // Global emitter objects
    {
        typeDef& td = languageService.getType(name);
        if(auto* cd = dynamic_cast<classDef*>(&td))
            if(cd->isGlobalEmitterObject || cd->isEmitterClass)
                candidates.push_back({name, name, format("emitter object '{0}'", name), false});
    }
    // #define symbols (substitute literal value)
    {
        auto it = definedSymbols.find(name);
        if(it != definedSymbols.end() && !it->second.empty()){
            bool isNumeric = !it->second.empty() && (isdigit(it->second[0]) || (it->second[0] == '-' && it->second.size() > 1));
            string ct = isNumeric ? "intliteral" : "stringliteral";
            candidates.push_back({it->second, ct, format("#define symbol '{0}'", name), false});
        }
    }

    // Dedupe: candidates that resolve to the same I6 emission AND the same type are the same
    // thing seen via multiple lookup paths (e.g. a verb registered both as a verbObjectDef and
    // as a global object stub). They represent one resolution, not two — collapse them.
    {
        vector<Candidate> deduped;
        for(auto& c : candidates){
            bool dup = false;
            for(auto& d : deduped) if(d.qualified == c.qualified && d.type == c.type){ dup = true; break; }
            if(!dup) deduped.push_back(c);
        }
        candidates = deduped;
    }
    // Globals beat enum values: see resolveIdentifierType for rationale.
    {
        bool hasNonEnum = false;
        for(auto& c : candidates) if(!c.isEnum){ hasNonEnum = true; break; }
        if(hasNonEnum){
            vector<Candidate> filtered;
            for(auto& c : candidates) if(!c.isEnum) filtered.push_back(c);
            candidates = filtered;
        }
    }
    // Resolve the candidate set. Cases:
    //   • 1 candidate         → use it.
    //   • 2+ candidates       → try memberHint, expected-type, then non-verb-beats-verb.
    //                           If still ambiguous, error.
    if(candidates.size() == 1){
        // Shadow-warning: a bare identifier inside an object body that resolves to a
        // file-scope candidate (enum value, global variable/function/object, verb,
        // #define, #using-imported member) but is ALSO a member of the enclosing object
        // (own or inherited) is genuinely ambiguous — the compiler can't tell whether
        // the user meant `self.<name>` (the property of THIS object) or the file-scope
        // identifier. The resolver silently picks the file-scope candidate, which often
        // surfaces as a downstream I6 warning ("Bare property name found") or a baffling
        // runtime mismatch. Surface the ambiguity here so the user can disambiguate.
        //
        // Note: forward-declared own members aren't yet on currentObject->members at
        // method-body parse time, so this catches inherited-from-base and
        // already-declared-own cases but not forward-declared-same-object cases.
        if(currentObject != nullptr){
            // Restrict to VARIABLE (property) collisions only. Method collisions are
            // visually unambiguous since methods are always called with parens, and
            // many objects inherit common methods (`print`, `is`, `provides`, etc.)
            // that the user calls via the global form by name without thinking of
            // them as members.
            auto isObjectProperty = [&]() -> bool {
                for(typeMember* m : currentObject->members)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                        if(vd->name == name) return true;
                function<bool(classDef*)> walk = [&](classDef* c) -> bool {
                    if(!c) return false;
                    for(typeMember* m : c->members)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                            if(vd->name == name) return true;
                    for(classDef* base : c->baseClasses)
                        if(walk(base)) return true;
                    return false;
                };
                return walk(currentObject->objectClass);
            };
            if(isObjectProperty()){
                parsingWarning(format(
                    "Bare '{0}' resolves as {1}, but '{0}' is also a property of the enclosing "
                    "object. Beguile cannot tell which you mean. To read the object's property, "
                    "write 'self.{0}'. To keep the current resolution and silence this warning, "
                    "qualify it explicitly.",
                    name, candidates[0].origin));
            }
        }
        return candidates[0].qualified;
    }
    if(candidates.size() >= 2){
        if(!memberHint.empty()){
            vector<Candidate*> satisfying;
            for(auto& c : candidates)
                if(typeHasMember(c.type, memberHint)) satisfying.push_back(&c);
            if(satisfying.size() == 1) return satisfying[0]->qualified;
        }
        if(!currentExpectedType.empty()){
            vector<Candidate*> compatible;
            for(auto& c : candidates)
                if(isTypeCompatible(c.type, currentExpectedType)) compatible.push_back(&c);
            if(compatible.size() == 1) return compatible[0]->qualified;
        }
        // Non-object beats object (last-resort tiebreaker). See resolveIdentifierType for
        // rationale: objects (and verbs, which are objects via `alias class verb for object`)
        // are reachable in their natural usage contexts — `action == V`, `V.method()`,
        // `func(V)` — all of which are handled by the memberHint and expected-type
        // filters above. Bare references like `score++` mean the variable.
        {
            bool hasNonObject = false;
            for(auto& c : candidates) if(!c.isObject){ hasNonObject = true; break; }
            if(hasNonObject){
                vector<Candidate> filtered;
                for(auto& c : candidates) if(!c.isObject) filtered.push_back(c);
                if(filtered.size() == 1) return filtered[0].qualified;
                candidates = filtered;  // narrowed but still ambiguous — fall through to error
            }
        }
        string msg = format("'{0}' is ambiguous: matches ", name);
        for(size_t i = 0; i < candidates.size(); i++){
            if(i > 0) msg += (i == candidates.size() - 1 ? " and " : ", ");
            msg += candidates[i].origin;
        }
        msg += ". Qualify the use explicitly to disambiguate.";
        parsingError(msg);
    }
    // Loose mode: when parsing #bgl{} content, unknown identifiers are assumed to refer to
    // names in the surrounding I6 stream (locals, globals, properties) rather than Beguile
    // declarations. Pass them through verbatim so the I6 compiler resolves them.
    if(looseIdentifierMode) return name;
    // Property-name fallback: see resolveIdentifierType for rationale. The bare name is
    // emitted verbatim so `obj.provides(weight)` becomes `obj provides weight` in I6.
    //
    // Inside an objectDef body, `self.` qualify the emission: I6 treats a bare property
    // name in expression context as the property's runtime numeric ID, not its value on
    // any particular object — `if(number < 2)` would compare the property ID, not the
    // owning object's `number` value. Matching the unconditional `self.X` emission used
    // by the explicit `self.X` path lets a user drop `self.` from a member read without
    // changing semantics. Note: the (property) cast site handles the `obj.provides(weight)`
    // case separately, so this rewrite doesn't break property-identifier usage.
    if(languageService.isKnownPropertyName(name)){
        if(currentObject != nullptr) return "self." + name;
        return name;
    }
    // Class-name fallback: see resolveIdentifierType for rationale. Bare class identifiers
    // emit verbatim so `obj.is(Container)` becomes `obj ofclass Container` in I6.
    if(languageService.isKnownClassName(name)) return name;
    return "";
}

// Register a closure capture: creates a global variable for the captured value.
// Returns the global name. Deduplicates: if the same outer variable is captured
// multiple times in the same lambda, returns the existing global.

string bglParser::addCapture(const string& outerName, const string& typeName){
    // Check if already captured in this lambda
    for(auto& cap : currentFunc->captures)
        if(cap.outerName == outerName) return cap.globalName;
    // Warn if capturing a for-loop variable — the round-trip (load/unload) means
    // modifications inside the lambda will affect the loop's progression.
    if(currentLoopVars.count(outerName))
        parsingWarning(format("Lambda captures loop variable '{0}'. Modifications inside the lambda will affect loop progression via capture round-trip.", outerName));
    string globalName = format("_bglCap{0}", languageService.captureCounter++);
    currentFunc->captures.push_back({outerName, globalName, typeName});
    return globalName;
}


classDef* bglParser::getDispatchClass(const string& typeName){
    typeDef& td = languageService.getType(typeName);
    if(auto* cls = dynamic_cast<classDef*>(&td)) return cls;
    if(auto* obj = dynamic_cast<objectDef*>(&td)) return obj->objectClass;

    // Generic specialization fallback: templated names like "array<int>" aren't
    // registered as their own typeDef — only the generic base ("array") is.
    // Strip the <...> suffix and look up the base so dispatch on parametric-typed
    // receivers (`array<int> arr; arr.method()` or `arr[i]`) reaches the generic
    // class. Element-type binding still happens via the elementType parameter
    // threaded through resolveMethod / findArraySubscriptOp.
    auto lt = typeName.find('<');
    if(lt != string::npos && lt > 0){
        typeDef& baseTd = languageService.getType(typeName.substr(0, lt));
        if(auto* cls = dynamic_cast<classDef*>(&baseTd)) return cls;
        if(auto* obj = dynamic_cast<objectDef*>(&baseTd)) return obj->objectClass;
    }
    return nullptr;
}


// ===============================================================================
// Type compatibility and arg conversion
// ===============================================================================
bool bglParser::isTypeCompatible(std::string argType, std::string paramType){
    if(paramType == "var") return true;  // var accepts any type without checking
    if(argType == "var") return true;    // var is assignable to any type (untyped source)
    if(argType == paramType) return true;
    // bglclass accepts any registered class as an operand. Lets `obj.is(Foo)` work for
    // every class regardless of whether `Foo` resolved as its own type (non-extern,
    // visible in `globals`) or fell through to the isKnownClassName fallback (extern).
    if(paramType == "bglclass" && languageService.isClassType(argType)) return true;
    // bnum → int implicit widening: bnums are int-valued bitmasks by definition.
    // Narrowing (int → bnum) still requires an explicit cast.
    if(paramType == "int"){
        auto* ed = dynamic_cast<enumDef*>(&languageService.getType(argType));
        if(ed && ed->isBnum) return true;
    }
    // bnum → ancestor bnum (via shared-base chain): a child bnum is compatible with any
    // ancestor in its baseBnum chain. Mirrors class subtype compatibility for bnum families.
    {
        auto* argEnum = dynamic_cast<enumDef*>(&languageService.getType(argType));
        auto* paramEnum = dynamic_cast<enumDef*>(&languageService.getType(paramType));
        if(argEnum && argEnum->isBnum && paramEnum && paramEnum->isBnum){
            for(enumDef* a = argEnum->baseBnum; a; a = a->baseBnum)
                if(a == paramEnum) return true;
        }
    }
    // func<...> compatibility: a func value is compatible with any func<...> param type
    if(argType == "func" && paramType.rfind("func<", 0) == 0) return true;
    if(argType.rfind("func<", 0) == 0 && paramType.rfind("func<", 0) == 0) return true;
    // array<T> compatibility: array is compatible with array<T> param
    if(argType == "array" && paramType.rfind("array<", 0) == 0) return true;
    if(argType.rfind("array<", 0) == 0 && paramType.rfind("array<", 0) == 0) return true;
    // ObjectDef → object: every objectDef is implicitly an object, so any objectDef-typed
    // value is assignable to a parameter of type 'object'. Mirrors class-hierarchy compatibility
    // for instance objects that don't have an explicit class declaration.
    if(paramType == "object" && dynamic_cast<objectDef*>(&languageService.getType(argType)) != nullptr)
        return true;
    // Object subtyping is handled by the class hierarchy check below —
    // only classes that actually inherit from 'object' are compatible with it.
    // Class hierarchy: argType is compatible with paramType if argType inherits from paramType
    {
        classDef* argCls2 = getDispatchClass(argType);
        classDef* paramCls2 = getDispatchClass(paramType);
        if(argCls2 && paramCls2){
            std::function<bool(classDef*)> inheritsFrom = [&](classDef* c) -> bool {
                for(classDef* base : c->baseClasses){
                    if(base == paramCls2 || inheritsFrom(base)) return true;
                }
                return false;
            };
            if(inheritsFrom(argCls2)) return true;
        }
    }
    // Block implicit upcast: if argType is an ancestor of paramType, object-level operator= inherited
    // from the base should not be used to allow assigning a less-specific type to a more-specific one.
    // e.g. assigning an 'object' to a 'room' variable must fail even though room inherits object's operator=.
    {
        classDef* argCls3  = getDispatchClass(argType);
        classDef* paramCls3 = getDispatchClass(paramType);
        if(argCls3 && paramCls3 && argCls3 != paramCls3){
            std::function<bool(classDef*)> isAncestorOf = [&](classDef* c) -> bool {
                for(classDef* base : c->baseClasses)
                    if(base == argCls3 || isAncestorOf(base)) return true;
                return false;
            };
            if(isAncestorOf(paramCls3)) return false;
        }
    }
    // Check target type's operator = (argType)
    // Two-pass: exact type match first, then var wildcard — so specific overloads always beat the catch-all
    classDef* cls = getDispatchClass(paramType);
    if(cls != nullptr){
        if(findMemberInHierarchy(cls, [&](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn && fn->name == "=" && !fn->params.empty() && fn->params[0]->type.name == argType;
        })) return true;
        if(findMemberInHierarchy(cls, [&](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn && fn->name == "=" && !fn->params.empty() && fn->params[0]->type.name == "var";
        })) return true;
    }
    // Check source type's conversion operator: emitter <paramType> operator()
    classDef* argCls = getDispatchClass(argType);
    if(argCls != nullptr && findMemberInHierarchy(argCls, [&](typeMember* m){
        auto* fn = dynamic_cast<functionDef*>(m);
        return fn && fn->name == "operator()" && fn->params.empty() && !fn->isExplicit && fn->returnType.name == paramType;
    })) return true;
    return false;
}

// Reorder named arguments to match parameter positions. Validates that all named args match valid parameter names.
// Reorder named-argument positions to match the parameter list. Inserts default expressions
// for slots not provided by the caller (named or positional). After this call, `args` and
// `interpSegmentsPerArg` are aligned to the parameter positions and `namedArgNames` is empty.
// Returns false and calls errorFn on any reorder error; returns true on success (including the
// no-named-args fast path which is a no-op).

void reorderNamedArgs(functionCallStatement& cs, functionDef* fd, function<bool(string)> errorFn){
    reorderNamedArgsImpl(cs.args, cs.namedArgNames, cs.interpSegmentsPerArg, fd, errorFn);
}


void bglParser::applyArgConversions(std::vector<expression*>& args, functionDef* fd){
    for(size_t i = 0; i < args.size() && i < fd->params.size(); i++){
        string argType = args[i]->resolvedType;
        string paramType = fd->params[i]->type.name;
        // Verb arguments: prefix with ## for I6 action constant syntax.
        // qualifyIdentifier returns bare verb names (e.g. "examine"); the ## prefix is
        // normally only applied by the verb's operator== emitter, but function args need it too.
        if(argType == "verb" && (paramType == "verb" || paramType == "var")){
            string t = args[i]->text();
            if(t.rfind("##", 0) != 0){  // don't double-prefix
                args[i]->tokens.clear();
                args[i]->tokens.push_back("##" + t);
            }
            continue;
        }
        if(paramType == "var" || argType == paramType || argType.empty()) continue;
        // Look for conversion operator on arg's class
        classDef* argCls = getDispatchClass(argType);
        if(argCls == nullptr) continue;
        typeMember* found = findMemberInHierarchy(argCls, [&](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn && fn->name == "operator()" && fn->params.empty() && fn->isEmitter && !fn->isExplicit && fn->returnType.name == paramType && dynamic_cast<i6Block*>(fn->body) != nullptr;
        });
        if(found){
            auto* fn = dynamic_cast<functionDef*>(found);
            auto* blk = dynamic_cast<i6Block*>(fn->body);
            string b = processBglConditionals(blk->i6Body);
            { size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
              size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1); }
            if(b.empty()){
                // Empty body = pass-through conversion; just update the type
                args[i]->resolvedType = paramType;
            } else {
                // $self = host of property access (parentProp's `parent($self)` etc.).
                // $val  = full receiver expression as written (`obj.parent`, `5`, `localInt`).
                // For non-property contexts the two coincide.
                string selfText = !args[i]->emitterSelf.empty() ? args[i]->emitterSelf : args[i]->text();
                string valText  = args[i]->text();
                b = i6Emitter::replaceWord(b, "$self", selfText);
                b = i6Emitter::replaceWord(b, "$val",  valText);
                args[i]->tokens.clear();
                args[i]->tokens.push_back(b);
                args[i]->resolvedType = paramType;
            }
        }
    }
}

// Canonicalize a parsed argument list against a resolved function signature.
// Runs the three steps that every call site needs after resolution: named-argument reordering,
// default-value fill, and source-type conversion via operator(). Keeping all three in one place
// prevents the drift that historically let each caller forget one step (e.g. default fill missing
// from the expression path, silently zeroing omitted args at runtime).

void bglParser::finalizeCallArgs(vector<expression*>& args, vector<string>& namedArgNames,
                                  vector<vector<interpolatedSegment>>& interpSegmentsPerArg,
                                  functionDef* fd){
    if(fd == nullptr) return;
    // Ensure namedArgNames/interpSegmentsPerArg are sized to match args (callers that don't
    // track named args pass empty vectors — pad them so reorder and default-fill can zip).
    while(namedArgNames.size() < args.size()) namedArgNames.push_back("");
    while(interpSegmentsPerArg.size() < args.size()) interpSegmentsPerArg.push_back({});
    reorderNamedArgsImpl(args, namedArgNames, interpSegmentsPerArg, fd,
        [this](string msg){ parsingError(msg); return false; });
    // Default fill for any trailing positional params not yet supplied (reorder only fills
    // when named args are present; this handles the all-positional case).
    for(size_t i = args.size(); i < fd->params.size(); i++){
        if(fd->params[i]->defaultValue.empty())
            parsingError(format("Required parameter '{0}' not provided", fd->params[i]->name));
        expression* defExpr = new expression();
        defExpr->tokens.push_back(fd->params[i]->defaultValue);
        args.push_back(defExpr);
        if(interpSegmentsPerArg.size() < args.size()) interpSegmentsPerArg.push_back({});
    }
    applyArgConversions(args, fd);
}

// Unified dotted-method-call binding. Replaces the duplicated resolve-validate-finalize sequences
// at the statement-level method call, method call in expression, and chain-continuation sites.

// ===============================================================================
// Method-call and global-call binding entry points
// ===============================================================================
functionDef* bglParser::bindMethodCall(string& objType, const string& objPath, const string& methodName,
                                        vector<expression*>& args, vector<string>& namedArgNames,
                                        vector<vector<interpolatedSegment>>& interpSegmentsPerArg,
                                        const string& elementType){
    MethodMatch mm = resolveMethodWithConversion(objType, objPath, methodName, args, elementType);
    // If type-resolution failed but we have named args and a unique arity-matching candidate,
    // reorder against that candidate's parameter positions and retry. This covers the common
    // case where named args are bound out-of-positional-order: type-check needs the args
    // already aligned to params before isTypeCompatible can validate them.
    if(mm.method == nullptr && mm.arityMatch != nullptr){
        bool hasNamed = false;
        for(auto& n : namedArgNames) if(!n.empty()){ hasNamed = true; break; }
        if(hasNamed){
            while(namedArgNames.size() < args.size()) namedArgNames.push_back("");
            while(interpSegmentsPerArg.size() < args.size()) interpSegmentsPerArg.push_back({});
            bool reorderOk = reorderNamedArgsImpl(args, namedArgNames, interpSegmentsPerArg, mm.arityMatch,
                [](string){ return false; });  // silent — let the second pass produce diagnostics
            if(reorderOk)
                mm = resolveMethodWithConversion(objType, objPath, methodName, args, elementType);
        }
    }
    if(!mm.nameFound)
        parsingError(format("No method '{0}' on type '{1}'", methodName, typeDisplayName(objType)));
    if(mm.nameMatch && !mm.arityMatch){
        size_t req = 0; for(paramDef* p : mm.nameMatch->params) if(p->defaultValue.empty()) req++;
        size_t tot = mm.nameMatch->params.size();
        parsingError(format("Method '{0}' on type '{1}' expects {2} argument(s), but {3} were supplied.\n  Expected: {4}",
            methodName, typeDisplayName(objType),
            (req == tot) ? to_string(tot) : to_string(req) + "-" + to_string(tot),
            args.size(), formatSignature(mm.nameMatch)));
    }
    if(mm.method == nullptr){
        // Loose mode (#bgl{}): if there's exactly one name+arity match, bind to it without
        // type-checking the arguments. The trade-off is documented in spec §15.6 — Beguile
        // names are still found, but argument types pass through to I6 unchecked. Multiple
        // candidates remain ambiguous and fall through to the standard error.
        if(looseIdentifierMode){
            functionDef* sole = nullptr;
            int matchCount = 0;
            std::function<void(classDef*)> gatherLoose = [&](classDef* c){
                if(!c) return;
                for(typeMember* m : c->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m))
                        if(fd->name == methodName && !fd->isPrePassStub){
                            size_t req = 0;
                            for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
                            if(args.size() >= req && args.size() <= fd->params.size()){
                                sole = fd; matchCount++;
                            }
                        }
                for(classDef* base : c->baseClasses) gatherLoose(base);
            };
            if(auto* cls = getDispatchClass(objType))
                gatherLoose(cls);
            if(matchCount == 1){
                mm.method = sole;
                finalizeCallArgs(args, namedArgNames, interpSegmentsPerArg, mm.method);
                mangleOverloadSetForReceiver(objType, methodName);
                return mm.method;
            }
        }
        // Build the provided-argument types string and gather candidate overloads (same name,
        // same arity), then for each candidate point at the first mismatching parameter.
        string provided;
        for(size_t i = 0; i < args.size(); i++){
            if(i) provided += ", ";
            string at = args[i]->resolvedType;
            provided += at.empty() ? "?" : typeDisplayName(at);
        }
        vector<functionDef*> candidates;
        std::function<void(classDef*)> gather = [&](classDef* c){
            if(!c) return;
            for(typeMember* m : c->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == methodName && !fd->isPrePassStub){
                        size_t req = 0;
                        for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
                        if(args.size() >= req && args.size() <= fd->params.size())
                            candidates.push_back(fd);
                    }
            for(classDef* base : c->baseClasses) gather(base);
        };
        if(auto* cls = getDispatchClass(objType))
            gather(cls);
        string detail;
        for(functionDef* fd : candidates){
            detail += "\n  candidate: " + formatSignature(fd);
            for(size_t i = 0; i < args.size(); i++){
                string argType = args[i]->resolvedType;
                string paramType = fd->params[i]->type.name;
                if(paramType == "var" || argType.empty() || isTypeCompatible(argType, paramType)) continue;
                detail += format("\n    arg {0}: '{1}' is not compatible with parameter '{2} {3}'",
                                 i + 1, typeDisplayName(argType),
                                 typeDisplayName(paramType), fd->params[i]->dName());
                break;  // first mismatch is enough — fixing it reveals any later ones
            }
        }
        parsingError(format("No overload of method '{0}' on type '{1}' accepts these argument types.\n  provided: ({2}){3}",
            methodName, typeDisplayName(objType), provided, detail));
    }
    finalizeCallArgs(args, namedArgNames, interpSegmentsPerArg, mm.method);
    mangleOverloadSetForReceiver(objType, methodName);
    return mm.method;
}

// Unified global-function-call binding. Replaces the duplicated resolveGlobalCall/validate/finalize
// sequences at the statement-level call site and parseExprFunctionCall.

bglParser::GlobalCallBinding bglParser::bindGlobalCall(const string& name, vector<expression*>& args,
                                                         vector<string>& namedArgNames,
                                                         vector<vector<interpolatedSegment>>& interpSegmentsPerArg,
                                                         functionDef* func, statementBlock* body){
    GlobalCallBinding out;
    GlobalCallMatch gcm = resolveGlobalCall(name, args, func, body);
    // validateGlobalCall returns the return type string; we discard it here (the caller derives
    // return type from the matched method). validateGlobalCall also throws on invalid calls.
    validateGlobalCall(gcm, name, args.size());
    out.funcVarReturnType = gcm.funcVarReturnType;
    if(gcm.funcVarReturnType.empty() && gcm.match){
        out.method = gcm.match;
        finalizeCallArgs(args, namedArgNames, interpSegmentsPerArg, gcm.match);
    }
    return out;
}

// Parses the segments of an interpolated string from the live stream.
// Assumes the '$' token has already been consumed; consumes the opening '"', segments, and closing '"'.

// ===============================================================================
// Namespace-scoped type resolution + enum-value access
// ===============================================================================
string bglParser::resolveNamespacedType(const string& dottedPath){
    size_t dot = dottedPath.find('.');
    if(dot == string::npos) return "";
    string head = dottedPath.substr(0, dot);
    string rest = dottedPath.substr(dot + 1);
    // Find starting object — check globals first, then #using imported scopes
    objectDef* curObj = nullptr;
    for(typeDef* g : languageService.globals)
        if(auto* od = dynamic_cast<objectDef*>(g))
            if(od->name == head){ curObj = od; break; }
    // If not a global, check #using imports: the head might be a member of an imported scope.
    // e.g., after `#using bgl;`, head="glulx" is a member of the bgl object.
    if(!curObj){
        for(objectDef* imp : usingObjectImports){
            for(typeMember* m : imp->members){
                auto* vd = dynamic_cast<variableDeclaration*>(m);
                if(!vd || vd->name != head) continue;
                // Follow the member to find the target object by initializer name
                string initName = vd->declaredExpressionValue ? vd->declaredExpressionValue->text() : "";
                if(!initName.empty())
                    for(typeDef* g : languageService.globals)
                        if(auto* od = dynamic_cast<objectDef*>(g))
                            if(od->name == initName){ curObj = od; break; }
                if(!curObj){
                    // Fall back to type name
                    for(typeDef* g : languageService.globals)
                        if(auto* od = dynamic_cast<objectDef*>(g))
                            if(od->name == vd->type.name){ curObj = od; break; }
                }
                if(curObj) break;
            }
            if(curObj) break;
        }
    }
    if(!curObj){
        // Also check class (for emitter namespace objects registered as classes)
        classDef* cls = getDispatchClass(head);
        if(cls != nullptr){
            // Walk class members for the next segment
            while(!rest.empty()){
                dot = rest.find('.');
                string seg = (dot == string::npos) ? rest : rest.substr(0, dot);
                rest = (dot == string::npos) ? "" : rest.substr(dot + 1);
                for(typeMember* m : cls->members){
                    auto* vd = dynamic_cast<variableDeclaration*>(m);
                    if(vd && vd->name == seg){
                        if(rest.empty() && vd->isAlias && languageService.isClassType(vd->type.name))
                            return vd->type.name;
                        cls = getDispatchClass(vd->type.name);
                        break;
                    }
                }
                if(!cls) return "";
            }
            return "";
        }
        return "";
    }
    // Walk object members
    while(!rest.empty()){
        dot = rest.find('.');
        string seg = (dot == string::npos) ? rest : rest.substr(0, dot);
        rest = (dot == string::npos) ? "" : rest.substr(dot + 1);
        bool found = false;
        for(typeMember* m : curObj->members){
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            if(!vd || vd->name != seg) continue;
            if(rest.empty()){
                // Final segment: only accept explicit type aliases (isAlias=true).
                // Without this guard, any member whose type is a class would match
                // (e.g., `obj.capacity` where capacity is int — int is a class).
                if(vd->isAlias && languageService.isClassType(vd->type.name))
                    return vd->type.name;
                return "";
            }
            // Intermediate: follow to next object or class.
            // For auto members on non-emitter objects, the type might be "object" (base class)
            // rather than the specific object type. Try matching by member's initializer name first,
            // then fall back to the type name.
            string nextType = vd->type.name;
            // If the member has a declared initializer pointing to an object, use that
            string initName = vd->declaredExpressionValue ? vd->declaredExpressionValue->text() : "";
            curObj = nullptr;
            // Try init name first (e.g., auto glulx = _bglGlulx → initName="_bglglulx")
            if(!initName.empty())
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == initName){ curObj = od; break; }
            // Fall back to type name
            if(!curObj)
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == nextType){ curObj = od; break; }
            if(!curObj){
                // Try class
                classDef* nextCls = getDispatchClass(nextType);
                if(nextCls){
                    // Switch to class-walking for the rest
                    while(!rest.empty()){
                        dot = rest.find('.');
                        string innerSeg = (dot == string::npos) ? rest : rest.substr(0, dot);
                        rest = (dot == string::npos) ? "" : rest.substr(dot + 1);
                        bool innerFound = false;
                        for(typeMember* im : nextCls->members){
                            auto* iv = dynamic_cast<variableDeclaration*>(im);
                            if(iv && iv->name == innerSeg){
                                if(rest.empty() && iv->isAlias && languageService.isClassType(iv->type.name))
                                    return iv->type.name;
                                nextCls = getDispatchClass(iv->type.name);
                                innerFound = true;
                                break;
                            }
                        }
                        if(!innerFound) return "";
                    }
                }
                return "";
            }
            found = true;
            break;
        }
        if(!found) return "";
    }
    return "";
}

// Peek ahead to check if the given token starts a valid namespace type path.
// Non-consuming — uses file.peekToken(N).

bool bglParser::isNamespacedTypePath(token firstTok){ 
    if(!firstTok.is(eTokenType::identifier)) return false;
    // Build the dotted path by peeking ahead
    string path = firstTok.value;
    int peekIdx = 1;
    while(file.peekToken(peekIdx).is(token::period)){
        token seg = file.peekToken(peekIdx + 1);
        if(!seg.is(eTokenType::identifier) && !seg.is(eTokenType::dataType)) break;
        path += "." + seg.value;
        peekIdx += 2;
    }
    if(path.find('.') == string::npos) return false;
    string resolved = resolveNamespacedType(path);
    return !resolved.empty();
}

// If first token is an identifier starting a namespace type path, consume the
// dot-delimited tokens and return a synthetic dataType token with the resolved flat name.
// If not a namespace path, return first unchanged.

token bglParser::consumeTypeToken(token first){
    if(!first.is(eTokenType::identifier))
        return first;
    // Check #using imported alias members (bare type from import)
    if(!file.peekToken().is(token::period)){
        for(objectDef* imp : usingObjectImports)
            for(typeMember* m : imp->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->isAlias && vd->name == first.value && languageService.isClassType(vd->type.name)){
                        first.value = vd->type.name;
                        first.tokenType = eTokenType::dataType;
                        return first;
                    }
        return first;
    }
    // Build the path
    string path = first.value;
    while(file.peekToken().is(token::period)){
        token dot = file.peekToken();
        token seg = file.peekToken(2);
        if(!seg.is(eTokenType::identifier) && !seg.is(eTokenType::dataType)) break;
        file.getToken(); // consume '.'
        file.getToken(); // consume segment
        path += "." + seg.value;
    }
    string resolved = resolveNamespacedType(path);
    if(resolved.empty())
        parsingError(format("'{0}' does not resolve to a type", path));
    token result = first;
    result.value = resolved;
    result.tokenType = eTokenType::dataType;
    return result;
}

// Try to resolve an expression-position namespaced enum value access, e.g.
// `bgl.glulx.winPlacement.above` where `bgl.glulx.winPlacement` namespace-resolves to the
// enum/bnum type `bglulxwindowplacement` and `above` is one of its named values.
// On success consumes the dot-delimited tokens after `first` and fills out params; on
// failure consumes nothing and returns false (caller falls through to normal handling).

bool bglParser::tryConsumeNamespacedEnumValue(token first, string& outFlatEmission, string& outEnumType){
    if(!first.is(eTokenType::identifier)) return false;
    if(!file.peekToken(1).is(token::period)) return false;  // must have at least one dot

    // Peek ahead to collect the full dotted path and its per-segment peek indices.
    // Segments alternate with periods: identifier . identifier . identifier …
    vector<string> segments;
    segments.push_back(first.value);
    int peekIdx = 1;
    while(file.peekToken(peekIdx).is(token::period)){
        token seg = file.peekToken(peekIdx + 1);
        if(!seg.is(eTokenType::identifier) && !seg.is(eTokenType::dataType)) break;
        segments.push_back(seg.value);
        peekIdx += 2;
    }
    if(segments.size() < 3) return false;  // need at least A.B.value — namespace + type + value

    // Strip the last segment and attempt namespace type resolution on the prefix.
    string valueName = segments.back();
    transform(valueName.begin(), valueName.end(), valueName.begin(), ::tolower);
    string prefixPath;
    for(size_t i = 0; i + 1 < segments.size(); i++){
        if(i) prefixPath += ".";
        prefixPath += segments[i];
    }
    string enumTypeName = resolveNamespacedType(prefixPath);
    if(enumTypeName.empty()){
        // Prefix didn't resolve as a namespace-to-type path. If the path *looks* like a
        // namespace access (head is a global object and the second segment is an alias /
        // nested-namespace auto member), walk segment-by-segment to locate the bad
        // segment and emit a specific diagnostic — much more useful than letting the
        // broken path flow into operator resolution and surface as 'no operator on object'.
        objectDef* rootObj = nullptr;
        for(typeDef* g : languageService.globals)
            if(auto* od = dynamic_cast<objectDef*>(g))
                if(od->name == first.value){ rootObj = od; break; }
        if(!rootObj) return false;
        // Second segment must be a member of rootObj — otherwise this isn't namespace-style;
        // let normal property-chain handling take over.
        string seg1Lower = segments[1]; transform(seg1Lower.begin(), seg1Lower.end(), seg1Lower.begin(), ::tolower);
        bool seg1IsMember = false;
        for(typeMember* m : rootObj->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == seg1Lower){ seg1IsMember = true; break; }
        if(!seg1IsMember) return false;
        // Walk: find the first segment that fails to resolve as a namespace step.
        objectDef* curObj = rootObj;
        string prefixSoFar = segments[0];
        for(size_t i = 1; i < segments.size(); i++){
            string segLower = segments[i]; transform(segLower.begin(), segLower.end(), segLower.begin(), ::tolower);
            variableDeclaration* foundMember = nullptr;
            functionDef* foundMethod = nullptr;
            for(typeMember* m : curObj->members){
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->name == segLower){ foundMember = vd; break; }
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == segLower){ foundMethod = fd; break; }
            }
            // If the segment is a method on this object, it can only appear as the final
            // segment (followed by a call). Hand off to normal expression handling — that's
            // already wired to dispatch namespace-method calls.
            if(foundMethod) return false;
            if(!foundMember)
                parsingError(format("'{0}' is not a member of '{1}'", segments[i], prefixSoFar));
            // If this is an alias member, the next segment should be an enum value — stop walking.
            if(foundMember->isAlias){
                auto* en = dynamic_cast<enumDef*>(&languageService.getType(foundMember->type.name));
                if(en && i + 1 < segments.size()){
                    string valLower = segments[i+1]; transform(valLower.begin(), valLower.end(), valLower.begin(), ::tolower);
                    bool valExists = false;
                    for(enumValueDef* ev : en->namedValues)
                        if(ev->name == valLower){ valExists = true; break; }
                    if(!valExists)
                        parsingError(format("'{0}' is not a named value of enum/bnum '{1}' (via {2}.{3})",
                                            segments[i+1], foundMember->type.name, prefixSoFar, segments[i]));
                }
                // Alias resolved — fall through to normal handling (unlikely if we got here).
                return false;
            }
            // Auto member / property — follow to its target object to keep walking.
            string initName = foundMember->declaredExpressionValue ? foundMember->declaredExpressionValue->text() : "";
            objectDef* next = nullptr;
            if(!initName.empty())
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == initName){ next = od; break; }
            if(!next)
                for(typeDef* g : languageService.globals)
                    if(auto* od = dynamic_cast<objectDef*>(g))
                        if(od->name == foundMember->type.name){ next = od; break; }
            if(!next) return false;  // non-namespace member — let normal handling proceed
            curObj = next;
            prefixSoFar += "." + segments[i];
        }
        return false;
    }
    auto* ed = dynamic_cast<enumDef*>(&languageService.getType(enumTypeName));
    if(!ed) return false;  // prefix resolved to a class (e.g. window alias), not an enum — let normal handling take over

    // Confirm the final segment is a named value of this enum and capture its value
    int matchedValue = 0;
    bool matched = false;
    for(enumValueDef* ev : ed->namedValues)
        if(ev->name == valueName){ matched = true; matchedValue = ev->value; break; }
    if(!matched)
        parsingError(format("'{0}' is not a named value of enum/bnum '{1}' (via {2})",
                            segments.back(), enumTypeName, prefixPath));

    // Commit: consume all tokens after `first` (segments[1..end] each preceded by '.')
    for(size_t i = 1; i < segments.size(); i++){
        file.getToken();  // consume '.'
        file.getToken();  // consume segment
    }
    // Inline the integer value for non-extern enums; extern enums keep the name (I6 keyword semantics).
    outFlatEmission = ed->isExternal ? valueName : to_string(matchedValue);
    outEnumType = enumTypeName;
    return true;
}



// ===============================================================================
// Dotted path type resolution (multi-hop hierarchy walk)
// ===============================================================================
std::string bglParser::resolvePathType(std::string path, functionDef* func, statementBlock* body, const string& memberHint) {
    size_t dot = path.find('.');
    if(dot == string::npos) return resolveIdentifierType(path, func, body, memberHint);
    string head = path.substr(0, dot);
    string tail = path.substr(dot + 1);
    // Don't pass memberHint to the head — the hint is about the final tail's resolved type, not
    // about a member on the head. Head-resolution uses normal first-match priority.
    string headType = resolveIdentifierType(head, func, body);
    if(headType.empty()) return "";

    // Walk each dotted segment of tail in turn. For each segment, look up a member by name on
    // the current type (specific object instance, then object type, then class hierarchy) and
    // advance the current type to that member's declared type. The last segment's type is the
    // path's resolved type. Handles arbitrary nesting depth (bgl.utilities.buf.X → buf's type)
    // via repeated single-hop walks; previously the code only ran one hop and returned "" for
    // multi-segment tails.
    string lowerHead = head;
    transform(lowerHead.begin(), lowerHead.end(), lowerHead.begin(), ::tolower);
    string currentType = headType;
    string remaining = tail;
    string instanceContext = lowerHead;   // for hop 1, may match a specific global object
    while(true){
        size_t segDot = remaining.find('.');
        string segment = (segDot == string::npos) ? remaining : remaining.substr(0, segDot);

        string memberType;
        // Specific object instance's members (only meaningful at the first hop where head
        // names a global object instance).
        if(!instanceContext.empty()){
            for(typeDef* g : languageService.globals)
                if(auto* od = dynamic_cast<objectDef*>(g))
                    if(od->name == instanceContext)
                        for(typeMember* m : od->members)
                            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                                if(vd->name == segment){ memberType = vd->type.name; break; }
            instanceContext.clear();
        }
        if(memberType.empty()){
            typeDef& td = languageService.getType(currentType);
            classDef* hierarchyRoot = nullptr;
            if(auto* od = dynamic_cast<objectDef*>(&td)){
                for(typeMember* m : od->members)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                        if(vd->name == segment){ memberType = vd->type.name; break; }
                // Fall through to the object's class hierarchy if the member isn't
                // declared on the instance directly. Inherited members like
                // `attributes` (declared on object / on an alias-class) are picked up
                // here. Without this hop, `self.attributes.X` on an instance that
                // didn't write `attributes = {…}` would fail to resolve, even though
                // the class declares the field.
                if(memberType.empty()) hierarchyRoot = od->objectClass;
            }
            else if(auto* cd = dynamic_cast<classDef*>(&td))
                hierarchyRoot = cd;
            if(memberType.empty() && hierarchyRoot != nullptr){
                typeMember* found = findMemberInHierarchy(hierarchyRoot, [&](typeMember* m){
                    auto* vd = dynamic_cast<variableDeclaration*>(m);
                    return vd != nullptr && vd->name == segment;
                });
                if(found) memberType = dynamic_cast<variableDeclaration*>(found)->type.name;
            }
        }
        if(memberType.empty()) return "";
        if(segDot == string::npos) return memberType;
        currentType = memberType;
        remaining = remaining.substr(segDot + 1);
    }
}

// Process an array<T> declaration: array<T> name[N]; or array<T> name = { ... };
