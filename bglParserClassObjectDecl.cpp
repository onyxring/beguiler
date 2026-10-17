#include "platform.h"
// ===============================================================================
// bglParserClassObjectDecl.cpp - class and object declaration processing.
//
// Covers the top-level `class`/`object` declarations plus all the member-parsing
// helpers that walk their bodies.
//
// Top-level declarations:
//   processClassDeclaration         - class Foo { ... }, extern/extend/emitter/alias
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
#include <regex>

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
// hide directives — per-type access control for inherited members
// ===============================================================================
// Parse `hide <name> [ . operator <op> ] [ ( operandTypes ) ] ;` (the current token is 'hide',
// already consumed by the caller). Appends one entry to cls.hiddenMembers. The operator token is
// read with the same conventions as an operator declaration (§5.6.6 / §10.8.6): `=`, `<=>`, `[]`,
// `[]=`, `()`, `?`, and identifier-qualified forms like `prefix++`.
// Keeps the routine a `replace` overrides reachable from its replacement, so tools can still read it.
static void keepReplaced(functionDef& replacement, typeMember* old) {
    if(auto* o = dynamic_cast<functionDef*>(old); o && !o->isPrePassStub && !replacement.replacedFunc) replacement.replacedFunc = o;
}

void bglParser::parseHideDirective(classDef& cls){
    hiddenMember hm;
    token nameTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
    hm.memberName = nameTok.value;
    hm.src = nameTok.src.line > 0 ? nameTok.src : file.currentLocation();
    token nxt = file.getToken();
    // optional `. operator <op>` — scope the hide to one operator on the member's type
    if(nxt.is(token::period)){
        token opKw = file.getToken();
        if(!opKw.is("operator"))
            parsingError(format("hide {0}: expected 'operator' after '.', got '{1}'", hm.memberName, (string)opKw));
        token opTok = file.getToken();
        if(opTok.is(token::parenOpen)){ file.getToken(token::parenClose); hm.operatorName = "()"; }
        else if(opTok.is(token::bracketOpen)){
            file.getToken(token::bracketClose);
            if(file.peekToken().is(token::assignment)){ file.getToken(); hm.operatorName = "[]="; }
            else hm.operatorName = "[]";
        }
        else if(opTok.is("?")) hm.operatorName = "?";
        else if(opTok.is(eTokenType::identifier)){ token opSym = file.getToken(eTokenType::oper); hm.operatorName = opTok.value + opSym.value; }
        else hm.operatorName = opTok.value;   // =, ==, <=>, +, -, …
        nxt = file.getToken();
    }
    // optional `( operandTypes )` — narrow to one overload
    if(nxt.is(token::parenOpen)){
        hm.hasSignature = true;
        token t = file.getToken();
        while(!t.is(token::parenClose)){
            if(t.is(token::comma)){ t = file.getToken(); continue; }
            hm.operandTypes.push_back(t.value);
            t = file.getToken();
        }
        nxt = file.getToken();
    }
    if(!nxt.is(token::endStatement))
        parsingError(format("hide {0}: expected ';' to end the hide directive", hm.memberName));
    // The pre-scan records it first; the declaration's own pass finds it there.
    for(hiddenMember& h : cls.hiddenMembers)
        if(h.memberName == hm.memberName && h.operatorName == hm.operatorName && h.operandTypes == hm.operandTypes
           && h.hasSignature == hm.hasSignature){ h.src = hm.src; return; }
    cls.hiddenMembers.push_back(hm);
}

// Report an error if accessing `memberName` (under operator `queriedOp` — "" for read/method-call,
// "=" etc. for a write) with the given operand types is hidden on `receiverCls` or an ancestor. The
// diagnostic points at the cast door (a base surface that doesn't hide it). No-op when not hidden.
void bglParser::enforceHidden(classDef* receiverCls, const string& memberName,
                              const string& queriedOp, const vector<string>& operandTypes,
                              const string& receiverText){
    if(receiverCls == nullptr) return;
    const hiddenMember* hit = findHiddenMember(receiverCls, memberName, queriedOp, operandTypes);
    if(hit == nullptr) return;
    string doorType = receiverCls->baseClasses.empty() ? string("BaseType")
                                                       : receiverCls->baseClasses[0]->dName();
    string what = hit->operatorName.empty()
                ? format("Member '{0}' is hidden", memberName)
                : format("'{0}.operator {1}' is hidden", memberName, hit->operatorName);
    parsingError(format("{0} on type '{1}'. Cast the receiver to the base surface — e.g. (({2}){3}).{4} — to reach it.",
                        what, receiverCls->dName(), doorType,
                        receiverText.empty() ? string("x") : receiverText, memberName));
}

// ===============================================================================
// Top-level: processClassDeclaration
// ===============================================================================
// Parse the optional `<T, …>` type-parameter clause of a class declaration, recording each
// parameter on `newClass`; a no-op when the next token is not '<'.
void bglParser::parseClassTypeParameters(classDef& newClass, token nameTok, bool isExtend, bool isAlias){
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
}

// Parse the optional pool-size clause (`[N]` or the extern marker `[]`). Always consumes the
// token after the class name/type parameters; leaves `tok` on the token that follows the clause.
void bglParser::parseClassPoolSize(classDef& newClass, token& tok, token nameTok, bool isExternal, bool isExtend, bool isEmitterClass, bool isAlias){
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
                parsingError(format(kindWord(&newClass) + " '{0}': empty pool brackets '[]' are only valid on `extern class` (marker for an I6-defined pooled type)", (string)nameTok));
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
                    parsingError(format(kindWord(&newClass) + " '{0}': pool size must be a positive integer (got {1})", (string)nameTok, n));
                newClass.poolSize = n;
            } else if(auto d = definedSymbols.find(inner.value); d != definedSymbols.end()){
                // A #define is its value here: Inform 6 never sees the symbol.
                string v = d->second;
                if(v.empty() || v.find_first_not_of("0123456789") != string::npos || stoi(v) <= 0)
                    parsingError(format(kindWord(&newClass) + " '{0}': pool size '{1}' must be a positive integer", (string)nameTok, inner.value));
                newClass.poolSize = stoi(v);
                newClass.poolSizeExpr.clear();
            } else {
                newClass.poolSize    = 1;  // positive sentinel — "is pooled" downstream checks
                newClass.poolSizeExpr = inner.value;
            }
            file.getToken(token::bracketClose);
        } else {
            parsingError(format(kindWord(&newClass) + " '{0}': expected integer, identifier, or ']' after '[' in pool clause, got '{1}'", (string)nameTok, (string)inner));
        }
        tok = file.getToken();
    }
}

// Parse the inheritance clause (`for Parent` on an alias class, `: Base, …` otherwise), rejecting
// cycles, and leave `tok` on the token that follows it.
void bglParser::parseClassInheritance(classDef& newClass, token& tok, token nameTok, bool isAlias){
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
            parsingError(format(kindWord(&newClass) + " '{0}': circular inheritance — '{1}' transitively inherits from '{0}'",
                                newClass.dName(), parentDisplay));
    };
    // The pre-scan may already have linked these bases (drainDeferredClassBases); add each once.
    auto addBase = [&](classDef* parent){
        if(find(newClass.baseClasses.begin(), newClass.baseClasses.end(), parent) == newClass.baseClasses.end())
            newClass.baseClasses.push_back(parent);
    };
    if(tok.is("for")){
        // alias class single-parent clause
        token parentTok = file.getToken({eTokenType::dataType, eTokenType::identifier});
        if(parentTok.is(eTokenType::identifier)) parentTok = consumeTypeToken(parentTok);   // `for ns.item` (an alias)
        classDef* parent = languageService.findClass(parentTok.value);
        if(!parent) unknownBaseError(parentTok);
        else { checkInheritanceCycle(parent, parentTok.originalValue); addBase(parent); }
        tok = file.getToken();
    } else if(tok.is(":")){
        do {
            token parentTok = file.getToken({eTokenType::dataType, eTokenType::identifier});
            if(parentTok.is(eTokenType::identifier)) parentTok = consumeTypeToken(parentTok);   // `: base`, `: ns.base` (an alias)
            classDef* parent = languageService.findClass(parentTok.value);
            if(!parent) unknownBaseError(parentTok);
            else { checkInheritanceCycle(parent, parentTok.originalValue); addBase(parent); }
            if(parent && parent->isPrimitive && !newClass.isPrimitive && !newClass.isValue && !newClass.isEmitterClass
               && !newClass.isAlias && !newClass.isExternal)
                parsingError(format(kindWord(&newClass) + " '{0}' can't derive from '{1}': a class's variable holds a reference to an "
                                    "object, and '{1}' is a primitive value. For a new kind of '{1}', write "
                                    "`primitive class {0} : {1}`.", newClass.dName(), parent->dName()));
            tok = file.getToken();
        } while(tok.is(","));
    }
    if(newClass.baseClasses.empty())
        if(string root = implicitRootName(&newClass); !root.empty())
            if(classDef* rc = languageService.findClass(root)) addBase(rc);
}

// Decode an `operator …` member name into `name` (the already-consumed `operator` token) and
// leave `tok` on the symbol that follows it — '(' for a method, ';' for the bodiless forms.
void bglParser::parseOperatorMemberName(token& tok, token& name){
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
        // Validate the operator symbol against the overloadable set (spec §5.6.6).
        // This also rejects valid operator tokens that are NOT overloadable
        // (e.g. `?.`, `??`, `=>`), which asserting `oper` alone would let through.
        static const string overloadableOps =
            " = + - * / % == != =~ < > <= >= ?= && || & | ^ << >> "
            "+= -= *= /= %= &= |= ^= <<= >>= ++ -- ! <=> ";
        if(overloadableOps.find(" " + opTok.value + " ") == string::npos)
            parsingError(format("'{0}' is not an overloadable operator. Overloadable operators are:"
                "{1}and the special forms operator(), operator[], operator[]=, operator switch, "
                "operator auto, and the ? query operator.", opTok.value, overloadableOps));
        name = opTok;
        tok=file.getToken(eTokenType::symbol);
    }
}

// Parse one method member of a class body; `tok` is the '(' opening its parameter list.
// Returns true when the member is fully handled and the member loop should move to the next.
bool bglParser::parseClassMethodMember(classDef& newClass, token& tok, token name, token returnType, Qualifiers& q, bool isEmitter, bool isExternal, bool isExtend, const string& i6alias){
    bool isReplace = q.isReplace;
    bool isExplicitConversion = q.isExplicit;
    bool isMemberStatic = q.isStatic;
    bool isMemberSuperposed = q.isSuperposed;
    if(!isEmitter && !isMemberStatic) rejectMethodOnWordProperty(name, newClass.dName(), i6alias);
    functionDef& funcDef=*(new functionDef());
    funcDef.name=(string) name; funcDef.displayName=name.originalValue;
    funcDef.src = name.src.line > 0 ? name.src : file.currentLocation();
    funcDef.returnType=languageService.getType((string) returnType);
    funcDef.isEmitter=isEmitter;
    funcDef.isExplicit=isExplicitConversion;
    funcDef.isDefault=q.isDefault;
    funcDef.isStatic=isMemberStatic;   // known before the parameters are read
    funcDef.isRequired = q.isRequired;
    if(!returnType.docComment.empty())   funcDef.docComment = returnType.docComment;
    else if(!name.docComment.empty())    funcDef.docComment = name.docComment;
    // Non-emitter operator methods (name starts with a non-identifier char, e.g. `=`,
    // `==`) need a mangled i6name so the emitted I6 property has a valid identifier.
    // The free-function path does this at declaration (bglParserDecls.cpp); mirror it
    // here so class operators are mangled at parse time too. This upholds the invariant
    // documented at mangleOverloadSet() (bglParser.cpp) — that operator overloads carry
    // an i6name by parse time — instead of relying solely on lazy call-site mangling,
    // which leaves a declared-but-never-called operator emitting a bare `=` as its I6
    // property name (invalid I6).
    if(!isEmitter && !funcDef.name.empty()
       && (funcDef.name == "operator()"
           || (!isalpha((unsigned char)funcDef.name[0]) && funcDef.name[0] != '_')
           || funcDef.name.rfind("prefix", 0) == 0 && funcDef.name.size() > 6 && !isalnum((unsigned char)funcDef.name[6])))
        funcDef.i6name = mangleOperatorName(funcDef.name);
    // A class may convert to several types (`int operator ()`, `explicit string operator ()`): each
    // is its own routine, named for its result type, so one doesn't overwrite the other.
    if(!isEmitter && funcDef.name == "operator()"){
        string rt = funcDef.returnType.name;
        for(char& ch : rt) if(!isalnum((unsigned char)ch)) ch = '_';
        funcDef.i6name = "_opconv_" + rt;
    }
    if(isExplicitConversion && funcDef.name != "operator()")
        parsingError("'explicit' is only valid on conversion operators (operator())");
    processParameterList(funcDef);
    // A binary operator declared with no operand would be unary, which Beguile has none of.
    static const set<string> binaryOps = {"+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>", "==", "!=", "<", ">",
                                          "<=", ">=", "<=>", "?=", "=~", "+=", "-=", "*=", "/=", "%=", "="};
    if(binaryOps.count(funcDef.name) && funcDef.params.empty() && !isMemberStatic)
        parsingError(format("'operator {0}' needs an operand: Beguile has no unary 'operator {0}'. Write a method for it instead.",
                            funcDef.name));
    consumeMethodI6Alias(funcDef);      // `Type method(...) as <i6name> { … }` (§3.11)
    // Synthesize the instance and copy-in for each param whose class copies.
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
                    parsingError(format(kindWord(&newClass) + " '{0}' already has an operator auto()", newClass.dName()));
        // Must be followed by ; (no body)
        token afterAuto = file.getToken();
        if(!afterAuto.is(token::endStatement))
            parsingError("operator auto() cannot have a body — declare return type only");
        newClass.members.push_back(&funcDef);
        tok = file.getToken();
        return true;
    }
    if(isEmitter && funcDef.name == "deinit" && !funcDef.params.empty())
        parsingError("Emitter 'deinit' cannot accept parameters");
    if(isEmitter && funcDef.name == "init" && !funcDef.params.empty()
       && (funcDef.params.size() != 1 || funcDef.params[0]->type.name != newClass.name))
        parsingError(format("Emitter 'init' takes no parameters, or one '{0}' to copy from.", newClass.dName()));
    if(isMemberStatic && q.isEmitter)
        parsingError(format("'{0}': 'static' and 'emitter' cannot be combined — an emitter inlines at the call site and has no routine to make static", funcDef.name));
    if(!isEmitter && (funcDef.name == "switch" || funcDef.name == "?"))
        parsingError(format("operator {0}() must be declared as an emitter", funcDef.name));
    // init/deinit are receiver lifecycle hooks and must inline — except a `static`
    // deinit, which is the value form: it takes the element as a parameter and emits
    // as a free routine, so generic code (array<T>) can hold its address and destroy
    // a slot it has no receiver for. Same instance/static split as `operator ==`.
    if(!isEmitter && !isMemberStatic && (funcDef.name == "init" || funcDef.name == "deinit"))
        parsingError(format("'{0}' must be declared as an emitter", funcDef.name));
    if(isMemberStatic && funcDef.name == "init")
        parsingError("'init' cannot be 'static' — construction needs the receiver it is initialising");
    if(isMemberStatic && funcDef.name == "deinit" && funcDef.params.size() != 1)
        parsingError("a 'static deinit' takes exactly one parameter: the value to destroy");
    funcDef.isStatic = isMemberStatic;
    // Carry `superposed` onto the method. Only a STATIC method (emitted as a free routine
    // via emitStaticClassRoutines→emitFunction) can honor it — the withhold/revive path
    // lives in emitFunction. An instance method emits as a property routine on the class
    // object and cannot be withheld/revived, so `superposed` there is inert. It's harmless
    // (a no-op), so warn rather than error: the method still works, it just won't drop.
    if(isMemberSuperposed && !isMemberStatic)
        parsingWarning(format("'superposed' on method '{0}' has no effect without 'static'. "
                              "An instance method emits as a property routine, which can't be "
                              "withheld and revived on demand; write 'static superposed' (a "
                              "free routine) for pay-only-if-used, or drop 'superposed'.", funcDef.name));
    funcDef.isSuperposed = isMemberSuperposed;
    // An operator the class overloads (pre-scan registered every overload) takes its per-signature
    // name now, the one the overload pass settles on, so call sites parsed from here on use it.
    if(!isEmitter && funcDef.name != "operator()" && !funcDef.name.empty()
       && !isalpha((unsigned char)funcDef.name[0]) && funcDef.name[0] != '_'){
        // Another signature makes a set; this declaration's own pre-scan stub has the same one.
        // A stub records parameter types as written, which may be a type alias (`code` for la11Code).
        function<string(const string&)> stubType = [&](const string& t) -> string {
            if(size_t lt = t.find('<'); lt != string::npos && t.back() == '>')   // `array<piece>`
                return t.substr(0, lt + 1) + stubType(t.substr(lt + 1, t.size() - lt - 2)) + ">";
            for(objectDef* imp : usingObjectImports)
                for(typeMember* m : imp->members)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd && vd->isAlias && vd->name == t) return vd->type.name;
            if(t.find('.') != string::npos)
                if(string r = resolveNamespacedType(t); !r.empty()) return r;
            return t;
        };
        auto sameSignature = [&](functionDef* fd){
            if(fd->params.size() != funcDef.params.size()) return false;
            for(size_t i = 0; i < fd->params.size(); i++){
                string t = fd->isPrePassStub ? stubType(fd->params[i]->type.name) : fd->params[i]->type.name;
                if(t != funcDef.params[i]->type.name) return false;
            }
            return true;
        };
        int overloads = 1;
        for(typeMember* m : newClass.members)
            if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd != &funcDef && fd->name == funcDef.name
               && !fd->isEmitter && !sameSignature(fd))
                overloads++;
        if(overloads > 1){
            auto overloadName = [](functionDef* fd){
                string disc;
                for(paramDef* p : fd->params){
                    string t = p->type.name;
                    if(size_t lt = t.find('<'); lt != string::npos) t = t.substr(0, lt);
                    disc += "_" + (t.empty() ? string("var") : t);
                }
                return mangleOperatorName(fd->name) + disc;
            };
            funcDef.i6name = overloadName(&funcDef);
            // An overload declared earlier didn't know it had company until now.
            for(typeMember* m : newClass.members)
                if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd != &funcDef && fd->name == funcDef.name
                   && !fd->isEmitter && !fd->isPrePassStub)
                    fd->i6name = overloadName(fd);
        }
    }
    if(funcDef.isRequired && !funcDef.isEmitter && file.peekToken().is(token::endStatement)){
        // `required bool handler();` — each object supplies the body; the class's is empty.
        file.getToken();
        funcDef.body = new statementBlock();
    } else if((isExternal || newClass.isExternal || newClass.isAlias) && !isEmitter && !funcDef.isStatic){
        // extern/alias class non-emitter INSTANCE methods not allowed: they would need to
        // emit a property routine on a class Beguile does not own. A `static` method has no
        // receiver, so it emits as a free routine and carries no such requirement — which is
        // how a bare-word type (string, float) publishes a callable comparison.
        parsingError(format("Non-emitter function '{0}' is not allowed in an extern or alias class "
                            "(a 'static' method is allowed — it needs no instance)", funcDef.name));
    } else if(funcDef.isEmitter && file.peekToken().is(token::endStatement)){
        // Semicolon-terminated emitter: pass-through (value unchanged)
        file.getToken(); // consume ';'
        i6Block& rawblock=*(new i6Block());
        rawblock.i6Body=" $self";
        funcDef.body=&rawblock;
    } else {
        file.getToken(token::braceOpen); //consume the open brace;
        if(funcDef.isRequired && !funcDef.isDefault)
            parsingWarning(format("'{0}' is required but has a body here, so nothing needs to supply it.", funcDef.dName()));
        if(funcDef.isEmitter){
            i6Block& rawblock=*(new i6Block());
            rawblock.i6Body = file.getRawTextThroughClosingBrace(/*isI6Content=*/true);
            funcDef.body=&rawblock;
            // A value emitter's body is the I6 expression spliced in at the call; `return …;` is a
            // statement there. Members of an emitter class are emitters even without the keyword.
            static const regex leadingReturn(R"(^\s*return\b)", regex::icase);
            if(funcDef.returnType.name != "void" && regex_search(rawblock.i6Body, leadingReturn))
                parsingError(format("'{0}' is an emitter{1}: its body is the Inform 6 expression spliced in where "
                                    "it is called, so it can't begin with 'return' — write the value alone, "
                                    "as in {{ $self-->1 }}", funcDef.name,
                                    q.isEmitter ? "" : " (every member of an emitter class is)"));
        } else {
            funcDef.body = new statementBlock();
            functionDef* savedFunc = currentFunc;
            currentFunc = &funcDef;
            openCompileContext(eCompileContext::codeBlock, dynamic_cast<statementBlock*>(funcDef.body));
            while(processNextStatement(funcDef) == false){}
            closeCompileContext(eCompileContext::codeBlock);
            currentFunc = savedFunc;
            if(funcDef.returnType.name != "void" && !allPathsReturn(dynamic_cast<statementBlock*>(funcDef.body)))
                parsingError(missingReturnMessage(funcDef));
            saveBackingsAcrossRecursion(funcDef);
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
            // A stub that recorded its parameters is matched by signature like any other member.
            if(fd->isPrePassStub && fd->params.empty()){ existing = m; break; }
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
                if(*it==existing){ keepReplaced(funcDef, existing); *it = &funcDef; break; }
            alreadyReplaced = true;
        }
        if(!funcDef.isEmitter) rejectMethodBesideField(newClass, funcDef.name, funcDef.dName(), true);
        if(alreadyReplaced){ tok = file.getToken(); return true; }
    }
    if(funcDef.isEmitter && funcDef.name == "_bglglobaldeclaration"){
        // declaration emitter — store raw body on the class, not as a member
        i6Block* body = dynamic_cast<i6Block*>(funcDef.body);
        if(body) newClass.globalDeclarationBody = body->i6Body;
    } else {
        if(!isExtend){
            // A method on an additive property contributes to it alongside every ancestor's; there is no
            // replacing one, so `replace` is an error and no shadowing warning applies.
            bool additive = languageService.isAdditiveProperty(funcDef.i6name.empty() ? funcDef.name : funcDef.i6name);
            if(additive && isReplace)
                parsingError(format("'{0}' is additive: every class's and object's contribution is kept, so it can't be replaced.",
                                    funcDef.dName()));
            if(isReplace && !additive && !funcDef.name.empty() && isalpha((unsigned char)funcDef.name[0])){
                bool found = false;
                function<void(classDef*)> anyBase = [&](classDef* c){
                    for(typeMember* m : c->members) if(m->name == funcDef.name) found = true;
                    for(classDef* base : c->baseClasses) if(!found) anyBase(base);
                };
                for(classDef* base : newClass.baseClasses) anyBase(base);
                for(typeMember* m : newClass.members)
                    if(auto* fd = dynamic_cast<functionDef*>(m); m->name == funcDef.name && !(fd && fd->isPrePassStub)) found = true;
                if(auto* g = languageService.findGlobalAs<variableDeclaration>(funcDef.name); g && g->type.name == "property")
                    found = true;   // a library property the class gives its own routine
                if(!found)
                    parsingWarning(format(kindWord(&newClass) + " '{0}': 'replace' specified but no base class has a member '{1}'; "
                                          "treating as new definition", newClass.dName(), funcDef.dName()));
            }
            // Check base class hierarchy for shadowed methods — warn if 'replace' not specified.
            // A "shadow" requires matching signature (arity + param types). Same name but
            // different arity or param types is an overload, not a shadow — no warning.
            if(!isReplace && !additive && !funcDef.name.empty()){
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
                                shadowedFrom = c->dName(); shadowedIsDefault = fd->isDefault || fd->isRequired; return;
                            }
                    for(classDef* base : c->baseClasses) searchBases(base);
                };
                for(classDef* base : newClass.baseClasses) searchBases(base);
                if(!shadowedFrom.empty() && !shadowedIsDefault)
                    parsingWarning(format(kindWord(&newClass) + " '{0}': method '{1}' shadows definition in base class '{2}'; use 'replace' to suppress this warning",
                        newClass.dName(),
                        funcDef.name == "operator()" ? string("operator ()")
                            : !funcDef.name.empty() && !isalpha((unsigned char)funcDef.name[0]) && funcDef.name[0] != '_'
                                ? "operator " + funcDef.name : funcDef.dName(),
                        shadowedFrom));
            }
            rejectValueWordName(funcDef.name);
            if(!funcDef.isEmitter) rejectMethodBesideField(newClass, funcDef.name, funcDef.dName(), true);
            // A later `extend … replace emitter` already holds this slot; its body is the one used.
            // Only the overload it replaces: same parameter types.
            auto sameSignature = [&](functionDef* fd){
                if(fd->params.size() != funcDef.params.size()) return false;
                for(size_t i = 0; i < fd->params.size(); i++)
                    if(fd->params[i]->type.name != funcDef.params[i]->type.name) return false;
                return true;
            };
            for(typeMember* m : newClass.members)
                if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->isForwardReplace && fd->name == funcDef.name
                   && (fd->params.empty() || sameSignature(fd))){
                    tok = file.getToken(); return true;
                }
            // Silently replace pre-scan stubs (may have been seeded by an 'extend class' that
            // appears later in the file — pre-scan pushes stubs into this class's member list).
            if(replaceStubMember(newClass.members, funcDef)) { tok = file.getToken(); return true; }
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
                            keepReplaced(funcDef, newClass.members[i]);
                            newClass.members[i] = &funcDef;
                            replacedExisting = true;
                            break;
                        }
                        parsingError(format(kindWord(&newClass) + " '{0}': method '{1}' with the same signature is already defined (originally at {2}:{3}); use 'replace' to override",
                            newClass.dName(), funcDef.dName(), fd->src.file, fd->src.line));
                    }
                }
            }
            if(replacedExisting) { tok = file.getToken(); return true; }
        }
        newClass.members.push_back(&funcDef);
    }
    return false;
}

// Parse one variable (or emitter-class alias) member of a class body; `tok` is the '=' or ';'
// following the member name. Returns true when the member is fully handled.
// The element type of a list-typed member: T of array<T>/rawArray<T>, or the parameter of a list
// class's single-parameter method (attributeList's element is an attribute). Empty if not a list.
string bglParser::listElementType(const string& typeName){
    if((typeName.rfind("array<",0)==0 || typeName.rfind("rawarray<",0)==0) && typeName.back()=='>'){
        size_t lt = typeName.find('<');
        return typeName.substr(lt+1, typeName.size()-lt-2);
    }
    if(classDef* listClass = languageService.findClass(typeName))
        for(typeMember* m : listClass->members)
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->params.size() == 1) return fd->params[0]->type.name;
    return "";
}

// One-element shorthand for a member value: a single element on a list-typed member is the
// one-element list, `attributes = light` ≡ `attributes = {light}`, `describe = "s"` ≡ `describe = {"s"}`,
// as `array<int> x = 3` ≡ `= {3}` for a declaration. A value of the member's own type (a string on a
// string member, an array or var pointer on an array) stays a plain value. Returns the list, or null.
initializerList* bglParser::asSingleElementList(expression* expr, const string& typeName){
    if(expr == nullptr || expr->resolvedType.empty()) return nullptr;
    string elem = listElementType(typeName);
    if(elem.empty()) return nullptr;
    bool isArrayType = typeName.rfind("array<",0)==0 || typeName.rfind("rawarray<",0)==0;
    bool isString = expr->resolvedType == "stringliteral" || expr->resolvedType == "string";
    bool isElement = (isArrayType && isString && (elem == "var" || elem == "string" || elem == "stringliteral"))
        || (!isTypeCompatible(expr->resolvedType, typeName) && isArrayElementCompatible(expr->resolvedType, elem));
    if(!isElement) return nullptr;
    initializerList* list = new initializerList();
    list->elements.push_back(expr);
    checkByteElementRange(expr, elem);
    return list;
}

// A member value that reassigns an inherited array (`name = {.lamp}`, `nums = {5, 6}`) declares that
// array again for this class or object: same element type, raw/tracked layout and I6 name, new
// contents. Returned as an array declaration so it is emitted and typed like the one it overrides
// (a tracked array keeps its length word; reads know T). Null when `value` isn't such an override.
variableDeclaration* bglParser::asInheritedArrayOverride(variableDeclaration& value, const vector<classDef*>& bases){
    auto* list = dynamic_cast<initializerList*>(value.declaredExpressionValue);
    expression* text = value.declaredExpressionValue != nullptr && value.declaredExpressionValue->resolvedType == "stringliteral"
                     ? value.declaredExpressionValue : nullptr;
    if(list == nullptr && text == nullptr) return nullptr;
    arrayDeclaration* base = nullptr;
    for(classDef* b : bases){
        typeMember* m = findMemberInHierarchy(b, [&](typeMember* mm){
            return mm->name == value.name && dynamic_cast<arrayDeclaration*>(mm) != nullptr;
        });
        if(m){ base = dynamic_cast<arrayDeclaration*>(m); break; }
    }
    if(base == nullptr) return nullptr;
    if(base->isByteArray){
        // The object's own buffer, the class's capacity, holding the value given (a string's characters
        // or a brace list), padded with zero bytes.
        if(list == nullptr){
            list = new initializerList();
            string t = text->text();
            if(t.size() >= 2 && t.front() == '"') t = t.substr(1, t.size() - 2);
            if(t.find_first_of("@^~") != string::npos)
                parsingError(format("'{0}': give an array<char> member a string of plain characters, or its codes in braces.", value.dName()));
            for(char c : t){
                expression* e = new expression(); e->tokens.push_back(to_string((unsigned char)c)); e->resolvedType = "char";
                list->elements.push_back(e);
            }
        }
        if(base->arraySize > 0 && (int)list->elements.size() > base->arraySize)
            parsingError(format("'{0}' holds {1} characters; its class gives it room for {2}.", value.dName(),
                                list->elements.size(), base->arraySize));
        while((int)list->elements.size() < base->arraySize){
            expression* z = new expression(); z->tokens.push_back("0"); z->resolvedType = "char";
            list->elements.push_back(z);
        }
        arrayDeclaration& arr = *(new arrayDeclaration());
        arr.name = value.name;
        arr.displayName = value.displayName.empty() ? base->displayName : value.displayName;
        arr.i6name = value.i6name.empty() ? base->i6name : value.i6name;
        arr.src = value.src;
        arr.type = base->type;
        arr.elementType = "char";
        arr.isByteArray = true;
        arr.arraySize = max(base->arraySize, (int)list->elements.size());
        arr.declaredExpressionValue = list;
        return &arr;
    }
    if(list == nullptr) return nullptr;
    arrayDeclaration& arr = *(new arrayDeclaration());
    arr.name = value.name;
    arr.displayName = value.displayName.empty() ? base->displayName : value.displayName;
    arr.i6name = value.i6name.empty() ? base->i6name : value.i6name;
    arr.src = value.src;
    arr.docComment = value.docComment;
    arr.type = base->type;
    arr.elementType = base->elementType;
    arr.isRaw = base->isRaw;
    arr.isRefLocal = base->isRefLocal && !base->isPromoted;   // a promoted base is promoted again below
    arr.declaredExpressionValue = list;
    // An additive property (`name`) adds to the class's words rather than replacing them, so it
    // carries only its own; padding it out to the class's count would add `0` words.
    arr.arraySize = languageService.isAdditiveProperty(arr.i6name.empty() ? arr.name : arr.i6name)
                  ? (int)list->elements.size() : max(base->arraySize, (int)list->elements.size());
    promoteMemberArray(arr);
    return &arr;
}

bool bglParser::parseClassVariableMember(classDef& newClass, token& tok, token name, token returnType, Qualifiers& q, bool isEmitter, bool isExtend, const string& i6alias, bool extendIsExtern){
    rejectMemberBindingMismatch(q.isRef, tok, returnType, name);
    if(tok.is(token::bindAssignment)) tok.value = token::assignment;   // a `ref` member's value, bound
    bool isReplace = q.isReplace;
    bool isMemberConst = q.isConst;
    bool isMemberStatic = q.isStatic;
    // `int runs asI6 pumpRuns;` beside a global `pumpRuns`: Inform 6 keeps properties and globals in one namespace.
    if(!i6alias.empty() && !isMemberStatic){
        string low = i6alias; transform(low.begin(), low.end(), low.begin(), ::tolower);
        for(typeDef* g : languageService.globals){
            string gi6 = g->i6name; transform(gi6.begin(), gi6.end(), gi6.begin(), ::tolower);
            if(g->name != low && gi6 != low) continue;
            if(auto* vd = dynamic_cast<variableDeclaration*>(g); vd && vd->type.name == "property") continue;
            if(auto* fd = dynamic_cast<functionDef*>(g); fd && fd->isEmitter) continue;
            parsingError(format("'{0}.{1}' is emitted as '{2}', which is also the name of '{3}'; Inform 6 keeps one "
                "namespace for both, so give the member another `asI6` name.", newClass.dName(),
                name.originalValue.empty() ? (string)name : name.originalValue, i6alias, g->dName()));
        }
    }
    // A single value under an additive property's name is reported at the member, before any use of it
    // reports something less clear (the whole-program check would reach it only after the class).
    {
        string prop = i6alias.empty() ? (string)name : i6alias;
        string t = (string)returnType;
        bool text = t == "string" || t == "stringorroutine" || t == "auto" || t.empty();
        if(!isEmitter && !newClass.isEmitterClass && !isMemberStatic && !text && file.peekToken().isNot(token::bracketOpen)
           && t.find('<') == string::npos && name.src.file.find("beguiLib") == string::npos
           && languageService.isAdditiveProperty(prop))
            parsingError(format("'{0}.{1}' is declared '{2}', but '{3}' is an ADDITIVE "
                "property: it holds a list of every class's and object's contributions, so the member is "
                "a `rawArray<T>`, a routine or a string. A single value is a one-element list "
                "(`rawArray<int> {1} = 3;`).",
                newClass.dName(), name.originalValue.empty() ? (string)name : name.originalValue, typeDisplayName(t), prop));
    }
    // Emitter class: allow alias members (typed reference to another class) but not variable declarations
    if(isEmitter || newClass.isEmitterClass){
        string aliasTypeName = (string)returnType;
        // auto inference: require = classReference to infer type
        if(aliasTypeName == "auto"){
            if(!tok.is(token::assignment))
                parsingError(format("'auto' alias member '{0}' requires an initializer to infer the type", (string)name));
            token rhs = file.getToken({eTokenType::identifier, eTokenType::dataType});
            classDef* rhsCls = languageService.findClass(rhs.value);
            if(!rhsCls)
                parsingError(format("'auto' alias member '{0}': '{1}' is not a declared class",
                    (string)name, rhs.originalValue.empty() ? rhs.value : rhs.originalValue));
            aliasTypeName = rhs.value;
            tok = file.getToken();  // consume ;
        }
        classDef* aliasCls = languageService.findClass(aliasTypeName);
        if(!aliasCls)
            parsingError(format("Emitter class '{0}' only supports emitter functions, emitter values, and class alias members; '{1}' is not a class",
                newClass.dName(), aliasTypeName));
        // Alias member: register as a variableDeclaration with the class type
        // No I6 backing — resolved at compile time for dot-access
        variableDeclaration& aliasDef = *(new variableDeclaration());
        aliasDef.name = (string)name;
        aliasDef.displayName = name.originalValue;
        aliasDef.src = name.src.line > 0 ? name.src : file.currentLocation();
        aliasDef.type = languageService.getType(aliasTypeName);
        aliasDef.isExternal = true;  // no I6 emission
        aliasDef.isConst = isMemberConst;
        aliasDef.isInline = q.isInline;
        if(q.isInline){ notePositional(newClass, aliasDef.name); addInlineOrder(newClass, aliasDef.name); }
        if(tok.is(token::assignment) && aliasTypeName != "auto")
            parsingError(format("Alias member '{0}' on emitter class cannot have an initializer; use 'auto' to infer type", (string)name));
        // Consume ; if present
        if(tok.isNot(token::endStatement))
            parsingError(format("Expected ';' after alias member '{0}'", (string)name));
        // Replace the pre-scan's stub of this member, as a class's other members do, so lookups find
        // the declaration (and its `const`) rather than the stub.
        auto stub = find_if(newClass.members.begin(), newClass.members.end(), [&](typeMember* m){
            return m->isPrePassStub && m->name == aliasDef.name && dynamic_cast<variableDeclaration*>(m); });
        if(stub != newClass.members.end()) *stub = &aliasDef;
        else newClass.members.push_back(&aliasDef);
        tok = file.getToken();
        return true;
    }
    if(tok.isNot(token::endStatement) && tok.isNot(token::assignment))
        parsingError(format("Expected '=' or ';' after member '{0}'", (string)name));
    // An object's `parent` is its place in the world tree, emitted as its I6 declaration's position.
    // An I6 class can't place its instances, so a class-level value would be emitted as a plain
    // property that places nothing.
    if(name.value == "parent" && tok.is(token::assignment))
        parsingError(format(kindWord(&newClass) + " '{0}': a class can't set 'parent'; set it on each object", newClass.dName()));
    if(name.value == "instancename" && tok.is(token::assignment))
        parsingError(format(kindWord(&newClass) + " '{0}': a class can't set 'instanceName'; set it on each object", newClass.dName()));
    // Note: alias-class members may carry a default value (e.g. `int priority = 10;` on
    // `class verb`). The value is a compile-time default consulted by the emitter when
    // lifting the field from an instance body; it is not emitted as an I6 property.
    variableDeclaration& varDef=*(new variableDeclaration());
    varDef.name=(string) name;
    varDef.displayName = name.originalValue;
    varDef.i6name = memberI6NameFor(varDef.name, i6alias, newClass.isExternal);
    varDef.src = name.src.line > 0 ? name.src : file.currentLocation();
    varDef.type=languageService.getType((string) returnType);
    // Keep a parameterized type's full name (getType gives the bare base): func<…> so the member is
    // callable, array<T>/rawArray<T> (an inherited array reassigned here) so reads know T.
    if(((string)returnType).rfind("func<", 0) == 0 || ((string)returnType).rfind("array<", 0) == 0
       || ((string)returnType).rfind("rawarray<", 0) == 0 || isUnionType((string)returnType)) varDef.type.name = (string)returnType;
    if(isMemberConst) varDef.isConst = true;
    varDef.isStatic = isMemberStatic;
    varDef.isInline = q.isInline;   // participates in positional inline construction (§6.2.1)
    if(q.isInline){ notePositional(newClass, varDef.name); addInlineOrder(newClass, varDef.name); }
    // A value class owns its members: one of its own type would own another, without end.
    if(!q.isRef && !isMemberStatic && varDef.type.name == newClass.name && !newClass.isExternal
       && (isValueClass(&newClass) || (plainSlotOwnsReferenceInstance(&newClass) && !tok.is(token::assignment))))
        parsingError(format(kindWord(&newClass) + " '{0}': member '{1}' is a '{0}', which would own another '{0}' without end; declare it 'ref'",
                            newClass.dName(), varDef.dName()));
    if(newClass.isExternal && !q.isRef && !isMemberStatic && !isWordArrayType(varDef.type.name))
        rejectPlainExternMember(varDef.type.name, varDef.dName());
    if(q.isTypeSealed) varDef.isTypeSealed = true;
    varDef.isLiteral = q.isLiteral;
    varDef.isRefLocal = q.isRef;   // `ref` member: assignments are pointer-copy (opt out of operator=)
    varDef.isRequired = q.isRequired;
    if(q.isRequired && tok.is(token::assignment))
        parsingWarning(format("'{0}' is required but has a value here, so nothing needs to supply it.", varDef.dName()));
    if(q.isRef){
        string why = refNotApplicable(getDispatchClass(varDef.type.name), typeDisplayName(varDef.type.name));
        if(!why.empty()) parsingError("'ref': " + why);
    }
    // A subclass re-declaring a base member that was marked `typesealed` keeps the sealed
    // type (and gets a warning) — mirrors the object-instance rule in processMemberVariable.
    {
        typeMember* inh = findMemberInHierarchy(&newClass, [&](typeMember* m){
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            return vd && vd->name == varDef.name && vd->isTypeSealed;
        });
        if(auto* inhVd = inh ? dynamic_cast<variableDeclaration*>(inh) : nullptr){
            if(varDef.type.name != inhVd->type.name){
                if(!sealedSlotTakes(inhVd->type.name, varDef.type.name))
                parsingWarning(format("You've redefined '{0}' (which is typesealed as '{1}') to '{2}'. The retype will be ignored and stay '{1}'.",
                    varDef.name, typeDisplayName(inhVd->type.name), typeDisplayName(varDef.type.name)));
                // Lock the slot to the sealed type; the initializer below still validates against
                // the written type (`returnType`), which has real value compatibility.
                varDef.type = inhVd->type;
                varDef.isTypeSealed = true;
            }
        }
    }
    if(!returnType.docComment.empty())   varDef.docComment = returnType.docComment;
    else if(!name.docComment.empty())    varDef.docComment = name.docComment;
    if(tok.is(token::assignment) && parseInlineAccessor(varDef, (string)returnType, (typeDef*)&newClass)){
        // the accessor took the member; fall through to registration
    }
    else if(tok.is(token::assignment)){
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
                string expectedElemType = (rtStr.rfind("array<",0)==0 || rtStr.rfind("rawarray<",0)==0)
                                          ? listElementType(rtStr) : "";
                if(!expectedElemType.empty())
                    for(size_t i = 0; i < list->elements.size(); i++){
                        expression* elem = list->elements[i];
                        if(elem->resolvedType.empty())
                            parsingError(format("Undeclared identifier in initializer list (element {0})", i));
                        else if(!zeroFitsBnum(elem, expectedElemType) && !isArrayElementCompatible(elem->resolvedType, expectedElemType))
                            parsingError(format("Element {0} has type '{1}', expected '{2}'", i, elem->resolvedType, expectedElemType));
                        checkByteElementRange(elem, expectedElemType);
                    }
            }
            varDef.declaredExpressionValue = list;
        } else {
            inMemberInitializer = true;   // fixed at compile time: a bare name is a global, never a member
            expression* expr = nullptr;
            try { expr = parseExpression(first, {token::endStatement}, nullptr, nullptr); } catch(...) { inMemberInitializer = false; throw; }
            inMemberInitializer = false;
            foldIntLiteralToFloat(expr, (string)returnType);
            if(!isMemberStatic) rejectRunTimeMemberRead(expr, varDef.dName());
            if(!isMemberStatic) rejectValueClassInitializer(expr, (string)returnType, varDef.dName());
            if(initializerList* list = asSingleElementList(expr, (string)returnType))
                varDef.declaredExpressionValue = list;
            else
                varDef.declaredExpressionValue = expr;
        }
    }
    variableDeclaration* member = &varDef;
    if(variableDeclaration* arr = asInheritedArrayOverride(varDef, newClass.baseClasses)) member = arr;
    // A static of an owning type (stringObj, a value class) is set up as a global of that type is.
    if(isMemberStatic && !q.isRef && member == &varDef)
        if(classDef* tc = languageService.findClass(varDef.type.name);
           tc != nullptr && !tc->isAlias
           && ((isValueClass(tc) && !tc->isEmitterClass) || findMemberInHierarchy(tc, [](typeMember* m){
                   auto* f = dynamic_cast<functionDef*>(m); return f && f->isEmitter && f->name == "init" && f->params.empty(); }))){
            auto* g = new variableDeclaration();
            g->name = "_bgl_" + newClass.name + "_" + varDef.name;
            g->displayName = "_bgl_" + newClass.dName() + "_" + varDef.dName();
            g->type = varDef.type;
            g->src = varDef.src;
            g->declaredExpressionValue = varDef.declaredExpressionValue;
            languageService.globals.push_back(g);
            recordGlobalVariableInit(*g, false, nullptr, nullptr);
            g->needsEarlyGlobalDecl = true;
            varDef.staticHasGlobal = true;
        }
    if(isExtend){
        typeMember* existing = nullptr;
        for(typeMember* m : newClass.members){
            variableDeclaration* vd = dynamic_cast<variableDeclaration*>(m);
            if(vd && vd->name == varDef.name){ existing = m; break; }
        }
        bool existingIsStub = existing != nullptr && existing->isPrePassStub;
        // Inform 6 owns an extern class's instances (the library's objects among them), so a new field
        // could never be given to all of them. `extend extern class` describes one Inform 6 already provides.
        if(newClass.isExternal && !extendIsExtern && !isMemberStatic && !isEmitter && !varDef.isAlias && (existing == nullptr || existingIsStub))
            parsingError(format("extend class '{0}': '{0}' is an extern class, so 'extend' can add only emitters and "
                                "static members to it. To give the type of a property Inform 6 already provides, write "
                                "`extend extern class {0}`; for a new one, declare '{1}' on a class of your own.",
                                newClass.dName(), varDef.dName()));
        if(existing && !existingIsStub && !isReplace)
            parsingError(format("extend class '{0}': member '{1}' is already defined; use 'replace' to override", newClass.dName(), varDef.dName()));
        if(!existing && isReplace)
            parsingWarning(format("extend class '{0}': 'replace' specified but there is no member '{1}' to replace; it is added",
                                  newClass.dName(), varDef.dName()));
        if(existing)
            for(auto it=newClass.members.begin(); it!=newClass.members.end(); ++it)
                if(*it==existing){ newClass.members.erase(it); break; }
    } else {
        warnMemberHidesDefine(varDef.name, varDef.dName());
        if(!varDef.isAlias && !varDef.isValueAlias && !varDef.isExternal)
            rejectMethodBesideField(newClass, varDef.name, varDef.dName(), false);
        // Check for an existing stub from pre-scan (static member variables) and replace it.
        bool replacedStub = false;
        for(size_t i = 0; i < newClass.members.size(); i++){
            if(auto* vd = dynamic_cast<variableDeclaration*>(newClass.members[i])){
                if(vd->name == varDef.name){
                    if(vd->isPrePassStub){
                        newClass.members[i] = (typeMember*)member;
                        replacedStub = true;
                    } else {
                        parsingError(format(kindWord(&newClass) + " '{0}': member '{1}' is already defined", newClass.dName(), varDef.dName()));
                    }
                    break;
                }
            }
        }
        if(!replacedStub) newClass.members.push_back((typeMember*)member);
        tok=file.getToken();
        return true;
    }
    newClass.members.push_back((typeMember*)member);
    return false;
}

// Parse one member of a class body — a single iteration of the member loop. On entry `tok` is the
// member's first token; on return it is the first token of the next member (or the closing '}').
void bglParser::parseClassMember(classDef& newClass, token& tok, bool isExternal, bool isExtend){
    markMemberStart(tok);
    rejectAccessModifier(tok);
    if(tok.is(eTokenType::eof))
        parsingError(format("Unexpected end of file inside class '{0}' — missing closing '}}'", newClass.dName()));
    // Conditional compilation between members. Needed for inline accessor objects — an
    // `auto name = { … }` accessor is parsed as a nested class body, so a target guard like
    //   #if TARGET_GLULX
    //       int _win = 0;
    //   #endif
    // sitting among the member declarations must be honored here, not treated as a member.
    // Route the #if/#elif/#else/#endif family through the shared directive handler, which
    // evaluates the condition and skips dead branches against this class context; any other
    // directive isn't a member form in a class body.
    if(tok.is(eTokenType::directive)){
        if(tok.is("#if") || tok.is("#elif") || tok.is("#else") || tok.is("#endif"))
            processDirective(tok, newClass);
        else
            parsingError(format("Unsupported directive in class body: '{0}'", tok.value));
        tok = file.getToken();
        return;
    }
    // `hide <member>[.operator <op>][(types)];` — per-type access control for an inherited
    // member. Recognised at member-start position; safe because no type is named `hide`, so a
    // member declaration never begins with it. Applies to both subclass bodies and `extend class`.
    if(tok.is("hide") && (file.peekToken().is(eTokenType::identifier) || file.peekToken().is(eTokenType::dataType))){
        parseHideDirective(newClass);
        tok = file.getToken();
        return;
    }
    Qualifiers q = parseQualifiers(tok);
    bool isEmitter = q.isEmitter;
    bool isReplace = q.isReplace;
    bool isOperator = false;
    token returnType;
    token name;
    // emitter class: 'emitter' keyword is optional — all members are implicitly emitters
    // Members of an emitter class are implicitly emitters — except a `static`, which is
    // deliberately a real routine (no receiver, emitted free). That is how an emitter
    // class can still publish something callable.
    if(newClass.isEmitterClass && !isEmitter && !q.isStatic) isEmitter = true;
    // Context-specific validation
    if(q.isExtern && !isExtend)
        parsingError("'extern' is not valid inside a class body (use on the class declaration itself)");
    if(q.isExtend)
        parsingError("'extend' is not valid inside a class body");
    if(q.isAlias)
        parsingError("'alias' is not valid inside a class body");
    // array<T> member: same handler as object bodies.
    // Class is never verb-derived, so no grammarRule downcast applies.
    // `rawArray<T>` declares the same storage as `array<T>` but without Beguile's
    // tracking layer — the plain I6 property array an I6 library expects to read as
    // `obj.&prop-->n`. Both route through processArrayMember, which records which.
    if((tok.is("array") || tok.is("rawarray")) && file.peekToken().is("<") && !genericTypeIsUnionHead()){
        processArrayMember(newClass.members, newClass.dName(), nullptr, &newClass, &q, tok.is("rawarray"), tok.docComment);
        tok = file.getToken();
        return;
    }
    // `inline name;` re-lists an inherited member as positional, at this point in the class's order.
    // The member itself stays where it is declared (and keeps its type and `const`).
    if(q.isInline && (tok.is(eTokenType::identifier) || tok.is(eTokenType::dataType)) && file.peekToken().is(token::endStatement)){
        string shown = tok.originalValue.empty() ? tok.value : tok.originalValue;
        if(!newClass.findMember([&](typeMember* m){ return dynamic_cast<variableDeclaration*>(m) && m->name == tok.value; }))
            parsingError(format(kindWord(&newClass) + " '{0}': 'inline {1};' makes an existing member positional, but '{0}' has no member '{1}'",
                                newClass.dName(), shown));
        if(!notePositional(newClass, tok.value))
            parsingError(format(kindWord(&newClass) + " '{0}': '{1}' is already positional in '{0}'", newClass.dName(), shown));
        addInlineOrder(newClass, tok.value);
        file.getToken(token::endStatement);
        tok = file.getToken();
        return;
    }
    // A member typed by a namespace path (`tables.flag flags;`), as a global may be.
    if(tok.is(eTokenType::identifier) && file.peekToken().is(token::period) && isNamespacedTypePath(tok))
        tok = consumeTypeToken(tok);
    // ... or by a `#using`-imported alias (`code c;`, `code pick()`).
    else if(tok.is(eTokenType::identifier) && (file.peekToken().is(eTokenType::identifier) || file.peekToken().is(eTokenType::dataType)))
        tok = consumeTypeToken(tok);
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
            for(classDef* base : newClass.baseClasses){
                typeMember* m = base->findMember([&](typeMember* mm){
                    auto* vd = dynamic_cast<variableDeclaration*>(mm);
                    return vd != nullptr && vd->name == memberName;
                });
                if(m == nullptr) continue;
                if(auto* arr = dynamic_cast<arrayDeclaration*>(m))
                    inferredType = (arr->isRaw ? "rawarray<" : "array<") + arr->elementType + ">";
                else
                    inferredType = dynamic_cast<variableDeclaration*>(m)->type.name;
                break;
            }
            if(!inferredType.empty()){
                // Inferred: treat tok as the member name, use the inherited type
                returnType.value = inferredType;
                returnType.tokenType = eTokenType::dataType;
                name = tok;
            } else {
                string shown = tok.originalValue.empty() ? tok.value : tok.originalValue;
                bool rootOnly = true;
                for(classDef* b : newClass.baseClasses) if(b->name != "_bglobject") rootOnly = false;
                token value = file.peekToken(2);   // after the `=`
                string suggested = value.isString() ? "string" : value.is(eTokenType::floatLiteral) ? "float"
                                 : value.is(eTokenType::charLiteral) ? "char"
                                 : (value.is("true") || value.is("false")) ? "bool" : "int";
                parsingError(format(kindWord(&newClass) + " '{0}': '{1}' is set here but isn't a member of '{0}'{2}. {3}To declare "
                    "a new member, give it a type: `{4} {1} = …;`.", newClass.dName(), shown,
                    rootOnly ? "" : " or its bases",
                    rootOnly ? format("A class has no base unless it names one: `class {0} : object {{ … }}` "
                                      "inherits object's members, such as `name`. ", newClass.dName()) : "", suggested));
                returnType = tok;
                name = file.getToken({eTokenType::identifier, eTokenType::dataType});
            }
        } else {
            if(tok.is(eTokenType::identifier) && file.peekToken(1).is(eTokenType::identifier)) unknownTypeError(tok);
            tok.assertDataType();
            returnType = tok;
            returnType.value = maybeParseUnionTail(returnType.value);  // A | B | ... union member type
            name = file.getToken({eTokenType::identifier, eTokenType::dataType});
        }
    } else {
        if(tok.is(eTokenType::identifier) && file.peekToken(1).is(eTokenType::identifier)) unknownTypeError(tok);
        returnType=tok.assertDataType();
        if(returnType.value == "func") returnType.value = parseFuncType();  // func<...> member type: consume its own <...>
        else if((returnType.value == "array" || returnType.value == "rawarray") && file.peekToken().is("<"))
            returnType.value = parseArrayTypeTail(returnType.value);   // `array<int> | string slot`
        returnType.value = maybeParseUnionTail(returnType.value);  // A | B | ... union member type
        name=file.getToken({eTokenType::identifier, eTokenType::dataType});
    }
    string memberI6Alias;
    if(name.is("operator")){
        isOperator=true;
        parseOperatorMemberName(tok, name);
    } else {
        // Optional `as <i6name>` (§3.11) after the member name, the same position and meaning it
        // has on an object member. Without this the token here is the identifier `as`, which is
        // neither a symbol nor an operator, and the member was rejected outright.
        if(file.peekToken().is("asi6")){
            file.getToken();   // 'as'
            token aliasTok = file.getToken(eTokenType::identifier);
            memberI6Alias = aliasTok.originalValue.empty() ? aliasTok.value : aliasTok.originalValue;
        }
        tok=file.getToken({eTokenType::symbol, eTokenType::oper});
    }

    if(tok.is(token::parenOpen))  { //this is a function
        if(parseClassMethodMember(newClass, tok, name, returnType, q, isEmitter, isExternal, isExtend, memberI6Alias)) return;
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
        if(!replaceStubMember(newClass.members, funcDef)){
            // A same-named value in this class body: `replace` swaps it in place, as for a method.
            auto it = find_if(newClass.members.begin(), newClass.members.end(), [&](typeMember* m){
                auto* fd = dynamic_cast<functionDef*>(m);
                return fd && fd->name == funcDef.name && fd->isValueEmitter;
            });
            if(it != newClass.members.end()){
                if(!q.isReplace)
                    parsingError(format(kindWord(&newClass) + " '{0}': emitter value '{1}' is already defined; use 'replace' to override",
                                        newClass.dName(), funcDef.dName()));
                *it = &funcDef;
            } else
                newClass.members.push_back(&funcDef);
        }
    }
    else{
        if(isOperator==true) parsingError("Operators must be functions.");
        if(parseClassVariableMember(newClass, tok, name, returnType, q, isEmitter, isExtend, memberI6Alias, isExternal)) return;
    }
    tok=file.getToken();
}

bool bglParser::processClassDeclaration(token tok, bool isExternal, bool isExtend, bool isEmitterClass, bool isAlias, token nameOverride, bool allowNested, bool isSuperposed, bool isValue, bool isPrimitive, bool isStruct){
    // `allowNested` = synthesizing an inline accessor's anonymous class from inside an object body
    // (`auto name = { … }`); the class is still registered globally, it's just parsed nested. All
    // other class declarations must be at global scope.
    if(!allowNested && getCurrentCompileContext()!=eCompileContext::global) parsingError(format("Class declarations are only allowed in global context:'{0}'", (string) tok));

    token nameTok = nameOverride.tokenType != eTokenType::unknown ? nameOverride : file.getToken({eTokenType::identifier, eTokenType::dataType});
    classDef* classPtr = nullptr;
    if(isExtend){
        classPtr = languageService.findClass((string)nameTok);
        if(classPtr == nullptr) parsingError(format("extend class '{0}': no previously defined class with that name", (string)nameTok));
        // The pre-scan knows every class, but an extend applies to the class as declared so far: it
        // has to come after the declaration.
        if(classPtr->isPrePassStub)
            parsingError(format("extend class '{0}': '{0}' is declared later; a class must be declared before it is extended",
                                nameTok.originalValue.empty() ? (string)nameTok : nameTok.originalValue));
        // A struct is extended as a struct, and a class as a class.
        string shown = nameTok.originalValue.empty() ? (string)nameTok : nameTok.originalValue;
        if(isStruct && !classPtr->isValue)
            parsingError(format("'{0}' is a class, not a struct; extend it with `extend class {0}`.", shown));
        if(!isStruct && classPtr->isValue && !classPtr->isEmitterClass)
            parsingError(format("'{0}' is a struct; extend it with `extend struct {0}`.", shown));
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
        if(isSuperposed)   newClass.isSuperposed = true;   // withhold source-order emit; revive on reference
        if(isValue)        newClass.isValue = true;
        if(isPrimitive)    newClass.isPrimitive = true;
    }
    if(isPrimitive && isExtend)
        parsingError(format("'extend class {0}': 'primitive' belongs on the original class declaration.", (string)nameTok));
    if(isValue && isPrimitive)
        parsingError(format("'{0}': a struct can't be primitive; a primitive holds its value directly.", (string)nameTok));
    if(isValue && (isExternal || isEmitterClass || isAlias))
        parsingError(format("'struct {0}': a struct has instances of its own; it can't be extern, emitter or alias.", (string)nameTok));
    // `superposed` only makes sense for a class that emits a revivable `Class` directive.
    if(isSuperposed){
        if(isExtend)       parsingError(format("'extend class {0}': 'superposed' belongs on the original class declaration, not on extend.", (string)nameTok));
        if(isExternal)     parsingError(format("'superposed extern class {0}': extern classes are I6-defined — Beguile emits no directive to withhold.", (string)nameTok));
        if(isEmitterClass) parsingError(format("'superposed emitter class {0}': emitter classes have no I6 backing to withhold.", (string)nameTok));
        if(isAlias)        parsingError(format("'superposed alias class {0}': alias classes dissolve to their parent for emission; nothing to withhold.", (string)nameTok));
    }

    classDef* savedClass = currentClass;
    currentClass = &newClass;
    openCompileContext(eCompileContext::objectDef);

    parseClassTypeParameters(newClass, nameTok, isExtend, isAlias);
    parseClassPoolSize(newClass, tok, nameTok, isExternal, isExtend, isEmitterClass, isAlias);
    // An extension adds members; the parent list belongs on the class's own declaration.
    if(isExtend && tok.is(":"))
        parsingError(format("'extend class {0}' can't add parents; list them on the class's own declaration.", newClass.dName()));
    if(isStruct && !isExtend && tok.is(":"))
        parsingError(format("struct '{0}' can't have bases: a struct doesn't inherit.", newClass.dName()));
    parseClassInheritance(newClass, tok, nameTok, isAlias);
    // Type parameters belong to `array` and `rawArray` alone (§8.1.1): array storage is keyed on those names.
    if(!newClass.typeParameters.empty() && !isExtend && newClass.name != "array" && newClass.name != "rawarray")
        parsingError(format(kindWord(&newClass) + " '{0}': Beguile has no generic classes; a type parameter belongs to "
            "`array<T>` and `rawArray<T>` only. Declare the member types directly; to give arrays "
            "a method that uses T, write it in `extend extern class array {{ … }}`.",
            newClass.dName()));
    // A struct has no subclasses.
    if(!isExtend && !newClass.isEmitterClass && !newClass.isAlias)
        for(classDef* base : newClass.baseClasses)
            if(isValueClass(base))
                parsingError(format("class '{0}' can't derive from struct '{1}': a struct has no subclasses.", newClass.dName(), base->dName()));
    // Marker-form extern pool class (`extern class Foo[];`) allows no body — it's purely a
    // declaration that the type exists and is pooled in I6. Skip body parsing and return.
    if(isExternal && newClass.poolSize == -1 && tok.is(token::endStatement)){
        closeCompileContext(eCompileContext::objectDef);
        currentClass = savedClass;
        return false;
    }
    tok.assert(token::braceOpen);
    prebuildInlineAccessors(newClass);   // so a method above an accessor knows its type
    tok=file.getToken();
    while(tok.isNot(token::braceClose)) parseClassMember(newClass, tok, isExternal, isExtend);
    // A struct that declares no copy operator gets one that copies each field. Its body is written once the
    // parse is done, when `extend struct` has added every field; until then this one stands in for it.
    bool declaresCopy = false;
    for(typeMember* m : newClass.members)
        if(auto* f = dynamic_cast<functionDef*>(m); f && f->name == "=" && !f->isPrePassStub && f->params.size() == 1
           && (getDispatchClass(f->params[0]->type.name) == &newClass
               || getDispatchClass(typeAliasTarget(f->params[0]->type.name)) == &newClass)) declaresCopy = true;
    // The pre-scan's stand-in for an automatic copy operator goes once the struct turns out to declare its own.
    if(isStruct && !isExtend && declaresCopy)
        newClass.members.erase(remove_if(newClass.members.begin(), newClass.members.end(), [&](typeMember* m){
            auto* f = dynamic_cast<functionDef*>(m);
            return f && f->isPrePassStub && f->name == "=" && f->params.size() == 1 && f->params[0]->type.name == newClass.name; }),
            newClass.members.end());
    if(isStruct && !isExtend && !declaresCopy){
        token close = tok;
        string nm = newClass.dName();
        file.openText(nm + " operator = (" + nm + " o) { return self; } }", nameTok.src.file, nameTok.src.line);
        try {
            tok = file.getToken();
            while(tok.isNot(token::braceClose)) parseClassMember(newClass, tok, isExternal, isExtend);
        } catch(...) { file.close(); throw; }
        file.close();
        tok = close;
        autoCopyStructs.push_back(&newClass);
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
                    parsingError(format(kindWord(&newClass) + " '{0}': only one 'create' method is allowed on a pooled class (I6's create veneer dispatches to a single property)", newClass.dName()));
                createFn = fd;
            } else if(fd->name == "destroy"){
                if(destroyFn != nullptr)
                    parsingError(format(kindWord(&newClass) + " '{0}': only one 'destroy' method is allowed on a pooled class", newClass.dName()));
                destroyFn = fd;
            }
        }
        if(createFn != nullptr){
            if(createFn->returnType.name != "void")
                parsingError(format(kindWord(&newClass) + " '{0}': 'create' must have void return type — I6 ignores the return value", newClass.dName()));
            // Z-machine only. Z's CA__Pr hands Cl__Ms a fixed `(obj, id, y, a, b, c, d)` and its
            // create arm enumerates the cases up to three, erroring past that; Glulx's CA__Pr and
            // Cl__Ms both forward `_vararg_count` through to `create`, so there is no cap there.
            if(targetIsZcode() && createFn->params.size() > 3)
                parsingError(format(kindWord(&newClass) + " '{0}': 'create' is limited to 3 parameters on the Z-machine (I6's class-message veneer enumerates its arguments; Glulx passes them through, so this limit is Z-only). Got {1}.", newClass.dName(), createFn->params.size()));
        }
        if(destroyFn != nullptr){
            if(destroyFn->returnType.name != "void")
                parsingError(format(kindWord(&newClass) + " '{0}': 'destroy' must have void return type — I6 ignores the return value", newClass.dName()));
            if(!destroyFn->params.empty())
                parsingError(format(kindWord(&newClass) + " '{0}': 'destroy' must take no parameters", newClass.dName()));
        }
    }

    newClass.copiesOnAssign = hasCopyOperator(&newClass);
    currentClass = savedClass;
    closeCompileContext(eCompileContext::objectDef);
    return false;
}
// Reads <ReturnType, ParamType, ...> from stream and returns the full "func<...>" type string.
// Called after the "func" token has been consumed.

// ===============================================================================
// Object body member parsers
// ===============================================================================
// Inline accessor declaration: `auto name = { <member decls + operators> }`. The braces hold a
// CLASS BODY (backing fields + get/set operators), not an initializer list. Synthesize an anonymous
// value class from the body, registered globally and placed just before `host` (the enclosing object
// or class) so its I6 `Class` precedes the instances baked for the member. Detected by a class-body
// shape at the front of the brace (`<type> <ident>` / `<type> operator` / `operator`); an ordinary
// `{ v1, v2 }` / `{ f = v }` initializer is left to the caller. Returns true when it took the member.
// A class body's inline accessors (`auto x = { … }`), built before its members are parsed, so a method
// written above one reads and writes it through its getter and setter. The body is then parsed from
// its start as usual; each accessor reuses the class built here.
void bglParser::prebuildInlineAccessors(classDef& cls){
    if(file.hasPendingToken) return;
    fileLexer::mark_t start = file.mark();
    // Only an accessor named above its declaration needs it: building one early would parse its body
    // before the members above it, which it may read.
    string before;
    auto usedBefore = [&](const string& name){
        string low = before; transform(low.begin(), low.end(), low.begin(), ::tolower);
        return regex_search(low, regex("(^|[^a-z0-9_])" + name + "([^a-z0-9_]|$)"));
    };
    try {
        token t = file.getToken();
        while(!t.is(eTokenType::eof) && !t.is(token::braceClose)){
            if(t.is(token::braceOpen)) before += " " + file.getRawTextThroughClosingBrace() + " ";
            else if(t.is("auto") && (file.peekToken().is(eTokenType::identifier) || file.peekToken().is(eTokenType::dataType))
                    && file.peekToken(2).is(token::assignment) && file.peekToken(3).is(token::braceOpen)
                    && !usedBefore(file.peekToken().value)){
                file.getToken(); file.getToken(token::assignment); file.getToken(token::braceOpen);
                before += " " + file.getRawTextThroughClosingBrace() + " ";
            }
            else if(t.is("auto") && (file.peekToken().is(eTokenType::identifier) || file.peekToken().is(eTokenType::dataType))
                    && file.peekToken(2).is(token::assignment) && file.peekToken(3).is(token::braceOpen)){
                token name = file.getToken();
                file.getToken(token::assignment);
                auto* v = new variableDeclaration();
                v->name = name.value;
                v->displayName = name.originalValue;
                if(parseInlineAccessor(*v, "auto", &cls)){
                    earlyAccessorClass[{&cls, v->name}] = v->type.name;
                    for(typeMember* m : cls.members)   // the pre-scan's stub learns the accessor's type
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd && vd->isPrePassStub && vd->name == v->name)
                            vd->type = v->type;
                }
            }
            else before += " " + t.value;
            t = file.getToken();
        }
    } catch(...) { file.rewind(start); throw; }
    file.rewind(start);
}

bool bglParser::parseInlineAccessor(variableDeclaration& prop, const string& typeName, typeDef* host){
    if(!((typeName == "auto" || typeName == "var") && file.peekToken().is(token::braceOpen))) return false;
    int q = 2;   // past member qualifiers (`static int reads;`)
    static const set<string> qualifiers = {"static", "const", "inline", "ref", "emitter", "explicit", "replace", "default", "superposed"};
    while(qualifiers.count(file.peekToken(q).value)) q++;
    token b2 = file.peekToken(q);
    int k = q + 1;   // past a dotted type path (`Units.mode _m;`)
    if(b2.is(eTokenType::identifier))
        while(file.peekToken(k).is(token::period)
              && (file.peekToken(k + 1).is(eTokenType::identifier) || file.peekToken(k + 1).is(eTokenType::dataType))) k += 2;
    token b3 = file.peekToken(k);
    bool classBody = b2.is("operator")
        || ((b2.is(eTokenType::dataType) || b2.is(eTokenType::identifier))
            && (b3.is(eTokenType::identifier) || b3.is("operator")))
        || ((b2.value == "array" || b2.value == "rawarray" || b2.value == "func") && file.peekToken(q + 1).is("<"));   // `array<int> _log[2];`
    if(!classBody) return false;
    // Built ahead by prebuildInlineAccessors: the body was parsed then; skip it here.
    if(auto* hc = dynamic_cast<classDef*>(host))
        if(auto it = earlyAccessorClass.find({hc, prop.name}); it != earlyAccessorClass.end()){
            file.getToken(token::braceOpen);
            file.getRawTextThroughClosingBrace();
            if(file.peekToken().is(token::endStatement)) file.getToken();
            prop.type = languageService.getType(it->second);
            return true;
        }
    string anon = format("_bglaccessor{0}", anonObjectCounter++);
    token nameTok; nameTok.tokenType = eTokenType::identifier; nameTok.value = anon; nameTok.originalValue = anon;
    token clsTok;  clsTok.tokenType  = eTokenType::identifier; clsTok.value  = "class";
    // `outer` names the enclosing object, a compile-time singleton; a class has no single instance.
    objectDef* savedAccOuter = accessorOuter;
    accessorOuter = currentObject;
    classDef* savedClass = currentClass;
    processClassDeclaration(clsTok, /*isExternal*/false, /*isExtend*/false, /*isEmitterClass*/false,
                            /*isAlias*/false, nameTok, /*allowNested*/true, /*isSuperposed*/false, /*isValue*/true);
    currentClass = savedClass;
    accessorOuter = savedAccOuter;
    if(file.peekToken().is(token::endStatement)) file.getToken();   // `auto x = { … };` reads either way
    prop.type = languageService.getType(anon);
    if(auto* accCls = languageService.findClass(anon)){
        // A one-off class for this member: superposed, so its `Class` materializes only when an
        // instance is baked for it; an unused host takes it along.
        accCls->isSuperposed = true;
        auto& g = languageService.globals;
        size_t pos = g.size();
        if(host)
            for(size_t i = 0; i < g.size(); i++) if(g[i] == host){ pos = i; break; }
        g.insert(g.begin() + pos, accCls);
    }
    return true;
}

void bglParser::parsePropertyValue(variableDeclaration& prop, string typeName){
    // Inline accessor declaration: `auto name = { <member decls + operators> }`. The braces hold a
    // CLASS BODY (backing fields + get/set operators), not an initializer list. Synthesize an
    // anonymous (non-`object`) class from the body — registered globally, so the emitter bakes a
    // backing instance for `name` (create+populate) and get/set dispatch lands on a real object.
    // Detected by a class-body shape at the front of the brace (`<type> <ident>` / `<type> operator`
    // / `operator`); an ordinary `{ v1, v2 }` / `{ f = v }` initializer takes the path below.
    if(parseInlineAccessor(prop, typeName, (typeDef*)currentObject)) return;
    fileLexer::mark_t beforeValue = file.mark();
    token first = file.getToken();
    // `fill = { 40, 200, 40 };` for a value-class member: the class's inline form, as `Rgb{ … }` (§11.3.2).
    if(first.is(token::braceOpen) && !file.hasPendingToken)
        if(classDef* vc = languageService.findClass(typeName); vc != nullptr && isValueClass(vc) && isInlineConstructible(vc)){
            file.rewind(beforeValue);
            token typeTok = first;
            typeTok.tokenType = eTokenType::dataType;
            typeTok.value = typeName;
            typeTok.originalValue = vc->dName();
            first = typeTok;
        }
    // Rows of an array of arrays bake as anonymous global arrays, as the class's own do.
    if(first.is(token::braceOpen) && isArrayOfArraysElement(listElementType(typeName))){
        prop.declaredExpressionValue = parseArrayInitializerList(listElementType(typeName), nullptr, nullptr);
        if(file.peekToken().is(token::endStatement)) file.getToken();
        return;
    }
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
            // In an attribute list a bare name is the attribute, ahead of a member of that name.
            if(listElementType(typeName) == "attribute" && t.is(eTokenType::name)
               && (file.peekToken().is(token::comma) || file.peekToken().is(token::braceClose)))
                if(auto* av = languageService.findGlobalAs<variableDeclaration>(t.value); av && av->type.name == "attribute"){
                    expression* elem = new expression();
                    elem->tokens.push_back(av->i6name.empty() ? av->name : av->i6name);
                    elem->resolvedType = "attribute";
                    token sep = file.getToken();
                    elem->terminator = sep.value;
                    list->elements.push_back(elem);
                    if(sep.is(token::braceClose)) break;
                    t = file.getToken();
                    continue;
                }
            expression* elem = parseExpression(t, {",", token::braceClose}, nullptr, nullptr);
            list->elements.push_back(elem);
            if(elem->terminator == token::braceClose) break;
            t = file.getToken();
        }
        if(file.peekToken().is(token::endStatement)) file.getToken();
        string expectedElemType = listElementType(typeName);
        for(size_t i = 0; i < list->elements.size(); i++){
            expression* elem = list->elements[i];
            if(elem->resolvedType.empty())
                parsingError(format("Undeclared identifier in initializer list (element {0})", i));
            else if(!expectedElemType.empty() && !zeroFitsBnum(elem, expectedElemType) && !isArrayElementCompatible(elem->resolvedType, expectedElemType))
                parsingError(format("Element {0} has type '{1}', expected '{2}'", i, elem->resolvedType, expectedElemType));
            checkByteElementRange(elem, expectedElemType);
        }
        if(list->elements.empty() && (typeName.rfind("array<", 0) == 0 || typeName.rfind("rawarray<", 0) == 0))
            parsingError(format("Array '{0}' has no storage: give it at least one element, or leave it to the class.", prop.dName()));
        prop.declaredExpressionValue = list;
    } else {
        // The member's type disambiguates the value (an enum member beats a same-named global).
        string savedExpected = currentExpectedType;
        if(typeName != "auto") currentExpectedType = typeName;
        inMemberInitializer = true;   // fixed at compile time: a bare name is a global, never a member
        expression* expr = nullptr;
        try { expr = parseExpression(first, {token::endStatement}, nullptr, nullptr); } catch(...) { inMemberInitializer = false; throw; }
        inMemberInitializer = false;
        currentExpectedType = savedExpected;
        // The object's I6 header name: text fixed when the program is compiled, so only a literal.
        if(prop.name == "instancename"){
            if(expr->resolvedType != "stringliteral")
                parsingError("'instanceName' takes a string literal: the name is fixed when the program is compiled.");
            prop.declaredExpressionValue = expr;
            return;
        }
        if(initializerList* list = asSingleElementList(expr, typeName))
            prop.declaredExpressionValue = list;
        // auto inference: if the property type is 'auto', adopt the expression's resolved type
        else if(typeName == "auto" && !expr->resolvedType.empty()){
            typeName = expr->resolvedType;
            prop.type = languageService.getType(typeName);
            prop.declaredExpressionValue = expr;
        } else {
            // An array<char> member takes a string (§11.8): the object's own buffer is made from it.
            if((typeName == "bytearray" || typeName == "array<char>") && expr->resolvedType == "stringliteral"){
                prop.declaredExpressionValue = expr;
                return;
            }
            applyImplicitConversion(expr, typeName);
            rejectValueClassInitializer(expr, typeName, prop.dName());
            if(!typeName.empty() && typeName != "var" && !expr->resolvedType.empty()
               && !isTypeCompatible(expr->resolvedType, typeName))
                applyAssignOperatorAsValue(expr, typeName);   // a primitive's `$target = $v` operator =
            if(!typeName.empty() && typeName != "var" && !expr->resolvedType.empty()
               && !isTypeCompatible(expr->resolvedType, typeName))
            {
                // A member initializer is fixed when the program is compiled, so a value class's
                // `operator =` (a routine) can't convert it; the class's own aggregate form can.
                classDef* vc = languageService.findClass(typeName);
                string hint = vc != nullptr && isValueClass(vc)
                    ? format(" A member initializer is fixed at compile time, where the class's operator = can't run; "
                             "write the value as `{0}{{ … }}` with its members.", typeDisplayName(typeName))
                    : string();
                parsingError(format("Cannot assign value of type '{0}' to property '{1}' of type '{2}'.{3}",
                    typeDisplayName(expr->resolvedType), prop.dName(), typeDisplayName(typeName), hint));
            }
            if(typeName == "verb") applyActionConstant(expr);
            rejectRunTimeMemberRead(expr, prop.dName());
            prop.declaredExpressionValue = expr;
        }
    }
}

// A member that owns a value-class instance starts from that class's own aggregate (`Cents{ … }`):
// its `operator =` is a routine, which can't run where the program is compiled.
void bglParser::rejectValueClassInitializer(expression* e, const string& typeName, const string& memberName){
    if(e == nullptr || e->resolvedType.empty() || typeName.empty()) return;
    classDef* vc = languageService.findClass(typeName);
    if(vc == nullptr || vc->isPrimitive || vc->isEmitterClass || !isValueClass(vc) || !classHasStoredFields(vc)) return;
    if(classDef* ac = languageService.classOf(e->resolvedType); ac != nullptr && (ac == vc || ac->hasAncestor(vc))) return;
    if(e->resolvedType == "nullliteral") return;
    parsingError(format("member '{0}' owns a '{1}', so it can't start from a value of type '{2}': its operator = can't "
        "run where the program is compiled. Write the value as `{1}{{ … }}` with its members.",
        memberName, typeDisplayName(typeName), typeDisplayName(literalBaseType(e->resolvedType))));
}

// A member named like a #define takes the name over in its class's or object's methods.
void bglParser::warnMemberHidesDefine(const string& name, const string& shown){
    rejectValueWordName(name);   // `int self;` can't be a member either
    if(definedSymbols.count(name))
        parsingWarning(format("Member '{0}' has the name of a #define symbol; in its methods the name means the member.", shown));
}

// A member initializer is fixed when the program is compiled: reading another object's member
// (`holder.v`) happens at run time, so it can't be one.
void bglParser::rejectRunTimeMemberRead(expression* e, const string& memberName){
    if(e == nullptr) return;
    // `int count = COUNT;`: the initializer names the global const, not the member it initializes
    // (Beguile ignores case, so the member's own name resolved first).
    {
        string t = e->text();
        t.erase(0, t.find_first_not_of(" \t")); t.erase(t.find_last_not_of(" \t") + 1);
        if(t.rfind("self.", 0) == 0 && t.find_first_of(".( ", 5) == string::npos){
            string nm = t.substr(5); transform(nm.begin(), nm.end(), nm.begin(), ::tolower);
            if(nm.rfind("_m_", 0) == 0) nm = nm.substr(3);
            if(auto* g = languageService.findGlobalAs<variableDeclaration>(nm); g && g->isConst && g->declaredExpressionValue){
                e->tokens = {g->i6name.empty() ? g->dName() : g->i6name};   // the I6 constant, by name
                return;
            }
        }
    }
    // A global variable is read at run time, so an initializer can't name one; a const can. A string's
    // own words are text, not names.
    {
        string t;
        char q = 0;
        for(char c : e->text()){
            if(q){ if(c == q) q = 0; continue; }
            if(c == '"'){ q = c; t += "0"; continue; }
            t += c;
        }
        static const regex word(R"((^|[^\w.$#'"])([A-Za-z_]\w*)(?=\s*($|[^\w.(])))");
        for(sregex_iterator it(t.begin(), t.end(), word), end; it != end; ++it){
            string w = (*it)[2].str(), lw = w;
            transform(lw.begin(), lw.end(), lw.begin(), ::tolower);
            auto* g = languageService.findGlobalAs<variableDeclaration>(lw);
            // A program's bodiless extern may name an Inform 6 object; the bindings mark theirs const.
            if(g == nullptr || g->isConst || g->isPrePassStub || (g->isExternal && g->src.file.find("beguiLib") == string::npos) || dynamic_cast<arrayDeclaration*>(g) != nullptr) continue;
            if(classDef* tc = languageService.findClass(g->type.name); tc != nullptr && !tc->isPrimitive && isValueClass(tc))
                continue;   // static data names the instance it holds
            const string& gt = g->type.name;
            if(gt == "attribute" || gt == "property" || gt == "verb" || gt == "grammartoken" || g->isNamespaceAlias()) continue;
            parsingError(format("member '{0}' is fixed when the program is compiled, so it can't read the variable '{1}'; "
                "use a const, or set the member in a routine.", memberName, g->dName()));
        }
    }
    string text = e->text(), bare;
    bool inQuote = false;
    for(char c : text){ if(c == '"') inQuote = !inQuote; else if(!inQuote) bare += c; }
    static const regex call(R"((^|[^\w.$#])([A-Za-z_]\w*)\s*\()");
    for(sregex_iterator it(bare.begin(), bare.end(), call), end; it != end; ++it){
        string lw = (*it)[2].str();
        transform(lw.begin(), lw.end(), lw.begin(), ::tolower);
        if(auto* fd = languageService.findGlobalAs<functionDef>(lw); fd && !fd->isEmitter)
            parsingError(format("member '{0}' is fixed when the program is compiled, so it can't call '{1}()', "
                "which runs at run time. Set it in a routine instead.", memberName, fd->dName()));
    }
    static const regex read(R"(([A-Za-z_]\w*)\s*\.\s*([A-Za-z_]\w*))");
    for(sregex_iterator it(bare.begin(), bare.end(), read), end; it != end; ++it){
        string owner = (*it)[1].str(), lower = owner;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if(auto* od = languageService.findGlobalAs<objectDef>(lower)){
            // Named as the author wrote it: `color.rgb`, not the backing `_bglcolor.rgb`.
            string ownerShown = od->dName();
            for(typeDef* g : languageService.globals)   // an `alias color = _bglColor` the author reached it through
                if(auto* host = dynamic_cast<objectDef*>(g))
                    for(typeMember* m : host->members)
                        if(auto* av = dynamic_cast<variableDeclaration*>(m); av && av->isNamespaceAlias()
                           && av->declaredExpressionValue != nullptr){
                            string t = av->declaredExpressionValue->text();
                            transform(t.begin(), t.end(), t.begin(), ::tolower);
                            if(t == lower) ownerShown = av->dName();
                        }
            string shown = ownerShown + "." + (*it)[2].str();
            parsingError(format("member '{0}' is fixed when the program is compiled, so it can't read '{1}', "
                "which is read at run time. Set it in a routine instead.", memberName, shown));
        }
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

// A member array is its property's data, as Inform 6 has it, unless it doesn't fit: a Z-machine property holds at
// most 32 words, counting the length slot. One that doesn't fit is promoted: its storage is a global per owning
// instance and the property holds a pointer to it, which every access goes through. Exempt: a rawArray and
// `array<dictionaryWord>`, which exist to BE I6 property data; an additive property (`name`), which I6 merges along
// the class chain; `array<char>`, whose property already holds a pointer to its buffer; and a static array.
bool bglParser::memberArrayPromotes(const arrayDeclaration& arrDecl){
    if(arrDecl.isRaw || arrDecl.isRefLocal || arrDecl.isStatic) return false;
    if(arrDecl.elementType == "dictionaryword") return false;
    if(arrDecl.isByteArray || arrDecl.elementType == "char") return false;
    const string& t = beguilerSettings.target;
    if(t.empty() || (t[0] != 'z' && t[0] != 'Z')) return false;   // Glulx properties are effectively unbounded
    int count = arrDecl.arraySize;
    if(auto* seed = dynamic_cast<initializerList*>(arrDecl.declaredExpressionValue)) count = max(count, (int)seed->elements.size());
    if(count + 1 <= 32) return false;
    return !languageService.isAdditiveProperty(arrDecl.i6name.empty() ? arrDecl.name : arrDecl.i6name);
}

void bglParser::promoteMemberArray(arrayDeclaration& arrDecl){
    if(!memberArrayPromotes(arrDecl)) return;
    arrDecl.isPromoted = true;
    arrDecl.isRefLocal = true;
}

bool bglParser::processArrayMember(vector<typeMember*>& members, const string& ownerDName, verbObjectDef* vodForGrammarRules,
                                   abstractObject* ctx, Qualifiers* q, bool declIsRaw, const string& docComment){
    // `declIsRaw` distinguishes the keyword the caller consumed. `rawArray<T>` opts out of the
    // tracking layer, giving the bare I6 property array (`obj.&prop-->n`, no header) that an I6
    // library reads directly; `array<T>` keeps Beguile's semantics.
    file.getToken("<");
    string elemType = readArrayElementType();
    bool literalElements = pendingLiteralElements;
    pendingLiteralElements = false;
    file.getToken(">");
    // `array<int> operator + (int v)`: an operator returning an array.
    bool isOperator = false;
    token propName;
    if(file.peekToken().is("operator") && ctx && q){
        propName = file.getToken();
        token afterName;
        parseOperatorMemberName(afterName, propName);   // consumes through the '('
        if(!afterName.is(token::parenOpen)) parsingError("Expected '(' after the operator.");
        isOperator = true;
    } else
        propName = file.getToken({eTokenType::identifier, eTokenType::dataType});   // a member may be named like a class
    if(q && q->isReplace && !isOperator && languageService.isAdditiveProperty(propName.value))
        parsingError(format("'{0}' is additive: every class's and object's contribution is kept, so it can't be replaced.",
                            propName.originalValue.empty() ? propName.value : propName.originalValue));

    // Routine form: `array<T> name(...)` — class/object member function returning a
    // generic array type (e.g. LINQ chain primitives like
    //   `array<var> filter(array<var> src, func<bool, var> pred) { ... }`).
    // We inline the routine-creation flow here instead of calling processRoutineDeclaration,
    // because that path registers into languageService.globals and we need member-vector
    // registration. This mirrors the relevant slice of processMemberMethod / class-body
    // routine creation, scoped to the array<T> return-type case.
    if(ctx && q && (isOperator || file.peekToken().is(token::parenOpen))){
        if(!isOperator) file.getToken(token::parenOpen);
        functionDef& funcDef = *(new functionDef());
        funcDef.name = (string)propName;
        funcDef.displayName = propName.originalValue;
        funcDef.src = propName.src.line > 0 ? propName.src : file.currentLocation();
        funcDef.returnType.name = "array<" + elemType + ">";
        funcDef.docComment = docComment;
        funcDef.isEmitter  = q->isEmitter;
        funcDef.isExternal = q->isExtern;
        funcDef.isStatic   = q->isStatic;
        if(isOperator && !funcDef.isEmitter) funcDef.i6name = mangleOperatorName(funcDef.name);

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
                parsingError(missingReturnMessage(funcDef));
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
    {
        bool ownerIsClass = dynamic_cast<classDef*>(ctx) != nullptr;
        bool ownerExtern = false;
        if(auto* oc = dynamic_cast<classDef*>(ctx)) ownerExtern = oc->isExternal || oc->isEmitterClass;   // a shape, no storage
        else if(auto* oo = dynamic_cast<objectDef*>(ctx)) ownerExtern = oo->isExternal;
        bool isRequired = q && q->isRequired;
        string shown = propName.originalValue.empty() ? propName.value : propName.originalValue;
        if(sym.is(token::endStatement) && !ownerExtern && !(q && (q->isRef || q->isExtern)) && !isRequired)
            parsingError(ownerIsClass
                ? format("Array '{0}' needs a capacity ([n]) or an initializer, or 'required' if every object must give it.", shown)
                : format("Array '{0}' needs a capacity ([n]) or an initializer.", shown));
        if(isRequired && !sym.is(token::endStatement))
            parsingWarning(format("'{0}' is required but has a value here, so nothing needs to supply it.", shown));
    }
    arrayDeclaration& arrDecl = *(new arrayDeclaration());
    arrDecl.isRequired = q && q->isRequired;
    arrDecl.docComment = docComment;
    arrDecl.src = file.currentLocation();   // so diagnostics on this member array report a line
    arrDecl.name = (string)propName;
    {
        bool ownerExtern = false;
        if(auto* oc = dynamic_cast<classDef*>(ctx)) ownerExtern = oc->isExternal;
        else if(auto* oo = dynamic_cast<objectDef*>(ctx)) ownerExtern = oo->isExternal;
        arrDecl.i6name = memberI6NameFor(arrDecl.name, "", ownerExtern);   // also rejects a class's name
    }
    arrDecl.literalElements = literalElements;
    if(q) arrDecl.isInline = q->isInline;   // an `inline array<T>` member is a positional slot (§6.2.1)
    if(q && q->isInline)
        if(auto* ownerCls = dynamic_cast<classDef*>(ctx)){ notePositional(*ownerCls, arrDecl.name); addInlineOrder(*ownerCls, arrDecl.name); }
    arrDecl.isRaw = declIsRaw;             // `rawArray<T>` member: no tracking layer
    // `ref array<T> name;` — the slot holds a POINTER to an array owned elsewhere rather than
    // inline property data, so it is addressed as a value: (obj.prop, 0), not (obj, prop).
    if(q) arrDecl.isRefLocal = q->isRef;
    arrDecl.type = languageService.getType("array");
    arrDecl.elementType = elemType;
    // A struct's instances are copied and pooled member by member; rows of rows aren't.
    if(auto* sc = dynamic_cast<classDef*>(ctx); sc != nullptr && isValueClass(sc) && !(q && (q->isRef || q->isStatic)) && isArrayOfArraysElement(elemType))
        parsingError(format("Array '{0}': a struct can't hold an array of arrays; make '{1}' a class, or make the member a "
            "`ref` to an array of arrays kept elsewhere.", propName.originalValue.empty() ? propName.value : propName.originalValue,
            sc->dName()));
    // `[N]` fixes the capacity; an initializer may follow to seed the leading slots, as on a global.
    if(sym.is(token::bracketOpen)){
        arrDecl.arraySize = readCompileTimeInt("an array's capacity");
        if(arrDecl.arraySize < 1)
            parsingError(format("Array '{0}' needs a capacity of at least 1.", arrDecl.dName()));
        file.getToken(token::bracketClose);
        sym = file.getToken({token::endStatement, token::assignment});
    }
    if(sym.is(token::assignment)){
        // Check for string initializer: array<char> name = "text";
        token peek = file.peekToken(1);
        if(peek.is(eTokenType::quote) || peek.is(eTokenType::rawQuote)){
            if(elemType == "char" || elemType == "charliteral"){
                if(arrDecl.arraySize > 0)   // as for a global: the text would decide the length, not the capacity
                    parsingError(format("Array '{0}': a string initializer cannot be combined with a declared size. "
                                        "Use `array<char> {0} = \"...\";` to size it from the literal, or "
                                        "`array<char> {0}[{1}];` and assign elements.", arrDecl.dName(), arrDecl.arraySize));
                token strTok = file.getToken();
                arrDecl.stringInitializer = strTok.value;
                arrDecl.isByteArray = true;
                arrDecl.type = languageService.getType("bytearray");
                if(file.peekToken().is(token::endStatement)) file.getToken();
            } else if(elemType.empty() || elemType == "string" || elemType == "stringliteral" || elemType == "var"){
                // One-element shorthand: `array<var>/<string> name = "s"` ≡ `= {"s"}` — a string is a
                // valid var/string element (mirrors `array<int> x = 3` ≡ {3}). Wrap the literal in a
                // one-element list so it flows through the normal element emit (e.g. an additive
                // property's `with describe "s"`). Only array<char> fills a char buffer from a string.
                token strTok = file.getToken();
                initializerList* list = new initializerList();
                list->elements.push_back(parseExpression(strTok, {token::endStatement}, nullptr, nullptr));
                if(file.peekToken().is(token::endStatement)) file.getToken();
                arrDecl.declaredExpressionValue = list;
            } else {
                parsingError(format("String initializer is only valid for array<char>; '{0}' cannot "
                    "hold a string element.", typeDisplayName(elemType)));
            }
        } else if(!file.peekToken().is(token::braceOpen)){
            // One-element shorthand: `array<int> nums = 3;` ≡ `= {3}`.
            expression* expr = parseExpression(file.getToken(), {token::endStatement}, nullptr, nullptr);
            initializerList* list = asSingleElementList(expr, (declIsRaw ? "rawarray<" : "array<") + elemType + ">");
            if(list == nullptr)
                parsingError(format("Cannot initialize member array '{0}' of {1} with a value of type '{2}'; "
                    "use a list: {{ … }}", arrDecl.dName(), typeDisplayName(elemType), typeDisplayName(expr->resolvedType)));
            arrDecl.declaredExpressionValue = list;
        } else if(isArrayOfArraysElement(elemType) || isInlineConstructible(getDispatchClass(elemType))){
            // Rows written in place (`{ {1, 2}, {3} }`) bake as anonymous global arrays; an element of a
            // class written as `{ … }` is that class's inline object.
            file.getToken(token::braceOpen);
            arrDecl.declaredExpressionValue = parseArrayInitializerList(elemType, nullptr, nullptr);
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
    if(arrDecl.arraySize > 0)
        if(auto* seed = dynamic_cast<initializerList*>(arrDecl.declaredExpressionValue);
           seed != nullptr && (int)seed->elements.size() > arrDecl.arraySize)
            parsingError(format("Array '{0}' declares capacity {1} but its initializer supplies {2} elements.",
                                arrDecl.dName(), arrDecl.arraySize, seed->elements.size()));
    if(auto* seed = dynamic_cast<initializerList*>(arrDecl.declaredExpressionValue);
       seed != nullptr && seed->elements.empty() && arrDecl.arraySize <= 0)
        parsingError(format("Array '{0}' has no storage: give it a capacity ([n]) or at least one element; declare it 'ref' to share another array.", arrDecl.dName()));
    // Set byte array type for char arrays
    if(elemType == "char"){
        arrDecl.isByteArray = true;
        arrDecl.type = languageService.getType("bytearray");
    }
    // A `static` array belongs to the class: one global array, `_bgl_<Class>_<name>`, as a static scalar is.
    if(q && q->isStatic){
        auto* owner = dynamic_cast<classDef*>(ctx);
        if(owner == nullptr) parsingError("'static' is not valid inside an object body");
        arrDecl.isStatic = true;
        // There is no `self` for a static array's values: a bare static method is its routine.
        if(auto* list = dynamic_cast<initializerList*>(arrDecl.declaredExpressionValue))
            for(expression* e : list->elements){
                string t = e != nullptr ? e->text() : "";
                if(t.rfind("self.", 0) != 0) continue;
                string mn = t.substr(5);
                if(auto* sm = dynamic_cast<functionDef*>(findMemberInHierarchy(owner, [&](typeMember* m){
                        auto* f = dynamic_cast<functionDef*>(m); return f && f->isStatic && !f->isEmitter && f->name == mn; })))
                    e->tokens.assign(1, i6Emitter::staticRoutineName(owner->declaringClass(sm) ? owner->declaringClass(sm) : owner, sm));
            }
        auto* g = new arrayDeclaration(arrDecl);
        g->name = "_bgl_" + owner->name + "_" + arrDecl.name;
        g->displayName = g->name;
        g->i6name.clear();
        g->isInline = false;
        languageService.registerInstance(*g);
        noteValueElementArray(*g, nullptr, false);
    } else
    if(!arrDecl.isRefLocal) noteValueElementArray(arrDecl, dynamic_cast<classDef*>(ctx), false);
    promoteMemberArray(arrDecl);
    warnMemberHidesDefine(arrDecl.name, arrDecl.dName());
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


void bglParser::consumeMethodI6Alias(functionDef& funcDef){
    // An override is the same I6 property as the method it overrides, so it takes that method's
    // I6 name; otherwise a call, emitted under the inherited name, would always run the base's.
    string inheritedI6;
    {
        vector<classDef*> bases;
        if(currentClass != nullptr) bases = currentClass->baseClasses;
        else if(currentObject != nullptr && currentObject->objectClass != nullptr) bases = {currentObject->objectClass};
        for(classDef* b : bases)
            if(typeMember* m = findMemberInHierarchy(b, [&](typeMember* x){
                    auto* fd = dynamic_cast<functionDef*>(x);
                    return fd != nullptr && fd->name == funcDef.name && !fd->isEmitter && !fd->isStatic && !fd->i6name.empty()
                        && sameParamTypes(fd, &funcDef);   // the overload it overrides, not another of the set
                })){ inheritedI6 = m->i6name; break; }
    }
    if(!inheritedI6.empty() && !funcDef.isEmitter && !funcDef.isStatic){
        if(file.peekToken().is("asi6")){
            file.getToken();
            token aliasTok = file.getToken(eTokenType::identifier);
            string mine = aliasTok.originalValue.empty() ? aliasTok.value : aliasTok.originalValue;
            string a = mine, b = inheritedI6;
            transform(a.begin(), a.end(), a.begin(), ::tolower); transform(b.begin(), b.end(), b.begin(), ::tolower);
            if(a != b)
                parsingError(format("'{0}' overrides a method emitted as '{1}', so it is emitted as '{1}' too; "
                                    "drop its `asI6 {2}`.", funcDef.dName(), inheritedI6, mine));
        }
        funcDef.i6name = inheritedI6;
        return;
    }
    if(!file.peekToken().is("asi6")){
        // Renamed like a data member when the name is an I6 operator word, a global routine, or a library
        // global or object the binding declares (`Action` and parser.h's `action`). A library property keeps it.
        // The library's own methods keep their names: a program routine named like one yields instead.
        bool libraryMethod = funcDef.src.file.find("beguiLib") != string::npos;
        if(!funcDef.isEmitter && !funcDef.isStatic
           && (isI6OperatorWord(funcDef.name) || isI6SystemFunction(funcDef.name) || namesI6Class(funcDef.name)
               || (!libraryMethod && namesLibraryGlobal(funcDef.name))))
            funcDef.i6name = "_m_" + funcDef.dName();
        return;
    }
    file.getToken();                                   // 'as'
    token aliasTok = file.getToken(eTokenType::identifier);
    // §3.11: the clause is ignored on operator methods, whose i6name is assigned by the
    // operator/overload mangler — a manual alias there would fight it. Still consumed, so the
    // declaration parses either way. An operator is stored under its bare symbol (`==`, not
    // `operator==`), so test the same shape the mangler does rather than a name prefix.
    bool isOperatorMethod = !funcDef.name.empty()
        && (funcDef.name == "operator()" || funcDef.name.rfind("operator", 0) == 0
            || (!isalpha((unsigned char)funcDef.name[0]) && funcDef.name[0] != '_'));
    if(!isOperatorMethod)
        funcDef.i6name = aliasTok.originalValue.empty() ? aliasTok.value : aliasTok.originalValue;
}

// `string Name()`: a method under an additive property that holds an object's words (`name`) would be one
// more entry in that list of words.
void bglParser::rejectMethodOnWordProperty(token name, const string& owner, const string& i6alias){
    if(name.src.file.find("beguiLib") != string::npos) return;
    string prop = i6alias.empty() ? (string)name : i6alias;
    transform(prop.begin(), prop.end(), prop.begin(), ::tolower);
    if(!languageService.isAdditiveProperty(prop)) return;
    for(typeDef* g : languageService.objectTypes)
        if(auto* cd = dynamic_cast<classDef*>(g))
            for(typeMember* m : cd->members)
                if(auto* ad = dynamic_cast<arrayDeclaration*>(m); ad && ad->elementType == "dictionaryword"
                   && (ad->i6name.empty() ? ad->name : ad->i6name) == prop)
                    parsingError(format("'{0}.{1}()' would join the ADDITIVE property '{2}', which holds an object's words; "
                        "give the method another name.", owner, name.originalValue.empty() ? (string)name : name.originalValue, prop));
}

void bglParser::processMemberMethod(objectDef& obj, token returnType, token name, bool isReplace, string i6alias){
    rejectMethodOnWordProperty(name, obj.dName(), i6alias);
    // Inside the object a bare name is either the type or the routine; it can't be both.
    for(typeMember* m : obj.members)
        if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd && vd->isAlias && !vd->isPrePassStub && vd->name == (string)name)
            parsingError(format("'{0}': method '{1}' has the name of the object's type alias '{2}'; rename one.",
                                obj.dName(), name.originalValue.empty() ? (string)name : name.originalValue, vd->dName()));
    functionDef& funcDef = *(new functionDef());
    funcDef.name = (string)name;
    funcDef.displayName = name.originalValue;
    // `Type method(...) as <i6name>` — emit the routine under a chosen I6 name. Skip operators,
    // whose i6name is assigned by the operator/overload mangler; a manual alias there would fight it.
    if(!i6alias.empty() && name.value.rfind("operator", 0) != 0)
        funcDef.i6name = i6alias;
    funcDef.src = name.src.line > 0 ? name.src : file.currentLocation();
    funcDef.returnType = languageService.getType((string)returnType);
    if(!returnType.docComment.empty())   funcDef.docComment = returnType.docComment;
    else if(!name.docComment.empty())    funcDef.docComment = name.docComment;
    processParameterList(funcDef);
    consumeMethodI6Alias(funcDef);      // `Type method(...) as <i6name> { … }` (§3.11)
    synthesizeParamBackings(funcDef, obj.dName());   // a value-class parameter is copied in, as in a class method
    // An operator (`==`, `[]`) emits under its mangled name, as a class's does.
    if(funcDef.i6name.empty() && !funcDef.name.empty() && !isalpha((unsigned char)funcDef.name[0]) && funcDef.name[0] != '_')
        funcDef.i6name = mangleOperatorName(funcDef.name);
    file.getToken(token::braceOpen);
    funcDef.body = new statementBlock();
    functionDef* savedFunc = currentFunc;
    currentFunc = &funcDef;
    openCompileContext(eCompileContext::codeBlock, dynamic_cast<statementBlock*>(funcDef.body));
    while(processNextStatement(funcDef) == false){}
    closeCompileContext(eCompileContext::codeBlock);
    currentFunc = savedFunc;
    if(funcDef.returnType.name != "void" && !allPathsReturn(dynamic_cast<statementBlock*>(funcDef.body)))
        parsingError(missingReturnMessage(funcDef));
    // Check class hierarchy for shadowed methods — warn if 'replace' not specified.
    // A "shadow" requires matching signature (arity + param types). Same name but
    // different arity or param types is an overload, not a shadow — no warning.
    string shadowedFrom;   // the class whose method this one overrides, if any
    if(!funcDef.name.empty()){
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
                        shadowedFrom = c->dName(); shadowedIsDefault = fd->isDefault || fd->isRequired; return;
                    }
            for(classDef* base : c->baseClasses) searchClass(base);
        };
        // Search objectClass hierarchy
        if(obj.objectClass) searchClass(obj.objectClass);
        // Search objectDef base classes (from ': Parent' clause)
        for(typeDef* base : obj.baseClasses)
            if(auto* cd = dynamic_cast<classDef*>(base)) searchClass(cd);
        // An additive property's methods all contribute; there is no replacing an ancestor's. An object may
        // still replace its own earlier contribution (`extend lamp { replace bool after() … }`).
        bool additive = languageService.isAdditiveProperty(funcDef.i6name.empty() ? funcDef.name : funcDef.i6name);
        bool ownEarlier = false;
        for(typeMember* m : obj.members)
            if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd != &funcDef && fd->name == funcDef.name && !fd->isPrePassStub) ownEarlier = true;
        if(additive && isReplace && !ownEarlier)
            parsingError(format("'{0}' is additive: every class's and object's contribution is kept, so it can't be replaced.",
                                funcDef.dName()));
        if(!additive && !isReplace && !shadowedFrom.empty() && !shadowedIsDefault)
            parsingWarning(format("object '{0}': method '{1}' shadows definition in class '{2}'; use 'replace' to suppress this warning",
                obj.dName(), funcDef.dName(), shadowedFrom));
    }
    // An `extend … replace` above the object: the object's own declaration, still to come, is what it replaces.
    if(isReplace && obj.isPrePassStub && !funcDef.isEmitter) funcDef.isForwardReplace = true;
    // A new signature beside its class's method of the name is an overload: a property of its own, so the
    // class's method stays the object's too.
    if(!funcDef.isEmitter && funcDef.i6name.empty() && obj.objectClass != nullptr && isalpha((unsigned char)(funcDef.name.empty() ? '_' : funcDef.name[0]))){
        bool classHasName = false;
        functionDef* sameSig = nullptr;
        mangleOverloadSetForReceiver(obj.objectClass->name, funcDef.name);
        for(classDef* c = obj.objectClass; c != nullptr; c = c->baseClasses.empty() ? nullptr : c->baseClasses[0])
            for(typeMember* m : c->members)
                if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->name == funcDef.name && !fd->isEmitter){
                    classHasName = true;
                    if(sameSig == nullptr && sameParamTypes(fd, &funcDef)) sameSig = fd;
                }
        // The same signature overrides the class's method under its routine name; a new one is an overload of its own.
        if(sameSig != nullptr){ if(!sameSig->i6name.empty()) funcDef.i6name = sameSig->i6name; }
        else if(classHasName) funcDef.i6name = mangleObjectMethodName(&funcDef);
    }
    if(!replaceStubMember(obj.members, funcDef)){
        // Signature-aware duplicate check: same name + same arity + same param types.
        // Same-arity-different-types is an overload, not a duplicate (mirrors the class-
        // body rule). The emitter mangles overload sets so each emits as a distinct I6
        // property.
        rejectValueWordName(funcDef.name);
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
                        keepReplaced(funcDef, fd);
                        // An `extend … replace` above the object: the object's own declaration, still to
                        // come, is what it replaces.
                        if(fd->isPrePassStub && obj.isPrePassStub) funcDef.isForwardReplace = true;
                        obj.members[i] = &funcDef;
                        replaced = true;
                        break;
                    }
            if(!replaced && shadowedFrom.empty())
                parsingWarning(format("object '{0}': 'replace' specified but no existing method '{1}' to replace", obj.dName(), funcDef.dName()));
        }
        if(!replaced){
            for(typeMember* m : obj.members){
                if(auto* fd = dynamic_cast<functionDef*>(m); fd && sigEq(fd) && fd->isForwardReplace && !fd->isEmitter){
                    keepReplaced(*fd, &funcDef);   // the object's own method is the one replaced
                    fd->isForwardReplace = false;
                    return;
                }
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(sigEq(fd))
                        parsingError(format("object '{0}': method '{1}' with the same signature is already defined (originally at {2}:{3}); use 'replace' to override",
                            obj.dName(), funcDef.dName(), fd->src.file, fd->src.line));
                // A value given to the same member: one property can't be both.
                if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd && vd->name == funcDef.name && !vd->isPrePassStub && !vd->isAlias)
                    parsingError(format("object '{0}': member '{1}' is already defined", obj.dName(), funcDef.dName()));
            }
        }
        if(!replaced)
            obj.members.push_back((typeMember*)&funcDef);
    }
}


void bglParser::processMemberVariable(objectDef& obj, string typeName, string name, bool hasValue, bool isReplace, string i6alias, bool isRef, string docComment){
    variableDeclaration& prop = *(new variableDeclaration());
    prop.name = name;
    prop.docComment = docComment;
    if(!typeName.empty()) warnMemberHidesDefine(name, name);
    // An inherited member keeps the I6 name its class gave it (an `asI6` name, or a clash rename).
    if(i6alias.empty() && obj.objectClass != nullptr)
        if(string inherited = memberI6Name(obj.objectClass->name, name); inherited != name) i6alias = inherited;
    prop.i6name = memberI6NameFor(prop.name, i6alias, obj.isExternal);
    prop.src = file.currentLocation();
    prop.type = languageService.getType(typeName);
    if(typeName.rfind("func<", 0) == 0 || isUnionType(typeName)) prop.type.name = typeName;  // getType returns base "func"; keep the parameterized name
    prop.isRefLocal = isRef;  // `ref` member: assignments are pointer-copy (opt out of operator= dispatch)
    if(obj.isExternal && !isRef && !typeName.empty() && !isWordArrayType(typeName)) rejectPlainExternMember(typeName, prop.dName());
    if(objectMemberIsConst){ prop.isConst = true; objectMemberIsConst = false; }
    if(isRef){
        string why = refNotApplicable(getDispatchClass(typeName), typeDisplayName(typeName));
        if(!why.empty()) parsingError("'ref': " + why);
    }
    // Type-sealed inherited member: if a base class marked this member `typesealed`, the slot type is
    // locked — a re-typed declaration keeps the sealed type (and warns). This lets `object parent =
    // <room>` initialize the inherited `parentProp parent` (so `self.parent = X` later dispatches to
    // parentProp's `move`) instead of shadowing it to type `object`.
    if(obj.objectClass){
        typeMember* inh = findMemberInHierarchy(obj.objectClass, [&](typeMember* m){
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            return vd && vd->name == name && vd->isTypeSealed;
        });
        if(auto* inhVd = inh ? dynamic_cast<variableDeclaration*>(inh) : nullptr){
            if(prop.type.name != inhVd->type.name){
                if(!sealedSlotTakes(inhVd->type.name, typeName))
                parsingWarning(format("You've redefined '{0}' (which is typesealed as '{1}') to '{2}'. The retype will be ignored and stay '{1}'.",
                    name, typeDisplayName(inhVd->type.name), typeDisplayName(typeName)));
                // Lock the SLOT to the sealed type (so `.parent = X` dispatches to its operator=), but
                // keep validating the initializer against the WRITTEN type below — the sealed pseudo-type
                // (parentProp) has no direct value compatibility, whereas the written type (object) does.
                prop.type = inhVd->type;
                prop.isTypeSealed = true;
            }
        }
    }
    // A value given here for an inherited `const` member is its one assignment: the member stays const.
    if(obj.objectClass)
        if(auto* inh = dynamic_cast<variableDeclaration*>(findMemberInHierarchy(obj.objectClass, [&](typeMember* m){
               auto* vd = dynamic_cast<variableDeclaration*>(m);
               return vd && vd->name == name; })); inh && inh->isConst)
            prop.isConst = true;
    if(hasValue) parsePropertyValue(prop, typeName);
    variableDeclaration* member = &prop;
    if(obj.objectClass)
        if(variableDeclaration* arr = asInheritedArrayOverride(prop, {obj.objectClass})) member = arr;
    bool replaced = false;
    if(isReplace){
        for(size_t i = 0; i < obj.members.size(); i++)
            if(auto* vd = dynamic_cast<variableDeclaration*>(obj.members[i]))
                if(vd->name == prop.name){
                    if(vd->isPrePassStub && obj.isPrePassStub) member->isForwardReplace = true;   // the object's own is still to come
                    obj.members[i] = member; replaced = true; break;
                }
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
                        obj.members[i] = member;
                        replaced = true;
                        break;
                    }
                }
                if(auto* existingFd = dynamic_cast<functionDef*>(obj.members[i]); existingFd && existingFd->isPrePassStub){
                    obj.members[i] = member;
                    replaced = true;
                    break;
                }
                // An `extend … replace` above the object already gave this member.
                if(obj.members[i]->isForwardReplace){ obj.members[i]->isForwardReplace = false; replaced = true; break; }
                parsingError(format("object '{0}': member '{1}' is already defined", obj.dName(), prop.dName()));
            }
        }
        if(!replaced) obj.members.push_back((typeMember*)member);
    }
}


void bglParser::processTypedMember(objectDef& obj, token typeTok, bool isReplace, bool isRef){
    // func<...> member: consume its own <...> so it isn't mistaken for the member name.
    if(typeTok.value == "func") typeTok.value = parseFuncType();
    else if((typeTok.value == "array" || typeTok.value == "rawarray") && file.peekToken().is("<"))
        typeTok.value = parseArrayTypeTail(typeTok.value);   // `array<int> | string note`
    typeTok.value = maybeParseUnionTail(typeTok.value);  // A | B | ... union member type
    // A member name may collide with a TYPE name — Beguile is case-insensitive, so a user class
    // `A` makes the bare word `a` lex as a dataType. Accept both, as the class-member path does;
    // otherwise a library member simply named `a` is undeclarable by any program that happens to
    // define a class of that letter.
    token propName = file.getToken({eTokenType::identifier, eTokenType::dataType});
    if(propName.is("operator")){
        // Named as a class's operators are (`==`, `[]`, `[]=`), so the operator lookup finds them.
        token opTok = file.getToken();
        if(opTok.is(token::parenOpen))
            propName.value = "operator()";
        else if(opTok.is(token::bracketOpen)){
            file.getToken(token::bracketClose);
            propName.value = "[]";
            if(file.peekToken().is(token::assignment)){ file.getToken(); propName.value = "[]="; }
        } else if(opTok.is("?"))
            propName.value = "?";
        else {
            opTok.assert(eTokenType::oper);
            propName.value = opTok.value;
        }
        propName.originalValue = propName.value;
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

        // `+=` / `-=` are extend-body operators, not declaration forms — give a clear message
        // instead of the generic "expected '='".
        { token opPeek = file.peekToken();
          if(opPeek.is("+=") || opPeek.is("-="))
              parsingError(format("`{0} {1}` is only valid inside an `extend` body. A declaration establishes the base grammar with `{0} = {{ ... }}`; use `extend` to append (`+=`), remove (`-=`), or `replace {0} = …`.", propName.value, opPeek.value)); }
        file.getToken(token::assignment);
        grammarRuleListDecl& gtd = *(new grammarRuleListDecl());
        gtd.name = propName.value;
        gtd.src = propName.src;
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
    // Optional `as <i6name>` I6-alias, positioned after the member name (mirrors the top-level
    // `Type name as alias …` order in processTypedObjectDeclaration/processAliasedDeclaration). Lets a
    // member's emitted I6 identifier differ from its Beguile name so it can dodge an I6 symbol clash
    // (e.g. `auto util = _bglUtil as bglUtil` avoids orLibrary's `object util`).
    string i6alias;
    if(file.peekToken().is("asi6")){
        file.getToken();  // consume 'as'
        token aliasTok = file.getToken(eTokenType::identifier);
        i6alias = aliasTok.originalValue.empty() ? aliasTok.value : aliasTok.originalValue;
    }
    token sym = file.getToken({token::assignment, token::endStatement, token::parenOpen, token::bindAssignment});
    if(sym.is(token::parenOpen))
        processMemberMethod(obj, typeTok, propName, isReplace, i6alias);
    else {
        rejectMemberBindingMismatch(isRef, sym, typeTok, propName);
        processMemberVariable(obj, typeTok.value, propName.value, sym.is(token::assignment) || sym.is(token::bindAssignment),
                              isReplace, i6alias, isRef, typeTok.docComment.empty() ? propName.docComment : typeTok.docComment);
    }
}


// A member of an extern class or object is a property the library declared: one of a class type refers to
// an instance and may be empty, so it is declared `ref`.
void bglParser::rejectPlainExternMember(const string& typeName, const string& name){
    if(!plainSlotOwnsReferenceInstance(getDispatchClass(typeName))) return;
    parsingError(format("'{1}' is the library's property, which refers to an instance or is empty: declare it `ref {0} {1}`.",
                        typeDisplayName(typeName), name));
}

// A `ref` member's declaration binds with `:=`; any other member's value is given with `=` (§3.7).
void bglParser::rejectMemberBindingMismatch(bool isRef, token sym, const token& typeTok, const token& nameTok){
    string type = typeTok.originalValue.empty() ? typeTok.value : typeTok.originalValue;
    string name = nameTok.originalValue.empty() ? nameTok.value : nameTok.originalValue;
    if(isRef && sym.is(token::assignment))
        parsingError(format("'ref {0} {1} = …': a 'ref' declaration binds with ':='. Write 'ref {0} {1} := …'.", type, name));
    if(!isRef && sym.is(token::bindAssignment))
        parsingError(format("'{0} {1} := …': ':=' binds a 'ref'. Write 'ref {0} {1} := …' to bind, or '=' to give a value.", type, name));
}

void bglParser::processInheritedMember(objectDef& obj, token nameTok){
    // World-model child placement: `children = { a, b, c }` lists objects to move INTO this object.
    // It's the inverse of `parent`: instead of naming this object's container, it names its contents.
    // Parse the object list here (the inherited `children` accessor isn't a stored property, so the
    // generic path would reject it) and stash it on the objectDef; a pre-emit pass desugars each entry
    // to a `parent` set on the listed object, reusing the compile-time positional-parent tree.
    if(nameTok.value == "children"){
        file.getToken(token::assignment);
        file.getToken(token::braceOpen);
        if(!file.peekToken().is(token::braceClose)){
            token t = file.getToken();
            while(true){
                expression* elem = parseExpression(t, {token::comma, token::braceClose}, nullptr, nullptr);
                if(elem->resolvedType.empty())
                    parsingError(format("children placement in '{0}': element is not a declared object", obj.dName()));
                obj.childrenPlacement.push_back(elem);
                if(elem->terminator == token::braceClose) break;
                t = file.getToken();
                if(t.is(token::braceClose)) break;   // trailing comma
            }
        } else file.getToken(token::braceClose);
        if(file.peekToken().is(token::endStatement)) file.getToken();
        return;
    }
    string propTypeName;
    // Search the object's declared class first (if any), then fall back to the base 'object' class.
    // For arrayDeclaration members, reconstruct the full templated typeName (e.g. `array<dictionaryWord>`)
    // so parsePropertyValue can extract the element type — otherwise `vd->type.name` returns just `array`
    // and parsePropertyValue's fallback misidentifies the element type as int (from operator[]'s index param).
    auto searchClass = [&](classDef* cls){
        if(!cls || !propTypeName.empty()) return;
        typeMember* m = cls->findMember([&](typeMember* mm){
            auto* vd = dynamic_cast<variableDeclaration*>(mm);
            return vd != nullptr && vd->name == nameTok.value;
        });
        if(m == nullptr) return;
        if(auto* arr = dynamic_cast<arrayDeclaration*>(m))
            propTypeName = "array<" + arr->elementType + ">";
        else
            propTypeName = dynamic_cast<variableDeclaration*>(m)->type.name;
    };
    searchClass(obj.objectClass != nullptr ? obj.objectClass : languageService.findClass("_bglobject"));
    if(propTypeName.empty()){
        // Suggest a type from the value about to be assigned.
        token v = file.peekToken(1).is(token::assignment) ? file.peekToken(2) : file.peekToken(1);
        string guess = v.is(eTokenType::integer) ? "int" : v.is(eTokenType::quote) ? "string"
                     : v.is(eTokenType::dictionaryWord) ? "dictionaryWord" : v.is(eTokenType::charLiteral) ? "char" : "object";
        string shown = nameTok.originalValue.empty() ? nameTok.value : nameTok.originalValue;
        bool freeStanding = false;
        if(auto* g = languageService.findGlobalAs<variableDeclaration>(nameTok.value)) freeStanding = g->type.name == "property";
        parsingError(format("'{0}' is not a member of this object's class or its bases; declare it with its type "
                            "(e.g. '{1} {0} = …;'){2}", shown, guess,
                            freeStanding ? ". A free-standing `property` declaration names the property for passing as a "
                                           "value; it does not give objects the member" : ""));
    }
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

        // `+=` / `-=` are extend-body operators, not declaration forms — clear message here too.
        { token opPeek = file.peekToken();
          if(opPeek.is("+=") || opPeek.is("-="))
              parsingError(format("`{0} {1}` is only valid inside an `extend` body. A declaration establishes the base grammar with `{0} = {{ ... }}`; use `extend` to append (`+=`), remove (`-=`), or `replace {0} = …`.", nameTok.value, opPeek.value)); }
        file.getToken(token::assignment);
        grammarRuleListDecl& gtd = *(new grammarRuleListDecl());
        gtd.name = nameTok.value;
        gtd.src = nameTok.src;
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
    processMemberVariable(obj, propTypeName, nameTok.value, hasValue, false, "", false, nameTok.docComment);
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
    string elemType = readArrayElementType();
    file.getToken(">");
    // Build the full generic type token (e.g. "array<int>" or "rawarray<int>") so downstream sees it.
    // arrayTok is the base-name token ("array" or "rawarray"), routed here by the grammar table.
    token typeTok = arrayTok;
    typeTok.value = arrayTok.value + "<" + elemType + ">";
    token name = file.getToken({eTokenType::identifier, eTokenType::dataType});
    token symbol = file.getToken({token::bracketOpen, token::assignment, token::endStatement, token::parenOpen, token::bindAssignment});
    if(symbol.is(token::parenOpen))
        return processRoutineDeclaration(typeTok, name, ctx, q.isExtern, q.isEmitter, q.isReplace, q.isDefault, q.isSuperposed);
    // `ref array<T> r := a;` is the declaration, then the bind.
    if(symbol.is(token::bindAssignment)){
        auto* fn = dynamic_cast<functionDef*>(&ctx);
        if(!q.isRef || fn == nullptr)
            parsingError("':=' binds a 'ref' local; declare it 'ref array<…>' inside a routine.");
        sourceLocation at = file.currentLocation();
        string value = file.getRawTextToStatementEnd();
        token end = symbol; end.value = token::endStatement; end.tokenType = eTokenType::symbol;
        declaringRefArray = true;   // it refers to another array's slots: no capacity, no instances of its own
        try { processArrayDeclaration(arrayTok, name, elemType, end, ctx, q.isExtern, q.isSuperposed, q.isConst); }
        catch(...) { declaringRefArray = false; throw; }
        declaringRefArray = false;
        file.openText(name.value + " := " + value + ";", at.file, at.line);
        try { processStatementDispatch(file.getToken(), *fn); } catch(...) { file.close(); throw; }
        file.close();
        return false;
    }
    if(symbol.is(token::endStatement) && !q.isExtern && !q.isRef)
        parsingError(format("Array '{0}' needs a capacity ([n]) or an initializer; declare it 'ref' to share another array.",
                            name.originalValue.empty() ? name.value : name.originalValue));
    processArrayDeclaration(arrayTok, name, elemType, symbol, ctx, q.isExtern, q.isSuperposed, q.isConst);
    return false;
}


// Records, in the main pass, that `name` is positional in `cls`; false if it already was. (The
// pre-scan fills inlineOrder too, so the order list can't answer this.)
bool bglParser::notePositional(classDef& cls, const string& name){
    return positionalNoted[&cls].insert(name).second;
}

void bglParser::addInlineOrder(classDef& cls, const string& name){
    if(find(cls.inlineOrder.begin(), cls.inlineOrder.end(), name) == cls.inlineOrder.end())
        cls.inlineOrder.push_back(name);
}

// The positional slots of `cls`, in order: the class's own list (its `inline` members and the
// inherited ones it re-lists), then each base's, SUBCLASS FIRST. A member appears once, where it is
// first listed, so a subclass that re-lists an inherited member decides its position.
vector<variableDeclaration*> bglParser::positionalMembers(classDef* cls){
    vector<variableDeclaration*> out;
    set<string> placed;
    set<variableDeclaration*> fromRoot;
    function<void(classDef*)> walk = [&](classDef* c){
        if(c == nullptr) return;
        for(const string& n : c->inlineOrder){
            if(placed.count(n)) continue;
            auto* vd = dynamic_cast<variableDeclaration*>(c->findMember([&](typeMember* m){
                return dynamic_cast<variableDeclaration*>(m) != nullptr && m->name == n; }));
            if(vd != nullptr){ placed.insert(n); out.push_back(vd); if(c->baseClasses.empty()) fromRoot.insert(vd); }
        }
        for(classDef* base : c->baseClasses) walk(base);
    };
    walk(cls);
    // The root's name slot is always last, however many parents put their slots ahead of it; a class
    // that places the name itself keeps it where it put it.
    stable_partition(out.begin(), out.end(), [&](variableDeclaration* vd){
        return !(vd->name == "instancename" && fromRoot.count(vd)); });
    return out;
}

// One member value of an aggregate (positional or named), stored on `od` as the value of `target`.
// On entry `vt` is the value's first token; returns the separator that ended it (',', ';' or '}').
// True when an object body opens with positional values (`Pet rex { "rex", 4; … }`): a value, not a
// member. Nothing that begins a member — a type, a name followed by '=', a qualifier, a directive —
// looks like this, so the two can't be confused.
bool bglParser::startsPositionalSection(token tok){
    switch(tok.tokenType){
        case eTokenType::quote: case eTokenType::rawQuote: case eTokenType::integer: case eTokenType::floatLiteral:
        case eTokenType::dictionaryWord: case eTokenType::charLiteral:
            return true;
        default: break;
    }
    if(tok.is(token::braceOpen) || tok.is("-") || tok.is(token::parenOpen)) return true;
    if(tok.is(eTokenType::identifier) || tok.is(eTokenType::dataType)){
        token next = file.peekToken();
        if(next.is(token::braceOpen) && tok.isDataType()) return true;   // an inline value, `Pt{ 2, 1 }`
        if((next.is(token::period) || next.is(eTokenType::dictionaryWord)) && languageService.findEnum(tok.value) != nullptr)
            return true;   // `eGrade.high`
        if(next.is(token::period)){   // a dotted value (`ns.edge.bottom`): a path that ends the value
            int k = 1;
            while(file.peekToken(k).is(token::period) && file.peekToken(k + 1).is(eTokenType::identifier)) k += 2;
            token end = file.peekToken(k);
            return end.is(",") || end.is(token::endStatement) || end.is(token::braceClose);
        }
        return next.is(",") || next.is(token::endStatement) || next.is(token::braceClose);
    }
    return false;
}

// The positional section of an object body: ','-separated values filling the class's positional
// members in order (positionalMembers), ended by the first ';'. Returns the token after it — the
// first member of the named section, or the body's '}'.
token bglParser::parseDeclarationPositionals(objectDef& obj, token tok){
    // The object's class: a verb's name dispatches as a verb, but its slots are its declared class's.
    classDef* cls = obj.objectClass != nullptr ? obj.objectClass : getDispatchClass(obj.name);
    vector<variableDeclaration*> slots = positionalMembers(cls);
    string display = cls != nullptr ? cls->dName() : obj.dName();
    size_t idx = 0;
    while(true){
        if(idx >= slots.size())
            parsingError(format("'{0}': too many positional values — '{1}' has {2} positional member(s) (declared `inline`)",
                                obj.dName(), display, (int)slots.size()));
        string terminator = bakeMemberValue(obj, slots[idx++], tok, display, nullptr, nullptr);
        if(terminator == token::braceClose){ token close; close.tokenType = eTokenType::symbol; close.value = token::braceClose; return close; }
        if(terminator == token::endStatement) return file.getToken();
        tok = file.getToken();
    }
}

string bglParser::bakeMemberValue(objectDef& od, variableDeclaration* target, token vt, const string& typeDisplay,
                                  functionDef* func, statementBlock* body){
    // The value is either a scalar expression or a NESTED `{ … }` aggregate. A nested aggregate
    // takes its shape from the target member's type: an `array<T>` member → a braced array literal
    // (baked as an array property), an object-backed member → a nested inline object (§6.2.1).
    string terminator;
    // A value class with positional members takes a bare `{ … }` as its own `Type{ … }`.
    if(vt.is(token::braceOpen) && dynamic_cast<arrayDeclaration*>(target) == nullptr)
        if(classDef* vc = getDispatchClass(target->type.name); vc != nullptr && !isObjectBackedClass(vc) && isInlineConstructible(vc)){
            file.pendingToken = vt;
            file.hasPendingToken = true;
            vt.value = vc->name;
            vt.originalValue = vc->dName();
            vt.tokenType = eTokenType::dataType;
        }
    if(vt.is(token::braceOpen)){
        if(arrayDeclaration* arrTarget = dynamic_cast<arrayDeclaration*>(target)){
            // array-typed member: parse `{ v1, v2, … }` as an array literal → array property.
            arrayDeclaration& amem = *(new arrayDeclaration());
            amem.name = target->name;
            amem.type = languageService.getType("array");
            amem.elementType = arrTarget->elementType;
            initializerList* list = new initializerList();
            token t = file.getToken();
            while(!t.is(token::braceClose) && !t.is(eTokenType::eof)){
                expression* e = parseExpression(t, {",", token::braceClose}, func, body);
                list->elements.push_back(e);
                if(e->terminator == token::braceClose) break;
                t = file.getToken();
            }
            amem.declaredExpressionValue = list;
            od.members.push_back(&amem);
        } else {
            classDef* fcls = getDispatchClass(target->type.name);
            if(fcls == nullptr || !isObjectBackedClass(fcls))
                parsingError(format("inline '{0}{{...}}': member '{1}' is neither an array nor an object-backed type, so it cannot take a '{{ … }}' value",
                                    typeDisplay, target->name));
            // object-typed member: a nested inline object (the '{' is already consumed as `vt`).
            string anonName = format("_bglanon{0}", anonObjectCounter++);
            bakeInlineObjectAggregate(fcls, target->type.name, anonName, func, body);
            variableDeclaration& mem = *(new variableDeclaration());
            mem.name = target->name;
            mem.i6name = target->i6name.empty() && target->isPrePassStub ? clashRenamedMember(target->name) : target->i6name;   // a class below renames it
            mem.type = target->type;
            expression* ref = new expression();
            ref->tokens.push_back(anonName);
            ref->resolvedType = target->type.name;
            mem.declaredExpressionValue = ref;
            od.members.push_back(&mem);
        }
        token sep = file.getToken({",", token::endStatement, token::braceClose});   // outer separator after '}'
        terminator = sep.value;
    } else {
        string savedExpected = currentExpectedType;
        currentExpectedType = target->type.name;
        expression* val = parseExpression(vt, {",", token::endStatement, token::braceClose}, func, body);
        currentExpectedType = savedExpected;
        applyImplicitConversion(val, target->type.name);
        if(func == nullptr) rejectValueClassInitializer(val, target->type.name, target->dName());
        // A primitive class holds its value directly, so a value its `operator =` takes is stored as is.
        auto primitiveAccepts = [&](const string& memberType, const string& valueType){
            classDef* mc = getDispatchClass(memberType);
            if(mc == nullptr || !mc->isPrimitive) return false;
            // The class's own overloads only: the root's `operator = (_bglPrimitive)` takes any primitive.
            for(typeMember* m : mc->members)
                if(auto* fn = dynamic_cast<functionDef*>(m))
                    if(fn->name == "=" && !fn->isStatic && fn->params.size() == 1
                       && fn->params[0]->type.name != "var" && isTypeCompatible(valueType, fn->params[0]->type.name))
                        return true;
            return false;
        };
        if(target->name == "instancename" && val->resolvedType != "stringliteral")
            parsingError("'instanceName' takes a string literal: the name is fixed when the program is compiled.");
        else if(!target->type.name.empty() && target->type.name != "var" && !val->resolvedType.empty()
                && !isTypeCompatible(val->resolvedType, target->type.name)
                && !primitiveAccepts(target->type.name, val->resolvedType))
            parsingError(format("'{0}': member '{1}' is '{2}', so it can't take a value of type '{3}'",
                                typeDisplay, target->dName(), typeDisplayName(target->type.name), typeDisplayName(val->resolvedType)));
        if(target->type.name == "verb") applyActionConstant(val);
        if(func == nullptr) rejectRunTimeMemberRead(val, target->dName());
        variableDeclaration& mem = *(new variableDeclaration());
        mem.name = target->name;
        mem.i6name = target->i6name.empty() && target->isPrePassStub ? clashRenamedMember(target->name) : target->i6name;   // a class below renames it
        mem.type = target->type;
        mem.isConst = target->isConst;
        mem.declaredExpressionValue = val;
        od.members.push_back(&mem);
        terminator = val->terminator;
    }
    return terminator;
}

void bglParser::bakeInlineObjectAggregate(classDef* cls, const string& typeDisplay, const string& objName,
                                          functionDef* func, statementBlock* body){
    objectDef& od = languageService.registerObject(objName, false, "");
    od.objectClass = cls;
    // An inline-baked anonymous object is a file-scope static regardless of where the `Type{ … }`
    // literal appears. When it appears inside a routine body (e.g. as a call argument), the compile
    // context isn't global, so registerObject did NOT add it to the emitted `globals` list — the
    // I6 output would then reference an object it never declares ("No such constant"). Force it in.
    if(std::find(languageService.globals.begin(), languageService.globals.end(), &od) == languageService.globals.end())
        languageService.globals.push_back(&od);
    std::vector<variableDeclaration*> inlineFields = positionalMembers(cls);
    auto findField = [](classDef* c, const std::string& fname) -> variableDeclaration* {
        if(!c) return nullptr;
        return dynamic_cast<variableDeclaration*>(c->findMember([&](typeMember* m){
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            return vd != nullptr && vd->name == fname;
        }));
    };
    // Separators carry meaning: POSITIONAL values are ','-separated (they read like a list);
    // a single ';' TRAILS the positional section and enters the NAMED section; NAMED members
    // are `field = value;` — ';'-separated, exactly like a normal object/class body. So a ','
    // always means "still positional", the first ';' means "now naming", and after that only
    // named members (';'-separated) may follow.
    size_t idx = 0;             // next positional slot
    bool inNamed = false;       // have we entered the named section
    bool sawPositional = false;
    bool expectNamed = false;   // previous separator was ';' → only a named member may follow
    token vt = file.getToken();
    while(!vt.is(token::braceClose)){
        if((vt.is(eTokenType::identifier) || vt.is(eTokenType::dataType)) && file.peekToken().is(":"))
            parsingError(format("inline '{0}{{...}}': a named member is written `{1} = value;`, not `{1}: value`",
                                typeDisplay, vt.originalValue.empty() ? vt.value : vt.originalValue));
        bool isNamed = (vt.is(eTokenType::identifier) || vt.is(eTokenType::dataType)) && file.peekToken().is("=");
        // Validate this item is legal in the current position.
        if(inNamed && !isNamed)
            parsingError(format("inline '{0}{{...}}': positional values must come before named ones", typeDisplay));
        if(expectNamed && !isNamed)
            parsingError(format("inline '{0}{{...}}': positional values are ','-separated; ';' begins the named section, "
                                "so only named members may follow it", typeDisplay));
        if(!inNamed && !expectNamed && isNamed && sawPositional)
            parsingError(format("inline '{0}{{...}}': named members follow a ';' — write `... ; {1} = value;` "
                                "(',' separates positional values only)",
                                typeDisplay, vt.originalValue.empty() ? vt.value : vt.originalValue));
        variableDeclaration* target;
        if(isNamed){
            inNamed = true;
            target = findField(cls, vt.value);
            if(!target)
                parsingError(format("inline '{0}{{...}}': '{1}' is not a member of the class",
                                    typeDisplay, vt.originalValue.empty() ? vt.value : vt.originalValue));
            file.getToken(token::assignment);   // consume '='
            vt = file.getToken();               // first token of the value
        } else {
            sawPositional = true;
            if(idx >= inlineFields.size())
                parsingError(format("inline '{0}{{...}}': too many positional values — the class declares {1} inline member(s)",
                                    typeDisplay, (int)inlineFields.size()));
            target = inlineFields[idx++];
        }
        string terminator = bakeMemberValue(od, target, vt, typeDisplay, func, body);
        if(terminator == token::braceClose) break;
        if(inNamed){
            // Named section: ';'-separated. A ',' between named members is an error.
            if(terminator == ",")
                parsingError(format("inline '{0}{{...}}': named members are separated by ';', not ',' — write `{1} = value;`",
                                    typeDisplay, target->name));
            expectNamed = true;
        } else {
            // Positional section: ',' stays positional; ';' transitions to the named section.
            expectNamed = (terminator == token::endStatement);
        }
        vt = file.getToken();
    }

    // ── Runtime field hoisting ──────────────────────────────────────────────────────────────
    // A static I6 object property can't hold a value computed from routine locals/params — it would
    // reference names not in scope at object-definition time ("No such constant as …"). When a
    // scalar field's value is a RUNTIME expression (a primitive value type, not a literal) AND we
    // are inside a routine body, hoist it OUT of the static object and set it at runtime, before
    // the enclosing statement (via pendingInjections). Constant fields (literals) and object-
    // reference members stay baked as static properties; a nested inline object's own runtime
    // fields were already hoisted by its own (recursive) bake. This is what lets an inline value
    // aggregate with runtime fields — `Type{ runtimeA, runtimeB }` — be passed to a routine (§6.2.1).
    // The per-site static object thus doubles as the per-site value temp (recursion caveat: a
    // recursive call reusing the same site reuses this instance — same limitation as class locals).
    for(typeMember* m : od.members)
        if(auto* ad = dynamic_cast<arrayDeclaration*>(m)){
            if(!ad->isPromoted && !ad->isRefLocal) promoteMemberArray(*ad);
        }
    if(func != nullptr){
        auto isRuntimeScalar = [](const std::string& t){
            return t=="int" || t=="uint" || t=="char" || t=="float" || t=="bool";
        };
        std::vector<typeMember*> kept;
        for(typeMember* m : od.members){
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            string why;
            // An array member's run-time elements: the static array holds 0 there, and each is stored first.
            if(auto* ad = dynamic_cast<arrayDeclaration*>(vd))
                if(auto* list = dynamic_cast<initializerList*>(ad->declaredExpressionValue))
                    for(size_t k = 0; k < list->elements.size(); k++){
                        expression* e = list->elements[k];
                        string whyEl;
                        if(e == nullptr || isLiteralValue(e, whyEl)) continue;
                        i6RawNode* inj = new i6RawNode();
                        inj->text = ad->isPromoted
                            ? "(" + objName + "." + memberI6Name(cls->name, ad->name) + ")-->" + to_string(k + 1) + " = " + e->text() + ";"
                            : objName + ".&" + memberI6Name(cls->name, ad->name) + "-->" + to_string(k) + " = " + e->text() + ";";
                        inj->cooked = true;
                        pendingInjections.push_back(inj);
                        expression* zero = new expression();
                        zero->tokens.push_back("0");
                        zero->resolvedType = "intliteral";
                        list->elements[k] = zero;
                    }
            if(vd && dynamic_cast<arrayDeclaration*>(vd) == nullptr && vd->declaredExpressionValue
               && (isRuntimeScalar(vd->declaredExpressionValue->resolvedType)
                   || !isLiteralValue(vd->declaredExpressionValue, why))){   // a string or object held in a local
                i6RawNode* inj = new i6RawNode();
                inj->text = objName + "." + memberI6Name(cls->name, vd->name) + " = " + vd->declaredExpressionValue->text() + ";";
                // The RHS may reference routine locals/params (e.g. `{width, height}` where width/height
                // are params) that shadow the aggregate class's property names. Mark the node `cooked` so
                // the emitter renames those value-context uses (RHS `width` → `_l_width`) while leaving the
                // LHS `.width` property access intact — and so it doesn't poison the shadow-rename guard.
                inj->cooked = true;
                pendingInjections.push_back(inj);
            } else {
                kept.push_back(m);
            }
        }
        od.members = kept;
    }
}


bool bglParser::processTypedObjectDeclaration(token typeTok, token nameTok, token classNameTok, Qualifiers& q, abstractObject& ctx){
    // Entered after "Type name : ClassName" have been consumed. Reads optional "as alias" then symbol.
    string objectClassName = classNameTok.value;
    // More than one base (`object dog : object, Animal, Robot { … }`): an object has one class, so
    // the bases go on a class of the object's own, declared here just ahead of it.
    if(file.peekToken().is(",")){
        vector<token> baseToks{classNameTok};
        while(file.peekToken().is(",")){
            file.getToken();
            baseToks.push_back(consumeTypeToken(file.getToken({eTokenType::dataType, eTokenType::identifier})));
        }
        string objDisplay = nameTok.originalValue.empty() ? (string)nameTok : nameTok.originalValue;
        classDef& cls = languageService.registerClass("_" + (string)nameTok + "_class", false, objDisplay);
        for(token& b : baseToks){
            classDef* base = languageService.findClass(b.value);
            if(!base) unknownBaseError(b);
            if(find(cls.baseClasses.begin(), cls.baseClasses.end(), base) == cls.baseClasses.end())
                cls.baseClasses.push_back(base);
        }
        objectClassName = cls.name;
    }
    string i6alias;
    if(file.peekToken().is("asi6")){
        file.getToken();
        token aliasTok = file.getToken(eTokenType::identifier);
        i6alias = aliasTok.originalValue.empty() ? aliasTok.value : aliasTok.originalValue;
    }
    token symbol = file.getToken({token::assignment, token::parenOpen, token::endStatement, token::braceOpen});
    if(symbol.is(token::parenOpen))
        return processRoutineDeclaration(typeTok, nameTok, ctx, q.isExtern, q.isEmitter, q.isReplace, q.isDefault, q.isSuperposed);
    else if(symbol.is(token::braceOpen))
        return processObjectDeclaration(typeTok, nameTok, q.isExtern, objectClassName, i6alias, true, q.isEmitter);
    else
        return processVariableDeclaration(typeTok, nameTok, symbol, ctx, q.isExtern, q.isConst, i6alias, q.isRef);
}


bool bglParser::processAliasedDeclaration(token typeTok, token nameTok, token aliasTok, Qualifiers& q, abstractObject& ctx){
    // Entered after "Type name as alias" have been consumed. Reads symbol.
    string i6alias = aliasTok.originalValue.empty() ? aliasTok.value : aliasTok.originalValue;
    token symbol = file.getToken({token::assignment, token::parenOpen, token::endStatement, token::braceOpen});
    if(symbol.is(token::parenOpen)){
        if(q.isExtern)
            parsingError(format("On an extern function the clause follows the parameters: `extern {0} {1}(…) asBgl {2};`.",
                (string)typeTok, i6alias, nameTok.originalValue.empty() ? nameTok.value : nameTok.originalValue));
        parsingError("'asI6' can't rename a free function.");
    }
    else if(symbol.is(token::braceOpen))
        return processObjectDeclaration(typeTok, nameTok, q.isExtern, "", i6alias, true, q.isEmitter);
    else
        return processVariableDeclaration(typeTok, nameTok, symbol, ctx, q.isExtern, q.isConst, i6alias, q.isRef);
}


// Parse an alias member (`alias` qualifier already consumed; `aliasName` is the member name).
// Two forms, both emitting NO I6 for the member itself (pure compile-time indirection):
//   alias name for Type;   — TYPE alias: `host.name` used in a type position resolves to Type.
//   alias name = Target;   — VALUE alias: `host.name.method()` resolves to Target's members at parse
//                            time, with NO runtime property on the host. If Target is an object, the
//                            objectDef redirect in qualifyIdentifier resolves the dispatch (via
//                            declaredExpressionValue); if a class, it's a class namespace alias (same
//                            shape as `emitter auto name = Class`). Because no property links the host
//                            to Target, Target is referenced only where the alias is actually used —
//                            so a `superposed` Target is emitted only then. This is what lets a
//                            namespace like `bgl.world` (alias to `superposed object _bglWorld`) cost
//                            nothing until a query is called.
void bglParser::parseAliasMember(token aliasName, std::vector<typeMember*>& members, const std::string& context){
    token sep = file.getToken();
    variableDeclaration& aliasDef = *(new variableDeclaration());
    aliasDef.name = (string)aliasName;
    aliasDef.displayName = aliasName.originalValue;
    aliasDef.src = aliasName.src;
    aliasDef.isExternal = true;  // no I6 emission — compile-time indirection only
    if(sep.is("for")){
        token typeTok = file.getToken({eTokenType::dataType, eTokenType::identifier});
        // `alias U for inner.T;`: a path to another alias names that alias's type.
        if(file.peekToken().is(token::period)){
            string path = typeTok.value;
            while(file.peekToken().is(token::period) && file.peekToken(2).is(eTokenType::name)){ file.getToken(); path += "." + file.getToken().value; }
            string t = resolveNamespacedType(path);
            if(t.empty())
                parsingError(format("alias member '{0}': '{1}' is not a declared type (class or enum)", aliasName.value, path));
            typeTok.value = t;
        }
        // `alias grade for kind;` where `kind` is itself a type alias (imported, or the object's own).
        if(!languageService.isClassType(typeTok.value) && languageService.findEnum(typeTok.value) == nullptr)
            if(string t = typeAliasTarget(typeTok.value); !t.empty()) typeTok.value = t;
        if(!languageService.isClassType(typeTok.value))
            parsingError(format("alias member '{0}': '{1}' is not a declared type (class or enum)",
                aliasName.value, typeTok.originalValue.empty() ? typeTok.value : typeTok.originalValue));
        aliasDef.type = languageService.getType(typeTok.value);
        aliasDef.isAlias = true;  // type alias
    } else if(sep.is(token::assignment)){
        token rhs = file.getToken({eTokenType::identifier, eTokenType::dataType});
        // `alias o = root.ops;`: a path through objects' value-alias and `auto` members to an object or a class.
        if(file.peekToken().is(token::period)){
            string shown = rhs.originalValue.empty() ? rhs.value : rhs.originalValue;
            objectDef* at = languageService.findGlobalAs<objectDef>(rhs.value);
            string target;
            while(file.peekToken().is(token::period)){
                file.getToken();
                token seg = file.getToken({eTokenType::identifier, eTokenType::dataType});
                shown += "." + (seg.originalValue.empty() ? seg.value : seg.originalValue);
                variableDeclaration* step = nullptr;
                if(at != nullptr)
                    for(typeMember* m : at->members)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd && vd->name == seg.value && (!step || step->isPrePassStub)) step = vd;
                at = nullptr; target.clear();
                if(step == nullptr) break;
                string init = step->declaredExpressionValue ? step->declaredExpressionValue->text() : "";
                if(!init.empty() && (at = languageService.findGlobalAs<objectDef>(init)) != nullptr) target = init;
                else if(step->isAlias || step->isValueAlias) target = step->type.name;
            }
            if(target.empty())
                parsingError(format("alias member '{0}': '{1}' does not name an object or a class", aliasName.value, shown));
            rhs.value = target;
            rhs.originalValue.clear();
        }
        typeDef& rhsType = languageService.getType(rhs.value);
        objectDef* rhsObj = dynamic_cast<objectDef*>(&rhsType);
        classDef*  rhsCls = dynamic_cast<classDef*>(&rhsType);
        if(!rhsObj && !rhsCls)
            parsingError(format("alias member '{0}': '{1}' is not a declared object or class",
                aliasName.value, rhs.originalValue.empty() ? rhs.value : rhs.originalValue));
        aliasDef.type = languageService.getType(rhs.value);
        aliasDef.isValueAlias = true;
        if(rhsObj){
            // Object value alias: dispatch resolves through the objectDef redirect, which reads
            // declaredExpressionValue for the target object name.
            expression* e = new expression();
            e->tokens.push_back(rhs.value);
            aliasDef.declaredExpressionValue = e;
        }
        // class target: type-only alias — same shape the emitter-auto class namespace alias uses.
    } else {
        parsingError(format("'alias {0}' in {1} requires 'for Type' or '= objectOrClass'",
            aliasName.value, context));
    }
    file.getToken(token::endStatement);
    // The pre-scan's stub of the alias gives way to it (the stub's type may be a name it couldn't resolve).
    if(aliasDef.isAlias)
        for(auto& m : members)
            if(m->name == aliasDef.name && m->isPrePassStub && dynamic_cast<variableDeclaration*>(m) != nullptr){ m = &aliasDef; return; }
    // A value alias's stub takes what the alias names, for a use the stub answers (`alias t = root.tally;`).
    if(aliasDef.isValueAlias)
        for(typeMember* m : members)
            if(auto* stub = dynamic_cast<variableDeclaration*>(m); stub && stub->isPrePassStub && stub->name == aliasDef.name){
                stub->type = aliasDef.type;
                stub->isValueAlias = true;
                stub->isExternal = true;   // compile-time only, as the alias itself
                if(aliasDef.declaredExpressionValue != nullptr) stub->declaredExpressionValue = aliasDef.declaredExpressionValue;
            }
    members.push_back(&aliasDef);
}

// Named union: `union Name = A | B [ { members } | ; ]` (the `union` keyword already consumed).
// Registers Name as an emitter class carrying the member type list; an optional `{ … }` body is
// parsed by processClassDeclaration so members (emitter/static, especially `print()`) reuse the
// normal class machinery. Compatibility later expands the name to its structural union.
bool bglParser::processUnionDeclaration(Qualifiers& q){
    if(getCurrentCompileContext() != eCompileContext::global)
        parsingError("union declarations are only allowed in global context");
    token nameTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
    file.getToken("=");

    // Read the member list: firstType [ '|' member ]…  (reusing the union-tail machinery).
    token ft = file.getToken({eTokenType::dataType, eTokenType::identifier});
    string first = ft.value;
    if(first == "func") first = parseFuncType();
    else if(first == "array" || first == "rawarray"){
        string full = parseArrayTypeTail(first);
        first = (full == "array<char>" || full == "array<charliteral>") ? "bytearray" : full;
    }
    string canonical = maybeParseUnionTail(first);          // "A|B…" (sorted + deduped)
    vector<string> members = splitUnionType(canonical);
    if(members.size() < 2)
        parsingError(format("union '{0}' must have at least two distinct member types", (string)nameTok));

    token next = file.peekToken();
    if(next.is(token::endStatement)){
        file.getToken(token::endStatement);
        classDef& cls = languageService.registerClass((string)nameTok, false, nameTok.originalValue);
        cls.isEmitterClass = true;
        cls.unionMembers   = members;
        if(!nameTok.docComment.empty()) cls.docComment = nameTok.docComment;
        return false;
    }
    if(next.is(token::braceOpen)){
        // Reuse the class member-body parser (registers the emitter class + parses `{ … }`),
        // then attach the union member list.
        processClassDeclaration(nameTok, /*isExternal*/false, /*isExtend*/false,
                                /*isEmitterClass*/true, /*isAlias*/false, nameTok);
        if(auto* cls = languageService.findClass((string)nameTok))
            cls->unionMembers = members;
        return false;
    }
    parsingError(format("union '{0}': expected ';' or '{{ … }}' after the member list", (string)nameTok));
    return false;
}

// "A|B…" structural expansion of a named-union type name; "" if typeName is not a named union
// (or is already a structural `A|B` form). Lets isTypeCompatible treat a named union transparently.
std::string bglParser::unionExpansionOf(const std::string& typeName){
    if(isUnionType(typeName)) return "";   // already structural
    auto* cls = languageService.findClass(typeName);
    if(cls && cls->isUnion()) return cls->unionExpansion();
    return "";
}

// Parse one member of an `extern object` body — a single iteration of that body's member loop.
// On entry `tok` is the member's first token; on return it is the next member's first token.
void bglParser::parseExternObjectMember(objectDef& newObj, token& tok){
    markMemberStart(tok);
    rejectAccessModifier(tok);
    if(tok.is("#if") || tok.is("#elif") || tok.is("#else") || tok.is("#endif")){
        processDirective(tok, newObj);
        tok = file.getToken();
        return;
    }
    Qualifiers q = parseQualifiers(tok);
    if(q.isEmitter){
        // Emitter methods and emitter values are allowed on extern objects
        token retType = tok;
        token propName = file.getToken({eTokenType::identifier, eTokenType::dataType});
        if(propName.is("operator")){
            token opTok = file.getToken();
            if(opTok.is(token::parenOpen)) propName.value = "operator()";
            else if(opTok.is(token::bracketOpen)){
                file.getToken(token::bracketClose);
                propName.value = "[]";
                if(file.peekToken().is(token::assignment)){ file.getToken(); propName.value = "[]="; }
            }
            else if(opTok.is("?")) propName.value = "?";
            else if(opTok.is("switch")) propName.value = "switch";
            else { opTok.assert(eTokenType::oper); propName.value = opTok.value; }
        }
        functionDef& funcDef = *(new functionDef());
        funcDef.name = (string)propName;
        funcDef.displayName = propName.originalValue;
        funcDef.src = propName.src.line > 0 ? propName.src : file.currentLocation();
        funcDef.returnType = languageService.getType((string)retType);
        funcDef.isEmitter = true;
        funcDef.isExplicit = q.isExplicit;
        funcDef.isDefault = q.isDefault;
        if(!retType.docComment.empty())       funcDef.docComment = retType.docComment;
        else if(!propName.docComment.empty()) funcDef.docComment = propName.docComment;
        token sym = file.getToken();
        bool funcHasParens = false;
        if(sym.is(token::parenOpen)){ funcHasParens = true; processParameterList(funcDef); sym = file.getToken(); }
        i6Block& rawblock = *(new i6Block());
        if(sym.is(token::endStatement)) rawblock.i6Body = " $self";
        else { rawblock.i6Body = file.getRawTextThroughClosingBrace(/*isI6Content=*/true); if(!funcHasParens) funcDef.isValueEmitter = true; }
        funcDef.body = &rawblock;
        if(!replaceStubMember(newObj.members, funcDef))   // the pre-scan's bodyless stub stands in for it until now
            newObj.members.push_back((typeMember*)&funcDef);
    } else if(tok.isDataType()){
        // Type declaration or method stub on extern object
        token propName = file.getToken({eTokenType::identifier, eTokenType::dataType});
        token sym = file.getToken({token::endStatement, token::assignment, token::parenOpen});
        if(sym.is(token::assignment)){
            if(tok.value == "auto" && file.peekToken().is(token::braceOpen))
                parsingError(format("extern object '{0}': '{1}' can't be an inline accessor; an I6-defined object can't hold "
                    "its backing instance.", newObj.dName(), propName.value));
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
            funcDef.displayName = propName.originalValue;
            funcDef.src = propName.src.line > 0 ? propName.src : file.currentLocation();
            funcDef.returnType = languageService.getType(tok.value);
            funcDef.isExternal = true;
            if(!tok.docComment.empty())           funcDef.docComment = tok.docComment;
            else if(!propName.docComment.empty()) funcDef.docComment = propName.docComment;
            processParameterList(funcDef);
            // Body is not allowed on non-emitter methods in extern objects
            token after = file.getToken();
            if(after.is(token::braceOpen)){
                file.getRawTextThroughClosingBrace();
                parsingError(format("extern object '{0}': non-emitter method '{1}' cannot have a body; use 'emitter' or declare without a body",
                    newObj.dName(), propName.value));
            }
            // else: after is ; — declaration stub, valid.
            // Replace the pre-scan stub (registered without default-value info) with
            // this fully-parsed funcDef so defaults/arity resolve at call sites; append
            // if no stub exists (e.g. a second overload after the first claimed it).
            if(!replaceStubMember(newObj.members, funcDef))
                newObj.members.push_back((typeMember*)&funcDef);
            tok = file.getToken();
            return;
        }
        // Property without initializer — register for type checking. Replace the
        // pre-scan stub (same name) so we don't leave a duplicate untyped member.
        variableDeclaration& prop = *(new variableDeclaration());
        prop.name = propName.value;
        prop.displayName = propName.originalValue;
        prop.src = propName.src;
        prop.type = languageService.getType(tok.value);
        prop.isExternal = true;
        if(!tok.docComment.empty())           prop.docComment = tok.docComment;
        else if(!propName.docComment.empty()) prop.docComment = propName.docComment;
        bool propReplaced = false;
        for(size_t i = 0; i < newObj.members.size(); i++)
            if(newObj.members[i]->name == prop.name)
                if(auto* vd = dynamic_cast<variableDeclaration*>(newObj.members[i]))
                    if(vd->isPrePassStub){ newObj.members[i] = &prop; propReplaced = true; break; }
        if(!propReplaced) newObj.members.push_back((typeMember*)&prop);
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

// Parse the body of an `extern object`/`extern verb` declaration: the dictionary-word verb
// shorthand, then the member loop, then the global registration of a bodied extern object.
void bglParser::parseExternObjectBody(objectDef& newObj, verbObjectDef* vod){
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
        return;
    }

    if(vod != nullptr && file.peekToken().is(token::braceClose)) vod->claimsNoWords = true;
    token tok = file.getToken();
    while(tok.isNot(token::braceClose) && tok.isNot(eTokenType::eof)){
        parseExternObjectMember(newObj, tok);
    }
    // Make an extern object WITH a body referenceable as a global (e.g.
    // playerCommands.pushCommand(...)). registerObject/rePushIfMissing deliberately keep
    // externs out of `globals` (bare `extern object foo;` is a variableDeclaration on a
    // separate path), so a bodied extern objectDef would otherwise be a type with no
    // referenceable name → "Unknown variable". Verb-derived externs are excluded: they live
    // in languageService.verbs and emit as grammar. emitObject() skips externs so no
    // duplicate `Object` directive is emitted for these.
    closeCompileContext(eCompileContext::objectDef);
    currentObject = savedObject;
    // Register (after restoring the outer context) so the check sees global scope, not the
    // objectDef context opened for member parsing above.
    if(getCurrentCompileContext() == eCompileContext::global && vod == nullptr){
        if(find(languageService.globals.begin(), languageService.globals.end(),
                (typeDef*)&newObj) == languageService.globals.end())
            languageService.globals.push_back((typeDef*)&newObj);
    }
}

// Parse one member of an object body — a single iteration of the member loop. On entry `tok` is
// the member's first token; on return it is the first token of the next member (or '}').
void bglParser::parseObjectMember(objectDef& newObj, token& tok){
    markMemberStart(tok);
    rejectAccessModifier(tok);
    if(tok.is(eTokenType::eof)) parsingError(format("Unexpected end of file inside object '{0}' — missing closing '}}'", newObj.dName()));
    if(tok.is(eTokenType::directive)){
        if(tok.is("#i6")) processI6InlineMember(newObj);
        else if(tok.is("#if") || tok.is("#elif") || tok.is("#else") || tok.is("#endif")) processDirective(tok, newObj);
        else parsingError(format("Unsupported directive in object body: '{0}'", tok.value));
        tok = file.getToken();
        return;
    }
    // A misplaced qualifier is reported at its member, not at the object.
    sourceLocation memberSrc = tok.src;
    memberSrc.col = max(1, tok.src.col - (int)tok.originalValue.size());
    auto memberError = [&](const string& msg){ if(memberSrc.line > 0) currentStatementSrc = memberSrc; parsingError(msg); };
    Qualifiers q = parseQualifiers(tok);
    // A member typed by a namespace path (`Units.mode m;`) or a `#using`-imported alias, as in a class.
    // An alias's own name is never read as the type it stands for.
    if(q.isAlias) {}
    else if(tok.is(eTokenType::identifier) && file.peekToken().is(token::period) && isNamespacedTypePath(tok))
        tok = consumeTypeToken(tok);
    else if(tok.is(eTokenType::identifier) && (file.peekToken().is(eTokenType::identifier) || file.peekToken().is(eTokenType::dataType)))
        tok = consumeTypeToken(tok);
    if(q.isRequired)
        memberError("'required' applies to class members: an object gives the member its value.");
    bool memberIsReplace = q.isReplace;
    // `box = v` where a class is also named Box (the member emits under an `asI6` name): a type
    // followed by `=` can't begin a declaration, so this is the member.
    bool assignsMember = file.peekToken().is(token::assignment) || file.peekToken().is(token::bindAssignment);
    if(assignsMember) tok.tokenType = eTokenType::identifier;
    // Context-specific validation
    if(q.isExtern)  memberError("'extern' is not valid inside an object body");
    if(q.isExtend)  memberError("'extend' is not valid inside an object body");
    if(q.isAlias){
        parseAliasMember(tok, newObj.members, "object body");
        tok = file.getToken();
        return;
    }
    if(!assignsMember) typeFromMemberAliasOrPath(newObj, tok);
    objectMemberIsConst = q.isConst;   // a `const` member, as in a class body (§8.3.2)
    if(q.isStatic)  memberError("'static' is not valid inside an object body");
    if(q.isDefault) memberError("'default' is only valid in class declarations, not object instances");
    if(q.isEmitter){
        // `emitter array<T> name(...)` — route to processArrayMember which already
        // handles emitter routines (via q->isEmitter). Without this dispatch, the
        // emitter branch below would try to consume `<` as the propName identifier
        // and fail. Mirrors the same routing done for non-emitter `array<T>` at the
        // bottom of this loop.
        if((tok.value == "array" || tok.value == "rawarray") && file.peekToken().is("<")){
            processArrayMember(newObj.members, newObj.dName(), dynamic_cast<verbObjectDef*>(&newObj), &newObj, &q, tok.value == "rawarray", tok.docComment);
            tok = file.getToken();
            return;
        }
        // Alias member: `emitter auto name = ClassRef;` — compile-time indirection to another class.
        // Supported on objects so namespace-like objects can expose emitter classes alongside real state.
        if(tok.value == "auto"){
            token aliasName = file.getToken({eTokenType::identifier, eTokenType::dataType});
            file.getToken(token::assignment);
            token rhs = file.getToken({eTokenType::identifier, eTokenType::dataType});
            classDef* rhsCls = languageService.findClass(rhs.value);
            if(!rhsCls)
                parsingError(format("'auto' alias member '{0}': '{1}' is not a declared class",
                    (string)aliasName, rhs.originalValue.empty() ? rhs.value : rhs.originalValue));
            file.getToken(token::endStatement);
            variableDeclaration& aliasDef = *(new variableDeclaration());
            aliasDef.name = (string)aliasName;
            aliasDef.displayName = aliasName.originalValue;
            aliasDef.src = aliasName.src;
            aliasDef.type = languageService.getType(rhs.value);
            aliasDef.isExternal = true;  // no I6 emission
            aliasDef.isValueAlias = true;   // a namespace hook: a path through it redirects to the class
            newObj.members.push_back(&aliasDef);
            tok = file.getToken();
            return;
        }
        // emitter method inside object body — parse as raw I6 body
        token retType = tok;
        // A member name may collide with a TYPE name (Beguile is case-insensitive, so a user
        // class `A` makes the bare word `a` lex as a dataType). Accept both, as class members do.
        token propName = file.getToken({eTokenType::identifier, eTokenType::dataType});
        if(propName.is("operator")){
            token opTok = file.getToken();
            if(opTok.is(token::parenOpen)) propName.value = "operator()";
            else if(opTok.is(token::bracketOpen)){
                file.getToken(token::bracketClose);
                propName.value = "[]";
                if(file.peekToken().is(token::assignment)){ file.getToken(); propName.value = "[]="; }
            }
            else if(opTok.is("?")) propName.value = "?";
            else if(opTok.is("switch")) propName.value = "switch";
            else { opTok.assert(eTokenType::oper); propName.value = opTok.value; }
        }
        functionDef& funcDef = *(new functionDef());
        funcDef.name = (string)propName;
        funcDef.displayName = propName.originalValue;
        funcDef.src = propName.src.line > 0 ? propName.src : file.currentLocation();
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
    } else if((tok.value == "array" || tok.value == "rawarray") && !genericTypeIsUnionHead())
        processArrayMember(newObj.members, newObj.dName(), dynamic_cast<verbObjectDef*>(&newObj), &newObj, &q, tok.value == "rawarray", tok.docComment);
    else if(tok.isDataType() && !assignsMember)
        processTypedMember(newObj, tok, memberIsReplace, q.isRef);
    else if(tok.is(eTokenType::identifier))
        processInheritedMember(newObj, tok);
    else
        parsingError(format("Unexpected token '{0}' in object body", tok.value));
    tok = file.getToken();
}

// A view of an object's type aliases (`alias row for Row;`), imported while its body is parsed so its
// methods can name those types bare; its other members stay out of the import.
objectDef* bglParser::pushOwnTypeAliases(objectDef& obj){
    auto* view = new objectDef();
    view->name = obj.name;
    syncOwnTypeAliases(obj, view);
    usingObjectImports.push_back(view);
    return view;
}

void bglParser::syncOwnTypeAliases(objectDef& obj, objectDef* view){
    view->members.clear();
    for(typeMember* m : obj.members)
        // A stub counts too: an alias a later `extend` adds is named in the body above it.
        if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd && vd->isAlias
           && (languageService.isClassType(vd->type.name) || languageService.findEnum(vd->type.name) != nullptr))
            view->members.push_back(m);
}

// The rows a class bakes for an array-of-arrays member (`= { {1, 2}, {3} }`) are copied for each
// instance that doesn't give the member a value of its own, so no two instances share a row.
// A sealed slot written with the type its `operator =` takes (`object parent = room;`) names the
// value it starts with, not a new type for the slot.
bool bglParser::sealedSlotTakes(const string& sealedType, const string& written){
    classDef* sc = getDispatchClass(sealedType);
    return sc != nullptr && findMemberInHierarchy(sc, [&](typeMember* m){
        auto* fd = dynamic_cast<functionDef*>(m);
        return fd && fd->name == "=" && fd->params.size() == 1 && fd->params[0]->type.name == written; }) != nullptr;
}

void bglParser::giveInstanceOwnRows(objectDef& obj){
    if(obj.objectClass == nullptr) return;
    function<expression*(expression*)> copyRow = [&](expression* e) -> expression* {
        string t = e != nullptr ? e->text() : "";
        auto* row = t.rfind("_bglanon", 0) == 0 ? languageService.findGlobalAs<arrayDeclaration>(t) : nullptr;
        if(row == nullptr) return e;
        auto* mine = new arrayDeclaration(*row);
        mine->name = mine->displayName = format("_bglanon{0}", anonObjectCounter++);
        if(auto* list = dynamic_cast<initializerList*>(row->declaredExpressionValue)){
            auto* rows = new initializerList(*list);
            for(expression*& inner : rows->elements) inner = copyRow(inner);
            mine->declaredExpressionValue = rows;
        }
        languageService.registerInstance(*mine);
        adoptInitialElements(mine->name, mine->elementType, dynamic_cast<initializerList*>(mine->declaredExpressionValue));
        auto* ref = new expression(*e);
        ref->tokens.assign(1, mine->name);
        return ref;
    };
    set<string> own;
    for(typeMember* m : obj.members)
        if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd == nullptr || !vd->isPrePassStub) own.insert(m->name);
    vector<variableDeclaration*> overrides;
    obj.objectClass->forEachMember([&](typeMember* m){
        auto* a = dynamic_cast<arrayDeclaration*>(m);
        if(a == nullptr || (a->isRefLocal && !a->isPromoted) || a->isStatic || !isArrayOfArraysElement(a->elementType) || own.count(a->name)) return;
        auto* list = dynamic_cast<initializerList*>(a->declaredExpressionValue);
        if(list == nullptr) return;
        own.insert(a->name);
        auto* rows = new initializerList(*list);
        bool copied = false;
        for(expression*& e : rows->elements){ expression* c = copyRow(e); copied |= c != e; e = c; }
        if(!copied) return;
        auto* value = new variableDeclaration();
        value->name = a->name;
        value->src = a->src;
        value->declaredExpressionValue = rows;
        if(variableDeclaration* arr = asInheritedArrayOverride(*value, {obj.objectClass})) overrides.push_back(arr);
    });
    for(variableDeclaration* arr : overrides){
        bool replaced = false;
        for(typeMember*& m : obj.members)
            if(m->name == arr->name){ m = arr; replaced = true; break; }
        if(!replaced) obj.members.push_back(arr);
    }
}

bool bglParser::processObjectDeclaration(token objectType, token name, bool isExternal, string className, string i6alias, bool hasBody, bool isEmitter, bool isSuperposed){
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
        if(classDef* cls = languageService.findClass(resolvedClassName))
            isVerbDerived = checkVerbBase(cls);
    }

    // Create verbObjectDef for verb-derived objects, objectDef otherwise
    objectDef* objPtr;
    verbObjectDef* vod = nullptr;
    string origName = name.originalValue.empty() ? (string)name : name.originalValue;
    if(isVerbDerived){
        if(!isExternal)
            for(verbObjectDef* v : languageService.verbs)
                if(v->isExternal && v->claimsNoWords && v->name == (string)name)
                    parsingError(format("'{0}' is a fake action of the I6 library: it has no grammar or action "
                        "routine, and Inform 6 can't give a real action its name. Give the verb another name.",
                        origName));
        // A verb whose class is declared below it was pre-scanned as a plain object: that stub goes.
        string lowerName = (string)name;
        transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        if(objectDef* stub = languageService.findObjectType(lowerName);
           stub != nullptr && stub->isPrePassStub && dynamic_cast<verbObjectDef*>(stub) == nullptr){
            auto drop = [&](vector<typeDef*>& v){ v.erase(remove(v.begin(), v.end(), (typeDef*)stub), v.end()); };
            drop(languageService.globals);
            languageService.objectInstances.erase(remove(languageService.objectInstances.begin(), languageService.objectInstances.end(), stub),
                                                  languageService.objectInstances.end());
        }
        verbObjectDef& vd = languageService.registerVerbObject(origName, isExternal);
        objPtr = &vd;
        vod = &vd;
    } else {
        objectDef& od = languageService.registerObject((string)name, isExternal, name.originalValue);
        objPtr = &od;
    }
    objectDef& newObj = *objPtr;
    newObj.isSuperposed = isSuperposed;
    if(!i6alias.empty()) newObj.i6name = i6alias;
    // Doc-comment attached to the object's declaration (leading type or name token).
    if(!objectType.docComment.empty())   newObj.docComment = objectType.docComment;
    else if(!name.docComment.empty())    newObj.docComment = name.docComment;
    if(!resolvedClassName.empty()){
        if(classDef* cls = languageService.findClass(resolvedClassName))
            newObj.objectClass = cls;
        else if(!className.empty())
            parsingError(format("Unknown class '{0}' in object declaration", className));
    }

    // Extern objects: parse body for type registration but validate members.
    // Only type declarations (no initializer) and emitters are allowed.
    if(isExternal){
        if(hasBody) parseExternObjectBody(newObj, vod);
        return false;
    }

    objectDef* savedObject = currentObject;
    currentObject = &newObj;
    openCompileContext(eCompileContext::objectDef);

    token tok;
    if(hasBody) tok = file.getToken();
    else tok.value = token::braceClose;   // `Note n;`: an object with no body
    if(startsPositionalSection(tok)) tok = parseDeclarationPositionals(newObj, tok);
    objectDef* aliases = pushOwnTypeAliases(newObj);   // the object's own type aliases are bare names inside it
    while(tok.isNot(token::braceClose)){ parseObjectMember(newObj, tok); syncOwnTypeAliases(newObj, aliases); }
    usingObjectImports.pop_back();
    giveInstanceOwnRows(newObj);

    closeCompileContext(eCompileContext::objectDef);
    currentObject = savedObject;

    // Verb-specific post-processing: link handler() (the action body). `verb` declares handler()
    // `required`, so a verb that gives none is reported with the other required members.
    if(vod && !isExternal){
        for(typeMember* m : vod->members)
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == "handler"){ vod->doFunc = fd; break; }
        // A verb declared through a subclass of verb may inherit its handler() (the core's default,
        // an emitter that runs the library's routine, doesn't count).
        if(!vod->doFunc && vod->objectClass != nullptr)
            if(typeMember* m = findMemberInHierarchy(vod->objectClass, [](typeMember* x){
                    auto* fd = dynamic_cast<functionDef*>(x);
                    return fd != nullptr && fd->name == "handler" && !fd->isEmitter && !fd->isRequired;
                }))
                vod->doFunc = dynamic_cast<functionDef*>(m);
    }

    return false;
}

// ===============================================================================
// extend <array> { inject/remove/move ... } - declarative build-time array editing
// ===============================================================================
// Edits a previously-declared array's baked initializer list in place. The normal array
// baking (i6Emitter) then emits the reordered/edited result — so this is a pure compile-time
// transform, zero runtime cost. Generic: it knows nothing about rules; any array works.
// Elements are referenced by NAME (matches an element's rendered text — the array-of-named-
// references case) or by [N] index; positions are `after X | before X | first | last`.
bool bglParser::processArrayExtension(arrayDeclaration* arr){
    string arrName = arr->name;
    // An extern array's data is defined in I6, not Beguile — there is no initializer for us to
    // edit, and Beguile emits no array directive for it. Editing it declaratively is meaningless.
    if(arr->isExternal)
        parsingError(format("extend {0}: cannot edit an extern array — its contents are defined "
                            "in I6, not Beguile, so there is no initializer to inject into", arrName));
    initializerList* init = dynamic_cast<initializerList*>(arr->declaredExpressionValue);
    if(!init){
        // Sized/uninitialized array had no list — start one so injects have somewhere to land.
        init = new initializerList();
        arr->declaredExpressionValue = init;
    }

    // Resolve an element reference (a name or [N]) to its index in init->elements.
    auto resolveElemRef = [&]() -> int {
        token t = file.getToken();
        if(t.is(token::bracketOpen)){
            token nTok = file.getToken(eTokenType::integer);
            file.getToken(token::bracketClose);
            int idx = stoi(nTok.value);
            if(idx < 0 || idx >= (int)init->elements.size())
                parsingError(format("extend {0}: index [{1}] out of range (array has {2} element(s))",
                                    arrName, idx, (int)init->elements.size()));
            return idx;
        }
        string name = t.value;   // lexer lowercases identifiers
        for(size_t i = 0; i < init->elements.size(); i++){
            string et = init->elements[i]->text();
            transform(et.begin(), et.end(), et.begin(), ::tolower);
            if(et == name) return (int)i;
        }
        parsingError(format("extend {0}: no element named '{1}' in the array",
                            arrName, t.originalValue.empty() ? t.value : t.originalValue));
        return -1;
    };

    // Given a just-read position keyword, return the insert index into init->elements.
    // `after`/`before` read an element reference; `first`/`last` are absolute.
    auto resolvePosition = [&](const std::string& clause) -> int {
        if(clause == "first")  return 0;
        if(clause == "last")   return (int)init->elements.size();
        if(clause == "after")  return resolveElemRef() + 1;
        if(clause == "before") return resolveElemRef();
        parsingError(format("extend {0}: expected 'after', 'before', 'first', 'last', or ';' but found '{1}'",
                            arrName, clause));
        return (int)init->elements.size();
    };

    file.getToken(token::braceOpen);
    token verb = file.getToken();
    while(!verb.is(token::braceClose)){
        // Conditional compilation between edits, as between any other members (§14.2.5).
        if(verb.is("#if") || verb.is("#elif") || verb.is("#else") || verb.is("#endif")){
            processDirective(verb, *arr);
            verb = file.getToken();
            continue;
        }
        if(verb.is("inject")){
            // inject <element> [after X | before X | first | last];   (no clause = append)
            // The element is a single reference/value (a named object or a literal) OR an inline
            // object declaration `Type{ … }` (parsed by parseExpression's primary → a baked anon
            // object + a reference). Both end up as an expression stored the same way the array's
            // original elements are, so they bake identically.
            token elemTok = file.getToken();
            expression* elem;
            std::string clauseStr;
            classDef* elemCls = getDispatchClass(arr->elementType);
            if(elemTok.is(token::braceOpen) && isInlineConstructible(elemCls) && !isArrayOfArraysElement(arr->elementType)){
                // Inferred inline object: a bare `{ … }` takes the array's element type (the '{' is
                // already consumed). Bake an anon object and inject a reference, just like the explicit
                // `Type{ … }` form below. The clause keyword (or ';') follows the closing '}'.
                string anonName = format("_bglanon{0}", anonObjectCounter++);
                bakeInlineObjectAggregate(elemCls, arr->elementType, anonName, nullptr, nullptr);
                elem = new expression();
                elem->tokens.push_back(anonName);
                elem->resolvedType = arr->elementType;
                clauseStr = file.getToken().value;            // after | before | first | last | ;
            } else if(getDispatchClass(elemTok.value) != nullptr && file.peekToken().is(token::braceOpen)){
                // Inline object element — let the expression parser build it; it stops at the
                // position keyword or ';' (all terminators here).
                elem = parseExpression(elemTok, {"after", "before", "first", "last", token::endStatement}, nullptr, nullptr);
                clauseStr = elem->terminator;                 // "after"/"before"/"first"/"last", or the ';' terminator
                if(elem->terminator == token::endStatement) clauseStr = ";";
            } else if(elemTok.is(token::braceOpen) && isArrayOfArraysElement(arr->elementType)){
                // An inner array, baked as the array's own `{ … }` elements are.
                string anonName = format("_bglanon{0}", anonObjectCounter++);
                bakeInlineArrayAggregate(arrayInnerType(arr->elementType), anonName, nullptr, nullptr);
                elem = new expression();
                elem->tokens.push_back(anonName);
                elem->resolvedType = arr->elementType;
                clauseStr = file.getToken().value;
            } else {
                // A value, read as the array's initial elements are (`eStation.forge`, a bare enum member, `.word`).
                string savedExpected = currentExpectedType;
                if(languageService.findEnum(arr->elementType) != nullptr) currentExpectedType = arr->elementType;
                elem = parseExpression(elemTok, {"after", "before", "first", "last", token::endStatement}, nullptr, nullptr);
                currentExpectedType = savedExpected;
                clauseStr = elem->terminator == token::endStatement ? string(";") : elem->terminator;
            }
            int pos;
            if(clauseStr == ";") pos = (int)init->elements.size();  // append
            else { pos = resolvePosition(clauseStr); file.getToken(token::endStatement); }
            init->elements.insert(init->elements.begin() + pos, elem);
        } else if(verb.is("remove")){
            // remove <name | [N]>;
            int idx = resolveElemRef();
            file.getToken(token::endStatement);
            init->elements.erase(init->elements.begin() + idx);
        } else if(verb.is("move")){
            // move <name | [N]>  <after X | before X | first | last>;
            int from = resolveElemRef();
            expression* moved = init->elements[from];
            init->elements.erase(init->elements.begin() + from);   // remove first; positions below are on the reduced list
            token clause = file.getToken();
            int pos = resolvePosition(clause.value);
            file.getToken(token::endStatement);
            init->elements.insert(init->elements.begin() + pos, moved);
        } else {
            parsingError(format("extend {0}: expected 'inject', 'remove', or 'move' but found '{1}'",
                                arrName, verb.originalValue.empty() ? verb.value : verb.originalValue));
        }
        verb = file.getToken();
    }
    return false;
}

// A member declaration whose type is written as an alias member of the object (`meter twice()`) or as a
// path to one (`ns.parts.ohms make()`): `tok` becomes that type.
void bglParser::typeFromMemberAliasOrPath(objectDef& obj, token& tok){
    if(!tok.is(eTokenType::identifier) || tok.isDataType()) return;
    if((file.peekToken().is(eTokenType::name) && !file.peekToken().is("operator")) || file.peekToken().value == "|"){   // `T | string Get()`
        for(typeMember* m : obj.members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd != nullptr && vd->name == tok.value
               && (vd->isAlias || (vd->isValueAlias && vd->declaredExpressionValue == nullptr))
               && languageService.isClassType(vd->type.name)){
                tok.value = vd->type.name; tok.originalValue = vd->type.dName(); tok.tokenType = eTokenType::dataType;
                return;
            }
        return;
    }
    // A path: names joined by `.`, then the member's name.
    int k = 1;
    string path = tok.value;
    while(file.peekToken(k).is(token::period) && file.peekToken(k + 1).is(eTokenType::name)){ path += "." + file.peekToken(k + 1).value; k += 2; }
    if(k == 1 || !file.peekToken(k).is(eTokenType::name)) return;
    string t = resolveNamespacedType(path);
    if(t.empty()) return;
    for(int i = 1; i < k; i++) file.getToken();
    tok.value = t; tok.originalValue = languageService.getType(t).dName(); tok.tokenType = eTokenType::dataType;
}

// ===============================================================================
// extend object { ... } - add members to an existing object
// ===============================================================================
// Parse one member of an `extend <object>` body — a single iteration of that member loop. On entry
// `tok` is the member's first token; on return it is the next member's first token (or '}').
// `extendBlockPriority` / `extendHadReplaceGrammar` are the block-local verb-grammar state.
void bglParser::parseExtendMember(objectDef* obj, verbObjectDef* vod, token& tok, token nameTok, bool isExternalObj, int& extendBlockPriority, int verbPriorityDefault, bool& extendHadReplaceGrammar){
    markMemberStart(tok);
    rejectAccessModifier(tok);
    if(tok.is(eTokenType::eof)) parsingError("Unexpected end of file inside object — missing closing '}'");
    if(tok.is(eTokenType::directive)){
        if(tok.is("#i6")) processI6InlineMember(*obj);
        else if(tok.is("#if") || tok.is("#elif") || tok.is("#else") || tok.is("#endif")) processDirective(tok, *obj);
        else parsingError(format("Unsupported directive in extend body: '{0}'", tok.value));
        tok = file.getToken();
        return;
    }
    Qualifiers q = parseQualifiers(tok);
    bool memberIsReplace = q.isReplace;
    if(q.isExtern)  parsingError("'extern' is not valid inside an extend body");
    if(q.isExtend)  parsingError("'extend' is not valid inside an extend body");
    if(q.isAlias){
        parseAliasMember(tok, obj->members, "extend body");
        tok = file.getToken();
        return;
    }
    if(q.isDefault) parsingError("'default' is only valid in class declarations, not object instances");
    typeFromMemberAliasOrPath(*obj, tok);
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
            else if(tok.value == "synonyms" && peekOp.is(token::assignment))
                isAllowed = true;  // `synonyms = { ... }` → I6 `Verb 'w' = 'anchor';` — recognized
                                   // regardless of verb resolution; the handler below gives a
                                   // targeted error if the extend target isn't a verb
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
        // `emitter auto name = Class;` aliases an *emitter class* (all methods inlined, no runtime
        // object). For a compile-time alias to a real *object* — no runtime property — use
        // `alias name = objectInstance;` (see parseAliasMember) instead; `emitter` would be a
        // misnomer there since the target is not an emitter.
        if(tok.value == "auto"){
            token aliasName = file.getToken({eTokenType::identifier, eTokenType::dataType});
            file.getToken(token::assignment);
            token rhs = file.getToken({eTokenType::identifier, eTokenType::dataType});
            classDef* rhsCls = languageService.findClass(rhs.value);
            if(!rhsCls)
                parsingError(format("'emitter auto' alias member '{0}': '{1}' is not a declared class "
                    "(use `alias {0} = {1};` for an object)",
                    (string)aliasName, rhs.originalValue.empty() ? rhs.value : rhs.originalValue));
            file.getToken(token::endStatement);
            variableDeclaration& aliasDef = *(new variableDeclaration());
            aliasDef.name = (string)aliasName;
            aliasDef.displayName = aliasName.originalValue;
            aliasDef.src = aliasName.src;
            aliasDef.type = languageService.getType(rhs.value);
            aliasDef.isExternal = true;  // no I6 emission
            aliasDef.isValueAlias = true;   // a namespace hook: a path through it redirects to the class
            obj->members.push_back(&aliasDef);
            tok = file.getToken();
            return;
        }
        token retType = tok;
        token propName = file.getToken({eTokenType::identifier, eTokenType::dataType});
        if(propName.is("operator")){
            token opTok = file.getToken();
            if(opTok.is(token::parenOpen)) propName.value = "operator()";
            else if(opTok.is(token::bracketOpen)){
                file.getToken(token::bracketClose);
                propName.value = "[]";
                if(file.peekToken().is(token::assignment)){ file.getToken(); propName.value = "[]="; }
            }
            else if(opTok.is("?")) propName.value = "?";
            else if(opTok.is("switch")) propName.value = "switch";
            else { opTok.assert(eTokenType::oper); propName.value = opTok.value; }
        }
        functionDef& funcDef = *(new functionDef());
        funcDef.name = (string)propName;
        funcDef.displayName = propName.originalValue;
        funcDef.src = propName.src.line > 0 ? propName.src : file.currentLocation();
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
                        if(fd->name == funcDef.name){ keepReplaced(funcDef, fd); obj->members[i] = &funcDef; replaced = true; break; }
            }
            if(!replaced) obj->members.push_back((typeMember*)&funcDef);
        }
    } else if((tok.value == "array" || tok.value == "rawarray") && !genericTypeIsUnionHead())
        processArrayMember(obj->members, obj->dName(), dynamic_cast<verbObjectDef*>(obj), obj, &q, tok.value == "rawarray", tok.docComment);
    else if(tok.isDataType()){
        // Check for += / -= compound assignment on a typed member
        token peekName = file.peekToken();
        token peekOp = file.peekToken(2);
        if((peekOp.is("+=") || peekOp.is("-=")) && peekName.is(eTokenType::identifier) && !peekName.is("operator")){
            token memberName = file.getToken();
            token op = file.getToken();
            processExtendCompoundAssignment(*obj, memberName, op.value, vod, extendBlockPriority);
        } else {
            processTypedMember(*obj, tok, memberIsReplace, q.isRef);
        }
    }
    else if(tok.is(eTokenType::identifier)){
        // Check for += / -= compound assignment (inferred type)
        token peekOp = file.peekToken();
        if(peekOp.is("+=") || peekOp.is("-=")){
            // In an `extend` body the grammar operators are explicit: `grammar += { ... }`
            // appends, `grammar -= { ... }` removes (matching lines; warns if none match).
            // Destructive whole-verb replacement is the separate `replace grammar = { ... }`.
            // A bare `grammar = { ... }` is rejected below so an append/replace can't be
            // silently confused. The grammarrulelist +=/-= logic lives in
            // processExtendCompoundAssignment (which also rejects -= on extern verbs).
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
            // In an `extend` body, only `replace grammar = { ... }` uses `=` (destructive
            // whole-verb replace). A BARE `grammar = { ... }` is rejected: append is `+=`,
            // remove is `-=`. This forces intent so an `=` can never silently append-or-replace
            // (the reason `grammar =` append was retired). In a `verb` DECLARATION, `grammar =`
            // remains the (only) form — that path is handled elsewhere, not here.
            bool isReplace = memberIsReplace;
            if(!isReplace)
                parsingError("In an `extend` body, `grammar = { ... }` is not allowed. Use `grammar += { ... }` to append, `grammar -= { ... }` to remove, or `replace grammar = { ... }` to replace the whole verb.");
            // Priority is meaningless under replace; if the user also set a non-default
            // `priority = N;` in the same extend block, that's a contradiction.
            if(extendBlockPriority != verbPriorityDefault)
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
                gl.isReplaceMode = true;   // only the replace path reaches here now
                gtd->grammarLines.push_back(gl);
                vod->grammarLines.push_back(gl);
            }
            extendHadReplaceGrammar = true;
        } else if(tok.value == "synonyms" && peekOp.is(token::assignment)){
            // `synonyms = {.w1, .w2};` → I6 `Verb 'w1' 'w2' = 'anchor';`. The listed words become
            // TRUE ALIASES of the anchor verb's grammar table (a later `Extend` on the verb flows
            // to them automatically) — distinct from copying grammar lines. Works for extern
            // (stdlib) and Beguile-native verbs; emitted as a globals-level node so it lands after
            // the anchor's own `Verb` directive.
            //
            // Recognized in ANY extend body (not gated on verb resolution), so a `synonyms` line
            // on a non-verb target gets a targeted error below instead of falling through to the
            // misleading generic "'synonyms' is not a property …" message. The block is parsed
            // (tokens consumed) regardless, to avoid a cascade of follow-on errors.
            file.getToken(token::assignment);
            file.getToken(token::braceOpen);
            verbSynonymDecl& syn = *(new verbSynonymDecl());
            syn.anchorVerb = vod ? vod->name : obj->name;   // primary word resolved at emit
            if(!file.peekToken().is(token::braceClose)){
                while(true){
                    token w = file.getToken(eTokenType::dictionaryWord);
                    string e; for(char ch : w.value) e += (ch == '\'') ? '^' : ch;
                    string i6word;
                    if(w.isPlural)         i6word = "'" + e + "//p'";
                    else if(e.size() == 1) i6word = "'" + e + "//'";
                    else                   i6word = "'" + e + "'";
                    syn.synonymWords.push_back(i6word);
                    token sep = file.getToken({token::comma, token::braceClose});
                    if(sep.is(token::braceClose)) break;
                }
            }
            if(file.peekToken().is(token::endStatement)) file.getToken();
            if(vod == nullptr)
                parsingError(format("`synonyms = {{ ... }}` is only valid when extending a verb — '{0}' is not a verb", obj->dName()));
            else if(syn.synonymWords.empty())
                parsingError("`synonyms = { ... }` requires at least one trigger word");
            else
                languageService.globals.push_back(&syn);
        } else {
            processInheritedMember(*obj, tok);
        }
    }
    else
        parsingError(format("Unexpected token '{0}' in extend body", tok.value));
    tok = file.getToken();
}

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
        if(auto* od = languageService.findGlobalAs<objectDef>(lower)) obj = od;
    }
    // Not a verb or object — a previously-declared array? Route to the declarative
    // build-time array editor (inject / remove / move).
    if(!obj){
        if(auto* arr = languageService.findGlobalAs<arrayDeclaration>(lower)){
            if(arr->isPrePassStub)
                parsingError(format("extend '{0}': the array is declared further down; an array must be declared before it is extended.",
                                    nameTok.originalValue.empty() ? nameTok.value : nameTok.originalValue));
            return processArrayExtension(arr);
        }
    }
    if(!obj)
        parsingError(format("extend '{0}': no previously declared object or array with that name",
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
    objectDef* aliases = pushOwnTypeAliases(*obj);   // the object's own type aliases are bare names inside it
    while(tok.isNot(token::braceClose)){
        parseExtendMember(obj, vod, tok, nameTok, isExternalObj, extendBlockPriority, verbPriorityDefault, extendHadReplaceGrammar);
        syncOwnTypeAliases(*obj, aliases);
    }
    usingObjectImports.pop_back();

    closeCompileContext(eCompileContext::objectDef);
    currentObject = savedObject;

    // For verb objects: link handler() (the action body) and add grammar to globals if external
    if(vod){
        for(typeMember* m : vod->members)
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == "handler"){ vod->doFunc = fd; break; }
    }

    // Reconcile pre-scan namespace-redirect stubs: preScanExtendObjectMembers registers `auto`/
    // `alias`/`emitter auto` members as isPrePassStub variableDeclarations so dotted resolution
    // (bgl.asm, bgl.util.*) stays order-free during the main pass. Now that this extend's real
    // members have been added above, drop any stub whose real same-named member has landed — so the
    // object keeps exactly one member per name (no duplicate). A stub with no real sibling yet
    // (added by a different, not-yet-processed extend) is left in place until its own extend runs.
    for(auto it = obj->members.begin(); it != obj->members.end(); ){
        auto* vd = dynamic_cast<variableDeclaration*>(*it);
        if(vd && vd->isPrePassStub){
            bool hasReal = false;
            for(typeMember* m2 : obj->members)
                if(m2 != *it && !m2->isPrePassStub && m2->name == vd->name){ hasReal = true; break; }
            if(hasReal){ it = obj->members.erase(it); continue; }
        }
        ++it;
    }

    return false;
}

// Compound assignment (+= / -=) on an existing collection member inside an extend body.

// `grammar += { … }` inside an extend body: parse the grammar lines and append them to the
// object's grammarRuleListDecl (creating it if needed) and to the verb's line list.
void bglParser::extendGrammarRuleListAppend(objectDef& obj, const string& memberNameStr, verbObjectDef* vod, int blockPriority){
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
}

// `grammar -= { … }` inside an extend body: remove matching grammar. A spec with no pattern is
// word-level (evicts the whole word on an extern verb); a spec with a pattern removes that one line.
void bglParser::extendGrammarRuleListRemove(objectDef& obj, const string& memberNameStr, verbObjectDef* vod, bool isExternalObj){
    // -= : remove grammar. The GRAIN is set by how much of the line the spec names:
    //   • WORD-LEVEL  `{.w}`        (no pattern)  → remove ALL of that word's grammar.
    //   • LINE-LEVEL  `{.w, pat…}`  (with pattern) → remove the one exactly-matching line.
    // (A single word is the only "match-many" form; partial-pattern prefix matching is
    // deliberately NOT supported — see languageSpec / Verbs-Grammar for the rationale.)
    //
    // Removal is source-order: a `-=` only sees lines accumulated so far (base + earlier
    // extends), so a later `+=` of the same line is unaffected. parseGrammarLines expands
    // alternations, so each spec carries a single trigger word.
    //
    // On an EXTERN (library) verb the two grains behave very differently, because Beguile
    // has only partial sight of a library verb:
    //   • line-level  → can match only Beguile's OWN additions (isOwnLine == false); the
    //     library's original patterns are opaque, so a spec naming one can't match and warns.
    //   • word-level  → EVICTION. It doesn't need to see patterns: it records the word in
    //     evictedExternWords, and the emitter lowers it to I6 `Extend only 'w' replace`,
    //     which peels the whole word off the library verb (and lets a native verb reclaim it).
    vector<grammarLine> toRemove = parseGrammarLines();

    // Is `w` a trigger word this (extern) verb is known to claim? verbWords is populated at
    // emit time, so at parse time we read the extern verb's own declared lines (isOwnLine) and
    // its name (the bare-`extern verb V;` default). Used only for extern diagnostics/eviction.
    auto externClaims = [&](const string& w) -> bool {
        if(!vod) return false;
        for(const grammarLine& gl : vod->grammarLines)
            if(gl.isOwnLine && (gl.verbWord == w
                                || find(gl.additionalVerbWords.begin(), gl.additionalVerbWords.end(), w) != gl.additionalVerbWords.end()))
                return true;
        bool anyOwn = false;
        for(const grammarLine& gl : vod->grammarLines) if(gl.isOwnLine){ anyOwn = true; break; }
        return !anyOwn && w == vod->name;   // bare `extern verb V;` claims its own name
    };
    // Count Beguile-added lines (isOwnLine == false) for word `w`.
    auto beguileAddedCount = [&](const string& w) -> int {
        int n = 0;
        if(vod) for(const grammarLine& gl : vod->grammarLines)
            if(!gl.isOwnLine && gl.verbWord == w) n++;
        return n;
    };

    for(const grammarLine& rem : toRemove){
        bool wordLevel = rem.patternTokens.empty() && rem.additionalVerbWords.empty();
        string vn = obj.displayName.empty() ? obj.name : obj.displayName;

        // ---- EXTERN word-level: eviction via `Extend only 'w' replace` ----
        if(wordLevel && isExternalObj){
            bool claimed = externClaims(rem.verbWord);
            int  added   = beguileAddedCount(rem.verbWord);
            // Drop Beguile's own additions for this word — the eviction subsumes them.
            if(vod)
                vod->grammarLines.erase(
                    remove_if(vod->grammarLines.begin(), vod->grammarLines.end(),
                        [&](const grammarLine& gl){ return !gl.isOwnLine && gl.verbWord == rem.verbWord; }),
                    vod->grammarLines.end());
            for(typeMember* m : obj.members)
                if(auto* g = dynamic_cast<grammarRuleListDecl*>(m))
                    if(g->name == memberNameStr)
                        g->grammarLines.erase(
                            remove_if(g->grammarLines.begin(), g->grammarLines.end(),
                                [&](const grammarLine& gl){ return gl.verbWord == rem.verbWord; }),
                            g->grammarLines.end());
            if(claimed || added > 0)
                languageService.evictedExternWords.insert(rem.verbWord);
            else
                parsingWarning(format("grammar -= on extern verb '{0}': '{1}' is not a word this verb claims and Beguile added no grammar for it, so nothing was evicted. Check the trigger word.", vn, rem.verbWord));
            continue;
        }

        // ---- NATIVE word-level, or LINE-level (native or extern) ----
        auto matchesSpec = [&](const grammarLine& gl){
            // Extern line-level: only Beguile's OWN additions are matchable.
            if(isExternalObj && gl.isOwnLine) return false;
            if(gl.verbWord != rem.verbWord) return false;
            if(wordLevel) return true;                              // whole word: ignore pattern
            return gl.additionalVerbWords == rem.additionalVerbWords
                && gl.patternTokens == rem.patternTokens
                && gl.isReverse == rem.isReverse;
        };
        int matched = 0;
        if(vod){
            for(const grammarLine& gl : vod->grammarLines) if(matchesSpec(gl)) matched++;
        } else {
            for(typeMember* m : obj.members)
                if(auto* g = dynamic_cast<grammarRuleListDecl*>(m))
                    if(g->name == memberNameStr)
                        for(const grammarLine& gl : g->grammarLines) if(matchesSpec(gl)) matched++;
        }
        for(typeMember* m : obj.members)
            if(auto* g = dynamic_cast<grammarRuleListDecl*>(m))
                if(g->name == memberNameStr)
                    g->grammarLines.erase(
                        remove_if(g->grammarLines.begin(), g->grammarLines.end(),
                            [&](const grammarLine& gl){ return matchesSpec(gl); }),
                        g->grammarLines.end());
        if(vod)
            vod->grammarLines.erase(
                remove_if(vod->grammarLines.begin(), vod->grammarLines.end(),
                    [&](const grammarLine& gl){ return matchesSpec(gl); }),
                vod->grammarLines.end());
        // A grammar object's rules for this verb, declared earlier, are its lines too (§13.4.5).
        if(vod)
            for(typeDef* g : languageService.globals)
                if(auto* list = dynamic_cast<grammarRuleListDecl*>(g)){
                    auto forThisVerb = [&](const grammarLine& gl){
                        string target = gl.targetVerb.empty() ? list->verbName : gl.targetVerb;
                        transform(target.begin(), target.end(), target.begin(), ::tolower);
                        return target == vod->name && matchesSpec(gl);
                    };
                    for(const grammarLine& gl : list->grammarLines) if(forThisVerb(gl)) matched++;
                    list->grammarLines.erase(remove_if(list->grammarLines.begin(), list->grammarLines.end(), forThisVerb),
                                             list->grammarLines.end());
                }

        if(matched == 0){
            string patStr;
            for(size_t i = 0; i < rem.patternTokens.size(); i++){ if(i) patStr += " "; patStr += rem.patternTokens[i]; }
            if(patStr.empty()) patStr = "(no pattern)";
            if(isExternalObj){
                // Two-layer diagnostic: distinguish "opaque library grammar" (word IS claimed —
                // point the user at word-level eviction) from a genuine typo (word not claimed).
                if(externClaims(rem.verbWord))
                    parsingWarning(format("grammar -= on extern verb '{0}': no Beguile-added grammar matches `'{1}' * {2}`. '{1}' is a library word whose own grammar Beguile can't see line-by-line — use `grammar -= {{ {{.{1}}} }}` to evict the whole word, or check your pattern.", vn, rem.verbWord, patStr));
                else
                    parsingWarning(format("grammar -= on extern verb '{0}': '{1}' is not a word this verb claims and Beguile added no grammar line `'{1}' * {2}`, so nothing was removed. Check the trigger word.", vn, rem.verbWord, patStr));
            } else if(wordLevel){
                parsingWarning(format("grammar -= on '{0}': no grammar for word `'{1}'` exists, so nothing was removed. Check the trigger word, or the order relative to the matching `grammar +=`.", vn, rem.verbWord));
            } else {
                parsingWarning(format("grammar -= on '{0}': no grammar line `'{1}' * {2}` exists, so nothing was removed. Check the trigger word and pattern, or the order relative to the matching `grammar +=`.", vn, rem.verbWord, patStr));
            }
        }
    }
}

// `array<T> += / -= { … }` inside an extend body: parse the element list and add it to, or remove
// it (by rendered text) from, the existing array member's baked initializer list.
void bglParser::extendArrayCompoundAssignment(objectDef& obj, const string& memberNameStr, const string& op){
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
        if(obj.objectClass)
            if(typeMember* m = obj.objectClass->findMember([&](typeMember* mm){
                    auto* vd = dynamic_cast<variableDeclaration*>(mm);
                    return vd != nullptr && vd->name == memberNameStr;
               }))
                memberType = dynamic_cast<variableDeclaration*>(m)->type.name;
    }
    if(memberType.empty())
        parsingError(format("'{0}' is not a member of this object; cannot use {1}", display, op));

    // -= on extern objects is not allowed — we don't control their original data. The ONE exception
    // is grammar: Beguile can `grammar +=` new lines onto a library verb (emitted as I6 `Extend`),
    // and it tracks exactly what it added — so it can remove ITS OWN additions again (`grammar -=`
    // just drops that `Extend`). It still cannot touch the library's original grammar (I6 has no
    // remove-a-line primitive, and Beguile doesn't know those patterns); the grammar `-=` handler
    // enforces that by matching only Beguile-added lines and warning otherwise.
    bool isExternalObj = obj.isExternal || (vod && vod->isExternal);
    if(isExternalObj && op == "-=" && memberType != "grammarrulelist")
        parsingError(format("Cannot use -= on extern object '{0}'; the original data is defined externally",
            obj.displayName.empty() ? obj.name : obj.displayName));

    // grammarRuleList += / -=
    if(memberType == "grammarrulelist"){
        if(op == "+="){
            extendGrammarRuleListAppend(obj, memberNameStr, vod, blockPriority);
        } else {
            extendGrammarRuleListRemove(obj, memberNameStr, vod, isExternalObj);
        }
    }
    // attributeList: only `=` is supported. Use `attributes = {a, b, !c}` to
    // set/replace and `!attr` to negate an inherited attribute (maps directly to
    // I6's `has ~attr`). `+=`/`-=` don't compose with extend/class semantics, so
    // they are rejected here.
    else if(memberType == "attributelist"){
        parsingError(format(
            "'{0} {1}': '{2}' is an attributeList, which only accepts `= {{a, b, !c}}`. "
            "To negate an inherited attribute use prefix `!`, e.g. `attributes = {{light, !scenery}}`. "
            "For runtime changes use give()/ungive().",
            display, op, display));
    }
    // array<T> += / -=
    else if(memberType == "array"){
        extendArrayCompoundAssignment(obj, memberNameStr, op);
    }
    else {
        parsingError(format("Type '{0}' does not support {1} in extend body", memberType, op));
    }

    if(file.peekToken().is(token::endStatement)) file.getToken();
}

//===============================================================================================================================
// Grammar declarations
//===============================================================================================================================

// A member's errors are placed from its own first token, not from the declaration that holds it.
void bglParser::markMemberStart(token& tok){
    if(tok.src.line <= 0 || tok.is(eTokenType::eof)) return;
    currentStatementSrc = tok.src;
    currentStatementSrc.col = max(1, tok.src.col - (int)tok.originalValue.size());
}

// A base names no class: say what it does name, when it names something.
void bglParser::unknownBaseError(token& base){
    string shown = base.originalValue.empty() ? base.value : base.originalValue;
    if(auto* od = languageService.findGlobalAs<objectDef>(base.value))
        parsingError(format("'{0}' is an object, not a class; a class derives from a class{1}.", shown,
            od->objectClass ? " (" + shown + "'s class is '" + od->objectClass->dName() + "')" : string()));
    if(languageService.findEnum(base.value) != nullptr)
        parsingError(format("'{0}' is an enum; a class derives only from a class.", shown));
    parsingError(format("Unknown base class '{0}'", shown));
}

// `public int size;`: members have no access modifiers.
void bglParser::rejectAccessModifier(token& tok){
    if(tok.is("public") || tok.is("private") || tok.is("protected") || tok.is("internal"))
        parsingError(format("Beguile has no access modifiers ('{0}'): every member is visible; drop it.", tok.value));
}

// A data member and a method of one name (Beguile names ignore case) are one Inform 6 property, so a
// class, with its bases, can't have both.
void bglParser::rejectMethodBesideField(classDef& cls, const string& name, const string& shown, bool addingMethod){
    for(classDef* c = &cls; c != nullptr; c = c->baseClasses.empty() ? nullptr : c->baseClasses.front())
        for(typeMember* m : c->members){
            if(m->name != name) continue;
            // A method over an inherited field gives that property its routine (`short_name`), as I6 allows.
            if(addingMethod && c != &cls) continue;
            // In one class a static of the name clashes too: `Bin.adds` could be either.
            bool sameClass = c == &cls;
            bool clash = addingMethod
                ? [&]{ auto* vd = dynamic_cast<variableDeclaration*>(m);
                       return vd && (sameClass || !vd->isStatic) && !vd->isAlias && !vd->isValueAlias && !vd->isExternal; }()
                : [&]{ auto* fd = dynamic_cast<functionDef*>(m); return fd && !fd->isEmitter && (sameClass || !fd->isStatic); }();
            if(clash)
                parsingError(format(kindWord(&cls) + " '{0}': {1} '{2}' has the name of {3} '{4}'{5}; a class's member and "
                                    "method can't share a name, so rename one.", cls.dName(), addingMethod ? "method" : "member", shown,
                                    addingMethod ? "member" : "method", m->dName(),
                                    c == &cls ? string() : " of class '" + c->dName() + "'"));
        }
}

bool isHeaderMember(const string& name);

// Each struct without a copy operator of its own gets one that copies every field: `=` for a field (which
// copies a struct, re-points a class, shares an array), `:=` for a `ref` field.
void bglParser::writeAutoCopyOperators(){
    string text;
    for(classDef* cls : autoCopyStructs){
        string nm = cls->dName(), body;
        for(typeMember* m : cls->members){
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            if(vd == nullptr || vd->isStatic || vd->isConst || vd->isAlias || vd->isValueAlias || vd->isExternal
               || vd->isPrePassStub || isHeaderMember(vd->name) || vd->type.name.rfind("_bglaccessor", 0) == 0) continue;
            string f = vd->dName();
            body += f + (vd->isRefLocal ? " := o." : " = o.") + f + "; ";
        }
        text += "extend struct " + nm + " { replace " + nm + " operator = (" + nm + " o) { " + body + "return self; } }\n";
    }
    autoCopyStructs.clear();
    if(text.empty()) return;
    file.openText(text, "<struct copy operators>", 1);
    try { while(processNextStatement() == false){} } catch(...) { file.close(); throw; }
    file.close();
}
