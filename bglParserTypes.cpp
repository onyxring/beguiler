#include <regex>
#include "platform.h"
// ===============================================================================
// bglParserTypes.cpp - type/name resolution + method/call binding.
//
// All entry points are member methods of bglParser; this file holds their
// definitions only.
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
#include "i6Emitter.h"
#include "bglParser.h"

// A #define's value as the literal it stands for (§14.2.1): an integer, a decimal number (a float),
// a quoted string, or any other word as a string.
static vector<string> funcTypeArgs(const string& t);
static pair<string,string> defineLiteral(const string& v){
    if(regex_match(v, regex(R"(-?\d+|\$\$?[0-9A-Fa-f]+)"))) return {v, v[0] == '-' ? "negativeintliteral" : "intliteral"};
    if(regex_match(v, regex(R"(-?\d+\.\d+([eE][-+]?\d+)?)")))
        return {string("$") + (v[0] == '-' ? "-" + v.substr(1) : "+" + v), "float"};
    if(v.size() >= 2 && v.front() == '"' && v.back() == '"') return {v, "stringliteral"};
    return {"\"" + v + "\"", "stringliteral"};
}
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
        pc->defaultUnparsed = fd->params[i]->defaultUnparsed;
        pc->defaultSrc = fd->params[i]->defaultSrc;
        pc->literalMembers = fd->params[i]->literalMembers;   // `literal T` stays a placeholder (see finalizeCallArgs)
        clone->params.push_back(pc);
    }
    return clone;
}


// A parameter's default as I6 text. A pre-scan stub holds the default's source, parsed here the first
// time a call above the declaration leaves the argument out.
string bglParser::paramDefaultText(paramDef* p){
    if(!p->defaultUnparsed) return p->defaultValue;
    p->defaultUnparsed = false;
    bool savedPending = file.hasPendingToken; token savedTok = file.pendingToken;
    eTokenType savedPrevType = file.prevTokenType; string savedPrevValue = file.prevTokenValue;
    int savedLastLine = file.lastTokenLine; string savedLastFile = file.lastTokenFile;
    string savedExpected = currentExpectedType;
    sourceLocation savedStmt = currentStatementSrc;
    file.hasPendingToken = false;
    currentExpectedType = p->type.name;
    currentStatementSrc = sourceLocation{};
    file.openText(p->defaultValue + ")", p->defaultSrc.file, p->defaultSrc.line);
    try {
        expression* e = parseExpression(file.getToken(), {token::parenClose}, nullptr, nullptr);
        p->defaultValue = e->text();
    } catch(...) {
        file.close();
        file.hasPendingToken = savedPending; file.pendingToken = savedTok;
        currentExpectedType = savedExpected; currentStatementSrc = savedStmt;
        throw;
    }
    file.close();
    file.hasPendingToken = savedPending; file.pendingToken = savedTok;
    file.prevTokenType = savedPrevType; file.prevTokenValue = savedPrevValue;
    file.lastTokenLine = savedLastLine; file.lastTokenFile = savedLastFile;
    currentExpectedType = savedExpected; currentStatementSrc = savedStmt;
    return p->defaultValue;
}

// A default argument's literal type, so the conversions a written argument gets apply to it too
// (an int default on a float parameter); "" when it isn't a plain literal.
static string defaultValueType(const string& v){
    if(v.empty()) return "";
    if(regex_match(v, regex(R"(-\d+)"))) return "negativeintliteral";
    if(regex_match(v, regex(R"(\d+|\$\$?[0-9A-Fa-f]+)"))) return "intliteral";
    if(v.rfind("$+", 0) == 0 || v.rfind("$-", 0) == 0) return "float";
    if(v.front() == '"') return "stringliteral";
    if(v == "true" || v == "false") return "bool";
    return "";
}

static string formatSignature(functionDef* fd){
    string sig = fd->dName() + "(";
    for(size_t i = 0; i < fd->params.size(); i++){
        if(i > 0) sig += ", ";
        sig += (fd->params[i]->type.name == "selftype" ? string("selfType") : fd->params[i]->type.dName()) + " " + fd->params[i]->dName();
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
            defExpr->tokens.push_back(parser.paramDefaultText(fd->params[i]));
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
    // An overload whose parameters match the arg types EXACTLY (not merely via an
    // isTypeCompatible conversion) is preferred over a compatible-but-inexact one, so a
    // concrete overload always beats a catch-all that a published `var operator =` would
    // otherwise make compatible. Without this, `stringObj.append('X')` binds a char to
    // `append(stringObj v)` (compatible, because stringObj has `operator =(var)`) instead of
    // the exact `append(char v)`. First-compatible-in-declaration-order still decides ties
    // among equally-inexact matches; exact just wins outright.
    functionDef* exactMatch = nullptr;
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
    if(cls && !elementType.empty()){
        // The nearest generic class in the hierarchy declares the parameter (byteArray's is array's T).
        classDef* generic = !cls->typeParameters.empty() ? cls : nullptr;
        if(generic == nullptr)
            for(classDef* a : cls->ancestorsNearestFirst()) if(!a->typeParameters.empty()){ generic = a; break; }
        if(generic != nullptr) bindings[generic->typeParameters[0]] = elementType;
    }
    // `selfType` in a signature is the receiver's own class at this call (an object's is its class).
    if(cls != nullptr){
        string base = typeName.substr(0, typeName.find('<'));
        string oc = objectsClassName(base);
        bindings["selftype"] = !oc.empty() ? oc : cls->name;
    }

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
        // A stub that recorded its parameters is checked like the definition; one that didn't is a guess.
        if(fd->isPrePassStub && fd->params.size() != args.size()){
            if(!result.method) result.method = fd;
            if(!result.arityMatch) result.arityMatch = fd;
            return;
        }

        // Check arity (accounting for default params)
        size_t req = 0;
        for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
        if(args.size() < req || args.size() > fd->params.size()) return;
        if(!result.arityMatch) result.arityMatch = fd;

        // Check arg types
        bool argsOk = true, usesVar = false, allExact = true;
        for(size_t i = 0; i < args.size() && argsOk; i++){
            string argType = args[i]->resolvedType;
            string paramType = fd->params[i]->type.name;
            // As for global calls: a var (or unknown) argument matches a `var` parameter exactly.
            bool argUnknown = argType.empty() || argType == "var";
            if(paramType == "var"){ if(!argUnknown){ usesVar = true; allExact = false; } }
            else if(argUnknown && argType == "var") allExact = false;
            else if(!argType.empty() && !isTypeCompatible(argType, paramType) && !argFitsByRule(args[i], paramType)) argsOk = false;
            else if(!genericValueFits(args[i], paramType, currentFunc, nullptr)) argsOk = false;
            else {
                // Exactness for the preference tiebreak normalizes a literal to the primitive it
                // denotes (charliteral→char, intliteral→int): `'X'` is an exact match for a `char`
                // param, not a widening — so `append('X')` prefers `append(char)` over an
                // `append(stringObj)` made compatible only by a published `var operator =`. This
                // normalization is deliberately kept out of isTypeCompatible (which stays literal-
                // agnostic to avoid perturbing plain compatibility); it only ranks matches here.
                string argBase = argType;
                if(argBase == "charliteral") argBase = "char";
                else if(argBase == "intliteral" || argBase == "negativeintliteral") argBase = "int";
                else if(string oc = objectsClassName(argBase); !oc.empty()) argBase = oc;
                if(argBase != paramType) allExact = false;  // compatible but not an exact type match
            }
        }
        if(!argsOk) return;
        if(usesVar){ if(!varFallback) varFallback = fd; }
        else {
            if(allExact && !exactMatch) exactMatch = fd;
            if(!result.method) result.method = fd;
        }
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
    //   c) self inside an object body.
    if(!result.method){
        string lowerPath = objPath;
        transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
        objectDef* targetObj = nullptr;
        // (a) Type-name is itself an objectDef.
        if(auto* od = languageService.findObjectType(typeName))
            targetObj = od;
        // (c) self inside an object body
        else if(lowerPath == "self" && currentObject != nullptr)
            targetObj = currentObject;
        // (b) objPath names a global objectDef
        else
            if(auto* od = languageService.findGlobalAs<objectDef>(lowerPath)) targetObj = od;
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

    // An exact-type overload wins over a merely-compatible one found first in declaration order.
    if(exactMatch) result.method = exactMatch;
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
    classDef* srcCls = languageService.findClass(typeName);
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


// Tier: parameters of the function whose body is being parsed.
optional<string> bglParser::resolveTypeFromParams(const string& name, functionDef* func){
    if(func == nullptr) return nullopt;
    for(paramDef* p : func->params)
        if(p->name == name) return p->type.name;
    return nullopt;
}

// Tier: locals declared directly in the statement block being parsed.
optional<string> bglParser::resolveTypeFromBodyLocals(const string& name, statementBlock* body){
    if(body == nullptr) return nullopt;
    for(statement* s : body->statements)
        if(auto* vd = dynamic_cast<variableDeclaration*>(s))
            if(vd->name == name) return vd->type.name;
    return nullopt;
}

// Tier: locals in ancestor blocks still open on activeBlockStack.
optional<string> bglParser::resolveTypeFromAncestorBlocks(const string& name, statementBlock* body){
    for(statementBlock* blk : activeBlockStack)
        if(blk != nullptr && blk != body)
            for(statement* s : blk->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name) return vd->type.name;
    return nullopt;
}

// Tier: members of the enclosing objectDef, including its class hierarchy.
optional<string> bglParser::resolveTypeFromCurrentObject(const string& name){
    if(currentObject == nullptr) return nullopt;
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
    return nullopt;
}

// Tier: members of the enclosing classDef and of its base classes.
optional<string> bglParser::resolveTypeFromCurrentClass(const string& name){
    if(currentClass == nullptr) return nullopt;
    for(typeMember* m : currentClass->members){
        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
            if(vd->name == name) return vd->type.name;
        // A value emitter (`emitter int Slots { 4 }`) read bare is a value of its type.
        if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->name == name && fd->isValueEmitter && fd->isEmitter)
            return fd->returnType.name;
    }
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
    return nullopt;
}

// Closure capture: check outer function's scope for type. This is a lexical-scope walk
// (the lambda's enclosing function), so it runs BEFORE file-scope candidates — standard
// scoping rule that captures shadow same-named globals.
optional<string> bglParser::resolveTypeFromCaptures(const string& name){
    if(lambdaOuterFunc == nullptr) return nullopt;
    for(paramDef* p : lambdaOuterFunc->params)
        if(p->name == name) return p->type.name;
    if(lambdaOuterBody != nullptr)
        for(statement* s : lambdaOuterBody->statements)
            if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                if(vd->name == name) return vd->type.name;
    for(auto it = lambdaOuterBlockStack.rbegin(); it != lambdaOuterBlockStack.rend(); ++it)
        for(statement* s : (*it)->statements)
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
        return currentClass ? currentClass->name : currentObject->name;
    return nullopt;
}

// Globals (variable, function, objectDef, classDef, enumDef)
void bglParser::collectTypeCandidatesFromGlobals(const string& name, vector<TypeCandidate>& candidates){
    // EVERY global of this name, not just the first. The BLR publishes unprefixed globals (`print`,
    // `the`, `typeof`, …), so a user declaration of the same name is a second global — and taking
    // only the first meant the user's never became a candidate, leaving the ranking below (which
    // exists precisely to break such ties by member hint) nothing to choose between. `obj.member`
    // on a shadowed name then resolved against the BLR entry and reported the member as missing.
    for(typeDef* g : languageService.globals){
        if(g->name != name) continue;
        string ct, origin;
        bool isObj = false, isFn = false, libRoutine = false;
        if(auto* vd = dynamic_cast<variableDeclaration*>(g)){ ct = vd->type.name; origin = format("global variable '{0}'", g->name); }
        else if(auto* fd = dynamic_cast<functionDef*>(g)){
            // A bare function NAME used as a value is a function reference (the I6 routine
            // name is its address), typed `func` — same as a lambda (which also resolves to
            // "func" and matches func<…> params). A *call* `name(args)` never reaches here;
            // it's handled by bindGlobalCall. So this lets `arr.filter(isEven)` bind like
            // `arr.filter((int x) => …)`. (void fd is unused here.)
            ct = funcValueType(fd->name); origin = format("global function '{0}'", g->name); isFn = true;
            libRoutine = fd->isExternal;
        }
        else if(auto* od = dynamic_cast<objectDef*>(g)){
            isObj = true;
            // A declared object is its own type: a one-instance subtype of its class, so members its
            // body adds resolve, and it fits wherever its class or an ancestor is expected. Member
            // and operator lookups reach the class through classOf/getDispatchClass. A verb is the
            // exception: its value is an action, typed by its verb class, which grammar and action
            // emission key on.
            ct = dynamic_cast<verbObjectDef*>(od) != nullptr && od->objectClass != nullptr ? od->objectClass->name : od->name;
            origin = format("global object '{0}'", g->name);
        }
        else if(dynamic_cast<classDef*>(g) || dynamic_cast<enumDef*>(g)){
            // Class/enum type-name reference (e.g. ClassName.staticMember). The type
            // identifies itself so dot-path code can locate the member.
            ct = name;
            origin = format("type '{0}'", g->name);
        }
        // Overloads of one global function all describe the same reference, and a pre-scan stub
        // may sit beside its real definition; collapse them so an overload set does not look like
        // an ambiguity.
        if(!ct.empty()){
            bool dup = false;
            for(auto& c : candidates) if(c.type == ct && c.isObject == isObj){ dup = true; break; }
            if(!dup) candidates.push_back({ct, origin, false, isObj, isFn, libRoutine});
        }
    }
}

// Reduce the collected file-scope candidates to a single type, or report the ambiguity.
// Disengaged means no candidate survived — the caller continues to its fallbacks.
optional<string> bglParser::selectTypeCandidate(const string& name, const string& memberHint,
                                                vector<TypeCandidate>& candidates){
    // Where a property is expected, a property's name is that property, whatever else shares it (a verb).
    if(currentExpectedType == "property" && namesProperty(name) && !candidates.empty()) return string("property");
    // A library routine named bare yields to anything else of the name: the program's own names
    // (and an enum's values) are what a bare word means; the routine is reached by calling it.
    if(any_of(candidates.begin(), candidates.end(), [](const TypeCandidate& c){ return !c.isLibraryRoutine; }))
        candidates.erase(remove_if(candidates.begin(), candidates.end(), [](const TypeCandidate& c){ return c.isLibraryRoutine; }),
                         candidates.end());
    // Dedupe: candidates resolving to the same type are the same thing seen via multiple lookup
    // paths (e.g. a verb registered both as a verbObjectDef and as a global object stub).
    {
        vector<TypeCandidate> deduped;
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
    // Except where an enumeration is expected (an argument to an enum parameter, a declared enum
    // variable) and only its values fit: `fixed` for a bGlulxWindowScale parameter is the scale value,
    // not a #using-imported print style of the same name.
    if(auto keep = enumCandidatesFittingExpected(candidates); !keep.empty()) candidates = keep;
    {
        bool hasNonEnum = false;
        for(auto& c : candidates) if(!c.isEnum){ hasNonEnum = true; break; }
        if(hasNonEnum){
            vector<TypeCandidate> filtered;
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
        // Kind filter: a FUNCTION reference and a variable/object of the same name are told apart
        // by how the name is USED, not by which was declared first. A call `name(args)` never
        // reaches here — bindGlobalCall handles it — so every use that does reach here is either a
        // dotted access or a bare value, and a function reference supports neither a member access
        // nor being shadowed by an author's own declaration. Dropping function candidates therefore
        // resolves the collision that the BLR's unprefixed globals (`print`, `the`, `typeof`, …)
        // otherwise create, while a lone function name still resolves as a reference
        // (`arr.filter(isEven)`) because it is then the only candidate.
        {
            bool hasNonFunction = false;
            for(auto& c : candidates) if(!c.isFunction){ hasNonFunction = true; break; }
            // Where a func<> is expected, though, the name is the function.
            bool funcExpected = currentExpectedType.rfind("func", 0) == 0 && !memberHint.size();
            if(hasNonFunction){
                vector<TypeCandidate> filtered;
                for(auto& c : candidates) if(c.isFunction == funcExpected) filtered.push_back(c);
                if(filtered.empty())
                    for(auto& c : candidates) if(!c.isFunction) filtered.push_back(c);
                candidates = filtered;
            }
        }
        if(candidates.size() == 1) return candidates[0].type;
        if(!memberHint.empty()){
            vector<TypeCandidate*> satisfying;
            for(auto& c : candidates)
                if(typeHasMember(c.type, memberHint)) satisfying.push_back(&c);
            if(satisfying.size() == 1) return satisfying[0]->type;
            // 0 or 2+ satisfy — fall through to expected-type filter, then ambiguity error.
        }
        if(!currentExpectedType.empty()){
            vector<TypeCandidate*> compatible;
            for(auto& c : candidates)
                if(isTypeCompatible(c.type, currentExpectedType)) compatible.push_back(&c);
            if(compatible.size() == 1) return compatible[0]->type;
            // Where a class is expected, the name of a class is that class, not a value of some class type.
            if(currentExpectedType == "bglclass"){
                vector<TypeCandidate*> types;
                for(auto* c : compatible) if(c->origin.rfind("type", 0) == 0) types.push_back(c);
                if(types.size() == 1) return types[0]->type;
            }
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
                vector<TypeCandidate> filtered;
                for(auto& c : candidates) if(!c.isObject) filtered.push_back(c);
                if(filtered.size() == 1) return filtered[0].type;
                candidates = filtered;  // narrowed but still ambiguous — fall through to error
            }
        }
        // All that is left is one global name carrying several overloads. A call never reaches
        // here (bindGlobalCall resolves it from the arguments), so this is a bare reference — and
        // a reference must name ONE routine, which the name alone cannot do.
        {
            bool allFunctions = true;
            for(auto& c : candidates) if(!c.isFunction){ allFunctions = false; break; }
            if(allFunctions)
                parsingError(format("'{0}' names {1} overloads, so it cannot be used as a bare "
                    "function reference — only a call can say which one is meant. Wrap the overload "
                    "you want in a function of its own and reference that.", name, candidates.size()));
        }
        // A global beats a #using-imported member of the same name (§10.4), quietly when both name
        // the same type (an imported alias of the global class).
        {
            int globals = 0; size_t globalAt = 0;
            for(size_t i = 0; i < candidates.size(); i++)
                if(candidates[i].origin.rfind("#using-imported", 0) != 0){ globals++; globalAt = i; }
            if(globals == 1){
                // Quiet only when the global is a type and the import an alias of that same type.
                bool sameType = candidates[globalAt].origin.rfind("type", 0) == 0;
                for(auto& c : candidates)
                    if(getDispatchClass(c.type) != getDispatchClass(candidates[globalAt].type)
                       || getDispatchClass(c.type) == nullptr) sameType = false;
                if(!sameType)
                    parsingWarning(format("'{0}' is both {1} and an imported member; the global is used. "
                        "Write the imported one with its full path.", name, candidates[globalAt].origin));
                return candidates[globalAt].type;
            }
        }
        // Where a property is expected (`obj.provides(number)`), a property's name is that property.
        if(currentExpectedType == "property" && namesProperty(name)) return string("property");
        string msg = format("'{0}' is ambiguous: matches ", name);
        for(size_t i = 0; i < candidates.size(); i++){
            if(i > 0) msg += (i == candidates.size() - 1 ? " and " : ", ");
            msg += candidates[i].origin;
        }
        msg += ". Qualify the use explicitly to disambiguate.";
        parsingError(msg);
    }
    return nullopt;
}

std::string bglParser::resolveIdentifierType(std::string name, functionDef* func, statementBlock* body, const string& memberHint){
    // Global-scope qualifier `::name`: skip lexical scope (params/locals/members) and resolve only
    // against file scope. Set on the head of a dotted path too (`::obj.member` → force-global `obj`).
    bool forceGlobalScope = false;
    if(name.size() > 2 && name[0]==':' && name[1]==':'){ forceGlobalScope = true; name = name.substr(2); }
    if(name == "null") return "nullliteral";
    // `outer` inside an inline accessor's operator body = the enclosing (host) object; its type is
    // that object's own type. Only meaningful while synthesizing the accessor class body.
    if(name == "outer" && accessorOuter != nullptr) return accessorOuter->name;
    if(name == "self" && lambdaOuterFunc == nullptr){
        // `self` inside an objectDef is the objectDef's own identity, regardless of base class.
        // resolveMethod walks `objectClass`'s hierarchy for inherited methods, so this works
        // whether the base is `object`, a user class, or anything else — no special-casing.
        if(currentClass != nullptr && accessorOuter != nullptr) return currentClass->name;   // an inline accessor's own self
        if(currentObject != nullptr) return currentObject->name;
        if(currentClass != nullptr) return currentClass->name;
        return "_bglobject";
    }

    // ── LEXICAL SCOPE — first match wins absolutely ────────────────────────────
    // Locals, params, and members of the enclosing object/class form the lexical scope of
    // the current code. Inner scope shadows outer by design, so first match wins without
    // any ambiguity check. memberHint is NOT applied here: if a local exists with this
    // name, it's the receiver — let dispatch produce a "no method on type X" error if the
    // local's type doesn't have the member, rather than silently looking past the shadow.
    if(!forceGlobalScope){
        if(auto r = resolveTypeFromParams(name, func))          return *r;
        if(auto r = resolveTypeFromBodyLocals(name, body))      return *r;
        if(auto r = resolveTypeFromAncestorBlocks(name, body))  return *r;
        if(auto r = resolveTypeFromCurrentObject(name))         return *r;
        if(auto r = resolveTypeFromCurrentClass(name))          return *r;
        if(auto r = resolveTypeFromCaptures(name))              return *r;
    }

    // ── FILE SCOPE — collect all matches, then apply ambiguity rule ───────────
    // Enum values, globals, verbs, emitter objects, #defines, and #using imports all live
    // at file/module scope. They share one conceptual namespace — collisions there are
    // accidents, not deliberate shadowing. Collect every match and let the ambiguity logic
    // below decide.
    vector<TypeCandidate> candidates;

    // Enum values: walk EVERY enum that contains this value name. Multiple enums may share
    // a value name (e.g. eColor.Red and eFlavor.Red); each is a distinct candidate that the
    // expected-type tie-breaker can disambiguate.
    // A class's name is the class (`object`, not eType.object) unless that enum is what's expected.
    bool classNamed = languageService.isKnownClassName(name);
    for(typeDef* t : languageService.objectTypes){
        auto* ed = dynamic_cast<enumDef*>(t);
        if(!ed) continue;
        if(classNamed && ed->name != currentExpectedType) continue;
        for(enumValueDef* v : ed->namedValues)
            if(v->name == name){
                candidates.push_back({ed->name, format("enum value of '{0}'", ed->name), true});
                break;
            }
    }
    collectTypeCandidatesFromGlobals(name, candidates);
    // Action constants (extern verb Take etc.)
    for(verbObjectDef* vd : languageService.verbs){
        string lower = vd->name;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if(lower == name){
            // A declared verb is already a candidate as its global object, typed by its verb class.
            if(vd->isExternal) candidates.push_back({"verb", format("verb '{0}'", vd->name), false, /*isObject=*/true});
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
        if(it != definedSymbols.end() && !it->second.empty() && !definesExternGlobal(name)){
            candidates.push_back({defineLiteral(it->second).second, format("#define symbol '{0}'", name), false});
        }
    }
    // #using class imports
    for(classDef* imp : usingImports){
        bool added = false;
        for(typeMember* m : imp->members){
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == name){ candidates.push_back({fd->returnType.name, format("#using-imported method '{0}.{1}'", imp->dName(), name), false, /*isObject=*/false, /*isFunction=*/!(fd->isValueEmitter && fd->isEmitter)}); added = true; break; }   // a value emitter expands to a value
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
                if(fd->name == name){ candidates.push_back({fd->returnType.name, format("#using-imported method '{0}.{1}'", imp->name, name), false, /*isObject=*/false, /*isFunction=*/!(fd->isValueEmitter && fd->isEmitter)}); added = true; break; }   // a value emitter expands to a value
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == name){ candidates.push_back({vd->type.name, format("#using-imported member '{0}.{1}'", imp->name, name), false}); added = true; break; }
        }
        if(added) continue;
    }

    if(auto r = selectTypeCandidate(name, memberHint, candidates)) return *r;

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
    classDef* objCls = currentObject->objectClass;
    return objCls != nullptr && objCls->findMember([&](typeMember* m){
        auto* vd = dynamic_cast<variableDeclaration*>(m);
        return vd != nullptr && vd->name == name;
    }) != nullptr;
}

// Return the declared element type of an array variable, or "" if `name` isn't an arrayDeclaration
// in any reachable scope. Walks the same scopes as resolveIdentifierType but looks specifically
// for arrayDeclaration subclass instances so the elementType field can be read.

string bglParser::resolveArrayElementType(const string& name, functionDef* func, statementBlock* body){
    if(size_t d = name.rfind('.'); d != string::npos)
        return resolveArrayElementTypeDotted(name.substr(0, d), name.substr(d + 1), func, body);
    if(func != nullptr)
        for(paramDef* p : func->params)
            if(p->name == name) {
                // Parameter type is stored as "array<T>" / "rawarray<T>" for typed array params
                const string& tn = p->type.name;
                string et = arrayElemType(tn);
                if(!et.empty()) return et;
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
    // A block between them (a loop body holding the array, read in an `if` inside it): innermost first.
    for(auto it = activeBlockStack.rbegin(); it != activeBlockStack.rend(); ++it)
        if(*it != nullptr && *it != body)
            for(statement* s : (*it)->statements)
                if(auto* ad = dynamic_cast<arrayDeclaration*>(s))
                    if(ad->name == name) return ad->elementType;
    // An array a lambda captures (from an enclosing routine), by its own name or its capture's.
    // A block inside the lambda (an `if` body) parses with its own context; the lambda is still currentFunc.
    if(functionDef* lam = func != nullptr && func->isLambda ? func : currentFunc != nullptr && currentFunc->isLambda ? currentFunc : nullptr){
        bool known = false;
        for(auto& cap : lam->captures) known |= cap.outerName == name || cap.bodyName() == name;
        if(!known && lam == currentFunc) qualifyFromCaptures(name);
        for(auto& cap : lam->captures)
            if(cap.outerName == name || cap.bodyName() == name){
                if(cap.typeName == "bytearray") return "char";
                if(string et = arrayElemType(cap.typeName); !et.empty()) return et;
            }
    }
    if(currentObject != nullptr)
        for(typeMember* m : currentObject->members)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(m))
                if(ad->name == name) return ad->elementType;
    // An inherited member: the class being declared and its bases, or the object's class chain.
    classDef* selfClass = currentClass != nullptr ? currentClass
                        : currentObject != nullptr ? currentObject->objectClass : nullptr;
    if(selfClass != nullptr)
        if(auto* ad = dynamic_cast<arrayDeclaration*>(findMemberInHierarchy(selfClass, [&](typeMember* m){
               return dynamic_cast<arrayDeclaration*>(m) != nullptr && m->name == name; })))
            return ad->elementType;
    if(auto* ad = languageService.findGlobalAs<arrayDeclaration>(name)) return ad->elementType;
    return "";
}

// Resolve the element type of `objName.propName` where propName is an array member of obj's class.

// `selfType` as a member array's element type is the owner's own class.
string bglParser::resolveArrayElementTypeDotted(const string& objName, const string& propName,
                                                  functionDef* func, statementBlock* body){
    string t = resolveArrayElementTypeDottedRaw(objName, propName, func, body);
    if(t != "selftype") return t;
    string owner = objName;
    if(size_t d = propName.rfind('.'); d != string::npos) owner += "." + propName.substr(0, d);
    string ot = owner == "self" ? currentSelfTypeName()
              : owner.find('.') == string::npos ? resolveIdentifierType(owner, func, body) : resolvePathType(owner, func, body);
    string oc = objectsClassName(ot);
    return !oc.empty() ? oc : ot;
}

string bglParser::resolveArrayElementTypeDottedRaw(const string& objNameIn, const string& propNameIn,
                                                  functionDef* func, statementBlock* body){
    // A longer path (`gh.item` + `items`, or `gh` + `item.items`): the array is the last segment,
    // and everything before it is the owner.
    string objName = objNameIn, propName = propNameIn;
    if(size_t d = propName.rfind('.'); d != string::npos){
        objName += "." + propName.substr(0, d);
        propName = propName.substr(d + 1);
    }
    // Find objName's class — walk objectInstances + globals
    classDef* cls = nullptr;
    for(typeDef* t : languageService.objectInstances)
        if(auto* od = dynamic_cast<objectDef*>(t))
            if(od->name == objName || (!od->i6name.empty() && od->i6name == objName)) {   // an emitted name (`_box`) too
                for(typeMember* m : od->members)
                    if(auto* ad = dynamic_cast<arrayDeclaration*>(m))
                        if(ad->name == propName || (!ad->i6name.empty() && ad->i6name == propName)) return ad->elementType;
                cls = od->objectClass;
                break;
            }
    // A verb, named by its Beguile name (an inline accessor's `outer`) or its I6 one.
    if(!cls)
        for(verbObjectDef* v : languageService.verbs){
            string vi6 = v->i6name; transform(vi6.begin(), vi6.end(), vi6.begin(), ::tolower);
            string low = objName; transform(low.begin(), low.end(), low.begin(), ::tolower);
            if(v->name != low && vi6 != low) continue;
            for(typeMember* m : v->members)
                if(auto* ad = dynamic_cast<arrayDeclaration*>(m); ad && ad->name == propName) return ad->elementType;
            cls = v->objectClass;
            break;
        }
    if(!cls) {
        string objType = objName.find_first_of(".(") != string::npos ? resolvePathType(objName, func, body)
                                                                    : resolveIdentifierType(objName, func, body);
        // `self` in an object's own method: the object is its own type.
        if(objectDef* od = languageService.findObjectType(objType)){
            for(typeMember* m : od->members)
                if(auto* ad = dynamic_cast<arrayDeclaration*>(m); ad && ad->name == propName) return ad->elementType;
            cls = od->objectClass;
        } else cls = languageService.classOf(objType);
    }
    function<string(classDef*)> walk = [&](classDef* c) -> string {
        if(!c) return "";
        for(typeMember* m : c->members)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(m))
                if(ad->name == propName || (!ad->i6name.empty() && ad->i6name == propName)) return ad->elementType;
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

functionDef* bglParser::findArraySubscriptOp(classDef* arrCls, const string& elemType, bool isWrite, const string& valType){
    if(!arrCls) return nullptr;
    // A literal value (valType a pseudo-type such as charliteral) prefers a `[]=` overload
    // declared against that pseudo-type (§2.4) before the element type's own.
    if(isWrite && !valType.empty() && valType != elemType && valType.size() > 7
       && valType.compare(valType.size() - 7, 7, "literal") == 0){
        // Only an overload that names the pseudo-type itself; a type parameter bound to it
        // (array<T>'s `T` → charliteral) is the element-type overload, not a literal one.
        functionDef* lit = nullptr;
        function<void(classDef*)> searchLit = [&](classDef* c) {
            if(!c || lit) return;
            for(typeMember* m : c->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == "[]=" && fd->params.size() == 2 && fd->params[1]->type.name == valType){ lit = fd; return; }
            for(classDef* base : c->baseClasses) searchLit(base);
        };
        searchLit(arrCls);
        if(lit) return lit;
    }
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

// Derive an element type for a class that defines a concrete operator[] (read) but is not a
// declared array<T> — e.g. string's `char operator[](int)`. Walks the hierarchy and returns the
// return type of the first 1-arg `[]` operator whose return is NOT one of the class's own type
// parameters (those are the array<T> case, which requires an explicit element type instead).
string bglParser::inferSubscriptElementType(classDef* cls){
    string result;
    function<void(classDef*)> search = [&](classDef* c){
        if(!c || !result.empty()) return;
        for(typeMember* m : c->members)
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == "[]" && fd->params.size() == 1){
                    bool isTypeParam = false;
                    for(const string& tp : c->typeParameters)
                        if(tp == fd->returnType.name) isTypeParam = true;
                    if(!isTypeParam){ result = fd->returnType.name; return; }
                }
        for(classDef* base : c->baseClasses){ if(!result.empty()) return; search(base); }
    };
    search(cls);
    // A class with only `operator []=` is typed by the value it stores.
    function<void(classDef*)> searchSet = [&](classDef* c){
        if(!c || !result.empty()) return;
        for(typeMember* m : c->members)
            if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->name == "[]=" && fd->params.size() == 2){
                result = fd->params[1]->type.name; return;
            }
        for(classDef* base : c->baseClasses) searchSet(base);
    };
    if(result.empty()) searchSet(cls);
    return result;
}

// Returns the I6-qualified form of an identifier based on scope:
//   - found in local params/vars → name (unqualified)
//   - found in current object's members → "self.name"
//   - found in enum values or globals → name (unqualified)
//   - not found → "" (caller should report an error)

// Check if a variable is declared const (local, global, or class member).

// ===============================================================================
// Candidate gathering + brace-argument type inference (for bare `foo({ … })`)
// ===============================================================================
// Collect global functionDefs named `name` (collection only, mirrors resolveGlobalCall's filter).
vector<functionDef*> bglParser::collectGlobalCandidates(const string& name){
    string lower = name;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    vector<functionDef*> out, stubs;
    for(typeDef* g : languageService.globals)
        if(auto* fd = dynamic_cast<functionDef*>(g))
            if(fd->name == lower){
                if(!fd->isPrePassStub) out.push_back(fd);
                else                   stubs.push_back(fd);
            }
    // Fall back to pre-scan stubs only when no real definition is visible yet — i.e. a FORWARD
    // call (the function is declared after this use site). The stubs now carry parameter types
    // (preScanCaptureParams), so brace-argument inference resolves order-independently. Real
    // definitions always win, so already-resolvable calls are unaffected.
    return out.empty() ? stubs : out;
}
// Collect method functionDefs named `methodName` reachable from a receiver of type `typeName`
// (class hierarchy + objectDef own members + its class hierarchy). Mirrors resolveMethod's walk.
vector<functionDef*> bglParser::collectMethodCandidates(const string& typeName, const string& methodName){
    string lower = methodName;
    transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    vector<functionDef*> out, stubs;
    set<functionDef*> seen;
    auto add = [&](functionDef* fd){
        if(fd->name != lower || seen.count(fd)) return;
        seen.insert(fd);
        if(fd->isPrePassStub) stubs.push_back(fd);   // forward method (declared later) — fallback only
        else                  out.push_back(fd);
    };
    auto walk = [&](classDef* c){
        if(!c) return;
        c->forEachMember([&](typeMember* m){ if(auto* fd = dynamic_cast<functionDef*>(m)) add(fd); });
    };
    walk(getDispatchClass(typeName));
    if(auto* od = languageService.findObjectType(typeName)){
        for(typeMember* m : od->members) if(auto* fd = dynamic_cast<functionDef*>(m)) add(fd);
        walk(od->objectClass);
    }
    // Real definitions win; a param-carrying pre-scan stub is used only when the method is
    // declared after this use site (forward call) — so brace-argument inference stays
    // order-independent, matching collectGlobalCandidates.
    return out.empty() ? stubs : out;
}
// Compute the per-position / per-name agreed object-backed class across candidate callees. A slot
// is set only when EVERY candidate that reaches it names the same object-backed class; any
// disagreement (or a non-object-backed / out-of-range param) leaves it nullptr → a bare `{` there
// errors and the author must write `Type{ … }`.
// The type's temporary-holding hooks, or null when it declares none.
static functionDef* temporaryHook(classDef* cls, const char* name, size_t params){
    if(cls == nullptr) return nullptr;
    for(classDef* c = cls; c != nullptr; c = c->baseClasses.empty() ? nullptr : c->baseClasses.front())
        for(typeMember* m : c->members)
            if(auto* fn = dynamic_cast<functionDef*>(m))
                if(fn->isEmitter && fn->name == name && fn->params.size() == params && dynamic_cast<i6Block*>(fn->body))
                    return fn;
    return nullptr;
}

bglParser::HeldTemporaries bglParser::holdCallTemporaries(const vector<pair<string*, string>>& operands){
    HeldTemporaries held;
    // An operand that emits a call (a routine, a getter) can allocate; a bare name or a literal can't.
    auto allocates = [](const string& t){ return t.find('(') != string::npos; };
    size_t allocating = 0;
    for(auto& [text, type] : operands) if(allocates(*text)) allocating++;
    if(allocating < 2) return held;
    for(auto& [text, type] : operands){
        if(!allocates(*text)) continue;
        classDef* cls = getDispatchClass(type);
        functionDef* hold = temporaryHook(cls, "holdtemporary", 1);
        if(hold == nullptr || !temporaryHook(cls, "temporarymark", 0) || !temporaryHook(cls, "releasetemporaries", 2)) continue;
        emitterBindings b; b.fn = hold; b.args = { *text }; b.trim = emitterTrim::wsSemi;
        *text = expandEmitterBody(dynamic_cast<i6Block*>(hold->body), b);
        if(find(held.owners.begin(), held.owners.end(), cls) == held.owners.end()) held.owners.push_back(cls);
    }
    return held;
}

string bglParser::wrapHeldCall(const HeldTemporaries& held, const string& callText, vector<string>* marks){
    string out = callText;
    for(classDef* cls : held.owners){
        functionDef* mark = temporaryHook(cls, "temporarymark", 0);
        functionDef* release = temporaryHook(cls, "releasetemporaries", 2);
        emitterBindings mb; mb.fn = mark; mb.trim = emitterTrim::wsSemi;
        string markText = expandEmitterBody(dynamic_cast<i6Block*>(mark->body), mb);
        if(marks != nullptr){ markText = heldMarkPlaceholder(marks->size()); marks->push_back(expandEmitterBody(dynamic_cast<i6Block*>(mark->body), mb)); }
        emitterBindings rb; rb.fn = release; rb.args = { out, markText }; rb.trim = emitterTrim::wsSemi;
        out = expandEmitterBody(dynamic_cast<i6Block*>(release->body), rb);
    }
    return out;
}

bool bglParser::isInlineConstructible(classDef* cls){
    if(cls == nullptr) return false;
    if(isObjectBackedClass(cls)) return true;
    for(variableDeclaration* vd : positionalMembers(cls)) if(!vd->isExternal) return true;
    return false;
}

bglParser::BraceArgHints bglParser::braceArgHints(const vector<functionDef*>& candidates){
    BraceArgHints h;
    size_t maxP = 0;
    for(auto* fd : candidates) maxP = std::max(maxP, fd->params.size());
    h.positional.assign(maxP, nullptr);
    vector<char> posSet(maxP, 0), posConflict(maxP, 0);
    map<string, classDef*> namedSeen;
    set<string> namedConflict;
    // A class participates in `Type{ … }` aggregate construction if it's object-backed OR it's a
    // value class declaring `inline` members (positional slots) — both bake through
    // bakeInlineObjectAggregate. Value/collection types with `operator=(initializerList)` use the
    // braced-LIST path instead and are intentionally excluded here.
    // A positional member with storage: the root's positional `instanceName` is a header slot, and
    // doesn't make an array or a value class an aggregate.
    auto inlineConstructible = [&](classDef* c){ return isInlineConstructible(c); };
    for(auto* fd : candidates){
        for(size_t i = 0; i < fd->params.size(); i++){
            classDef* cls = getDispatchClass(fd->params[i]->type.name);
            classDef* obj = inlineConstructible(cls) ? cls : nullptr;
            if(!posSet[i]){ h.positional[i] = obj; posSet[i] = 1; }
            else if(h.positional[i] != obj) posConflict[i] = 1;
            const string& pn = fd->params[i]->name;   // canonical (lowercased)
            auto it = namedSeen.find(pn);
            if(it == namedSeen.end()) namedSeen[pn] = obj;
            else if(it->second != obj) namedConflict.insert(pn);
        }
    }
    for(size_t i = 0; i < maxP; i++) if(posConflict[i]) h.positional[i] = nullptr;
    for(auto& [k, v] : namedSeen) if(v != nullptr && !namedConflict.count(k)) h.named[k] = v;
    // Agreed enumeration parameter types (see BraceArgHints). A position some candidate lacks
    // still counts: that candidate can't take an argument there at all.
    h.positionalEnum.assign(maxP, "");
    vector<char> enumConflict(maxP, 0);
    map<string, string> namedEnumSeen;
    set<string> namedEnumConflict;
    for(auto* fd : candidates){
        for(size_t i = 0; i < fd->params.size(); i++){
            const string& t = fd->params[i]->type.name;
            string e = (languageService.findEnum(t) || t == "property" || t == "bglclass") ? t : "";   // a property or class parameter too
            if(h.positionalEnum[i].empty() && !enumConflict[i]) h.positionalEnum[i] = e;
            else if(h.positionalEnum[i] != e) enumConflict[i] = 1;
            const string& pn = fd->params[i]->name;
            auto it = namedEnumSeen.find(pn);
            if(it == namedEnumSeen.end()) namedEnumSeen[pn] = e;
            else if(it->second != e) namedEnumConflict.insert(pn);
        }
    }
    for(size_t i = 0; i < maxP; i++) if(enumConflict[i]) h.positionalEnum[i] = "";
    h.positionalRef.assign(maxP, !candidates.empty());
    map<string, bool> namedRefSeen;
    for(auto* fd : candidates){
        for(size_t i = 0; i < maxP; i++)
            if(i >= fd->params.size() || !fd->params[i]->isRef) h.positionalRef[i] = 0;
        for(paramDef* p : fd->params){
            auto it = namedRefSeen.find(p->name);
            namedRefSeen[p->name] = (it == namedRefSeen.end() || it->second) && p->isRef;
        }
    }
    for(auto& [k, v] : namedRefSeen) if(v) h.namedRef.insert(k);
    for(auto& [k, v] : namedEnumSeen) if(!v.empty() && !namedEnumConflict.count(k)) h.namedEnum[k] = v;
    return h;
}

// ===============================================================================
// Argument parsing and global-call binding
// ===============================================================================
bglParser::ParsedArgList bglParser::parseCallArgList(functionDef* func, statementBlock* body, const BraceArgHints& braceHints){
    ParsedArgList result;
    // Function arguments are a fresh expression context — clear the outer expected-type so it
    // doesn't bleed into arg resolution. A BARE inline-object aggregate `{ … }` in argument
    // position IS inferred, though: `braceHints` (computed up-front from the callee's candidate
    // signatures) supplies the object-backed class per position/name so the aggregate bakes against
    // the parameter type (the 4th inference slot; see §6.2.1).
    string savedExpectedArgs = currentExpectedType;
    currentExpectedType = "";
    struct BindReset { bool& f; bool saved; ~BindReset(){ f = saved; } } bindReset{readingForBind, readingForBind};
    readingForBind = false;   // an argument is an ordinary read
    size_t posIdx = 0;   // count of POSITIONAL args seen so far (named args don't advance it)
    token firstArgTok = file.getToken();
    while(firstArgTok.isNot(token::parenClose)){
        if(firstArgTok.is(token::comma))
            parsingError(format("Argument {0} is missing: nothing comes before its ','.", result.args.size() + 1));
        // Named argument detection: identifier followed by ':'
        string namedArgName;
        if(firstArgTok.is(eTokenType::name) && file.peekToken(1).is(":")){
            namedArgName = firstArgTok.value;
            file.getToken(); // consume ':'
            firstArgTok = file.getToken(); // read the value expression's first token
        }
        // Bare inline-object aggregate argument: `foo({ … })` — infer the object type from the
        // parameter. The '{' is already consumed (it's firstArgTok), so bake directly.
        if(firstArgTok.is(token::braceOpen)){
            classDef* cls = nullptr;
            if(!namedArgName.empty()){
                string nm = namedArgName;
                transform(nm.begin(), nm.end(), nm.begin(), ::tolower);
                auto it = braceHints.named.find(nm);
                if(it != braceHints.named.end()) cls = it->second;
            } else if(posIdx < braceHints.positional.size()){
                cls = braceHints.positional[posIdx];
            }
            if(cls == nullptr){
                string where = namedArgName.empty() ? format("argument {0}", posIdx + 1)
                                                    : format("argument '{0}'", namedArgName);
                parsingError(format("cannot infer the type of the '{{ … }}' {0}: the parameter is not "
                                    "an object-backed class, is out of range, or overloads disagree here "
                                    "— write '<Type>{{ … }}' to name the type explicitly", where));
            }
            string anonName = format("_bglanon{0}", anonObjectCounter++);
            bakeInlineObjectAggregate(cls, cls->name, anonName, func, body);  // consumes through '}'
            expression* arg = new expression();
            arg->tokens.push_back(anonName);
            arg->resolvedType = cls->name;
            token sep = file.getToken();   // ',' or ')'
            arg->terminator = sep.value;
            result.args.push_back(arg);
            result.namedArgNames.push_back(namedArgName);
            while(result.interpSegmentsPerArg.size() < result.args.size()) result.interpSegmentsPerArg.push_back({});
            if(namedArgName.empty()) posIdx++;
            if(sep.is(token::parenClose)){ currentExpectedType = savedExpectedArgs; return result; }
            firstArgTok = file.getToken();
            continue;
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
            if(namedArgName.empty()) posIdx++;
            if(sep.is(token::parenClose)) break;
            firstArgTok = file.getToken();
        } else {
            if(!namedArgName.empty()){
                string nm = namedArgName;
                transform(nm.begin(), nm.end(), nm.begin(), ::tolower);
                auto it = braceHints.namedEnum.find(nm);
                currentExpectedType = it != braceHints.namedEnum.end() ? it->second : "";
            } else {
                currentExpectedType = posIdx < braceHints.positionalEnum.size() ? braceHints.positionalEnum[posIdx] : "";
            }
            {
                string nm = namedArgName;
                transform(nm.begin(), nm.end(), nm.begin(), ::tolower);
                readingForBind = namedArgName.empty() ? posIdx < braceHints.positionalRef.size() && braceHints.positionalRef[posIdx]
                                                      : braceHints.namedRef.count(nm) > 0;
            }
            expression* arg = parseExpression(firstArgTok, {token::comma, token::parenClose}, func, body);
            readingForBind = false;
            currentExpectedType = "";
            if(arg->resolvedType == "void")
                parsingError(format("'{0}' has no value to pass; a print rule goes in interpolated text "
                    "($\"{{{0}}}\") or stands as a statement.", firstArgTok.originalValue.empty() ? firstArgTok.value : firstArgTok.originalValue));
            result.args.push_back(arg);
            result.namedArgNames.push_back(namedArgName);
            if(namedArgName.empty()) posIdx++;
            if(arg->terminator == token::parenClose) { currentExpectedType = savedExpectedArgs; return result; }
            firstArgTok = file.getToken();
        }
    }
    // A `)` straight after a `,` ends the loop here; an interpolated argument's own `)` breaks out instead.
    if(firstArgTok.is(token::parenClose) && !result.args.empty()) parsingWarning("a trailing comma ends this argument list");
    currentExpectedType = savedExpectedArgs;
    return result;
}

// ── Shared: resolve a global function call ───────────────────────────────────
// Given a function name and parsed argument expressions, finds the best matching
// global function definition.  Priority: exact type > conversion > var fallback.

bglParser::GlobalCallMatch bglParser::resolveGlobalCall(const string& name, const vector<expression*>& args,
                                                         functionDef* func, statementBlock* body){
    GlobalCallMatch result;
    // A local or parameter func<> of the name is nearer than any routine (`func<int,int> cap; cap(4)`).
    if(func != nullptr && (qualifyFromParams(name, func) || qualifyFromBodyLocals(name, body) || qualifyFromAncestorBlocks(name, body)))
        if(string varType = resolveIdentifierType(name, func, body); varType.rfind("func<", 0) == 0)
            if(vector<string> targs = funcTypeArgs(varType); !targs.empty()){
                result.funcVarReturnType = targs[0]; result.varType = varType; return result;
            }
    functionDef* upcastMatch = nullptr;       // every non-exact argument reaches its parameter's class by inheritance
    functionDef* conversionMatch = nullptr;
    functionDef* varFallback = nullptr;
    // Ranks one candidate; true when it is an exact match (nothing can beat it).
    auto consider = [&](functionDef* fd) -> bool {
        if(fd->name != name) return false;
        if(result.nameMatch == nullptr) result.nameMatch = fd;
        size_t req = 0;
        for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
        if(args.size() < req || args.size() > fd->params.size()) return false;
        if(result.arityMatch == nullptr) result.arityMatch = fd;
        bool argsOk = true;
        bool usesVar = false;
        bool needsConversion = false;
        bool onlyUpcasts = true;
        for(size_t i = 0; i < args.size() && argsOk; i++){
            string argType = args[i]->resolvedType;
            string paramType = fd->params[i]->type.name;
            // An arg whose type is `var` or UNKNOWN (empty — e.g. a computed-property read
            // `obj.(p)`) can only be matched SAFELY by the `var`-param overload; assuming it's
            // any concrete type is an order-dependent guess (whichever compatible concrete
            // overload — print(bool)/print(string)/… — happens to be first in the globals list).
            // So: var/unknown arg + `var` param = EXACT (the right, deterministic match);
            // var/unknown arg + concrete param = a conversion (ranked below exact), so the var
            // overload wins. A concrete arg against a `var` param stays the catch-all fallback.
            bool argUnknown = argType.empty() || argType == "var";
            if(paramType == "var"){ if(!argUnknown) usesVar = true; }
            else if(argUnknown) needsConversion = true;
            else if(argType == paramType || objectsClassName(argType) == paramType) {} // exact (an object is its class's)
            else if(isTypeCompatible(argType, paramType) || argFitsByRule(args[i], paramType)){
                needsConversion = true;
                if(!isUpcast(paramType, argType)) onlyUpcasts = false;
            }
            else argsOk = false;
            if(argsOk && !genericValueFits(args[i], paramType, func, body)) argsOk = false;
        }
        if(!argsOk) return false;
        if(usesVar){ if(varFallback == nullptr) varFallback = fd; }
        else if(!needsConversion){ result.match = fd; return true; }
        else if(onlyUpcasts){ if(upcastMatch == nullptr) upcastMatch = fd; }
        else if(conversionMatch == nullptr) conversionMatch = fd;
        return false;
    };
    auto settle = [&]{
        if(result.match == nullptr) result.match = upcastMatch;
        if(result.match == nullptr) result.match = conversionMatch;
        if(result.match == nullptr) result.match = varFallback;
    };
    for(typeDef* g : languageService.globals)
        if(auto* fd = dynamic_cast<functionDef*>(g))
            if(consider(fd)) return result;
    settle();
    // If no global found, check #using imports for a method with the matching name, ranked the same way.
    // This lets `#using bgl.asm` make `get_prop_len(x)` work as a bare call.
    if(result.match == nullptr){
        bool exact = false;
        functionDef* globalArity = result.arityMatch;
        result.arityMatch = nullptr;
        for(classDef* imp : usingImports){
            for(typeMember* m : imp->members)
                if(auto* fd = dynamic_cast<functionDef*>(m); fd && consider(fd)){ exact = true; break; }
            if(exact) break;
        }
        if(!exact)
            for(objectDef* imp : usingObjectImports){
                for(typeMember* m : imp->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m); fd && consider(fd)){ exact = true; break; }
                if(exact) break;
            }
        settle();
        // A member's own arguments are checked when the call is validated; one matching by arity still binds.
        if(result.match == nullptr && result.arityMatch != nullptr) result.match = result.arityMatch;
        if(result.arityMatch == nullptr) result.arityMatch = globalArity;
    }
    // If no global found, check for func<> variable
    if(result.nameMatch == nullptr){
        string varType = resolveIdentifierType(name, func, body);
        result.varType = varType;
        if(varType.rfind("func<", 0) == 0)
            if(vector<string> targs = funcTypeArgs(varType); !targs.empty()) result.funcVarReturnType = targs[0];
    }
    return result;
}

// ── Shared: validate a global function call and report arity/type errors ─────
// Called after resolveGlobalCall; issues parsingError if validation fails.
// Returns the resolved function's return type name.

string bglParser::validateGlobalCall(GlobalCallMatch& gcm, const string& funcName, const vector<expression*>& args){
    size_t argCount = args.size();
    if(!gcm.funcVarReturnType.empty()){
        // A func<> value is called with its function type's parameters (§2.9).
        vector<string> targs = funcTypeArgs(gcm.varType);
        if(targs.size() > 1){
            if(argCount != targs.size() - 1)
                parsingError(format("'{0}' takes {1} argument(s), but {2} were supplied.", funcName, targs.size() - 1, argCount));
            for(size_t i = 0; i < argCount; i++){
                const string& at = args[i]->resolvedType;
                const string& pt = targs[i + 1];
                if(at.empty() || pt == "var" || isTypeCompatible(at, pt)) continue;
                parsingError(format("'{0}' can't take this call: arg {1}: '{2}' is not compatible with parameter type '{3}'.",
                                    funcName, i + 1, typeDisplayName(at), typeDisplayName(pt)));
            }
        }
        return gcm.funcVarReturnType;
    }
    if(gcm.nameMatch == nullptr){
        // Loose mode (#bgl{} content): unresolved function names are assumed to be I6
        // routines defined in the surrounding stream. Return "var" as the inferred type;
        // emission falls through to a literal `name(args)` call which I6 will resolve.
        if(looseIdentifierMode) return "var";
        // The name reached a verb's object, so a function of the verb's name lost to it.
        if(funcName.size() > 9 && strncasecmp(funcName.c_str(), "_bglVerb_", 9) == 0)
            parsingError(format("'{0}' names both a verb and a function; a verb is a global name, so rename one "
                                "of them.", funcName.substr(9)));
        if(!gcm.varType.empty())
            parsingError(format("'{0}' is a variable of type '{1}', not a function, so it can't be called.",
                                funcName, typeDisplayName(gcm.varType)));
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
        string provided, detail;
        for(size_t i = 0; i < args.size(); i++){
            if(i) provided += ", ";
            provided += args[i]->resolvedType.empty() ? "?" : typeDisplayName(args[i]->resolvedType);
        }
        for(typeDef* g : languageService.globals){
            auto* fd = dynamic_cast<functionDef*>(g);
            if(fd == nullptr || fd->name != gcm.nameMatch->name || fd->isPrePassStub || fd->params.size() < args.size()) continue;
            detail += "\n  candidate: " + formatSignature(fd);
            for(size_t i = 0; i < args.size(); i++){
                string argType = args[i]->resolvedType, paramType = fd->params[i]->type.name;
                if(paramType == "var" || argType.empty() || isTypeCompatible(argType, paramType)) continue;
                detail += format("\n    arg {0}: '{1}' is not compatible with parameter '{2} {3}'",
                                 i + 1, typeDisplayName(argType), typeDisplayName(paramType), fd->params[i]->dName());
                break;
            }
        }
        parsingError(format("No overload of function '{0}' accepts these argument types.\n  provided: ({1}){2}",
                            dispName, provided, detail));
    }
    return gcm.match->returnType.name;
}


// ===============================================================================
// Identifier qualification + small lookup utilities
// ===============================================================================
// `$opref(<op>[, <operandType>])` — a callable REFERENCE to one of a type's operators,
// for generic code that must dispatch through the type it was handed.
//
// An operator is referenceable only if it is NOT an emitter: an emitter is inlined text
// and has no address or property to point at. Of the referenceable forms:
//
//   static operator    -> a FREE ROUTINE name; its address is the value. The only form
//                         that can serve a bare-word element (a packed string literal,
//                         float bits), which has nothing to dispatch on.
//   instance operator  -> a PROPERTY name (`operator ==` is mangled to `_opeqeq` at
//                         declaration), sent to the element itself.
//
// Consumers tell them apart with metaclass(op)==Routine — the obj-or-routine idiom
// orLibrary uses — so one value carries either.
//
// Overloads: emitters are filtered out first, which in practice leaves one candidate.
// Where several remain, an operand type matching `preferOperand` wins; if that is still
// ambiguous it is a compile error naming the candidates, since silently taking the first
// is the kind of quiet wrong answer this mechanism exists to avoid.
//
// "" (empty) when the type publishes no referenceable form — the caller emits "0" and the
// container falls back to word comparison, so int/char/object pay nothing.
string bglParser::operatorRef(const string& typeName, const string& opName,
                              const string& preferOperand, bool* isStaticOut){
    if(typeName.empty()) return "";
    auto* cd = languageService.findClass(typeName);
    if(cd == nullptr) return "";
    mangleOverloadSetForReceiver(typeName, opName);   // the names an overload set emits under

    // findMemberInHierarchy walks until the predicate returns true; never returning true
    // makes it visit every member, which is how the candidate list is gathered.
    vector<functionDef*> cands;
    findMemberInHierarchy(cd, [&](typeMember* mm){
        auto* f = dynamic_cast<functionDef*>(mm);
        if(!f || f->name != opName || f->isEmitter) return false;   // emitters are not referenceable
        // The class is searched before its bases, so a base's operator of the same signature is shadowed.
        for(functionDef* c : cands){
            bool same = c->isStatic == f->isStatic && c->params.size() == f->params.size();
            for(size_t i = 0; same && i < f->params.size(); i++) same = c->params[i]->type.name == f->params[i]->type.name;
            if(same) return false;
        }
        cands.push_back(f);
        return false;
    });
    if(cands.empty()) return "";

    if(cands.size() > 1){
        // Type names are stored folded (typeDisplayName restores the declared casing), so the
        // operand named in $opref(op, <type>) is matched case-insensitively. Unnamed, the operand
        // is the type itself: a container's elements are all of it.
        auto fold = [](string v){ transform(v.begin(), v.end(), v.begin(), ::tolower); return v; };
        const string want = fold(preferOperand.empty() ? typeName : preferOperand);
        vector<functionDef*> narrowed;
        for(functionDef* f : cands){
            size_t idx = f->isStatic ? 1 : 0;        // static takes (lhs, rhs); instance takes (rhs)
            if(f->params.size() > idx && fold(f->params[idx]->type.name) == want)
                narrowed.push_back(f);
        }
        if(narrowed.size() == 1) cands = narrowed;
    }
    if(cands.size() > 1){
        string list;
        for(functionDef* f : cands){
            string ops;
            for(size_t i = 0; i < f->params.size(); i++)
                ops += (i ? ", " : "") + typeDisplayName(f->params[i]->type.name);
            list += format("\n    {0}operator {1}({2})", f->isStatic ? "static " : "", opName, ops);
        }
        parsingError(format("ambiguous operator reference: '{0}' has {1} referenceable "
                            "'operator {2}' overloads:{3}\n  Disambiguate by naming the operand "
                            "type, e.g. $opref({2}, <type>).",
                            typeDisplayName(typeName), cands.size(), opName, list));
    }
    functionDef* f = cands.front();
    if(isStaticOut != nullptr) *isStaticOut = f->isStatic;
    if(f->isStatic){
        // Name the routine after the class that DECLARES the operator, not the one asked
        // about. A static emits once, under its declaring class, so an inherited operator
        // referenced through a subclass produced `_bgl_Derived__oplteqgt` for a routine
        // emitted as `_bgl_Base__oplteqgt` — a name I6 rejects outright. This bit both
        // `Type::operator` and $opref, so a sort over a subclass was broken too.
        classDef* declaring = cd == nullptr ? nullptr : cd->declaringClassOf(f);
        return i6Emitter::staticRoutineName(declaring ? declaring : cd, f);
    }
    return f->i6name;                                 // instance: the mangled property name
}


// True when `name` is a LOCAL or PARAMETER in the current scope whose declared type is
// `property` or `var` — i.e. a runtime value that names a property. This is deliberately
// narrower than resolveIdentifierType, which reports "property" for any identifier that is
// a member of ANY class (isKnownPropertyName). That breadth is right for `provides(foo)`
// but useless here: it would make every ordinary method name look like a property value,
// and it would let a file-scope `property foo;` grant `obj.foo` access, which it must not.
bool bglParser::isPropertyValuedLocal(const string& name, functionDef* func, statementBlock* body){
    auto typeQualifies = [](const string& t){ return t == "property" || t == "var"; };
    if(func != nullptr)
        for(paramDef* p : func->params)
            if(p->name == name) return typeQualifies(p->type.name);
    for(statementBlock* b : {body, func != nullptr ? dynamic_cast<statementBlock*>(func->body) : nullptr}){
        if(b == nullptr) continue;
        for(statement* st : b->statements)
            if(auto* vd = dynamic_cast<variableDeclaration*>(st))
                if(vd->name == name) return typeQualifies(vd->type.name);
    }
    // In a lambda, a name it captures from the enclosing routine.
    if(func != nullptr && func->isLambda){
        for(auto& cap : func->captures) if(cap.outerName == name) return typeQualifies(cap.typeName);
        if(func == currentFunc && lambdaOuterFunc != nullptr && lambdaOuterFunc != func)
            return isPropertyValuedLocal(name, lambdaOuterFunc, lambdaOuterBody);
    }
    return false;
}

string bglParser::substituteElemOps(const string& body, const string& elemType,
                                    const string& contextName){
    string out = body;
    size_t at = 0;
    while((at = out.find("$opref", at)) != string::npos){
        // `$oprefReq(...)` is `$opref(...)` plus a diagnostic: the reference still resolves
        // the same way and still substitutes 0 when absent, but the BLR is declaring that a
        // missing one leaves the operation meaningless rather than merely defaulted. Which
        // of the two a call wants is the BLR's knowledge, not the compiler's — sort() needs
        // an ordering, indexOf() is content with identity.
        bool required = out.compare(at + 6, 3, "Req") == 0;
        size_t open = at + 6 + (required ? 3 : 0);
        if(open >= out.size() || out[open] != '('){ at += 6; continue; }
        size_t close = out.find(')', open + 1);
        if(close == string::npos) break;                       // unterminated; leave as-is
        string args = out.substr(open + 1, close - open - 1);
        auto trim = [](string v){ size_t a = v.find_first_not_of(" \t");
                                  size_t b = v.find_last_not_of(" \t");
                                  return a == string::npos ? string() : v.substr(a, b - a + 1); };
        string op = args, operand;
        if(size_t comma = args.rfind(','); comma != string::npos){
            op = trim(args.substr(0, comma));
            operand = trim(args.substr(comma + 1));   // matched verbatim; type names are case-sensitive
        } else op = trim(args);
        // With no operand named, prefer the overload taking the type itself. array<T> asks
        // for `==` in order to compare a T against a T, so a type with several overloads
        // (`==(Money, Money)` and `==(Money, int)`) resolves to the same-type one instead of
        // being reported ambiguous. Only consulted when more than one candidate exists, so
        // single-overload types are unaffected.
        if(operand.empty()) operand = elemType;
        bool isStaticOp = false;
        string rep = operatorRef(elemType, op, operand, &isStaticOp);
        // A slot of a value class owns an instance from the class's pool: the store copies into
        // it through the class's copy operator, and dropping the element returns it.
        if(classDef* vc = owningValueElementClass(elemType); vc != nullptr && !isStaticOp && (op == "=" || op == "deinit")){
            if(op == "="){
                functionDef* copyOp = findAssignOperator(vc, elemType, [](functionDef*){ return true; }, /*allowInherited*/true);
                if(copyOp == nullptr || copyOp->isEmitter)
                    parsingError(format("an element of an array<{0}> is copied in through {0}'s copy operator, "
                        "`operator = ({0} o)`, which '{0}' doesn't declare.", typeDisplayName(elemType)));
                if(copyOp->i6name.empty()) copyOp->i6name = mangleOperatorName(copyOp->name);
                languageService.valueElementCopyOps[vc] = copyOp->i6name;
            }
            languageService.valueElementArrays[vc];   // the emitter writes the routines for every class named here
            rep = (op == "=" ? "_bglValAssign_" : "_bglValFree_") + vc->dName();
        } else if(!isStaticOp && !rep.empty()){
            // A property id can't be told from a routine address at run time (on Glulx either can be a
            // small number), so an instance operator is handed over as a routine that sends it. An
            // instance `=` or `deinit` has no receiver in an empty slot, so the runtime gets none.
            if(op == "=" || op == "deinit") rep = "0";
            else {
                languageService.instanceOpRefs.insert(rep);
                rep = "_bglOpRef_" + rep;
            }
        }
        if(rep.empty()){
            // Only a type Beguile actually emits is worth warning about. The bare-word
            // builtins (int, char, object, …) are `extern`, and for them the word IS the
            // value, so the word-semantics default is the correct answer rather than a
            // silent wrong one. Same predicate emitClass uses to decide what it owns.
            if(required){
                auto* cd = languageService.classOf(elemType);
                if(cd != nullptr && !cd->isExternal && !cd->isEmitterClass && !cd->isAlias)
                    parsingWarning(format(
                        "'{0}' publishes no 'operator {1}', so {2}falls back to word semantics "
                        "— for a class that means comparing object addresses, not values. "
                        "Declare `{3} operator {1} ({0} o)` on '{0}' to give it one, or the "
                        "`static {3} operator {1} ({0} a, {0} b)` form if the operator needs "
                        "both operands as parameters.",
                        typeDisplayName(elemType), op,
                        contextName.empty() ? "" : format("'{0}()' ", contextName),
                        op == "<=>" ? "int" : "bool"));
            }
            rep = "0";
        }
        out.replace(at, close - at + 1, rep);
        at += rep.size();
    }
    return out;
}

void bglParser::rejectNonStaticOnTypeName(const string& headText, classDef* cls, const string& member){
    if(cls == nullptr || member.empty()) return;
    // Only when the head really is a TYPE name — an instance that happens to share the name wins.
    if(!languageService.isClassType(headText) && resolveNamespacedType(headText).empty()) return;
    // So does a local or parameter of that name (§3.10).
    if(currentFunc != nullptr){
        string h = headText; transform(h.begin(), h.end(), h.begin(), ::tolower);
        for(paramDef* p : currentFunc->params) if(p->name == h) return;
        function<bool(statementBlock*)> declares = [&](statementBlock* b){
            if(b == nullptr) return false;
            for(statement* st : b->statements){
                if(auto* vd = dynamic_cast<variableDeclaration*>(st); vd && vd->name == h) return true;
            }
            return false;
        };
        if(declares(dynamic_cast<statementBlock*>(currentFunc->body))) return;
        for(statementBlock* b : activeBlockStack) if(declares(b)) return;
        // And a member of the class or object being written (class scope comes before file scope).
        if(qualifyFromCurrentObject(h) || qualifyFromCurrentClass(h)) return;
    }
    typeMember* found = findMemberInHierarchy(cls, [&](typeMember* m){
        auto* vd = dynamic_cast<variableDeclaration*>(m);
        return vd != nullptr && !vd->isStatic && !vd->isAlias && vd->name == member;
    });
    if(found == nullptr) return;
    parsingError(format("'{0}' is a per-instance member of '{1}', so it cannot be reached through the "
                        "type name '{2}'. Only `static` members belong to the class itself; use an "
                        "instance, or declare '{0}' static.", member, cls->dName(), headText));
}

// Case-insensitive, word-boundary search for a `$token` in raw body text. i6Emitter has its own
// copy but keeps it file-static; Beguile is case-insensitive, so `$I6NAME` must match too.
static size_t findTokenCI(const string& hay, const string& needle, size_t pos){
    for(; pos + needle.size() <= hay.size(); pos++){
        size_t i = 0;
        for(; i < needle.size(); i++)
            if(tolower((unsigned char)hay[pos+i]) != tolower((unsigned char)needle[i])) break;
        if(i < needle.size()) continue;
        size_t after = pos + needle.size();
        if(after < hay.size() && (isalnum((unsigned char)hay[after]) || hay[after] == '_')) continue;
        return pos;
    }
    return string::npos;
}

// ─── `$i6Name(<path>[(<types>)])` — the identifier Inform 6 knows a declaration by ───
//
// Emitter bodies are raw I6, so a body that wants to call a Beguile routine has to spell that
// routine's emitted name itself. That spelling is not the author's to know: `as` renames it, a
// `static` method is mangled to `_bgl_<class>_<method>`, and the mangling grows a parameter-type
// discriminator the moment a second overload of that name appears. `$i6Name` asks the compiler
// instead. The parenthesized type list selects among overloads, exactly as `Type::operator ==(int)`
// and `$opref(==, Money)` already do.
//
// An emitter is never referenceable — it has no routine and no constant — so naming one is an
// error rather than a silent `0`. This mirrors `$opref`, which yields 0 only because a missing
// operator has a sound word-semantics default; a missing name has none.
string bglParser::resolveI6Name(const string& spec){
    auto trim = [](string v){ size_t a = v.find_first_not_of(" \t\n\r");
                              size_t b = v.find_last_not_of(" \t\n\r");
                              return a == string::npos ? string() : v.substr(a, b - a + 1); };
    // Split the optional overload signature off the path: `bgl.asm.callf(int, int)`.
    string path = trim(spec), sig;
    bool hasSig = false;
    if(size_t p = path.find('('); p != string::npos){
        if(path.empty() || path.back() != ')'){
            parsingError(format("$i6Name({0}) — unterminated overload signature; expected a closing ')'", spec));
            return "";
        }
        sig     = trim(path.substr(p + 1, path.size() - p - 2));
        path    = trim(path.substr(0, p));
        hasSig  = true;
    }
    vector<string> sigTypes;
    if(hasSig && !sig.empty()){
        size_t from = 0;
        while(true){
            size_t c = sig.find(',', from);
            sigTypes.push_back(trim(sig.substr(from, c == string::npos ? string::npos : c - from)));
            if(c == string::npos) break;
            from = c + 1;
        }
    }
    if(path.empty()){ parsingError("$i6Name() — no declaration named"); return ""; }
    // The parenthesized list is a SIGNATURE — type names selecting one overload, as in
    // `Type::operator ==(int)` (§4.15) — not an argument list. Passing values used to fall through
    // the overload match and be reported as a missing member, which pointed at the wrong thing.
    for(const string& t : sigTypes){
        if(t.empty()){
            parsingError(format("$i6Name({0}) — empty entry in the overload signature; it takes type "
                                "names, e.g. $i6Name({1}(int, int))", spec, path));
            return "";
        }
        string probe = t;
        transform(probe.begin(), probe.end(), probe.begin(), ::tolower);
        if(languageService.getType(probe).name.empty()){
            parsingError(format("$i6Name({0}) — '{1}' is not a type name. The parentheses after a "
                                "path hold the SIGNATURE that selects one overload, not the "
                                "arguments: write $i6Name({2}(int, int)), then supply the arguments "
                                "outside — $i6Name({2}(int, int))($a, $b).", spec, t, path));
            return "";
        }
    }
    // Declarations are registered under their folded name (Beguile is case-insensitive), so every
    // lookup below folds too. `shown` keeps the author's spelling for the diagnostics.
    const string shown = path;
    transform(path.begin(), path.end(), path.begin(), ::tolower);

    // Does this function's parameter list match the signature the author named? An empty
    // signature matches anything, so a single-overload lookup never has to spell one out.
    auto sigMatches = [&](functionDef* fd){
        if(!hasSig) return true;
        if(fd->params.size() != sigTypes.size()) return false;
        for(size_t i = 0; i < sigTypes.size(); i++){
            string a = fd->params[i]->type.name, b = sigTypes[i];
            transform(a.begin(), a.end(), a.begin(), ::tolower);
            transform(b.begin(), b.end(), b.begin(), ::tolower);
            if(a != b) return false;
        }
        return true;
    };
    auto describe = [&](functionDef* fd){
        string d = fd->dName() + "(";
        for(size_t i = 0; i < fd->params.size(); i++){ if(i) d += ", "; d += typeDisplayName(fd->params[i]->type.name); }
        return d + ")";
    };
    auto notReferenceable = [&](const string& what){
        parsingError(format("$i6Name({0}) — '{1}' is an emitter; it has no I6 name because no routine "
                            "is emitted for it. Use $i6Expr(...) to expand it inline instead.", spec, what));
        return string();
    };

    size_t dot = path.rfind('.');
    if(dot == string::npos){
        // Top-level: a global routine, a global variable, or a named object/class/enum.
        if(auto* fd = languageService.findGlobalAs<functionDef>(path)){
            if(fd->isEmitter) return notReferenceable(fd->dName());
            return fd->i6name.empty() ? fd->name : fd->i6name;
        }
        // Named declarations before plain globals: an object instance is registered in BOTH
        // tables, and only the objectDef carries its `as` alias.
        typeDef& td = languageService.getType(path);
        if(auto* od = dynamic_cast<objectDef*>(&td)) return od->i6name.empty() ? od->name : od->i6name;
        if(auto* cd = dynamic_cast<classDef*>(&td))  return cd->i6Name();
        if(auto* g = languageService.findGlobal(path))
            return g->i6name.empty() ? g->name : g->i6name;
        if(!td.name.empty())                         return td.i6name.empty() ? td.name : td.i6name;
        parsingError(format("$i6Name({0}) — no declaration named '{1}' is in scope", spec, shown));
        return "";
    }

    // Dotted: resolve the head to a type, then find the member on it.
    string head = trim(path.substr(0, dot)), member = trim(path.substr(dot + 1));
    string headType;
    // A class named by its own name is the type itself, not a value of type class.
    if(typeDef& ht = languageService.getType(head); dynamic_cast<classDef*>(&ht) != nullptr
                                                     && dynamic_cast<objectDef*>(&ht) == nullptr)
        headType = ht.name;
    if(headType.empty()) headType = resolvePathType(head, nullptr, nullptr, member);
    if(headType.empty()) headType = head;          // the head may already BE a type name
    // An enum value: an extern enum's is an I6 word of its own; a Beguile enum's is its number.
    if(auto* ed = languageService.findEnum(headType))
        for(enumValueDef* ev : ed->namedValues)
            if(ev->name == member) return ed->isExternal ? member : to_string(ev->value);
    classDef* cls = getDispatchClass(headType);
    if(cls == nullptr){
        parsingError(format("$i6Name({0}) — '{1}' does not name a type with members", spec, head));
        return "";
    }
    vector<functionDef*> candidates;
    findMemberInHierarchy(cls, [&](typeMember* m){
        if(auto* fd = dynamic_cast<functionDef*>(m))
            if(fd->name == member && sigMatches(fd)) candidates.push_back(fd);
        return false;                              // visit every member; we collect rather than pick
    });
    if(candidates.size() > 1){
        string list;
        for(functionDef* fd : candidates){ if(!list.empty()) list += ", "; list += describe(fd); }
        parsingError(format("$i6Name({0}) — '{1}' is ambiguous between {2}. Name the parameter types "
                            "to select one, e.g. $i6Name({1}(int, int)).", spec, shown, list));
        return "";
    }
    if(candidates.size() == 1){
        functionDef* fd = candidates.front();
        if(fd->isEmitter) return notReferenceable(shown);
        // Settle the overload set's names FIRST, exactly as an ordinary call site does before it
        // reads i6name. The mangler adds an arity component for an overloaded name
        // (`_bgl_Money_scale_2_int_int`), which staticRoutineName alone does not — computing the
        // name here instead produced `_bgl_Money_scale_int_int`, a routine that is never emitted.
        mangleOverloadSetForReceiver(headType, fd->name);
        // staticRoutineName reads i6name and adds the `_bgl_<class>_` prefix, so it is still the
        // right call — it just has to run AFTER the mangler has settled the overload's i6name.
        if(fd->isStatic){
            classDef* hc = getDispatchClass(headType);
            classDef* owner = hc ? hc->declaringClass(fd) : nullptr;
            return i6Emitter::staticRoutineName(owner ? owner : hc, fd);
        }
        return fd->i6name.empty() ? fd->name : fd->i6name;   // instance method → its property name
    }
    // Not a method — a data member, whose emitted property name `as` may have renamed.
    string prop = memberI6Name(headType, member);
    if(prop != member || findMemberInHierarchy(cls, [&](typeMember* m){
           auto* vd = dynamic_cast<variableDeclaration*>(m); return vd != nullptr && vd->name == member; }) != nullptr)
        return prop;
    parsingError(format("$i6Name({0}) — '{1}' has no member '{2}'", spec, typeDisplayName(headType), member));
    return "";
}

// ─── `$i6Expr(<beguile expression>)` — expand another emitter (or any call) inline ───
//
// An emitter body is raw I6, so it cannot call another emitter: the callee has no routine to name.
// `$i6Expr` parses its payload as ordinary Beguile and substitutes the I6 that expression emits, so
// a body can reach the pure-expression opcode wrappers, inherit their `##if` target gating, and get
// overload resolution — none of which a textual body can do for itself.
//
// Binding, not text. The payload is parsed with the body's tokens standing for TYPED values: `$v`
// denotes a value of `v`'s declared type whose emitted text is the argument's I6. Substituting the
// text first and parsing the result cannot work — the substituted text is I6, not Beguile
// (`$self` is routinely something like `_glktmpsizebuf-->(0+1)`), and it would also throw away the
// types that make the type-checking and overload resolution possible.
//
// Mechanism: each bound token is rewritten to a unique placeholder identifier declared, at its
// Beguile type, in a synthetic scope handed to parseExpression. The parse therefore sees a
// well-typed expression; the placeholders are swapped back for the bound I6 text afterwards.
// The `)` closing the `(` at `open`, skipping parentheses inside string and character literals.
static size_t matchingParen(const string& s, size_t open){
    int depth = 0;
    for(size_t i = open; i < s.size(); i++){
        char c = s[i];
        if(c == '"' || c == '\''){
            for(i++; i < s.size() && s[i] != c; i++) if(s[i] == '\\') i++;
            continue;
        }
        if(c == '(') depth++;
        else if(c == ')' && --depth == 0) return i;
    }
    return string::npos;
}

string bglParser::substituteI6Exprs(const string& body, const emitterBindings& b,
                                    functionDef* func, statementBlock* enclosing){
    string out = body;
    size_t at = 0;
    while((at = findTokenCI(out, "$i6Expr", at)) != string::npos){
        size_t open = at + 7;
        while(open < out.size() && (out[open] == ' ' || out[open] == '\t')) open++;
        if(open >= out.size() || out[open] != '('){ at += 7; continue; }
        size_t close = matchingParen(out, open);
        if(close == string::npos){ parsingError("$i6Expr( — unterminated; expected a closing ')'"); break; }
        string payload = out.substr(open + 1, close - open - 1);

        // Bind each token to a placeholder local of the right Beguile type.
        statementBlock* scope = new statementBlock();
        vector<pair<string,string>> restore;        // placeholder → the token it stands for
        int slot = 0;
        vector<string> untyped;      // present at this site, but with no Beguile type to parse against
        // Only the TYPE is needed: the payload is parsed against a placeholder of that type, and the
        // placeholder is handed back as the token afterwards. The argument's TEXT is never needed
        // here, which is precisely what lets this run where the arguments are not yet bound.
        auto bind = [&](const string& token, const string& type){
            if(findTokenCI(payload, token, 0) == string::npos) return;
            if(type.empty()){ untyped.push_back(token); return; }
            // Lowercase: Beguile folds identifiers, so a mixed-case placeholder is registered
            // under one spelling and looked up under another — the receiver of a method call in
            // the payload then failed to resolve ("Unknown variable '_bglxpr0_'").
            string ph = format("_bglxpr{0}_", slot++);
            // An array receiver needs its element type to be subscripted, which a plain variable of
            // the base type ("array") does not carry.
            bool isArray = token != "$host" && (isWordArrayType(type) || type == "bytearray"
                                                || type.rfind("rawarray", 0) == 0);
            auto* vd = isArray ? new arrayDeclaration() : new variableDeclaration();
            if(isArray){
                auto* ad = static_cast<arrayDeclaration*>(vd);
                ad->elementType = type == "bytearray" ? "char" : arrayElemType(type);
                if(ad->elementType.empty() && b.elemType) ad->elementType = *b.elemType;
                ad->isRaw = type.rfind("rawarray", 0) == 0;
                if(b.prop && *b.prop != "0" && b.prop->find('<') == string::npos) i6ExprMemberArrays.insert(ph);
                else i6ExprMemberArrays.erase(ph);
            }
            vd->name = ph;
            vd->type = languageService.getType(type);
            if(vd->type.name.empty()) vd->type.name = type;
            scope->statements.push_back(vd);
            payload = i6Emitter::replaceWord(payload, token, ph);
            // Restore to the TOKEN, not to the argument's text. The final text of an argument is
            // only knowable at emit time — i6Emitter::exprText applies the per-routine display-name,
            // spill and local-rename maps — so substituting it here would emit `viaamp` where the
            // deferred pass emits `viaAmp`. Handing the token back instead leaves $i6Expr's output
            // in the same shape as any other emitter body, so whichever pass would have filled the
            // token still does, and $i6Expr works in statement and expression position alike.
            restore.push_back({ph, token});
        };
        // A site that names no receiver type still has one: the class or object declaring the emitter.
        string selfType = b.selfType;
        if(selfType.empty() && b.fn != nullptr)
            for(typeDef* td : languageService.objectTypes){
                auto* cd = dynamic_cast<classDef*>(td);
                if(cd != nullptr && find(cd->members.begin(), cd->members.end(), b.fn) != cd->members.end()){
                    selfType = cd->name; break;
                }
            }
        if(selfType.empty() && b.fn != nullptr)
            for(typeDef* td : languageService.objectInstances)
                if(auto* od = dynamic_cast<objectDef*>(td);
                   od != nullptr && find(od->members.begin(), od->members.end(), b.fn) != od->members.end()){
                    selfType = od->name; break;
                }
        if(b.self) bind("$self", selfType);
        if(b.val)  bind("$val",  selfType);
        if(b.host) bind("$host", selfType);
        // Every parameter, whether or not this site has its argument yet: a body reached as a bare
        // statement stages its `$param` tokens for the deferred pass, and $i6Expr has to parse
        // against the same signature there as anywhere else.
        // A `var` parameter is parsed as the caller's argument type, when the site knows it: `var`
        // says "any value", so the argument is the only thing that says which value this is. A
        // literal argument keeps its literal pseudo-type, which overload resolution already ranks.
        if(b.fn != nullptr)
            for(size_t i = 0; i < b.fn->params.size(); i++){
                paramDef* p = b.fn->params[i];
                string type = p->type.name;
                if(type == "var" && i < b.argTypes.size() && !b.argTypes[i].empty()) type = b.argTypes[i];
                bind("$" + p->name, type);
            }
        // A token the caller never bound is not substitutable here, and leaving it would reach I6
        // as a literal `$name`. Say so where the author can act on it.
        if(size_t stray = payload.find('$'); stray != string::npos){
            size_t e = stray + 1;
            while(e < payload.size() && (isalnum((unsigned char)payload[e]) || payload[e] == '_')) e++;
            string tok = payload.substr(stray, e - stray);
            string src = out.substr(open + 1, close - open - 1);
            // Two different failures wear the same shape, and the fix differs: an unknown token is
            // the author's typo, a known-but-untyped one is a use site that never supplied the
            // receiver's type — nothing the BLR author can do about it from the body.
            if(find(untyped.begin(), untyped.end(), tok) != untyped.end())
                parsingError(format("$i6Expr({0}) — '{1}' is available here but its Beguile type is "
                                    "not, so the expression cannot be type-checked. Reaching the "
                                    "receiver through $i6Expr is not supported at this kind of use "
                                    "site; use the plain token outside $i6Expr instead.", src, tok));
            else
                parsingError(format("$i6Expr({0}) — '{1}' is not bound at this use site; $i6Expr can "
                                    "only reference this emitter's own parameters and receiver.",
                                    src, tok));
            out.replace(at, close - at + 1, "");
            continue;
        }

        // Sub-parse. The lexer keeps a stack of streams, so pushing one mid-parse is safe, but its
        // scratch state (a peeked token, the brace depth) belongs to the OUTER stream and has to be
        // put back exactly as it was.
        bool  savedHasPending  = file.hasPendingToken;
        token savedPending     = file.pendingToken;
        // The previous token decides how the outer stream lexes what follows (`.w` after `)` is a
        // member, not a dictionary word), so the sub-parse's last token must not leak out.
        eTokenType savedPrevType  = file.prevTokenType;
        string     savedPrevValue = file.prevTokenValue;
        auto restoreLexer = [&]{
            file.hasPendingToken = savedHasPending; file.pendingToken = savedPending;
            file.prevTokenType = savedPrevType; file.prevTokenValue = savedPrevValue;
        };
        string emitted;
        size_t injectionsBefore = pendingInjections.size();
        // The payload is an expression, not a statement, so give the parser the terminator it
        // expects rather than letting it read off the end of the stream.
        file.openText(payload + ";", "$i6Expr", 0);
        file.hasPendingToken = false;
        try {
            file.bleedSpaces();
            token first = file.getToken();
            // Placeholders live in `scope`; with none (an island) fall through to the enclosing
            // routine's own scope so the payload can name its locals.
            // A void call is a payload too: an emitter body uses it as a statement (`$i6Expr(::print($v))`).
            bool savedAllowVoid = allowVoidReturnExpr;
            allowVoidReturnExpr = true;
            expression* e = scope->statements.empty() && enclosing != nullptr
                          ? parseExpression(first, {";"}, func, enclosing)
                          : parseExpression(first, {";"}, func, scope);
            allowVoidReturnExpr = savedAllowVoid;
            emitted = e != nullptr ? e->text() : "";
            file.bleedSpaces();
            if(file.peekChar() != EOF)
                parsingError(format("$i6Expr({0}) holds more than one expression; write one $i6Expr for each.",
                                    out.substr(open + 1, close - open - 1)));
        } catch(...) {
            file.close();
            restoreLexer();
            throw;
        }
        file.close();
        restoreLexer();

        for(auto& [ph, text] : restore) emitted = i6Emitter::replaceWord(emitted, ph, text);
        // Set-up statements (a `??` temporary) go into the caller's routine, where no token is
        // filled later: they take the bound text itself.
        for(size_t n = injectionsBefore; n < pendingInjections.size(); n++){
            auto* raw = dynamic_cast<i6RawNode*>(pendingInjections[n]);
            if(raw == nullptr) continue;
            for(auto& [ph, tokenName] : restore){
                if(findTokenCI(raw->text, ph, 0) == string::npos) continue;
                optional<string> text;
                if(tokenName == "$self") text = b.self;
                else if(tokenName == "$val") text = b.val;
                else if(tokenName == "$host") text = b.host;
                else if(b.fn != nullptr)
                    for(size_t i = 0; i < b.fn->params.size() && i < b.args.size(); i++)
                        if("$" + b.fn->params[i]->name == tokenName) text = b.args[i];
                if(!text) parsingError(format("$i6Expr({0}) — '{1}' has no value at this use site to hold in a temporary.",
                                              out.substr(open + 1, close - open - 1), tokenName));
                else raw->text = i6Emitter::replaceWord(raw->text, ph, *text);
            }
        }
        out.replace(at, close - at + 1, emitted);
        at += emitted.size();
    }
    return out;
}

// Resolve the two Beguile-reaching tokens inside a raw-I6 island (§15.2). An island is raw text
// exactly as an emitter body is, and wants the same two escapes — but it has no receiver and no
// parameters, so nothing else is bound. `#bgl` remains the way to run Beguile STATEMENTS in an
// island; these two are for naming a declaration and for inlining one expression.
string bglParser::resolveIslandTokens(const string& raw, functionDef* func, statementBlock* enclosing){
    if(raw.find('$') == string::npos) return raw;      // the common case: no token, no work
    emitterBindings none;
    // An I6 `!` comment runs to the end of its line and is left as written, tokens and all.
    string out, code;
    auto flush = [&]{ if(!code.empty()){ out += substituteI6Names(substituteI6Exprs(code, none, func, enclosing)); code.clear(); } };
    char quote = 0;
    for(size_t i = 0; i < raw.size(); i++){
        char c = raw[i];
        if(quote){ code += c; if(c == quote) quote = 0; continue; }
        if(c == '"' || c == '\''){ quote = c; code += c; continue; }
        if(c == '!'){
            flush();
            size_t e = raw.find('\n', i);
            if(e == string::npos) e = raw.size();
            out += raw.substr(i, e - i);
            i = e - 1;
            continue;
        }
        code += c;
    }
    flush();
    return out;
}

// Replace every `$i6Name(...)` in an emitter body. Parenthesis-aware, so an overload signature
// inside the payload does not terminate the token early.
string bglParser::substituteI6Names(const string& body){
    string out = body;
    size_t at = 0;
    while((at = findTokenCI(out, "$i6Name", at)) != string::npos){
        size_t open = at + 7;
        while(open < out.size() && (out[open] == ' ' || out[open] == '\t')) open++;
        if(open >= out.size() || out[open] != '('){
            parsingError("$i6Name takes the name in parentheses: `$i6Name(path)`.");
            at += 7; continue;
        }
        size_t close = matchingParen(out, open);
        if(close == string::npos){
            parsingError("$i6Name( — unterminated; expected a closing ')'");
            break;
        }
        string rep = resolveI6Name(out.substr(open + 1, close - open - 1));
        out.replace(at, close - at + 1, rep);
        at += rep.size();
    }
    return out;
}

// The I6 property name a member is emitted under. `Type member as <i6name>;` lets a member's
// emitted identifier differ from its Beguile name, which is how a member dodges an I6 symbol
// clash — including one of the compiler's reserved additive properties such as `name`. The
// declaration honoured the alias but access sites did not, emitting `obj.<beguileName>` for a
// property declared under another name, which I6 rejects outright.
string bglParser::withMemberI6Name(const string& written, string qualified, functionDef* func, statementBlock* body){
    if(func == nullptr || written.find('.') == string::npos || qualified.find('.') == string::npos) return qualified;
    auto split = [](const string& t){ vector<string> v; size_t a = 0, b; while((b = t.find('.', a)) != string::npos){ v.push_back(t.substr(a, b - a)); a = b + 1; } v.push_back(t.substr(a)); return v; };
    vector<string> w = split(written), q = split(qualified);
    if(q.size() < w.size()) return qualified;
    // Every member along the path, not just the last, is emitted under its I6 name (`o._out_part._in_w`).
    // The written path's members are the qualified one's last segments; its head may have grown (`self.`).
    string prefix = w[0];
    for(size_t i = 1; i < w.size(); i++){
        string type = i == 1 ? resolveIdentifierType(w[0], func, body) : resolvePathType(prefix, func, body);
        // A verb's name types as its action; its members are the verb object's.
        if(i == 1 && type == "verb"){
            string head = w[0]; transform(head.begin(), head.end(), head.begin(), ::tolower);
            for(verbObjectDef* v : languageService.verbs) if(v->name == head){ type = head; break; }
        }
        string aliased = memberI6Name(type, w[i]);
        size_t j = q.size() - (w.size() - i);
        if(aliased != w[i] && strcasecmp(q[j].c_str(), w[i].c_str()) == 0) q[j] = aliased;
        prefix += "." + w[i];
    }
    string out = q[0];
    for(size_t i = 1; i < q.size(); i++) out += "." + q[i];
    return out;
}

void bglParser::rejectMemberNamedLikeClass(const string& name, const string& shown){
    classDef* cls = languageService.findClass(name);
    if(cls == nullptr || cls->isEmitterClass || cls->isAlias || cls->isPrimitive) return;
    string i6 = cls->i6Name();
    transform(i6.begin(), i6.end(), i6.begin(), ::tolower);
    if(i6 != name) return;
    parsingError(format("Member '{0}' has the same name as class '{1}'. Inform 6 keeps classes and properties "
                        "in one namespace, ignoring case, so rename the member or give it an I6 name with "
                        "`asI6`.", shown, cls->dName()));
}

string bglParser::memberI6NameFor(const string& name, const string& i6alias, bool ownerIsExtern){
    if(!i6alias.empty() || ownerIsExtern) return i6alias;
    rejectMemberNamedLikeClass(name, name);
    return clashRenamedMember(name);
}

string bglParser::propertyI6Name(const string& name){
    string renamed = clashRenamedMember(name);
    return renamed.empty() ? name : renamed;
}

// Inform 6 reads these as operators, so `obj.ofclass(…)` or `obj.has` can't name a member.
bool bglParser::isI6OperatorWord(const string& name){
    static const std::set<string> words = {"has", "hasnt", "in", "notin", "ofclass", "provides", "or"};
    return words.count(name) > 0;
}

string bglParser::clashRenamedMember(const string& name){
    if(isI6OperatorWord(name)) return "_m_" + name;
    // Globals Inform 6 declares for itself before any source is read.
    static const std::set<string> i6SetupGlobals = {"sender", "self", "temp_global", "temp__global2",
        "temp__global3", "temp__global4", "sw__var", "sys__glob0", "sys__glob1", "sys__glob2", "sys_statusline_flag"};
    if(i6SetupGlobals.count(name)) return "_m_" + name;
    // Inform 6's system functions can't be used as a property's name either.
    static const std::set<string> i6SystemFunctions = {"child", "children", "elder", "eldest", "indirect",
        "metaclass", "parent", "random", "sibling", "younger", "youngest"};
    if(i6SystemFunctions.count(name)) return "_m_" + name;
    if(name == "main") return "_m_" + name;   // every I6 library defines a `Main` routine
    // Inform 6 keeps properties, globals, objects and routines in one namespace. A `property` global
    // names this very property; an emitter emits no symbol.
    for(typeDef* g : languageService.globals){
        if(g->name != name) continue;
        if(auto* vd = dynamic_cast<variableDeclaration*>(g)){ if(vd->type.name != "property") return "_m_" + name; }
        else if(auto* fd = dynamic_cast<functionDef*>(g)){ if(!fd->isEmitter) return "_m_" + name; }
    }
    if(languageService.findObjectType(name) != nullptr) return "_m_" + name;
    for(verbObjectDef* v : languageService.verbs) if(v->name == name) return "_m_" + name;
    return "";
}

// A global the binding declares under this name that emits an I6 symbol: a variable (not a property),
// a routine or an object.
// `(T)x` reaching a strict ancestor of x's type. An object is its own type (§11.2), a subtype of its
// class, so casting it to that class is an upcast too: `((Block)self).report()` in an object's override.
bool bglParser::isUpcast(const string& castType, const string& actualType){
    classDef* to = getDispatchClass(castType);
    classDef* from = getDispatchClass(actualType);
    if(isAncestorClass(to, from)) return true;
    objectDef* od = languageService.findObjectType(actualType);
    return od != nullptr && to != nullptr && to == from && languageService.findClass(actualType) == nullptr;
}

// The class of a named object, when `t` is the object's own type (§11.2); "" otherwise.
string bglParser::objectsClassName(const string& t){
    if(languageService.findClass(t) != nullptr) return "";
    objectDef* od = languageService.findObjectType(t);
    return od != nullptr && od->objectClass != nullptr ? od->objectClass->name : "";
}

bool bglParser::namesLibraryMethod(const string& name){
    auto has = [&](const vector<typeMember*>& members){
        for(typeMember* m : members)
            if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->name == name && !fd->isEmitter && !fd->isStatic
               && fd->src.file.find("beguiLib") != string::npos)
                return true;
        return false;
    };
    for(typeDef* g : languageService.globals){
        if(auto* cd = dynamic_cast<classDef*>(g); cd && has(cd->members)) return true;
        if(auto* od = dynamic_cast<objectDef*>(g); od && has(od->members)) return true;
    }
    return false;
}

bool bglParser::namesLibraryGlobal(const string& name){
    // A name the library's own Beguile code also defines as a routine (`print`) is one the compiler
    // calls by name: overloading it doesn't move the methods.
    for(typeDef* g : languageService.globals)
        if(auto* fd = dynamic_cast<functionDef*>(g); fd && fd->name == name && !fd->isExternal
           && fd->src.file.find("beguiLib") != string::npos)
            return false;
    for(typeDef* g : languageService.globals){
        if(g->name != name) continue;
        // A variable or const (the binding's or the program's), as Inform 6 keeps it in the same namespace.
        if(auto* vd = dynamic_cast<variableDeclaration*>(g)){
            if(vd->type.name == "property" || vd->isNamespaceAlias()) continue;
            if(vd->isExternal || vd->src.file.find("beguiLib") == string::npos) return true;
        }
        else if(auto* fd = dynamic_cast<functionDef*>(g)){
            // A routine the binding declares, or one of the program's own (not the library's Beguile code).
            if(!fd->isEmitter && (fd->isExternal || (!fd->src.file.empty() && fd->src.file.find("beguiLib") == string::npos)))
                return true;
        }
        else if(auto* od = dynamic_cast<objectDef*>(g)){ if(dynamic_cast<verbObjectDef*>(od) == nullptr) return true; }
    }
    return false;
}

// `Ping.perform()` in a body with a member named `ping`: the member's type has no such method and a
// verb of that name has, so the call is the verb's.
bool bglParser::verbShadowedByMember(const string& name, const string& memberType, const string& methodName){
    string low = name, meth = methodName;
    transform(low.begin(), low.end(), low.begin(), ::tolower);
    transform(meth.begin(), meth.end(), meth.begin(), ::tolower);
    bool isVerb = false;
    for(verbObjectDef* v : languageService.verbs) if(v->name == low){ isVerb = true; break; }
    if(!isVerb) return false;
    classDef* mc = getDispatchClass(memberType);
    return mc == nullptr || findMemberInHierarchy(mc, [&](typeMember* m){
        return dynamic_cast<functionDef*>(m) != nullptr && m->name == meth; }) == nullptr;
}

std::string bglParser::memberI6Name(const std::string& recvTypeName, const std::string& memberName){
    if(recvTypeName.empty() || memberName.empty()) return memberName;
    typeDef& td = languageService.getType(recvTypeName);
    auto pick = [&](vector<typeMember*>& members) -> string {
        for(typeMember* m : members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                if(vd->name == memberName && !vd->i6name.empty()) return vd->i6name;
                // A member not yet declared (used above its class) gets the name its declaration will.
                if(vd->name == memberName && vd->isPrePassStub && !vd->isExternal)
                    if(string r = clashRenamedMember(memberName); !r.empty()) return r;
            }
        return "";
    };
    if(auto* od = dynamic_cast<objectDef*>(&td)){
        if(string a = pick(od->members); !a.empty()) return a;
        for(classDef* c = od->objectClass; c != nullptr; c = c->baseClasses.empty() ? nullptr : c->baseClasses[0])
            if(string a = pick(c->members); !a.empty()) return a;
    }
    if(auto* cd = dynamic_cast<classDef*>(&td))
        for(classDef* c = cd; c != nullptr; c = c->baseClasses.empty() ? nullptr : c->baseClasses[0])
            if(string a = pick(c->members); !a.empty()) return a;
    // A verb's name names its action too, so the verb object is found among the verbs (by either name).
    for(verbObjectDef* v : languageService.verbs){
        string i6 = v->i6name; transform(i6.begin(), i6.end(), i6.begin(), ::tolower);
        if(v->name == recvTypeName || (!i6.empty() && i6 == recvTypeName))
            if(string a = pick(v->members); !a.empty()) return a;
    }
    return memberName;
}

// Whether a member array carries Beguile's trailing length slot. Mirrors the emitter's rule
// exactly, since the two must agree on the layout: `rawArray<T>` opts out, and
// `array<dictionaryWord>` stays bare because I6 reads it as parser data. Consulted by the
// inline size()/length() fast path, which cannot route through _bglArray when <array> is
// absent and so has to know the layout itself.
// Whether a member array is declared `ref` — it then holds a POINTER to an array owned
// elsewhere, so every operation addresses it as a value (obj.prop, 0) rather than as inline
// property data (obj, prop). Addressing it the member way indexes the slot the pointer lives
// in. Same resolution shape as memberArrayIsTracked, including `self` inside a body.
bool bglParser::memberArrayIsRef(const std::string& ownerName, const std::string& propName,
                                 functionDef* func, statementBlock* body){
    auto check = [&](vector<typeMember*>& members) -> int {   // -1 unknown, 0 no, 1 yes
        for(typeMember* m : members)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(m); ad != nullptr && !ad->isPrePassStub)
                if(ad->name == propName || (!ad->i6name.empty() && ad->i6name == propName)) return ad->isRefLocal ? 1 : 0;
        return -1;
    };
    // `self` in an inline accessor is the accessor's own class, not the object hosting it.
    if(ownerName == "self" && currentClass != nullptr && currentClass->name.rfind("_bglaccessor", 0) == 0){
        for(classDef* c = currentClass; c != nullptr; c = c->baseClasses.empty() ? nullptr : c->baseClasses[0]){
            int rc = check(c->members); if(rc >= 0) return rc == 1;
        }
    }
    // `self` in an object's body is that object itself; looking it up again by name can find another
    // symbol of that name (a verb's action).
    if(ownerName == "self" && currentObject != nullptr){
        int r = check(currentObject->members); if(r >= 0) return r == 1;
        for(classDef* c = currentObject->objectClass; c != nullptr; c = c->baseClasses.empty() ? nullptr : c->baseClasses[0]){
            int rc = check(c->members); if(rc >= 0) return rc == 1;
        }
    }
    string owner = ownerName == "self" ? (currentObject ? currentObject->name
                                        : (currentClass ? currentClass->name : string()))
                                      : ownerName;
    if(owner.empty()) return false;
    string rt = resolveIdentifierType(owner, func, body);
    typeDef& td = languageService.getType(rt.empty() ? owner : rt);
    if(auto* od = dynamic_cast<objectDef*>(&td)){
        int r = check(od->members); if(r >= 0) return r == 1;
        for(classDef* c = od->objectClass; c != nullptr; c = c->baseClasses.empty() ? nullptr : c->baseClasses[0]){
            int rc = check(c->members); if(rc >= 0) return rc == 1;
        }
    }
    if(auto* cd = dynamic_cast<classDef*>(&td))
        for(classDef* c = cd; c != nullptr; c = c->baseClasses.empty() ? nullptr : c->baseClasses[0]){
            int rc = check(c->members); if(rc >= 0) return rc == 1;
        }
    return false;
}

// A RAW member array — `rawArray<T>`, or `array<dictionaryWord>`, whose layout I6 owns — is
// inline property data with no length word, so its extent is fixed by the property and nothing
// can grow or shrink it. The _bglArray helpers all assume the tracked layout and maintain a
// trailing length slot that is not there: setLength overwrites the last ELEMENT, and pop/clear
// walk past the end ("tried to write outside memory using -->"). Reject at compile time instead,
// mirroring the rawArray-pointer guard in bindMethodCall.
bool bglParser::rejectRawMemberLengthOp(const std::string& owner, const std::string& prop,
                                        const std::string& methName, functionDef* func,
                                        statementBlock* body){
    // Every operation whose body reads or writes the length word, plus the wrappers that
    // delegate to one. size()/length() are absent deliberately: the compiler lowers those to
    // `obj.#prop/WORDSIZE` at the call site, so they need no length word and stay available.
    static const std::set<std::string> needsLengthWord = {
        "length =", "clear", "freeall", "append", "insert", "prepend", "remove", "removevalue",
        "push", "pop", "dequeue", "enqueue", "popend", "peek", "peekend",
        "indexof", "reverse", "sort", "sortdefault"
    };
    if(needsLengthWord.count(methName) == 0) return false;
    // Without <array> nothing is tracked; the method lookup says where these come from.
    if(!languageService.arrayInUse) return false;
    if(memberArrayIsTracked(owner, prop, func, body)) return false;
    string what = methName == "length =" ? string("Assigning 'length'") : "'" + methName + "()'";
    parsingError(format("{0} needs an array's length word, but '{1}.{2}' is a RAW member array "
        "(rawArray<T>, or array<dictionaryWord>, whose layout I6 owns). It is inline property data "
        "with no length word — its extent is fixed at {1}.#{2}/WORDSIZE — so the operation would "
        "read the last ELEMENT as a length and walk off the end. size, length and subscripting "
        "do work on it; declare the member `array<T>` if you need the rest.", what, owner, prop));
    return true;
}

bool bglParser::memberArrayIsTracked(const std::string& ownerNameIn, const std::string& propName,
                                     functionDef* func, statementBlock* body){
    if(!languageService.arrayInUse) return false;
    // An owner written as its I6 name (`_box` for an object named by a reserved word) is that object.
    string ownerName = ownerNameIn;
    for(typeDef* t : languageService.objectInstances)
        if(!t->i6name.empty() && t->i6nameAvoidsKeyword && t->i6name == ownerNameIn){ ownerName = t->name; break; }
    auto check = [&](vector<typeMember*>& members) -> int {   // -1 unknown, 0 raw, 1 tracked
        for(typeMember* m : members)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(m))
                if(ad->name == propName || (!ad->i6name.empty() && ad->i6name == propName))
                    // Raw when the slot cannot hold a trailing length word: an explicit
                    // `rawArray<T>`, or a member bound to an ADDITIVE property, whose values
                    // accumulate across the hierarchy so a length slot would land inside the
                    // data. (Keyed on additivity rather than on `dictionaryword`, which was only
                    // ever a proxy for it — `name` and its kin are the additive ones.)
                    return (ad->isRaw || languageService.isAdditiveProperty(
                                             ad->i6name.empty() ? ad->name : ad->i6name)) ? 0 : 1;
        return -1;
    };
    // `self` in an inline accessor is the accessor's own class, not the object hosting it.
    if(ownerName == "self" && currentClass != nullptr && currentClass->name.rfind("_bglaccessor", 0) == 0){
        for(classDef* c = currentClass; c != nullptr; c = c->baseClasses.empty() ? nullptr : c->baseClasses[0]){
            int rc = check(c->members); if(rc >= 0) return rc == 1;
        }
    }
    // `self` in an object's body is that object itself; looking it up again by name can find another
    // symbol of that name (a verb's action).
    if(ownerName == "self" && currentObject != nullptr){
        int r = check(currentObject->members); if(r >= 0) return r == 1;
        for(classDef* c = currentObject->objectClass; c != nullptr; c = c->baseClasses.empty() ? nullptr : c->baseClasses[0]){
            int rc = check(c->members); if(rc >= 0) return rc == 1;
        }
    }
    string owner = ownerName == "self" ? (currentObject ? currentObject->name
                                        : (currentClass ? currentClass->name : string()))
                                      : ownerName;
    if(owner.empty()) return false;
    // A computed owner (`(getBox()).items`, a cast's receiver) has no name to look up: the member is
    // tracked when every member array of that name is.
    if(owner[0] == '('){
        int found = -1;
        auto scan = [&](vector<typeMember*>& members){ int r = check(members); if(r >= 0) found = found == -1 ? r : min(found, r); };
        for(typeDef* g : languageService.globals)
            if(auto* c = dynamic_cast<classDef*>(g)) scan(c->members);
        for(typeDef* t : languageService.objectInstances)
            if(auto* od = dynamic_cast<objectDef*>(t)) scan(od->members);
        return found == 1;
    }
    // An object that has a base class resolves through resolveIdentifierType to its CLASS, whose
    // members do not include the ones declared on the instance — so `object w : Base { array<int>
    // t[4]; }` looked up `t` on Base, missed it, and fell through to the "not tracked" default.
    // Look the owner up as an object FIRST so instance-declared members are found; the resolved
    // type is the fallback for locals, parameters, and anything that is not a named object.
    typeDef* ownerObj = nullptr;
    { typeDef& t = languageService.getType(owner);
      if(dynamic_cast<objectDef*>(&t) != nullptr) ownerObj = &t; }
    string resolvedOwner = owner.find('.') != string::npos ? resolvePathType(owner, func, body)
                                                          : resolveIdentifierType(owner, func, body);
    if(resolvedOwner.empty() && currentFunc != nullptr)   // a lambda's capture slot stands for the local it copied
        for(auto& cap : currentFunc->captures)
            if(cap.bodyName() == owner){ resolvedOwner = cap.typeName; break; }
    typeDef& td = ownerObj != nullptr
                ? *ownerObj
                : languageService.getType(resolvedOwner.empty() ? owner : resolvedOwner);
    if(auto* od = dynamic_cast<objectDef*>(&td)){
        int r = check(od->members); if(r >= 0) return r == 1;
        for(classDef* c = od->objectClass; c != nullptr; c = c->baseClasses.empty() ? nullptr : c->baseClasses[0]){
            int rc = check(c->members); if(rc >= 0) return rc == 1;
        }
    }
    if(auto* cd = dynamic_cast<classDef*>(&td))
        for(classDef* c = cd; c != nullptr; c = c->baseClasses.empty() ? nullptr : c->baseClasses[0]){
            int rc = check(c->members); if(rc >= 0) return rc == 1;
        }
    return false;
}

bool bglParser::splitQualifiedMember(const string& name, functionDef* func, statementBlock* body,
                                     string& ownerOut, string& propOut){
    if(func == nullptr) return false;
    string qualified = qualifyIdentifier(name, func, body);
    if(qualified.empty()) qualified = name;   // already qualified (a capture slot, a renamed local)
    size_t dot = qualified.rfind('.');
    if(dot == string::npos) return false;   // global/local — not a property access
    ownerOut = qualified.substr(0, dot);
    propOut  = qualified.substr(dot + 1);
    return true;
}

// Head of a dotted path, plus the pre-split pieces of its tail, shared by the dotted-path tiers.
// `firstSeg`/`rest` are lowercased; `head`/`tail` keep the user's case.
    // Bare #using-imported namespace alias as the head (e.g. `ui` from `#using bgl`, where
    // bgl declares `alias ui = _bglUi`). Such a head is not itself a type, so the
    // getType(head)-keyed alias-member redirect below misses and the whole path falls through
    // to a raw property chain (`_bglui.statusbar.height...`) that names nothing in I6. Resolve
    // the path against the alias's target so `ui.statusBar.height` qualifies exactly like the
    // fully-qualified `bgl.ui.statusBar.height`. Gated so a param/local/global of the same name
    // still wins (only fires when the head isn't a scoped variable).
optional<string> bglParser::qualifyDottedHeadViaUsingAlias(const DottedPath& p, functionDef* func,
                                                           statementBlock* body, bool forceGlobalScope){
    const string& head = p.head;
    const string& tail = p.tail;
    if(!forceGlobalScope
       && languageService.findClass(head) == nullptr
       && languageService.findObjectType(head) == nullptr){
        bool shadowed = false;
        if(func != nullptr) for(paramDef* p : func->params) if(p->name == head){ shadowed = true; break; }
        if(!shadowed && body != nullptr) for(statement* s : body->statements)
            if(auto* lv = dynamic_cast<variableDeclaration*>(s)) if(lv->name == head){ shadowed = true; break; }
        if(!shadowed) for(statementBlock* blk : activeBlockStack)
            if(blk != nullptr) for(statement* s : blk->statements)
                if(auto* lv = dynamic_cast<variableDeclaration*>(s)) if(lv->name == head){ shadowed = true; break; }
        if(!shadowed && languageService.findGlobalAs<variableDeclaration>(head) != nullptr) shadowed = true;
        if(!shadowed)
            for(objectDef* imp : usingObjectImports)
                for(typeMember* m : imp->members)
                    if(auto* avd = dynamic_cast<variableDeclaration*>(m))
                        if(avd->name == head){
                            string tgt = avd->declaredExpressionValue ? avd->declaredExpressionValue->text() : avd->type.name;
                            if(!tgt.empty()
                               && (languageService.findClass(tgt) != nullptr
                                   || languageService.findObjectType(tgt) != nullptr))
                                return qualifyIdentifier(tgt + "." + tail, func, body);
                        }
    }
    return nullopt;
}

// Tier: the head names a class (or emitter namespace) — resolve the tail against its value
// emitters and alias members.
optional<string> bglParser::qualifyDottedViaClassHead(const DottedPath& p, functionDef* func,
                                                      statementBlock* body){
    const string& head          = p.head;
    const string& firstSeg      = p.firstSeg;
    const string& rest          = p.rest;
    const string& qualifiedHead = p.qualifiedHead;
    classDef* cls = languageService.findClass(head);
    if(cls){
        for(typeMember* m : cls->members){
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == firstSeg && fd->isValueEmitter && fd->isEmitter && rest.empty())
                    if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                        emitterBindings hb; hb.self = qualifiedHead; hb.val = qualifiedHead;
                        hb.trim = emitterTrim::wsSemi;
                        return expandEmitterBody(blk, hb);
                    }
            // Alias member: resolve through the alias type for the remaining path
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == firstSeg && !rest.empty()){
                    // Recurse: qualify "aliasType.rest"
                    return qualifyIdentifier(vd->type.name + "." + rest, func, body);
                }
        }
    }
    return nullopt;
}

    // Namespace auto-member redirect on an OBJECT head (e.g. bgl.ui → _bglUi), mirroring the
    // expression-context walk in parseExpression. Without this, a statement-context method call
    // like `bgl.ui.pressAnyKey()` qualifies to the literal runtime chain `bgl.ui.pressAnyKey()`
    // — which reads the property's routine value and calls it WITHOUT setting `self` to the
    // backing object — instead of a proper message-send `_bglUi.pressAnyKey()`. Only redirects
    // when the member's initializer names a global object (namespace alias), so plain
    // value-typed properties are untouched.
optional<string> bglParser::qualifyDottedViaObjectHead(const DottedPath& p, functionDef* func,
                                                       statementBlock* body){
    const string& head     = p.head;
    const string& firstSeg = p.firstSeg;
    const string& rest     = p.rest;
    if(auto* instObj = languageService.findObjectType(head)){
        for(typeMember* m : instObj->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == firstSeg){
                    // `isAlias` is what separates a namespace hook (`alias ui = _bglUi;`) from an
                    // ordinary property that merely starts out pointing at an object
                    // (`object door { room target = hallway; }`). Both are "a member whose declared
                    // initializer names a global object", so following the initializer alone turned
                    // every such property into a compile-time redirect: `door.target` emitted
                    // `hallway`, so `door.target = kitchen;` assigned to the object symbol and every
                    // read ignored the property's storage.
                    string initName = vd->isNamespaceAlias() && vd->declaredExpressionValue
                                    ? vd->declaredExpressionValue->text() : "";
                    if(!initName.empty())
                        if(auto* od = languageService.findGlobalAs<objectDef>(initName))
                            return qualifyIdentifier(rest.empty() ? od->name : od->name + "." + rest, func, body);
                    // Alias member targeting an emitter CLASS (`emitter auto style = _bglStyle`):
                    // recurse into the class so `ns.style.member` reaches its value emitters.
                    // Restrict to emitter-class targets and alias members — a PLAIN class-typed
                    // (owned) member is a real instance, so `obj.member.field` must stay the
                    // runtime instance path (`obj.member.field`), not be rewritten to the type.
                    if(!rest.empty()){
                        string clsName = !initName.empty() ? initName : vd->type.name;
                        auto* tcls = languageService.findClass(clsName);
                        if(tcls && (tcls->isEmitterClass || vd->isAlias))
                            return qualifyIdentifier(clsName + "." + rest, func, body);
                    }
                    break;
                }
    }
    return nullopt;
}

// Terminal branch (not a fall-through tier): every path through a dotted name resolves here,
// so this returns the qualified string directly. "" means the head itself did not resolve.
string bglParser::qualifyDottedPath(const string& name, size_t dot, functionDef* func,
                                    statementBlock* body, bool forceGlobalScope){
    string head = name.substr(0, dot);
    string tail = name.substr(dot + 1);
    // `Crate.journal.x`: a static member is its global.
    if(size_t next = tail.find('.'); !forceGlobalScope)
        if(string g = staticMemberGlobal(head + "." + tail.substr(0, next)); !g.empty())
            return next == string::npos ? g : g + tail.substr(next);
    // Re-attach the global qualifier to the head so the recursion forces file scope for it, and
    // hint it with the member being reached. When the head matches more than one declaration — a
    // user global shadowing one of the BLR's unprefixed globals (`print`, `the`, `typeof`, …) —
    // the hint is what picks the candidate whose type actually has that member. Without it the
    // head resolved to whichever matched first and the access was reported against the wrong one.
    string headHint = tail.substr(0, tail.find('.'));
    string qualifiedHead = qualifyIdentifier((forceGlobalScope ? "::" : "") + head, func, body, headHint);
    if(qualifiedHead.empty()) return "";
    // A verb's member: the verb object, by its I6 name (its Beguile name is also its action's).
    if(qualifiedHead == head && !qualifyFromParams(head, func) && !qualifyFromBodyLocals(head, body)
       && !qualifyFromAncestorBlocks(head, body))
        for(verbObjectDef* v : languageService.verbs)
            if(v->name == head && !v->i6name.empty()
               && any_of(v->members.begin(), v->members.end(), [&](typeMember* m){ return m->name == headHint; })){
                qualifiedHead = v->i6name; break;
            }
    DottedPath p{head, tail, qualifiedHead, "", ""};
    if(auto r = qualifyDottedHeadViaUsingAlias(p, func, body, forceGlobalScope)) return *r;
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
    p.firstSeg = firstSeg;
    p.rest     = rest;
    if(auto r = qualifyDottedViaClassHead(p, func, body))  return *r;
    if(auto r = qualifyDottedViaObjectHead(p, func, body)) return *r;
    return qualifiedHead + "." + tail;
}

// Tier 1a: params of current (possibly nested) context. A parameter is a routine local; one that
// owns an instance is pointed at it at entry (emitParamCopyIns).
optional<string> bglParser::qualifyFromParams(const string& name, functionDef* func){
    if(func == nullptr) return nullopt;
    for(paramDef* p : func->params)
        if(p->name == name) return name;
    return nullopt;
}

// Tier 1b: locals in current block. A local is a routine local; one that owns an instance is set to
// it at entry (emitOwnedLocalSetup).
optional<string> bglParser::qualifyFromBodyLocals(const string& name, statementBlock* body){
    if(body == nullptr) return nullopt;
    for(statement* s : body->statements)
        if(auto* vd = dynamic_cast<variableDeclaration*>(s))
            if(vd->name == name) return name;
    return nullopt;
}

// Tier 1c: locals in ancestor blocks (when inside nested if/for/while/etc.).
// Walks the activeBlockStack — each block was pushed when its compile context opened.
// This handles the case where the AST is still being built (parent statements haven't
// been pushed to their body yet) by checking each in-progress block directly.
// A local in an enclosing block hides a global const of its name.
bool bglParser::isConstName(const string& name, functionDef* func, statementBlock* body){
    if(body != nullptr)
        for(statement* s : body->statements)
            if(auto* vd = dynamic_cast<variableDeclaration*>(s); vd && vd->name == name) return vd->isConst;
    for(auto it = activeBlockStack.rbegin(); it != activeBlockStack.rend(); ++it)
        if(*it != nullptr && *it != body)
            for(statement* s : (*it)->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s); vd && vd->name == name) return vd->isConst;
    // A member of the class or object being written comes before a global of the name (§3.8.2).
    auto memberConst = [&](const vector<typeMember*>& members, bool& found){
        for(typeMember* m : members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd && vd->name == name){ found = true; return vd->isConst; }
        return false;
    };
    bool found = false;
    if(currentObject != nullptr){ bool c = memberConst(currentObject->members, found); if(found) return c; }
    for(classDef* k = currentClass != nullptr ? currentClass : (currentObject ? currentObject->objectClass : nullptr); k != nullptr;
        k = k->baseClasses.empty() ? nullptr : k->baseClasses.front()){
        bool c = memberConst(k->members, found); if(found) return c;
    }
    return isConstVariable(name, func, body);
}

optional<string> bglParser::qualifyFromAncestorBlocks(const string& name, statementBlock* body){
    for(statementBlock* blk : activeBlockStack)
        if(blk != nullptr && blk != body)
            for(statement* s : blk->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name) return name;
    return nullopt;
}

// Tier 2: current object/class members (variables and methods) → qualify with self
// Walks the full class hierarchy (depth-first through baseClasses) so inherited members
// from multiple bases are reachable as bare identifiers inside method bodies.
// Uses the member's displayName (via dName()) so user case is preserved in I6 emission.
optional<string> bglParser::qualifyFromCurrentObject(const string& name){
    if(currentObject == nullptr) return nullopt;
    for(typeMember* m : currentObject->members)
        if(m->name == name){
            // `as <i6name>` — a member emits under its alias, so a bare reference from
            // inside the object's own body must resolve to the same property.
            if(auto* avd = dynamic_cast<variableDeclaration*>(m))
                return "self." + (avd->i6name.empty() ? avd->dName() : avd->i6name);
            if(auto* afd = dynamic_cast<functionDef*>(m)){
                // Named bare as a value, a method loses to a variable of the same name, as its type does.
                if(auto* gv = languageService.findGlobalAs<variableDeclaration>(name); gv != nullptr && gv->type.name != "property")
                    return nullopt;
                return "self." + (afd->i6name.empty() ? afd->dName() : afd->i6name);
            }
        }
    // Inherited from the object's class chain: a member, so it shadows a global of the same name.
    if(currentObject->objectClass != nullptr)
        if(typeMember* m = findMemberInHierarchy(currentObject->objectClass, [&](typeMember* c){ return c->name == name; })){
            if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                if(vd->isStatic || vd->isExternal) return nullopt;
                return "self." + (vd->i6name.empty() ? vd->dName() : vd->i6name);
            }
            if(auto* fd = dynamic_cast<functionDef*>(m); fd != nullptr && !fd->isStatic && !fd->isEmitter){
                // Named bare as a value, a method loses to a variable of the same name, as its type does.
                if(auto* gv = languageService.findGlobalAs<variableDeclaration>(name); gv != nullptr && gv->type.name != "property")
                    return nullopt;
                return "self." + (fd->i6name.empty() ? fd->dName() : fd->i6name);
            }
        }
    // An inherited value emitter (`child`, `sibling` on object): read like a member, so it is one here.
    if(currentObject->objectClass != nullptr)
        if(auto* ve = dynamic_cast<functionDef*>(findMemberInHierarchy(currentObject->objectClass, [&](typeMember* m){
                auto* f = dynamic_cast<functionDef*>(m); return f && f->name == name && f->isValueEmitter && f->isEmitter; })))
            if(auto* blk = dynamic_cast<i6Block*>(ve->body)){
                emitterBindings vb; vb.self = "self"; vb.val = "self"; vb.trim = emitterTrim::wsSemi;
                return expandEmitterBody(blk, vb);
            }
    return nullopt;
}

// Tier: members of the enclosing classDef — own members (statics included) then base classes.
optional<string> bglParser::qualifyFromCurrentClass(const string& name){
    if(currentClass == nullptr) return nullopt;
    // Direct members: match both variables and functions (same as before).
    // Static variables resolve to their mangled global name, not `self.name` — they
    // live outside any instance's property table.
    for(typeMember* m : currentClass->members){
        if(m->name == name){
            if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                if(vd->isStatic) return "_bgl_" + currentClass->dName() + "_" + vd->dName();
                // `as <i6name>` — the member emits under the alias, so a bare reference
                // inside the object's own body has to resolve to it too.
                return "self." + (vd->i6name.empty() ? vd->dName() : vd->i6name);
            }
            else if(auto* fd = dynamic_cast<functionDef*>(m)){
                // A value emitter (`emitter int Slots { 4 }`) is its body, not a property.
                if(fd->isValueEmitter && fd->isEmitter)
                    if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                        emitterBindings vb; vb.self = "self"; vb.val = "self"; vb.trim = emitterTrim::wsSemi;
                        return expandEmitterBody(blk, vb);
                    }
                // Named bare as a value, a method loses to a variable of the same name, as its type does.
                if(auto* gv = languageService.findGlobalAs<variableDeclaration>(name); gv != nullptr && gv->type.name != "property")
                    return nullopt;
                return "self." + m->dName();
            }
        }
    }
    // Base class hierarchy: only match VARIABLES, not functions. Function/method resolution
    // has its own hierarchy-aware path (resolveMethod) that checks arity and types correctly.
    // Walking functions here would shadow global functions of the same name (e.g. `print`
    // on _bglObject shadowing the global `print(string)` emitter).
    // Returns the member's displayName when found in a base.
    std::function<string(classDef*)> findVarDisplayInBases = [&](classDef* c) -> string {
        for(typeMember* m : c->members)
            if(m->name == name)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m)) return vd->i6name.empty() ? vd->dName() : vd->i6name;
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
    // An inherited value emitter (`child`, `sibling` on object): read like a member, so it is one here.
    if(auto* ve = dynamic_cast<functionDef*>(findMemberInHierarchy(currentClass, [&](typeMember* m){
            auto* f = dynamic_cast<functionDef*>(m); return f && f->name == name && f->isValueEmitter && f->isEmitter; })))
        if(auto* blk = dynamic_cast<i6Block*>(ve->body)){
            emitterBindings vb; vb.self = "self"; vb.val = "self"; vb.trim = emitterTrim::wsSemi;
            return expandEmitterBody(blk, vb);
        }
    return nullopt;
}

// Tier 7 (hoisted): closure capture — inside a lambda, check the outer function's scope
// BEFORE file-scope candidates. Captures shadow same-named globals, matching standard
// lexical-scoping semantics in any closure-bearing language.
optional<string> bglParser::qualifyFromCaptures(const string& name){
    if(lambdaOuterFunc == nullptr || currentFunc == nullptr) return nullopt;
    // Check outer function's params
    for(paramDef* p : lambdaOuterFunc->params)
        if(p->name == name)
            return addCapture(name, p->type.name);
    // Check immediate enclosing body (e.g. for-loop body)
    if(lambdaOuterBody != nullptr)
        for(statement* s : lambdaOuterBody->statements)
            if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                if(vd->name == name)
                    return addCapture(name, vd->type.name, vd);
    // The blocks still open around the lambda, innermost first.
    for(auto it = lambdaOuterBlockStack.rbegin(); it != lambdaOuterBlockStack.rend(); ++it)
        for(statement* s : (*it)->statements)
            if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                if(vd->name == name)
                    return addCapture(name, vd->type.name, vd);
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
            return addCapture(name, vd->type.name, vd);
    }
    // Capture chaining: walk the full lambda nesting stack. A name from further out reaches this
    // lambda through the enclosing lambda's own capture of it, since the name isn't in scope there.
    auto viaEnclosing = [&](const string& typeName, variableDeclaration* vd) -> string {
        if(lambdaOuterFunc == nullptr || !lambdaOuterFunc->isLambda) return addCapture(name, typeName, vd);
        for(auto& cap : lambdaOuterFunc->captures)
            if(cap.outerName == name) return addCapture(cap.bodyName(), cap.typeName);
        functionDef* inner = currentFunc;
        currentFunc = lambdaOuterFunc;
        string outerGlobal = addCapture(name, typeName, vd);
        currentFunc = inner;
        return addCapture(outerGlobal, typeName);
    };
    for(int si = (int)lambdaOuterFuncStack.size() - 1; si >= 0; si--){
        functionDef* ancestor = lambdaOuterFuncStack[si];
        if(ancestor == lambdaOuterFunc) continue;
        for(paramDef* p : ancestor->params)
            if(p->name == name)
                return viaEnclosing(p->type.name, nullptr);
        statementBlock* ancestorBody = dynamic_cast<statementBlock*>(ancestor->body);
        if(ancestorBody != nullptr)
            for(statement* s : ancestorBody->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == name)
                        return viaEnclosing(vd->type.name, vd);
        for(auto& cap : ancestor->captures)
            if(cap.outerName == name)
                return addCapture(cap.bodyName(), cap.typeName);
    }
    // `self` capture: when a lambda is inside an object method, `self` refers to the
    // enclosing object. Capture it to a global.
    if(name == "self" && (currentClass != nullptr || currentObject != nullptr)){
        string selfType = currentClass ? currentClass->name : currentObject->name;
        return addCapture("self", selfType);
    }
    return nullopt;
}

// Globals (variable, function, objectDef, classDef, enumDef)
void bglParser::collectQualifyCandidatesFromGlobals(const string& name, vector<QualifyCandidate>& candidates){
    // Every global of this name — see collectTypeCandidatesFromGlobals for why the first alone
    // is not enough.
    for(typeDef* g : languageService.globals){
        if(g->name != name) continue;
        string qual, ct, origin;
        bool isObj = false, isFn = false, libRoutine = false;
        if(auto* fd = dynamic_cast<functionDef*>(g)){
            ct = fd->returnType.name;
            origin = format("global function '{0}'", g->name);
            libRoutine = fd->isExternal && !fd->isEmitter;
            if(fd->isValueEmitter && fd->isEmitter){
                if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                    string b = expandEmitterBody(blk, {});
                    size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                    size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                    qual = b;
                } else qual = g->i6name.empty() ? name : g->i6name;
            } else qual = g->i6name.empty() ? name : g->i6name;
            isFn = true;
        }
        else if(auto* vd = dynamic_cast<variableDeclaration*>(g)){
            ct = vd->type.name;
            qual = g->i6name.empty() ? name : g->i6name;
            origin = format("global variable '{0}'", g->name);
            // Read before its declaration: I6 wants a variable defined before use (arrays and
            // constants are symbols I6 resolves later, so only a variable moves).
            if(vd->isPrePassStub && !vd->isExternal && !vd->isConst && dynamic_cast<arrayDeclaration*>(vd) == nullptr)
                vd->needsEarlyGlobalDecl = true;
        }
        else if(auto* od = dynamic_cast<objectDef*>(g)){
            ct = dynamic_cast<verbObjectDef*>(od) != nullptr && od->objectClass != nullptr
                 ? od->objectClass->name : od->name;   // its own type, a verb its class (see resolveIdentifierType)
            qual = g->i6name.empty() ? name : g->i6name;
            origin = format("global object '{0}'", g->name);
            isObj = true;
        }
        else if(dynamic_cast<classDef*>(g) || dynamic_cast<enumDef*>(g)){
            // Class/enum type-name reference (e.g. for ClassName.staticMember dot-paths).
            // The dot-path code in qualifyIdentifier consults this and resolves the tail.
            if(auto* cd = dynamic_cast<classDef*>(g)) qual = cd->i6Name();
            else qual = g->i6name.empty() ? name : g->i6name;
            ct = name;  // type identifies itself
            origin = format("type '{0}'", g->name);
        }
        if(!qual.empty()){
            bool dup = false;
            for(auto& c : candidates) if(c.qualified == qual && c.type == ct){ dup = true; break; }
            if(!dup) candidates.push_back({qual, ct, origin, false, isObj, isFn, libRoutine});
        }
    }
}

// #using class imports
void bglParser::collectQualifyCandidatesFromUsingClassImports(const string& name, functionDef* func,
                                                              statementBlock* body,
                                                              vector<QualifyCandidate>& candidates){
    for(classDef* imp : usingImports){
        for(typeMember* m : imp->members){
            string qual, ct, origin;
            bool matched = false, isFn = false;
            if(auto* fd = dynamic_cast<functionDef*>(m)){
                if(fd->name != name) continue;
                // A value emitter expands to a value; a method import is a function reference.
                isFn = !(fd->isValueEmitter && fd->isEmitter);
                ct = fd->returnType.name;
                origin = format("#using-imported method '{0}.{1}'", imp->dName(), name);
                if(fd->isValueEmitter && fd->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                        emitterBindings ub; ub.self = imp->name; ub.val = imp->name;
                        ub.trim = emitterTrim::wsSemi;
                        qual = expandEmitterBody(blk, ub);
                    } else qual = imp->name + "." + name;
                } else qual = imp->name + "." + name;
                matched = true;
            }
            else if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                if(vd->name != name) continue;
                ct = vd->type.name;
                // A static is the class's global; otherwise qualify recursively, so a namespace auto
                // member resolves through to its backing object (parallels the object-import case below).
                qual = vd->isStatic ? "_bgl_" + imp->name + "_" + name
                                    : qualifyIdentifier(imp->name + "." + name, func, body);
                origin = format("#using-imported variable '{0}.{1}'", imp->dName(), name);
                matched = true;
            }
            if(matched){ candidates.push_back({qual, ct, origin, false, /*isObject=*/false, isFn}); break; }
        }
    }
}

// #using object imports
void bglParser::collectQualifyCandidatesFromUsingObjectImports(const string& name, functionDef* func,
                                                               statementBlock* body,
                                                               vector<QualifyCandidate>& candidates){
    for(objectDef* imp : usingObjectImports){
        for(typeMember* m : imp->members){
            string qual, ct, origin;
            bool matched = false, isFn = false;
            if(auto* fd = dynamic_cast<functionDef*>(m)){
                if(fd->name != name) continue;
                // A value emitter expands to a value; a method import is a function reference.
                isFn = !(fd->isValueEmitter && fd->isEmitter);
                ct = fd->returnType.name;
                origin = format("#using-imported method '{0}.{1}'", imp->name, name);
                if(fd->isValueEmitter && fd->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                        emitterBindings ub; ub.self = imp->name; ub.val = imp->name;
                        ub.trim = emitterTrim::wsSemi;
                        qual = expandEmitterBody(blk, ub);
                    } else qual = imp->name + "." + name;
                } else qual = imp->name + "." + name;
                matched = true;
            }
            else if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                if(vd->name != name) continue;
                ct = vd->type.name;
                // Recursively qualify so a namespace auto member resolves through to its backing
                // object (e.g. `#using bgl` + `ui` → `bgl.ui` → `_bglUi`), matching a fully-qualified
                // `bgl.ui`. Plain value members are unaffected (they re-qualify to themselves).
                qual = qualifyIdentifier(imp->name + "." + name, func, body);
                // A type alias named as a value is its class (`o.is(crate)`).
                if(vd->isAlias)
                    if(classDef* ac = languageService.findClass(vd->type.name)) qual = ac->i6Name();
                origin = format("#using-imported member '{0}.{1}'", imp->name, name);
                matched = true;
            }
            if(matched){ candidates.push_back({qual, ct, origin, false, /*isObject=*/false, isFn}); break; }
        }
    }
}

// Reduce the collected file-scope candidates to a single emission, or report the ambiguity.
// Disengaged means no candidate survived — the caller continues to its fallbacks.
optional<string> bglParser::selectQualifiedCandidate(const string& name, const string& memberHint,
                                                     bool forceGlobalScope,
                                                     vector<QualifyCandidate>& candidates){
    if(currentExpectedType == "property" && namesProperty(name) && !candidates.empty()){
        // The name a member of that name is emitted under (a method keeps its own; renames vary).
        for(typeDef* g : languageService.globals){
            vector<typeMember*>* members = nullptr;
            if(auto* cd = dynamic_cast<classDef*>(g)) members = &cd->members;
            else if(auto* od = dynamic_cast<objectDef*>(g)) members = &od->members;
            if(members) for(typeMember* m : *members)
                if(m->name == name && !m->isPrePassStub) return m->i6name.empty() ? name : m->i6name;
        }
        return propertyI6Name(name);
    }
    // A library routine named bare yields to anything else of the name: the program's own names
    // (and an enum's values) are what a bare word means; the routine is reached by calling it.
    if(any_of(candidates.begin(), candidates.end(), [](const QualifyCandidate& c){ return !c.isLibraryRoutine; }))
        candidates.erase(remove_if(candidates.begin(), candidates.end(), [](const QualifyCandidate& c){ return c.isLibraryRoutine; }),
                         candidates.end());
    // Dedupe: candidates that resolve to the same I6 emission AND the same type are the same
    // thing seen via multiple lookup paths (e.g. a verb registered both as a verbObjectDef and
    // as a global object stub). They represent one resolution, not two — collapse them.
    {
        vector<QualifyCandidate> deduped;
        for(auto& c : candidates){
            bool dup = false;
            for(auto& d : deduped) if(d.qualified == c.qualified && d.type == c.type){ dup = true; break; }
            if(!dup) deduped.push_back(c);
        }
        candidates = deduped;
    }
    // Globals beat enum values: see resolveIdentifierType for rationale (and its expected-enum exception).
    if(auto keep = enumCandidatesFittingExpected(candidates); !keep.empty()) candidates = keep;
    {
        bool hasNonEnum = false;
        for(auto& c : candidates) if(!c.isEnum){ hasNonEnum = true; break; }
        if(hasNonEnum){
            vector<QualifyCandidate> filtered;
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
        if(!forceGlobalScope && currentObject != nullptr){
            // Restrict to VARIABLE (property) collisions only. Method collisions are
            // visually unambiguous since methods are always called with parens, and
            // many objects inherit common methods (`print`, `is`, `provides`, etc.)
            // that the user calls via the global form by name without thinking of
            // them as members.
            // `::name` explicitly forces the global, so no ambiguity remains — no warning.
            auto isObjectProperty = [&]() -> bool {
                for(typeMember* m : currentObject->members)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                        if(vd->name == name) return true;
                classDef* objCls = currentObject->objectClass;
                return objCls != nullptr && objCls->findMember([&](typeMember* m){
                    auto* vd = dynamic_cast<variableDeclaration*>(m);
                    return vd != nullptr && vd->name == name;
                }) != nullptr;
            };
            if(isObjectProperty()){
                parsingWarning(format(
                    "Bare '{0}' resolves as {1}, but '{0}' is also a property of the enclosing "
                    "object. Beguile cannot tell which you mean. Write 'self.{0}' for the object's "
                    "property, or '::{0}' to force the global and silence this warning.",
                    name, candidates[0].origin));
            }
        }
        return candidates[0].qualified;
    }
    if(candidates.size() >= 2){
        // Kind filter: a FUNCTION reference and a variable/object of the same name are told apart
        // by how the name is USED, not by which was declared first. A call `name(args)` never
        // reaches here — bindGlobalCall handles it — so every use that does reach here is either a
        // dotted access or a bare value, and a function reference supports neither a member access
        // nor being shadowed by an author's own declaration. Dropping function candidates therefore
        // resolves the collision that the BLR's unprefixed globals (`print`, `the`, `typeof`, …)
        // otherwise create, while a lone function name still resolves as a reference
        // (`arr.filter(isEven)`) because it is then the only candidate.
        {
            bool hasNonFunction = false;
            for(auto& c : candidates) if(!c.isFunction){ hasNonFunction = true; break; }
            // Where a func<> is expected, though, the name is the function.
            bool funcExpected = currentExpectedType.rfind("func", 0) == 0 && !memberHint.size();
            if(hasNonFunction){
                vector<QualifyCandidate> filtered;
                for(auto& c : candidates) if(c.isFunction == funcExpected) filtered.push_back(c);
                if(filtered.empty())
                    for(auto& c : candidates) if(!c.isFunction) filtered.push_back(c);
                candidates = filtered;
            }
        }
        if(candidates.size() == 1) return candidates[0].qualified;
        if(!memberHint.empty()){
            vector<QualifyCandidate*> satisfying;
            for(auto& c : candidates)
                if(typeHasMember(c.type, memberHint)) satisfying.push_back(&c);
            if(satisfying.size() == 1) return satisfying[0]->qualified;
        }
        if(!currentExpectedType.empty()){
            vector<QualifyCandidate*> compatible;
            for(auto& c : candidates)
                if(isTypeCompatible(c.type, currentExpectedType)) compatible.push_back(&c);
            if(compatible.size() == 1) return compatible[0]->qualified;
            // Where a class is expected, the name of a class is that class, not a value of some class type.
            if(currentExpectedType == "bglclass"){
                vector<QualifyCandidate*> types;
                for(auto* c : compatible) if(c->origin.rfind("type", 0) == 0) types.push_back(c);
                if(types.size() == 1) return types[0]->qualified;
            }
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
                vector<QualifyCandidate> filtered;
                for(auto& c : candidates) if(!c.isObject) filtered.push_back(c);
                if(filtered.size() == 1) return filtered[0].qualified;
                candidates = filtered;  // narrowed but still ambiguous — fall through to error
            }
        }
        // All that is left is one global name carrying several overloads. A call never reaches
        // here (bindGlobalCall resolves it from the arguments), so this is a bare reference — and
        // a reference must name ONE routine, which the name alone cannot do.
        {
            bool allFunctions = true;
            for(auto& c : candidates) if(!c.isFunction){ allFunctions = false; break; }
            if(allFunctions)
                parsingError(format("'{0}' names {1} overloads, so it cannot be used as a bare "
                    "function reference — only a call can say which one is meant. Wrap the overload "
                    "you want in a function of its own and reference that.", name, candidates.size()));
        }
        // A global beats a #using-imported member of the same name (§10.4), quietly when both name
        // the same type (an imported alias of the global class).
        {
            int globals = 0; size_t globalAt = 0;
            for(size_t i = 0; i < candidates.size(); i++)
                if(candidates[i].origin.rfind("#using-imported", 0) != 0){ globals++; globalAt = i; }
            if(globals == 1){
                // Quiet only when the global is a type and the import an alias of that same type.
                bool sameType = candidates[globalAt].origin.rfind("type", 0) == 0;
                for(auto& c : candidates)
                    if(getDispatchClass(c.type) != getDispatchClass(candidates[globalAt].type)
                       || getDispatchClass(c.type) == nullptr) sameType = false;
                if(!sameType)
                    parsingWarning(format("'{0}' is both {1} and an imported member; the global is used. "
                        "Write the imported one with its full path.", name, candidates[globalAt].origin));
                return candidates[globalAt].qualified;
            }
        }
        // Where a property is expected (`obj.provides(number)`), a property's name is that property.
        if(currentExpectedType == "property" && namesProperty(name)) return propertyI6Name(name);
        string msg = format("'{0}' is ambiguous: matches ", name);
        for(size_t i = 0; i < candidates.size(); i++){
            if(i > 0) msg += (i == candidates.size() - 1 ? " and " : ", ");
            msg += candidates[i].origin;
        }
        msg += ". Qualify the use explicitly to disambiguate.";
        parsingError(msg);
    }
    return nullopt;
}

// True if `name` is a property: a `property` global, or a member of any class or object.
bool bglParser::namesProperty(const string& name){
    if(auto* gv = languageService.findGlobalAs<variableDeclaration>(name); gv && gv->type.name == "property") return true;
    for(typeDef* g : languageService.globals){
        vector<typeMember*>* members = nullptr;
        if(auto* cd = dynamic_cast<classDef*>(g)) members = &cd->members;
        else if(auto* od = dynamic_cast<objectDef*>(g)) members = &od->members;
        if(members) for(typeMember* m : *members) if(m->name == name) return true;
    }
    return false;
}

std::string bglParser::qualifyIdentifier(std::string name, functionDef* func, statementBlock* body, const string& memberHint){
    // Global-scope qualifier `::name`: skip lexical scope (params/locals/members) and emit the bare
    // file-scope name. On a dotted path (`::obj.member`) the prefix forces the HEAD to global.
    bool forceGlobalScope = false;
    if(name.size() > 2 && name[0]==':' && name[1]==':'){ forceGlobalScope = true; name = name.substr(2); }
    if(name == "null") return "nothing";
    if(name == "self" && lambdaOuterFunc == nullptr) return "self";
    // `outer` emits the enclosing (host) object's I6 name — a compile-time constant captured while
    // synthesizing an inline accessor's class body.
    if(name == "outer" && accessorOuter != nullptr)
        return accessorOuter->i6name.empty() ? accessorOuter->dName() : accessorOuter->i6name;
    if(name == "outer" && currentClass != nullptr && currentClass->name.rfind("_bglaccessor", 0) == 0)
        parsingError("'outer' exists only in an inline accessor declared in an object; a class's accessor has no single host.");
    // Handle dot-path: qualify the head, then check for value emitter on the tail
    size_t dot = name.find('.');
    if(dot != string::npos) return qualifyDottedPath(name, dot, func, body, forceGlobalScope);
    // `::name` past a parameter or local of the same name: the routine's local is renamed at
    // emission, and the marker keeps this reference out of that rename (stripped in resolvedOutput).
    if(forceGlobalScope && func != nullptr
       && (qualifyFromParams(name, func) || qualifyFromBodyLocals(name, body) || qualifyFromAncestorBlocks(name, body))){
        string g = qualifyIdentifier("::" + name, nullptr, nullptr, memberHint);
        bool isIdent = !g.empty() && all_of(g.begin(), g.end(), [](char c){ return isalnum((unsigned char)c) || c == '_'; })
                       && !isdigit((unsigned char)g[0]);
        if(!isIdent) return g;
        (currentFunc != nullptr ? currentFunc : func)->globalEscapes.insert(name);   // loop bodies parse under a stand-in func
        return kGlobalEscapeMarker + g;
    }
    if(!forceGlobalScope){
        if(auto r = qualifyFromParams(name, func))          return *r;
        if(auto r = qualifyFromBodyLocals(name, body))      return *r;
        if(auto r = qualifyFromAncestorBlocks(name, body))  return *r;
        // A member read inside a lambda goes through `self`, which the lambda must capture like the
        // explicit `self.member` form does: it may run when `self` is some other object.
        auto viaSelf = [&](const string& q){
            if(lambdaOuterFunc != nullptr && q.rfind("self.", 0) == 0) qualifyFromCaptures("self");
            return q;
        };
        if(auto r = qualifyFromCurrentObject(name))         return viaSelf(*r);
        if(auto r = qualifyFromCurrentClass(name))          return viaSelf(*r);
        if(auto r = qualifyFromCaptures(name))              return *r;
    }

    // ── FILE SCOPE — collect all matches, then apply ambiguity rule ───────────
    // Same principle as resolveIdentifierType: enum values, globals, verbs, emitter objects,
    // #defines, and #using imports share one conceptual namespace. Multiple matches across
    // these tiers indicates a real ambiguity, not legitimate scope shadowing.
    vector<QualifyCandidate> candidates;

    // Enum values: walk EVERY enum containing this name so multiple enums sharing a value
    // each become a candidate (the expected-type filter below disambiguates).
    bool classNamed = languageService.isKnownClassName(name);   // the class, unless its enum is expected
    for(typeDef* t : languageService.objectTypes){
        auto* ed = dynamic_cast<enumDef*>(t);
        if(!ed) continue;
        if(classNamed && ed->name != currentExpectedType) continue;
        bool match = false; int matchedValue = 0;
        for(enumValueDef* ev : ed->namedValues)
            if(ev->name == name){ match = true; matchedValue = ev->value; break; }
        if(!match) continue;
        string enumQualified;
        if(ed->isExternal) enumQualified = name;
        else                enumQualified = to_string(matchedValue);
        candidates.push_back({enumQualified, ed->name, format("enum value of '{0}'", ed->name), true});
    }
    collectQualifyCandidatesFromGlobals(name, candidates);
    collectQualifyCandidatesFromUsingClassImports(name, func, body, candidates);
    collectQualifyCandidatesFromUsingObjectImports(name, func, body, candidates);
    // Verb names (action constants)
    for(verbObjectDef* vd : languageService.verbs){
        string lower = vd->name;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if(lower == name){
            // A declared verb is already a candidate as its global object; one name, one verb.
            string qual = vd->i6name.empty() ? vd->name : vd->i6name;
            bool seen = false;
            for(auto& c : candidates) if(c.qualified == qual) seen = true;
            if(!seen) candidates.push_back({qual, "verb", format("verb '{0}'", vd->name), false, /*isObject=*/true});
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
        if(it != definedSymbols.end() && !it->second.empty() && !definesExternGlobal(name)){
            auto [text, ct] = defineLiteral(it->second);
            // A #defineI6 symbol is an I6 Constant too: it is read by name.
            if(auto i6 = i6DefinedSymbols.find(name); i6 != i6DefinedSymbols.end()) text = i6->second;
            candidates.push_back({text, ct, format("#define symbol '{0}'", name), false});
        }
    }

    if(auto r = selectQualifiedCandidate(name, memberHint, forceGlobalScope, candidates)) return *r;
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
    // Class-name fallback: see resolveIdentifierType for rationale. A bare class identifier is the
    // class's I6 name, so `obj.is(Container)` becomes `obj ofclass _bglClass_Container`.
    if(languageService.isKnownClassName(name)){
        classDef* cls = languageService.findClass(name);
        return cls != nullptr ? cls->i6Name() : name;
    }
    return "";
}

// `#defineI6 MAX_TIMERS 48` sets the value of a constant the bindings declare `extern`: both names are the
// one Inform 6 constant, so the extern declaration (typed) is the name and the define isn't a rival.
bool bglParser::definesExternGlobal(const string& name){
    if(!i6DefinedSymbols.count(name)) return false;
    auto* vd = languageService.findGlobalAs<variableDeclaration>(name);
    return vd != nullptr && vd->isExternal;
}

// Register a closure capture: creates a global variable for the captured value.
// Returns the global name. Deduplicates: if the same outer variable is captured
// multiple times in the same lambda, returns the existing global.

string bglParser::addCapture(const string& outerName, const string& typeName, variableDeclaration* vd){
    // Check if already captured in this lambda
    for(auto& cap : currentFunc->captures)
        if(cap.outerName == outerName) return cap.bodyName();
    int n = languageService.captureCounter++;
    string globalName = format("_bglCap{0}", n);
    // An array is captured with its element type, as it is declared.
    string type = typeName;
    if(auto* ad = dynamic_cast<arrayDeclaration*>(vd); ad && !ad->isByteArray && !ad->elementType.empty() && type.find('<') == string::npos)
        type = (ad->isRaw ? "rawarray<" : "array<") + ad->elementType + ">";
    string why;
    bool isConstant = vd != nullptr && vd->isConst && vd->declaredExpressionValue != nullptr
                      && isLiteralValue(vd->declaredExpressionValue, why);
    currentFunc->captures.push_back({outerName, globalName, type, isConstant,
                                     outerName == "self" ? "" : format("_bglcl{0}", n)});
    return currentFunc->captures.back().bodyName();
}


// The class `selfType` names in the code being parsed: the class being declared, or the class of the
// object being declared; "" outside a class or object.
string bglParser::currentSelfTypeName(){
    if(currentClass != nullptr) return currentClass->name;
    if(currentObject != nullptr && currentObject->objectClass != nullptr) return currentObject->objectClass->name;
    return "";
}

classDef* bglParser::getDispatchClass(const string& typeName){
    if(typeName == "selftype"){ string own = currentSelfTypeName(); return own.empty() ? nullptr : languageService.findClass(own); }
    typeDef& td = languageService.getType(typeName);
    if(auto* cls = dynamic_cast<classDef*>(&td)) return cls;
    // An enum/bnum with emitter methods dispatches through its unregistered companion emitter class.
    // Only when a companion exists — a plain enum still returns nullptr (no members, no dispatch), so
    // its behavior (incl. bnum's built-in `|` handling) is unchanged. The companion holds only emitter
    // methods (no operators), so operator resolution finds nothing and falls through to int/bnum paths.
    if(auto* ed = dynamic_cast<enumDef*>(&td)) return ed->companion;   // null when the enum has no emitters
    if(dynamic_cast<objectDef*>(&td)) return languageService.classOf(typeName);

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

// Property-class discriminator — see the declaration in bglParser.h. The rule is generic and the
// specifics (the actual `operator()` body) live in the BLR: a property accessor reads through the
// OWNER and TRANSFORMS it ($self, non-identity), vs a value conversion ($val / bare $self / empty).
bool bglParser::isPropertyClassReadEmitter(typeMember* m){
    auto* fn = dynamic_cast<functionDef*>(m);
    if(!(fn && fn->name == "operator()" && fn->params.empty()
         && fn->isEmitter && !fn->isExplicit)) return false;
    auto* blk = dynamic_cast<i6Block*>(fn->body);
    if(blk == nullptr) return false;
    string t = blk->i6Body;
    { size_t s = t.find_first_not_of(" \t\n\r"); if(s != string::npos) t = t.substr(s); else t.clear();
      size_t e = t.find_last_not_of(" \t\n\r;"); if(e != string::npos) t = t.substr(0, e+1); }
    return t != "$self" && t != "$val" && t.find("$self") != string::npos;
}

bool bglParser::isPropertyClassType(const string& typeName){
    classDef* cls = getDispatchClass(typeName);
    if(cls == nullptr || !cls->isEmitterClass || cls->isPrimitive) return false;
    return findMemberInHierarchy(cls, [this](typeMember* m){ return isPropertyClassReadEmitter(m); }) != nullptr;
}

std::set<string> bglParser::collectPropertyClassMemberNames(){
    std::set<string> names, plain;
    auto scanMembers = [&](const vector<typeMember*>& members){
        for(typeMember* m : members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                (isPropertyClassType(vd->type.name) ? names : plain).insert(vd->name);
    };
    for(typeDef* g : languageService.objectTypes)
        if(auto* cd = dynamic_cast<classDef*>(g)) scanMembers(cd->members);
    for(typeDef* g : languageService.objectInstances)
        if(auto* od = dynamic_cast<objectDef*>(g)) scanMembers(od->members);
    // A name some other type declares as an ordinary property (`int length`) reads raw legitimately.
    for(const string& n : plain) names.erase(n);
    return names;
}


// ===============================================================================
// Type compatibility and arg conversion
// ===============================================================================
// A union type name carries a top-level '|' (one not nested inside a func<...>/array<...>
// argument list). These two helpers recognize and split such names.
bool bglParser::isUnionType(const std::string& t){
    int depth = 0;
    for(char c : t){
        if(c == '<') depth++;
        else if(c == '>') depth--;
        else if(c == '|' && depth == 0) return true;
    }
    return false;
}
std::vector<std::string> bglParser::splitUnionType(const std::string& t){
    std::vector<std::string> out;
    int depth = 0; std::string cur;
    for(char c : t){
        if(c == '<') depth++;
        else if(c == '>') depth--;
        if(c == '|' && depth == 0){ out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if(!cur.empty()) out.push_back(cur);
    return out;
}

// The `operator =` that assigns a value of `valueType` to a slot of class `cls`, or null.
//
// The search walks the value's type and then its ancestors, and takes the first `operator =` on
// `cls` (or inherited by it) whose parameter is that type — so the most specific parameter wins.
// An operator whose parameter is `cls` itself or one of its ancestors is plain assignment of a
// `cls`: it applies only to a value that IS a `cls` (derives from it), so an unrelated object isn't
// accepted just because every class shares a root. An operator taking any other type is a
// conversion (a setter, `parentProp`'s `operator = (object)`) and applies to that type's values.
//
// An operator declared for a strict ancestor of `cls` (e.g. `_bglObject`'s pass-through store) is
// inherited assignment; `allowInherited` false skips it. A `var` (untyped) value has no chain, so it
// takes the class's own assignment.
functionDef* bglParser::findAssignOperator(classDef* cls, const string& valueType,
                                           const function<bool(functionDef*)>& accept, bool allowInherited,
                                           const string& targetType){
    if(cls == nullptr || valueType.empty()) return nullptr;
    // `null`, the empty reference, fits an operator taking any class type (not a primitive value).
    if(valueType == "nullliteral")
        return dynamic_cast<functionDef*>(findMemberInHierarchy(cls, [&](typeMember* mm){
            auto* fn = dynamic_cast<functionDef*>(mm);
            if(!fn || fn->name != "=" || fn->isStatic || fn->params.size() != 1 || !accept(fn)) return false;
            classDef* pc = getDispatchClass(fn->params[0]->type.name);
            return pc != nullptr && !pc->isPrimitive;
        }));
    classDef* valCls = valueType == "var" ? nullptr : getDispatchClass(valueType);
    bool valueIsCls = valueType == "var" || (valCls != nullptr && (valCls == cls || valCls->hasAncestor(cls)));
    // The value's type, then its class and that class's ancestors, nearest first.
    vector<string> chain;
    auto addType = [&](const string& t){ if(find(chain.begin(), chain.end(), t) == chain.end()) chain.push_back(t); };
    if(valueType == "var"){
        addType(cls->name);
        for(classDef* c : cls->ancestorsNearestFirst()) addType(c->name);
    } else {
        addType(valueType);
        if(valCls != nullptr){
            addType(valCls->name);
            for(classDef* c : valCls->ancestorsNearestFirst()) addType(c->name);
        }
        // A literal reaches an operator taking its base type, `3` an `operator = (int)` (§2.10.1).
        if(string base = literalBaseType(valueType); base != valueType) addType(base);
        if(valueType == "char" || valueType == "charliteral") addType("int");   // a char widens to int (§12.4)
    }
    for(const string& t : chain){
        typeMember* m = findMemberInHierarchy(cls, [&](typeMember* mm){
            auto* fn = dynamic_cast<functionDef*>(mm);
            if(!fn || fn->name != "=" || fn->isStatic || fn->params.size() != 1) return false;
            const string& pt = fn->params[0]->type.name;
            classDef* paramCls = getDispatchClass(pt);
            // A templated parameter (`array<T>`) matches by class: on an ancestor step, or on the value's
            // own class when its element types fit the target's (`array<int>` into `array<int>`).
            bool templated = pt.find('<') != string::npos && paramCls != nullptr && valCls != nullptr
                             && getDispatchClass(t) == paramCls;
            bool matches = pt == t
                || (templated && paramCls != valCls && valCls->hasAncestor(paramCls))
                || (templated && paramCls == valCls && !targetType.empty() && templateArgsFit(valueType, targetType));
            if(!matches || !accept(fn)) return false;
            bool paramIsClsOrAncestor = paramCls != nullptr && (paramCls == cls || cls->hasAncestor(paramCls));
            // A primitive's own `operator = (base)` (`digits : int` taking an int) is its declared route.
            bool ownPrimitiveFromBase = cls->isPrimitive && paramCls != cls
                && find(cls->members.begin(), cls->members.end(), (typeMember*)fn) != cls->members.end();
            if(paramIsClsOrAncestor && !valueIsCls && !ownPrimitiveFromBase) return false;   // not a `cls`: no plain assignment
            bool inherited = paramCls != nullptr && paramCls != cls && cls->hasAncestor(paramCls) && !ownPrimitiveFromBase;
            if(inherited && !allowInherited) return false;
            // A value class copies through a copy operator: never through an inherited reference
            // assignment (such as _bglObject's), which would re-point instead.
            if(inherited && isValueClass(cls))
                if(classDef* decl = cls->declaringClassOf(fn); decl != nullptr && !isValueClass(decl)) return false;
            return true;
        });
        if(m) return dynamic_cast<functionDef*>(m);
    }
    return nullptr;
}

// True when `fn` is plain assignment for class `cls`: its parameter is `cls` itself or one of its
// ancestors (as opposed to a conversion from some other type, such as a setter's `operator = (int)`).
bool bglParser::isPlainAssignOperator(classDef* cls, functionDef* fn){
    if(cls == nullptr || fn == nullptr || fn->params.size() != 1) return false;
    classDef* paramCls = getDispatchClass(fn->params[0]->type.name);
    return paramCls != nullptr && (paramCls == cls || cls->hasAncestor(paramCls));
}

// Does the target type's `operator =` accept an `argType` value? Only a conversion counts here — plain
// assignment of the class's own values is the class hierarchy's business (isTypeCompatible).
bool bglParser::compatibleViaAssignmentOperator(const string& argType, const string& paramType){
    classDef* cls = getDispatchClass(paramType);
    if(cls == nullptr) return false;
    return findAssignOperator(cls, argType, [](functionDef*){ return true; }, /*allowInherited*/false) != nullptr;
}

// The type of a global function's NAME used as a value: its signature, `func<ret,param,…>`, so a
// func<…> slot can check it. An overloaded name denotes a set, not one signature, so it stays the
// unchecked `func` (as a lambda does).
// The type a literal's pseudo-type stands for (`intLiteral` → `int`); any other type unchanged.
bool bglParser::expectedTypeIsEnum(){
    return !currentExpectedType.empty() && languageService.findEnum(currentExpectedType) != nullptr;
}

string bglParser::literalBaseType(const string& t){
    if(t == "intliteral" || t == "negativeintliteral") return "int";
    if(t == "stringliteral") return "string";
    if(t == "charliteral") return "char";
    if(t == "nullliteral") return "_bglobject";
    return t;
}

string bglParser::funcValueType(const string& name){
    functionDef* only = nullptr;
    for(typeDef* g : languageService.globals)
        if(auto* fd = dynamic_cast<functionDef*>(g))
            if(fd->name == name){ if(only) return "func"; only = fd; }
    if(only == nullptr) return "func";
    string t = "func<" + (only->returnType.name.empty() ? string("void") : only->returnType.name);
    for(paramDef* p : only->params) t += "," + p->type.name;
    return t + ">";
}

// Split `func<a,b<c,d>,e>` into its top-level type arguments.
vector<string> funcSignatureArgs(const string& t){ return funcTypeArgs(t); }
static vector<string> funcTypeArgs(const string& t){
    vector<string> out;
    size_t lt = t.find('<');
    if(lt == string::npos || t.back() != '>') return out;
    string inner = t.substr(lt + 1, t.size() - lt - 2);
    int depth = 0; string cur;
    for(char c : inner){
        if(c == '<') depth++;
        else if(c == '>') depth--;
        if(c == ',' && depth == 0){ out.push_back(cur); cur.clear(); continue; }
        if(c != ' ') cur += c;
    }
    out.push_back(cur);
    return out;
}

// Two func<…> types match when they have the same number of type arguments and each position agrees:
// the same type, `var` on either side, a type compatible in either direction, or a position the target
// leaves as an unresolved type parameter (`T` in a generic method's `func<bool,T>`).
// Whether a generic value's type arguments fit the target's: `array<int>` does not fit `array<string>`.
// Each argument must be the same type, `var` on either side, compatible with the target's, or a
// position the target leaves as an unresolved type parameter (`T`). Non-generic sides always fit.
bool bglParser::templateArgsFit(const string& valueType, const string& targetType){
    if(valueType.find('<') == string::npos || targetType.find('<') == string::npos) return true;
    vector<string> a = funcTypeArgs(valueType), p = funcTypeArgs(targetType);
    if(a.size() != p.size()) return false;
    for(size_t i = 0; i < a.size(); i++){
        if(a[i] == p[i] || a[i] == "var" || p[i] == "var") continue;
        if(!languageService.isClassType(p[i]) && languageService.findEnum(p[i]) == nullptr
           && p[i].find('<') == string::npos && p[i].find('|') == string::npos) continue;
        if(!isTypeCompatible(a[i], p[i])) return false;
    }
    return true;
}

// An array variable reads as the bare `array` / `rawarray`; its element type lives on the declaration.
// For a plain variable name, recover the full `array<T>` so element types can be compared.
string bglParser::elementAwareType(const expression* e, functionDef* func, statementBlock* body){
    const string& t = e->resolvedType;
    if((t == "array" || t == "rawarray") && e->tokens.size() == 1){
        const string& n = e->tokens[0];
        if(!n.empty() && all_of(n.begin(), n.end(), [](char c){ return isalnum((unsigned char)c) || c == '_'; })){
            string elem = resolveArrayElementType(n, func ? func : currentFunc, body);
            if(!elem.empty()) return t + "<" + elem + ">";
        }
    }
    return t;
}

// A generic target (`array<string>`) rejects a value whose type arguments don't fit it.
bool bglParser::genericValueFits(const expression* e, const string& targetType, functionDef* func, statementBlock* body){
    if(e == nullptr || targetType.find('<') == string::npos || targetType.rfind("func<", 0) == 0) return true;
    return templateArgsFit(elementAwareType(e, func, body), targetType);
}

bool bglParser::funcSignaturesCompatible(const string& argType, const string& paramType){
    if(argType == paramType) return true;
    vector<string> a = funcTypeArgs(argType), p = funcTypeArgs(paramType);
    if(a.empty() || p.empty() || a.size() != p.size()) return false;
    for(size_t i = 0; i < a.size(); i++){
        if(a[i] == p[i] || a[i] == "var" || p[i] == "var") continue;
        // A func value is called with raw words: a float and an int are different signatures.
        if((a[i] == "float") != (p[i] == "float")) return false;
        if(!languageService.isClassType(p[i]) && languageService.findEnum(p[i]) == nullptr
           && p[i].find('<') == string::npos && p[i].find('|') == string::npos && p[i] != "void") continue;
        if(isTypeCompatible(a[i], p[i]) || isTypeCompatible(p[i], a[i])) continue;
        return false;
    }
    return true;
}

bool bglParser::isTypeCompatible(std::string argType, std::string paramType){
    // Inside the declaring class's own code, `selfType` is that class.
    if(string own = currentSelfTypeName(); !own.empty()){
        if(argType == "selftype") argType = own;
        if(paramType == "selftype") paramType = own;
    }
    if(paramType == "var") return true;  // var accepts any type without checking
    if(argType == "var") return true;    // var is assignable to any type (untyped source)
    if(argType == "nullliteral") return true;   // `null`, the empty reference, fits every type
    if(argType == paramType) return true;
    if((argType == "bool" && paramType == "ebool") || (argType == "ebool" && paramType == "bool")) return true;   // §2.2
    // A nested array<char> element keeps its spelled type; the declared form is byteArray.
    auto byteForm = [](const std::string& t){ return t == "array<char>" ? std::string("bytearray") : t; };
    if(byteForm(argType) == byteForm(paramType)) return true;
    // Named union → structural expansion: a named union (`union X = A|B`) is transparent for
    // assignment/passing (member lookup stays nominal elsewhere via its classDef). Expand here so
    // the structural-union rules below apply, then fall through.
    { string ax = unionExpansionOf(argType);   if(!ax.empty()) argType = ax; }
    { string px = unionExpansionOf(paramType); if(!px.empty()) paramType = px; }
    if(argType == paramType) return true;
    // An alias class is its parent under another name, in either direction.
    {
        auto dealias = [&](const std::string& t){
            classDef* c = languageService.findClass(t);
            while(c != nullptr && c->isAlias && !c->baseClasses.empty()) c = c->baseClasses[0];
            return c != nullptr ? c->name : t;
        };
        std::string ad = dealias(argType), pd = dealias(paramType);
        if(ad != argType || pd != paramType) return isTypeCompatible(ad, pd);
    }
    // Union types. A value/union is assignable INTO a union when every member of the source is
    // compatible with some member of the target; a value is assignable into a union when it is
    // compatible with some member. A UNION is NOT assignable into a plain member type — that
    // direction requires an explicit `(Member)x` narrowing cast (handled at the cast site).
    if(isUnionType(paramType) || isUnionType(argType)){
        std::vector<std::string> am = isUnionType(argType)   ? splitUnionType(argType)   : std::vector<std::string>{argType};
        std::vector<std::string> pm = isUnionType(paramType) ? splitUnionType(paramType) : std::vector<std::string>{paramType};
        for(const auto& a : am){
            bool matched = false;
            for(const auto& p : pm) if(isTypeCompatible(a, p)){ matched = true; break; }
            if(!matched) return false;
        }
        return true;
    }
    // bglclass accepts any registered class as an operand. Lets `obj.is(Foo)` work for
    // every class regardless of whether `Foo` resolved as its own type (non-extern,
    // visible in `globals`) or fell through to the isKnownClassName fallback (extern).
    if(paramType == "bglclass" && languageService.isClassType(argType)) return true;
    // bnum → int implicit widening: bnums are int-valued bitmasks by definition.
    // Narrowing (int → bnum) still requires an explicit cast.
    if(paramType == "int"){
        auto* ed = languageService.findEnum(argType);
        if(ed && ed->isBnum) return true;
    }
    // char → int widening: a char is a byte-sized non-negative integer (0..255), so it widens to int
    // freely, exactly like bnum above. This is what makes byte arrays usable numerically — reading
    // `array<char>` elements into ints (`int a = bytes[i]`) and doing arithmetic on them. The reverse
    // (int → char) stays a narrowing that needs an explicit cast, except inside array-element
    // initializers/writes where the intent is unambiguous (see isArrayElementCompatible).
    if(paramType == "int" && (argType == "char" || argType == "charliteral")) return true;
    // bnum → ancestor bnum (via shared-base chain): a child bnum is compatible with any
    // ancestor in its baseBnum chain. Mirrors class subtype compatibility for bnum families.
    {
        auto* argEnum = languageService.findEnum(argType);
        auto* paramEnum = languageService.findEnum(paramType);
        if(argEnum && argEnum->isBnum && paramEnum && paramEnum->isBnum){
            for(enumDef* a = argEnum->baseBnum; a; a = a->baseBnum)
                if(a == paramEnum) return true;
        }
    }
    // func<...> compatibility: a func value is compatible with any func<...> param type
    if(argType == "func" && paramType.rfind("func<", 0) == 0) return true;
    if(argType.rfind("func<", 0) == 0 && paramType.rfind("func<", 0) == 0) return funcSignaturesCompatible(argType, paramType);
    // array<T> compatibility: array is compatible with array<T> param
    if(argType == "array" && paramType.rfind("array<", 0) == 0) return true;
    if(argType.rfind("array<", 0) == 0 && paramType.rfind("array<", 0) == 0) return templateArgsFit(argType, paramType);
    // An object declared as its own type is compatible with its class and that class's ancestors.
    if(languageService.findObjectType(argType) != nullptr){
        classDef* oc = getDispatchClass(argType);
        classDef* pc = languageService.findClass(paramType);
        if(oc != nullptr && pc != nullptr && (oc == pc || oc->hasAncestor(pc))) return true;
    }
    // Object subtyping is handled by the class hierarchy check below —
    // only classes that actually inherit from 'object' are compatible with it.
    // Class hierarchy: argType is compatible with paramType if argType inherits from paramType
    {
        classDef* argCls2 = getDispatchClass(argType);
        classDef* paramCls2 = getDispatchClass(paramType);
        if(argCls2 && paramCls2){
            if(argCls2->hasAncestor(paramCls2)) return true;
        }
    }
    // Block implicit upcast: if argType is an ancestor of paramType, object-level operator= inherited
    // from the base should not be used to allow assigning a less-specific type to a more-specific one.
    // e.g. assigning an 'object' to a 'room' variable must fail even though room inherits object's operator=.
    {
        classDef* argCls3  = getDispatchClass(argType);
        classDef* paramCls3 = getDispatchClass(paramType);
        if(argCls3 && paramCls3 && argCls3 != paramCls3 && paramCls3->hasAncestor(argCls3)){
            // A primitive over its base (`digits : int`) that declares its own `operator =` for the
            // value takes it; only the base's inherited one is no route.
            bool ownAssign = paramCls3->isPrimitive && any_of(paramCls3->members.begin(), paramCls3->members.end(),
                [&](typeMember* m){ auto* f = dynamic_cast<functionDef*>(m);
                    if(!f || f->name != "=" || f->params.size() != 1) return false;
                    const string& pt = f->params[0]->type.name;
                    return pt == argType || pt == literalBaseType(argType)
                        || (pt == "int" && (argType == "char" || argType == "charliteral")); });
            if(!ownAssign) return false;
        }
    }
    if(compatibleViaAssignmentOperator(argType, paramType)) return true;
    // Check source type's conversion operator: emitter <paramType> operator()
    classDef* argCls = getDispatchClass(argType);
    if(argCls != nullptr && findMemberInHierarchy(argCls, [&](typeMember* m){
        auto* fn = dynamic_cast<functionDef*>(m);
        return fn && fn->name == "operator()" && fn->params.empty() && !fn->isExplicit && fn->returnType.name == paramType;
    })) return true;
    return false;
}

// Element-type compatibility for array / list initializers. Same as isTypeCompatible, plus one
// initializer-only relaxation: a byte array (element type `char`) accepts integer literals, since a
// char is a byte-sized int — so `array<char> = {5, 10, 15}` type-checks. This is kept SEPARATE from
// isTypeCompatible on purpose: a global intliteral→char rule perturbs operator-overload resolution
// (it changes `<` / `==` dispatch and thus emitted spacing), so the relaxation is scoped to array
// element checks only. Out-of-byte-range values are caught by Inform 6 when the byte array is emitted.
bool bglParser::isArrayElementCompatible(string argType, string elementType){
    // Byte array (element type `char`) accepts integer values — literals and plain `int` — as
    // elements: `array<char> t = {5, 10, 15}` and `t[i] = someInt`. This is a scoped int→char
    // narrowing, allowed only where the byte-array intent is explicit (initializers and element
    // writes), never as a global rule (that perturbs operator-overload dispatch). Integer LITERALS
    // outside 0..255 are rejected at compile time by checkByteElementRange (called at the same
    // sites); non-literal int values that overflow a byte wrap at runtime, like any byte assignment.
    if(elementType == "char" && (argType == "intliteral" || argType == "negativeintliteral" || argType == "int"))
        return true;
    return isTypeCompatible(argType, elementType);
}

// A byte array (`array<char>`) stores unsigned bytes (0..255). When an element is given as an integer
// LITERAL we know its value at compile time, so reject anything that would not fit — otherwise it
// silently wraps to its low byte at runtime (300 -> 44), a quiet data bug. Non-literal int values
// (variables, expressions) can't be checked here and wrap at runtime, matching a byte assignment.
void bglParser::checkByteElementRange(expression* elem, const string& elementType){
    if(elementType != "char" || elem == nullptr) return;
    if(elem->resolvedType != "intliteral" && elem->resolvedType != "negativeintliteral") return;
    string t = elem->text();
    size_t s = t.find_first_not_of(" \t\r\n"); if(s == string::npos) return; t = t.substr(s);
    size_t e = t.find_last_not_of(" \t\r\n"); if(e != string::npos) t = t.substr(0, e + 1);
    long v;
    try { v = stol(t, nullptr, 0); } catch(...) { return; }  // not a plain numeric literal — skip
    if(v < 0 || v > 255)
        parsingError(format("Byte value {0} is out of range for array<char> (must be 0..255).", v));
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


// A value that reaches a bare-word type (an emitter class such as `float`) only through that type's
// `operator =` must pass through it wherever it lands, not only at an assignment: `float`'s
// `operator = (int)` is the @numtof that turns 5 into 5.0, and without it an argument or return value
// arrives as the raw int bits. Applies when the operator is an emitter whose body is the single
// statement `$target = <expr>`; <expr> is substituted for the value. Returns true if it rewrote `e`.
// Enumerations, structural unions and func<…> types have no class, so the operator= dispatch that type-checks an
// assignment or initializer never runs for them. Hold them to the same rule a function argument
// meets: the value must be isTypeCompatible with the target. `what` names the target in the error.
void bglParser::checkClasslessAssignable(const expression* value, const string& targetType, const string& what){
    if(value == nullptr || targetType.empty()) return;
    const string& valueType = value->resolvedType;
    if(valueType.empty() || value->text() == "nothing") return;   // `null` fits every type
    if(!genericValueFits(value, targetType, currentFunc, nullptr))
        parsingError(format("Cannot assign value of type '{0}' to {1} of type '{2}'",
                            typeDisplayName(elementAwareType(value, currentFunc, nullptr)), what, typeDisplayName(targetType)));
    if(languageService.findEnum(targetType) == nullptr && !isUnionType(targetType)
       && targetType.rfind("func<", 0) != 0) return;
    if(!isTypeCompatible(valueType, targetType))
        parsingError(format("Cannot assign value of type '{0}' to {1} of type '{2}'",
                            typeDisplayName(valueType), what, typeDisplayName(targetType)));
}

// A `return` value is held to the rule an assignment into the return type meets, and converted the same
// way: through the return type's `operator =` (int → float), or an implicit conversion operator on the
// value's type (a Celsius returned as int). `fnName` names the function in the error.
void bglParser::checkReturnValue(expression* e, const string& returnType, const string& fnName){
    if(e == nullptr || looseIdentifierMode) return;
    if(returnType.empty() || returnType == "void" || returnType == "var") return;
    const string valueType = e->resolvedType;
    if(valueType.empty() || valueType == "var" || valueType == "void" || e->text() == "nothing") return;
    applyImplicitConversion(e, returnType);
    applyAssignOperatorAsValue(e, returnType);
    auto fail = [&](const string& shown){
        parsingError(format("Cannot return a value of type '{0}' from '{1}', which returns '{2}'",
                            typeDisplayName(shown), fnName, typeDisplayName(returnType)));
    };
    if(!genericValueFits(e, returnType, currentFunc, nullptr)) fail(elementAwareType(e, currentFunc, nullptr));
    if(e->resolvedType == returnType) return;
    if(!isTypeCompatible(e->resolvedType, returnType)) fail(e->resolvedType);
    // Compatible through the value's own implicit conversion operator: apply it, as an assignment does.
    if(classDef* cls = getDispatchClass(e->resolvedType))
        if(findMemberInHierarchy(cls, [&](typeMember* m){
               auto* fn = dynamic_cast<functionDef*>(m);
               return fn && fn->name == "operator()" && fn->params.empty() && !fn->isExplicit
                      && fn->returnType.name == returnType;
           })){
            string converted = applyCastConversion(e->text(), e->resolvedType, returnType);
            e->tokens.clear();
            e->tokens.push_back(converted);
            e->resolvedType = returnType;
        }
}

// A value passed to a routine's parameter of another type takes that type's conversion (int → float).
void bglParser::convertToParam(expression* e, const string& valueType, const string& paramType){
    if(e == nullptr || valueType.empty() || valueType == "var" || paramType.empty() || paramType == "var"
       || valueType == paramType) return;
    string text = e->text();
    string converted = applyCastConversion(text, valueType, paramType);
    if(converted == text) return;
    e->tokens.assign(1, converted);
    e->resolvedType = paramType;
}

bool bglParser::applyAssignOperatorAsValue(expression* e, const string& targetType){
    if(e == nullptr) return false;
    const string argType = e->resolvedType;
    if(argType.empty() || argType == "var" || targetType.empty() || targetType == "var" || argType == targetType)
        return false;
    classDef* cls = getDispatchClass(targetType);
    if(cls == nullptr || !cls->isEmitterClass) return false;
    auto isAssignEmitter = [](typeMember* m){
        auto* fn = dynamic_cast<functionDef*>(m);
        return fn && fn->name == "=" && fn->isEmitter && fn->params.size() == 1
               && dynamic_cast<i6Block*>(fn->body) != nullptr;
    };
    auto* op = dynamic_cast<functionDef*>(findMemberInHierarchy(cls, [&](typeMember* m){
        return isAssignEmitter(m) && dynamic_cast<functionDef*>(m)->params[0]->type.name == argType;
    }));
    if(op == nullptr)
        op = dynamic_cast<functionDef*>(findMemberInHierarchy(cls, [&](typeMember* m){
            if(!isAssignEmitter(m)) return false;
            const string& pt = dynamic_cast<functionDef*>(m)->params[0]->type.name;
            return pt != targetType && !isPlainAssignOperator(cls, dynamic_cast<functionDef*>(m))
                   && isTypeCompatible(argType, pt);
        }));
    if(op == nullptr) return false;
    string body = dynamic_cast<i6Block*>(op->body)->i6Body;
    size_t a = body.find_first_not_of(" \t\r\n");
    if(a == string::npos || body.compare(a, 7, "$target") != 0) return false;
    size_t eq = body.find_first_not_of(" \t", a + 7);
    if(eq == string::npos || body[eq] != '=' || (eq + 1 < body.size() && body[eq + 1] == '=')) return false;
    string rhs = body.substr(eq + 1);
    size_t z = rhs.find_last_not_of(" \t\r\n;");
    if(z == string::npos) return false;
    rhs = rhs.substr(0, z + 1);
    if(rhs.find(';') != string::npos) return false;   // more than one statement: not a plain value
    i6Block tmp; tmp.i6Body = rhs;
    emitterBindings bind;
    bind.fn = op; bind.args = { e->text() };
    bind.val = e->text();
    bind.trim = emitterTrim::wsSemi;
    string b = expandEmitterBody(&tmp, bind);
    if(b.empty()) return false;
    e->tokens.clear();
    e->tokens.push_back(b);
    e->resolvedType = targetType;
    return true;
}

bool bglParser::applyActionConstant(expression* e){
    if(e == nullptr || e->resolvedType != "verb" || e->tokens.size() != 1) return false;
    string t = e->tokens[0];
    if(t.rfind("##", 0) == 0) return false;
    string tl = t; transform(tl.begin(), tl.end(), tl.begin(), ::tolower);
    if(tl == "action" || tl == "action_to_be" || tl == "second_action") return false;
    for(verbObjectDef* v : languageService.verbs){
        string vi6 = v->i6name; transform(vi6.begin(), vi6.end(), vi6.begin(), ::tolower);
        if(v->name != tl && vi6 != tl) continue;
        e->tokens[0] = "##" + v->dName();   // an action is named for the verb, whatever its object's I6 name
        return true;
    }
    return false;
}

bool bglParser::zeroFitsBnum(expression* e, const string& targetType){
    if(e == nullptr || e->resolvedType != "intliteral" || e->text() != "0") return false;
    enumDef* ed = languageService.findEnum(targetType);
    return ed != nullptr && ed->isBnum;
}

// An argument that fits a parameter by a rule rather than by its type: a literal 0 for a bnum, or a
// bare member name for a `property` parameter, which names the property (§11.7.3).
bool bglParser::argFitsByRule(expression* e, const string& paramType){
    if(zeroFitsBnum(e, paramType)) return true;
    if(e == nullptr || paramType != "property") return false;
    string t = e->text();
    return t.rfind("self.", 0) == 0 && languageService.isKnownPropertyName(t.substr(5));
}

// An integer literal where a float is stored is that float literal (`80` → `$+80.0`), so it needs no
// conversion at run time — which a value fixed when the program is compiled can't have.
bool bglParser::foldIntLiteralToFloat(expression* e, const string& targetType){
    if(e == nullptr || targetType != "float") return false;
    if(e->resolvedType != "intliteral" && e->resolvedType != "negativeintliteral") return false;
    string t = e->text(), digits = t;
    t.erase(remove(t.begin(), t.end(), ' '), t.end());
    bool negative = !t.empty() && t[0] == '-';
    digits = negative ? t.substr(1) : t;
    if(digits.size() > 2 && digits.front() == '(' && digits.back() == ')') digits = digits.substr(1, digits.size() - 2);
    if(digits.empty() || digits.find_first_not_of("0123456789") != string::npos) return false;
    e->tokens.assign(1, string("$") + (negative ? "-" : "+") + digits + ".0");
    e->resolvedType = "float";
    return true;
}

bool bglParser::applyImplicitConversion(expression* e, const string& targetType){
    if(e == nullptr || targetType.empty() || targetType == "var") return false;
    if(foldIntLiteralToFloat(e, targetType)) return true;
    const string valueType = e->resolvedType;
    if(valueType.empty() || valueType == "var" || valueType == targetType) return false;
    // A literal 0 is every bnum's empty set (§2.7.2).
    if(valueType == "intliteral" && e->text() == "0")
        if(enumDef* ed = languageService.findEnum(targetType); ed != nullptr && ed->isBnum){
            e->resolvedType = targetType;
            return true;
        }
    classDef* vc = getDispatchClass(valueType);
    if(vc == nullptr) return false;
    if(classDef* tc = getDispatchClass(targetType))
        if(findAssignOperator(tc, valueType, [](functionDef*){ return true; }, true, targetType)) return false;
    bool hasConversion = findMemberInHierarchy(vc, [&](typeMember* m){
        auto* fn = dynamic_cast<functionDef*>(m);
        return fn && fn->name == "operator()" && fn->params.empty() && !fn->isExplicit
               && fn->returnType.name == targetType;
    }) != nullptr;
    if(!hasConversion) return false;
    string text = e->text();
    string converted = applyCastConversion(text, valueType, targetType);
    if(converted == text) return false;
    e->tokens.clear();
    e->tokens.push_back(converted);
    e->resolvedType = targetType;
    return true;
}

void bglParser::applyArgConversions(std::vector<expression*>& args, functionDef* fd){
    for(size_t i = 0; i < args.size() && i < fd->params.size(); i++){
        string paramType = fd->params[i]->type.name;
        // A default filled in for a float parameter converts as a written argument would (`by = 2`).
        if(args[i]->resolvedType.empty() && paramType == "float" && args[i]->tokens.size() == 1
           && args[i]->tokens[0] == fd->params[i]->defaultValue)
            args[i]->resolvedType = defaultValueType(fd->params[i]->defaultValue);
        string argType = args[i]->resolvedType;
        // An array parameter takes one reference, and a member array is property data on its owner
        // with no address of its own to give (§12.7): reject it here rather than at run time.
        if(isWordArrayType(argType) && (isWordArrayType(paramType) || paramType == "var")){
            string text = args[i]->text();
            if(text.find('(') == string::npos && text.find('.') != string::npos){
                statementBlock* cb = currentFunc != nullptr ? dynamic_cast<statementBlock*>(currentFunc->body) : nullptr;
                string recv = text;
                ArrayReceiver r = arrayReceiver(recv, text, argType, currentFunc, cb);
                if(r.isMember)
                    parsingError(format("'{0}' is a member array, which can't be passed as an argument: an array "
                        "parameter takes a global, local or `ref` array. Pass its owner instead, or declare the "
                        "member `ref` and bind it to an array with `:=`.", text));
            }
        }
        // A verb named as an argument is its action constant (`Take` → ##Take); a verb-typed variable or
        // one of the library's action globals already holds an action value and passes as it is.
        if(argType == "verb" && (paramType == "verb" || paramType == "var")){
            applyActionConstant(args[i]);
            continue;
        }
        if(paramType == "var" || argType == paramType || argType.empty()) continue;
        // Look for conversion operator on arg's class
        classDef* argCls = getDispatchClass(argType);
        if(argCls == nullptr){ applyAssignOperatorAsValue(args[i], paramType); continue; }
        typeMember* found = findMemberInHierarchy(argCls, [&](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn && fn->name == "operator()" && fn->params.empty() && fn->isEmitter && !fn->isExplicit && fn->returnType.name == paramType && dynamic_cast<i6Block*>(fn->body) != nullptr;
        });
        if(!found){
            // A conversion written as a regular method (a property accessor's getter) runs the same
            // way: `self.id` passed to an int parameter must call the accessor, not pass the accessor
            // object itself.
            bool methodConversion = findMemberInHierarchy(argCls, [&](typeMember* m){
                auto* fn = dynamic_cast<functionDef*>(m);
                return fn && fn->name == "operator()" && fn->params.empty() && !fn->isEmitter
                       && !fn->isExplicit && fn->returnType.name == paramType;
            }) != nullptr;
            if(methodConversion){
                string converted = applyCastConversion(args[i]->text(), argType, paramType);
                args[i]->tokens.clear();
                args[i]->tokens.push_back(converted);
                args[i]->resolvedType = paramType;
                continue;
            }
            applyAssignOperatorAsValue(args[i], paramType);
            continue;
        }
        {
            auto* fn = dynamic_cast<functionDef*>(found);
            auto* blk = dynamic_cast<i6Block*>(fn->body);
            // $self = host of property access (parentProp's `parent($self)` etc.).
            // $val  = full receiver expression as written (`obj.parent`, `5`, `localInt`).
            // For non-property contexts the two coincide.
            emitterBindings ac;
            ac.self = !args[i]->emitterSelf.empty() ? args[i]->emitterSelf : args[i]->text();
            ac.val  = args[i]->text();
            ac.trim = emitterTrim::wsSemi;
            string b = expandEmitterBody(blk, ac);
            if(b.empty()){
                // Empty body = pass-through conversion; just update the type
                args[i]->resolvedType = paramType;
            } else {
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
// prevents the drift of a caller forgetting one step (e.g. default fill missing from the
// expression path, silently zeroing omitted args at runtime).

void bglParser::checkLiteralValue(const vector<string>& literalMembers, const string& slotType,
                                  const expression* value, const string& what){
    if(value == nullptr) return;
    const string& valueType = value->resolvedType;
    if(valueType.empty()) return;
    // The value is checked when it fits a literal member of the slot's type and no other member.
    vector<string> members = isUnionType(slotType) ? splitUnionType(slotType) : vector<string>{slotType};
    bool fitsLiteral = false, fitsOther = false;
    string literalMember;
    for(const string& m : members){
        if(valueType != m && !isTypeCompatible(valueType, m)) continue;
        if(find(literalMembers.begin(), literalMembers.end(), m) != literalMembers.end()){
            fitsLiteral = true;
            if(literalMember.empty()) literalMember = m;
        }
        else fitsOther = true;
    }
    string why;
    if(!fitsLiteral || fitsOther || isLiteralValue(value, why)) return;
    string kind = literalMember.rfind("func<", 0) == 0 ? "function" : typeDisplayName(literalMember);
    string msg = what;
    size_t k = msg.find("{KIND}");
    if(k != string::npos) msg.replace(k, 6, kind);
    parsingError(format("{0}, known at compile time: {1}.", msg, why));
}

void bglParser::checkLiteralElements(const arrayDeclaration& arr, const vector<expression*>& values){
    const string& t = arr.elementType;
    vector<string> members = isUnionType(t) ? splitUnionType(t) : vector<string>{t};
    for(expression* v : values)
        checkLiteralValue(members, t, v, format("'{0}' holds literal elements: it needs a literal {{KIND}}", arr.dName()));
}

// The declaration an assignment target names: a local, a member of `self` or of a named object or
// class-typed value, or a global.
variableDeclaration* bglParser::findAssignedDeclaration(const string& path, functionDef* func, statementBlock* body){
    auto memberOf = [&](classDef* cls, const string& name) -> variableDeclaration* {
        if(cls == nullptr) return nullptr;
        return dynamic_cast<variableDeclaration*>(findMemberInHierarchy(cls, [&](typeMember* m){ return m->name == name; }));
    };
    auto objectMember = [&](objectDef* od, const string& name) -> variableDeclaration* {
        if(od == nullptr) return nullptr;
        variableDeclaration* inherited = memberOf(od->objectClass, name);
        // An object's value for a class's `literal` member is still that member: keep its qualifier.
        if(inherited != nullptr && inherited->isLiteral) return inherited;
        for(typeMember* m : od->members)
            if(m->name == name) if(auto* vd = dynamic_cast<variableDeclaration*>(m)) return vd;
        return inherited;
    };
    size_t dot = path.rfind('.');
    if(dot == string::npos){
        function<variableDeclaration*(statementBlock*)> inBlock = [&](statementBlock* blk) -> variableDeclaration* {
            if(blk == nullptr) return nullptr;
            for(statement* st : blk->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(st)) if(vd->name == path) return vd;
            return nullptr;
        };
        if(auto* vd = inBlock(body)) return vd;
        for(statementBlock* blk : activeBlockStack) if(auto* vd = inBlock(blk)) return vd;
        if(currentFunc != nullptr) if(auto* vd = inBlock(dynamic_cast<statementBlock*>(currentFunc->body))) return vd;
        if(auto* vd = objectMember(currentObject, path)) return vd;
        if(auto* vd = memberOf(currentClass, path)) return vd;
        return languageService.findGlobalAs<variableDeclaration>(path);
    }
    string head = path.substr(0, dot), member = path.substr(dot + 1);
    if(head == "self"){
        if(auto* vd = objectMember(currentObject, member)) return vd;
        return memberOf(currentClass, member);
    }
    if(auto* od = languageService.findObjectType(head)) if(auto* vd = objectMember(od, member)) return vd;
    return memberOf(languageService.classOf(resolveIdentifierType(head, func, body)), member);
}

// The variable a lambda in `e` captures (other than a `const` with a compile-time value), or "".
string bglParser::capturedByLambdaIn(const expression* e){
    if(e == nullptr) return "";
    for(const string& t : e->tokens){
        if(t.rfind("_bglLambda_", 0) != 0) continue;
        for(typeDef* g : languageService.globals)
            if(auto* fd = dynamic_cast<functionDef*>(g))
                if(fd->name == t)
                    for(auto& cap : fd->captures)
                        if(!cap.isConstant) return cap.outerName;
    }
    return "";
}

// A `literal` value is known at compile time: a literal, a constant, an enum member, an object or
// class, a named function, a lambda that captures nothing, or a `literal` parameter passed on. The
// expression's emitted tokens are checked one by one; operators and punctuation are skipped.
bool bglParser::isLiteralValue(const expression* e, string& why){
    if(e->resolvedType == "nullliteral") return true;   // `null` is known at compile time
    auto sameName = [](const string& a, const string& b){ return strcasecmp(a.c_str(), b.c_str()) == 0; };
    for(size_t ti = 0; ti < e->tokens.size(); ti++){
        const string& t = e->tokens[ti];
        if(t.empty()) continue;
        char c = t[0];
        if((isalpha((unsigned char)c) || c == '_') && ti + 1 < e->tokens.size() && e->tokens[ti + 1] == "("){
            why = format("'{0}()' is a call, evaluated at run time", t);
            return false;
        }
        if(c == '"' || c == '\'' || c == '$' || isdigit((unsigned char)c)) continue;   // literal text or number
        if(!isalpha((unsigned char)c) && c != '_') continue;                           // operator, punctuation
        // A `literal` variable or member holds only literal values, so reading one is literal too.
        auto readsLiteralSlot = [&](){
            variableDeclaration* vd = findAssignedDeclaration(t, currentFunc, nullptr);
            return vd != nullptr && vd->isLiteral;
        };
        if(t.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != string::npos){
            if(readsLiteralSlot()) continue;
            why = format("'{0}' is read at run time", t);                             // a member or property read
            return false;
        }
        if(t.rfind("_bglLambda_", 0) == 0){
            expression one; one.tokens.push_back(t);
            string captured = capturedByLambdaIn(&one);
            if(!captured.empty()){ why = format("the lambda captures '{0}'", captured); return false; }
            continue;
        }
        if(currentFunc != nullptr){
            bool isParam = false;
            for(paramDef* p : currentFunc->params)
                if(sameName(p->name, t)){
                    isParam = true;
                    if(p->literalMembers.empty()){ why = format("'{0}' is a variable", p->displayName.empty() ? p->name : p->displayName); return false; }
                }
            if(isParam) continue;
        }
        bool known = false;
        // A static method named as a value (`Calc.dbl`) is a named function: its routine.
        for(typeDef* ot : languageService.objectTypes){
            auto* cd = dynamic_cast<classDef*>(ot);
            if(cd == nullptr || known) continue;
            for(typeMember* m : cd->members)
                if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->isStatic && !fd->isEmitter
                   && sameName(i6Emitter::staticRoutineName(cd, fd), t)){ known = true; break; }
        }
        if(known) continue;
        for(typeDef* g : languageService.globals){
            if(!sameName(g->name, t) && !sameName(g->i6name, t)) continue;
            known = true;
            if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                if(!vd->isConst && !vd->isNamespaceAlias() && !vd->isLiteral){ why = format("'{0}' is a variable", vd->dName()); return false; }
            break;
        }
        if(!known && !readsLiteralSlot()){ why = format("'{0}' is a variable", t); return false; }   // a local
    }
    return true;
}

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
        defExpr->tokens.push_back(paramDefaultText(fd->params[i]));
        args.push_back(defExpr);
        if(interpSegmentsPerArg.size() < args.size()) interpSegmentsPerArg.push_back({});
    }
    // A value-class parameter given another type its class's `operator =` takes (`show(7.5)`) gets
    // an instance made from the value, as a declaration `Volume a = 2.5;` does.
    for(size_t i = 0; i < args.size() && i < fd->params.size(); i++)
        if(args[i] != nullptr) convertIntoValueClass(args[i], fd->params[i]->type.name);
    // One argument a call returns may be the same instance another returns into; Inform 6
    // evaluates arguments right to left.
    for(size_t i = 0; args.size() > 1 && i < args.size(); i++){
        if(fd->isEmitter || args[i] == nullptr) continue;
        string text = args[i]->text();
        string kept = keepValueResult(text, args[i]->resolvedType);
        if(kept != text){ args[i]->tokens.clear(); args[i]->tokens.push_back(kept); }
    }
    // `literal` parameters. A literal member that is not one of the parameter's (substituted) type
    // members is a generic placeholder — `literal T` on a storing array method — and applies only
    // while binding a call on an `array<literal T>` receiver.
    for(size_t i = 0; i < args.size() && i < fd->params.size(); i++){
        paramDef* p = fd->params[i];
        if(p->literalMembers.empty()) continue;
        vector<string> members = isUnionType(p->type.name) ? splitUnionType(p->type.name) : vector<string>{p->type.name};
        vector<string> literal;
        for(const string& m : p->literalMembers){
            if(find(members.begin(), members.end(), m) != members.end()) literal.push_back(m);
            else if(bindingLiteralElements) literal.insert(literal.end(), members.begin(), members.end());
        }
        if(!literal.empty())
            checkLiteralValue(literal, p->type.name, args[i],
                format("'{0}' needs a literal {{KIND}} for '{1}'", fd->dName(),
                       p->displayName.empty() ? p->name : p->displayName));
    }
    // Property-identifier arguments: a parameter typed `property` wants the BARE I6 property
    // constant (its slot number), not a `self.<name>` value read. Inside an object method body,
    // qualifyIdentifier emits a bare known-property name as `self.<name>` (the "drop self."
    // member-read convenience) — correct for reading a value, wrong for a property identifier.
    // Because arguments are emitted before overload resolution picks this signature (see
    // parseCallArgList), we normalize here: strip a leading `self.` when the parameter is
    // `property` and the remainder names a known property. This makes a `property` parameter act
    // as an implicit `(property)` cast, so engines like orGameState can take `property task` and
    // callers pass a bare task name without a file-scope `extern property` re-declaration.
    for(size_t i = 0; i < args.size() && i < fd->params.size(); i++){
        if(fd->params[i]->type.name != "property") continue;
        string t = args[i]->text();
        if(t.rfind("self.", 0) == 0 && languageService.isKnownPropertyName(t.substr(5))){
            args[i]->tokens.clear();
            args[i]->tokens.push_back(t.substr(5));
            args[i]->resolvedType = "property";
        }
    }
    applyArgConversions(args, fd);
    // The argument of a copied-back `ref` takes the routine's final value: right after the call where
    // the call is an expression (wrapRefBacks), else once the statement is done.
    lastRefBacks.clear();
    if(!fd->isEmitter && !fd->isExternal)
        for(size_t i = 0; i < args.size() && i < fd->params.size(); i++)
            if(refCopiesBack(fd->params[i]) && args[i] != nullptr){
                i6RawNode& back = *(new i6RawNode());
                back.text = args[i]->text() + " = " + refBackGlobal(i) + ";";
                postInjections.push_back(&back);
                lastRefBacks.push_back(&back);
            }
}

// Unified dotted-method-call binding. Replaces the duplicated resolve-validate-finalize sequences
// at the statement-level method call, method call in expression, and chain-continuation sites.

// ===============================================================================
// Method-call and global-call binding entry points
// ===============================================================================
// obj.member() where `member` is a func<...> property: an indirect call through the
// property (I6 runs the routine stored in it). Synthesize a non-emitter functionDef
// carrying the func's return type so the caller emits `obj.member(args)` verbatim.
// Covers both class instances and bare object instances (and their class hierarchy).
// Returns nullptr when the receiver has no such property.
functionDef* bglParser::synthesizeFuncPropertyCall(const string& objType, const string& methodName){
    auto funcMemberType = [&](typeMember* m) -> string {
        auto* vd = dynamic_cast<variableDeclaration*>(m);
        return (vd && vd->name == methodName && vd->type.name.rfind("func<", 0) == 0) ? vd->type.name : string();
    };
    string fpt;
    if(classDef* dc = getDispatchClass(objType))
        if(typeMember* fm = findMemberInHierarchy(dc, [&](typeMember* m){ return !funcMemberType(m).empty(); }))
            fpt = funcMemberType(fm);
    if(fpt.empty())
        if(auto* od = languageService.findObjectType(objType)){
            for(typeMember* m : od->members){ string t = funcMemberType(m); if(!t.empty()){ fpt = t; break; } }
            if(fpt.empty() && od->objectClass)
                if(typeMember* fm = findMemberInHierarchy(od->objectClass, [&](typeMember* m){ return !funcMemberType(m).empty(); }))
                    fpt = funcMemberType(fm);
        }
    if(!fpt.empty()){
        size_t lt = fpt.find('<'), comma = fpt.find(',', lt), gt = fpt.rfind('>');
        string ret = (comma != string::npos && comma < gt)
            ? fpt.substr(lt+1, comma-lt-1) : fpt.substr(lt+1, gt-lt-1);
        functionDef* synth = new functionDef();
        synth->name = methodName;
        synth->returnType.name = ret;
        return synth;
    }
    return nullptr;
}

    // Build the provided-argument types string and gather candidate overloads (same name,
    // same arity), then for each candidate point at the first mismatching parameter.
void bglParser::reportMethodOverloadMismatch(const string& objType, const string& methodName,
                                             vector<expression*>& args){
    string provided;
    for(size_t i = 0; i < args.size(); i++){
        if(i) provided += ", ";
        string at = args[i]->resolvedType;
        provided += at.empty() ? "?" : typeDisplayName(at);
    }
    vector<functionDef*> candidates;
    if(auto* cls = getDispatchClass(objType))
        cls->forEachMember([&](typeMember* m){
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == methodName && !fd->isPrePassStub){
                    size_t req = 0;
                    for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
                    if(args.size() >= req && args.size() <= fd->params.size())
                        candidates.push_back(fd);
                }
        });
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

functionDef* bglParser::nativeGetterOf(const string& memberType){
    if(memberType.empty()) return nullptr;
    classDef* mcls = getDispatchClass(memberType);
    // Only an accessor (a value class) reads through its getter; a reference member is the instance itself.
    if(mcls != nullptr && !isValueClass(mcls)) return nullptr;
    // The right operand of its class's own operator (`a.weight == b.weight`) is the accessor, not its reading.
    if(mcls != nullptr && mcls == ownOperatorOperand && getDispatchClass(currentExpectedType) == mcls) return nullptr;
    auto* fn = mcls ? dynamic_cast<functionDef*>(findMemberInHierarchy(mcls, [&](typeMember* m){
        auto* f = dynamic_cast<functionDef*>(m);
        return f && f->name=="operator()" && f->params.empty() && !f->isEmitter && !f->isExplicit
               && !f->returnType.name.empty() && f->returnType.name != "void";
    })) : nullptr;
    if(fn && fn->i6name.empty()) fn->i6name = mangleOperatorName(fn->name);
    return fn;
}

bglParser::MethodMatch bglParser::resolveMethodNamed(const string& typeName, const string& objPath, const string& methodName,
                                                     vector<expression*>& args, vector<string>& namedArgNames){
    MethodMatch mm = resolveMethod(typeName, objPath, methodName, args);
    bool hasNamed = false;
    for(auto& n : namedArgNames) if(!n.empty()){ hasNamed = true; break; }
    functionDef* target = mm.method ? mm.method : mm.arityMatch;
    if(!hasNamed || target == nullptr) return mm;
    vector<vector<interpolatedSegment>> interp;
    finalizeCallArgs(args, namedArgNames, interp, target);
    return resolveMethod(typeName, objPath, methodName, args);
}

functionDef* bglParser::bindMethodCall(string& objType, const string& objPath, const string& methodNameIn,
                                        vector<expression*>& args, vector<string>& namedArgNames,
                                        vector<vector<interpolatedSegment>>& interpSegmentsPerArg,
                                        const string& elementTypeIn){
    // The <linq> operations walk a word array; a byte array's elements aren't words.
    if(objType == "bytearray"){
        string mn = methodNameIn; transform(mn.begin(), mn.end(), mn.begin(), ::tolower);
        if(classDef* arrCls = languageService.findClass("array"))
            if(auto* f = dynamic_cast<functionDef*>(findMemberInHierarchy(arrCls, [&](typeMember* m){ return m->name == mn; }));
               f != nullptr && f->src.file.find("linq.bgl") != string::npos)
                parsingError(format("'{0}' is a <linq> operation, which works on word arrays; an array<char> "
                                    "doesn't take it. Copy the characters into an array<int> first.", methodNameIn));
            else if(f != nullptr && f->isEmitter && f->src.file.find("array.bgl") != string::npos)
                if(auto* blk = dynamic_cast<i6Block*>(f->body); blk != nullptr && blk->i6Body.find("_bglArray.") != string::npos)
                    parsingError(format("'{0}' from <array> works on word arrays; an array<char> doesn't take it "
                                        "(its operations are <buf>'s).", methodNameIn));

    }
    // An `extern class` named as the receiver (`Chit.total(…)`) types as `bglClass`; its static methods
    // are the class's own.
    if(objType == "bglclass" && objPath.find('.') == string::npos){
        string lower = objPath; transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if(classDef* named = languageService.findClass(lower); named != nullptr
           && findMemberInHierarchy(named, [&](typeMember* m){ auto* f = dynamic_cast<functionDef*>(m);
                  string mn = methodNameIn; transform(mn.begin(), mn.end(), mn.begin(), ::tolower);
                  return f != nullptr && f->isStatic && f->name == mn; }))
            objType = lower;
    }
    // A receiver typed `array<T>` (an inner array, `grid[1]`) binds T from its type.
    string elementType = elementTypeIn;
    if(elementType.empty() && objType == "bytearray") elementType = "char";   // array<char>'s T
    if(elementType.empty() && (objType.rfind("array<", 0) == 0 || objType.rfind("rawarray<", 0) == 0)
       && objType.back() == '>')
        elementType = objType.substr(objType.find('<') + 1, objType.size() - objType.find('<') - 2);
    string methodName = methodNameIn;   // members are keyed in lower case
    transform(methodName.begin(), methodName.end(), methodName.begin(), ::tolower);
    // A verb object is typed `verb` (its value is the action), but its methods are its own class's
    // (an i6Verb's handler() runs the I6 action routine).
    if(objType == "verb")
        for(verbObjectDef* v : languageService.verbs)
            if(v->name == objPath && v->objectClass != nullptr){ objType = v->objectClass->name; break; }
    // On an `array<literal T>` receiver, the methods that store an element (declared `literal T`)
    // take only literal values; finalizeCallArgs reads this while binding.
    struct LiteralElementsGuard {
        bool& slot; bool saved;
        LiteralElementsGuard(bool& s, bool v) : slot(s), saved(s) { slot = v; }
        ~LiteralElementsGuard() { slot = saved; }
    };
    auto* recvArr = dynamic_cast<arrayDeclaration*>(findAssignedDeclaration(objPath, currentFunc, nullptr));
    LiteralElementsGuard literalGuard(bindingLiteralElements, recvArr != nullptr && recvArr->literalElements);
    // A rawArray<T> is a bare I6 word pointer with no length header, so its extent
    // isn't recoverable at runtime: there is no count slot to read, and I6's `.#`
    // operator is property-only (it needs an object.property operand, not a bare
    // address — `(ptr.#)` fails to compile). Reject size()/length() here, mirroring
    // the for-in rejection in processFor, rather than emit invalid I6. A file-scope
    // rawArray *literal* is count-prefixed and dispatches through the <array> path
    // (`_bglArray.size(arr, 0)`), so it never carries a `rawarray<...>` static type
    // and never reaches this guard.
    if((methodName == "size" || methodName == "length") && objType.rfind("rawarray<", 0) == 0)
        parsingError(format("'{0}' is unavailable on rawArray '{1}': a rawArray is a bare I6 word "
            "pointer with no length header, so its size isn't known at runtime. Pass the length "
            "explicitly (e.g. as a separate parameter) instead.", methodName, objPath));
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
    if(!mm.nameFound){
        if(functionDef* synth = synthesizeFuncPropertyCall(objType, methodName)) return synth;
        // A verb's name types as its action: its own members are the verb object's.
        if(objType == "verb" && objPath.find('.') == string::npos){
            string path = objPath; transform(path.begin(), path.end(), path.begin(), ::tolower);
            for(verbObjectDef* v : languageService.verbs){
                string i6 = v->i6name; transform(i6.begin(), i6.end(), i6.begin(), ::tolower);
                if(v->name != path && i6 != path) continue;
                for(typeMember* m : v->members)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd && vd->name == methodName && vd->type.name.rfind("func<", 0) == 0){
                        const string& fpt = vd->type.name;
                        size_t lt = fpt.find('<'), comma = fpt.find(',', lt), gt = fpt.rfind('>');
                        functionDef* synth = new functionDef();
                        synth->name = methodName;
                        synth->returnType.name = (comma != string::npos && comma < gt) ? fpt.substr(lt+1, comma-lt-1) : fpt.substr(lt+1, gt-lt-1);
                        return synth;
                    }
            }
        }
        // `arr.length()` where `length` is a property: say so, rather than "no method".
        if(classDef* oc = getDispatchClass(objType))
            if(findMemberInHierarchy(oc, [&](typeMember* m){
                   return dynamic_cast<variableDeclaration*>(m) != nullptr && m->name == methodName; }))
                parsingError(format("'{0}' is a property of '{1}', not a method; use it without parentheses "
                                    "('{0}', not '{0}()')", methodName, typeDisplayName(objType)));
        // An array method of `<array>`'s, on an array, in a program that doesn't include it.
        static const set<string> arrayMethods = {"append","clear","contains","copy","dequeue","enqueue","find",
            "indexof","insert","peek","peekend","pop","popend","prepend","push","remove","removevalue",
            "reverse","sort","swap","istracked","setlength"};
        if(!languageService.arrayInUse && arrayMethods.count(methodName) && getDispatchClass(objType) != nullptr
           && getDispatchClass(objType)->name == "array")
            parsingError(format("'{0}' on '{1}' comes from `<array>`: add `#include <array>`.",
                methodName, typeDisplayName(objType)));
        // If the receiver is an array and <linq> isn't included, the missing method is very
        // likely a LINQ chain op (filter/map/take/…) that now lives in <linq>. Point there.
        if(!languageService.linqInUse && getDispatchClass(objType) != nullptr
           && getDispatchClass(objType)->name == "array")
            parsingError(format("No method '{0}' on type '{1}'. If this is a LINQ chain operation "
                "(filter/map/take/skip/distinct/orderBy/first/last/any/all/count), add `#include <linq>`.",
                methodName, typeDisplayName(objType)));
        parsingError(format("No method '{0}' on type '{1}'", methodName, typeDisplayName(objType)));
    }
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
            if(auto* cls = getDispatchClass(objType))
                cls->forEachMember([&](typeMember* m){
                    if(auto* fd = dynamic_cast<functionDef*>(m))
                        if(fd->name == methodName && !fd->isPrePassStub){
                            size_t req = 0;
                            for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
                            if(args.size() >= req && args.size() <= fd->params.size()){
                                sole = fd; matchCount++;
                            }
                        }
                });
            if(matchCount == 1){
                mm.method = sole;
                finalizeCallArgs(args, namedArgNames, interpSegmentsPerArg, mm.method);
                mangleOverloadSetForReceiver(objType, methodName);
                return mm.method;
            }
        }
        reportMethodOverloadMismatch(objType, methodName, args);
    }
    // A value (`emitter int size { … }`) is read, never called.
    if(mm.method->isEmitter && mm.method->isValueEmitter)
        parsingError(format("'{0}' is an emitter value, not a function; use it without parentheses ('{0}', not '{0}()')", methodName));
    finalizeCallArgs(args, namedArgNames, interpSegmentsPerArg, mm.method);
    // An argument for an array method's element parameter (`T`) becomes an element: on an array that
    // outlives this routine (a global or a member) a capturing lambda would outlive its captures.
    if(isWordArrayType(objType) || objType.rfind("array", 0) == 0){
        bool lasting = objPath.find('.') != string::npos
                    || languageService.findGlobalAs<arrayDeclaration>(objPath) != nullptr;
        for(size_t i = 0; i < args.size() && i < mm.method->params.size(); i++){
            const string& pt = mm.method->params[i]->type.name;
            if(pt == "t" || (!elementType.empty() && pt == elementType)){
                if(lasting) rejectEscapingLambda(args[i], format("stored in '{0}'", objPath));
                else rejectLoopStoredLambda(args[i], objPath);
            }
        }
    }
    mangleOverloadSetForReceiver(objType, methodName);
    noteMethodRecursion(mm.method);
    return mm.method;
}

// A call to the method being parsed — on self or on another instance — re-enters it, so its value-class
// locals need saving across the call (prepareRecursionSave), as a self-calling function's do.
void bglParser::noteMethodRecursion(functionDef* called){
    functionDef* owner = replaceOwner();
    if(called == nullptr || owner == nullptr || called->isEmitter || (currentClass == nullptr && currentObject == nullptr)) return;
    if(called == owner || (called->name == owner->name && called->params.size() == owner->params.size()))
        owner->callsItself = true;
}

// Unified global-function-call binding. Replaces the duplicated resolveGlobalCall/validate/finalize
// sequences at the statement-level call site and parseExprFunctionCall.

string bglParser::emitterSignatureKey(const functionDef& fd){
    string key = fd.name + "(";
    for(paramDef* p : fd.params) key += p->type.name + ",";
    return key + ")";
}

functionDef* bglParser::replaceOwner(){
    if(currentFunc == nullptr || !currentFunc->isLambda) return currentFunc;
    for(functionDef* f : lambdaOuterFuncStack) if(f != nullptr && !f->isLambda) return f;
    return currentFunc;
}

// `print(v)` prints a value whose type declares its own print() through that member, as an
// interpolation segment does: the first such call gives the type a `print(T)` overload that sends it.
// Objects need none — print(_bglObject) already asks the object at run time.
// An emitter class has no instance to print: a value of one prints as what its implicit
// `operator ()` gives, unless the class declares a print() of its own.
void bglParser::convertStoragelessForPrint(expression* arg){
    classDef* cls = arg->resolvedType.empty() ? nullptr : getDispatchClass(arg->resolvedType);
    if(cls == nullptr || !cls->isEmitterClass || cls->isPrimitive || cls->isUnion()) return;
    auto* own = dynamic_cast<functionDef*>(findMemberInHierarchy(cls, [](typeMember* m){
        auto* f = dynamic_cast<functionDef*>(m); return f != nullptr && f->name == "print" && f->params.empty(); }));
    if(own != nullptr)
        if(classDef* decl = cls->declaringClassOf(own); decl != nullptr && decl->name != "_bglobject") return;
    auto* conv = dynamic_cast<functionDef*>(findMemberInHierarchy(cls, [](typeMember* m){
        auto* f = dynamic_cast<functionDef*>(m);
        return f != nullptr && f->name == "operator()" && f->params.empty() && !f->isExplicit
            && !f->returnType.name.empty() && f->returnType.name != "void"; }));
    if(conv == nullptr) return;
    arg->tokens = {applyCastConversion(arg->text(), arg->resolvedType, conv->returnType.name)};
    arg->resolvedType = conv->returnType.name;
}

void bglParser::ensureOwnPrintOverload(const string& typeName){
    if(typeName.empty() || typeName == "var") return;
    classDef* cls = getDispatchClass(typeName);
    if(cls == nullptr || isObjectBackedClass(cls)) return;
    auto* own = dynamic_cast<functionDef*>(findMemberInHierarchy(cls, [](typeMember* m){
        auto* f = dynamic_cast<functionDef*>(m);
        return f != nullptr && f->name == "print" && f->params.empty();
    }));
    if(own == nullptr) return;
    if(classDef* decl = cls->declaringClassOf(own); decl == nullptr || decl->name == "_bglobject" || decl->name == "_bglprimitive") return;
    for(typeDef* g : languageService.globals)
        if(auto* fd = dynamic_cast<functionDef*>(g); fd && fd->name == "print" && fd->params.size() == 1
           && fd->params[0]->type.name == typeName) return;
    auto* fd = new functionDef();
    fd->name = "print";
    fd->isEmitter = true;
    fd->returnType = languageService.getType("void");
    fd->src = own->src;
    auto* p = new paramDef();
    p->name = "val";
    p->type = languageService.getType(typeName);
    if(p->type.name.empty()) p->type.name = typeName;
    fd->params.push_back(p);
    auto* blk = new i6Block();
    blk->i6Body = "$i6Expr($val.print())";
    fd->body = blk;
    languageService.globals.push_back(fd);
}

bglParser::GlobalCallBinding bglParser::bindGlobalCall(const string& name, vector<expression*>& args,
                                                         vector<string>& namedArgNames,
                                                         vector<vector<interpolatedSegment>>& interpSegmentsPerArg,
                                                         functionDef* func, statementBlock* body){
    GlobalCallBinding out;
    // Mangle the overload set (if this name has one) BEFORE resolving, so the matched overload
    // already carries its i6name — the expression path emits its call text immediately, long
    // before the post-parse safety-net pass runs.
    mangleGlobalOverloadSet(name);
    // Named arguments fill parameters by name, so they can only be type-checked once placed. Place
    // them against each same-named overload in turn and bind the first one they then resolve to.
    bool hasNamed = false;
    for(auto& n : namedArgNames) if(!n.empty()){ hasNamed = true; break; }
    if(hasNamed){
        for(typeDef* g : languageService.globals){
            auto* fd = dynamic_cast<functionDef*>(g);
            if(fd == nullptr || fd->name != name) continue;
            vector<expression*> placed = args;
            vector<string> names = namedArgNames;
            vector<vector<interpolatedSegment>> interps = interpSegmentsPerArg;
            while(names.size() < placed.size()) names.push_back("");
            while(interps.size() < placed.size()) interps.push_back({});
            if(!reorderNamedArgsImpl(placed, names, interps, fd, [](string){ return false; })) continue;
            if(resolveGlobalCall(name, placed, func, body).match != fd) continue;
            args = placed; namedArgNames = names; interpSegmentsPerArg = interps;
            break;
        }
    }
    if(name == "print" && args.size() == 1 && args[0] != nullptr){
        convertStoragelessForPrint(args[0]);
        ensureOwnPrintOverload(args[0]->resolvedType);
    }
    GlobalCallMatch gcm = resolveGlobalCall(name, args, func, body);
    // The library's overload was chosen though the program's own function of the name takes the call too.
    if(gcm.match != nullptr && gcm.match->src.file.find("beguiLib") != string::npos && !looseIdentifierMode)
        for(typeDef* g : languageService.globals)
            if(auto* fd = dynamic_cast<functionDef*>(g); fd && fd != gcm.match && fd->name == name && !fd->isPrePassStub
               && !fd->src.file.empty() && fd->src.file.find("beguiLib") == string::npos){
                size_t req = 0; for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
                if(args.size() < req || args.size() > fd->params.size()) continue;
                // A union or `var` parameter is meant to lose to a more specific overload (§2.8).
                bool fits = true;
                for(size_t i = 0; i < args.size() && fits; i++){
                    const string& pt = fd->params[i]->type.name;
                    classDef* pc = languageService.findClass(pt);
                    if(pt == "var" || isUnionType(pt) || (pc != nullptr && !pc->unionMembers.empty())) fits = false;
                    else if(args[i] && !args[i]->resolvedType.empty() && !isTypeCompatible(args[i]->resolvedType, pt)) fits = false;
                }
                if(fits){
                    parsingWarning(format("this call goes to the library's '{0}', which fits it better than the program's own "
                        "'{0}' (line {1}); give the program's function another name, or pass all its arguments.",
                        gcm.match->dName(), fd->src.line));
                    break;
                }
            }
    // A stringObj where a string parameter is expected: say so, rather than that no overload fits.
    if(gcm.match == nullptr && gcm.arityMatch != nullptr && !looseIdentifierMode)
        for(size_t i = 0; i < args.size() && i < gcm.arityMatch->params.size(); i++)
            if(args[i] != nullptr && args[i]->resolvedType == "stringobj" && gcm.arityMatch->params[i]->type.name == "string")
                parsingError(format("Argument {0} of '{1}' is a stringObj, which isn't a string; give the parameter "
                    "the type stringObj or `string | stringObj`.", i + 1, gcm.arityMatch->dName()));
    // validateGlobalCall returns the return type string; we discard it here (the caller derives
    // return type from the matched method). validateGlobalCall also throws on invalid calls.
    validateGlobalCall(gcm, name, args);
    // A call to the routine being parsed (it may resolve to the routine's pre-scan stub).
    if(gcm.match != nullptr && currentFunc != nullptr && currentClass == nullptr && currentObject == nullptr
       && (gcm.match == currentFunc
           || (gcm.match->name == currentFunc->name && gcm.match->params.size() == currentFunc->params.size())))
        currentFunc->callsItself = true;
    out.funcVarReturnType = gcm.funcVarReturnType;
    // A call through a func<…> variable converts each argument to its parameter type (an accessor's getter).
    if(!gcm.funcVarReturnType.empty()){
        vector<string> ta = funcTypeArgs(resolveIdentifierType(name, func, body));
        for(size_t i = 0; i < args.size() && i + 1 < ta.size(); i++){
            const string& want = ta[i + 1];
            if(args[i] == nullptr || args[i]->resolvedType.empty() || args[i]->resolvedType == want || want == "var") continue;
            // A float doesn't narrow implicitly, as it doesn't for a direct call.
            if(args[i]->resolvedType == "float" && want != "float")
                parsingError(format("Argument {0} of '{1}' is a float; its func<> type takes '{2}' there. Cast it.",
                                    i + 1, name, typeDisplayName(want)));
            string text = args[i]->text();
            string conv = applyCastConversion(text, args[i]->resolvedType, want);
            if(conv != text){ args[i]->tokens = {conv}; args[i]->resolvedType = want; }
        }
    }
    if(gcm.funcVarReturnType.empty() && gcm.match){
        // A local of the routine's name would hide it in I6 (`int count = Count();`): the local is renamed.
        if(currentFunc != nullptr && !gcm.match->isEmitter) currentFunc->globalEscapes.insert(name);
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
    if(auto* od = languageService.findGlobalAs<objectDef>(head)) curObj = od;
    // If not a global, check #using imports: the head might be a member of an imported scope.
    // e.g., after `#using bgl;`, head="glulx" is a member of the bgl object.
    if(!curObj){
        for(objectDef* imp : usingObjectImports){
            for(typeMember* m : imp->members){
                auto* vd = dynamic_cast<variableDeclaration*>(m);
                if(!vd || vd->name != head) continue;
                // Follow the member to find the target object by initializer name — an `alias`
                // member only; see qualifyDottedViaObjectHead.
                string initName = vd->isNamespaceAlias() && vd->declaredExpressionValue
                                ? vd->declaredExpressionValue->text() : "";
                if(!initName.empty())
                    if(auto* od = languageService.findGlobalAs<objectDef>(initName)) curObj = od;
                if(!curObj){
                    // Fall back to type name
                    if(auto* od = languageService.findGlobalAs<objectDef>(vd->type.name)) curObj = od;
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
            // Only an `alias` member redirects to the object its initializer names; an ordinary
            // property with an object-valued default is storage, and its type is its own.
            string initName = vd->isNamespaceAlias() && vd->declaredExpressionValue
                            ? vd->declaredExpressionValue->text() : "";
            curObj = nullptr;
            // Try init name first (e.g., auto glulx = _bglGlulx → initName="_bglglulx")
            if(!initName.empty())
                if(auto* od = languageService.findGlobalAs<objectDef>(initName)) curObj = od;
            // Fall back to type name
            if(!curObj)
                if(auto* od = languageService.findGlobalAs<objectDef>(nextType)) curObj = od;
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
        // A member of the class or object being compiled, or a parameter, outranks an import (§10.4).
        auto ownMember = [&](const string& n){
            auto real = [&](typeMember* m){   // an alias member is a type name, not a member to reach
                auto* vd = dynamic_cast<variableDeclaration*>(m);
                return m->name == n && !(vd && vd->isNamespaceAlias());
            };
            classDef* cls = currentClass != nullptr ? currentClass : currentObject != nullptr ? currentObject->objectClass : nullptr;
            if(currentObject != nullptr)
                for(typeMember* m : currentObject->members) if(real(m)) return true;
            return cls != nullptr && cls->findMember(real) != nullptr;
        };
        if(currentFunc != nullptr && (ownMember(first.value) || qualifyFromParams(first.value, currentFunc)))
            return first;
        for(objectDef* imp : usingObjectImports)
            for(typeMember* m : imp->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->isAlias && vd->name == first.value
                       && (languageService.isClassType(vd->type.name) || languageService.findEnum(vd->type.name) != nullptr)){
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
    // Head is normally an identifier (a namespace object like `bgl`); a direct `EnumType.value` head
    // is a type name, which lexes as dataType — accept both so qualified enum/bnum values resolve.
    if(!first.is(eTokenType::identifier) && !first.is(eTokenType::dataType)) return false;
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
    // Direct `EnumType.value` (2 segments, no namespace prefix): scope the value lookup to the named
    // enum/bnum so a value whose bare name collides with another enum's value still resolves. Without
    // this, `bGlulxWindowScale.fixed` would fall through to the bare-identifier resolver and be reported
    // ambiguous against, e.g., eGlulxStyleType.fixed.
    if(segments.size() == 2){
        auto* et = languageService.findEnum(segments[0]);
        // A #using-imported alias of an enum (`size.large` after `#using kit` with `alias size for eSize`).
        for(objectDef* imp : usingObjectImports){
            if(et != nullptr) break;
            if(string t = resolveNamespacedType(imp->name + "." + segments[0]); !t.empty())
                et = languageService.findEnum(t);
        }
        if(et == nullptr) return false;   // not a direct enum/bnum type — let normal handling proceed
        string valLower = segments[1];
        transform(valLower.begin(), valLower.end(), valLower.begin(), ::tolower);
        int mv = 0; bool m = false;
        for(enumValueDef* ev : et->namedValues)
            if(ev->name == valLower){ m = true; mv = ev->value; break; }
        if(!m)
            parsingError(format("'{0}' is not a named value of enum/bnum '{1}'", segments[1], segments[0]));
        file.getToken();  // consume '.'
        file.getToken();  // consume value
        outFlatEmission = et->isExternal ? valLower : to_string(mv);
        outEnumType = et->name;
        return true;
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
        if(auto* od = languageService.findGlobalAs<objectDef>(first.value)) rootObj = od;
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
            // A property-class member (e.g. parentProp `parent`) means this is a property-read
            // chain (`obj.parent.parent`, `obj.parent.method()`), not a namespace path — defer to
            // the read-emitter / dot-chaining handlers rather than mis-reading it as a namespace.
            if(foundMember && isPropertyClassType(foundMember->type.name)) return false;
            if(!foundMember)
                parsingError(format("'{0}' is not a member of '{1}'", segments[i], prefixSoFar));
            // If this is an alias member, the next segment should be an enum value — stop walking.
            if(foundMember->isAlias){
                auto* en = languageService.findEnum(foundMember->type.name);
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
            // A namespace step is an `alias` member; anything else is a property, and
            // `obj.prop.member` is an ordinary runtime chain that the property handlers own.
            // Following a property's initializer here made this diagnostic claim the path and
            // then reject it, because a class method is not among the target object's own members.
            objectDef* next = nullptr;
            if(foundMember->isNamespaceAlias() && foundMember->declaredExpressionValue)
                next = languageService.findGlobalAs<objectDef>(foundMember->declaredExpressionValue->text());
            if(!next && foundMember->isNamespaceAlias())
                next = languageService.findGlobalAs<objectDef>(foundMember->type.name);
            if(!next) return false;  // not a namespace step — let normal handling proceed
            curObj = next;
            prefixSoFar += "." + segments[i];
        }
        return false;
    }
    auto* ed = languageService.findEnum(enumTypeName);
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
// `selfType` as a member's type is the owner's own class: `r1.next` is a Room2 on a Room2.
std::string bglParser::resolvePathType(std::string path, functionDef* func, statementBlock* body, const string& memberHint) {
    string t = resolvePathTypeRaw(path, func, body, memberHint);
    if(t != "selftype") return t;
    size_t dot = path.rfind('.');
    if(dot == string::npos) return currentSelfTypeName().empty() ? t : currentSelfTypeName();
    string owner = path.substr(0, dot);
    string ot = owner == "self" ? currentSelfTypeName()
              : owner.find('.') == string::npos ? resolveIdentifierType(owner, func, body) : resolvePathType(owner, func, body);
    string oc = objectsClassName(ot);
    return !oc.empty() ? oc : (ot.empty() ? t : ot);
}

std::string bglParser::resolvePathTypeRaw(std::string path, functionDef* func, statementBlock* body, const string& memberHint) {
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
    // via repeated single-hop walks; a single hop would return "" for multi-segment tails.
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
            typeDef* tdp = &languageService.getType(currentType);
            // A verb's name names its action too; its members are the verb object's.
            if(dynamic_cast<objectDef*>(tdp) == nullptr && dynamic_cast<classDef*>(tdp) == nullptr)
                for(verbObjectDef* v : languageService.verbs) if(v->name == currentType){ tdp = v; break; }
            typeDef& td = *tdp;
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

// A value-class result a call returns is the callee's own instance, which the next call into it
// overwrites. Where the result is still needed after another call, copy it into an instance of
// the calling routine's own first; the copy operator's return is discarded (`* 0`).
string bglParser::keepValueResult(const string& text, const string& typeName){
    functionDef* func = currentFunc;
    if(func == nullptr) return text;
    bool isCall = false;   // a routine call somewhere in it: `name(` — but not a checked subscript's address
    for(size_t i = 1; i < text.size() && !isCall; i++){
        if(text[i] != '(' || !(isalnum((unsigned char)text[i - 1]) || text[i - 1] == '_')) continue;
        size_t b = i;
        while(b > 0 && (isalnum((unsigned char)text[b - 1]) || text[b - 1] == '_')) b--;
        string word = text.substr(b, i - b);
        isCall = word != "_bglAt" && word != "_bglMemberAt" && word != "_bglByteAt" && word.rfind("_bglValSlot_", 0) != 0;
    }
    if(!isCall) return text;
    classDef* cls = languageService.findClass(typeName);
    if(!cls || cls->isEmitterClass || !isValueClass(cls) || !classHasStoredFields(cls)) return text;
    functionDef* op = findAssignOperator(cls, typeName, [](functionDef*){ return true; }, /*allowInherited*/true);
    if(op == nullptr || op->isEmitter) return text;
    if(op->i6name.empty()) op->i6name = mangleOperatorName(op->name);
    string context = currentClass != nullptr ? currentClass->name
                   : currentObject != nullptr ? currentObject->name : string();
    // An operator's name is its symbol: it goes by its routine name (`[]` → `_opsubscript`).
    bool isOperator = func->name == "operator()" || (!func->name.empty() && !isalpha((unsigned char)func->name[0]) && func->name[0] != '_');
    string name = "_bglKeep_" + (context.empty() ? "" : context + "_") + (isOperator ? mangleOperatorName(func->name) : func->name);
    int n = 1;
    while(!paramBackingNames.insert(name + "_" + to_string(n)).second) n++;
    name += "_" + to_string(n);
    variableDeclaration* backing = new variableDeclaration();
    backing->name = name;
    backing->displayName = name;
    backing->type = languageService.getType(typeName);
    backing->src = func->src;
    backing->isInstanceBacking = true;
    languageService.registerInstance(*backing);
    func->ownBackings.push_back({name, typeName});
    return format("({0}.{1}({2}) * 0 + {0})", name, op->i6name, text);
}

// A value of another type given where a value class is wanted: an instance of the class, set from the
// value through the class's own `operator =` for that type, held in a temporary of the routine's.
void bglParser::convertIntoValueClass(expression* arg, const string& typeName){
    functionDef* func = currentFunc;
    const string& at = arg->resolvedType;
    if(func == nullptr || at.empty() || at == typeName || at == "nullliteral") return;
    classDef* cls = languageService.findClass(typeName);
    if(cls == nullptr || cls->isEmitterClass || !isValueClass(cls) || !classHasStoredFields(cls)) return;
    if(classDef* ac = languageService.classOf(at); ac != nullptr && (ac == cls || ac->hasAncestor(cls))) return;
    functionDef* op = findAssignOperator(cls, at, [&](functionDef* f){
        return f->params.size() == 1 && f->params[0]->type.name != cls->name && isTypeCompatible(at, f->params[0]->type.name); },
        /*allowInherited*/true);
    if(op == nullptr || op->isEmitter) return;
    if(op->i6name.empty()) op->i6name = mangleOperatorName(op->name);
    mangleOverloadSetForReceiver(cls->name, op->name);
    string name = format("_bglConv_{0}_{1}", func->name, paramBackingNames.size());
    while(!paramBackingNames.insert(name).second) name += "_";
    auto* backing = new variableDeclaration();
    backing->name = name; backing->displayName = name;
    backing->type = languageService.getType(typeName);
    backing->src = func->src;
    backing->isInstanceBacking = true;
    languageService.registerInstance(*backing);
    func->ownBackings.push_back({name, typeName});
    string text = arg->text();
    if(op->params[0]->type.name == "float" && at != "float") text = applyCastConversion(text, at, "float");
    arg->tokens.assign(1, format("({0}.{1}({2}) * 0 + {0})", name, op->i6name, text));
    arg->resolvedType = typeName;
}
