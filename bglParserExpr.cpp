#include <regex>
#include "platform.h"
// ═══════════════════════════════════════════════════════════════════════════════
// bglParserExpr.cpp — expression-level parsing for the Beguile compiler.
//
// All entry points are member methods of bglParser; this file holds their
// definitions only.
//
// parseExpression() branch map (search for these section markers):
//   PARENS          — open/close paren, lambda detection, cast prefix
//   LITERALS        — int, string, char, dictionary word
//   IDENTIFIER      — the largest block: subscript, function call, optional chain,
//                     dot-access, postfix query, identifier fallback
//   NULL COALESCING — ??
//   BINARY OPERATOR — emitter dispatch via applyBinaryOperator()
//   DOT CHAINING    — .method() / .property on resolved expression
//   TERNARY         — ? : operator
//   DIRECTIVE       — #beguilerSettings.property references
//
// Public-facing helpers in this file:
//   parseLambdaExpr            — lift `(args) => expr` into a global functionDef
//   applyBinaryOperator        — operator emitter resolution + RHS parsing
//   parseExpression            — the main expression parser
//   parseExprFunctionCall      — name(args) handling
//   parseExprPrefixNot         — prefix `!`
//   parseExprTernary           — `?:` ternary lowering
//   parseExprNullCoalescing    — `??` lowering
//
// File-local statics:
//   operatorPrecedence, kPrecedenceOps  — operator precedence table
// ═══════════════════════════════════════════════════════════════════════════════
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
#include "i6Emitter.h"
#include "bglParser.h"
#include "fileLexer.h"
#include "token.h"
#include "bglLanguageService.h"
#include "bglParserHelpers.h"

using namespace std;


// ═══════════════════════════════════════════════════════════════════════════════

// Substitute a property-class member's operator() read emitter — see bglParser.h. Generic:
// works for any property-class (parentProp, etc.); the emitter body lives in the BLR.
string bglParser::applyPropertyClassRead(const string& objText, const string& memberType, string& outRetType,
                                         const ArrayReceiver* arr){
    outRetType = "";
    classDef* cls = getDispatchClass(memberType);
    if(cls == nullptr || !cls->isEmitterClass || cls->isPrimitive) return "";
    typeMember* found = findMemberInHierarchy(cls, [this](typeMember* m){ return isPropertyClassReadEmitter(m); });
    if(found == nullptr) return "";
    auto* fn = dynamic_cast<functionDef*>(found);
    auto* blk = dynamic_cast<i6Block*>(fn->body);
    emitterBindings bind; bind.self = objText; bind.val = objText; bind.trim = emitterTrim::wsSemi;
    // On an array the owner is the array, addressed as its emitters expect: (owner, prop) for a
    // member array, (array, 0) otherwise.
    if(arr != nullptr){
        if(arr->isMember){ bind.self = arr->owner; bind.prop = arr->prop; }
        else bind.prop = "0";
    }
    string b = expandEmitterBody(blk, bind);
    if(b.empty()) return "";
    outRetType = fn->returnType.name;
    return b;
}

// `acc += 3` / `acc++` as an expression, where the accessor's class declares that operator: the
// operator gets the accessor itself, not the value its getter reads.
bool bglParser::nextOperatorIsOwn(ExprParseState& st, const string& memberType){
    // Compound and step operators, and the binary ones: an accessor's class's own operator applies to
    // the member, ahead of the value its getter reads (§4.4). The root classes' operators don't count.
    static const set<string> ops = {"+=", "-=", "*=", "/=", "%=", "|=", "&=", "^=", "<<=", ">>=", "++", "--",
                                    "+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>",
                                    "==", "!=", "?=", "=~", "<", ">", "<=", ">=", "<=>", "?", "?.", "??"};
    token next = st.prefetched ? *st.prefetched : file.peekToken();
    if(next.value == "?" && !queryIsPostfix(st.prefetched ? file.peekToken() : file.peekToken(2))) return false;
    return ops.count(next.value) && declaresOwnOperator(memberType, next.value);
}

// After a `?`: true when no operand follows, so the `?` is the postfix query, not a ternary.
bool bglParser::queryIsPostfix(token after){
    static const set<string> enders = {")", ";", ",", "]", "}", "&&", "||", "?", ":", "??"};
    return after.is(eTokenType::eof) || enders.count(after.value) > 0;
}

// True when a value class declares `op` taking an operand of its own class.
bool bglParser::takesOwnOperand(classDef* cls, const string& op){
    if(cls == nullptr || !isValueClass(cls)) return false;
    bool ordering = op == "<" || op == ">" || op == "<=" || op == ">=";
    auto isA = [&](classDef* pc){   // the class or one of its ancestors: an inherited operator takes it too (not the roots')
        if(pc == nullptr || pc->name == "_bglobject" || pc->name == "_bglprimitive") return false;
        for(classDef* c = cls; c != nullptr; c = c->baseClasses.empty() ? nullptr : c->baseClasses[0]) if(c == pc) return true;
        return false;
    };
    return findMemberInHierarchy(cls, [&](typeMember* m){
        auto* f = dynamic_cast<functionDef*>(m);
        if(f == nullptr || (f->name != op && !(ordering && f->name == "<=>"))) return false;
        if(!f->isStatic) return f->params.size() == 1 && isA(getDispatchClass(f->params[0]->type.name));
        return f->params.size() == 2 && isA(getDispatchClass(f->params[1]->type.name));   // `static operator <=>(T a, T b)`
    }) != nullptr;
}

bool bglParser::declaresOwnOperator(const string& type, const string& opIn){
    classDef* mcls = getDispatchClass(type);
    if(mcls == nullptr) return false;
    const string op = opIn == "?." || opIn == "??" ? "?" : opIn;   // `?.` and `??` test with the class's `operator ?`
    auto declares = [&](const string& name){
        return findMemberInHierarchy(mcls, [&](typeMember* m){
            if(dynamic_cast<functionDef*>(m) == nullptr || m->name != name) return false;
            classDef* owner = mcls->declaringClass(m);
            return owner == nullptr || (owner->name != "_bglobject" && owner->name != "_bglprimitive"); }) != nullptr;
    };
    if(declares(op)) return true;
    // An ordering the class derives from its `<=>` is its own too.
    return (op == "<" || op == ">" || op == "<=" || op == ">=") && declares("<=>");
}

// A type's emitter `operator ?`: an object declared as its own type first, then its class.
functionDef* bglParser::findQueryOp(classDef* cls, const string& type){
    auto fits = [](typeMember* m){
        auto* fn = dynamic_cast<functionDef*>(m);
        return fn && fn->name == "?" && fn->isEmitter && fn->params.empty() && dynamic_cast<i6Block*>(fn->body) != nullptr;
    };
    if(!type.empty())
        if(objectDef* od = languageService.findObjectType(type))
            for(typeMember* m : od->members) if(fits(m)) return dynamic_cast<functionDef*>(m);
    return cls != nullptr ? dynamic_cast<functionDef*>(findMemberInHierarchy(cls, fits)) : nullptr;
}

// An object named bare, as I6 knows it: one named by a word I6 reserves (`box`) emits as `_box`.
string bglParser::objectI6Name(const string& name){
    if(name.find('.') != string::npos) return name;
    string low = name; transform(low.begin(), low.end(), low.begin(), ::tolower);
    // A parameter or local of that name is the variable, not the object or verb.
    if(currentFunc != nullptr && (qualifyFromParams(low, currentFunc) || qualifyFromAncestorBlocks(low, nullptr)
                                  || qualifyFromBodyLocals(low, dynamic_cast<statementBlock*>(currentFunc->body))))
        return name;
    if(auto* od = dynamic_cast<objectDef*>(&languageService.getType(low)); od != nullptr && !od->i6name.empty()) return od->i6name;
    for(verbObjectDef* v : languageService.verbs) if(v->name == low && !v->i6name.empty()) return v->i6name;
    // A global variable emitted under another name (`held` → `_held`).
    if(auto* vd = dynamic_cast<variableDeclaration*>(languageService.findGlobal(low)); vd != nullptr && !vd->i6name.empty())
        return vd->i6name;
    return name;
}

bglParser::ArrayReceiver bglParser::arrayReceiver(string& objName, const string& rawObjName, const string& objType,
                                                 functionDef* func, statementBlock* body){
    ArrayReceiver r;
    if(!isWordArrayType(objType)) return r;
    size_t d = objName.rfind('.');
    if(d != string::npos){ r.owner = objName.substr(0, d); r.prop = objName.substr(d + 1); r.isMember = true; }
    else r.isMember = splitQualifiedMember(rawObjName, func, body, r.owner, r.prop);
    // A `ref` member holds a POINTER to an array owned elsewhere: address it as a value, through
    // the property READ where the pointer lives (splitQualifiedMember yields `self` for a bare
    // member inside a body, so this joins to `self.nums`, not the raw `nums`).
    if(r.isMember && memberArrayIsRef(r.owner, r.prop, func, body)){
        string q = r.owner.find('.') == string::npos && func != nullptr ? qualifyIdentifier(r.owner, func, body) : "";
        objName = objectI6Name(q.empty() ? r.owner : q) + "." + propertyI6Name(r.prop);   // a lambda reads its capture
        r.isMember = false;
    }
    // A static array reached through an instance (`level.history` in an accessor) is one global array.
    if(r.isMember){
        string owner = r.owner.rfind("self.", 0) == 0 ? r.owner.substr(5) : r.owner;
        string ot = owner == "self" ? "" : owner.find('.') != string::npos ? resolvePathType(owner, func, body)
                                                                           : resolveIdentifierType(owner, func, body);
        if(classDef* c = ot.empty() ? nullptr : getDispatchClass(ot)){
            typeMember* sm = findMemberInHierarchy(c, [&](typeMember* m){
                auto* ad = dynamic_cast<arrayDeclaration*>(m); return ad && ad->isStatic && ad->name == r.prop; });
            if(auto* ad = dynamic_cast<arrayDeclaration*>(sm)){
                objName = "_bgl_" + c->declaringClass(ad)->name + "_" + r.prop;
                r.isMember = false;
                r.staticElemType = ad->elementType;
            }
        }
    }
    return r;
}

// A tracked member spends its LAST word on the length: capacity is one less than the property
// holds, and the length is read from that slot. A rawArray or dictionaryWord array keeps the bare
// layout, where length == size. Must agree with the emitter's member-array layout.
// An element of an array of a value class, reached for a member: the slot's own instance, drawn from
// the class's pool if nothing has been stored there yet. `a-->(i)` becomes `_bglValSlot_C(a, i)`.
string bglParser::valueSlotText(const string& elemText, const string& elemType){
    classDef* cls = elemType.empty() ? nullptr : languageService.findClass(elemType);
    // A value class's elements own instances, whether or not the array is declared yet (a use above it).
    if(cls == nullptr || (!languageService.valueElementArrays.count(cls) && !(isValueClass(cls) && !cls->isEmitterClass)))
        return elemText;
    size_t at = string::npos;
    int depth = 0;
    for(size_t i = 0; i + 2 < elemText.size(); i++){
        char c = elemText[i];
        if(c == '(') depth++;
        else if(c == ')') depth--;
        else if(depth == 0 && elemText.compare(i, 3, "-->") == 0) at = i;
    }
    if(at == string::npos) return elemText;
    languageService.valueSlotClasses.insert(cls);
    string base = elemText.substr(0, at), idx = elemText.substr(at + 3);
    auto trim = [](string t){ size_t a = t.find_first_not_of(" "), b = t.find_last_not_of(" ");
                              return a == string::npos ? t : t.substr(a, b - a + 1); };
    return "_bglValSlot_" + cls->dName() + "(" + trim(base) + ", " + trim(idx) + ")";
}

// The same for an $i6Expr placeholder standing for a member array, with `$prop` left for the emitter's binding.
static string i6ExprMemberSizeText(const string& placeholder, const string& which, const string& type){
    string words = "((" + placeholder + ".#$prop)/WORDSIZE)";
    if(type.rfind("rawarray", 0) == 0) return words;
    return which == "size" ? "(" + words + " - 1)" : "(" + placeholder + ".&$prop-->(" + words + " - 1))";
}

string bglParser::memberArraySizeText(const ArrayReceiver& r, const string& which,
                                      functionDef* func, statementBlock* body){
    string owner = objectI6Name(r.owner);
    string words = "((" + owner + ".#" + propertyI6Name(r.prop) + ")/WORDSIZE)";
    if(r.decl ? !memberArrayDeclTracked(r.decl) : !memberArrayIsTracked(r.owner, r.prop, func, body)) return words;
    return which == "size" ? "(" + words + " - 1)"
                           : "(" + owner + ".&" + propertyI6Name(r.prop) + "-->(" + words + " - 1))";
}

string bglParser::expandMemberValueEmitter(string objText, const string& rawRecv, const string& recvType,
                                           const string& memberName, functionDef* func, statementBlock* body,
                                           string& outRetType, arrayDeclaration* memberDecl){
    auto isValueEmitterNamed = [&](typeMember* m){
        auto* fd = dynamic_cast<functionDef*>(m);
        return fd && fd->name == memberName && fd->isEmitter && fd->isValueEmitter;
    };
    // An object declared as its own type carries its emitters among its own members.
    functionDef* ve = nullptr;
    if(auto* od = languageService.findObjectType(recvType))
        for(typeMember* m : od->members) if(isValueEmitterNamed(m)){ ve = dynamic_cast<functionDef*>(m); break; }
    classDef* rc = getDispatchClass(recvType);
    if(ve == nullptr && rc == nullptr) return "";
    if(ve == nullptr) ve = dynamic_cast<functionDef*>(findMemberInHierarchy(rc, isValueEmitterNamed));
    auto* blk = ve ? dynamic_cast<i6Block*>(ve->body) : nullptr;
    if(blk == nullptr) return "";
    outRetType = ve->returnType.name;
    // An $i6Expr placeholder for a member array: its inline layout, `$prop` left for the emitter's own binding.
    if(i6ExprMemberArrays.count(rawRecv) && (memberName == "size" || memberName == "length"))
        return i6ExprMemberSizeText(objText, memberName, recvType);
    ArrayReceiver arr = arrayReceiver(objText, rawRecv, recvType, func, body);
    if(arr.isMember) arr.decl = memberDecl;
    if(arr.isMember && (memberName == "size" || memberName == "length"))
        return memberArraySizeText(arr, memberName, func, body);
    emitterBindings vb;
    vb.self = arr.isMember ? arr.owner : objText;
    vb.val  = objText;
    vb.host = objText.substr(0, objText.rfind('.') == string::npos ? objText.size() : objText.rfind('.'));
    if(isWordArrayType(recvType)) vb.prop = arr.isMember ? arr.prop : "0";
    vb.selfType = recvType;
    if(rc != nullptr) vb.cls = rc->i6Name();
    vb.trim = emitterTrim::wsSemi;
    return expandEmitterBody(blk, vb);
}

// Fluent property-class read chain (IDENT.m1.m2… → nested reads). See bglParser.h. Peek-only until
// it commits; consumes the chain tokens and returns true only when the whole member path is
// property-class reads. Generic — the member set comes from the BLR-defined property-classes.
bool bglParser::tryConsumePropertyClassReadChain(token first, functionDef* func, statementBlock* body,
                                                 string& outEmission, string& outType){
    if(!first.is(eTokenType::identifier)) return false;
    if(!file.peekToken(1).is(token::period)) return false;
    // Collect the dotted path via peek (no consumption yet), mirroring tryConsumeNamespacedEnumValue.
    vector<string> segments; segments.push_back(first.value);
    int peekIdx = 1;
    while(file.peekToken(peekIdx).is(token::period)){
        token seg = file.peekToken(peekIdx + 1);
        if(!seg.is(eTokenType::identifier)) break;
        // A method call (seg followed by '(') isn't a property-class read member — stop before it.
        if(file.peekToken(peekIdx + 2).is(token::parenOpen)) break;
        segments.push_back(seg.value);
        peekIdx += 2;
    }
    // Need head + at least TWO member reads (grandparent or deeper). A single read (obj.parent) is
    // already handled by the well-tested property-access branch — don't disturb it here.
    if(segments.size() < 3) return false;
    string accumType = resolveIdentifierType(first.value, func, body);
    if(accumType.empty()) return false;
    string accumText = (func != nullptr) ? qualifyIdentifier(first.value, func, body) : first.value;
    if(accumText.empty()) accumText = first.value;
    for(size_t i = 1; i < segments.size(); i++){
        classDef* cls = getDispatchClass(accumType);
        if(cls == nullptr) return false;
        typeMember* mem = findMemberInHierarchy(cls, [&](typeMember* m){
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            return vd != nullptr && vd->name == segments[i];
        });
        if(mem == nullptr) return false;
        string memberType = dynamic_cast<variableDeclaration*>(mem)->type.name;
        if(!isPropertyClassType(memberType)) return false;   // every member segment must be a property-class read
        string ret; string read = applyPropertyClassRead(accumText, memberType, ret);
        if(read.empty()) return false;
        accumText = read;
        accumType = ret;
    }
    // Commit: consume the chain tokens (periods + member identifiers) after `first`.
    int toConsume = 2 * (int)(segments.size() - 1);
    for(int k = 0; k < toConsume; k++) file.getToken();
    outEmission = accumText;
    outType = accumType;
    return true;
}

// ===============================================================================
// parseLambdaExpr - lift lambda expression to a global functionDef
// ===============================================================================
// Parse a lambda expression. The opening '(' has already been consumed — or, for the bare
// `=> body` form (bareArrow), the `=>` itself, and there is no parameter list.
// Builds a lifted global functionDef, adds it to languageService.globals, returns its name.
string bglParser::parseLambdaExpr(functionDef* outerFunc, statementBlock* outerBody, bool bareArrow){
    functionDef& fd = *(new functionDef());
    fd.name = format("_bglLambda_{0}", lambdaCounter++);
    fd.isLambda = true;
    fd.isEmitter = false;
    fd.isExternal = false;
    fd.src = file.currentLocation();

    // Parse parameter list: (type name, type name, ...) or ()
    token t = bareArrow ? token() : file.getToken(); // first type token or ')'
    while(!bareArrow && t.isNot(token::parenClose)){
        paramDef& p = *(new paramDef());
        if(t.is(eTokenType::identifier)) t = consumeTypeToken(t);   // an alias or dotted type path
        string typeName = t.value;
        if(typeName == "func") typeName = parseFuncType();
        else if(typeName == "array" || typeName == "rawarray") typeName = parseArrayTypeTail(typeName);
        p.type = languageService.getType(typeName);
        if(p.type.name.empty()) p.type.name = typeName; // for func<...> or unknown types
        token nameTok = file.getToken({eTokenType::identifier, eTokenType::dataType});   // a name may be spelled like a type
        p.name = nameTok.value;
        // A parameter that hides something warns, as a function's parameter does.
        {
            string shown = nameTok.originalValue.empty() ? nameTok.value : nameTok.originalValue;
            sourceLocation savedSrc = currentStatementSrc;
            if(nameTok.src.line > 0) currentStatementSrc = nameTok.src;
            // As a function's parameter does: a global variable, function, object or type of the name. A chain may be
            // read twice (a sized array's initializer), so each is said once.
            bool warned = false;
            for(typeDef* g : languageService.globals){
                if(g->name != p.name || warned) continue;
                if(auto* gv = dynamic_cast<variableDeclaration*>(g)){
                    const string& gt = gv->type.name;
                    if(gt == "grammartoken" || gt == "attribute" || gt == "property" || gt == "verb" || gv->isNamespaceAlias()) continue;
                }
                if(dynamic_cast<verbObjectDef*>(g) != nullptr) continue;
                if(auto* cd = dynamic_cast<classDef*>(g))
                    warnOnceHere("Parameter '" + shown + "' has the name of " + (cd->isUnion() ? "union '" : "class '") + g->dName()
                        + "'; in this lambda the name means the parameter, though it still names the type in declarations.");
                else
                    warnOnceHere("Parameter '" + shown + "' shadows global of the same name; '::" + shown + "' reaches the global.");
                warned = true;
            }
            if(!warned && definedSymbols.count(p.name))
                warnOnceHere("Parameter '" + shown + "' has the name of a #define symbol; in this lambda the name means the parameter.");
            if(!warned && outerFunc != nullptr){
                bool outer = false;
                for(paramDef* op : outerFunc->params) if(op->name == p.name) outer = true;
                if(!outer && (qualifyFromBodyLocals(p.name, outerBody) || qualifyFromAncestorBlocks(p.name, outerBody))) outer = true;
                if(outer) parsingWarning("Parameter '" + shown + "' shadows capturable local '" + shown + "' from enclosing function.");
            }
            currentStatementSrc = savedSrc;
        }
        fd.params.push_back(&p);
        t = file.getToken(); // ',' or ')'
        if(t.is(",")) t = file.getToken(); // advance to next type
    }

    // Consume =>
    if(!bareArrow) file.getToken("=>");
    synthesizeParamBackings(fd);   // a parameter of a copying class gets its own instance, as a function's does

    statementBlock* lambdaBody = new statementBlock();
    fd.body = lambdaBody;

    // Set up outer scope context for capture detection.
    // Use currentFunc (the real enclosing function) rather than outerFunc (which may be a
    // synthetic context created by for/while/if body processing). currentFunc's body contains
    // all locals at every nesting level; outerBody is the immediate enclosing block.
    functionDef* savedOuterFunc = lambdaOuterFunc;
    statementBlock* savedOuterBody = lambdaOuterBody;
    lambdaOuterFunc = currentFunc ? currentFunc : outerFunc;
    lambdaOuterBody = outerBody;
    // Push the outer function onto the stack so nested lambdas can chain through multiple levels
    if(lambdaOuterFunc != nullptr) lambdaOuterFuncStack.push_back(lambdaOuterFunc);
    // Save and clear the activeBlockStack — the lambda is a new function scope.
    // Outer-function locals should go through Tier 7 (capture) not Tier 1c (bare name).
    vector<statementBlock*> savedBlockStack = activeBlockStack;
    vector<statementBlock*> savedOuterBlocks = lambdaOuterBlockStack;
    lambdaOuterBlockStack = savedBlockStack;
    activeBlockStack.clear();

    token bodyStart = file.getToken();
    if(bodyStart.is(token::braceOpen)){
        // Block body
        functionDef* savedFunc = currentFunc;
        currentFunc = &fd;
        // Set-up the enclosing statement is still collecting (an earlier lambda's capture load) runs
        // before that statement, not in this body.
        vector<statement*> savedPending = pendingInjections, savedPost = postInjections;
        pendingInjections.clear(); postInjections.clear();
        openCompileContext(eCompileContext::codeBlock, lambdaBody);
        fd.isLambda = true;   // its first `return` sets the return type (processReturnExpr)
        while(processNextStatement(fd) == false){}
        closeCompileContext(eCompileContext::codeBlock);
        pendingInjections = savedPending; postInjections = savedPost;
        currentFunc = savedFunc;
        if(fd.returnType.name.empty()) fd.returnType.name = hasReturn(lambdaBody) ? "var" : "void";
    } else {
        // Single-expression body. Terminates at ';' (assignment RHS), ',' (next function
        // argument), or ')' (end of enclosing argument list). parseExpression's paren
        // tracking ensures inner `,`/`)` inside the body don't terminate prematurely.
        // For arg-position lambdas, the `,` or `)` terminator must be visible to the
        // enclosing arg parser — we stash it on the parser-level token slot, which the
        // caller's next getNext() will pick up before reading more from the lexer.
        functionDef* savedFunc = currentFunc;
        currentFunc = &fd;   // as for a block body: captures are recorded on the lambda
        // An interpolated-string argument (`=> print($"…")`) expands only in a call statement, so a
        // body holding one is parsed as that statement.
        bool hasInterpolation = false;
        bool assigns = false;   // `=> n += 1`: an assignment has no value, so the lambda is void
        static const set<string> assignOps = {"=", "+=", "-=", "*=", "/=", "%=", "|=", "&=", "^=", "<<=", ">>="};
        for(int k = 0, depth = 0; ; k++){
            token pk = k == 0 ? bodyStart : file.peekToken(k);
            if(pk.is(eTokenType::eof)) break;
            if(pk.is(token::parenOpen) || pk.is(token::braceOpen) || pk.is(token::bracketOpen)) depth++;
            else if(pk.is(token::parenClose) || pk.is(token::braceClose) || pk.is(token::bracketClose)){ if(depth == 0) break; depth--; }
            else if(depth == 0 && (pk.is(token::comma) || pk.is(token::endStatement))) break;
            else if(depth == 0 && assignOps.count(pk.value)) assigns = true;
            // An interpolation after an operator is an operand: the expression parse says it has no value.
            if(pk.is("$") && file.peekToken(k + 1).is(eTokenType::quote)){
                token before = k == 0 ? token() : k == 1 ? bodyStart : file.peekToken(k - 1);
                static const set<string> binary = {"+", "-", "*", "/", "%", "?", ":", "&&", "||", "==", "!=", "<", ">", "<=", ">=", "??"};
                hasInterpolation = !binary.count(before.value);
                break;
            }
        }
        if(hasInterpolation){
            vector<statement*> savedPending = pendingInjections, savedPost = postInjections;
            pendingInjections.clear(); postInjections.clear();
            bool savedBodyStmt = lambdaBodyStatement;
            lambdaBodyStatement = true;
            openCompileContext(eCompileContext::codeBlock, lambdaBody);
            processStatement(bodyStart, fd);
            closeCompileContext(eCompileContext::codeBlock);
            lambdaBodyStatement = savedBodyStmt;
            pendingInjections = savedPending; postInjections = savedPost;
            currentFunc = savedFunc;
            fd.returnType.name = "void";
            if(!stashedToken)
                parsingError("A lambda whose body passes an interpolated string must be a single call, "
                             "such as `=> print($\"…\")`; use a block body `=> { … }` otherwise.");
        } else {
            // Set-up the body's expression needs (a ternary's temp, a nested lambda's captures) runs
            // inside the lambda, not in the routine that declares it.
            vector<statement*> savedPending = pendingInjections, savedPost = postInjections;
            pendingInjections.clear(); postInjections.clear();
            // `:` ends a body that is the true branch of a ternary (a `?:` inside the body takes its own).
            bool savedAllowVoid = allowVoidReturnExpr;
            allowVoidReturnExpr = true;   // `=> shout(3)`: a void call is the body (below)
            expression* retExpr = nullptr;
            try {
                retExpr = parseExpression(bodyStart, {token::endStatement, token::comma, token::parenClose,
                                                      token::braceClose, ":"}, &fd, lambdaBody);   // `}`: last of a `{ … }` list
            } catch(...) { allowVoidReturnExpr = savedAllowVoid; throw; }
            allowVoidReturnExpr = savedAllowVoid;
            vector<statement*> innerSetUp = pendingInjections, innerAfter = postInjections;
            pendingInjections = savedPending; postInjections = savedPost;
            for(statement* inj : innerSetUp) lambdaBody->statements.push_back(inj);
            currentFunc = savedFunc;
            // Stash whatever terminator the inner parseExpression consumed so the enclosing
            // parser still sees it. Applies to all three: ';' (assignment RHS), ',' (next arg),
            // ')' (close of enclosing call). Without this, the outer parse loses sync and
            // misreads subsequent statements as part of the lambda's expression.
            if(retExpr->terminator == ";" || retExpr->terminator == "," || retExpr->terminator == ")"
               || retExpr->terminator == "}" || retExpr->terminator == ":"){
                token t;
                t.value = retExpr->terminator;
                t.tokenType = eTokenType::symbol;
                stashedToken = t;
            }
            if(retExpr->resolvedType == "void" || assigns){
                // `=> print("…")`: a call with no value is the body itself, not something to return.
                // What a `?.` chain leaves is its temporary, already run: no statement of its own.
                string text = retExpr->text();
                bool bare = !text.empty() && all_of(text.begin(), text.end(), [](char c){ return isalnum((unsigned char)c) || c == '_'; });
                if(!bare){
                    i6RawNode* stmt = new i6RawNode();
                    stmt->text = text + ";";
                    lambdaBody->statements.push_back(stmt);
                }
                for(statement* inj : innerAfter) lambdaBody->statements.push_back(inj);
                fd.returnType.name = "void";
            } else {
                fd.returnType.name = retExpr->resolvedType.empty() ? "var" : literalBaseType(retExpr->resolvedType);
                returnStatement& ret = *(new returnStatement());
                ret.src = fd.src;
                ret.returnExpression = retExpr->text();
                if(!innerAfter.empty()){
                    // A copy-back runs after the value is computed and before it is returned.
                    auto* held = new variableDeclaration();
                    held->name = "_bgllamret";
                    held->type.name = "var";
                    held->declaredExpressionValue = retExpr;
                    lambdaBody->statements.push_back(held);
                    for(statement* inj : innerAfter) lambdaBody->statements.push_back(inj);
                    ret.returnExpression = "_bgllamret";
                }
                lambdaBody->statements.push_back(&ret);
            }
        }
    }

    // Restore outer scope context
    lambdaOuterFunc = savedOuterFunc;
    lambdaOuterBody = savedOuterBody;
    activeBlockStack = savedBlockStack;
    lambdaOuterBlockStack = savedOuterBlocks;
    if(!lambdaOuterFuncStack.empty()) lambdaOuterFuncStack.pop_back();

    // Emit capture globals — insert at front so they appear before functions in the I6 output.
    // If `self` was captured, inject `self = _bglCapN;` at the top of the lambda body so
    // all self.member references inside the lambda work via I6's assignable self pseudo-variable.
    for(auto& cap : fd.captures){
        variableDeclaration& capGlobal = *(new variableDeclaration());
        capGlobal.name = cap.globalName;
        // A variable holding a property or attribute, not a declaration of one; a value class's instance is
        // held by reference (a typed global would own an instance of its own).
        classDef* capCls = languageService.findClass(cap.typeName);
        capGlobal.type.name = cap.typeName == "property" || cap.typeName == "attribute" || (capCls != nullptr && isValueClass(capCls))
                            ? "var" : cap.typeName;
        capGlobal.needsEarlyGlobalDecl = true;   // a class method's lambda is emitted with the class
        languageService.globals.insert(languageService.globals.begin(), &capGlobal);
        if(!cap.localName.empty()){
            auto* local = new variableDeclaration();
            local->name = cap.localName;
            local->type.name = "var";
            local->isSynthetic = true;
            i6RawNode* load = new i6RawNode();
            load->text = cap.localName + " = " + cap.globalName + ";";
            lambdaBody->statements.insert(lambdaBody->statements.begin(), load);
            lambdaBody->statements.insert(lambdaBody->statements.begin(), local);
            fd.cleanups.push_back({cap.localName, cap.globalName + " = " + cap.localName + ";"});
        }
        if(cap.outerName == "self"){
            // I6's `self` is one global: the routine that called the lambda (an <array> routine,
            // whose self is _bglArray) gets its own back on every exit.
            auto* saved = new variableDeclaration();
            saved->name = "_bglself";
            saved->type.name = "var";
            saved->isSynthetic = true;
            i6RawNode* selfAssign = new i6RawNode();
            selfAssign->text = "_bglself = self; self = " + cap.globalName + ";";
            lambdaBody->statements.insert(lambdaBody->statements.begin(), selfAssign);
            lambdaBody->statements.insert(lambdaBody->statements.begin(), saved);
            fd.cleanups.push_back({"_bglself", "self = _bglself;"});
        }
    }

    // Lift: append to globals — I6 doesn't require routines to precede call sites,
    // and inserting at front would place lambdas before _bgl_temp (I6 compile error)
    languageService.globals.push_back(&fd);

    // If there are captures, emit load/unload assignments around the enclosing statement.
    // Load (pendingInjections): copy locals → globals BEFORE the lambda is used.
    // Unload (postInjections): copy globals → locals AFTER the call returns, so
    // modifications inside the lambda are visible to the enclosing scope.
    // For stored lambdas (declaration, not immediate call), the unload is a no-op
    // since the lambda hasn't been called yet — the globals become canonical storage.
    for(auto& cap : fd.captures){
        i6RawNode& load = *(new i6RawNode());
        load.text = cap.globalName + " = " + cap.outerName + ";";
        load.cooked = true;   // the local is renamed here as everywhere else in the routine
        captureLoads.insert(&load);
        pendingInjections.push_back(&load);
        i6RawNode& unload = *(new i6RawNode());
        unload.text = cap.outerName + " = " + cap.globalName + ";";
        unload.cooked = true;
        postInjections.push_back(&unload);
    }

    return fd.name;
}

// ===============================================================================
// operator precedence table (file-local)
// ===============================================================================
// C-style operator precedence: higher number = tighter binding.
// Operators not in this table return -1 and take the single-token RHS fallback path
// (used for compound-assign and other unmanaged ops).
static int operatorPrecedence(const string& op){
    if(op == "*" || op == "/" || op == "%")                                return 11;
    if(op == "+" || op == "-")                                             return 10;
    if(op == "<<" || op == ">>")                                           return  9;
    if(op == "<=>")                                                        return  9;
    if(op == "<" || op == "<=" || op == ">" || op == ">=")                 return  8;
    if(op == "==" || op == "!=" || op == "?=" || op == "=~" || op == ":==") return  7;
    if(op == "&")                                                          return  6;
    if(op == "^")                                                          return  5;
    if(op == "|")                                                          return  4;
    if(op == "&&")                                                         return  3;
    if(op == "||")                                                         return  2;
    return -1;
}

// All operators registered in operatorPrecedence() — used when building RHS terminator lists
// so a sub-expression knows when to stop based on an encountered operator's level.
// Structural '(' tokens an expression opened before its current receiver: not part of the receiver,
// so a member access or call reads the receiver after them and puts them back in front.
static size_t leadingParens(const expression* expr){
    size_t n = 0;
    while(n < expr->tokens.size() && expr->tokens[n] == "(") n++;
    // A leading `(` the receiver itself closes (`(b - a)`) is part of it, not an open group.
    int depth = 0, closed = 0;
    for(size_t i = n; i < expr->tokens.size(); i++){
        if(expr->tokens[i] == "(") depth++;
        else if(expr->tokens[i] == ")" && --depth < 0){ closed++; depth = 0; }
    }
    return n - min(n, (size_t)closed);
}
static string receiverText(const expression* expr){
    string text;
    for(size_t i = leadingParens(expr); i < expr->tokens.size(); i++) text += expr->tokens[i];
    return text;
}
// Replaces the receiver with `text`, keeping the parentheses opened before it.
static void replaceReceiver(expression* expr, const string& text){
    size_t n = leadingParens(expr);
    expr->tokens.assign(n, "(");
    expr->tokens.push_back(text);
}

// Moves the structural tokens an expression opened before its left operand (unclosed `(`, a `~~`)
// into `prefix`. A `(` the operand itself closes (`(b - a)`) stays: it is part of the operand.
static void stripOpenPrefix(expression* expr, vector<string>& prefix){
    size_t run = 0, opens = 0;
    while(run < expr->tokens.size() && (expr->tokens[run] == "(" || expr->tokens[run] == "~~"))
        if(expr->tokens[run++] == "(") opens++;
    int depth = 0; size_t closed = 0;
    for(size_t i = run; i < expr->tokens.size(); i++){
        if(expr->tokens[i] == "(") depth++;
        else if(expr->tokens[i] == ")" && --depth < 0){ closed++; depth = 0; }
    }
    size_t open = opens - min(opens, closed);
    while(expr->tokens.size() > 1 && (expr->tokens.front() == "~~" || (expr->tokens.front() == "(" && open > 0))){
        if(expr->tokens.front() == "(") open--;
        prefix.push_back(expr->tokens.front());
        expr->tokens.erase(expr->tokens.begin());
    }
}

static const vector<string> kPrecedenceOps = {
    "*","/","%","+","-","<<",">>","<=>","<","<=",">",">=","==","!=","?=","=~",":==","&","^","|","&&","||"
};

// ===============================================================================
// applyBinaryOperator - binary operator emitter resolution + RHS parsing
// ===============================================================================
// I6's `~~` (logical NOT) binds LOOSER than `&&` / `||`, so a bare `~~x && y` parses as
// `~~(x && y)` — the negation swallows the whole conjunction and the test inverts silently.
// Beguile emits `!x` as a bare `~~x`, so any operand text that begins with `~~` must be
// parenthesised before it is joined to a binary operator.
static string parenIfNegated(const string& text){
    size_t i = text.find_first_not_of(" \t");
    if(i != string::npos && text.compare(i, 2, "~~") == 0) return "(" + text + ")";
    return text;
}

// Binary operator resolution: read RHS, find matching emitter, inline.
// Returns true if handled; false if the operator should pass through as raw I6.
bool bglParser::applyBinaryOperator(expression* expr, const string& opName, classDef* cls,
    const vector<string>& terminators, int parenDepth,
    function<token()> getNext, optional<token>& prefetched,
    functionDef* func, statementBlock* body)
{
    const string lhsAtEntry = expr->text();

    // Postfix operators (++ and --): no RHS needed. Find the zero-param emitter and inline.
    if(opName == "++" || opName == "--"){
        // An accessor's reading (`n.hits._opconv_int()`) isn't a slot: its setter takes the stepped value.
        if(string stepped = stepThroughAccessor(expr->text(), opName, false); !stepped.empty()){
            expr->tokens.assign(1, stepped);
            return true;
        }
        functionDef* postfixOp = nullptr;
        if(typeMember* m = findMemberInHierarchy(cls, [&](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn && fn->name == opName && fn->params.empty() && fn->isEmitter;
        })) postfixOp = dynamic_cast<functionDef*>(m);
        if(postfixOp != nullptr){
            if(auto* blk = dynamic_cast<i6Block*>(postfixOp->body)){
                string lhsText = expr->text();
                emitterBindings pb;
                pb.self = !expr->emitterSelf.empty() ? expr->emitterSelf : lhsText;
                pb.val  = lhsText;
                if(cls != nullptr){ pb.cls = cls->i6Name(); pb.selfType = cls->name; }
                pb.trim = emitterTrim::wsSemi;
                expr->tokens.clear();
                // Bracketed: I6 cannot read `i-- - 1`.
                expr->tokens.push_back("(" + expandEmitterBody(blk, pb) + ")");
            }
            if(!postfixOp->returnType.name.empty()) expr->resolvedType = postfixOp->returnType.name;
            return true;
        }
        // A method `operator ++` / `--`: sent to the operand, its result the value.
        if(cls != nullptr)
            if(auto* fn = dynamic_cast<functionDef*>(findMemberInHierarchy(cls, [&](typeMember* m){
                    auto* f = dynamic_cast<functionDef*>(m); return f && f->name == opName && f->params.empty() && !f->isEmitter; }))){
                if(fn->i6name.empty()) fn->i6name = mangleOperatorName(fn->name);
                string operand = expr->text();
                expr->tokens.clear();
                expr->tokens.push_back(parenthesizeReceiver(operand) + "." + fn->i6name + "()");
                if(!fn->returnType.name.empty() && fn->returnType.name != "void") expr->resolvedType = fn->returnType.name;
                return true;
            }
        // No postfix emitter — pass through as raw I6
        string operand = expr->text();
        expr->tokens.clear();
        expr->tokens.push_back("(" + operand + opName + ")");
        return true;
    }

    // Step 1: Read RHS token and determine its type and text.
    // For operators with a registered precedence level, parse the RHS as a sub-expression that
    // terminates on any operator with level <= myPrec. This yields standard C-like precedence:
    // `a + b * c` parses as `a + (b * c)` because `*`'s level (11) is higher than `+`'s (10),
    // so `*` is NOT a terminator when we're parsing `+`'s RHS, and gets consumed by the sub-parse.
    // Equal-level ops (e.g. `a + b + c`) terminate, giving left-associative evaluation.
    int myPrec = operatorPrecedence(opName);
    token rhs = getNext();
    if(rhs.is(eTokenType::oper) && rhs.value == "+") rhs = getNext();   // a unary plus: Inform 6 has none
    string rhsType, rhsText;

    if(myPrec >= 0){
        vector<string> rhsTerminators = terminators;
        for(const string& op : kPrecedenceOps)
            if(operatorPrecedence(op) <= myPrec)
                rhsTerminators.push_back(op);
        if(parenDepth > 0) rhsTerminators.push_back(token::parenClose);
        rhsTerminators.push_back("?"); // ternary has lowest precedence — always a terminator
        // Set expected type for the RHS to the LHS class — most operators take same-type args,
        // so this is the right hint for disambiguating an enum-value reference like `fixed`.
        string savedExpectedOp = currentExpectedType;
        if(cls != nullptr) currentExpectedType = cls->name;
        classDef* savedOwnOperand = ownOperatorOperand;
        ownOperatorOperand = takesOwnOperand(cls, opName) ? cls : nullptr;
        expression* rhsExpr = parseExpression(rhs, rhsTerminators, func, body);
        ownOperatorOperand = savedOwnOperand;
        if(rhsExpr->tokens.empty() && dynamic_cast<initializerList*>(rhsExpr) == nullptr)
            parsingError(format("'{0}' has nothing after it: an operand is missing.", opName));
        absorbPostfixQuery(rhsExpr);   // `x == a?` queries `a`
        currentExpectedType = savedExpectedOp;
        token terminatorTok;
        terminatorTok.value = rhsExpr->terminator;
        terminatorTok.tokenType = eTokenType::oper;
        // Some terminators aren't operators (;, ), }). Preserve their original token type so the
        // outer loop recognises them correctly (e.g. as end-of-statement or close-paren).
        if(rhsExpr->terminator == ";" || rhsExpr->terminator == ")" || rhsExpr->terminator == "?")
            terminatorTok.tokenType = eTokenType::symbol;
        prefetched = terminatorTok;
        rhsType = rhsExpr->resolvedType;
        rhsText = rhsExpr->text();
    }
    else if(rhs.is(eTokenType::integer))         { rhsType="intliteral";    rhsText=rhs.value; }
    else if(rhs.isString())                      { rhsType="stringliteral"; rhsText=rhs.value; }
    else if(rhs.is(eTokenType::charLiteral))     { rhsType="charliteral";   bool bare = (!rhs.value.empty() && all_of(rhs.value.begin(),rhs.value.end(),::isdigit)) || rhs.value.rfind("@",0)==0; rhsText = bare ? rhs.value : "'"+rhs.value+"'"; }
    else if(rhs.is(eTokenType::name) && (file.peekToken().is(token::period) || file.peekToken().is(token::parenOpen))){
        vector<string> rhsTerminators = terminators;
        if(parenDepth > 0) rhsTerminators.push_back(token::parenClose);
        string savedExpectedOp2 = currentExpectedType;
        if(cls != nullptr) currentExpectedType = cls->name;
        classDef* savedOwnOperand = ownOperatorOperand;
        ownOperatorOperand = takesOwnOperand(cls, opName) ? cls : nullptr;
        expression* rhsExpr = parseExpression(rhs, rhsTerminators, func, body);
        ownOperatorOperand = savedOwnOperand;
        currentExpectedType = savedExpectedOp2;
        token terminatorTok; terminatorTok.value = rhsExpr->terminator;
        prefetched = terminatorTok;
        rhsType = rhsExpr->resolvedType;
        rhsText = rhsExpr->text();
    }
    else if(rhs.is(eTokenType::name)) {
        // Bare identifier RHS — apply the same expected-type context for resolution.
        string savedExpectedOp3 = currentExpectedType;
        if(cls != nullptr) currentExpectedType = cls->name;
        rhsType = resolveIdentifierType(rhs.value, func, body);
        rhsText = (func != nullptr) ? qualifyIdentifier(rhs.value, func, body) : rhs.value;
        currentExpectedType = savedExpectedOp3;
        if(rhsText.empty()) rhsText = rhs.value;
    }
    else if(rhs.is(token::parenOpen)){
        expression* rhsExpr = parseExpression(file.getToken(), {token::parenClose}, func, body);
        rhsText = "(" + rhsExpr->text() + ")";
    }
    else if(rhs.is(eTokenType::directive)){
        if(rhs.value.rfind("##", 0) == 0)
            parsingError(format("'##' prefix is not valid in Beguile source. Write '{0}' directly.", rhs.value.substr(2)));
        parsingError(format("Directive '{0}' is not valid in an expression.", rhs.value));
    }

    // A verb operand named directly is its action constant (`action == Take` compares with ##Take);
    // a verb-typed variable or member is already an action value.
    if(rhsType == "verb"){
        expression r; r.tokens.push_back(rhsText); r.resolvedType = "verb";
        if(applyActionConstant(&r)) rhsText = r.tokens[0];
    }
    // `action == Seal` beside a class Seal: against an action, the name is the verb's (a verb may share a class's name).
    if(expr->resolvedType == "verb")
        if(classDef* rc = languageService.findClass(rhsType); rc != nullptr && (strcasecmp(rhsText.c_str(), rc->i6Name().c_str()) == 0 || strcasecmp(rhsText.c_str(), rc->name.c_str()) == 0))
            for(verbObjectDef* v : languageService.verbs)
                if(v->name == rc->name){ rhsText = "##" + v->dName(); rhsType = "verb"; break; }
    if(expr->resolvedType == "verb" && expr->tokens.size() == 1) applyActionConstant(expr);
    rhsText = keepValueResult(rhsText, rhsType);

    // Step 2: Find matching operator emitter
    // 'var' is the escape-hatch type — if either side is var, skip param-type checking
    // (treat the same as an empty/unknown rhsType: match by operator name alone).
    functionDef* matchedOp = nullptr;
    // Two passes, non-static first. An instance operator INLINES at the call site; a static
    // costs a routine call, so where a type declares both the instance form must win. The
    // static form is the fallback — and the only form for `<=>`, which has no instance
    // spelling (its two operands are both parameters).
    // The declared type a literal DENOTES. `100` resolves as intLiteral, but an operator
    // declared `(Money, int)` plainly accepts it; without this, `m == 100` reported no
    // matching operator while `m == someInt` matched. Consulted only in the widened pass
    // below — isTypeCompatible deliberately carries no global literal rule, because one
    // perturbs overload resolution everywhere (see isArrayElementCompatible's note).
    auto literalBase = [](const string& t) -> string {
        if(t == "intliteral" || t == "negativeintliteral") return "int";
        if(t == "charliteral") return "char";
        if(t == "nullliteral") return "_bglobject";
        return "";
    };
    // widenMode: 0 = exact only, 1 = exact literal-base (paramT == literalBase(rhsType)),
    //            2 = convertible (isTypeCompatible(literalBase, paramT)). Modes are tried in
    // order so a MORE-SPECIFIC overload wins: for a `intLiteral` RHS, `operator*(int)` (base-exact,
    // mode 1) beats `operator*(float)` (convertible only because float publishes an int→float path,
    // mode 2). Without this ordering the first-declared of two widen candidates won, which raw-
    // substituted the int into the float operator (denormal). Exact (mode 0) still always wins.
    // An object declared as its own type matches its class's operators exactly.
    string rhsClassName;
    if(languageService.findObjectType(rhsType) != nullptr)
        if(classDef* rc = languageService.classOf(rhsType)) rhsClassName = rc->name;
    // A class named as a value (`Box`), not an instance of it, is a bglClass.
    bool rhsIsClassValue = false;
    if(classDef* rc = languageService.findClass(rhsType)) rhsIsClassValue = rhsText == rc->i6Name();
    string matchName = opName;   // `<=>` while deriving an ordering operator (below)
    auto opMatches = [&](typeMember* m, bool wantStatic, int widenMode){
        auto* opFn = dynamic_cast<functionDef*>(m);
        if(!opFn || opFn->name != matchName) return false;
        if(opFn->isStatic != wantStatic) return false;
        // A pre-scan stub without its parameters matches by name only (exact phase).
        if(opFn->isPrePassStub && opFn->params.empty()) return widenMode == 0;
        size_t rhsIdx = opFn->isStatic ? 1 : 0;   // static takes (lhs, rhs); instance takes (rhs)
        if(opFn->params.size() <= rhsIdx) return false;
        // `selfType` is the left operand's own class (an object's is its class).
        string paramStore = opFn->params[rhsIdx]->type.name;
        if(paramStore == "selftype"){
            string oc = objectsClassName(expr->resolvedType);
            paramStore = !oc.empty() ? oc : cls->name;
        }
        const string& paramT = paramStore;
        // A `var` parameter accepts anything, so it is the UNIVERSAL fallback and has to lose to
        // any more specific overload — most importantly, an inherited one has to lose to the
        // subclass's own. `object` declares `emitter eBool operator == (var)`, so while `var`
        // counted as an exact match a class deriving from `object` could never override `==`:
        // `class W : object { bool operator ==(int) }` had its own operator beaten by the
        // inherited universal one, and `w == 6` emitted a raw address comparison instead.
        // A genuinely untyped RHS still matches in mode 0 — there is nothing more specific to
        // prefer in that case.
        bool rhsUnknown = rhsType.empty() || rhsType == "var";
        bool exact = rhsUnknown || paramT == rhsType || (!rhsClassName.empty() && paramT == rhsClassName)
                  || (rhsType == "bytearray" && paramT == "array<char>");   // one type, two spellings
        if(widenMode == 0) return exact;
        if(exact) return false;                          // exact already claimed in mode 0
        string base = literalBase(rhsType);
        if(widenMode == 1) return !base.empty() && paramT == base;   // prefer the literal-base overload
        if(widenMode == 2){
            if(!base.empty() && paramT != base && isTypeCompatible(base, paramT)) return true;   // convertible
            if(paramT == "float" && rhsType == "int") return true;          // int → float (§2.3); a char widens to int first
            if(paramT == "bglclass" && rhsIsClassValue) return true;
            // An array argument meets an array parameter of the same class (`array` vs `array<int>`).
            if(isWordArrayType(paramT) && isWordArrayType(rhsType) && getDispatchClass(paramT) == getDispatchClass(rhsType)
               && (paramT.find('<') == string::npos || rhsType.find('<') == string::npos || templateArgsFit(rhsType, paramT)))
                return true;
            // A comparison's eBool and a bool interoperate (§2.2).
            if((rhsType == "ebool" && paramT == "bool") || (rhsType == "bool" && paramT == "ebool")) return true;
            // A bnum widens to int (§2.7.3), so `int & flag` takes int's operator as `flag & int` does.
            if(paramT == "int")
                if(auto* re = languageService.findEnum(rhsType); re != nullptr && re->isBnum) return true;
            // A parameter of a class accepts an instance of it or of a class derived from it — including
            // an object declared as its own type, whose class is the parameter's.
            classDef* argCls = languageService.classOf(rhsType);
            classDef* paramCls = languageService.findClass(paramT);
            if(argCls != nullptr && paramCls != nullptr && (argCls == paramCls || argCls->hasAncestor(paramCls))) return true;
            // A value the parameter's class converts from (`"desk"` for a stringObj), as a call's argument is (§2.10.1).
            return paramCls != nullptr && !paramCls->isPrimitive && !rhsType.empty()
                && isTypeCompatible(rhsType, paramT);
        }
        return paramT == "var";                          // mode 3: the universal accepter, last
    };
    // Exact first, then base-exact widen, then convertible widen — so every resolution that already
    // worked resolves identically (exact always wins), and among widen candidates the more specific
    // one wins. Within each phase, non-static wins: an instance operator inlines, a static costs a call.
    // An object declared with its own operators (`object dial { eBool operator == (int v) … }`): its own first.
    objectDef* lhsObj = languageService.findObjectType(expr->resolvedType);
    auto findOperator = [&]{
        for(int widenMode : {0, 1, 2, 3}){
            if(matchedOp != nullptr) break;
            if(lhsObj != nullptr)
                for(typeMember* m : lhsObj->members) if(opMatches(m, false, widenMode)){ matchedOp = dynamic_cast<functionDef*>(m); break; }
            if(matchedOp != nullptr) break;
            for(bool wantStatic : {false, true}){
                if(matchedOp != nullptr) break;
                if(typeMember* m = findMemberInHierarchy(cls, [&](typeMember* mm){ return opMatches(mm, wantStatic, widenMode); }))
                    matchedOp = dynamic_cast<functionDef*>(m);
            }
        }
    };
    findOperator();
    // A char widens to int (§12.4): an operator char lacks is int's.
    if(!matchedOp && cls->name == "char")
        if(classDef* intCls = languageService.findClass("int")){ cls = intCls; findOperator(); }
    // A type without its own `<`, `>`, `<=` or `>=` gets it from its `<=>`: `a < b` is
    // `(a <=> b) < 0`. A declared ordering operator, inherited or not, always wins.
    bool derivedFromCompare = false;
    if(!matchedOp && (opName == "<" || opName == ">" || opName == "<=" || opName == ">=")){
        matchName = "<=>";
        findOperator();
        derivedFromCompare = matchedOp != nullptr;
        matchName = opName;
    }

    // LHS conversion fallback: if LHS has operator() → convertedType, retry operator search on that type.
    // The "converted-to-converted" clause (param type == convertedType) also requires the RHS to
    // be compatible with convertedType, so the fallback only fires when the entire converted-to
    // operator signature genuinely accepts the call. Accepting ANY rhsType there silently passes
    // type-incompatible comparisons like `int != property`, because both compile to bit-level I6 ops.
    if(!matchedOp){
        for(typeMember* m : cls->members){
            auto* convFn = dynamic_cast<functionDef*>(m);
            if(!convFn || convFn->name != "operator()" || !convFn->params.empty() || convFn->isExplicit) continue;
            string convertedType = convFn->returnType.name;
            classDef* convCls = getDispatchClass(convertedType);
            if(!convCls) continue;
            // The converted type's ordering may come from its `<=>` too (`"desk" < s` through string's).
            bool ordering = opName == "<" || opName == ">" || opName == "<=" || opName == ">=";
            auto findConvOp = [&](const string& n){ return findMemberInHierarchy(convCls, [&](typeMember* m){
                auto* opFn = dynamic_cast<functionDef*>(m);
                if(!opFn || opFn->name != n) return false;
                if(opFn->isPrePassStub) return true;
                if(opFn->params.empty()) return false;
                const string& paramT = opFn->params[0]->type.name;
                if(rhsType.empty() || rhsType=="var" || paramT==rhsType || paramT=="var") return true;
                // Converted-to-converted: the operator is, say, `char != (char)`, and we got here
                // by converting LHS int → char. Only valid if RHS is itself compatible with char.
                if(paramT == convertedType) return isTypeCompatible(rhsType, convertedType);
                return false;
            }); };
            typeMember* m2 = findConvOp(opName);
            if(m2 == nullptr && ordering && (m2 = findConvOp("<=>")) != nullptr) derivedFromCompare = true;
            if(m2 != nullptr){
                matchedOp = dynamic_cast<functionDef*>(m2);
                // A right operand that only converts to the parameter (an accessor) converts too.
                if(!matchedOp->params.empty() && !rhsType.empty() && rhsType != "var")
                    rhsText = applyCastConversion(rhsText, rhsType, matchedOp->params[0]->type.name);
                // The operator is the converted type's, so its left operand is the converted value.
                vector<string> prefix;
                stripOpenPrefix(expr, prefix);
                string converted = applyCastConversion(expr->text(), cls->name, convertedType);
                expr->tokens.clear();
                for(auto& p : prefix) expr->tokens.push_back(p);
                expr->tokens.push_back(converted);
                expr->emitterSelf.clear();
                cls = convCls;
                break;
            }
        }
    }

    // Conversion fallback: LHS has operator() returning rhsType → raw I6 compatible
    bool useRawFallback = false;
    if(!matchedOp && !rhsType.empty()){
        if(findMemberInHierarchy(cls, [&](typeMember* m){
            auto* opFn = dynamic_cast<functionDef*>(m);
            return opFn && opFn->name=="operator()" && opFn->params.empty() &&
                   opFn->isEmitter && !opFn->isExplicit && opFn->returnType.name==rhsType;
        })) useRawFallback = true;
    }

    // RHS conversion fallback: RHS type has operator() → type that LHS has the operator for
    if(!matchedOp && !useRawFallback && !rhsType.empty()){
        classDef* rhsCls = getDispatchClass(rhsType);
        if(rhsCls != nullptr){
            for(typeMember* rm : rhsCls->members){
                auto* convFn = dynamic_cast<functionDef*>(rm);
                if(!convFn || convFn->name != "operator()" || !convFn->params.empty() || convFn->isExplicit) continue;
                string convertedType = convFn->returnType.name;
                if(typeMember* m2 = findMemberInHierarchy(cls, [&](typeMember* m){
                    auto* opFn = dynamic_cast<functionDef*>(m);
                    if(!opFn || opFn->name != opName) return false;
                    if(opFn->isPrePassStub) return true; // pre-scan stub: match by name only
                    return !opFn->params.empty() && opFn->params[0]->type.name == convertedType;
                })){
                    matchedOp = dynamic_cast<functionDef*>(m2);
                    if(!convFn->isEmitter){
                        // A regular-method conversion (an accessor's getter) runs through its routine.
                        if(convFn->i6name.empty()) convFn->i6name = mangleOperatorName(convFn->name);
                        rhsText = parenthesizeReceiver(rhsText) + "." + convFn->i6name + "()";
                    } else if(auto* convBlk = dynamic_cast<i6Block*>(convFn->body)){
                        emitterBindings cb; cb.self = rhsText; cb.val = rhsText; cb.trim = emitterTrim::wsSemi;
                        string convBody = expandEmitterBody(convBlk, cb);
                        if(!convBody.empty()) rhsText = convBody;
                    }
                    break;
                }
            }
        }
    }

    if(!matchedOp && !useRawFallback && !rhsType.empty() && !languageService.arrayInUse && cls->name == "array"
       && (opName == "+" || opName == "-"))
        parsingError(format("'{0}=' on an array comes from `<array>`: add `#include <array>`.", opName));
    if(!matchedOp && !useRawFallback && !rhsType.empty())
        parsingError(format("No operator '{0}' on type '{1}' accepting '{2}'", opName, cls->dName(), typeDisplayName(rhsType)));

    if(matchedOp && !matchedOp->returnType.name.empty())
        expr->resolvedType = matchedOp->returnType.name;
    else if(matchedOp && matchedOp->isPrePassStub){
        // Pre-scan stub: no return type info. Infer from operator category.
        static const vector<string> comparisonOps = {"==","!=","<",">","<=",">=","?=","=~"};
        static const vector<string> logicalOps = {"&&","||"};
        if(find(comparisonOps.begin(), comparisonOps.end(), opName) != comparisonOps.end()) expr->resolvedType = "ebool";
        else if(find(logicalOps.begin(), logicalOps.end(), opName) != logicalOps.end()) expr->resolvedType = "ebool";
    }

    // Step 3: Apply the result
    // The operand takes the operator's parameter type (an int given to a `float` parameter is converted).
    if(matchedOp != nullptr && (!matchedOp->isPrePassStub || !matchedOp->params.empty()) && !rhsType.empty()){
        size_t pi = matchedOp->isStatic ? 1 : 0;
        if(matchedOp->params.size() > pi){
            const string& want = matchedOp->params[pi]->type.name;
            if(want == "float" && rhsType != "float" && rhsType != "var") rhsText = applyCastConversion(rhsText, rhsType, want);
            else if(!matchedOp->isEmitter){
                expression conv; conv.tokens.assign(1, rhsText); conv.resolvedType = rhsType;
                convertIntoValueClass(&conv, want);
                rhsText = conv.text();
            }
        }
    }
    i6Block* blk = (matchedOp && matchedOp->isEmitter && !matchedOp->isPrePassStub) ? dynamic_cast<i6Block*>(matchedOp->body) : nullptr;
    if(useRawFallback){
        // Comparison operators produce eBool; others preserve the LHS type
        static const vector<string> comparisonOps = {"==","!=","<",">","<=",">=","?=","=~"};
        if(find(comparisonOps.begin(), comparisonOps.end(), opName) != comparisonOps.end())
            expr->resolvedType = "ebool";
        string lhsText = parenIfNegated(expr->text());
        expr->tokens.clear();
        expr->tokens.push_back(lhsText + opName + parenIfNegated(rhsText));
    } else if(blk != nullptr){
        // For identifier RHS, check if it's a function call and collect full text.
        // Skip this when the RHS was already fully resolved by the precedence sub-parse
        // (myPrec >= 0), since rhsText is complete and reading further would corrupt the stream.
        if(myPrec < 0 && rhs.is(eTokenType::name)){
            token rhsNext = getNext();
            if(rhsNext.is(token::parenOpen)){
                rhsText = rhs.value + token::parenOpen;
                int callDepth = 1;
                token argTok = file.getToken();
                while(callDepth > 0){
                    if(argTok.is(token::parenOpen)) callDepth++;
                    else if(argTok.is(token::parenClose)){ callDepth--; if(callDepth==0) break; }
                    rhsText += argTok.value;
                    if(callDepth > 0) argTok = file.getToken();
                }
                rhsText += token::parenClose;
            } else {
                prefetched = rhsNext;
            }
        }
        // Separate leading structural tokens (parens) from the actual operand
        vector<string> prefix;
        // `!(` arrives as its own `~~` ahead of the parens; it applies to the whole group too.
        stripOpenPrefix(expr, prefix);
        string lhsText = expr->text();
        // Both operands may be temporaries: hold them until the operator's result is built.
        string heldLhs = lhsText, heldRhs = rhsText;
        HeldTemporaries heldTemps = holdCallTemporaries({{&heldLhs, cls != nullptr ? cls->name : expr->resolvedType},
                                                         {&heldRhs, rhsType}});
        // $self = host of property access (parentProp's `parent($self) == $v` etc.).
        // $val  = full receiver expression as written (`obj.parent`, `5`, `localInt`).
        // For non-property contexts the two coincide.
        emitterBindings bo;
        bo.self = !expr->emitterSelf.empty() ? expr->emitterSelf : heldLhs;
        bo.val  = heldLhs;
        if(cls != nullptr){ bo.cls = cls->i6Name(); bo.selfType = cls->name; }
        bo.fn = matchedOp; bo.args.push_back(heldRhs);
        bo.trim = emitterTrim::ws;
        string b = expandEmitterBody(blk, bo);
        if(heldTemps.any()) b = wrapHeldCall(heldTemps, b);
        expr->tokens.clear();
        for(auto& p : prefix) expr->tokens.push_back(p);
        expr->tokens.push_back(b);
    } else if(matchedOp && !matchedOp->isEmitter && (!matchedOp->isPrePassStub || !matchedOp->params.empty())){
        // Non-emitter operator. A `static` one has no receiver — it emits as a FREE routine
        // taking both operands, which is also what makes it usable when the LHS is a bare word
        // (a packed literal has nothing to send a message to). An instance one is a message
        // send on the LHS via its mangled property name.
        // Separate leading structural parens from the operand, as the emitter branch does —
        // otherwise `(a <=> b) < 0` folds the '(' into the LHS and emits `routine((a, b))`.
        vector<string> prefix;
        // `!(` arrives as its own `~~` ahead of the parens; it applies to the whole group too.
        stripOpenPrefix(expr, prefix);
        string lhsText = expr->text();
        // Settle the overload set's names BEFORE reading i6name. An operator carries a
        // parse-time name (`==` → `_opeqeq`) shared by every overload; mangleOverloadSet
        // appends parameter types when there is more than one, and this call site would
        // otherwise bake in the undiscriminated name. Method calls do the same via
        // mangleOverloadSetForReceiver in bindMethodCall.
        if(cls != nullptr) mangleOverloadSetForReceiver(cls->name, matchedOp->name);
        if(matchedOp->i6name.empty()) matchedOp->i6name = mangleOperatorName(matchedOp->name);
        if(matchedOp->returnType.name.empty() || matchedOp->returnType.name == "void")
            expr->resolvedType = cls->name;
        else
            expr->resolvedType = matchedOp->returnType.name;
        expr->tokens.clear();
        for(auto& p : prefix) expr->tokens.push_back(p);
        string heldLhs = lhsText, heldRhs = rhsText;
        HeldTemporaries heldTemps = holdCallTemporaries({{&heldLhs, cls->name}, {&heldRhs, rhsType}});
        string call;
        if(matchedOp->isStatic)
            call = i6Emitter::staticRoutineName(cls->declaringClass(matchedOp) ? cls->declaringClass(matchedOp) : cls, matchedOp) + "(" + heldLhs + ", " + heldRhs + ")";
        else {
            // A message send binds tighter than any I6 operator, so a receiver that is more than a
            // name (`boxes-->(0+1)`) must be grouped or the send lands on its last operand.
            bool bare = all_of(heldLhs.begin(), heldLhs.end(), [](char c){ return isalnum((unsigned char)c) || c == '_' || c == '.'; });
            // `((Coin)t) == 6`: the ancestor's own operator, as `((Coin)t).m()` runs the ancestor's method.
            string op = matchedOp->i6name;
            string unwrapped = lhsText;
            while(unwrapped.size() >= 2 && unwrapped.front() == '(' && unwrapped.back() == ')') unwrapped = unwrapped.substr(1, unwrapped.size() - 2);
            if(!expr->upcastText.empty() && unwrapped == expr->upcastText)
                if(classDef* owner = cls->declaringClass(matchedOp); owner != nullptr && !matchedOp->isEmitter)
                    op = owner->i6Name() + "::" + op;
            expr->upcastText.clear();
            call = (bare ? heldLhs : "(" + heldLhs + ")") + "." + op + "(" + heldRhs + ")";
        }
        expr->tokens.push_back(heldTemps.any() ? wrapHeldCall(heldTemps, call) : call);
    } else {
        // No operator found on this type
        parsingError(format("No operator '{0}' on type '{1}' accepting '{2}'",
            opName, cls->dName(), typeDisplayName(rhsType.empty() ? "unknown" : rhsType)));
    }
    if(derivedFromCompare){
        expr->tokens.back() = "(" + expr->tokens.back() + ") " + opName + " 0";
        expr->resolvedType = "bool";
    }
    // `a?.level > 0`: a chain that stopped left null, which the class's comparison can't take; a null compares unequal.
    static const set<string> comparisons = {"==", "!=", "<", ">", "<=", ">=", "=~"};
    if(!expr->nullableText.empty() && comparisons.count(opName) && !matchedOp->isEmitter && !expr->tokens.empty()){
        string unwrappedLhs = lhsAtEntry;
        while(unwrappedLhs.size() >= 2 && unwrappedLhs.front() == '(' && unwrappedLhs.back() == ')') unwrappedLhs = unwrappedLhs.substr(1, unwrappedLhs.size() - 2);
        if(unwrappedLhs == expr->nullableText)
            expr->tokens.back() = opName == "!="
                ? "(" + expr->nullableText + " == 0 || (" + expr->tokens.back() + "))"
                : "(" + expr->nullableText + " ~= 0 && (" + expr->tokens.back() + "))";
    }
    expr->nullableText.clear();
    return true;
}

// ===============================================================================
// parseExpression sub-functions: ternary, null coalescing, function call, prefix !
// ===============================================================================
// ── parseExpression sub-functions ─────────────────────────────────────────────

// Ternary operator: condition ? trueExpr : falseExpr
// Lowers to if/else injection using a unique _bgl_tempN. Replaces expr contents and sets terminator.
void bglParser::parseExprTernary(expression* expr, const vector<string>& terminators, functionDef* func, statementBlock* body, int outerParenDepth){
    string tempName = newChainTemp();
    string condText = expr->text();
    size_t mark = pendingInjections.size();
    expression* trueExpr  = parseExpression(file.getToken(), {":"}, func, body);
    string trueSetUp = takeBranchSetUp(mark);
    mark = pendingInjections.size();
    expression* falseExpr = parseExpression(file.getToken(), terminators, func, body);
    string falseSetUp = takeBranchSetUp(mark);
    string injText = "if (" + condText + ") { " + trueSetUp + tempName + " = " + trueExpr->text()
                   + "; } else { " + falseSetUp + tempName + " = " + falseExpr->text() + "; }";
    i6RawNode* inj = new i6RawNode();
    inj->text = injText;
    inj->cooked = true;   // built from Beguile expressions: locals take their renames
    pendingInjections.push_back(inj);
    expr->tokens.clear();
    expr->tokens.push_back(tempName);
    expr->resolvedType = !trueExpr->resolvedType.empty() ? trueExpr->resolvedType : falseExpr->resolvedType;
    expr->terminator = falseExpr->terminator;
}

// Null coalescing: lhs ?? fallback
// Lowers to if injection using a unique _bgl_tempN and operator?(). Replaces expr contents and sets terminator.
void bglParser::parseExprNullCoalescing(expression* expr, const vector<string>& terminators, functionDef* func, statementBlock* body){
    string tempName = newChainTemp();
    string lhsText = expr->text();
    string lhsType = expr->resolvedType;
    classDef* lhsCls = !lhsType.empty() ? getDispatchClass(lhsType) : nullptr;
    functionDef* nullTestFn = nullptr;
    if(lhsCls != nullptr)
        nullTestFn = findQueryOp(lhsCls, lhsType);
    if(nullTestFn == nullptr)
        parsingError(format("Type '{0}' does not support null coalescing (no operator?() emitter)", lhsType));
    auto* blk = dynamic_cast<i6Block*>(nullTestFn->body);
    emitterBindings nb; nb.self = tempName; nb.val = tempName; nb.trim = emitterTrim::wsSemi;
    string nullTest = expandEmitterBody(blk, nb);
    // A value class's own test reads its fields; a failed `?.` before it left 0, which has none.
    if(isValueClass(lhsCls)) nullTest = "(" + tempName + " ~= 0 && (" + nullTest + "))";
    size_t mark = pendingInjections.size();
    expression* fallback = parseExpression(file.getToken(), terminators, func, body);
    // The fallback's own set-up (a `?.` chain's temporaries) runs only when the fallback is taken.
    string fallbackSetUp = takeBranchSetUp(mark);
    string injText = tempName + " = " + lhsText + "; if (~~(" + nullTest + ")) { " + fallbackSetUp + tempName + " = " + fallback->text() + "; }";
    // An accessor tested by its own `operator ?` gives its getter's value when the fallback isn't one (`cap ?? "none"`).
    auto isOfLhsClass = [&](const string& t){
        for(classDef* c = getDispatchClass(t); c != nullptr; c = c->baseClasses.empty() ? nullptr : c->baseClasses[0])
            if(c == lhsCls) return true;
        return false;
    };
    if(isValueClass(lhsCls) && !fallback->resolvedType.empty() && !isOfLhsClass(fallback->resolvedType))
        if(functionDef* g = nativeGetterOf(lhsType)){
            injText = tempName + " = " + lhsText + "; if (" + nullTest + ") " + tempName + " = " + tempName + "." + g->i6name
                    + "(); else { " + fallbackSetUp + tempName + " = " + fallback->text() + "; }";
            lhsType = expr->resolvedType = g->returnType.name;
        }
    i6RawNode* inj = new i6RawNode();
    inj->text = injText;
    inj->cooked = true;   // built from Beguile expressions: locals take their renames
    pendingInjections.push_back(inj);
    expr->tokens.clear();
    expr->tokens.push_back(tempName);
    // The result is the left side's type when the fallback fits it (`s ?? "none"` stays a stringObj, and
    // `find(1) ?? vb` is a Valve, whichever it turns out to be).
    if(!fallback->resolvedType.empty() && !(lhsType.size() && lhsType != "var" && lhsType != "object" && lhsType != "_bglobject"
                                            && isTypeCompatible(fallback->resolvedType, lhsType)))
        expr->resolvedType = fallback->resolvedType;
    expr->terminator = fallback->terminator;
}

// `a && b?.c`: set-up an operand right of `&&`/`||` needs (a `?.` chain's temporaries) runs only when
// that operand is evaluated. When any does, the expression becomes a temporary computed operand by operand.
void bglParser::guardShortCircuitSetUp(expression* expr, const vector<ShortCircuitOp>& ops){
    if(ops.empty() || pendingInjections.size() <= ops.front().injectionsBefore) return;
    for(size_t i = 1; i < ops.size(); i++) if(ops[i].injectionsBefore < ops[i - 1].injectionsBefore) return;
    for(size_t i = ops.front().injectionsBefore; i < pendingInjections.size(); i++){
        auto* raw = dynamic_cast<i6RawNode*>(pendingInjections[i]);
        if(captureLoads.count(pendingInjections[i]) || raw == nullptr || !raw->parts.empty() || raw->isI6Island) return;
    }
    // The operands, split at the top-level `&&`/`||` (an operator's emission may have merged tokens).
    string whole = expr->text();
    vector<string> operands; vector<string> opsSeen;
    int depth = 0; char q = 0; size_t from = 0;
    for(size_t k = 0; k < whole.size(); k++){
        char c = whole[k];
        if(q){ if(c == '\\') k++; else if(c == q) q = 0; continue; }
        if(c == '"' || c == '\''){ q = c; continue; }
        if(c == '(' || c == '[') depth++;
        else if(c == ')' || c == ']') depth--;
        else if(depth == 0 && k + 1 < whole.size() && ((c == '&' && whole[k + 1] == '&') || (c == '|' && whole[k + 1] == '|'))){
            operands.push_back(whole.substr(from, k - from));
            opsSeen.push_back(whole.substr(k, 2));
            from = k + 2; k++;
        }
    }
    operands.push_back(whole.substr(from));
    if(opsSeen.size() != ops.size()) return;
    for(size_t i = 0; i < ops.size(); i++) if(opsSeen[i] != ops[i].op) return;
    // Each operand's set-up as text, taken last operand first so the marks stay valid.
    vector<string> setUp(ops.size());
    for(size_t i = ops.size(); i-- > 0;) setUp[i] = takeBranchSetUp(ops[i].injectionsBefore);
    string t = newChainTemp();
    // Operand by operand: `&&` goes on while t holds, `||` while it doesn't; an `&&` after an `||`
    // belongs to that `||`'s right side, so it nests inside it.
    string text = t + " = ((" + operands[0] + ") ~= 0);";
    int open = 0;
    for(size_t i = 0; i < ops.size(); i++){
        if(ops[i].op == "||"){ while(open > 0){ text += " }"; open--; } text += " if (~~" + t + ") {"; open++; }
        text += (ops[i].op == "&&" ? " if (" + t + ") { " : " ") + setUp[i] + t + " = ((" + operands[i + 1] + ") ~= 0);";
        if(ops[i].op == "&&") text += " }";
    }
    while(open > 0){ text += " }"; open--; }
    auto* n = new i6RawNode(); n->text = text; n->cooked = true;
    pendingInjections.push_back(n);
    expr->tokens.assign(1, t);
}

// Function call in expression context: parses args, resolves global/self call,
// validates arity+types, inlines emitters. Returns true if an emitter was inlined
// (caller should 'continue' to skip getNext at loop bottom).
bool bglParser::parseExprFunctionCall(expression* expr, const string& callName, bool isSelfCall,
                                       functionDef* func, statementBlock* body){
    // Compute brace-argument type hints so a bare `foo({ … })` infers its object type from the
    // callee's parameter (see parseCallArgList / §6.2.1). Self-calls resolve against the enclosing
    // object/class; plain calls against global functions of that name.
    BraceArgHints braceHints;
    functionDef* resolvedGlobal = nullptr;   // the overload bindGlobalCall picked, for emission
    if(isSelfCall){
        string recvType = currentObject ? currentObject->name : (currentClass ? currentClass->name : string());
        if(!recvType.empty()) braceHints = braceArgHints(collectMethodCandidates(recvType, callName));
    } else {
        braceHints = braceArgHints(collectGlobalCandidates(callName));
    }
    // Parse args as proper expressions (shared with statement-level path)
    ParsedArgList pal = parseCallArgList(func, body, braceHints);
    lastRefBacks.clear();   // the arguments' own calls have taken theirs
    rejectInterpolatedArgsInExpression(pal, callName);
    // Resolve and validate
    string retType;
    functionDef* staticSelf = nullptr;   // a static method called by its bare name inside its class
    functionDef* selfTarget = nullptr;   // the member a self call reaches, for its emitted name
    if(isSelfCall){
        // Beguile names are case-insensitive; canonical form is lowercased. callName may
        // be the user's case-preserved spelling (camelCase, etc.), so normalize for compare.
        string canonName = callName;
        transform(canonName.begin(), canonName.end(), canonName.begin(), ::tolower);
        // Recursive self-call: currentFunc is the function being parsed and isn't yet in
        // currentObject->members. Use it directly for retType when names match.
        // With sibling overloads of the name (`Add(a)` inside `Add(a, b)`), the arguments choose.
        set<string> signatures;
        auto noteOverloads = [&](const vector<typeMember*>& members){
            for(typeMember* m : members)
                if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->name == canonName){
                    string sig;
                    for(paramDef* p : fd->params) sig += p->type.name + ",";
                    signatures.insert(sig);
                }
        };
        if(currentObject != nullptr) noteOverloads(currentObject->members);
        classDef* selfClass = currentClass != nullptr ? currentClass : currentObject != nullptr ? currentObject->objectClass : nullptr;
        for(classDef* c = selfClass; c != nullptr; c = c->baseClasses.empty() ? nullptr : c->baseClasses.front())
            noteOverloads(c->members);
        if(currentFunc != nullptr && currentFunc->name == canonName) noteOverloads({currentFunc});   // not in the class yet
        bool overloaded = signatures.size() > 1;
        bool recursing = currentFunc != nullptr && currentFunc->name == canonName && !overloaded;
        if(recursing)
            retType = currentFunc->returnType.name;
        functionDef* selfMethod = nullptr;
        if(currentObject != nullptr && !overloaded)
            for(typeMember* m : currentObject->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == canonName){ selfMethod = fd; if(retType.empty()) retType = fd->returnType.name; break; }
        // Inherited: resolved on self's type, so overloads bind by their arguments.
        if(selfMethod == nullptr && !recursing){
            string selfType = currentObject ? currentObject->name : (currentClass ? currentClass->name : string());
            if(!selfType.empty())
                if(functionDef* m = resolveMethod(selfType, "self", canonName, pal.args).method){
                    selfMethod = m;
                    if(retType.empty()) retType = m->returnType.name;
                }
            // An inline accessor's own members, while its host is the current object.
            if(selfMethod == nullptr && currentObject != nullptr && currentClass != nullptr)
                if(functionDef* m = resolveMethod(currentClass->name, "self", canonName, pal.args).method){
                    selfMethod = m;
                    if(retType.empty()) retType = m->returnType.name;
                }
        }
        // If the self-target is an emitter, inline its body here so the call site
        // splices the raw I6 expression instead of dispatching through self.X(...).
        // Without this, sibling emitters become I6 property calls and lose their
        // inline-substitution semantics.
        // A method of that name that no overload fits: report it as a call through self would.
        if(selfMethod == nullptr && !recursing){
            string selfType = currentObject ? currentObject->name : (currentClass ? currentClass->name : string());
            classDef* sc = selfType.empty() ? nullptr : getDispatchClass(selfType);
            if(sc == nullptr && currentObject != nullptr) sc = currentObject->objectClass;
            // No overload of the member fits, but a global function of the name may: as with one overload, the call is its.
            if(languageService.findGlobalAs<functionDef>(canonName) != nullptr) isSelfCall = false;
            else if(sc != nullptr && findMemberInHierarchy(sc, [&](typeMember* m){
                   return dynamic_cast<functionDef*>(m) != nullptr && m->name == canonName; }) != nullptr){
                vector<string> named; vector<vector<interpolatedSegment>> interp;
                bindMethodCall(selfType, "self", callName, pal.args, named, interp);
            }
        }
        functionDef* target = selfMethod != nullptr ? selfMethod
                            : recursing ? currentFunc : nullptr;
        if(target != nullptr && target->isStatic && !target->isEmitter) staticSelf = target;
        selfTarget = target;
        noteMethodRecursion(target);
        if(target != nullptr) finalizeCallArgs(pal.args, pal.namedArgNames, pal.interpSegmentsPerArg, target);   // defaulted arguments
        // An `extend class` below may overload it: the set settles its names now, as a call through an instance does.
        if(target != nullptr && !target->isEmitter)
            mangleOverloadSetForReceiver(currentObject ? currentObject->name : (currentClass ? currentClass->name : string()), canonName);
        if(selfMethod && selfMethod->isEmitter && selfMethod->isValueEmitter)
            parsingError(format("'{0}' is an emitter value, not a function; use it without parentheses ('{0}', not '{0}()')", callName));
        if(selfMethod && selfMethod->isEmitter){
            if(auto* blk = dynamic_cast<i6Block*>(selfMethod->body)){
                emitterBindings sb; sb.self = "self"; sb.val = "self"; sb.trim = emitterTrim::wsSemi;
                sb.fn = selfMethod;
                for(expression* a : pal.args) sb.args.push_back(a->text());
                for(expression* a : pal.args) sb.argTypes.push_back(a->resolvedType);
                expr->tokens.push_back(expandEmitterBody(blk, sb));
                if(expr->resolvedType.empty() && !retType.empty()) expr->resolvedType = retType;
                return true; // emitter inlined — caller should continue
            }
        }
    }
    if(!isSelfCall){
        GlobalCallBinding gcb = bindGlobalCall(callName, pal.args, pal.namedArgNames,
                                                 pal.interpSegmentsPerArg, func, body);
        resolvedGlobal = gcb.method;
        if(!gcb.funcVarReturnType.empty())      retType = gcb.funcVarReturnType;
        else if(gcb.method != nullptr)          retType = gcb.method->returnType.name;
        else                                    retType = "var"; // loose mode: unresolved → opaque
        // A value emitter is NOT callable: `bold` (value, §14.4.5) and `bold()` (zero-arg function)
        // are distinct declarations, so parentheses on a value are an error, not a tolerated no-op.
        if(gcb.method && gcb.method->isEmitter && gcb.method->isValueEmitter)
            parsingError(format("'{0}' is an emitter value, not a function; use it without parentheses ('{0}', not '{0}()')", callName));
        // Emitter inlining: substitute params and push as single token
        if(gcb.method && gcb.method->isEmitter){
            if(auto* blk = dynamic_cast<i6Block*>(gcb.method->body)){
                emitterBindings gb; gb.fn = gcb.method; gb.trim = emitterTrim::wsSemi;
                for(expression* a : pal.args) gb.args.push_back(a->text());
                for(expression* a : pal.args) gb.argTypes.push_back(a->resolvedType);
                expr->tokens.push_back(expandEmitterBody(blk, gb));
                if(expr->resolvedType.empty() && !retType.empty()) expr->resolvedType = retType;
                return true; // emitter inlined — caller should continue
            }
        }
    }
    if(retType == "void" && !allowVoidReturnExpr)
        parsingError(format("Cannot use void function '{0}' in an expression",
            [&]{ auto* fd = languageService.findGlobalAs<functionDef>(callName); return fd ? fd->dName() : callName; }()));
    if(expr->resolvedType.empty() && !retType.empty()) expr->resolvedType = retType;
    // Flatten parsed args back to tokens for the enclosing expression. Qualify the callee: a bare
    // name only resolves in I6 for a global routine, but an object-member call (e.g. a method reached
    // through `#using bgl.printRules`) must emit as `Owner.method(…)`. qualifyIdentifier returns the
    // bare name for a global and the owner-qualified path for a member, so it's correct either way —
    // matching the statement-call path, which qualifies via emitObjectPath.
    string callEmit = callName;
    if(!isSelfCall){
        // An overload set emits the routine name of the overload that was resolved — the Beguile
        // name belongs to all of them, so asking qualifyIdentifier for it would be asking which
        // one, a question only the arguments answer.
        bool importedMember = false;
        string importOwner;
        if(resolvedGlobal != nullptr)
            for(objectDef* imp : usingObjectImports)
                for(typeMember* m : imp->members) if(m == resolvedGlobal){ importedMember = true; importOwner = imp->name; }
        // A global function called beside an imported method of its name: the global, as a statement says too.
        if(resolvedGlobal != nullptr && !importedMember)
            for(objectDef* imp : usingObjectImports)
                for(typeMember* m : imp->members)
                    if(auto* f = dynamic_cast<functionDef*>(m); f && f->name == resolvedGlobal->name && !f->isEmitter)
                        warnOnceHere(format("'{0}' is both global function '{0}' and an imported member; the global is used. "
                            "Write the imported one with its full path.", resolvedGlobal->name));
        if(resolvedGlobal != nullptr && !importedMember && !resolvedGlobal->i6name.empty() && !resolvedGlobal->isEmitter)
            callEmit = resolvedGlobal->i6name;
        else if(resolvedGlobal != nullptr && isUsingImportedValueEmitter(callName))
            ;   // the call is the global function's; a value emitter of the same name is never called
        else if(resolvedGlobal != nullptr && find(languageService.globals.begin(), languageService.globals.end(), resolvedGlobal) != languageService.globals.end())
            ;   // a global function, called by its own name: not a member of self that shares it
        else {
            // An imported method is its object's, even beside a global (a verb) of its name that can't be called.
            string q = importedMember && !importOwner.empty() ? importOwner + "." + callName : qualifyIdentifier(callName, func, body);
            if(!q.empty()) callEmit = q;
            // A method of an imported object (`bgl.printRules.link`): the overload's own routine name.
            size_t dot = callEmit.rfind('.');
            if(resolvedGlobal != nullptr && !resolvedGlobal->isEmitter && !resolvedGlobal->isStatic && dot != string::npos){
                string owner = callEmit.substr(0, dot);
                if(objectDef* od = languageService.findObjectType(owner)){
                    mangleOverloadSetForReceiver(od->name, resolvedGlobal->name);
                    if(!resolvedGlobal->i6name.empty()) callEmit = owner + "." + resolvedGlobal->i6name;
                }
            }
        }
        // A static method a `#using` class import names: the class's free routine, not a message send.
        if(resolvedGlobal != nullptr && resolvedGlobal->isStatic && !resolvedGlobal->isEmitter)
            for(classDef* imp : usingImports)
                if(classDef* owner = imp->declaringClass(resolvedGlobal)){
                    mangleOverloadSetForReceiver(owner->name, resolvedGlobal->name);
                    callEmit = i6Emitter::staticRoutineName(owner, resolvedGlobal);
                    break;
                }
    }
    if(staticSelf != nullptr){
        // It has no receiver: it emits as the declaring class's free routine.
        classDef* selfClass = currentClass != nullptr ? currentClass
                            : currentObject != nullptr ? currentObject->objectClass : nullptr;
        classDef* declaring = selfClass;
        for(classDef* c = selfClass; c != nullptr; ){
            bool own = false;
            for(typeMember* m : c->members) if(m == staticSelf){ own = true; break; }
            if(own){ declaring = c; break; }
            c = c->baseClasses.empty() ? nullptr : c->baseClasses.front();
        }
        if(declaring != nullptr) mangleOverloadSetForReceiver(declaring->name, staticSelf->name);
        // The overload being parsed (or one not yet in the class) takes the name its stub settled.
        if(declaring != nullptr && staticSelf->i6name.empty())
            for(typeMember* m : declaring->members)
                if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd != staticSelf && fd->name == staticSelf->name
                   && !fd->i6name.empty() && sameParamTypes(fd, staticSelf)){ staticSelf->i6name = fd->i6name; break; }
        callEmit = declaring != nullptr ? i6Emitter::staticRoutineName(declaring, staticSelf) : callName;
    }
    // A lambda calling a method of the class it is written in reaches it through `self`, which it captures.
    if(isSelfCall && staticSelf == nullptr && lambdaOuterFunc != nullptr && currentFunc != nullptr && currentFunc->isLambda)
        qualifyFromCaptures("self");
    // A member renamed for I6 (`_m_score`, an overload's mangled name) is sent under that name.
    string selfName = selfTarget != nullptr ? methodCallName(selfTarget, callName) : callName;
    string calleeText = staticSelf != nullptr ? callEmit : isSelfCall ? "self." + selfName : callEmit;
    if(callPastLocal && func != nullptr && calleeText.find('.') == string::npos){
        (currentFunc != nullptr ? currentFunc : func)->globalEscapes.insert(callName);
        calleeText = kGlobalEscapeMarker + calleeText;
    }
    size_t callStart = expr->tokens.size();
    vector<string> argTexts;
    vector<pair<string*, string>> operands;
    for(expression* a : pal.args) argTexts.push_back(a->text());
    // A call through a func<> value has no definition to settle its arguments: a value-class result each
    // argument returns is kept apart, as finalizeCallArgs does for a direct call.
    if(resolvedGlobal == nullptr && selfTarget == nullptr && staticSelf == nullptr && pal.args.size() > 1)
        for(size_t i = 0; i < pal.args.size(); i++) argTexts[i] = keepValueResult(argTexts[i], pal.args[i]->resolvedType);
    for(size_t i = 0; i < pal.args.size(); i++) operands.push_back({&argTexts[i], pal.args[i]->resolvedType});
    // A union parameter takes no copy of its argument, so a temporary passed to one is held for the call.
    bool toUnion = false;
    if(functionDef* callee = resolvedGlobal != nullptr ? resolvedGlobal : selfTarget)
        for(size_t i = 0; i < pal.args.size() && i < callee->params.size(); i++)
            if(const string& pt = callee->params[i]->type.name; (isUnionType(pt) || (getDispatchClass(pt) != nullptr && getDispatchClass(pt)->isUnion()))
               && argTexts[i].find('(') != string::npos) toUnion = true;
    if(HeldTemporaries held = holdCallTemporaries(operands, toUnion); held.any()){
        string call = calleeText + "(";
        for(size_t i = 0; i < argTexts.size(); i++) call += (i ? ", " : "") + argTexts[i];
        expr->tokens.push_back(wrapHeldCall(held, call + ")"));
    } else {
        expr->tokens.push_back(calleeText);
        expr->tokens.push_back(token::parenOpen);
        for(size_t i = 0; i < argTexts.size(); i++){
            if(i > 0) expr->tokens.push_back(",");
            expr->tokens.push_back(argTexts[i]);
        }
        expr->tokens.push_back(token::parenClose);
    }
    if(!lastRefBacks.empty()){
        string callText;
        for(size_t i = callStart; i < expr->tokens.size(); i++) callText += expr->tokens[i];
        expr->tokens.resize(callStart);
        expr->tokens.push_back(wrapRefBacks(callText));
    }
    // `pick(0)(5)`: the call yields a func<…>, which the next argument list calls; the result is its return type.
    while(file.peekToken().is(token::parenOpen) && retType.rfind("func<", 0) == 0){
        vector<string> ta = funcSignatureArgs(retType);
        file.getToken(token::parenOpen);
        ParsedArgList more = parseCallArgList(func, body);
        rejectInterpolatedArgsInExpression(more, callName);
        string callText;
        for(size_t i = callStart; i < expr->tokens.size(); i++) callText += expr->tokens[i];
        expr->tokens.resize(callStart);
        string call = "(" + callText + ")(";
        for(size_t i = 0; i < more.args.size(); i++) call += (i ? ", " : "") + more.args[i]->text();
        expr->tokens.push_back("(" + call + "))");
        if(expr->resolvedType == retType) expr->resolvedType = ta.empty() ? string("var") : ta[0];
        retType = ta.empty() ? string("var") : ta[0];
    }
    return false;
}

// `!x` through x's type's `operator !` (an emitter inline, a method through its routine); false when
// the type declares none.
bool bglParser::applyNotOperator(expression* expr, const string& opType, const string& opText){
    classDef* cls = getDispatchClass(opType);
    if(cls == nullptr) return false;
    if(typeMember* m = findMemberInHierarchy(cls, [&](typeMember* tm){
        auto* fn = dynamic_cast<functionDef*>(tm);
        return fn && fn->name == "!" && fn->params.empty() && fn->isEmitter;
    })){
        functionDef* notOp = dynamic_cast<functionDef*>(m);
        if(i6Block* blk = dynamic_cast<i6Block*>(notOp->body)){
            emitterBindings nob; nob.self = opText; nob.val = opText; nob.trim = emitterTrim::ws;
            expr->tokens.push_back(expandEmitterBody(blk, nob));
            if(expr->resolvedType.empty()) expr->resolvedType = notOp->returnType.name;
            return true;
        }
    }
    // Non-emitter operator!: dispatch through its mangled routine, the same way a
    // non-emitter operator= or conversion operator() does. Without this the declaration
    // is accepted and then ignored, and the fallback emits `~~obj` — I6's not of
    // the object's ADDRESS, which is never zero, so the test is silently always false.
    if(typeMember* m = findMemberInHierarchy(cls, [&](typeMember* tm){
        auto* fn = dynamic_cast<functionDef*>(tm);
        return fn && fn->name == "!" && fn->params.empty() && !fn->isEmitter;
    })){
        functionDef* notOp = dynamic_cast<functionDef*>(m);
        if(notOp->i6name.empty()) notOp->i6name = mangleOperatorName(notOp->name);
        expr->tokens.push_back(opText + "." + notOp->i6name + "()");
        if(expr->resolvedType.empty()) expr->resolvedType = notOp->returnType.name;
        return true;
    }
    return false;
}

// Prefix logical-not: handles !name, !name? (negated query), and fallback to ~~.
// operand is the token following '!'. Sets prefetched if the operand needs re-processing.
bool bglParser::parseExprPrefixNot(expression* expr, token operand, optional<token>& prefetched,
                                    functionDef* func, statementBlock* body){
    if(operand.is(eTokenType::name)){
        string opType = resolveIdentifierType(operand.value, func, body);
        string opText = (func != nullptr) ? qualifyIdentifier(operand.value, func, body) : operand.value;
        if(opText.empty()) opText = operand.value;
        // Check for !v? (negated postfix query) — only if type has operator?() and the `?` ends the
        // operand: `!v ? a : b` is a ternary on `!v`.
        token afterQ = file.peekToken(2);
        bool ternaryAhead = afterQ.is(eTokenType::name) || afterQ.is(eTokenType::integer) || afterQ.is(eTokenType::floatLiteral)
            || afterQ.isString() || afterQ.is(eTokenType::charLiteral) || afterQ.is(eTokenType::dictionaryWord)
            || afterQ.is(token::parenOpen) || afterQ.value == "!" || afterQ.value == "-" || afterQ.value == "$" || (afterQ.is("new") && !afterQ.is(eTokenType::dictionaryWord));
        if(file.peekToken(1).is("?") && !ternaryAhead){
            classDef* cls = getDispatchClass(opType);
            functionDef* queryFn = nullptr;
            if(cls != nullptr)
                queryFn = findQueryOp(cls, opType);
            if(queryFn != nullptr){
                file.getToken(); // consume '?'
                auto* blk = dynamic_cast<i6Block*>(queryFn->body);
                emitterBindings qb; qb.self = opText; qb.val = opText; qb.trim = emitterTrim::wsSemi;
                expr->tokens.push_back("~~(" + expandEmitterBody(blk, qb) + ")");
                if(expr->resolvedType.empty()) expr->resolvedType = queryFn->returnType.name;
                return true;
            }
        }
        if(applyNotOperator(expr, opType, opText)) return true;
    }
    // Fallback: emit ~~ (I6 NOT) and put operand back for normal processing. `!x` is a bool, whatever x is
    // (`!best || p > best` with best a class-typed member).
    if(operand.is(eTokenType::name) && expr->resolvedType.empty()
       && [&]{ string t = resolveIdentifierType(operand.value, func, body); classDef* c = getDispatchClass(t);
               return c != nullptr && !c->isPrimitive && nativeGetterOf(t) == nullptr; }()
       && !(file.peekToken(1).is(token::period) || file.peekToken(1).is("?.") || file.peekToken(1).is(token::parenOpen)
            || file.peekToken(1).is(token::bracketOpen))){
        string opText = func != nullptr ? qualifyIdentifier(operand.value, func, body) : operand.value;
        expr->tokens.push_back("(~~(" + (opText.empty() ? operand.value : opText) + "))");
        expr->resolvedType = "bool";
        return true;
    }
    expr->tokens.push_back("~~");
    prefetched = operand;
    return true; // always handled (either emitter or fallback)
}

// Whether member `fd` can take the call being parsed. Only a global function of the same name makes
// this matter: then a member whose parameters can't take the peeked argument count gives way to it.
// `openConsumed`: the call's '(' has already been read.
bool bglParser::memberTakesCall(functionDef* fd, const string& name, bool openConsumed){
    if(languageService.findGlobalAs<functionDef>(name) == nullptr) return true;
    int depth = 0, commas = 0, n = -1;
    const int first = openConsumed ? 1 : 2;   // peekToken(1) is the next token
    for(int i = first; n < 0; i++){
        token t = file.peekToken(i);
        if(t.is(eTokenType::eof)) return true;
        if(t.is(token::parenOpen) || t.is(token::bracketOpen) || t.is(token::braceOpen)) depth++;
        else if(t.is(token::parenClose) || t.is(token::bracketClose) || t.is(token::braceClose)){
            if(depth == 0) n = (i == first) ? 0 : commas + 1;
            else depth--;
        }
        else if(t.is(token::comma) && depth == 0) commas++;
    }
    size_t required = 0;
    for(paramDef* p : fd->params) if(p->defaultValue.empty()) required++;
    return (size_t)n >= required && (size_t)n <= fd->params.size();
}

// ── Namespace-scoped type resolution ─────────────────────────────────────────
// Walks a dotted path (e.g. "bgl.glulx.window") through namespace objects.
// At each intermediate step, follows value alias members (auto x = Obj;) to the
// next namespace object. At the final step, looks for an alias member (isAlias=true)


// True when `t` ends this expression: one of the caller's terminator tokens, seen at
// the activation's own paren depth.
bool bglParser::exprIsTerminator(const ExprParseState& st, const token& t){
    const int& parenDepth = st.parenDepth;
    const int& startParenDepth = st.startParenDepth;
    const vector<string>& terminators = *st.terminators;

    if(parenDepth > startParenDepth) return false;  // inside our own parens — not a terminator
    // A literal is a value, whatever its text: '?' or ';' as a character is not punctuation.
    if(t.tokenType == eTokenType::charLiteral || t.tokenType == eTokenType::quote || t.tokenType == eTokenType::rawQuote) return false;
    // With nothing parsed yet, `&`, `-` and `!` start an operand (address-of, negation, not); they end nothing.
    if(st.expr != nullptr && st.expr->tokens.empty() && (t.value == "&" || t.value == "-" || t.value == "!")) return false;
    for(const string& term : terminators)
        if(t.value == term) return true;
    return false;
}

// The expression loop's token source: a token parked by a sub-parse first, then the
// parser-wide stash, then the lexer.
token bglParser::exprNext(ExprParseState& st){
    optional<token>& prefetched = st.prefetched;

    if(prefetched.has_value()){ token t = *prefetched; prefetched = nullopt; return t; }
    if(stashedToken.has_value()){ token t = *stashedToken; stashedToken = nullopt; return t; }
    return file.getToken();
}

// Raw binary operator whose LHS has no emitter (boolean/non-class LHS, e.g. `true && …`,
// `suppress == false && …`). The operator itself passes through as raw I6, but the RHS may
// still contain emitter-class operations (e.g. `obj.parent == player`). Parse the RHS as its
// own sub-expression so those resolve, then emit `op <rhs>`. Without this the RHS shares this
// flat expression's stale (non-class) resolvedType and its operators never dispatch — they
// leak out as raw I6 property access (`obj.parent` instead of `parent(obj)`). Mirrors the RHS
// precedence sub-parse in applyBinaryOperator(). Non-precedence ops (level <0, e.g. compound
// assign) keep the single-token passthrough.
void bglParser::exprEmitRawBinaryOp(ExprParseState& st, const string& opTok){
    expression* expr = st.expr;
    int& parenDepth = st.parenDepth;
    optional<token>& prefetched = st.prefetched;
    const vector<string>& terminators = *st.terminators;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    // Only logical && / || need the RHS sub-parse: their RHS is a full boolean sub-expression
    // that may contain emitter-class comparisons (`obj.parent == player`). Other raw operators
    // (arithmetic, bitwise, comparison) keep the token-by-token passthrough — those RHSs
    // don't open a fresh emitter dispatch context.
    // A comparison's right side is typed on its own too: `f == (g | x)` must not read `g` as the
    // left operand of `|` once the comparison has made the expression a bool.
    // A binary operator needs a left operand: at the start, or after another operator, `+` is a unary
    // plus (Inform 6 has none, so it is dropped) and anything else is missing its operand.
    static const std::set<string> binaryOnly = {"+", "*", "/", "%", "<<", ">>", "<", ">", "<=", ">=", "==", "!=",
        "~=", "&&", "||", "|", "^", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", "=", "<=>"};
    if(binaryOnly.count(opTok)){
        const string prev = expr->tokens.empty() ? string("(") : expr->tokens.back();
        if(prev == "(" || prev == "," || binaryOnly.count(prev) || prev == "-" || prev == "!" || prev == "~~"){
            if(opTok == "+") return;
            parsingError(format("Missing operand before '{0}'.", opTok));
        }
    }
    static const std::set<string> comparisons = {"==", "!=", "~=", "<", ">", "<=", ">="};
    bool comparison = comparisons.count(opTok) > 0;
    if(opTok != "&&" && opTok != "||" && !comparison){
        expr->tokens.push_back(opTok);
        return;
    }
    int rawPrec = operatorPrecedence(opTok);
    if(rawPrec < 0){ expr->tokens.push_back(opTok); return; }
    vector<string> rhsTerminators = terminators;
    for(const string& op : kPrecedenceOps)
        if(operatorPrecedence(op) <= rawPrec)
            rhsTerminators.push_back(op);
    if(parenDepth > 0) rhsTerminators.push_back(token::parenClose);
    rhsTerminators.push_back("?");
    expression* rhsExpr = parseExpression(exprNext(st), rhsTerminators, func, body);
    if(rhsExpr->tokens.empty() && dynamic_cast<initializerList*>(rhsExpr) == nullptr)
        parsingError(format("'{0}' has nothing after it: an operand is missing.", opTok));
    absorbPostfixQuery(rhsExpr);   // `x && a?` queries `a`
    // The LHS accumulated so far may be a bare `~~x`, and I6's `~~` binds looser than
    // && / || — so `~~x && y` would parse as `~~(x && y)`. Parenthesise both sides.
    // Collapsing is safe here: the wrap only applies when the text STARTS with `~~`,
    // so there are no preceding structural parens to disturb.
    {
        string lhsText = expr->text();
        string wrapped = parenIfNegated(lhsText);
        if(wrapped != lhsText){ expr->tokens.clear(); expr->tokens.push_back(wrapped); }
    }
    expr->tokens.push_back(opTok);
    expr->tokens.push_back(parenIfNegated(rhsExpr->text()));
    if(comparison) expr->resolvedType = "bool";
    token terminatorTok;
    terminatorTok.value = rhsExpr->terminator;
    terminatorTok.tokenType = eTokenType::oper;
    if(rhsExpr->terminator == ";" || rhsExpr->terminator == ")" || rhsExpr->terminator == "?")
        terminatorTok.tokenType = eTokenType::symbol;
    prefetched = terminatorTok;
}

// The set-up a ternary branch's own expression produced (a nested ternary, a `?.` test), taken out of
// the statement's pending list so it runs inside that branch only. Plain raw text is moved; anything
// else stays where it is, ahead of the whole ternary.
string bglParser::takeBranchSetUp(size_t mark){
    if(mark >= pendingInjections.size()) return "";
    for(size_t i = mark; i < pendingInjections.size(); i++){
        auto* raw = dynamic_cast<i6RawNode*>(pendingInjections[i]);
        if(raw == nullptr || !raw->parts.empty() || raw->isI6Island) return "";
    }
    // A lambda's capture load stays ahead of the whole ternary: its copy-back runs after the
    // statement whichever branch was taken.
    string text;
    vector<statement*> kept;
    for(size_t i = mark; i < pendingInjections.size(); i++){
        if(captureLoads.count(pendingInjections[i])){ kept.push_back(pendingInjections[i]); continue; }
        text += dynamic_cast<i6RawNode*>(pendingInjections[i])->text + " ";
    }
    pendingInjections.resize(mark);
    pendingInjections.insert(pendingInjections.end(), kept.begin(), kept.end());
    return text;
}

// Helper: assemble a pending ternary — creates injection, replaces expr with temp name
void bglParser::exprAssembleTernary(ExprParseState& st){
    expression* expr = st.expr;
    using PendingTernary = ExprParseState::PendingTernary;
    vector<ExprParseState::PendingTernary>& pendingTernaries = st.pendingTernaries;

    if(pendingTernaries.empty()) return;
    PendingTernary pt = pendingTernaries.back();
    pendingTernaries.pop_back();
    string falseText = expr->text();
    string falseType = expr->resolvedType;
    if(falseType == "void")
        parsingError("A ?: branch must have a value, and this one is a call that returns none.");
    // Prefix parens the condition left belong to the outer expression, not to this branch.
    {
        string branch = falseText;
        for(size_t i = 0; i < pt.prefixParens.size() && !branch.empty(); i++)
            if(branch.compare(0, pt.prefixParens[i].size(), pt.prefixParens[i]) == 0) branch = branch.substr(pt.prefixParens[i].size());
        if(branch.find_first_not_of(" ") == string::npos) parsingError("The ?: has no value after its ':'.");
        if(pt.trueText.find_first_not_of(" ") == string::npos) parsingError("The ?: has no value before its ':'.");
    }
    string falseSetUp = takeBranchSetUp(pt.falseMark);
    string injText = "if (" + pt.condText + ") { " + pt.trueSetUp + pt.tempName + " = " + pt.trueText
                   + "; } else { " + falseSetUp + pt.tempName + " = " + falseText + "; }";
    i6RawNode* inj = new i6RawNode();
    inj->text = injText;
    inj->cooked = true;   // built from Beguile expressions: locals take their renames
    pendingInjections.push_back(inj);
    expr->tokens.clear();
    expr->emitterSelf.clear();   // the result is the temporary, not a member read in the condition
    // Restore structural prefix parens so the outer expression stays balanced
    for(auto& p : pt.prefixParens) expr->tokens.push_back(p);
    expr->tokens.push_back(pt.tempName);
    // A ternary yields ONE value, so its type has to account for both branches. When they agree —
    // the same type, or a base/derived pair, checked both ways so the base wins whichever side it
    // is written on — that is the type. Literals compare as the type they denote, so `-1` and `0`
    // are both ints. `var` on either side is compatible with everything.
    //
    // When they DON'T agree, the result is the union of the two (§2.9): a ternary over a string and
    // a routine is exactly what `string | func<void>` describes. Nothing is lost — a union value is
    // one word either way, which is why this used to pass untyped — and the destination now decides
    // whether it is legal: a union-typed target accepts it, and a target of one branch's type
    // rejects it as the mistyping it is.
    auto denotedType = [](const string& t){
        if(t == "intliteral" || t == "negativeintliteral") return string("int");
        if(t == "charliteral")  return string("char");
        if(t == "stringliteral") return string("string");
        return t;
    };
    if(!pt.trueType.empty() && !falseType.empty()){
        string tt = denotedType(pt.trueType), ft = denotedType(falseType);
        // Two objects (or classes) of one family are their nearest common class, below the root.
        auto commonClass = [&]() -> string {
            auto clsOf = [&](const string& t) -> classDef* {
                if(string oc = objectsClassName(t); !oc.empty()) return languageService.findClass(oc);
                return languageService.findClass(t);
            };
            classDef* a = clsOf(tt); classDef* b = clsOf(ft);
            if(a == nullptr || b == nullptr || a->isEmitterClass || b->isEmitterClass) return "";
            vector<classDef*> line{a};
            for(classDef* x : a->ancestorsNearestFirst()) line.push_back(x);
            for(classDef* c : line){
                if(c->baseClasses.empty() || c->name == "object") break;   // the roots say nothing
                if(b == c || b->hasAncestor(c)) return c->name;
            }
            return "";
        };
        if(isTypeCompatible(ft, tt))      expr->resolvedType = pt.trueType;
        else if(isTypeCompatible(tt, ft)) expr->resolvedType = falseType;
        else if(string cc = commonClass(); !cc.empty()) expr->resolvedType = cc;
        else                              expr->resolvedType = canonicalUnionOf({tt, ft});
    }
    else expr->resolvedType = !pt.trueType.empty() ? pt.trueType : falseType;
}

// Cast emission helper. When `(T)x` is parsed, the consumer site calls this with the
// source's emitted text and resolved type plus the target type. If the source type has
// an `operator() → T` emitter, its body is substituted and returned as the new text;
// if the body uses `$target` (statement-form, e.g. `@numtof $val $target`), a temp slot
// is allocated and the substituted body is queued as a side-effect injection. When no
// matching emitter exists, the source text is returned unchanged — relabel-only
// behavior for bit-compatible casts like int↔uint.
string bglParser::applyCastConversion(const string& srcText, const string& srcType,
                                      const string& targetType){
    if(targetType.empty() || srcType.empty() || srcType == targetType) return srcText;
    // A verb named directly, as an int, is its action number (`(int)PutOn` is ##PutOn).
    if(srcType == "verb" && (targetType == "int" || targetType == "uint")){
        expression v; v.tokens.push_back(srcText); v.resolvedType = "verb";
        if(applyActionConstant(&v)) return v.tokens[0];
    }
    classDef* srcCls = getDispatchClass(srcType);
    if(srcCls == nullptr) return srcText;
    typeMember* found = findMemberInHierarchy(srcCls, [&](typeMember* m){
        auto* fn = dynamic_cast<functionDef*>(m);
        return fn && fn->name == "operator()" && fn->params.empty()
               && fn->isEmitter && fn->returnType.name == targetType
               && dynamic_cast<i6Block*>(fn->body) != nullptr;
    });
    // A char converts as the int it widens to (§12.4): `(float)c` is a number, not reinterpreted bits.
    if(found == nullptr && (srcType == "char" || srcType == "charliteral") && targetType != "int")
        return applyCastConversion(srcText, "int", targetType);
    if(found == nullptr){
        // Non-emitter (regular-method) conversion operator → call the emitted routine, mirroring
        // the non-emitter operator= dispatch. This is the parity that lets a regular Beguile-method
        // `operator()` execute at a conversion site, not just an emitter operator().
        typeMember* nm = findMemberInHierarchy(srcCls, [&](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn && fn->name == "operator()" && fn->params.empty()
                   && !fn->isEmitter && fn->returnType.name == targetType;
        });
        if(nm){
            auto* fn = dynamic_cast<functionDef*>(nm);
            if(fn->i6name.empty()) fn->i6name = mangleOperatorName(fn->name);
            return parenthesizeReceiver(srcText) + "." + fn->i6name + "()";
        }
        return srcText;
    }
    auto* fn = dynamic_cast<functionDef*>(found);
    auto* blk = dynamic_cast<i6Block*>(fn->body);
    emitterBindings cv; cv.self = srcText; cv.val = srcText; cv.trim = emitterTrim::wsSemi;
    string b = expandEmitterBody(blk, cv);
    if(b.empty()) return srcText;
    if(b.find("$target") != string::npos){
        string tempName = newChainTemp();
        b = i6Emitter::replaceWord(b, "$target", tempName);
        i6RawNode* inj = new i6RawNode();
        while(!b.empty() && b.back() == ';') b.pop_back();
        inj->text = b + ";";
        inj->cooked = true;   // built from Beguile expressions: locals take their renames
        pendingInjections.push_back(inj);
        return tempName;
    }
    return b;
}

// ─── `Type::operator <op>` — a user-facing operator reference ────
// Yields a callable address for one of a type's operators, the same lookup
// `$opref` performs inside emitter bodies (§14.4.3). Only a `static` operator
// has an address; an instance operator inlines or dispatches through a
// property, so it is not referenceable and is reported as such. An optional
// parenthesized operand type selects among overloads:
//     arr.sort(string::operator <=>)
//     Money::operator ==(int)
// The lexer glues "::" onto the following identifier, so the qualifier arrives
// as one "::operator" token rather than two.
bglParser::ExprStep bglParser::parseExprOperatorRef(ExprParseState& st){
    expression* expr = st.expr;
    token& cur = st.cur;

    string refType = cur.value;
    if(file.getToken().value == "::") file.getToken();   // consume "::operator", or "::" "operator" when spaced
    token opTok = file.getToken();
    string opName = opTok.value;
    // Multi-token operators the lexer splits (e.g. "[" "]") are not referenceable
    // here; the named binary/comparison operators are what generic code wants.
    static const set<string> overloadable = {"+", "-", "*", "/", "%", "&", "|", "^", "<", ">", "<=", ">=",
                                             "==", "!=", "<=>", "<<", ">>", "&&", "||", "=~", "="};
    if(!overloadable.count(opName)
       && find(languageService.operators.begin(), languageService.operators.end(), opName) == languageService.operators.end())
        parsingError(format("'{0}' is not an operator name in "
                            "'{1}::operator {0}'", opName, refType));
    string operand;
    if(file.peekToken(1).is(token::parenOpen)){
        file.getToken();                              // consume "("
        operand = file.getToken().value;
        token close = file.getToken();
        if(close.isNot(token::parenClose))
            parsingError(format("expected ')' after the operand type in "
                                "'{0}::operator {1}({2}'", refType, opName, operand));
    }
    // No type list: the func<> it is stored in names the operand (`func<int, Mass, int> c = Mass::operator <=>;`).
    if(operand.empty() && currentExpectedType.rfind("func<", 0) == 0){
        vector<string> fa = funcSignatureArgs(currentExpectedType);
        if(fa.size() == 3) operand = fa[2];
    }
    bool isStatic = false;
    string ref = operatorRef(refType, opName, operand, &isStatic);
    if(ref.empty())
        parsingError(format("'{0}' publishes no referenceable 'operator {1}'. Only a "
                            "'static' operator has an address a reference can name.",
                            typeDisplayName(refType), opName));
    if(!isStatic)
        parsingError(format("'{0}::operator {1}' is an instance operator, which dispatches "
                            "through a property and has no address. Declare a 'static' "
                            "form of the operator to make it referenceable.",
                            typeDisplayName(refType), opName));
    expr->tokens.push_back(ref);
    if(expr->resolvedType.empty()) expr->resolvedType = "func";
    cur = exprNext(st);
    return ExprStep::Continue;
}

void bglParser::rejectInterpolatedArgsInExpression(const ParsedArgList& pal, const string& callee){
    for(auto& segs : pal.interpSegmentsPerArg)
        if(!segs.empty()){
            // A routine can't take one anywhere; only an emitter can, as a statement.
            vector<functionDef*> cands = collectGlobalCandidates(callee);
            if(!cands.empty() && none_of(cands.begin(), cands.end(), [](functionDef* f){ return f->isEmitter; }))
                parsingError(format("An interpolated string ($\"…\") has no value, so it can only be passed to an "
                    "emitter, which prints it in place; '{0}' is a routine. Print the text inside it instead.", callee));
            parsingError(format("An interpolated string ($\"…\") can be passed to '{0}' only in a call that "
                "is a statement of its own, such as `{0}($\"…\");`, not inside an expression.", callee));
        }
}

// `=> body` at the start of an operand — a parameterless lambda, the same as `() => body`.
bglParser::ExprStep bglParser::parseExprBareLambda(ExprParseState& st){
    expression* expr = st.expr;
    string lambdaName = parseLambdaExpr(st.func, st.body, true);
    expr->tokens.push_back(lambdaName);
    expr->resolvedType = funcValueType(lambdaName);
    st.cur = exprNext(st);
    return ExprStep::Continue;
}

// '(' — a lambda literal, a cast prefix, or a structural paren group.
bglParser::ExprStep bglParser::parseExprParenOpen(ExprParseState& st){
    expression* expr = st.expr;
    int& parenDepth = st.parenDepth;
    token& cur = st.cur;
    string& castType = st.castType;
    vector<ExprParseState::PendingParenCast>& parenCastStack = st.parenCastStack;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    // Lambda detection: () => ... OR (type name, ...) => ...
    {
        bool isLambda = false;
        token p1 = file.peekToken(1);
        if(p1.is(")") && file.peekToken(2).is("=>"))
            isLambda = true;
        else if((p1.is(eTokenType::dataType) || p1.is(eTokenType::identifier)) && file.peekToken(2).is(eTokenType::identifier)
                && file.peekToken(2).value.rfind("::", 0) != 0)   // `(Box::operator <=>)` is an operator reference
            isLambda = true;
        else if(p1.is(eTokenType::dataType) && file.peekToken(2).is(eTokenType::dataType)
                && (file.peekToken(3).is(token::comma) || (file.peekToken(3).is(token::parenClose) && file.peekToken(4).is("=>"))))
            isLambda = true;   // `(Crate crate) =>`: a parameter named like its class lexes as a type
        else if(p1.is(eTokenType::identifier) && file.peekToken(2).is(token::period)){
            // A dotted parameter type, `(kinds.pack q) =>`: the path, then a name, then `,` or `)`.
            int k = 2;
            while(file.peekToken(k).is(token::period)
                  && (file.peekToken(k + 1).is(eTokenType::identifier) || file.peekToken(k + 1).is(eTokenType::dataType))) k += 2;
            isLambda = file.peekToken(k).is(eTokenType::identifier)
                    && (file.peekToken(k + 1).is(token::comma) || file.peekToken(k + 1).is(token::parenClose))
                    && !resolveNamespacedType([&]{ string p = p1.value; for(int i = 3; i < k; i += 2) p += "." + file.peekToken(i).value; return p; }()).empty();
        }
        else if((p1.value == "func" || p1.value == "array" || p1.value == "rawarray") && file.peekToken(2).is("<")){
            // A generic parameter type, `(func<int> g) =>`: a name follows its closing `>`.
            int depth = 0, k = 2;
            for(; k < 64; k++){
                token pk = file.peekToken(k);
                if(pk.is("<")) depth++;
                else if(pk.is(">")) depth--;
                else if(pk.is(">>")) depth -= 2;
                else if(pk.is(eTokenType::eof) || pk.is(token::endStatement)) break;
                if(depth <= 0) break;
            }
            isLambda = depth == 0 && file.peekToken(k + 1).is(eTokenType::identifier);
        }
        if(isLambda){
            string lambdaName = parseLambdaExpr(func, body);
            expr->tokens.push_back(lambdaName);
            // Its signature, so a func<…> slot can check it: parameter types as written, the return
            // type inferred (`var` for a block body, which then matches any return type).
            expr->resolvedType = funcValueType(lambdaName);
            cur = exprNext(st);
            return ExprStep::Continue;
        }
    }
    // `(a ?? b)`: `??` is parsed only at the top of an expression, so the group is one of its own.
    {
        int depth = 1;
        bool coalesce = false;
        int scanDepth = 1;
        vector<token> ahead = file.peekTokens(400, [&](token& t){
            if(t.is(token::parenOpen)) scanDepth++;
            else if(t.is(token::parenClose) && --scanDepth == 0) return true;
            return t.is(token::endStatement) || t.is(token::braceOpen) || (scanDepth == 1 && t.value == "??");
        });
        // `(off && x?.m() > 1)`: a group whose `&&`/`||` has a `?.` operand is one of its own too, so the
        // operand's set-up runs only when it is evaluated.
        bool shortCircuit = false, chained = false;
        for(size_t k = 0; k < ahead.size() && depth > 0; k++){
            token& pk = ahead[k];
            if(pk.is(eTokenType::eof) || pk.is(token::endStatement) || pk.is(token::braceOpen)) break;
            if(pk.is(token::parenOpen)) depth++;
            else if(pk.is(token::parenClose)) depth--;
            else if(depth == 1 && pk.value == "??"){ coalesce = true; break; }
            else if(depth == 1 && (pk.value == "&&" || pk.value == "||")) shortCircuit = true;
            else if(pk.value == "?.") chained = true;
        }
        if(shortCircuit && chained && func != nullptr) coalesce = true;
        if(coalesce){
            expression* inner = parseExpression(file.getToken(), {token::parenClose}, func, body);
            string text = inner->text();
            bool oneName = !text.empty() && all_of(text.begin(), text.end(), [](char c){ return isalnum((unsigned char)c) || c == '_'; });
            expr->tokens.push_back(oneName ? text : "(" + text + ")");   // usually the `??` temporary itself
            if(expr->resolvedType.empty()) expr->resolvedType = inner->resolvedType;
            // `((Box)(o ?? b1)).n`: a cast before the group is the group's.
            if(!castType.empty() && expr->tokens.size() >= 1){
                expr->tokens.back() = applyCastConversion(expr->tokens.back(), inner->resolvedType, castType);
                expr->resolvedType = castType;
                castType = "";
            }
            // `(a ?? b)[i]`: the result's own `operator []`.
            if(file.peekToken().is(token::bracketOpen) && expr->tokens.size() == 1){
                string row = expr->tokens.back();
                chainSubscripts(expr, row, func, body);
            }
            cur = exprNext(st);
            return ExprStep::Continue;
        }
    }
    // `((T)obj).member`: the cast retypes the receiver, so the member is T's (for a method of a
    // strict ancestor, T's own version runs). In a chain, `((A)(B)obj)` or `((A)((B)obj))`, the
    // outermost cast is the receiver's type.
    {
        int k = 1, groups = 0, casts = 0;
        string outer, inner;
        while(true){
            // A type alias, by path or bare (`(Ns.Part)`, `(Part)`), casts as its type does.
            int aliasEnd = 0; string aliasType;
            if(file.peekToken(k).is(token::parenOpen) && file.peekToken(k + 1).is(eTokenType::name) && func != nullptr){
                string path = file.peekToken(k + 1).value;
                int e = k + 2;
                while(file.peekToken(e).is(token::period) && file.peekToken(e + 1).is(eTokenType::name)){ path += "." + file.peekToken(e + 1).value; e += 2; }
                if(file.peekToken(e).is(token::parenClose) && !qualifyFromParams(path, func) && !qualifyFromBodyLocals(path, body)){
                    aliasType = typeAliasTarget(path);
                    if(!aliasType.empty()) aliasEnd = e;
                }
            }
            if(aliasEnd > 0){
                if(outer.empty()) outer = aliasType;
                inner = aliasType;
                casts++; k = aliasEnd + 1;
            } else if(file.peekToken(k).is(token::parenOpen) && file.peekToken(k + 1).is(eTokenType::dataType)
               && file.peekToken(k + 2).is(token::parenClose)){
                if(outer.empty()) outer = file.peekToken(k + 1).value;
                inner = file.peekToken(k + 1).value;
                casts++; k += 3;
            } else if(file.peekToken(k).is(token::parenOpen) && file.peekToken(k + 1).is(eTokenType::identifier)
                      && file.peekToken(k + 2).is(token::parenClose) && file.peekToken(k + 3).is(eTokenType::name)
                      && languageService.findObjectType(file.peekToken(k + 1).value) != nullptr
                      && !qualifyFromParams(file.peekToken(k + 1).value, func) && !qualifyFromBodyLocals(file.peekToken(k + 1).value, body)
                      && [&]{ int q = k + 3; while(file.peekToken(q + 1).is(token::period) && file.peekToken(q + 2).is(eTokenType::name)) q += 2;
                              return file.peekToken(q + 1).is(token::parenOpen) || file.peekToken(q + 1).is(token::bracketOpen); }()){
                // An object is its own type: a call or element receiver (`((trayD)pick().spare).extra`) casts as under a class name.
                if(outer.empty()) outer = file.peekToken(k + 1).value;
                inner = file.peekToken(k + 1).value;
                casts++; k += 3;
            } else if(casts > 0 && file.peekToken(k).is(token::parenOpen)){
                groups++; k++;
            } else break;
        }
        // `((Base)row[i]).m()`: an element receiver cast to an ancestor runs that ancestor's method.
        // The element may be reached by a path (`board.slots[i]`).
        // A call receiver (`((Tray)pick()).m()`, `((Sealed)pick().next()).seal`) and a path after either go the same way.
        int kp = k;
        while(file.peekToken(kp).is(eTokenType::name) && file.peekToken(kp + 1).is(token::period) && file.peekToken(kp + 2).is(eTokenType::name)) kp += 2;
        // `((Bin)((Tray)rack[0])).weigh()`: nested casts leave one more `)` per group before the `.`.
        if(casts >= 1 && file.peekToken(k).is(eTokenType::name)
           && (file.peekToken(kp + 1).is(token::bracketOpen) || file.peekToken(kp + 1).is(token::parenOpen))){
            int e = kp + 1, depth = 0;
            for(; e < k + 400; e++){
                token pk = file.peekToken(e);
                if(pk.is(token::bracketOpen) || pk.is(token::parenOpen)) depth++;
                else if((pk.is(token::bracketClose) || pk.is(token::parenClose)) && --depth == 0){
                    // A further `.name` or group belongs to the same operand.
                    token after = file.peekToken(e + 1);
                    if(after.is(token::bracketOpen) || after.is(token::parenOpen)) continue;
                    int f = e;
                    while(file.peekToken(f + 1).is(token::period) && file.peekToken(f + 2).is(eTokenType::name)) f += 2;
                    if(f != e && (file.peekToken(f + 1).is(token::bracketOpen) || file.peekToken(f + 1).is(token::parenOpen))){ e = f; continue; }
                    e = f;
                    break;
                }
                else if(pk.is(eTokenType::eof) || pk.is(token::endStatement)) break;
            }
            // `((Panel)board.slots[i]).code`: a member read off the cast element goes on as a typed value.
            bool closed = depth == 0;
            for(int c = 1; closed && c <= groups + 1; c++) closed = file.peekToken(e + c).is(token::parenClose);
            int g = groups;   // the closing `)`s past the first, which the receiver's parse leaves
            if(closed && file.peekToken(e + g + 2).is(token::period)
               && !file.peekToken(e + g + 4).is(token::parenOpen)){
                for(int i = 1; i < k; i++) file.getToken();                      // `(` `T` `)`, once per cast
                expression* recvExpr = parseExpression(file.getToken(), {token::parenClose}, func, body);
                for(int c = 0; c < g; c++) file.getToken(token::parenClose);
                convertReceiverForCast(recvExpr, outer);
                expr->tokens.push_back("(" + recvExpr->text() + ")");
                expr->resolvedType = outer;
                return ExprStep::Advance;
            }
            if(closed && file.peekToken(e + g + 2).is(token::period)
               && file.peekToken(e + g + 4).is(token::parenOpen)){
                for(int i = 1; i < k; i++) file.getToken();                      // `(` `T` `)`, once per cast
                expression* recvExpr = parseExpression(file.getToken(), {token::parenClose}, func, body);
                for(int c = 0; c < g; c++) file.getToken(token::parenClose);
                if(casts > 1) recvExpr->resolvedType = inner;                   // `((A)(B)row[i])`: the element is a B
                convertReceiverForCast(recvExpr, outer);
                file.getToken(token::period);
                token member = file.getToken();
                file.getToken(token::parenOpen);
                ParsedArgList pal = parseCallArgList(func, body);
                string recvText = recvExpr->text(), recvType = recvExpr->resolvedType;
                string castT = outer;
                functionDef* m = bindMethodCall(castT, recvText, member.value, pal.args, pal.namedArgNames, pal.interpSegmentsPerArg);
                classDef* anc = isUpcast(outer, recvType) ? getDispatchClass(outer) : nullptr;
                if(m->isEmitter && anc != nullptr)
                    parsingError(format("Ancestor-qualified dispatch '({0}){1}.{2}(...)' is not supported: '{2}' is an emitter method.",
                                        outer, recvText, member.value));
                string callName = methodCallName(m, member.value);
                string call = parenthesizeReceiver(recvText) + "." + (anc != nullptr ? anc->i6Name() + "::" + callName : callName) + "(";
                for(size_t i = 0; i < pal.args.size(); i++) call += (i ? ", " : "") + pal.args[i]->text();
                expr->tokens.push_back(call + ")");
                expr->resolvedType = m->returnType.name;
                return ExprStep::Advance;
            }
        }
        bool matches = casts > 0 && file.peekToken(k).is(eTokenType::name);
        for(int g = 0; matches && g <= groups; g++) matches = file.peekToken(k + 1 + g).is(token::parenClose);
        if(matches && file.peekToken(k + 2 + groups).is(token::period)){
            // A path that goes on past the member (`((T)obj).items.length`, `….items[0]`) chains off the
            // retyped receiver, as off a call's result.
            token after = file.peekToken(k + 4 + groups);
            bool chainGoesOn = func != nullptr && (after.is(token::period) || after.is(token::bracketOpen));
            // `((Node)h).label` where h's class converts to Node: the member is read off the converted value.
            string nameType = func != nullptr ? resolveIdentifierType(file.peekToken(k).value, func, body) : string();
            classDef* nameCls = getDispatchClass(nameType);
            classDef* outerCls = getDispatchClass(outer);
            bool converts = func != nullptr && nameCls != nullptr && outerCls != nullptr && nameCls != outerCls
                && !nameCls->hasAncestor(outerCls) && !outerCls->hasAncestor(nameCls)
                && findMemberInHierarchy(nameCls, [&](typeMember* m){ auto* f = dynamic_cast<functionDef*>(m);
                       return f && f->name == "operator()" && f->params.empty() && getDispatchClass(f->returnType.name) == outerCls; }) != nullptr;
            for(int i = 1; i < k; i++) file.getToken();
            st.chainCast = castType;   // a cast written before the group applies to what the member reads
            castType = outer;
            cur = file.getToken();
            for(int g = 0; g <= groups; g++) file.getToken(token::parenClose);
            if(converts){
                expr->tokens.push_back("(" + applyCastConversion(qualifyIdentifier(cur.value, func, body), nameType, outer) + ")");
                expr->resolvedType = outer;
                castType = st.chainCast;
                st.chainCast.clear();
                return ExprStep::Advance;
            }
            if(chainGoesOn){
                // Parenthesized, it is a value: nothing re-resolves it by its name's declared type.
                expr->tokens.push_back("(" + qualifyIdentifier(cur.value, func, body) + ")");
                expr->resolvedType = outer;
                castType = st.chainCast;
                st.chainCast.clear();
                return ExprStep::Advance;
            }
            st.receiverCast = true;
            st.innerCast = casts > 1 ? inner : string();   // `((A)(B)obj)`: the receiver is a B
            return ExprStep::Continue;
        }
    }
    // Check for cast expression: (TypeName)expr
    // A type alias named bare (an imported one, or the object's own inside its body) casts as its type does.
    string aliasCast;
    if(file.peekToken(1).is(eTokenType::identifier) && file.peekToken(2).is(token::parenClose)
       && !qualifyFromParams(file.peekToken(1).value, func) && !qualifyFromBodyLocals(file.peekToken(1).value, body)){
        token aliasTok = file.peekToken(1);
        for(objectDef* imp : usingObjectImports)
            for(typeMember* m : imp->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd && vd->isAlias && vd->name == aliasTok.value
                   && (languageService.isClassType(vd->type.name) || languageService.findEnum(vd->type.name) != nullptr))
                    aliasCast = vd->type.name;
    }
    if(!aliasCast.empty() || (file.peekToken(1).is(eTokenType::dataType) && file.peekToken(2).is(token::parenClose))){
        castType = aliasCast.empty() ? file.getToken(eTokenType::dataType).value : (file.getToken(), aliasCast);
        file.getToken(token::parenClose);
        // A `!` or another cast after the cast is its operand (`(int)!t`, `(int)(char)c`):
        // parse that operand on its own and convert its result, so neither the operator nor the
        // inner cast takes the outer cast's place.
        token nx = file.peekToken();
        bool innerCast = nx.is(token::parenOpen) && file.peekToken(2).is(eTokenType::dataType) && file.peekToken(3).is(token::parenClose);
        if(nx.is("!") || innerCast){
            string outer = castType;
            castType = "";
            vector<string> terms = *st.terminators;
            for(const string& o : kPrecedenceOps) terms.push_back(o);
            for(const char* t : {"?", ":", ",", "]"}) terms.push_back(t);
            if(parenDepth > 0) terms.push_back(token::parenClose);
            expression* sub = parseExpression(file.getToken(), terms, func, body);
            expr->tokens.push_back("(" + applyCastConversion(sub->text(), sub->resolvedType, outer) + ")");
            if(expr->resolvedType.empty()) expr->resolvedType = outer;
            token t;
            t.value = sub->terminator;
            bool isOp = find(kPrecedenceOps.begin(), kPrecedenceOps.end(), t.value) != kPrecedenceOps.end();
            t.tokenType = isOp ? eTokenType::oper : eTokenType::symbol;
            st.prefetched = t;
            return ExprStep::Advance;
        }
        cur = exprNext(st);
        return ExprStep::Continue;  // re-process the token after the cast with castType set
    }
    // Templated cast target: (func<...>)x / (array<T>)x / (rawarray<T>)x. These type
    // names only ever appear in type position, so `(` <one of them> `<` is unambiguously a
    // cast — parse the full templated name and expect the closing ')'. Enables narrowing a
    // union value to a func<...> member: `(func<void>)x`.
    if((file.peekToken(1).value == "func" || file.peekToken(1).value == "array"
            || file.peekToken(1).value == "rawarray")
       && file.peekToken(2).value == "<"){
        string base = file.getToken(eTokenType::dataType).value;
        castType = (base == "func") ? parseFuncType() : parseArrayTypeTail(base);
        file.getToken(token::parenClose);
        cur = exprNext(st);
        return ExprStep::Continue;
    }
    // Instance cast: `(instanceName)expr`. An objectDef instance is its own type but is
    // lexed as an *identifier* (not a dataType), so it doesn't match the class-cast branch
    // above. Detect `(` ident `)` where the identifier names an objectDef, and an OPERAND
    // follows — distinguishing a real cast from a grouped value `(obj)` followed by an
    // operator/terminator. This lets `((library)x).instanceMember` resolve the instance's
    // own members (unchecked downcast — the author asserts the runtime type; same as `(var)`
    // but type-checked against the named instance).
    {
        token p1c = file.peekToken(1);
        if(p1c.is(eTokenType::identifier) && file.peekToken(2).is(token::parenClose)
           && languageService.findObjectType(p1c.value) != nullptr){
            token p3c = file.peekToken(3);
            // An object can't be called, so `(board)(…)` is a cast of the group too.
            bool operandFollows = p3c.is(eTokenType::identifier) || p3c.is(eTokenType::dataType)
                || p3c.is(eTokenType::integer) || p3c.is(eTokenType::quote) || p3c.is(eTokenType::rawQuote)
                || p3c.is(eTokenType::charLiteral) || p3c.is(eTokenType::dictionaryWord) || p3c.is(token::parenOpen);
            if(operandFollows){
                castType = file.getToken(eTokenType::identifier).value;
                file.getToken(token::parenClose);
                cur = exprNext(st);
                return ExprStep::Continue;
            }
        }
    }
    // Cast to a dotted type path (`(kit.box)v`, §10.1): the path names a type through namespace aliases.
    {
        string path; int i = 1;
        while((file.peekToken(i).is(eTokenType::identifier) || file.peekToken(i).is(eTokenType::dataType))
              && file.peekToken(i + 1).is(token::period)){
            path += file.peekToken(i).value + ".";
            i += 2;
        }
        token last = file.peekToken(i);
        if(!path.empty() && (last.is(eTokenType::identifier) || last.is(eTokenType::dataType))
           && file.peekToken(i + 1).is(token::parenClose)){
            token after = file.peekToken(i + 2);
            bool operandFollows = after.is(eTokenType::identifier) || after.is(eTokenType::dataType)
                || after.is(eTokenType::integer) || after.is(token::parenOpen);
            string typeName = operandFollows ? resolveNamespacedType(path + last.value) : "";
            if(!typeName.empty()){
                for(int k = 0; k < i + 1; k++) file.getToken();   // the path and ')'
                castType = typeName;
                cur = exprNext(st);
                return ExprStep::Continue;
            }
        }
    }
    // Cast followed by parenthesized expression: `(T)(...)`. The cast applies to
    // the result of the inner expression, not to the first identifier inside, so
    // park castType on the stack and clear it before descending. The matching
    // close-paren below pops and applies via applyCastConversion.
    if(!castType.empty()){
        parenCastStack.push_back({castType, parenDepth});
        castType = "";
    }
    parenDepth++;
    expr->tokens.push_back(cur.value);
    return ExprStep::Advance;
}

// ')' — closes a paren group, assembles a ternary opened at this depth, and applies
// a cast that was parked for it.
bglParser::ExprStep bglParser::parseExprParenClose(ExprParseState& st){
    expression* expr = st.expr;
    int& parenDepth = st.parenDepth;
    int& startParenDepth = st.startParenDepth;
    token& cur = st.cur;
    using PendingParenCast = ExprParseState::PendingParenCast;
    vector<ExprParseState::PendingParenCast>& parenCastStack = st.parenCastStack;
    vector<ExprParseState::PendingTernary>& pendingTernaries = st.pendingTernaries;

    bool closesGroup = parenDepth > startParenDepth;
    if(closesGroup) parenDepth--;
    // A ')' that closes the group the ternary sits in ends its false branch: `(cond ? a : b)`. One
    // that closes a group inside the false branch, `cond ? a : (b)`, does not.
    if(!pendingTernaries.empty() && closesGroup && parenDepth < pendingTernaries.back().parenDepthAtQuestion){
        exprAssembleTernary(st);
    }
    expr->tokens.push_back(cur.value);
    // Apply any cast that was queued for this paren depth. The inner expression
    // resolved against its natural type; we now convert it to the cast's target.
    string castFrom;   // the type a cast closed here converted from (an upcast dispatches statically)
    if(!parenCastStack.empty() && parenCastStack.back().parenDepthAtPush == parenDepth
       && file.peekToken().is(token::period) && st.castType.empty() && st.chainResultCast.empty()){
        // `(int)(group).member`, `(board)((Panel)o).home()`: the chain is read first; the cast is its result's.
        st.chainResultCast = parenCastStack.back().castType;
        parenCastStack.pop_back();
    }
    if(!parenCastStack.empty() && parenCastStack.back().parenDepthAtPush == parenDepth){
        PendingParenCast pc = parenCastStack.back();
        parenCastStack.pop_back();
        castFrom = expr->resolvedType;
        string srcText = expr->text();
        string newText = applyCastConversion(srcText, expr->resolvedType, pc.castType);
        if(newText != srcText){
            expr->tokens.clear();
            expr->tokens.push_back(newText);
        }
        expr->resolvedType = pc.castType;
    }
    // `(expr)(args)`: a call through the group's routine value; a func<…> result type
    // becomes the callee's return type.
    if(closesGroup && file.peekToken().is(token::parenOpen)){
        const string t = expr->resolvedType;
        file.getToken(token::parenOpen);
        ParsedArgList pal = parseCallArgList(st.func, st.body);
        rejectInterpolatedArgsInExpression(pal, "call");
        string call = "(";
        for(size_t i = 0; i < pal.args.size(); i++){
            if(i > 0) call += ", ";
            call += pal.args[i]->text();
        }
        // Bracket the group and its call together: a bare `(a)(4)` after I6's `print` reads as the
        // print rule `(a)`. The group is the trailing tokens back to its opening '('.
        string group;
        int depth = 0;
        while(!expr->tokens.empty()){
            string tk = expr->tokens.back();
            expr->tokens.pop_back();
            group = tk + group;
            for(char ch : tk){ if(ch == ')') depth++; else if(ch == '(') depth--; }
            if(depth <= 0) break;
        }
        expr->tokens.push_back("(" + group + call + "))");
        if(t.rfind("func<", 0) == 0){
            size_t lt = t.find('<'), comma = t.find(',', lt), gt = t.rfind('>');
            expr->resolvedType = (comma != string::npos && comma < gt) ? t.substr(lt+1, comma-lt-1) : t.substr(lt+1, gt-lt-1);
        }
        else if(t == "func") expr->resolvedType = "var";
    }
    // `(expr)[i]` / `((Shelf)self)[i]`: the group's operator [] (when the group is the operand so far).
    while(closesGroup && file.peekToken().is(token::bracketOpen) && !expr->resolvedType.empty()){
        const string t = expr->resolvedType;
        vector<string> saved = expr->tokens;
        string group;
        int depth = 0;
        while(!expr->tokens.empty()){
            string tk = expr->tokens.back();
            expr->tokens.pop_back();
            group = tk + group;
            for(char ch : tk){ if(ch == ')') depth++; else if(ch == '(') depth--; }
            if(depth <= 0) break;
        }
        bool wholeOperand = expr->tokens.empty() || (expr->tokens.size() == 1 && expr->tokens[0] == "(");
        classDef* gc = getDispatchClass(t == "array<char>" ? "bytearray" : t);
        string elem = t == "bytearray" || t == "array<char>" ? "char" : arrayInnerType(t);
        functionDef* getM = nullptr;
        if(gc != nullptr && !elem.empty()) getM = findArraySubscriptOp(gc, elem, /*isWrite=*/false);
        if(gc != nullptr && elem.empty())
            getM = dynamic_cast<functionDef*>(findMemberInHierarchy(gc, [](typeMember* m){
                auto* f = dynamic_cast<functionDef*>(m); return f && f->name == "[]" && f->params.size() == 1; }));
        if(!wholeOperand || getM == nullptr){ expr->tokens = saved; break; }
        file.getToken(token::bracketOpen);
        expression* idx = parseExpression(file.getToken(), {token::bracketClose}, st.func, st.body);
        string recv = "(" + group + ")";
        string text;
        if(auto* blk = getM->isEmitter ? dynamic_cast<i6Block*>(getM->body) : nullptr){
            emitterBindings gb; gb.self = recv; gb.val = recv; gb.prop = "0"; gb.fn = getM; gb.args.push_back(idx->text());
            text = expandEmitterBody(blk, gb);
        } else {
            if(getM->i6name.empty()) getM->i6name = mangleOperatorName(getM->name);
            // `((Base)x)[i]` calls Base's operator, as `((Base)x).m()` calls Base's method.
            classDef* owner = gc->declaringClass(getM);
            if(castFrom.empty()){   // `((Base)name)`: the name's own type
                string inner = group;
                while(inner.size() >= 2 && inner.front() == '(' && inner.back() == ')') inner = inner.substr(1, inner.size() - 2);
                if(!inner.empty() && inner.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == string::npos)
                    castFrom = inner == "self" && currentClass != nullptr ? currentClass->name
                             : resolveIdentifierType(inner, st.func, st.body);
            }
            string routine = !castFrom.empty() && castFrom != t && isUpcast(t, castFrom) && owner != nullptr
                           ? owner->i6Name() + "::" + getM->i6name : getM->i6name;
            text = recv + "." + routine + "(" + idx->text() + ")";
        }
        expr->tokens.push_back(text);
        expr->resolvedType = elem.empty() || getM->returnType.name != "t" ? getM->returnType.name : elem;
        if(expr->resolvedType == "T" || expr->resolvedType == "t") expr->resolvedType = elem;
    }
    return ExprStep::Advance;
}

// Integer literal, with a pending cast and a leading unary '-' folded in.
bglParser::ExprStep bglParser::parseExprIntLiteral(ExprParseState& st){
    expression* expr = st.expr;
    token& cur = st.cur;
    string& castType = st.castType;

    if(!castType.empty()){
        bool isNegated = (expr->tokens.size() == 1 && expr->tokens[0] == "-");
        string srcType = isNegated ? "negativeintliteral" : "intliteral";
        // Build the source text including the leading '-' if present, then pop it
        // from tokens so the post-cast emission doesn't repeat the minus sign.
        string lit = cur.value;
        if(isNegated){
            lit = "-" + lit;
            expr->tokens.pop_back();
        }
        string newText = applyCastConversion(lit, srcType, castType);
        expr->tokens.push_back(newText);
        expr->resolvedType = castType;
        castType = "";
    } else if(expr->resolvedType.empty()){
        // `-7`, or `(-7)`: a minus with nothing but opening parentheses before it.
        bool isNegated = !expr->tokens.empty() && expr->tokens.back() == "-"
            && all_of(expr->tokens.begin(), expr->tokens.end() - 1, [](const string& t){ return t == "("; });
        expr->resolvedType = isNegated ? "negativeintliteral" : "intliteral";
        expr->tokens.push_back(cur.value);
    } else {
        expr->tokens.push_back(cur.value);
    }
    return ExprStep::Advance;
}

// ─── FLOAT LITERAL: 1.0, .3, (with a leading '-' → negative) ───────
bglParser::ExprStep bglParser::parseExprFloatLiteral(ExprParseState& st){
    expression* expr = st.expr;
    token& cur = st.cur;
    string& castType = st.castType;

    // Emit as I6's Glulx float constant `$+<decimal>` / `$-<decimal>` — the I6 compiler
    // computes the IEEE-754 single-precision bits. A leading unary '-' (sitting as the sole
    // token so far) selects `$-` and is consumed so it isn't repeated. Glulx-only: the
    // Z-machine has no float opcodes, so `float` isn't even a type there.
    if(definedSymbols.find("target_glulx") == definedSymbols.end())
        parsingError(format("float literal '{0}' requires a Glulx target — the Z-machine has no floating point.", cur.value));
    bool isNegated = (expr->tokens.size() == 1 && expr->tokens[0] == "-");
    if(isNegated) expr->tokens.pop_back();
    string i6lit = (isNegated ? "$-" : "$+") + cur.value;
    if(!castType.empty()){
        // Explicit cast on a float literal, e.g. `(int)1.5`.
        string newText = applyCastConversion(i6lit, "float", castType);
        expr->tokens.push_back(newText);
        expr->resolvedType = castType;
        castType = "";
    } else {
        expr->tokens.push_back(i6lit);
        if(expr->resolvedType.empty()) expr->resolvedType = "float";
    }
    return ExprStep::Advance;
}

// `new TypeName(args)` — pool-class allocation. Emits as `TypeName.create(args)`.
// The type must be a pooled class: declared with `[N]` or marker `extern class Foo[]`.
// Returns `nothing` at runtime if the pool is exhausted.
bglParser::ExprStep bglParser::parseExprNew(ExprParseState& st){
    expression* expr = st.expr;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    token typeTok = file.getToken({eTokenType::dataType, eTokenType::identifier});
    // A bare alias, imported or the object's own.
    if(!file.peekToken().is(token::period)) typeTok = consumeTypeToken(typeTok);
    // A namespace path to the class (`new kit.parts.token(9)`).
    if(file.peekToken().is(token::period)){
        string path = typeTok.value;
        while(file.peekToken().is(token::period) && !file.peekToken(2).is(token::parenOpen)
              && (file.peekToken(2).is(eTokenType::identifier) || file.peekToken(2).is(eTokenType::dataType))){
            file.getToken(token::period);
            path += "." + file.getToken().value;
        }
        if(string t = resolveNamespacedType(path); !t.empty()) typeTok.value = t;
        else parsingError(format("'new {0}': '{0}' is not a class", path));
    }
    classDef* cls = getDispatchClass(typeTok.value);
    if(cls == nullptr)
        parsingError(format("'new {0}': '{0}' is not a class", typeTok.originalValue.empty() ? typeTok.value : typeTok.originalValue));
    if(cls->poolSize == 0)
        parsingError(format("'new {0}': class is not pooled. Declare with `class {0}[N]` (sized pool) or `extern class {0}[]` (extern marker) to enable allocation.", cls->dName()));
    file.getToken(token::parenOpen);
    ParsedArgList pal = parseCallArgList(func, body);
    rejectInterpolatedArgsInExpression(pal, "new");
    // The arguments are create()'s: named ones placed, defaults filled and types checked like a call's.
    if(findMemberInHierarchy(cls, [](typeMember* m){
           auto* fd = dynamic_cast<functionDef*>(m); return fd != nullptr && fd->name == "create" && !fd->isEmitter; }))
    {   string createOn = cls->name;
        bindMethodCall(createOn, cls->i6Name(), "create", pal.args, pal.namedArgNames, pal.interpSegmentsPerArg, "");
    }
    string call = cls->i6Name() + ".create(";
    for(size_t i = 0; i < pal.args.size(); i++){
        if(i > 0) call += ", ";
        call += pal.args[i]->text();
    }
    call += ")";
    expr->tokens.push_back(call);
    if(expr->resolvedType.empty()) expr->resolvedType = cls->name;
    return ExprStep::Advance;
}

// `Type{ fields }` — inline anonymous object declaration: the named object declaration
// (`Type name { ... }`) minus the name, in an expression position. This BRIDGES to the
// existing declaration machinery: synthesize an anonymous global object, route the body
// through processObjectDeclaration (so field parsing + baking are identical to a named
// object), and evaluate the expression to a REFERENCE to that baked object. Compile-time
// only — nothing runs; it's one more baked static object plus a pointer to it. Generic:
// works for any class that can be declared as a named object.
// True when `first` begins a type alias path (`ns.pair`, or a name imported by #using) naming a class
// and a `{` follows it: an inline object. Consumes nothing.
bool bglParser::aliasedInlineObjectAhead(const token& first){
    string path = first.value;
    int k = 1;
    while(file.peekToken(k).is(token::period)
          && (file.peekToken(k + 1).is(eTokenType::identifier) || file.peekToken(k + 1).is(eTokenType::dataType))){
        path += "." + file.peekToken(k + 1).value;
        k += 2;
    }
    if(!file.peekToken(k).is(token::braceOpen)) return false;
    string resolved;
    if(k == 1){
        for(objectDef* imp : usingObjectImports)
            for(typeMember* m : imp->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->isAlias && vd->name == first.value) resolved = vd->type.name;
    } else resolved = resolveNamespacedType(path);
    return !resolved.empty() && languageService.isClassType(resolved) && getDispatchClass(resolved) != nullptr;
}

bglParser::ExprStep bglParser::parseExprInlineObject(ExprParseState& st){
    expression* expr = st.expr;
    token& cur = st.cur;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    classDef* cls = getDispatchClass(cur.value);
    token nameTok = cur;                          // inherit src/line; override identity
    nameTok.value = format("_bglanon{0}", anonObjectCounter++);
    nameTok.originalValue = "";
    nameTok.tokenType = eTokenType::identifier;
    file.getToken(token::braceOpen);              // consume '{'
    bakeInlineObjectAggregate(cls, cur.value, nameTok.value, func, body);
    expr->tokens.push_back(nameTok.value);        // the expression is a reference to the baked object
    if(expr->resolvedType.empty()) expr->resolvedType = cur.value;
    return ExprStep::Advance;
}

// ── name[i]: subscript access ──
// Continuation: chained subscripts on an element — `grid[i][j]`. Each step applies the element's
// operator[] to the PRIOR subscript VALUE (a pointer to an inner array, or a class instance).
void bglParser::chainSubscripts(expression* expr, string& subscriptText, functionDef* func, statementBlock* body){
    auto classSubscriptElem = [&](const string& t) -> string {
        classDef* c = isArrayOfArraysElement(t) || t == "bytearray" ? nullptr : getDispatchClass(t);
        auto* g = c == nullptr ? nullptr : dynamic_cast<functionDef*>(findMemberInHierarchy(c, [](typeMember* m){
            auto* fd = dynamic_cast<functionDef*>(m);
            return fd != nullptr && fd->name == "[]" && fd->params.size() == 1; }));
        return g != nullptr ? g->returnType.name : "";
    };
    while(!subscriptText.empty() && file.peekToken().is(token::bracketOpen)
          && (isArrayOfArraysElement(expr->resolvedType) || expr->resolvedType == "bytearray"
              || !classSubscriptElem(expr->resolvedType).empty())){
        file.getToken(); // consume '['
        string curType   = expr->resolvedType;
        string innerElem = curType == "bytearray" ? "char" : arrayInnerType(curType);
        if(innerElem.empty()) innerElem = classSubscriptElem(curType);
        else if(innerElem == "char") curType = "bytearray";   // an array<char> row is a byte array
        expression* idx  = parseExpression(file.getToken(), {token::bracketClose}, func, body);
        classDef* curCls = getDispatchClass(curType);
        functionDef* getM = (curCls != nullptr && !innerElem.empty())
                          ? findArraySubscriptOp(curCls, innerElem, /*isWrite=*/false) : nullptr;
        // Overloaded `operator []` (`shelves[0][zone.region]`): the index's type chooses.
        if(curCls != nullptr && !isArrayOfArraysElement(curType) && curType != "bytearray"){
            vector<functionDef*> gets;
            findMemberInHierarchy(curCls, [&](typeMember* m){
                if(auto* f = dynamic_cast<functionDef*>(m); f && f->name == "[]" && f->params.size() == 1) gets.push_back(f);
                return false; });
            if(gets.size() > 1){
                const string it = idx->resolvedType, base = literalBaseType(it);
                functionDef* pick = nullptr;
                for(int pass = 0; pass < 2 && pick == nullptr; pass++)
                    for(functionDef* f : gets){
                        const string& pt = f->params[0]->type.name;
                        if(pass == 0 ? (pt == it || pt == base) : isTypeCompatible(it, pt)){ pick = f; break; }
                    }
                if(pick != nullptr){ getM = pick; innerElem = pick->returnType.name; mangleOverloadSetForReceiver(curType, "[]"); }
            }
        }
        i6Block* blk = getM != nullptr ? dynamic_cast<i6Block*>(getM->body) : nullptr;
        if(getM == nullptr || (getM->isEmitter && blk == nullptr))
            parsingError(format("No operator[] returning '{0}' on type '{1}' for chained subscript.",
                                typeDisplayName(innerElem), typeDisplayName(curType)));
        string recv = "(" + subscriptText + ")";
        if(!getM->isEmitter){
            string routine = getM->i6name.empty() ? mangleOperatorName(getM->name) : getM->i6name;
            subscriptText = recv + "." + routine + "(" + idx->text() + ")";
        } else {
            emitterBindings ib; ib.self = recv; ib.val = recv;
            ib.prop = "0";   // recv is a pointer to a tracked/byte array
            ib.fn = getM; ib.args.push_back(idx->text());
            subscriptText = expandEmitterBody(blk, ib);
        }
        if(!expr->tokens.empty()) expr->tokens.pop_back();
        expr->tokens.push_back(subscriptText);
        expr->resolvedType = innerElem;
    }
}

// True when `name` is a member of `type`'s class (an accessor's own field or method).
bool bglParser::isOwnMember(const string& type, const string& name){
    classDef* c = getDispatchClass(type);
    return c != nullptr && findMemberInHierarchy(c, [&](typeMember* m){ return m->name == name; }) != nullptr;
}

// True when the path goes on (`.name`) to a member of `type` itself, rather than of what its getter reads.
bool bglParser::pathGoesOnToOwnMember(ExprParseState& st, const string& type){
    if(st.prefetched ? !st.prefetched->is(token::period) : !file.peekToken().is(token::period)) return false;
    token next = st.prefetched ? file.peekToken() : file.peekToken(2);
    return isOwnMember(type, next.value);
}

// A bare member whose type is an accessor with a getter (`Target` for `self.target`): its read through
// the getter, and the type read. False for any other name.
bool bglParser::bareAccessorRead(const string& name, functionDef* func, statementBlock* body, string& text, string& type,
                                 const string& memberHint){
    if(func == nullptr) return false;
    string qualified = qualifyIdentifier(name, func, body, memberHint);
    if(qualified.find('.') == string::npos || qualified.find('(') != string::npos) return false;
    string idType = resolveIdentifierType(name, func, body, memberHint);
    if(isPropertyClassType(idType)) return false;
    functionDef* g = nativeGetterOf(idType);
    if(g == nullptr) return false;
    text = qualified + "." + g->i6name + "()";
    type = g->returnType.name;
    return true;
}

bglParser::ExprStep bglParser::parseExprSubscript(ExprParseState& st, token& next){
    // `::name[i]`: the global array, written as the escape the emitter resolves past a same-named local.
    string escaped = st.cur.value.rfind("::", 0) == 0 && st.cur.value.find('.') == string::npos ? st.cur.value : string();
    size_t from = st.expr->tokens.size();
    ExprStep r = parseExprSubscriptInner(st, next);
    if(!escaped.empty())
        for(size_t i = from; i < st.expr->tokens.size(); i++)
            st.expr->tokens[i] = replaceWord(st.expr->tokens[i], escaped, kGlobalEscapeMarker + escaped.substr(2));
    return r;
}

bglParser::ExprStep bglParser::parseExprSubscriptInner(ExprParseState& st, token& next){
    expression* expr = st.expr;
    token& cur = st.cur;
    string& castType = st.castType;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    string arrName = cur.value;
    // A static member (an array, a stringObj) named bare in its own class's code is its global.
    if(currentClass != nullptr && arrName.find('.') == string::npos && func != nullptr
       && !qualifyFromParams(arrName, func) && !qualifyFromBodyLocals(arrName, body) && !qualifyFromAncestorBlocks(arrName, body))
        if(auto* sm = dynamic_cast<variableDeclaration*>(findMemberInHierarchy(currentClass, [&](typeMember* m){
                auto* vd = dynamic_cast<variableDeclaration*>(m); return vd && vd->isStatic && vd->name == arrName; })))
            arrName = cur.value = "_bgl_" + currentClass->declaringClass(sm)->name + "_" + sm->name;
    // `Box.last[1]`: a static through its class is its global.
    if(arrName.find('.') != string::npos)
        if(string g = staticMemberGlobal(arrName); !g.empty()) arrName = cur.value = g;
    // A dotted receiver names a member; resolveIdentifierType does not resolve one,
    // so `shelf.items[0]` found no array type, missed the member-array branch below,
    // and emitted the global form `shelf-->(i+1)` — indexing off the object rather
    // than its property. Writes already used resolvePathType and were correct, so
    // only reads were wrong.
    string arrType = arrName.find('.') == string::npos
                   ? resolveIdentifierType(arrName, func, body)
                   : resolvePathType(arrName, func, body);
    classDef* arrCls = getDispatchClass(arrType);
    expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, body);
    rejectConstantIndexOutside(arrName, indexExpr, func, body);
    // Element-type-aware lookup: find operator[] whose return type matches the
    // array's declared element type. byteArray (array<char>) keeps its char operator[];
    // other arrays have int/object/bool/string overloads plus implicit synthesis for
    // user classes.
    string elemType = resolveArrayElementType(arrName, func, body);
    if(elemType.empty() && arrType == "bytearray") elemType = "char";
    // Non-array classes (e.g. string) carry no declared element type; derive it
    // from a concrete operator[] return so subscript resolves through the facade.
    if(elemType.empty() && arrCls != nullptr) elemType = inferSubscriptElementType(arrCls);
    functionDef* getMethod = nullptr;
    if(arrCls != nullptr && !elemType.empty())
        getMethod = findArraySubscriptOp(arrCls, elemType, /*isWrite=*/false);
    // An object declared with its own `operator []`.
    if(getMethod == nullptr)
        if(objectDef* od = languageService.findObjectType(arrType))
            for(typeMember* m : od->members)
                if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->name == "[]" && fd->params.size() == 1){
                    getMethod = fd; elemType = fd->returnType.name; break;
                }
    if(getMethod == nullptr) {
        if(elemType.empty())
            parsingError(format("Subscript on '{0}': no declared element type. Declare as array<T>.", arrName));
        parsingError(format("No operator[] returning '{0}' on type '{1}'. Add an overload or use a supported element type.",
            typeDisplayName(elemType), typeDisplayName(arrType)));
    }
    // Overloads by index type (`[] (int)`, `[] (string)`): the one the index matches, exactly first.
    objectDef* ownerObj = languageService.findObjectType(arrType);
    if((arrCls != nullptr || ownerObj != nullptr) && !getMethod->isEmitter && !isWordArrayType(arrType)){
        vector<functionDef*> subs;
        auto take = [&](const vector<typeMember*>& members){
            for(typeMember* m : members)
                if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->name == "[]" && fd->params.size() == 1 && !fd->isEmitter)
                    subs.push_back(fd);
        };
        std::function<void(classDef*)> collect = [&](classDef* c){
            if(c == nullptr) return;
            take(c->members);
            for(classDef* b : c->baseClasses) collect(b);
        };
        if(ownerObj != nullptr) take(ownerObj->members);   // an object's own overloads come first
        collect(arrCls);
        if(subs.size() > 1){
            const string& it = indexExpr->resolvedType;
            functionDef* pick = nullptr;
            for(functionDef* fd : subs)
                if(fd->params[0]->type.name == it || literalBaseType(it) == fd->params[0]->type.name){ pick = fd; break; }
            if(pick == nullptr)
                for(functionDef* fd : subs)
                    if(isTypeCompatible(it, fd->params[0]->type.name)){ pick = fd; break; }
            if(pick == nullptr)
                parsingError(format("No operator [] on '{0}' takes an index of type '{1}'.", typeDisplayName(arrType), typeDisplayName(it)));
            getMethod = pick;
            mangleOverloadSetForReceiver(arrType, "[]");
        }
    }
    if(expr->resolvedType.empty() || expr->tokens.empty()) expr->resolvedType = getMethod->returnType.name;
    string subscriptText;
    // Member (property) WORD arrays use the orLibrary property-array convention:
    // a plain `with prop a b c` property accessed via `obj.&prop-->n` (0-indexed,
    // no count slot), NOT the count-prefixed global/table form. Qualifying the bare
    // name yields "owner.prop" for a member (the global/table emitter body's
    // `$val-->($i+1)` would otherwise index off the bare property id → garbage).
    string memberOwner, memberProp;
    bool isMemberWordArray = isWordArrayType(arrType) &&
        splitQualifiedMember(arrName, func, body, memberOwner, memberProp);
    if(isMemberWordArray && memberArrayIsRef(memberOwner, memberProp, func, body)){
        arrName = objectI6Name(memberOwner) + "." + propertyI6Name(memberProp);   // read the pointer, not the slot
        isMemberWordArray = false;
    }
    // A bare member that holds a pointer (array<char>, stringObj, a class with operator []) is self's.
    if(!isWordArrayType(arrType) && arrName.find('.') == string::npos
       && splitQualifiedMember(arrName, func, body, memberOwner, memberProp))
        arrName = memberOwner + "." + propertyI6Name(memberProp);
    // Inside a lambda, an outer variable (or `self`) as the receiver is read through its capture.
    // A block inside the lambda (an `if` body) parses with its own context; the lambda is still currentFunc.
    if(functionDef* lam = func != nullptr && func->isLambda ? func : currentFunc != nullptr && currentFunc->isLambda ? currentFunc : nullptr;
       lam != nullptr && arrName.find('.') == string::npos){
        string q = qualifyIdentifier(arrName, func, body);
        for(auto& cap : lam->captures) if(cap.bodyName() == q){ arrName = q; break; }
    }
    if(i6ExprMemberArrays.count(arrName)){
        isMemberWordArray = true;
        subscriptText = arrName + ".&$prop-->(" + indexExpr->text() + ")";
        expr->tokens.push_back(subscriptText);
    }
    else if(isMemberWordArray){
        subscriptText = checkedMemberSlot(memberOwner, propertyI6Name(memberProp), indexExpr->text(), false,
                                          memberArrayIsTracked(memberOwner, memberProp, func, body));
        subscriptText = valueSlotText(subscriptText, elemType);   // a value class's element: its slot's own instance
        expr->tokens.push_back(subscriptText);
        if(isArrayOfArraysElement(elemType) && expr->tokens.size() == 1) expr->resolvedType = elemType;   // a row, subscripted on
    }
    else if(getMethod->isEmitter)
        if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)) {
            emitterBindings sb; sb.self = objectI6Name(arrName); sb.val = sb.self; sb.selfType = arrType;
            sb.prop = (isWordArrayType(arrType) || arrType == "bytearray") ? "0" : "<$prop undefined>";
            sb.fn = getMethod; sb.args.push_back(indexExpr->text());
            subscriptText = expandEmitterBody(blk, sb);
            // An element of an array of a value class is its slot's instance, drawn on first use.
            if(isWordArrayType(arrType)) subscriptText = valueSlotText(subscriptText, elemType);
            expr->tokens.push_back(subscriptText);
        }
    if(subscriptText.empty() && !isMemberWordArray && !getMethod->isEmitter){
        // A routine-bodied operator [] is a method: send it.
        string routine = getMethod->i6name.empty() ? mangleOperatorName(getMethod->name) : getMethod->i6name;
        subscriptText = objectI6Name(arrName) + "." + routine + "(" + indexExpr->text() + ")";
        expr->tokens.push_back(subscriptText);
    }
    chainSubscripts(expr, subscriptText, func, body);
    // `steps[i](args)`: the element is a function value, called. Parenthesized, because a bare
    // `a-->i (args)` reads in I6 as a call of i.
    if(!subscriptText.empty() && file.peekToken().is(token::parenOpen) && expr->resolvedType.rfind("func", 0) == 0){
        const string t = expr->resolvedType;
        file.getToken(token::parenOpen);
        ParsedArgList pal = parseCallArgList(func, body);
        rejectInterpolatedArgsInExpression(pal, "call");
        string call = "(" + subscriptText + ")(";
        for(size_t i = 0; i < pal.args.size(); i++){
            if(i > 0) call += ", ";
            call += pal.args[i]->text();
        }
        if(!expr->tokens.empty()) expr->tokens.pop_back();
        expr->tokens.push_back(call + ")");
        size_t lt = t.find('<'), comma = t.find(',', lt), gt = t.rfind('>');
        expr->resolvedType = lt == string::npos ? "var"
            : (comma != string::npos && comma < gt) ? t.substr(lt+1, comma-lt-1) : t.substr(lt+1, gt-lt-1);
        return ExprStep::Advance;
    }
    // Continuation: if next token is '.', handle dot-access on the subscript result
    // (e.g. arr[0].field or arr[0].method()). Resolve against the element type's class.
    if(!subscriptText.empty() && file.peekToken().is(token::period)){
        file.getToken(); // consume '.'
        // I6's property operator `.` binds TIGHTER than the array operator `-->`, so a bare
        // `arr-->(i).member` parses as `arr --> ((i).member)`. Parenthesize the subscript so
        // member/method access lands on the element: `(arr-->(i)).member`.
        string elemType = expr->resolvedType;
        subscriptText = valueSlotText(subscriptText, elemType);
        string recv = "(" + subscriptText + ")";
        token member = file.getToken();
        if(file.peekToken().is(token::parenOpen)){
            // Method call: arr[0].method(args) — resolve via bindMethodCall
            file.getToken(); // consume '('
            ParsedArgList pal = parseCallArgList(func, body, braceArgHints(collectMethodCandidates(elemType, member.value)));
            rejectInterpolatedArgsInExpression(pal, member.value);
            vector<string> namedArgNames = pal.namedArgNames;
            vector<vector<interpolatedSegment>> interpSegs = pal.interpSegmentsPerArg;
            string innerElemType = (elemType.rfind("array<", 0) == 0 || elemType.rfind("rawarray<", 0) == 0)
                                 ? listElementType(elemType) : string();
            functionDef* method = bindMethodCall(elemType, subscriptText, member.value,
                pal.args, namedArgNames, interpSegs, innerElemType);
            if(!expr->tokens.empty()) expr->tokens.pop_back();
            if(method->isEmitter && !method->isPrePassStub){
                if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                    emitterBindings em; em.self = recv; em.val = recv;
                    // When the element is itself an array (array-of-arrays), a method like
                    // `grid[i].length()` dispatches through `_bglArray.X($self, $prop, …)`.
                    // `recv` is a pointer to a tracked inner array, i.e. the global-array
                    // dispatch form, so $prop is the 0 sentinel. No-op when $prop is absent.
                    if(isArrayOfArraysElement(elemType) || elemType == "bytearray") em.prop = "0";
                    em.selfType = elemType;
                    em.elemType = innerElemType;   // one substitution covers every $opref in the body
                    em.fn = method;
                    for(expression* a : pal.args) em.args.push_back(a->text());
                    for(expression* a : pal.args) em.argTypes.push_back(a->resolvedType);
                    em.trim = emitterTrim::wsSemi;
                    expr->tokens.push_back(expandEmitterBody(blk, em));
                }
            } else {
                // Non-emitter method: emit as subscriptText.method(args)
                const string callName = methodCallName(method, member.value);
                string call = recv + "." + callName + "(";
                for(size_t pi = 0; pi < pal.args.size(); pi++){
                    if(pi > 0) call += ", ";
                    call += pal.args[pi]->text();
                }
                call += ")";
                expr->tokens.push_back(call);
            }
            if(!method->returnType.name.empty()) expr->resolvedType = method->returnType.name;
            subscriptCallResult(expr, expr->tokens.size() - 1, func, body);   // `arr[0].tray()[1]`
        } else {
            // Property access: arr[0].field
            classDef* elemCls = getDispatchClass(elemType);
            if(elemCls != nullptr){
                function<string(classDef*)> findPropType = [&](classDef* c) -> string {
                    for(typeMember* m : c->members)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                            if(vd->name == member.value) return memberTypeName(vd);   // an array with its element type
                    for(classDef* base : c->baseClasses){
                        string t = findPropType(base);
                        if(!t.empty()) return t;
                    }
                    return "";
                };
                string propType = findPropType(elemCls);
                if(!propType.empty()) expr->resolvedType = propType;
            }
            // `arr[0].items[i]` — an element of the element's member word array (inline property data).
            arrayDeclaration* memberArr = elemCls == nullptr ? nullptr :
                dynamic_cast<arrayDeclaration*>(findMemberInHierarchy(elemCls, [&](typeMember* m){
                    return dynamic_cast<arrayDeclaration*>(m) != nullptr && m->name == member.value; }));
            if(memberArr != nullptr && !heldByPointer(memberArr) && file.peekToken().is(token::bracketOpen)
               && isWordArrayType(expr->resolvedType)){
                file.getToken();   // '['
                expression* innerIndex = parseExpression(file.getToken(), {token::bracketClose}, func, body);
                rejectConstantIndexOutside(memberArr, innerIndex, member.originalValue);
                if(!expr->tokens.empty()) expr->tokens.pop_back();
                expr->tokens.push_back(valueSlotText(checkedMemberSlot("(" + recv + ")", memberI6Name(elemType, member.value),
                                                                       innerIndex->text(), false, memberArrayDeclTracked(memberArr)), memberArr->elementType));
                expr->resolvedType = memberArr->elementType;
                if(file.peekToken().is(token::bracketOpen) && isArrayOfArraysElement(memberArr->elementType)){   // `ring[0].grid[0][1]`
                    string row = expr->tokens.back();
                    chainSubscripts(expr, row, func, body);
                }
                return ExprStep::Advance;
            }
            // An emitter value or a getter on the element (`grid[0].length`) reads through its
            // emitter; a subscript's result is a value, so an array element is (array, 0).
            string readRet;
            string read = expandMemberValueEmitter(recv, "", elemType, member.value, func, body, readRet);
            if(read.empty() && isPropertyClassType(expr->resolvedType)){
                ArrayReceiver valueArr;
                read = applyPropertyClassRead(recv, expr->resolvedType, readRet,
                                              isWordArrayType(elemType) ? &valueArr : nullptr);
            }
            if(!expr->tokens.empty()) expr->tokens.pop_back();
            if(!read.empty()){
                expr->tokens.push_back(read);
                if(!readRet.empty()) expr->resolvedType = readRet;
            } else if(functionDef* g = castType.empty() && !readingForBind && !pathGoesOnToOwnMember(st, expr->resolvedType)
                                       && !nextOperatorIsOwn(st, expr->resolvedType) ? nativeGetterOf(expr->resolvedType) : nullptr){
                expr->tokens.push_back(recv + "." + memberI6Name(elemType, member.value) + "." + g->i6name + "()");
                expr->resolvedType = g->returnType.name;
            } else
                expr->tokens.push_back(recv + "." + memberI6Name(elemType, member.value));
            // `stubs[0].text[1]`: the member's own `operator []`.
            if(file.peekToken().is(token::bracketOpen)){
                string row = expr->tokens.back();
                chainSubscripts(expr, row, func, body);
            }
        }
    }
    // Apply a pending cast to the subscript (or subscript.member) result —
    // e.g. `(verb)results[0]`. The bare-identifier path applies casts too;
    // without this the cast leaks past the next operator, mistyping
    // `(verb)r[0] == V` as `verb` instead of `ebool` (breaks a following `||`).
    if(!castType.empty() && !expr->tokens.empty()){
        string operand = expr->tokens.back();
        expr->tokens.back() = applyCastConversion(operand, expr->resolvedType, castType);
        expr->resolvedType = castType;
        castType = "";
    }
    return ExprStep::Advance;
}

// ── name(args): function call in expression ──
bglParser::ExprStep bglParser::parseExprCall(ExprParseState& st, token& next){
    expression* expr = st.expr;
    token& cur = st.cur;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    string callName = cur.value;
    // `::name(args)` calls the global function, past any member of the same name (§3.9).
    bool forceGlobal = callName.size() > 2 && callName[0] == ':' && callName[1] == ':';
    if(forceGlobal) callName = callName.substr(2);
    // replace chaining: replaced() resolves to the predecessor's mangled name
    functionDef* replacing = replaceOwner();   // `replaced()` in a lambda belongs to the routine around it
    if(callName == "replaced" && replacing && replacing->replaceFoundNothing)
        parsingError(format("replaced() has nothing to call: no '{0}' is declared before this replace. A replace "
            "follows what it replaces; move it below '{0}' (or below the include that declares it).", replacing->dName()));
    if(callName == "replaced" && replacing && !replacing->replacedTarget.empty()){
        callName = replacing->replacedTarget;
        replacing->replacedWasCalled = true;
    }
    // Self-call detection for object member methods.
    // Checks direct members of currentObject AND walks the class hierarchy
    // (currentClass->baseClasses) so inherited methods resolve as self.method().
    // Inherited methods that share a name with a global function are skipped —
    // the global path has proper arity matching and should win (e.g. print).
    bool isSelfCall = false;
    // A member that cannot take this many arguments does not hide a global function of its name
    // (`print(x)` inside a class's own `print()`).
    auto memberFits = [&](functionDef* fd){ return memberTakesCall(fd, callName, true); };
    // A local or parameter of the name (a func<> value) is nearer than any member (§3.8.1).
    bool isLocalName = func != nullptr && (qualifyFromParams(callName, func) || qualifyFromBodyLocals(callName, body)
                                           || qualifyFromAncestorBlocks(callName, body));
    if(!forceGlobal) rejectCallOfPlainLocal(callName, cur.originalValue, func, body);
    if(func != nullptr && !forceGlobal && !isLocalName && callName.find('.') == string::npos){
        // Recursive self-call: the function being parsed isn't yet in
        // currentObject->members (registered post-body), so match by name
        // against currentFunc when we're inside an object/class method.
        if((currentObject != nullptr || currentClass != nullptr) &&
           currentFunc != nullptr && currentFunc->name == callName && memberFits(currentFunc))
            isSelfCall = true;
        if(!isSelfCall && currentObject != nullptr)
            for(typeMember* m : currentObject->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == callName && memberFits(fd)){ isSelfCall = true; break; }
        // Inherited methods: the class being declared, or the class of the object being declared.
        classDef* selfClass = currentClass != nullptr ? currentClass
                            : currentObject != nullptr ? currentObject->objectClass : nullptr;
        // The class's own method is nearer than a global function of the name (`::name(…)` reaches the
        // global); one that can't take this many arguments doesn't hide it (`print(x)` in a class's `print()`).
        if(!isSelfCall && selfClass != nullptr){
            bool isGlobalFunc = languageService.findGlobalAs<functionDef>(callName) != nullptr;
            isSelfCall = selfClass->findMember([&](typeMember* m){
                auto* fd = dynamic_cast<functionDef*>(m);
                return fd != nullptr && fd->name == callName && (!isGlobalFunc || memberFits(fd));
            }) != nullptr;
        }
    }
    // Note: '(' was already consumed by exprNext(st) above
    // A pending cast applies to the call's result — `(Dog)make()`. Without this the cast was dropped
    // and the value kept the callee's declared return type.
    string& castType = st.castType;
    string pendingCast = castType;
    castType = "";
    size_t callStart = expr->tokens.size();
    auto applyResultCast = [&]{
        if(pendingCast.empty() || expr->tokens.size() <= callStart) return;
        string callText;
        for(size_t i = callStart; i < expr->tokens.size(); i++) callText += expr->tokens[i];
        expr->tokens.resize(callStart);
        expr->tokens.push_back(applyCastConversion(callText, expr->resolvedType, pendingCast));
        expr->resolvedType = pendingCast;
    };
    // `::name(…)` past a local of the name: the call is marked so the local, not the call, is renamed.
    callPastLocal = forceGlobal && isLocalName;
    if(parseExprFunctionCall(expr, callName, isSelfCall, func, body)) {
        callPastLocal = false;
        subscriptCallResult(expr, callStart, func, body);
        applyResultCast();
        // Emitter was inlined — advance past the function call and continue
        cur = exprNext(st);
        return ExprStep::Continue;
    }
    callPastLocal = false;
    subscriptCallResult(expr, callStart, func, body);
    applyResultCast();
    return ExprStep::Advance;
}

// `call(args)[i]`: the operator [] of what the call returned, the tokens from `callStart` being the call.
void bglParser::subscriptCallResult(expression* expr, size_t callStart, functionDef* func, statementBlock* body){
    while(file.peekToken().is(token::bracketOpen) && callStart < expr->tokens.size() && !expr->resolvedType.empty()){
        const string t = expr->resolvedType;
        classDef* cls = getDispatchClass(t == "array<char>" ? "bytearray" : t);
        if(cls == nullptr) return;
        string elem = t == "bytearray" || t == "array<char>" ? "char" : arrayInnerType(t);
        functionDef* getM = !elem.empty() ? findArraySubscriptOp(cls, elem, /*isWrite=*/false)
            : dynamic_cast<functionDef*>(findMemberInHierarchy(cls, [](typeMember* m){
                  auto* f = dynamic_cast<functionDef*>(m); return f && f->name == "[]" && f->params.size() == 1; }));
        if(getM == nullptr) return;
        auto* blk = getM->isEmitter ? dynamic_cast<i6Block*>(getM->body) : nullptr;
        if(getM->isEmitter && blk == nullptr) return;
        string callText;
        for(size_t i = callStart; i < expr->tokens.size(); i++) callText += expr->tokens[i];
        file.getToken(token::bracketOpen);
        expression* idx = parseExpression(file.getToken(), {token::bracketClose}, func, body);
        string recv = "(" + callText + ")";
        string text;
        if(blk != nullptr){
            emitterBindings gb; gb.self = recv; gb.val = recv; gb.prop = "0"; gb.fn = getM; gb.args.push_back(idx->text());
            text = expandEmitterBody(blk, gb);
        } else {
            if(getM->i6name.empty()) getM->i6name = mangleOperatorName(getM->name);
            text = recv + "." + getM->i6name + "(" + idx->text() + ")";
        }
        expr->tokens.resize(callStart);
        expr->tokens.push_back(text);
        string ret = getM->returnType.name;
        expr->resolvedType = (ret == "t" || ret == "T") && !elem.empty() ? elem : ret;
    }
}

// ── name?.: optional chaining (expression-level) ──
bglParser::ExprStep bglParser::parseExprOptionalChain(ExprParseState& st, token& next){
    expression* expr = st.expr;
    token& cur = st.cur;
    optional<token>& prefetched = st.prefetched;
    string& castType = st.castType;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    // Optional chaining: obj?.member or obj?.method(args)
    // Lowers to: _bgl_tempN = obj; if(nullTest) _bgl_tempN = _bgl_tempN.member;
    string optTemp = newChainTemp();
    string receiverCastType;          // `((T)x)?.…`: the receiver's type under its cast
    classDef* chainAncestor = nullptr;   // T when it is a strict ancestor: the first call runs T's version
    // A stringObj a step returns is held until the statement is done: a later step's temporary
    // string (a char argument's) would otherwise take its buffer.
    bool chainHeldString = false;
    auto holdChainString = [&](const string& call, const string& type){
        if(strcasecmp(type.c_str(), "stringobj") != 0) return call;
        chainHeldString = true;
        return "_bglStringLockStack.stackLock(" + call + ")";
    };
    string lhsName = cur.value;
    string lhsType;
    // A receiver already parsed (`owner.dog?.parent`) arrives as text + type; leading `(` tokens
    // belong to an enclosing group and stay in place ahead of the chain's result.
    vector<string> chainPrefix;
    string resultCast;
    if(!st.chainRecvText.empty()){
        lhsName = st.chainRecvText;
        lhsType = st.chainRecvType;
        // Only parentheses the receiver leaves open belong to the enclosing group: `(o)?.parent` keeps its own.
        long unclosed = 0;
        for(const auto& t : expr->tokens){ if(t == "(") unclosed++; else if(t == ")") unclosed--; }
        while(!expr->tokens.empty() && (expr->tokens.front() == "~~" || (expr->tokens.front() == "(" && unclosed > 0))){
            if(expr->tokens.front() == "(") unclosed--;
            chainPrefix.push_back(expr->tokens.front());
            expr->tokens.erase(expr->tokens.begin());
        }
        lhsName.clear(); for(const auto& t : expr->tokens) lhsName += t;
        st.chainRecvText.clear(); st.chainRecvType.clear();
        // `((Node)leaf)?.Kind()`: the receiver was upcast, so its first call runs the ancestor's version.
        {
            string bare = lhsName;
            while(bare.size() >= 2 && bare.front() == '(' && bare.back() == ')') bare = bare.substr(1, bare.size() - 2);
            if(!expr->upcastText.empty() && bare == expr->upcastText && isUpcast(lhsType, expr->upcastFrom))
                chainAncestor = getDispatchClass(lhsType);
            expr->upcastText.clear();
        }
        resultCast = castType;   // `(T)a.next?.x`: a cast written before the path is the chain's result's
    } else {
        // A cast before the chain applies to its result, as before a `.` path (§4.11).
        resultCast = castType;
        chainPrefix = expr->tokens;   // an enclosing group's open parentheses
        // `++x?.m`: the step belongs to the member the chain reads.
        if(!chainPrefix.empty() && (chainPrefix.back() == "++" || chainPrefix.back() == "--")){
            chainPrefixStepOp = chainPrefix.back();
            chainPrefix.pop_back();
        }
        // `((Node)leaf)?.Kind()`: a cast of the receiver retypes it; to a strict ancestor, its call runs that ancestor's version.
        if(st.receiverCast){ receiverCastType = castType; resultCast = st.chainCast; st.chainCast.clear(); st.receiverCast = false; }
        // Hinted with the member reached on it: a name both a verb and an imported member is the import's.
        string hint = file.peekToken().value;
        lhsType = resolveIdentifierType(lhsName, func, body, hint);
        // As this routine reaches it: a member through self, an outer local through a lambda's capture.
        if(!lhsType.empty() && func != nullptr)
            if(string q = qualifyIdentifier(lhsName, func, body, hint); !q.empty()) lhsName = q;
        if(!receiverCastType.empty()){
            if(isUpcast(receiverCastType, lhsType)) chainAncestor = getDispatchClass(receiverCastType);
            lhsType = receiverCastType;
        }
    }
    castType = "";
    if(lhsType.empty()) parsingError(format("Unknown variable '{0}' in optional chain", lhsName));
    // Look up operator?() on LHS type for the null test
    classDef* lhsCls = getDispatchClass(lhsType);
    if(lhsCls == nullptr) parsingError(format("Type '{0}' does not support optional chaining (not a class)", lhsType));
    functionDef* nullTestFn = findQueryOp(lhsCls, lhsType);
    if(nullTestFn == nullptr)
        parsingError(format("Type '{0}' does not support optional chaining (no operator?() emitter)", lhsType));
    auto getNullTest = [&](functionDef* fn, const string& selfText) -> string {
        auto* blk = dynamic_cast<i6Block*>(fn->body);
        emitterBindings nb; nb.self = selfText; nb.val = selfText; nb.trim = emitterTrim::wsSemi;
        return expandEmitterBody(blk, nb);
    };
    // Build the guarded chain
    string injText = optTemp + " = " + lhsName + ";";
    string currentType = lhsType;
    // One property step off optTemp. A property-class member (e.g. `parent`) reads through its
    // operator() emitter, exactly as a plain `.` read does; propType becomes the read's type.
    // `after` is the token that follows the member: the accessor's own member or operator takes the accessor itself.
    // A member word array the chain stopped at (`car?.loads`): the temporary still holds its owner, and
    // the array is its property — `$prop` for the next step's emitters.
    string chainArrProp;
    arrayDeclaration* chainArr = nullptr;
    auto arraySizeRead = [&](const string& which) -> string {
        string words = "((" + optTemp + ".#" + chainArrProp + ")/WORDSIZE)";
        if(!memberArrayDeclTracked(chainArr)) return words;
        return which == "size" ? "(" + words + " - 1)" : "(" + optTemp + ".&" + chainArrProp + "-->(" + words + " - 1))";
    };
    // A member's declared type: an object's own member first, then its class's.
    auto memberDeclType = [&](const string& owner, const string& name) -> string {
        typeMember* pm = nullptr;
        if(objectDef* od = languageService.findObjectType(owner))
            for(typeMember* m : od->members) if(m->name == name){ pm = m; break; }
        if(pm == nullptr) if(classDef* cc = getDispatchClass(owner))
            pm = findMemberInHierarchy(cc, [&](typeMember* m){ return m->name == name; });
        auto* vd = dynamic_cast<variableDeclaration*>(pm);
        return vd != nullptr ? memberTypeName(vd) : string();
    };
    auto chainPropertyRead = [&](const string& memberName, string& propType, token* after = nullptr) -> string {
        if(classDef* cls = getDispatchClass(currentType))
            if(auto* vd = dynamic_cast<variableDeclaration*>(findMemberInHierarchy(cls, [&](typeMember* m){
                    auto* v = dynamic_cast<variableDeclaration*>(m); return v && v->name == memberName; })))
                propType = vd->type.name;
        if(propType.empty()) propType = memberDeclType(currentType, memberName);   // an object's own member
        string readRet;
        string read = expandMemberValueEmitter(optTemp, "", currentType, memberName, func, body, readRet);
        if(read.empty()){ ArrayReceiver whole; read = applyPropertyClassRead(optTemp, propType, readRet, isWordArrayType(currentType) ? &whole : nullptr); }
        if(read.empty()){
            string plain = optTemp + "." + memberI6Name(currentType, memberName);
            bool keep = readingForBind || (after != nullptr
                && ((after->is(token::period) && isOwnMember(propType, file.peekToken().value))
                    || (!after->is(token::period) && declaresOwnOperator(propType, after->value)
                        && (!after->is("?") || queryIsPostfix(file.peekToken())))));
            if(functionDef* g = keep ? nullptr : nativeGetterOf(propType)){ propType = g->returnType.name; return plain + "." + g->i6name + "()"; }
            return plain;
        }
        if(!readRet.empty()) propType = readRet;
        return read;
    };
    // Process chain steps: each is ?.member, ?.method(), or a trailing .member/.method()
    while(true){
        string nullTest = getNullTest(nullTestFn, optTemp);
        injText += " if (" + nullTest + ") {";
        // member name may be a `dataType` token if it collides with a type name — accept both.
        token member = file.getToken({eTokenType::identifier, eTokenType::dataType});
        token afterMember = exprNext(st);
        if(afterMember.is(token::parenOpen)){
            // ?.method(args) — find method, inline emitter or call
            string methName = member.value;
            classDef* cls = getDispatchClass(currentType);
            ParsedArgList opal = parseCallArgList(func, body, braceArgHints(collectMethodCandidates(currentType, methName)));
            vector<expression*> callArgs = opal.args;
            // An array's methods are resolved with its element type bound (`indexOf(T)`).
            MethodMatch mm = !arrayElemType(currentType).empty() && opal.namedArgNames.end() == find_if(opal.namedArgNames.begin(), opal.namedArgNames.end(), [](const string& n){ return !n.empty(); })
                ? resolveMethod(currentType, optTemp, methName, callArgs, arrayElemType(currentType))
                : resolveMethodNamed(currentType, optTemp, methName, callArgs, opal.namedArgNames);
            functionDef* method = mm.method;
            if(!method) method = synthesizeFuncPropertyCall(currentType, methName);   // a func<> member is called through it
            if(!method && !collectMethodCandidates(currentType, methName).empty()) reportMethodOverloadMismatch(currentType, methName, callArgs);
            if(!method) parsingError(format("No method '{0}' on type '{1}' in optional chain", methName,
                cls != nullptr ? cls->dName() : typeDisplayName(currentType)));
            { string savedRecv = defaultArgReceiver; defaultArgReceiver = optTemp;
              finalizeCallArgs(callArgs, opal.namedArgNames, opal.interpSegmentsPerArg, method);   // defaulted arguments
              defaultArgReceiver = savedRecv; }
            mangleOverloadSetForReceiver(currentType, methName);
            if(method->isEmitter){
                if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                    emitterBindings mb; mb.self = optTemp; mb.val = optTemp; mb.trim = emitterTrim::wsSemi;
                    if(!chainArrProp.empty()){ mb.prop = chainArrProp; mb.elemType = arrayElemType(currentType); mb.elemContext = methName; }
                    else if(isWordArrayType(currentType)){ mb.prop = "0"; mb.elemType = arrayElemType(currentType); mb.elemContext = methName; }
                    mb.fn = method;
                    for(expression* a : callArgs) mb.args.push_back(a->text());
                    for(expression* a : callArgs) mb.argTypes.push_back(a->resolvedType);
                    injText += method->returnType.name == "void"   // a statement emitter (`give`) has no value to keep
                        ? " " + expandEmitterBody(blk, mb) + ";"
                        : " " + optTemp + " = " + expandEmitterBody(blk, mb) + ";";
                }
            } else {
                string callName = methodCallName(method, methName);
                if(chainAncestor != nullptr)
                    if(classDef* owner = chainAncestor->declaringClass(method)) callName = owner->i6Name() + "::" + callName;
                chainAncestor = nullptr;
                string call = optTemp + "." + callName + "(";
                for(size_t i = 0; i < callArgs.size(); i++){
                    if(i > 0) call += ", ";
                    call += callArgs[i]->text();
                }
                call += ")";
                // A value-class result is copied out, as a direct call's is: two in one expression stay apart.
                injText += " " + optTemp + " = " + holdChainString(keepValueResult(call, method->returnType.name), method->returnType.name) + ";";
            }
            currentType = method->returnType.name;
            afterMember = exprNext(st);
        } else {
            // ?.property
            string propType = resolvePathType("_x_." + member.value, func, body);
            arrayDeclaration* memberArr = nullptr;
            // An object's own member array first, then its class's.
            if(objectDef* od = languageService.findObjectType(currentType))
                for(typeMember* m : od->members)
                    if(auto* ad = dynamic_cast<arrayDeclaration*>(m); ad && m->name == member.value){ memberArr = ad; break; }
            if(memberArr == nullptr)
                if(classDef* cc = getDispatchClass(currentType))
                    memberArr = dynamic_cast<arrayDeclaration*>(findMemberInHierarchy(cc, [&](typeMember* m){
                        return dynamic_cast<arrayDeclaration*>(m) != nullptr && m->name == member.value; }));
            if(!chainArrProp.empty() && (member.value == "length" || member.value == "size")){
                // `car?.loads?.length`: read from the owner the temporary still holds.
                injText += " " + optTemp + " = " + arraySizeRead(member.value) + ";";
                chainArrProp.clear(); chainArr = nullptr;
                currentType = "int";
            } else if(memberArr != nullptr && !heldByPointer(memberArr) && (afterMember.is(token::period) || afterMember.is("?."))){
                // Stop at the array: its owner stays in the temporary for the next step.
                chainArrProp = memberI6Name(currentType, member.value);
                chainArr = memberArr;
                currentType = "array<" + memberArr->elementType + ">";
            } else if(memberArr != nullptr && afterMember.is(token::bracketOpen)){
                // `x?.history[1]`: an element of the member array, read once the receiver is known.
                expression* idx = parseExpression(file.getToken(), {token::bracketClose}, func, body);
                string prop = memberI6Name(currentType, member.value);
                string read = heldByPointer(memberArr)
                    ? valueSlotText(chainedSubscriptRead(dynamic_cast<functionDef*>(findMemberInHierarchy(getDispatchClass("array<" + memberArr->elementType + ">"),
                          [](typeMember* m){ auto* f = dynamic_cast<functionDef*>(m); return f && f->name == "[]" && f->params.size() == 1; })),
                          optTemp + "." + prop, idx->text(), "array<" + memberArr->elementType + ">"), memberArr->elementType)
                    : valueSlotText(checkedMemberSlot(optTemp, prop, idx->text(), false, memberArrayDeclTracked(memberArr)), memberArr->elementType);
                injText += " " + optTemp + " = " + read + ";";
                currentType = memberArr->elementType;
                afterMember = exprNext(st);
            } else if(memberArr != nullptr && heldByPointer(memberArr) && (afterMember.is(token::period) || afterMember.is("?."))){
                // A member array held through a pointer is a value: the next step works on the array itself.
                injText += " " + optTemp + " = " + optTemp + "." + memberI6Name(currentType, member.value) + ";";
                currentType = "array<" + memberArr->elementType + ">";
            } else if(classDef* pc = getDispatchClass(propType.empty() ? memberDeclType(currentType, member.value) : propType);
                      pc != nullptr && pc->isEmitterClass && pc->isExternal && !pc->isPrimitive && afterMember.is(token::period)
                      && languageService.findEnum(propType.empty() ? memberDeclType(currentType, member.value) : propType) == nullptr
                      && findMemberInHierarchy(pc, [this](typeMember* m){ return isPropertyClassReadEmitter(m); }) == nullptr){
                // A property-class member (`children`) followed by its own member: the owner stays the receiver,
                // as the property class's emitters take it.
                currentType = pc->name;
            } else {
                // As after `.`, a member the receiver's type doesn't declare is an error (`ns?.count` on an object).
                if(!looseIdentifierMode && func != nullptr && !currentType.empty() && currentType != "var"
                   && !isPropertyValuedLocal(member.value, func, body)){
                    bool declared = false;
                    if(objectDef* od = languageService.findObjectType(currentType))
                        for(typeMember* m : od->members) if(m->name == member.value) declared = true;
                    classDef* rc = languageService.classOf(currentType);
                    if(!declared && rc != nullptr && findMemberInHierarchy(rc, [&](typeMember* m){ return m->name == member.value; }) == nullptr)
                        parsingError(format("'{0}' is not a declared property of type '{1}'. Declare it as a member so the "
                                            "access type-checks.", member.value, typeDisplayName(currentType)));
                }
                injText += " " + optTemp + " = " + chainPropertyRead(member.value, propType, &afterMember) + ";";
                if(propType.empty()) propType = memberDeclType(currentType, member.value);
                // An accessor read at the end of the chain gives its getter's value, as a plain read does.
                bool ownOpNext = declaresOwnOperator(propType, afterMember.value)
                                 && (!afterMember.is("?") || queryIsPostfix(file.peekToken()));
                if(!afterMember.is(token::period) && !afterMember.is("?.") && !readingForBind && !ownOpNext)
                    if(functionDef* fn = nativeGetterOf(propType)){
                        injText += " " + optTemp + " = " + optTemp + "." + fn->i6name + "();";
                        propType = fn->returnType.name;
                    }
                currentType = propType;
            }
        }
        // `x?.me()[0]`, `x?.next[0]`: a class's own operator [] on what the step gave.
        while(afterMember.is(token::bracketOpen) && chainArrProp.empty() && !isWordArrayType(currentType) && currentType != "bytearray"){
            classDef* sc = getDispatchClass(currentType);
            auto* gm = sc ? dynamic_cast<functionDef*>(findMemberInHierarchy(sc, [](typeMember* m){
                auto* f = dynamic_cast<functionDef*>(m); return f && f->name == "[]" && f->params.size() == 1; })) : nullptr;
            if(gm == nullptr) break;
            expression* idx = parseExpression(file.getToken(), {token::bracketClose}, func, body);
            injText += " " + optTemp + " = " + chainedSubscriptRead(gm, optTemp, idx->text(), currentType) + ";";
            currentType = gm->returnType.name;
            afterMember = exprNext(st);
        }
        // Check for continuation: another ?. or regular .
        if(afterMember.is("?.")){
            // Another optional step: look up operator?() on the current type
            classDef* nextCls = getDispatchClass(currentType);
            nullTestFn = nullptr;
            if(nextCls != nullptr)
                nullTestFn = findQueryOp(nextCls, currentType);
            if(nullTestFn == nullptr)
                parsingError(format("Type '{0}' does not support optional chaining (no operator?() emitter)", currentType));
            continue; // next chain step
        } else if(afterMember.is(token::period)){
            // Regular dot after optional chain: ?.parent().name — non-guarded step
            // (member name may collide with a type name → accept dataType too).
            token nextMember = file.getToken({eTokenType::identifier, eTokenType::dataType});
            token afterNext = exprNext(st);
            if(afterNext.is(token::parenOpen)){
                // .method(args) — build as non-guarded call
                string methName = nextMember.value;
                classDef* cls = getDispatchClass(currentType);
                ParsedArgList opal = parseCallArgList(func, body, braceArgHints(collectMethodCandidates(currentType, methName)));
                vector<expression*> callArgs = opal.args;
                MethodMatch mm2 = !arrayElemType(currentType).empty() && opal.namedArgNames.end() == find_if(opal.namedArgNames.begin(), opal.namedArgNames.end(), [](const string& n){ return !n.empty(); })
                    ? resolveMethod(currentType, optTemp, methName, callArgs, arrayElemType(currentType))
                    : resolveMethodNamed(currentType, optTemp, methName, callArgs, opal.namedArgNames);
                functionDef* method = mm2.method;
                if(!method) method = synthesizeFuncPropertyCall(currentType, methName);   // a func<> member is called through it
                if(!method && !collectMethodCandidates(currentType, methName).empty()) reportMethodOverloadMismatch(currentType, methName, callArgs);
                if(!method) parsingError(format("No method '{0}' on type '{1}' in optional chain", methName,
                    cls != nullptr ? cls->dName() : typeDisplayName(currentType)));
                { string savedRecv = defaultArgReceiver; defaultArgReceiver = optTemp;
                  finalizeCallArgs(callArgs, opal.namedArgNames, opal.interpSegmentsPerArg, method);   // defaulted arguments
                  defaultArgReceiver = savedRecv; }
                mangleOverloadSetForReceiver(currentType, methName);
                if(method->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                        emitterBindings mb; mb.self = optTemp; mb.val = optTemp; mb.trim = emitterTrim::wsSemi;
                        if(!chainArrProp.empty()){ mb.prop = chainArrProp; mb.elemType = arrayElemType(currentType); mb.elemContext = methName; }
                    else if(isWordArrayType(currentType)){ mb.prop = "0"; mb.elemType = arrayElemType(currentType); mb.elemContext = methName; }
                        mb.fn = method;
                        for(expression* a : callArgs) mb.args.push_back(a->text());
                        for(expression* a : callArgs) mb.argTypes.push_back(a->resolvedType);
                        injText += method->returnType.name == "void"   // a statement emitter (`give`) has no value to keep
                        ? " " + expandEmitterBody(blk, mb) + ";"
                        : " " + optTemp + " = " + expandEmitterBody(blk, mb) + ";";
                    }
                } else {
                    const string callName = methodCallName(method, methName);
                    string call = optTemp + "." + callName + "(";
                    for(size_t i = 0; i < callArgs.size(); i++){ if(i > 0) call += ", "; call += callArgs[i]->text(); }
                    call += ")";
                    injText += " " + optTemp + " = " + holdChainString(keepValueResult(call, method->returnType.name), method->returnType.name) + ";";
                }
                currentType = method->returnType.name;
                afterMember = exprNext(st);
                // Could chain further — check again
                if(afterMember.is("?.") || afterMember.is(token::period)) { prefetched = afterMember; /* TODO: loop */ }
                else prefetched = afterMember;
            } else if(!chainArrProp.empty() && (nextMember.value == "length" || nextMember.value == "size")){
                // `car?.loads.length`: read from the owner the temporary still holds.
                injText += " " + optTemp + " = " + arraySizeRead(nextMember.value) + ";";
                chainArrProp.clear(); chainArr = nullptr;
                currentType = "int";
                prefetched = afterNext;
            } else {
                // .property
                string propType;
                injText += " " + optTemp + " = " + chainPropertyRead(nextMember.value, propType) + ";";
                currentType = propType;
                prefetched = afterNext;
            }
            break; // end of chain
        } else {
            prefetched = afterMember;
            break; // end of chain
        }
    }
    // `x?.m++` as a value: the step that reads the member steps it too, so the member changes, not the
    // temporary (`--` likewise; prefix `++x?.m` sets prefixStepOp).
    {
        static const std::regex lastRead(R"((\S+) = \1\.([A-Za-z_][A-Za-z0-9_]*);\s*$)");
        std::smatch lm;
        token after = prefetched ? *prefetched : file.peekToken();
        string op = !chainPrefixStepOp.empty() ? chainPrefixStepOp
                  : (after.value == "++" || after.value == "--") ? after.value : string();
        if(!op.empty() && regex_search(injText, lm, lastRead) && lm[1].str() == optTemp && languageService.findClass(currentType) != nullptr
           && (currentType == "int" || currentType == "char")){
            string member = lm[2].str();
            string stepped = chainPrefixStepOp.empty() ? optTemp + "." + member + op : op + optTemp + "." + member;
            injText = injText.substr(0, lm.position(0)) + optTemp + " = " + stepped + ";";
            if(chainPrefixStepOp.empty()){ if(prefetched) prefetched.reset(); else file.getToken(); }
            chainPrefixStepOp.clear();
        }
        // The chain ends at an element (`x?.marks[i]++`): I6 steps an array entry in place.
        static const std::regex lastElem(R"((\S+) = ([^;]*-->[^;]+);\s*$)");
        if(!op.empty() && regex_search(injText, lm, lastElem) && lm[1].str() == optTemp && (currentType == "int" || currentType == "char")){
            string slot = lm[2].str();
            size_t arrow = slot.rfind("-->");
            string held = newChainTemp(), at = held + "-->" + slot.substr(arrow + 3);
            string step = op == "++" ? " + 1" : " - 1";
            string base = held + " = " + slot.substr(0, arrow) + "; ";
            injText = injText.substr(0, lm.position(0)) + base
                    + (chainPrefixStepOp.empty() ? optTemp + " = " + at + "; " + at + " = " + optTemp + step + ";"
                                                 : optTemp + " = " + at + step + "; " + at + " = " + optTemp + ";");
            if(chainPrefixStepOp.empty()){ if(prefetched) prefetched.reset(); else file.getToken(); }
            chainPrefixStepOp.clear();
        }
        // The chain ends at an accessor's reading: step it through its setter.
        static const std::regex lastGet(R"((\S+) = (\1\.[A-Za-z_][A-Za-z0-9_]*\.[A-Za-z_][A-Za-z0-9_]*\(\));\s*$)");
        if(!op.empty() && regex_search(injText, lm, lastGet) && lm[1].str() == optTemp){
            size_t before = pendingInjections.size();
            if(string stepped = stepThroughAccessor(lm[2].str(), op, !chainPrefixStepOp.empty()); !stepped.empty()){
                string setUp = takeBranchSetUp(before);
                injText = injText.substr(0, lm.position(0)) + setUp + optTemp + " = " + stepped + ";";
                if(chainPrefixStepOp.empty()){ if(prefetched) prefetched.reset(); else file.getToken(); }
                chainPrefixStepOp.clear();
            }
        }
        if(!chainPrefixStepOp.empty()){ chainPrefix.push_back(chainPrefixStepOp); chainPrefixStepOp.clear(); }
    }
    // Close all open if-braces. A failed test yields null, not the receiver it stopped at — a type's
    // `operator ?` can be false for a value that isn't 0.
    {
        size_t opens = 0;
        for(size_t i = 0; i < injText.size(); i++)
            if(injText[i] == '{') opens++;
            else if(injText[i] == '}') opens--;
        for(size_t i = 0; i < opens; i++) injText += " } else " + optTemp + " = 0;";
    }
    i6RawNode* inj = new i6RawNode();
    inj->text = injText;
    inj->cooked = true;   // built from Beguile expressions: locals take their renames
    if(chainHeldString && !optStringMarkOpen){ inj->text = "_bglStringLockStack.stackMark(); " + inj->text; optStringMarkOpen = true; }
    // A string result is a temporary: anything else the statement computes could take its buffer,
    // so it is held until the statement's expression is done.
    if(strcasecmp(currentType.c_str(), "stringobj") == 0 && exprParseDepth > stmtExprBase){
        if(!optStringMarkOpen){ inj->text = "_bglStringLockStack.stackMark(); " + inj->text; optStringMarkOpen = true; }
        inj->text += " " + optTemp + " = _bglStringLockStack.stackLock(" + optTemp + ");";
    }
    pendingInjections.push_back(inj);
    expr->tokens.clear();
    for(const auto& p : chainPrefix) expr->tokens.push_back(p);
    if(!resultCast.empty()){
        expr->tokens.push_back(applyCastConversion(optTemp, currentType, resultCast));
        expr->resolvedType = resultCast;
        return ExprStep::Advance;
    }
    expr->tokens.push_back(optTemp);
    expr->nullableText = optTemp;
    if(!currentType.empty()) expr->resolvedType = currentType;
    return ExprStep::Advance;
}

// `self.property` — resolve the member against the enclosing class/object.
bglParser::ExprStep bglParser::parseExprSelfMember(ExprParseState& st, token& member, token& afterMember){
    expression* expr = st.expr;
    optional<token>& prefetched = st.prefetched;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    // Property access: self.property — look up type in current class/object
    prefetched = afterMember;
    string memberType;
    if(currentClass != nullptr)
        for(typeMember* m : currentClass->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == member.value){ memberType = vd->type.name; break; }
    if(memberType.empty() && currentObject != nullptr)
        for(typeMember* m : currentObject->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == member.value){ memberType = vd->type.name; break; }
    // Inherited member: fall back to the class hierarchy. Without this,
    // `self.attributes` on an instance that didn't write
    // `attributes = {…}` leaves resolvedType empty, and the next chained
    // `.method(...)` falls through to global-function resolution and
    // surfaces as "Undeclared function 'X'".
    if(memberType.empty()){
        classDef* hier = currentClass;
        if(hier == nullptr && currentObject != nullptr)
            hier = currentObject->objectClass;
        if(hier != nullptr){
            typeMember* found = findMemberInHierarchy(hier, [&](typeMember* m){
                auto* vd = dynamic_cast<variableDeclaration*>(m);
                return vd != nullptr && vd->name == member.value;
            });
            if(found) memberType = dynamic_cast<variableDeclaration*>(found)->type.name;
        }
    }
    // Inside a lambda: qualify 'self' through capture if needed
    string selfText = "self";
    if(lambdaOuterFunc != nullptr){
        string qualified = qualifyIdentifier("self", func, body);
        if(!qualified.empty()) selfText = qualified;
    }
    // Property-class read: if the member's type exposes an owner-based operator()
    // read emitter (parentProp → parent($self)), dispatch through it — mirrors the
    // non-self property-access path. Without this, `self.parent` emitted a raw
    // property read (reading a non-existent I6 property).
    string selfType = currentClass != nullptr ? currentClass->name : currentObject != nullptr ? currentObject->name : "";
    string accessText = selfText + "." + (selfType.empty() ? member.value : memberI6Name(selfType, member.value));
    {
        string readRet;
        string readText = selfType.empty() ? "" : expandMemberValueEmitter(selfText, "self", selfType, member.value, func, body, readRet);
        if(readText.empty()) readText = applyPropertyClassRead(selfText, memberType, readRet);
        if(!readText.empty()){ accessText = readText; if(!readRet.empty()) memberType = readRet; }
        else if(functionDef* g = nativeGetterOf(memberType); g != nullptr && !pathGoesOnToOwnMember(st, memberType)
                && !nextOperatorIsOwn(st, memberType)){
            accessText += "." + g->i6name + "()";   // not when the path reads the accessor's own member
            memberType = g->returnType.name;
        }
    }
    // `(Chip)self.child`: a cast written before the path applies to its end.
    bool pathGoesOn = st.prefetched ? st.prefetched->is(token::period) : file.peekToken().is(token::period);
    if(!st.castType.empty() && !st.receiverCast && !pathGoesOn && !memberType.empty()){
        expr->tokens.push_back(applyCastConversion(accessText, memberType, st.castType));
        expr->resolvedType = st.castType;
        st.castType = "";
        return ExprStep::Advance;
    }
    if(expr->resolvedType.empty() && !memberType.empty()) expr->resolvedType = memberType;
    expr->tokens.push_back(accessText);
    // Capture host for $self substitution in rvalue property-class operators
    // (mirrors the non-self property-access path).
    expr->emitterSelf = selfText;
    return ExprStep::Advance;
}

// `cursor.hello()`, `cursor.marks.append(4)`: a member the accessor's class doesn't declare (the roots' members
// don't count) is the member of what its getter reads; the receiver becomes that read.
bool bglParser::redirectThroughGetter(string& recvText, string& recvType, const string& member){
    functionDef* g = nativeGetterOf(recvType);
    classDef* ac = getDispatchClass(recvType);
    if(g == nullptr || ac == nullptr) return false;
    auto own = [&](typeMember* m){
        if(m->name != member) return false;
        classDef* owner = ac->declaringClass(m);
        return owner == nullptr || (owner->name != "_bglobject" && owner->name != "_bglprimitive");
    };
    if(findMemberInHierarchy(ac, own) != nullptr) return false;
    classDef* rc = getDispatchClass(g->returnType.name);
    if(rc == nullptr || findMemberInHierarchy(rc, [&](typeMember* m){ return m->name == member; }) == nullptr) return false;
    recvText = parenthesizeReceiver(recvText) + "." + g->i6name + "()";
    recvType = g->returnType.name;
    return true;
}

// `((Node)h)` where h's class is unrelated to Node but converts to it: h is converted first.
void bglParser::convertReceiverForCast(expression* recv, const string& target){
    classDef* from = getDispatchClass(recv->resolvedType);
    classDef* to = getDispatchClass(target);
    if(from == nullptr || to == nullptr || from == to || from->hasAncestor(to) || to->hasAncestor(from)) return;
    if(findMemberInHierarchy(from, [&](typeMember* m){ auto* f = dynamic_cast<functionDef*>(m);
           return f && f->name == "operator()" && f->params.empty() && getDispatchClass(f->returnType.name) == to; }) == nullptr) return;
    recv->tokens = {applyCastConversion(recv->text(), recv->resolvedType, target)};
    recv->resolvedType = target;
}

// A method's I6 name. One not yet declared (called above its class or object) gets the name its
// declaration will: a member named like a global (an attribute such as `light`) is renamed.
string bglParser::methodCallName(functionDef* fd, const string& written){
    if(fd != nullptr && fd->substitutedFrom != nullptr) fd = fd->substitutedFrom;   // named as its declaration is
    if(fd == nullptr || !fd->i6name.empty()) return fd ? fd->i6name : written;
    // The same rule the declaration applies (parseClassMethodMember / processMemberMethod).
    if(fd->isPrePassStub && !fd->isEmitter && !fd->isExternal && !fd->isStatic
       && (isI6OperatorWord(fd->name) || isI6SystemFunction(fd->name) || namesI6Class(fd->name)
           || (fd->src.file.find("beguiLib") == string::npos && namesLibraryGlobal(fd->name))))
        return "_m_" + fd->dName();
    return written;
}

// `obj.method(args)` — bind the call on the receiver's type and emit it.
bglParser::ExprStep bglParser::parseExprMemberCall(ExprParseState& st, token& member, token& afterMember){
    expression* expr = st.expr;
    token& cur = st.cur;
    string& castType = st.castType;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    // method call in expression context: obj.method(args)
    lastRefBacks.clear();
    string objName = cur.value;
    string rawObjName = cur.value;   // pre-qualify form, for inheritance-check ($self rewrite)
    string methName = member.value;
    // Pass memberHint=methName so the resolver prefers a candidate whose type
    // exposes the method, breaking name-collision ties (e.g. enum value vs
    // class instance with the same case-insensitive name).
    // The receiver's ACTUAL static type, independent of any cast. Needed to decide
    // whether an explicit cast is a genuine UPCAST (T an ancestor of the real type),
    // which is what triggers ancestor-qualified (static `::`) dispatch below.
    string actualType = st.receiverCast && !st.innerCast.empty() ? st.innerCast
                      : resolveIdentifierType(objName, func, body, methName);
    st.innerCast.clear();
    // `(T)obj.method()` casts the call's result; only `((T)obj).method()` retypes the receiver.
    string resultCast;
    if(!castType.empty() && !st.receiverCast){ resultCast = castType; castType = ""; }
    if(st.receiverCast && !st.chainCast.empty()){ resultCast = st.chainCast; st.chainCast.clear(); }
    st.receiverCast = false;
    string explicitCast = castType;   // "" unless the receiver was written `((T)obj).method()`
    string objType = !castType.empty() ? castType : actualType;
    castType = "";  // consume the cast
    // Ancestor-qualified method dispatch: `((Base)obj).method(args)` forces STATIC
    // dispatch to Base's version, emitted as I6 `obj.Base::method(args)`. Triggered
    // ONLY when the cast target is a strict ancestor of the receiver's actual type
    // (a real upcast); a plain base-typed local, an identity cast, or a downcast all
    // keep dynamic dispatch. `((Base)self).f()` is the super-call idiom.
    classDef* ancestorDispatchClass = nullptr;
    if(!explicitCast.empty())
        if(isUpcast(explicitCast, actualType))
            ancestorDispatchClass = getDispatchClass(explicitCast);
    if(objType.empty()) parsingError(format("Unknown variable '{0}'", objName));
    // Qualify objName for I6 emission: a #using-imported member like `glulx`
    // needs to emit as `bgl.glulx` (the actual property path) so I6 resolves
    // it correctly. Locals/globals qualify to themselves.
    // The receiver is what its type came from: a verb picked for the method (a member of the same
    // name has no such method) is the verb, not the member.
    bool receiverIsVerb = objName.find('.') == string::npos && actualType != "verb"
                       && verbShadowedByMember(objName, actualType, methName);
    if(receiverIsVerb){ actualType = "verb"; if(castType.empty()) objType = "verb"; }
    // A verb's own method (`Probe.uses2()`) is sent to the verb object, not its action.
    verbObjectDef* ownVerb = nullptr;
    bool globalQualified = objName.rfind("::", 0) == 0;   // `::Tally` names the verb past a local that hides it
    string verbName = globalQualified ? objName.substr(2) : objName;
    if(verbName.find('.') == string::npos && castType.empty()
       && (globalQualified || !(func != nullptr && (qualifyFromParams(objName, func) || qualifyFromBodyLocals(objName, body) || qualifyFromAncestorBlocks(objName, body)))))
        for(verbObjectDef* v : languageService.verbs)
            if(v->name == verbName && !v->i6name.empty()
               && any_of(v->members.begin(), v->members.end(), [&](typeMember* m){ return m->name == methName; })){ ownVerb = v; break; }
    if(ownVerb != nullptr){ objName = (globalQualified ? "::" : "") + ownVerb->name; if(explicitCast.empty()) actualType = objType = "verb"; }
    else if(func != nullptr && !receiverIsVerb){
        string qualified = qualifyIdentifier(objName, func, body, methName);
        if(!qualified.empty()) objName = qualified;
    } else if(func == nullptr){
        // Outside a routine (a global's initializer) only the `#using` imports can rename it: an
        // `alias` or `auto` member names the object it stands for, any other member is reached by path.
        if(languageService.findGlobalAs<typeDef>(objName) == nullptr)
        for(objectDef* imp : usingObjectImports)
            for(typeMember* m : imp->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd && vd->name == objName && !vd->isPrePassStub){
                    string target = vd->declaredExpressionValue ? vd->declaredExpressionValue->text() : "";
                    bool namesObject = !target.empty() && languageService.findGlobalAs<objectDef>(target) != nullptr;
                    objName = namesObject ? target : imp->dName() + "." + objName;
                    goto qualifiedFromUsing;
                }
        qualifiedFromUsing:;
    }
    if(explicitCast.empty() && redirectThroughGetter(objName, objType, methName)){ actualType = objType; rawObjName = objName; }
    // The receiver may be a classDef or an objectDef (each unclassed objectDef
    // is its own type). Both have addressable methods; bindMethodCall handles
    // either via its Step-2 objectDef-member fallback.
    typeDef& objTd = languageService.getType(objType);
    bool opaqueRecv = (dynamic_cast<classDef*>(&objTd) == nullptr && dynamic_cast<objectDef*>(&objTd) == nullptr);
    // An enum/bnum with emitter methods dispatches through its companion emitter
    // class (getDispatchClass returns it) — treat as non-opaque so the emitter
    // method resolves and inlines with $self = the value.
    if(opaqueRecv){ if(auto* ed = dynamic_cast<enumDef*>(&objTd)) if(ed->companion) opaqueRecv = false; }
    // Generic specialization fallback: a templated receiver name like
    // "array<int>" (from a parametric param) isn't a registered type, but
    // its base ("array") is. Treat as non-opaque if the base resolves to
    // a class — bindMethodCall threads the element type for substitution.
    if(opaqueRecv){
        auto lt = objType.find('<');
        if(lt != string::npos && lt > 0){
            typeDef& baseTd = languageService.getType(objType.substr(0, lt));
            if(dynamic_cast<classDef*>(&baseTd) != nullptr) opaqueRecv = false;
        }
    }
    // Computed message send: `obj.p(args)` where `p` is not a method but IS a
    // property-valued name in scope. Emits I6's `obj.(p)(args)`, which resolves
    // the property at runtime and binds self to the receiver — the same send the
    // BLR reaches for inside #i6 blocks. Only considered when the name is not a
    // real method, so it never shadows one.
    bool isRealMember = false;
    if(classDef* rc = getDispatchClass(objType))
        isRealMember = findMemberInHierarchy(rc, [&](typeMember* m){ return m->name == methName; }) != nullptr;
    if(!isRealMember)
        if(auto* od = languageService.findObjectType(objType))
            for(typeMember* m : od->members)
                if(m->name == methName){ isRealMember = true; break; }
    if(!isRealMember && isPropertyValuedLocal(methName, func, body)){
        string propExpr = qualifyIdentifier(methName, func, body);
        ParsedArgList cpal = parseCallArgList(func, body);
        string argText;
        for(size_t i = 0; i < cpal.args.size(); i++)
            argText += (i ? ", " : "") + cpal.args[i]->text();
        expr->tokens.push_back(objName + ".(" + propExpr + ")(" + argText + ")");
        expr->resolvedType = "var";   // the property's routine is untyped here
        cur = exprNext(st);
        return ExprStep::Continue;
    }
    if(opaqueRecv && !looseIdentifierMode)
        parsingError(format("Type '{0}' has no methods", objType));

    // parse argument list (handles named args via name: value syntax; and a bare
    // `{ … }` arg inferred from the method's parameter type — §6.2.1)
    // A receiver that is itself an object (`bgl.ui.statusBar` → `_bglStatus`) resolves to its class,
    // but its methods may be declared on the object; look there too.
    vector<functionDef*> argCandidates = collectMethodCandidates(objType, methName);
    if(argCandidates.empty() && languageService.findObjectType(objName) != nullptr)
        argCandidates = collectMethodCandidates(objName, methName);
    string hintElem = isWordArrayType(objType) ? resolveArrayElementType(rawObjName, func, body) : string();
    if(hintElem.empty() && isWordArrayType(objType) && rawObjName.find('.') != string::npos)
        hintElem = resolveArrayElementTypeDotted(rawObjName.substr(0, rawObjName.rfind('.')), rawObjName.substr(rawObjName.rfind('.') + 1), func, body);
    ParsedArgList pal = parseCallArgList(func, body, braceArgHints(argCandidates, hintElem));
    rejectInterpolatedArgsInExpression(pal, methName);
    vector<expression*>& callArgs = pal.args;
    functionDef* method = nullptr;
    if(opaqueRecv){
        // Loose mode: receiver opaque (likely an I6 symbol). Skip method
        // binding; emit verbatim as objName.methName(args), result type var.
        string call = objName + "." + methName + "(";
        for(size_t i = 0; i < callArgs.size(); i++){
            if(i > 0) call += ", ";
            call += callArgs[i]->text();
        }
        call += ")";
        expr->tokens.push_back(call);
        expr->resolvedType = "var";
    } else {
    // Element-type binding for generic receivers (array<T>, etc.):
    // look up the receiver path's element type so the method-resolver
    // can substitute T → concrete type. Use the bare (pre-qualify) name
    // first so a member array resolves (qualifyIdentifier rewrote objName
    // to "self.<prop>", which the element-type lookup doesn't strip).
    // `Policy.step(10)`: a static func<> member is a global holding a routine, called directly.
    if(classDef* sc = getDispatchClass(objType))
        if(auto* sv = dynamic_cast<variableDeclaration*>(findMemberInHierarchy(sc, [&](typeMember* m){
               auto* vd = dynamic_cast<variableDeclaration*>(m);
               return vd != nullptr && vd->isStatic && vd->name == methName && vd->type.name.rfind("func", 0) == 0; }))){
            string call = "_bgl_" + sc->declaringClass(sv)->name + "_" + sv->name + "(";
            for(size_t i = 0; i < callArgs.size(); i++) call += (i > 0 ? ", " : "") + callArgs[i]->text();
            expr->tokens.push_back(call + ")");
            vector<string> targs = funcSignatureArgs(sv->type.name);
            expr->resolvedType = targs.empty() ? "var" : targs[0];
            return ExprStep::Advance;
        }
    string recvElemType = resolveArrayElementType(rawObjName, func, body);
    if(recvElemType.empty()) recvElemType = resolveArrayElementType(objName, func, body);
    if(recvElemType.empty() && rawObjName.find('.') != string::npos){   // a member array (`box.slots`)
        size_t ed = rawObjName.rfind('.');
        recvElemType = resolveArrayElementTypeDotted(rawObjName.substr(0, ed), rawObjName.substr(ed + 1), func, body);
    }
    if(recvElemType.empty() && objType == "bytearray") recvElemType = "char";
    method = bindMethodCall(objType, objName, methName,
                                           callArgs, pal.namedArgNames, pal.interpSegmentsPerArg,
                                           recvElemType);
    if(ownVerb != nullptr){ objName = ownVerb->i6name; rawObjName = objName; }   // sent to the verb object

    // `hide` enforcement: is this method hidden on the receiver's static type?
    // objType is cast-aware, so `(Base)obj.m()` checks against Base — the door.
    {   vector<string> argTypeNames;
        for(expression* a : callArgs) argTypeNames.push_back(a->resolvedType);
        enforceHidden(getDispatchClass(objType), methName, "", argTypeNames, objName);
    }

    // A value emitter is NOT callable: `obj.bold` (value, §14.4.5) and `obj.bold()`
    // (zero-arg function) are distinct; parens on a value are an error here too,
    // matching the bare/global-call path.
    if(method->isEmitter && method->isValueEmitter)
        parsingError(format("'{0}' is an emitter value, not a function; use it without parentheses ('{0}', not '{0}()')", methName));

    // Ancestor-qualified dispatch requires a real I6 routine property for `::` to
    // select. An `emitter` method is inlined at the call site — there is no routine
    // to qualify — so `(Base)obj.emitterMethod()` is a compile error (v1).
    if(ancestorDispatchClass != nullptr && method->isEmitter)
        parsingError(format("Ancestor-qualified dispatch '({0}){1}.{2}(...)' is not supported: '{2}' is an emitter method (inlined at the call site), so there is no routine for the ancestor cast to select. Ancestor dispatch works on regular (non-emitter) methods.",
                            explicitCast, rawObjName, methName));

    expr->resolvedType = method->returnType.name;

    // Compute $prop for array method calls in expression context
    string exprPropValue = (isWordArrayType(objType) || objType == "bytearray") ? "0" : "<$prop undefined>";

    // Member (property) WORD array? Detect once for member-aware lowering.
    ArrayReceiver arr = arrayReceiver(objName, rawObjName, objType, func, body);
    const string& mOwner = arr.owner;
    const string& mProp  = arr.prop;
    bool isMemberArr = arr.isMember;

    // Temporaries among the arguments are held until the call returns (holdCallTemporaries).
    vector<string> heldArgs;
    for(expression* a : callArgs) heldArgs.push_back(a->text());
    HeldTemporaries heldTemps;
    {
        vector<pair<string*, string>> operands;
        for(size_t i = 0; i < callArgs.size(); i++) operands.push_back({&heldArgs[i], callArgs[i]->resolvedType});
        heldTemps = holdCallTemporaries(operands);
    }
    string callText;
    if(isMemberArr) rejectRawMemberLengthOp(mOwner, mProp, methName, func, body);
    if(isMemberArr && (methName == "size" || methName == "length")){
        callText = memberArraySizeText(arr, methName, func, body);
        expr->tokens.push_back(callText);
    }
    else if(method->isEmitter){
        if(auto* blk = dynamic_cast<i6Block*>(method->body)){
            // When the receiver is a bare identifier that names an inherited
            // member of the enclosing object (e.g. bare `attributes` from
            // `object`'s `attributeList attributes;`), the implicit owner for
            // $self / $val is `self`, not the receiver name. Check the
            // pre-qualify form because qualifyIdentifier may have already
            // rewritten the bare identifier to "self.<name>" upstream.
            // Member array: $self is the owning object, $prop the property
            // name — the dual-form _bglArray utility branches on metaclass($self)
            // to use the property convention. Globals/locals pass the array +
            // a 0 sentinel (set via exprPropValue below).
            string selfHost = isMemberArr ? mOwner
                             : (isInheritedObjectMember(rawObjName, func, body) ? string("self") : objName);
            if(isMemberArr) exprPropValue = mProp;
            // The receiver is the array a <linq> step returned, held by nothing: the next step frees it once read.
            if(!isMemberArr && isWordArrayType(objType) && selfHost.rfind("_bglArray.", 0) == 0 && languageService.linqInUse)
                exprPropValue = "-1";
            emitterBindings mb; mb.self = selfHost; mb.val = selfHost;
            // $host — the object a PROXY member is accessed on: the receiver with its
            // trailing `.member` stripped. For `container.children.length()` the method
            // receiver ($self/$val) is `container.children`, but a storageless world-tree
            // proxy (childrenProp) needs the host `container`. Emitters opt in by naming
            // $host; ordinary methods (which want the full receiver) are unaffected.
            mb.host = selfHost.substr(0, selfHost.rfind('.') == string::npos ? selfHost.size()
                                                                            : selfHost.rfind('.'));
            // $class — declared type of the receiver. Ignores multiple inheritance:
            // resolves to the variable's static type, not the type that owns the
            // inherited emitter. Useful for emitters that emit class-message I6
            // (e.g. `$class.copy($self, $src)` from the bglAllocated mixin).
            if(auto* recvCls = getDispatchClass(objType)) mb.cls = recvCls->i6Name();
            mb.selfType = objType;
            mb.fn = method;
            mb.args = heldArgs;
            for(expression* a : callArgs) mb.argTypes.push_back(a->resolvedType);
            // $prop is substituted after the parameters, so an emitter with a `prop`
            // parameter (e.g. `provides(property prop)`) wins over the fallback.
            mb.prop = exprPropValue;
            // One substitution covers every $opref(<op>) in the body. Outside
            // array emitters there is no element type, so $opref resolves
            // against the receiver's own type instead.
            mb.elemType = recvElemType.empty() ? objType : recvElemType;
            mb.elemContext = method->name;
            mb.trim = emitterTrim::ws;
            string b = expandEmitterBody(blk, mb);
            if(heldTemps.any()) b = wrapHeldCall(heldTemps, b);
            callText = b;
            expr->tokens.push_back(b);
        }
    } else if(method->isStatic){
        // Static method: no receiver/self, so it emits as a file-scope routine
        // (emitStaticClassRoutines) named _bgl_<class>_<method>. A message-send
        // `obj.method(args)` would look up a non-existent property and fail in I6
        // ("No such constant as <method>"); emit a direct call to that routine.
        classDef* stCls = getDispatchClass(objType);
        if(stCls != nullptr)   // through a subclass name: the routine is the declaring class's
            if(classDef* owner = stCls->declaringClass(method)) stCls = owner;
        string call = i6Emitter::staticRoutineName(stCls, method) + "(";
        for(size_t i = 0; i < heldArgs.size(); i++){
            if(i > 0) call += ", ";
            call += heldArgs[i];
        }
        call += ")";
        if(heldTemps.any()) call = wrapHeldCall(heldTemps, call);
        callText = call;
        expr->tokens.push_back(call);
    } else {
        // non-emitter: emit verbatim as obj.method(args) (or obj.<mangled>(args)
        // if this method is part of an overload set). When the receiver was an
        // explicit ancestor cast, qualify the send with the ancestor's I6 class
        // (`obj.Base::method(args)`) so I6 selects THAT class's routine statically,
        // bypassing any override on the receiver's actual type — the super-call /
        // ancestor-version-dispatch idiom.
        // A func-typed data member has no i6name of its own; its property may be renamed (`_m_fits`).
        string callName = method->i6name.empty() ? memberI6Name(objType, methName) : method->i6name;
        if(method->i6name.empty() && callName == methName) callName = methodCallName(method, methName);   // one declared further down
        string dispatch = ancestorDispatchClass != nullptr
                        ? ancestorDispatchClass->i6Name() + "::" + callName
                        : callName;
        string call = objName + "." + dispatch + "(";
        for(size_t i = 0; i < heldArgs.size(); i++){
            if(i > 0) call += ", ";
            call += heldArgs[i];
        }
        call += ")";
        if(heldTemps.any()) call = wrapHeldCall(heldTemps, call);
        callText = call;
        expr->tokens.push_back(call);
    }

    // First-call + subscript: `arr.method(args)[i]` dispatches operator[] on
    // the method's return type. Same shape as the chained-call subscript path
    // below; without this, the raw `[i]` falls through and I6 chokes on
    // method-call-result indexing.
    if(!callText.empty()) subscriptCallResult(expr, expr->tokens.size() - 1, func, body);
    } // end of typed-receiver else
    if(!lastRefBacks.empty() && !expr->tokens.empty()) expr->tokens.back() = wrapRefBacks(expr->tokens.back());
    // A cast whose target can't be a receiver type applies to what the call RETURNED — the last
    // call of a chain.
    // `(T)a.f()?.g`: the `?.` chain that follows takes the cast for its result.
    if(!resultCast.empty() && file.peekToken().is("?.")){ castType = resultCast; resultCast.clear(); }
    if(!resultCast.empty() && file.peekToken().is(token::period)){
        st.chainResultCast = resultCast; resultCast.clear();
    }
    if(!resultCast.empty() && !expr->tokens.empty()){
        expr->tokens.back() = applyCastConversion(expr->tokens.back(), expr->resolvedType, resultCast);
        expr->resolvedType = resultCast;
    }
    return ExprStep::Advance;
}

// `obj.prop[i]` — subscript a property array.
bglParser::ExprStep bglParser::parseExprMemberSubscript(ExprParseState& st, token& member, token& afterMember){
    expression* expr = st.expr;
    token& cur = st.cur;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    // Property array subscript in expression: obj.prop[i]
    string objName = cur.value;
    string propName = member.value;
    // `Calc.table[i]`: a static array is a global one.
    string g = staticMemberGlobal(objName + "." + propName);
    // `tagA.marks[0]`: a static array reached through an instance is still its class's global.
    if(g.empty() && func != nullptr)
        if(classDef* hc = getDispatchClass(objName.find('.') == string::npos ? resolveIdentifierType(objName, func, body)
                                                                             : resolvePathType(objName, func, body)))
            if(typeMember* sm = findMemberInHierarchy(hc, [&](typeMember* m){
                    auto* vd = dynamic_cast<arrayDeclaration*>(m); return vd && vd->isStatic && vd->name == propName; }))
                g = "_bgl_" + hc->declaringClass(sm)->name + "_" + propName;
    // `Box.last[1]`: a static that isn't an array (a stringObj) subscripted by its class's `operator []`.
    if(!g.empty() && languageService.findGlobalAs<arrayDeclaration>(g) == nullptr)
        if(auto* gv = languageService.findGlobalAs<variableDeclaration>(g))
            if(classDef* vc = getDispatchClass(gv->type.name))
                if(auto* gm = dynamic_cast<functionDef*>(findMemberInHierarchy(vc, [](typeMember* m){
                       auto* f = dynamic_cast<functionDef*>(m); return f && f->name == "[]" && f->params.size() == 1; }))){
                    expression* idx = parseExpression(file.getToken(), {token::bracketClose}, func, body);
                    string text;
                    if(auto* blk = gm->isEmitter ? dynamic_cast<i6Block*>(gm->body) : nullptr){
                        emitterBindings sb; sb.self = g; sb.val = g; sb.prop = "0"; sb.fn = gm; sb.args.push_back(idx->text());
                        text = expandEmitterBody(blk, sb);
                    } else {
                        if(gm->i6name.empty()) gm->i6name = mangleOperatorName(gm->name);
                        text = g + "." + gm->i6name + "(" + idx->text() + ")";
                    }
                    expr->tokens.push_back(text);
                    if(expr->resolvedType.empty()) expr->resolvedType = gm->returnType.name;
                    return ExprStep::Advance;
                }
    if(!g.empty()){
        auto* gad = languageService.findGlobalAs<arrayDeclaration>(g);
        classDef* gCls = gad != nullptr ? getDispatchClass(gad->isByteArray ? "bytearray" : "array") : nullptr;
        expression* idx = parseExpression(file.getToken(), {token::bracketClose}, func, body);
        string el = gad != nullptr ? (gad->isByteArray ? "char" : gad->elementType) : "";
        functionDef* gm = gCls != nullptr ? findArraySubscriptOp(gCls, el, /*isWrite=*/false) : nullptr;
        auto* blk = gm != nullptr ? dynamic_cast<i6Block*>(gm->body) : nullptr;
        if(blk == nullptr) parsingError(format("Subscript on '{0}.{1}': no readable operator[]", objName, propName));
        emitterBindings sb; sb.self = g; sb.val = g; sb.prop = "0"; sb.fn = gm; sb.args.push_back(idx->text());
        string elemText = expandEmitterBody(blk, sb);
        // `Calc.table[1](n)`: an element that is a function value, called — parenthesized.
        if(file.peekToken().is(token::parenOpen) && el.rfind("func", 0) == 0){
            file.getToken(token::parenOpen);
            ParsedArgList pal = parseCallArgList(func, body);
            rejectInterpolatedArgsInExpression(pal, "call");
            string call = "(" + elemText + ")(";
            for(size_t i = 0; i < pal.args.size(); i++) call += (i > 0 ? ", " : "") + pal.args[i]->text();
            elemText = call + ")";
            size_t lt = el.find('<'), comma = el.find(',', lt), gt = el.rfind('>');
            el = lt == string::npos ? "var" : (comma != string::npos && comma < gt) ? el.substr(lt+1, comma-lt-1) : el.substr(lt+1, gt-lt-1);
        }
        // `Holder.tags[0].weight`: the element is the slot's own instance, read as one operand.
        if(file.peekToken().is(token::period)) elemText = "(" + valueSlotText(elemText, el) + ")";
        expr->tokens.push_back(elemText);
        bool chains = file.peekToken().is(token::bracketOpen) && expr->tokens.size() == 1;   // `Grid.cells[r][c]`
        if(expr->resolvedType.empty() || chains) expr->resolvedType = el;
        if(chains) chainSubscripts(expr, elemText, func, body);
        return ExprStep::Advance;
    }
    string propType = resolvePathType(objName + "." + propName, func, body);
    classDef* arrCls = getDispatchClass(propType);
    expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, body);
    string elemType = resolveArrayElementTypeDotted(objName, propName, func, body);
    if(elemType.empty() && propType == "bytearray") elemType = "char";
    functionDef* getMethod = nullptr;
    if(arrCls != nullptr && !elemType.empty())
        getMethod = findArraySubscriptOp(arrCls, elemType, /*isWrite=*/false);
    // A member whose class declares its own operator[] (an owned value-class instance) is sent it.
    if(getMethod == nullptr && elemType.empty() && arrCls != nullptr && !isWordArrayType(propType))
        if(auto* g = dynamic_cast<functionDef*>(findMemberInHierarchy(arrCls, [](typeMember* m){
                auto* fd = dynamic_cast<functionDef*>(m);
                return fd != nullptr && fd->name == "[]" && fd->params.size() == 1; }))){
            string owner = func != nullptr ? qualifyIdentifier(objName, func, body) : objName;
            string recv = (owner.empty() ? objName : owner) + "." + propertyI6Name(propName);
            if(auto* blk = g->isEmitter ? dynamic_cast<i6Block*>(g->body) : nullptr){
                emitterBindings mb; mb.self = recv; mb.val = recv;
                mb.fn = g; mb.args.push_back(indexExpr->text());
                expr->tokens.push_back(expandEmitterBody(blk, mb));
            } else {
                string routine = g->i6name.empty() ? mangleOperatorName(g->name) : g->i6name;
                expr->tokens.push_back(recv + "." + routine + "(" + indexExpr->text() + ")");
            }
            expr->resolvedType = g->returnType.name;
            return ExprStep::Advance;
        }
    if(getMethod == nullptr) {
        if(elemType.empty())
            parsingError(format("Subscript on '{0}.{1}': property is not a declared array", objName, propName));
        parsingError(format("No operator[] returning '{0}' on type '{1}'", typeDisplayName(elemType), typeDisplayName(propType)));
    }
    // As the expression's first operand the element is its type, whatever reading the owner path left there.
    if(expr->resolvedType.empty() || expr->tokens.empty()) expr->resolvedType = getMethod->returnType.name;
    // A word-array member is INLINE property data: it is read as
    // `obj.&prop-->(i)`, with no count slot, exactly as the write path and
    // the bare-name read already do. Running the global emitter body here
    // instead produced `obj-->(i+1)` — indexing off the OBJECT, which
    // returns unrelated memory rather than the element.
    if(isWordArrayType(propType) && !memberArrayIsRef(objName, propName, func, body)){
        string owner = func != nullptr ? qualifyIdentifier(objName, func, body) : objName;
        if(owner.empty()) owner = objName;
        rejectConstantIndexOutside(dynamic_cast<arrayDeclaration*>(findAssignedDeclaration(objName + "." + propName, func, body)),
                                   indexExpr, member.originalValue);
        expr->tokens.push_back(valueSlotText(checkedMemberSlot(owner, propertyI6Name(propName), indexExpr->text(), false,
                                                               memberArrayIsTracked(objName, propName, func, body)), elemType));
        if(file.peekToken().is(token::bracketOpen) && expr->tokens.size() == 1){   // a row, or an element with `[]`
            expr->resolvedType = elemType;
            string text = expr->tokens.back();
            chainSubscripts(expr, text, func, body);
        }
    }
    else if(getMethod->isEmitter)
        if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)) {
            // The receiver is the property READ `obj.prop` whenever the
            // property holds a POINTER rather than inline data: a byte-array
            // member (separate backing array), and a `ref` member (an array
            // owned elsewhere). Binding to bare `obj` would index the object.
            bool holdsPointer = (propType == "bytearray")
                             || memberArrayIsRef(objName, propName, func, body);
            string owner = func != nullptr ? qualifyIdentifier(objName, func, body) : objName;   // a lambda reads its capture
            if(owner.empty()) owner = objName;
            string recv = holdsPointer ? (objectI6Name(owner) + "." + propertyI6Name(propName)) : objName;
            emitterBindings mb; mb.self = recv; mb.val = recv; mb.prop = propName;
            mb.fn = getMethod; mb.args.push_back(indexExpr->text());
            string text = expandEmitterBody(blk, mb);
            if(isWordArrayType(propType)) text = valueSlotText(text, elemType);
            expr->tokens.push_back(text);
            if(file.peekToken().is(token::bracketOpen) && expr->tokens.size() == 1){   // a row, or an element with `[]`
                expr->resolvedType = elemType;
                chainSubscripts(expr, text, func, body);
            }
        }
    // `fx.steps[1](3)`: the element is a function value, called — parenthesized, as for a global array.
    if(!expr->tokens.empty() && file.peekToken().is(token::parenOpen) && elemType.rfind("func", 0) == 0){
        file.getToken(token::parenOpen);
        ParsedArgList pal = parseCallArgList(func, body);
        rejectInterpolatedArgsInExpression(pal, "call");
        string call = "(" + expr->tokens.back() + ")(";
        for(size_t i = 0; i < pal.args.size(); i++) call += (i > 0 ? ", " : "") + pal.args[i]->text();
        expr->tokens.back() = call + ")";
        size_t lt = elemType.find('<'), comma = elemType.find(',', lt), gt = elemType.rfind('>');
        expr->resolvedType = lt == string::npos ? "var"
            : (comma != string::npos && comma < gt) ? elemType.substr(lt+1, comma-lt-1) : elemType.substr(lt+1, gt-lt-1);
    }
    // `(Valve)rack.slots[0]`: a cast written before the path applies to the element it ends at.
    if(!st.castType.empty() && !expr->tokens.empty() && !file.peekToken().is(token::period) && !file.peekToken().is(token::bracketOpen)){
        expr->tokens.back() = applyCastConversion(expr->tokens.back(), expr->resolvedType.empty() ? elemType : expr->resolvedType, st.castType);
        expr->resolvedType = st.castType;
        st.castType = "";
    }
    return ExprStep::Advance;
}

// The property access itself, once enum / static / value-emitter / alias-member
// resolution has been ruled out for `obj.member`.
bglParser::ExprStep bglParser::parseExprMemberPropertyRead(ExprParseState& st, token& member){
    expression* expr = st.expr;
    token& cur = st.cur;
    string& castType = st.castType;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    // Object property access: emit as obj.prop. Route obj through
    // qualifyIdentifier so a class-typed local with synthesized
    // backing (isClassLocalWithBacking) emits as the backing's
    // i6name instead of the bare local slot — otherwise the read
    // would go through the int slot (= `nothing`) and miss the
    // backing object's properties entirely.
    string propType = resolvePathType(cur.value + "." + member.value, func, body);
    string objText = func != nullptr ? qualifyIdentifier(cur.value, func, body, member.value) : cur.value;
    string computedProp;   // set when `obj.p` is a computed property access
    string recvType = !objText.empty() ? resolveIdentifierType(cur.value, func, body) : "";
    // In an island, a name Inform 6 declares is an object Beguile can't see: a member every object has
    // (`parent`, `child`, …) is read as that object's.
    if(looseIdentifierMode && propType.empty() && (recvType.empty() || recvType == "var"))
        if(classDef* oc = getDispatchClass("object"))
            if(typeMember* om = findMemberInHierarchy(oc, [&](typeMember* m){ return m->name == member.value; })){
                recvType = "object";
                if(auto* vd = dynamic_cast<variableDeclaration*>(om)) propType = vd->type.name;
            }
    // `((T)obj).member` reads T's member; `(T)obj.member` casts the value read (below).
    string receiverCastType;
    if(st.receiverCast){
        receiverCastType = castType;
        castType = st.chainCast;   // `(int)((T)obj).member` casts the member's value
        st.chainCast.clear();
        st.receiverCast = false;
        recvType = receiverCastType;
        propType.clear();
    }
    // The path walk misses a member of a generic receiver reached through a parameter or a
    // subscript (`data.length`, `grid[0].length`); the receiver's type still declares it.
    if((member.value == "size" || member.value == "length") && recvType.rfind("rawarray<", 0) == 0)
        parsingError(format("'{0}' is unavailable on rawArray '{1}': a rawArray is a bare I6 word "
            "pointer with no length header, so its size isn't known at runtime. Pass the length "
            "explicitly (e.g. as a separate parameter) instead.", member.value, cur.value));
    if(propType.empty() && !recvType.empty())
        if(classDef* rc = getDispatchClass(recvType))
            if(auto* vd = dynamic_cast<variableDeclaration*>(findMemberInHierarchy(rc, [&](typeMember* m){
                    auto* v = dynamic_cast<variableDeclaration*>(m); return v && v->name == member.value; })))
                propType = vd->type.name;
    bool castGoesOn = st.prefetched ? st.prefetched->is(token::period) : file.peekToken().is(token::period);
    if(!objText.empty() && (castType.empty() || !castGoesOn)){
        string veRet;
        string veText = expandMemberValueEmitter(objText, cur.value, recvType, member.value, func, body, veRet);
        // `(Chip)self.child`: the cast applies to the value the emitter reads (a `?.` chain after it takes it instead).
        bool chainNext = st.prefetched ? st.prefetched->is("?.") : file.peekToken().is("?.");
        if(!veText.empty() && !castType.empty() && !chainNext){
            expr->tokens.push_back(applyCastConversion(veText, veRet, castType));
            expr->resolvedType = castType;
            castType = "";
            return ExprStep::Advance;
        }
        if(!veText.empty()){
            expr->tokens.push_back(veText);
            if(expr->resolvedType.empty()) expr->resolvedType = veRet;
            expr->emitterSelf = cur.value;
            return ExprStep::Advance;
        }
    }
    // `Calc.twice` with no call: a static method is a routine of its own, so it is a function value —
    // in a routine or in a file-scope initializer alike.
    // An object's method needs its object to run, which a func<> value doesn't carry.
    if(propType.empty() && !looseIdentifierMode && func != nullptr){
        string rt = resolveIdentifierType(cur.value, func, body);
        auto isMethod = [&](typeMember* m){
            auto* fd = dynamic_cast<functionDef*>(m);
            return fd != nullptr && fd->name == member.value && !fd->isStatic; };
        functionDef* mm = nullptr;
        if(objectDef* od = languageService.findObjectType(rt))
            for(typeMember* m : od->members) if(isMethod(m)){ mm = static_cast<functionDef*>(m); break; }
        if(mm == nullptr)
            if(classDef* rc = getDispatchClass(rt)) mm = dynamic_cast<functionDef*>(findMemberInHierarchy(rc, isMethod));
        if(mm != nullptr){
                string params, args;
                for(size_t i = 0; i < mm->params.size(); i++){
                    params += (i ? ", " : "") + typeDisplayName(mm->params[i]->type.name) + " " + mm->params[i]->name;
                    args   += (i ? ", " : "") + mm->params[i]->name;
                }
                string shown = cur.originalValue.empty() ? cur.value : cur.originalValue;
                parsingError(format("'{0}.{1}' is a method, which needs its object to run, so it isn't a function "
                    "value. Pass a lambda that calls it: `({2}) => {0}.{1}({3})`.", shown, mm->dName(), params, args));
        }
    }
    if(propType.empty() && !looseIdentifierMode)
        if(classDef* sc = getDispatchClass(cur.value))
            if(auto* sm = dynamic_cast<functionDef*>(findMemberInHierarchy(sc, [&](typeMember* m){
                   auto* fd = dynamic_cast<functionDef*>(m);
                   return fd != nullptr && fd->name == member.value && fd->isStatic && !fd->isEmitter; }))){
                classDef* owner = sc->declaringClass(sm);
                // An overloaded static's name doesn't say which routine it is.
                int overloads = 0;
                findMemberInHierarchy(sc, [&](typeMember* m){
                    auto* fd = dynamic_cast<functionDef*>(m);
                    if(fd != nullptr && fd->name == member.value && fd->isStatic && !fd->isEmitter && !fd->isPrePassStub) overloads++;
                    return false; });
                if(overloads > 1)
                    parsingError(format("'{0}.{1}' names {2} overloads, so it cannot be used as a bare "
                        "function reference — only a call can say which one is meant. Wrap the overload "
                        "you want in a function of its own and reference that.", cur.originalValue.empty() ? cur.value : cur.originalValue, sm->dName(), overloads));
                expr->tokens.push_back(i6Emitter::staticRoutineName(owner ? owner : sc, sm));
                string t = "func<" + (sm->returnType.name.empty() ? string("void") : sm->returnType.name);
                for(paramDef* p : sm->params) t += "," + p->type.name;
                expr->resolvedType = t + ">";
                return ExprStep::Advance;
            }
    // A verb's own member (`::Probe.uses`, or `Probe.uses` where nothing nearer has the name) is the verb object's.
    if(propType.empty() && castType.empty()){
        bool global = cur.value.rfind("::", 0) == 0;
        string head = global ? cur.value.substr(2) : cur.value;
        bool hidden = !global && func != nullptr && (qualifyFromParams(head, func) || qualifyFromBodyLocals(head, body)
                                                     || qualifyFromAncestorBlocks(head, body));
        if(!hidden)
            for(verbObjectDef* v : languageService.verbs)
                if(v->name == head && !v->i6name.empty())
                    for(typeMember* m : v->members)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd && vd->name == member.value && !vd->isStatic){
                            expr->tokens.push_back(v->i6name + "." + (vd->i6name.empty() ? vd->dName() : vd->i6name));
                            if(expr->resolvedType.empty()) expr->resolvedType = vd->type.name;
                            return ExprStep::Advance;
                        }
    }
    if(objText.empty()){
        // Receiver didn't resolve. Outside loose mode, a dotted access on an
        // unknown object is an error, just like a bare undeclared identifier —
        // Beguile does not pass properties through on objects it can't see.
        // Register the object (`extern object X;`) or declare it so the access
        // can dispatch/type-check. Loose mode (#bgl islands) and global scope
        // (func == nullptr) keep the verbatim-passthrough for I6 interop.
        if(func != nullptr && !looseIdentifierMode
           && resolveIdentifierType(cur.value, func, body).empty())
            parsingError(format("Undeclared identifier '{0}'", cur.value));
        objText = cur.value;
    }
    else if(propType.empty() && func != nullptr && !looseIdentifierMode){
        // Receiver is known but the property isn't a declared member of its
        // type. Beguile does not pass properties through on a name it hasn't
        // seen — a free-standing `property foo;` only registers the identifier
        // for passing as a value, it does NOT grant `obj.foo` access. Declare
        // the property as a member of the object (or its extern class):
        // `extern class Thing { int foo; }`, or extend a base such as
        // `object` in a binding. Loose mode (#bgl islands) still passes through.
        // Computed property access: `obj.p` where `p` is not a member of
        // obj's type but IS a property-valued name in scope (from
        // `(property)x`, or a `property`/`var` parameter). It names the
        // property to read rather than one called "p". I6's `.` already
        // resolves a local on its right-hand side exactly this way — the
        // mangled local emits straight through — and because locals carry a
        // mangled name it can never be captured by a real property of the
        // same spelling.
        if(isPropertyValuedLocal(member.value, func, body))
            computedProp = qualifyIdentifier(member.value, func, body);
        else {
            string recvType = resolveIdentifierType(cur.value, func, body);
            // `aim.lvl` where `aim` is an accessor: a member the accessor lacks is read off what its getter reads.
            if(functionDef* g = nativeGetterOf(recvType))
                if(classDef* gc = getDispatchClass(g->returnType.name);
                   gc != nullptr && findMemberInHierarchy(gc, [&](typeMember* m){ return m->name == member.value; }) != nullptr){
                    expr->tokens.push_back(objText + "." + g->i6name + "()");
                    expr->resolvedType = g->returnType.name;
                    token after = exprNext(st);
                    return after.is(token::parenOpen) ? parseExprDotChainCall(st, member, after)
                                                      : parseExprDotChainRead(st, member, after);
                }
            parsingError(format("'{0}' is not a declared property of '{1}' (type '{2}'). "
                "Declare it as a member so the access type-checks.",
                member.value, cur.value, recvType));
        }
    }
    // An object's name is part of its I6 header, not a property: I6 can print it but not hand it back.
    if(member.value == "instancename" && !propType.empty() && func != nullptr)
        parsingError("'instanceName' can't be read: it is the object's I6 header name, which can only be "
                     "printed. Use print(obj).");
    // A computed access emits I6's parenthesised form, `obj.(expr)`. The bare
    // `obj.p` would work in I6 too, but the emitter renames a local whose name
    // matches a dotted-access name (maybeRename, to keep locals from being
    // shadowed by properties) — so the bare spelling would rename the very
    // local being referenced out of the way. Inside parentheses the name is an
    // ordinary expression token, so it renames consistently or not at all.
    // `hide` enforcement (read): a WHOLE-member hide (`hide height;`) blocks
    // reads too. queriedOp="" matches only whole-member entries — an
    // operator-only hide (`hide height.operator =;`) leaves the read intact.
    // A receiver cast makes `((Base)obj).member` resolve against Base (the door).
    {   string hideRecvType = !receiverCastType.empty() ? receiverCastType
                             : resolveIdentifierType(cur.value, func, body);
        enforceHidden(getDispatchClass(hideRecvType), member.value, "", {}, cur.value);
    }
    string memberOwnerType = !receiverCastType.empty() ? receiverCastType : resolveIdentifierType(cur.value, func, body, member.value);
    string accessText = computedProp.empty()
        ? objText + "." + memberI6Name(memberOwnerType, member.value)
        : objText + ".(" + computedProp + ")";
    // Property-class read: if the member's declared type is a property-class
    // (owner-based operator() read emitter, e.g. parentProp → parent($self)),
    // dispatch the read through it instead of emitting a raw property read.
    // Value-conversion operator()s ($val-based) are excluded, so int/string/
    // etc. member reads are unaffected. The value re-types to the emitter's
    // return type so downstream comparisons/casts operate on the read result.
    {
        string arrObj = objText;
        ArrayReceiver arr = arrayReceiver(arrObj, cur.value, recvType, func, body);
        string readRet;
        string readText;
        if(arr.isMember && (member.value == "size" || member.value == "length") && isPropertyClassType(propType)){
            readText = memberArraySizeText(arr, member.value, func, body);
            readRet = "int";
        } else if(i6ExprMemberArrays.count(cur.value) && (member.value == "size" || member.value == "length")){
            readText = i6ExprMemberSizeText(arrObj, member.value, recvType);
            readRet = "int";
        } else
            readText = applyPropertyClassRead(arrObj, propType, readRet, isWordArrayType(recvType) ? &arr : nullptr);
        if(!readText.empty()){ accessText = readText; if(!readRet.empty()) propType = readRet; }
    }
    // Regular-method (non-emitter) operator() getter: if the emitter read above
    // didn't rewrite it and the member's type declares a Beguile-method conversion
    // operator, fire it on this read — `<obj>.<member>._opconv()` — so a native
    // getter "works as intended" on a bare read, not only at an explicit cast
    // (which the castType block below still handles). This is a READ site;
    // assignment targets are parsed by the statement parser (→ operator=), and
    // explicit conversions (cast-only) are excluded.
    // ... unless the path goes on (`self.width._method`): that reads a member of the accessor itself.
    bool readGoesOn = pathGoesOnToOwnMember(st, propType);
    // The member as I6 names it (`_m_cap` for a member that shares a library routine's name).
    bool plainMember = computedProp.empty()
        && accessText == objText + "." + memberI6Name(memberOwnerType, member.value);
    // A `ref` binds to the path's last member itself; a member before it is read as usual (its getter runs).
    bool pathContinues = st.prefetched ? st.prefetched->is(token::period) : file.peekToken().is(token::period);
    if(castType.empty() && !readGoesOn && plainMember && (!readingForBind || pathContinues) && !nextOperatorIsOwn(st, propType))
        if(functionDef* fn = nativeGetterOf(propType)){
            accessText = accessText + "." + fn->i6name + "()";
            propType = fn->returnType.name;
        }
    // Cast precedence: `(T)obj.prop` means cast applies to the property
    // access result, not to the bare `obj`. If castType is set here, run
    // it through applyCastConversion against the property's resolved type.
    // For bit-compatible casts (uint↔int) this is a no-op text change; for
    // float casts it materializes a temp via the conversion emitter.
    // A path that goes on (`(int)o.part.width`) casts its end, so the cast waits for the last member.
    bool pathGoesOn = st.prefetched ? (st.prefetched->is(token::period) || st.prefetched->is("?."))
                                    : (file.peekToken().is(token::period) || file.peekToken().is("?."));
    // A getter returning an enum or bnum is read first, and its value is cast.
    if(!castType.empty() && !pathGoesOn && !readGoesOn && plainMember && !readingForBind)
        if(functionDef* fn = nativeGetterOf(propType); fn != nullptr && fn->returnType.name != castType
           && languageService.findEnum(fn->returnType.name) != nullptr){
            accessText = accessText + "." + fn->i6name + "()";
            propType = fn->returnType.name;
        }
    if(!castType.empty() && !pathGoesOn){
        string newText = applyCastConversion(accessText, propType, castType);
        expr->tokens.push_back(newText);
        expr->resolvedType = castType;
        castType = "";
    } else {
        if((expr->resolvedType.empty() || !castType.empty()) && !propType.empty()) expr->resolvedType = propType;
        expr->tokens.push_back(accessText);
        // A member word array read whole: where it lives, for a for-in that walks it in place.
        expr->memberArrayDecl = nullptr;
        if(plainMember && isWordArrayType(propType))
            if(classDef* oc = languageService.classOf(memberOwnerType))
                if(auto* ad = dynamic_cast<arrayDeclaration*>(findMemberInHierarchy(oc, [&](typeMember* m){
                       return dynamic_cast<arrayDeclaration*>(m) != nullptr && m->name == member.value; })); ad && !heldByPointer(ad)){
                    expr->memberArrayOwner = objText;
                    expr->memberArrayProp = memberI6Name(memberOwnerType, member.value);
                    expr->memberArrayDecl = ad;
                }
    }
    // Capture host text for $self substitution in rvalue property-class
    // operator emitters (parent($self), give $self $attr, etc.). A primitive or value member is
    // its own receiver.
    classDef* propCls = propType.empty() ? nullptr : languageService.classOf(propType);
    if(propCls == nullptr || !(propCls->isPrimitive || propCls->isValue)) expr->emitterSelf = cur.value;
    return ExprStep::Advance;
}

// `obj.member` with no call or subscript after it: enum value, static member, value
// emitter, alias/auto redirect, or a plain property read.
bglParser::ExprStep bglParser::parseExprMemberRead(ExprParseState& st, token& member, token& afterMember){
    expression* expr = st.expr;
    token& cur = st.cur;
    optional<token>& prefetched = st.prefetched;
    string& castType = st.castType;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    prefetched = afterMember;
    // `::Valve.total`: a class at file scope is the class.
    if(cur.value.rfind("::", 0) == 0 && languageService.findClass(cur.value.substr(2)) != nullptr) cur.value = cur.value.substr(2);
    // Distinguish enum-qualified access (EnumType.value → _EnumType_value),
    // static member access (ClassName.staticMember → _bgl_ClassName_memberName),
    // from object/variable property access (obj.prop → obj.prop)
    // Bare #using-imported namespace alias as the chain receiver (e.g. `ui`
    // from `#using bgl`, where bgl declares `alias ui = _bglUi`): `cur.value`
    // is "ui", not a type, so the alias-member redirects below would miss and
    // the chain falls through to a raw property access. Resolve it to the
    // alias's target canonical name so `ui.statusBar.…` redirects exactly like
    // fully-qualified `bgl.ui.statusBar.…`. Gated: a param/local of the same
    // name still wins.
    if(languageService.findClass(cur.value) == nullptr
       && languageService.findObjectType(cur.value) == nullptr){
        bool shadowed = false;
        if(func != nullptr) for(paramDef* p : func->params) if(p->name == cur.value){ shadowed = true; break; }
        if(!shadowed && body != nullptr) for(statement* s : body->statements)
            if(auto* lv = dynamic_cast<variableDeclaration*>(s)) if(lv->name == cur.value){ shadowed = true; break; }
        if(!shadowed && func != nullptr && (qualifyFromCurrentObject(cur.value) || qualifyFromCurrentClass(cur.value)))
            shadowed = true;   // a member outranks an import (§10.4)
        if(!shadowed)
            for(objectDef* imp : usingObjectImports)
                for(typeMember* m : imp->members)
                    if(auto* avd = dynamic_cast<variableDeclaration*>(m))
                        // Only a namespace hook redirects; a plain member (`array<T> Items`) is the receiver itself.
                        if(avd->name == cur.value && (avd->isNamespaceAlias() || avd->declaredExpressionValue != nullptr)
                           && dynamic_cast<arrayDeclaration*>(avd) == nullptr){
                            string tgt = avd->declaredExpressionValue ? avd->declaredExpressionValue->text() : avd->type.name;
                            if(!tgt.empty()
                               && (languageService.findClass(tgt) != nullptr
                                   || languageService.findObjectType(tgt) != nullptr
                                   || (avd->isAlias && languageService.findEnum(tgt) != nullptr)))
                                cur.value = tgt;
                        }
    }
    // An alias of the object being written, named bare in its own code (`t.moves` for `alias t = root.tally;`).
    if(currentObject != nullptr && func != nullptr && languageService.findClass(cur.value) == nullptr
       && !qualifyFromParams(cur.value, func) && !qualifyFromBodyLocals(cur.value, body) && !qualifyFromAncestorBlocks(cur.value, body))
        for(typeMember* m : currentObject->members)
            if(auto* avd = dynamic_cast<variableDeclaration*>(m); avd && avd->name == cur.value && avd->isValueAlias){
                string tgt = avd->declaredExpressionValue ? avd->declaredExpressionValue->text() : avd->type.name;
                if(!tgt.empty() && (languageService.findClass(tgt) != nullptr || languageService.findObjectType(tgt) != nullptr)){ cur.value = tgt; break; }
            }
    // `::Type.member`: the type itself, past a local or parameter of its name.
    if(cur.value.size() > 2 && cur.value.rfind("::", 0) == 0
       && (languageService.findEnum(cur.value.substr(2)) != nullptr || languageService.findClass(cur.value.substr(2)) != nullptr))
        cur.value = cur.value.substr(2);
    bool isEnum = languageService.findEnum(cur.value) != nullptr;
    classDef* maybeCls = getDispatchClass(cur.value);
    bool isStaticAccess = false;
    if(maybeCls != nullptr){
        // A subclass name reaches its bases' statics too: one variable, the declaring class's.
        typeMember* sm = findMemberInHierarchy(maybeCls, [&](typeMember* m){
            auto* vd = dynamic_cast<variableDeclaration*>(m); return vd && vd->isStatic && vd->name == member.value; });
        if(auto* vd = dynamic_cast<variableDeclaration*>(sm)){
            if(expr->resolvedType.empty()){
                expr->resolvedType = vd->type.name;
                // A static array is typed with its elements, as a global array is.
                if(auto* ad = dynamic_cast<arrayDeclaration*>(vd); ad && !ad->isByteArray && !ad->elementType.empty()
                   && vd->type.name.find('<') == string::npos)
                    expr->resolvedType = (ad->isRaw ? "rawarray<" : "array<") + ad->elementType + ">";
            }
            expr->tokens.push_back("_bgl_" + maybeCls->declaringClass(vd)->name + "_" + member.value);
            isStaticAccess = true;
        }
    }
    // Value emitter member: expand body inline. Members can live on either
    // a class (Cls.member) or an object instance (obj.member, e.g. bgl.wordsize),
    // so we try classDef members first, then objectDef members.
    bool isValueEmitterAccess = false;
    string valueEmitterType;
    auto tryInlineValueEmitter = [&](vector<typeMember*>& members) -> bool {
        for(typeMember* m : members)
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == member.value && fd->isValueEmitter && fd->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                        emitterBindings vb; vb.self = objectI6Name(cur.value); vb.val = vb.self; vb.trim = emitterTrim::wsSemi;
                        vb.selfType = cur.value;   // `$i6Expr($self.rows > 3)` types `$self` as the object
                        expr->tokens.push_back(expandEmitterBody(blk, vb));
                        if(expr->resolvedType.empty()) expr->resolvedType = fd->returnType.name;
                        valueEmitterType = fd->returnType.name;
                        return true;
                    }
                    return false;
                }
        return false;
    };
    if(!isStaticAccess && maybeCls != nullptr)
        isValueEmitterAccess = tryInlineValueEmitter(maybeCls->members);
    if(!isStaticAccess && !isValueEmitterAccess){
        objectDef* maybeObj = languageService.findObjectType(cur.value);
        if(maybeObj != nullptr)
            isValueEmitterAccess = tryInlineValueEmitter(maybeObj->members);
    }
    // `(Nd)crate.child`: a cast before the path applies to the value the emitter reads.
    if(isValueEmitterAccess && !castType.empty() && !afterMember.is(token::period) && !afterMember.is("?.") && !expr->tokens.empty()){
        expr->tokens.back() = applyCastConversion(expr->tokens.back(), valueEmitterType, castType);
        expr->resolvedType = castType;
        castType = "";
    }
    // `(Job)roomA.child.sibling`: the path goes on as a chain off the value; the cast waits for its end.
    else if(isValueEmitterAccess && !castType.empty() && afterMember.is(token::period)
            && st.chainResultCast.empty() && !st.receiverCast && !expr->tokens.empty()){
        st.chainResultCast = castType;
        castType = "";
    }
    // Alias member on emitter class: transparent resolution — continue chaining as the alias type
    bool isAliasMember = false;
    if(!isStaticAccess && !isValueEmitterAccess && maybeCls != nullptr && maybeCls->isEmitterClass){
        for(typeMember* m : maybeCls->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->name == member.value){
                    // Alias: set cur to the alias type name, re-enter the loop
                    cur.value = vd->type.name;
                    cur.tokenType = eTokenType::identifier;  // ensure it enters identifier branch
                    expr->resolvedType = "";  // reset so the alias target resolves fresh
                    prefetched = afterMember;
                    isAliasMember = true;
                    break;
                }
    }
    // Alias member on object instance: same redirect as the class case
    if(!isStaticAccess && !isValueEmitterAccess && !isAliasMember){
        objectDef* instObj = languageService.findObjectType(cur.value);
        if(instObj != nullptr){
            for(typeMember* m : instObj->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->isExternal && vd->name == member.value && getDispatchClass(vd->type.name) != nullptr
                       // A namespace member (an alias, `emitter auto asm = bglOpCodes;`, an emitter class);
                       // a typed property of an extern object (`int lvl;`) is storage.
                       && (vd->isNamespaceAlias() || vd->isPrePassStub
                           || (getDispatchClass(vd->type.name)->isEmitterClass && !getDispatchClass(vd->type.name)->isPrimitive))){
                        cur.value = vd->type.name;
                        cur.tokenType = eTokenType::identifier;
                        expr->resolvedType = "";
                        prefetched = afterMember;
                        isAliasMember = true;
                        break;
                    }
        }
    }
    // Nothing class-level claimed the member: a static, a value emitter and an alias member are all
    // reachable through a type NAME, so if none of them matched and the class declares the member
    // per-instance, the access is on the class itself and cannot be meant.
    if(!isStaticAccess && !isValueEmitterAccess && !isAliasMember)
        rejectNonStaticOnTypeName(cur.value, maybeCls, member.value);

    // Auto member on object instance pointing to another object: redirect cur
    // so chained access (e.g. bgl.glulx.method) continues walking. Only applies
    // to namespace-style auto members — those whose initializer names a global
    // object, OR whose declared type is an emitter class. Plain value-typed
    // properties (int x; string s;) fall through to normal property handling.
    if(!isStaticAccess && !isValueEmitterAccess && !isAliasMember){
        objectDef* instObj = languageService.findObjectType(cur.value);
        if(instObj != nullptr){
            for(typeMember* m : instObj->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->name == member.value){
                        // Members whose emitter-class type exposes an operator() READ
                        // emitter (e.g. `parentProp parent = room1`) carry their own read
                        // semantics; never treat them as namespace auto-member redirects,
                        // which would constant-fold the read to the initializer's object.
                        // Fall through to normal property handling so the read emitter
                        // (applyPropertyClassRead) fires instead. Namespace autos (bgl.world,
                        // bgl.glulx — emitter classes WITHOUT a read emitter) still redirect.
                        { classDef* mcd = getDispatchClass(vd->type.name);
                          if(mcd && mcd->isEmitterClass && !mcd->isPrimitive
                             && findMemberInHierarchy(mcd, [this](typeMember* m){ return isPropertyClassReadEmitter(m); }) != nullptr) break; }
                        // An `alias` member redirects to the object it names; an ordinary property
                        // with an object-valued default is storage, and reading it must read the
                        // property. See qualifyDottedViaObjectHead (bglParserTypes.cpp).
                        string initName = vd->isNamespaceAlias() && vd->declaredExpressionValue
                                        ? vd->declaredExpressionValue->text() : "";
                        objectDef* target = nullptr;
                        if(!initName.empty())
                            if(auto* od = languageService.findGlobalAs<objectDef>(initName)) target = od;
                        if(target){
                            cur.value = target->name;
                            cur.tokenType = eTokenType::identifier;
                            expr->resolvedType = "";
                            prefetched = afterMember;
                            isAliasMember = true;
                        } else if(vd->isExternal){
                            // Auto pointing to an emitter class (e.g. emitter auto asm = bglOpCodes).
                            // Gated on isExternal because that's how `auto name = X;` members are
                            // declared (no I6 backing). Without the gate, any plain int/bool field
                            // (whose type is an emitter-class wrapper) would trigger a namespace
                            // redirect and the parser would consume past the expression boundary.
                            // A primitive (`int lvl;` on an extern object) is a stored value, not a namespace.
                            auto* cd = getDispatchClass(vd->type.name);
                            if(cd && cd->isEmitterClass && !cd->isPrimitive){
                                cur.value = vd->type.name;
                                cur.tokenType = eTokenType::identifier;
                                expr->resolvedType = "";
                                prefetched = afterMember;
                                isAliasMember = true;
                            }
                        }
                        break;
                    }
        }
    }
    if(isEnum){
        if(expr->resolvedType.empty()) expr->resolvedType = cur.value;
        // Inline the integer value (extern enums emit the name — I6 keyword).
        auto* ed = languageService.findEnum(cur.value);
        if(ed && !ed->isExternal){
            int v = 0; bool found = false;
            for(enumValueDef* ev : ed->namedValues)
                if(ev->name == member.value){ v = ev->value; found = true; break; }
            if(found) expr->tokens.push_back(to_string(v));
            else expr->tokens.push_back("_" + cur.value + "_" + member.value);
        } else {
            expr->tokens.push_back(member.value);
        }
        // Consume a pending cast on a dotted enum VALUE — `(int)E.member`. Per spec
        // §4.9.1 enum→int needs an explicit cast; enum values inline as integer
        // literals, so this is a pure retype (applyCastConversion leaves the literal
        // text unchanged) that sets the operand's type to the cast target, so e.g.
        // `x == (int)E.member` resolves as int==int. Without it the cast was silently
        // dropped and the comparison failed ("No operator '==' on type 'int'
        // accepting '<enum>'"). Mirrors the property-access cast handling below and the
        // already-working cast on an enum *variable*.
        // A member after the value (`(int)E.south.dRow`) is read first; the cast is the chain's.
        if(!castType.empty() && afterMember.value != "."){
            expr->tokens.back() = applyCastConversion(expr->tokens.back(), expr->resolvedType, castType);
            expr->resolvedType = castType;
            castType = "";
        }
    } else if(isAliasMember) {
        return ExprStep::Continue;  // re-enter loop with cur set to alias type
    } else if(!isStaticAccess && !isValueEmitterAccess) {
        return parseExprMemberPropertyRead(st, member);
    }
    return ExprStep::Advance;
}

// `name.` — read the member name, then dispatch on what follows it.
bglParser::ExprStep bglParser::parseExprMemberAccess(ExprParseState& st, token& next){
    token& cur = st.cur;

    // Bare `parent.children` in a method: the property-class member is read first, as `self.parent` is.
    if(cur.value != "self" && st.func != nullptr && !looseIdentifierMode){
        string t = resolveIdentifierType(cur.value, st.func, st.body, file.peekToken().value);
        classDef* pc = isPropertyClassType(t) ? getDispatchClass(t) : nullptr;
        string m = file.peekToken().value;
        // Only the property class's own members (`children.length`) read off the property itself.
        if(pc != nullptr && none_of(pc->members.begin(), pc->members.end(), [&](typeMember* x){ return x->name == m; })
           && qualifyIdentifier(cur.value, st.func, st.body).rfind("self.", 0) == 0){
            token member = cur;
            cur.value = "self";
            return parseExprSelfMember(st, member, next);
        }
    }
    // A member name after `.` is always an identifier in this position, but the
    // lexer classifies it as `dataType` when it collides (case-insensitively) with
    // a declared type name (e.g. member `color` when a class `Color` exists, or a
    // member literally named `object`). Accept both so such members are reachable.
    token member = file.getToken({eTokenType::identifier, eTokenType::dataType});
    // Read afterMember here so both self and non-self paths share it.
    // For the self property-access case, put it back via prefetched.
    token afterMember = exprNext(st);
    if(cur.value == "self" && !afterMember.is(token::parenOpen) && !afterMember.is(token::bracketOpen)){
        return parseExprSelfMember(st, member, afterMember);
    } else {
        // Non-self identifier, or self.method(args).
        // afterMember was already read above; resolveIdentifierType("self",...)
        // now returns the current object/class type, so self.method() works here too.
        if(afterMember.is(token::parenOpen)){
            return parseExprMemberCall(st, member, afterMember);
        } else if(afterMember.is(token::bracketOpen)) {
            return parseExprMemberSubscript(st, member, afterMember);
        } else {
            return parseExprMemberRead(st, member, afterMember);
        }
    }
}

// ── name?: postfix query operator ──
// Only treat ? as postfix if the identifier's type has operator?() AND ? is not
// a terminator for this expression. When ? is a terminator (e.g. from precedence
// sub-parse), it's the start of a ternary, not a postfix query.
bglParser::ExprStep bglParser::parseExprPostfixQuery(ExprParseState& st, token& next){
    expression* expr = st.expr;
    int& parenDepth = st.parenDepth;
    int& startParenDepth = st.startParenDepth;
    token& cur = st.cur;
    optional<token>& prefetched = st.prefetched;
    const vector<string>& terminators = *st.terminators;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    // Check if ? is a terminator — if so, it's ternary, not postfix
    bool qIsTerminator = false;
    if(parenDepth <= startParenDepth)
        for(const string& term : terminators)
            if(term == "?"){ qIsTerminator = true; break; }
    // The postfix query ends its operand's expression (§4.10): an operand after the `?` makes it a ternary.
    {
        token after = file.peekToken();
        if(after.is(eTokenType::name) || after.is(eTokenType::integer) || after.is(eTokenType::floatLiteral)
           || after.isString() || after.is(eTokenType::charLiteral) || after.is(eTokenType::dictionaryWord)
           || after.is(token::parenOpen) || after.value == "!" || after.value == "-" || after.value == "$" || (after.is("new") && !after.is(eTokenType::dictionaryWord)))
            qIsTerminator = true;
    }
    string varName = cur.value;
    string varType = resolveIdentifierType(varName, func, body);
    string accessorRead;   // `Target?` asks of what the accessor's getter reads, unless its class has its own `?`
    { string readType; if(!declaresOwnOperator(varType, "?") && bareAccessorRead(varName, func, body, accessorRead, readType)) varType = readType; }
    classDef* cls = !varType.empty() ? languageService.classOf(varType) : nullptr;
    functionDef* queryFn = nullptr;
    if(cls != nullptr && !qIsTerminator)
        queryFn = findQueryOp(cls, varType);
    // Bare `parent?` in a method: the property-class member is read through its read emitter first.
    if(cls != nullptr && !qIsTerminator && accessorRead.empty() && func != nullptr && isPropertyClassType(varType)){
        string qualified = qualifyIdentifier(varName, func, body);
        size_t dot = qualified.rfind('.');
        string readType;
        string readText = dot != string::npos ? applyPropertyClassRead(qualified.substr(0, dot), varType, readType) : "";
        classDef* readCls = !readType.empty() ? languageService.classOf(readType) : nullptr;
        if(functionDef* q = readCls != nullptr ? findQueryOp(readCls, readType) : nullptr)
            if(auto* blk = dynamic_cast<i6Block*>(q->body)){
                emitterBindings qb; qb.self = "(" + readText + ")"; qb.val = qb.self; qb.trim = emitterTrim::wsSemi;
                expr->tokens.push_back(expandEmitterBody(blk, qb));
                if(expr->resolvedType.empty()) expr->resolvedType = q->returnType.name;
                return ExprStep::Advance;
            }
    }
    if(queryFn != nullptr){
        // Type supports postfix ? — inline the emitter
        string qualified = !accessorRead.empty() ? accessorRead : func != nullptr ? qualifyIdentifier(varName, func, body) : varName;
        if(qualified.empty()) parsingError(format("Undeclared identifier '{0}'", varName));
        auto* blk = dynamic_cast<i6Block*>(queryFn->body);
        emitterBindings qb; qb.self = qualified; qb.val = qualified; qb.trim = emitterTrim::wsSemi;
        expr->tokens.push_back(expandEmitterBody(blk, qb));
        if(expr->resolvedType.empty()) expr->resolvedType = queryFn->returnType.name;
    } else {
        // No operator?() — put ? back for ternary handling. A bare accessor is what its getter reads.
        prefetched = next;
        if(!accessorRead.empty()){
            if(expr->resolvedType.empty()) expr->resolvedType = varType;
            expr->tokens.push_back(accessorRead);
            return ExprStep::Advance;
        }
        string qualified = func != nullptr ? qualifyIdentifier(cur.value, func, body) : cur.value;
        if(!qualified.empty()){
            if(expr->resolvedType.empty()) expr->resolvedType = resolveIdentifierType(cur.value, func, body);
            expr->tokens.push_back(qualified);
        } else if(func != nullptr || !looseIdentifierMode){
            parsingError(format("Undeclared identifier '{0}'", cur.value));
        } else {
            expr->tokens.push_back(cur.value);
        }
    }
    return ExprStep::Advance;
}

// A name with nothing structural after it: resolve it to a variable, global, bare
// value emitter or action constant.
bglParser::ExprStep bglParser::parseExprBareIdentifier(ExprParseState& st, token& next){
    expression* expr = st.expr;
    token& cur = st.cur;
    optional<token>& prefetched = st.prefetched;
    string& castType = st.castType;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    // An overloaded global function named as a value: the name doesn't say which routine it is.
    if(func != nullptr && !looseIdentifierMode && !qualifyFromParams(cur.value, func) && !qualifyFromBodyLocals(cur.value, body)
       && !qualifyFromAncestorBlocks(cur.value, body) && !qualifyFromCurrentObject(cur.value) && !qualifyFromCurrentClass(cur.value)
       && languageService.findGlobalAs<variableDeclaration>(cur.value) == nullptr){
        set<string> signatures;
        for(typeDef* g : languageService.globals)
            if(auto* fd = dynamic_cast<functionDef*>(g); fd && fd->name == cur.value && !fd->isPrePassStub && !fd->isEmitter){
                string sig;
                for(paramDef* p : fd->params) sig += p->type.name + ",";
                signatures.insert(sig);
            }
        if(signatures.size() > 1)
            parsingError(format("'{0}' names {1} overloads, so it cannot be used as a bare "
                "function reference — only a call can say which one is meant. Wrap the overload "
                "you want in a function of its own and reference that.", cur.value, signatures.size()));
    }
    // A `#using`-imported object's method named bare, not called: it needs its object, so it isn't a value.
    // A global variable of the name wins over the import (§10.4), as do locals and members.
    if(func != nullptr && !looseIdentifierMode && languageService.findGlobalAs<variableDeclaration>(cur.value) != nullptr
       && !qualifyFromParams(cur.value, func) && !qualifyFromBodyLocals(cur.value, body) && !qualifyFromAncestorBlocks(cur.value, body)
       && !qualifyFromCurrentObject(cur.value) && !qualifyFromCurrentClass(cur.value))
        for(objectDef* imp : usingObjectImports)
            for(typeMember* m : imp->members)
                if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->name == cur.value && !fd->isStatic && !fd->isEmitter)
                    warnOnceHere(format("'{0}' is both a global variable and a method imported from '{1}'; the global is used. "
                                        "Write the imported one with its full path.", fd->dName(), imp->dName()));
    if(func != nullptr && !looseIdentifierMode && !qualifyFromParams(cur.value, func) && !qualifyFromBodyLocals(cur.value, body)
       && !qualifyFromAncestorBlocks(cur.value, body)
       && !qualifyFromCurrentObject(cur.value) && !qualifyFromCurrentClass(cur.value)   // a member outranks an import
       && languageService.findGlobalAs<variableDeclaration>(cur.value) == nullptr)
        for(objectDef* imp : usingObjectImports)
            for(typeMember* m : imp->members)
                if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->name == cur.value && !fd->isStatic && !fd->isEmitter){
                    string params, args;
                    for(size_t i = 0; i < fd->params.size(); i++){
                        params += (i ? ", " : "") + typeDisplayName(fd->params[i]->type.name) + " " + fd->params[i]->name;
                        args   += (i ? ", " : "") + fd->params[i]->name;
                    }
                    parsingError(format("'{0}' is a method of '{1}', which needs its object to run, so it isn't a "
                        "function value. Pass a lambda that calls it: `({2}) => {1}.{0}({3})`.",
                        fd->dName(), imp->dName(), params, args));
                }

    // I6-syntax leak: a bare identifier followed by another bare identifier (no '.',
    // '(', '[', operator, or other separator between them) is never valid in a
    // Beguile expression. This catches I6 infix-keyword forms like `o provides X`,
    // `o ofclass C`, `o has light`, `o in container`, etc. that the property/class
    // name passthroughs would otherwise quietly mash into the emitted text.
    // Each of these has a canonical Beguile method-call form.
    // A word the caller ends this expression on (`to` in `for (i in a to b)`) is not a juxtaposition.
    const vector<string>& stops = *st.terminators;
    bool isStop = find(stops.begin(), stops.end(), next.value) != stops.end();
    if(!isStop && (next.is(eTokenType::name) || next.is(eTokenType::identifier) || next.is(eTokenType::dataType))){
        static const std::map<std::string,std::string> i6KeywordMigrations = {
            {"provides", "{lhs}.provides({rhs})"},
            {"ofclass",  "{lhs}.is({rhs})"},
            {"has",      "{lhs}.has({rhs})"},
            {"hasnt",    "!{lhs}.has({rhs})"},
            {"in",       "{lhs}.parent == {rhs}"},
            {"notin",    "{lhs}.parent != {rhs}"},
        };
        auto it = i6KeywordMigrations.find(next.value);
        if(it != i6KeywordMigrations.end()){
            // Build the suggested form by substituting {lhs}/{rhs}. Peek one more
            // token so we can show the actual `rhs` the user wrote, not a placeholder.
            token rhs = file.peekToken(1);
            string lhs = cur.value, rhsText = rhs.value;
            string suggestion = it->second;
            size_t pos;
            while((pos = suggestion.find("{lhs}")) != string::npos) suggestion.replace(pos, 5, lhs);
            while((pos = suggestion.find("{rhs}")) != string::npos) suggestion.replace(pos, 5, rhsText);
            parsingError(format(
                "'{0} {1} {2}' is I6 syntax; in Beguile use `{3}`.",
                lhs, next.value, rhsText, suggestion));
        }
        // `Box::sum(…)`: the lexer glues `::` onto the name that follows.
        if(next.value.rfind("::", 0) == 0 && next.value.size() > 2){
            string member = next.value.substr(2), owner = cur.originalValue.empty() ? cur.value : cur.originalValue;
            parsingError(format("'{0}::{1}' — a class's member is reached with '.': `{0}.{1}`. `::` names a global "
                "(`::{1}`) or a type's operator (`{0}::operator ==`).", owner, member));
        }
        parsingError(format(
            "Unexpected identifier '{0}' following '{1}'. Did you mean `{1}.{0}(...)` or are you missing an operator between them?",
            next.value, cur.value));
    }
    prefetched = next;
    // `action != Take` in a class with a `take()` method: the member wins (§3.8.2), but a method named
    // bare, not called, isn't a value; the verb was meant.
    if(func != nullptr && !next.is(token::parenOpen) && !next.is(token::period) && !looseIdentifierMode
       && !qualifyFromParams(cur.value, func) && !qualifyFromBodyLocals(cur.value, body)){
        classDef* cls = currentClass != nullptr ? currentClass : currentObject != nullptr ? currentObject->objectClass : nullptr;
        auto isMethod = [&](typeMember* m){ auto* fd = dynamic_cast<functionDef*>(m);
                                            return fd && fd->name == cur.value && !fd->isEmitter && !fd->isStatic; };
        bool method = false;
        if(currentObject != nullptr) for(typeMember* m : currentObject->members) if(isMethod(m)) method = true;
        if(!method && cls != nullptr) method = cls->findMember(isMethod) != nullptr;
        if(method)
            for(verbObjectDef* v : languageService.verbs)
                if(v->name == cur.value)
                    parsingError(format("'{0}' here names this object's method, which isn't a value; for the verb, "
                        "write `::{0}`.", cur.originalValue.empty() ? cur.value : cur.originalValue));
    }
    // Resolve identifier: variables/params/globals take priority over verb action constants.
    // Only emit ##VerbName if the identifier doesn't resolve as a declared variable.
    // qualifyIdentifier handles: params/locals → name, object members → self.name,
    // globals → name, action constants (verbDefs) → ##VerbName.
    // It works correctly with func==nullptr (skips param/local tiers gracefully).
    // When the next token is '.', peek the member name and pass as a hint so the
    // resolver picks a candidate whose type actually exposes that member — disambiguates
    // collisions (e.g. enum value vs class instance with the same name).
    string memberHint;
    if(next.is(token::period)){
        token after = file.peekToken(1);
        if(after.is(eTokenType::identifier) || after.is(eTokenType::dataType))
            memberHint = after.value;
    }
    // Bare GLOBAL value emitter (an emitter declared WITHOUT parentheses, §14.4.5): expand
    // its body inline as a typed value, so `int x = wordSize;` works like the member form
    // `ns.wordSize`. Only when used as a value — not a call/subscript, not a dotted access.
    if(memberHint.empty() && !next.is(token::parenOpen) && !next.is(token::bracketOpen)){
        functionDef* ve = nullptr;
        for(typeDef* g : languageService.globals)
            if(auto* fd = dynamic_cast<functionDef*>(g))
                if(fd->name == cur.value && fd->isValueEmitter && fd->isEmitter){ ve = fd; break; }
        if(ve == nullptr)
            for(classDef* imp : usingImports)
                for(typeMember* m : imp->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m))
                        if(fd->name == cur.value && fd->isValueEmitter && fd->isEmitter){ ve = fd; break; }
        if(ve != nullptr)
            if(auto* blk = dynamic_cast<i6Block*>(ve->body)){
                string b = expandEmitterBody(blk, {});
                size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                expr->tokens.push_back(b);
                if(expr->resolvedType.empty()) expr->resolvedType = ve->returnType.name;
                cur = exprNext(st);
                return ExprStep::Continue;
            }
    }
    string qualified = qualifyIdentifier(cur.value, func, body, memberHint);
    if(!qualified.empty()){
        if(!castType.empty()){
            // Try invoking source-type's operator()→castType emitter. For int↔uint
            // (passthrough bodies) this returns the source text unchanged, matching
            // the relabel-only cast behavior. For float casts it emits the
            // @numtof/@ftonumz body, allocating a temp slot if needed.
            string srcType = resolveIdentifierType(cur.value, func, body, memberHint);
            string srcText = qualified;
            // A bare property member (`parent` for `self.parent`) is read through its class before the cast.
            if(size_t ownerDot = qualified.rfind('.'); ownerDot != string::npos && !next.is(token::period) && isPropertyClassType(srcType)){
                string readRet, read = applyPropertyClassRead(qualified.substr(0, ownerDot), srcType, readRet);
                if(!read.empty()){ srcText = read; if(!readRet.empty()) srcType = readRet; }
            }
            string newText = applyCastConversion(srcText, srcType, castType);
            expr->tokens.push_back(newText);
            if(castType != srcType && isUpcast(castType, srcType)){ expr->upcastText = newText; expr->upcastFrom = srcType; }
            expr->resolvedType = castType;
            castType = "";
        } else {
            string idType = resolveIdentifierType(cur.value, func, body, memberHint);
            // A bare member whose type is a property accessor (`target` for `self.target`) reads
            // through its getter, on the member's owner.
            size_t ownerDot = qualified.rfind('.');
            string readRet, read;
            if(ownerDot != string::npos && !next.is(token::period) && isPropertyClassType(idType))
                read = applyPropertyClassRead(qualified.substr(0, ownerDot), idType, readRet);
            functionDef* getter = nullptr;
            if(read.empty() && ownerDot != string::npos && (getter = nativeGetterOf(idType)) != nullptr
               && ((next.is(token::period) && isOwnMember(idType, file.peekToken().value))
                   || (!next.is(token::period) && declaresOwnOperator(idType, next.value)
                       && (!next.is("?") || queryIsPostfix(file.peekToken())))))
                getter = nullptr;   // `Level._v` reads the accessor's own member; `level + 2` uses its own `+`
            if(getter != nullptr && readingForBind && !next.is(token::period)) getter = nullptr;   // `ref R r := Temp;` binds the accessor
            if(!read.empty()){
                if(expr->resolvedType.empty() || expr->tokens.empty()) expr->resolvedType = readRet;
                expr->tokens.push_back(read);
            } else if(getter != nullptr){
                // A bare accessor member (`Level` for `self.level`) reads through its getter, as `self.Level` does.
                if(expr->resolvedType.empty() || expr->tokens.empty()) expr->resolvedType = getter->returnType.name;
                expr->tokens.push_back(qualified + "." + getter->i6name + "()");
            } else {
                if(expr->resolvedType.empty()) expr->resolvedType = idType;
                expr->tokens.push_back(qualified);
            }
        }
    } else if(func != nullptr || !looseIdentifierMode){
        parsingError(format("Undeclared identifier '{0}'", cur.value));
    } else {
        expr->tokens.push_back(cur.value); // #bgl island: an I6 name Beguile hasn't seen
    }
    return ExprStep::Advance;
}

// ═── IDENTIFIER: a second dispatch, on the token AFTER the name ══
// Handles: subscript name[i], function call name(args), optional
// chain name?., dot-access name.member/name.method(), postfix
// query name?, and plain identifier fallback.
// ═════════════════════════════════════════════════════════════════
bglParser::ExprStep bglParser::parseExprIdentifier(ExprParseState& st){
    expression* expr = st.expr;
    int& parenDepth = st.parenDepth;
    token& cur = st.cur;
    string& castType = st.castType;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    if(cur.value == "self") rejectSelfWithoutObject(func);
    if(file.peekToken().value == "=>" && !st.prefetched)
        parsingError(format("A lambda's parameters are written with their types, in parentheses: `(Type {0}) => …`.",
                            cur.originalValue.empty() ? cur.value : cur.originalValue));
    if(cur.value == "outer") rejectStrayOuter(func, body);

    // ── Cast to property: `(property)IDENT` ──
    // Bypasses the normal lexical-member walk that would otherwise resolve
    // bare `n_to` inside a room body to `self.n_to` (the value). With the cast,
    // the bare name is interpreted as the I6 property identifier (slot ID),
    // matching the I6 idiom `if(selected_direction != n_to) {…}`. The operand
    // must be a bare identifier — complex expressions don't have a meaningful
    // "property identifier" interpretation. Validates that IDENT names a
    // declared property (free-standing decl or class/object member name).
    if(castType == "property"){
        if(!languageService.isKnownPropertyName(cur.value))
            parsingError(format("'(property){0}': '{0}' is not a declared property. "
                                "Declare with `property {0};` or `extern property {0};` at file scope, "
                                "or it must be a member of a declared class or object.",
                                cur.originalValue.empty() ? cur.value : cur.originalValue));
        expr->tokens.push_back(cur.value);
        expr->resolvedType = "property";
        castType = "";
        cur = exprNext(st);
        return ExprStep::Continue;
    }
    // Fluent property-class read chain: `obj.parent.parent` → `parent(parent(obj))`. Tried
    // BEFORE the namespace-enum resolver (which would otherwise mis-read the 3+ segment path
    // and error "'parent' is not a member of 'obj.parent'"). Only fires for pure property-class
    // read chains of >=2 members; anything else defers (consumes nothing).
    {   string pcEmission, pcType;
        if(tryConsumePropertyClassReadChain(cur, func, body, pcEmission, pcType)){
            if(expr->resolvedType.empty()) expr->resolvedType = pcType;
            expr->tokens.push_back(pcEmission);
            cur = exprNext(st);
            return ExprStep::Continue;
        }
    }
    // A class named through a namespace path (`kit.parts.crate`) as a value: the class itself.
    if(file.peekToken(1).is(token::period) && languageService.findGlobalAs<objectDef>(cur.value) != nullptr){
        string path = cur.value;
        int k = 1;
        while(file.peekToken(k).is(token::period)
              && (file.peekToken(k + 1).is(eTokenType::identifier) || file.peekToken(k + 1).is(eTokenType::dataType))){
            path += "." + file.peekToken(k + 1).value;
            k += 2;
        }
        token after = file.peekToken(k);
        classDef* nsCls = nullptr;
        if(!after.is(token::parenOpen) && !after.is(token::period) && !after.is(eTokenType::identifier))
            if(string t = resolveNamespacedType(path); !t.empty()) nsCls = languageService.findClass(t);
        if(nsCls != nullptr && castType.empty()){
            for(int i = 1; i < k; i++) file.getToken();
            if(expr->resolvedType.empty()) expr->resolvedType = nsCls->name;
            expr->tokens.push_back(nsCls->i6Name());
            cur = exprNext(st);
            return ExprStep::Continue;
        }
    }
    // Namespaced enum value: bgl.glulx.winPlacement.above — resolve the prefix as a
    // namespace-scoped type, then bind the tail segment as one of the enum's named values.
    {   string emission, enumType;
        if(tryConsumeNamespacedEnumValue(cur, emission, enumType)){
            if(expr->resolvedType.empty()) expr->resolvedType = enumType;
            expr->tokens.push_back(emission);
            // Consume a pending cast on the enum VALUE — `(int)Enum.member` (also the
            // namespaced `bgl.glulx.Enum.member` form). Per spec §4.9.1 enum→int needs an
            // explicit cast; enum values inline as integer literals, so this is a pure retype
            // (applyCastConversion leaves the literal text unchanged) that sets the operand's
            // type to the cast target — so `x == (int)Enum.member` resolves as int==int and
            // `int y = (int)Enum.member` assigns. Without it the cast was silently dropped.
            if(!castType.empty()){
                expr->tokens.back() = applyCastConversion(expr->tokens.back(), expr->resolvedType, castType);
                expr->resolvedType = castType;
                castType = "";
            }
            cur = exprNext(st);  // advance past the consumed value — the loop's normal exprNext(st) is skipped by our continue
            return ExprStep::Continue;
        }
    }
    token next = exprNext(st);
    // `outer.m` names the host object, so its path is the host's (`outer.cells[0]`, `outer.log.clear()`).
    if(cur.value == "outer" && accessorOuter != nullptr && (next.is(token::period) || next.is(token::bracketOpen)))
        cur.value = accessorOuter->name;
    if(accessorOuter == nullptr && (cur.value == "outer" || cur.value.rfind("outer.", 0) == 0)
       && currentClass != nullptr && currentClass->name.rfind("_bglaccessor", 0) == 0 && resolveIdentifierType("outer", func, body).empty())
        parsingError("'outer' exists only in an inline accessor declared in an object; a class's accessor has no single host.");
    // ── name[i]: subscript access ──
    if(next.is(token::bracketOpen)){
        // An array imported by #using (`notes[1]` for `catalog.notes`) is subscripted by its full path.
        if(cur.value.find('.') == string::npos && func != nullptr && !qualifyFromParams(cur.value, func)
           && !qualifyFromBodyLocals(cur.value, body) && !qualifyFromAncestorBlocks(cur.value, body)
           && !qualifyFromCurrentObject(cur.value) && !qualifyFromCurrentClass(cur.value) && !qualifyFromCaptures(cur.value))
            for(objectDef* imp : usingObjectImports)
                for(typeMember* m : imp->members)
                    if(auto* ad = dynamic_cast<arrayDeclaration*>(m); ad && ad->name == cur.value && !ad->isStatic){
                        cur.value = imp->name + "." + cur.value;
                        goto importedArray;
                    }
        importedArray:
        return parseExprSubscript(st, next);
    }
    // ── name(args): function call in expression ──
    else if(next.is(token::parenOpen)){
        return parseExprCall(st, next);
    }
    // ── name?.: optional chaining (expression-level) ──
    else if((parenDepth == 0 || !castType.empty()) && next.is("?.")){
        string readText, readType;
        if(bareAccessorRead(cur.value, func, body, readText, readType, file.peekToken().value)){
            // `Target?.parent` chains off what the accessor's getter reads.
            expr->tokens.push_back(readText);
            if(expr->resolvedType.empty()) expr->resolvedType = readType;
            st.chainRecvText = "<expr>";
            st.chainRecvType = readType;
        }
        return parseExprOptionalChain(st, next);
    }
    // ── name.member: dot-access (property, method call, enum, static) ──
    else if(next.is(token::period)){
        return parseExprMemberAccess(st, next);
    }
    // ── name?: postfix query operator ──
    // Only treat ? as postfix if the identifier's type has operator?() AND ? is not
    // a terminator for this expression. When ? is a terminator (e.g. from precedence
    // sub-parse), it's the start of a ternary, not a postfix query.
    else if(next.is("?")){
        return parseExprPostfixQuery(st, next);
    }
    else {
        return parseExprBareIdentifier(st, next);
    }
}

// ─── BINARY OPERATOR: emitter dispatch via applyBinaryOperator() ─
bglParser::ExprStep bglParser::parseExprOperator(ExprParseState& st){
    expression* expr = st.expr;
    if(st.cur.value == "=")
        parsingError("An assignment is a statement, not a value: assign first, then read the variable.");
    // `<expr>?.member`: optional chaining off a receiver that is already parsed (a member read).
    if(st.cur.value == "?." && !expr->tokens.empty() && !expr->resolvedType.empty()){
        st.chainRecvText = "<expr>";
        st.chainRecvType = expr->resolvedType;
        token q = st.cur;
        return parseExprOptionalChain(st, q);
    }
    int& parenDepth = st.parenDepth;
    token& cur = st.cur;
    optional<token>& prefetched = st.prefetched;
    string& castType = st.castType;
    const vector<string>& terminators = *st.terminators;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    // `a :== b`: the same instance, compared as references; no `operator ==` is called.
    if(cur.value == ":=="){
        auto instanceType = [&](const string& t){
            classDef* c = languageService.classOf(t);
            return t == "nullliteral" || isWordArrayType(t) || t == "bytearray" || (c != nullptr && !c->isPrimitive);
        };
        if(!instanceType(expr->resolvedType))
            parsingError(format("':==' compares instances, and '{0}' is a value of type '{1}': use '=='.",
                                expr->text(), typeDisplayName(expr->resolvedType.empty() ? "unknown" : expr->resolvedType)));
        vector<string> rhsTerminators = terminators;
        for(const string& op : kPrecedenceOps)
            if(operatorPrecedence(op) <= operatorPrecedence(":==")) rhsTerminators.push_back(op);
        if(parenDepth > 0) rhsTerminators.push_back(token::parenClose);
        rhsTerminators.push_back("?");
        expression* rhsExpr = parseExpression(exprNext(st), rhsTerminators, func, body);
        if(rhsExpr->tokens.empty()) parsingError("':==' has nothing after it: an operand is missing.");
        if(!instanceType(rhsExpr->resolvedType))
            parsingError(format("':==' compares instances, and '{0}' is a value of type '{1}': use '=='.",
                                rhsExpr->text(), typeDisplayName(rhsExpr->resolvedType.empty() ? "unknown" : rhsExpr->resolvedType)));
        string lhsText = expr->text();
        expr->tokens.assign(1, "(" + lhsText + " == " + rhsExpr->text() + ")");
        expr->resolvedType = "bool";
        token terminatorTok;
        terminatorTok.value = rhsExpr->terminator;
        terminatorTok.tokenType = (rhsExpr->terminator == ";" || rhsExpr->terminator == ")" || rhsExpr->terminator == "?")
                                ? eTokenType::symbol : eTokenType::oper;
        prefetched = terminatorTok;
        return ExprStep::Advance;
    }

    if(!expr->resolvedType.empty()){
        // A plain enum doesn't widen to int (§2.7.3), so int arithmetic doesn't apply to it.
        if(enumDef* en = languageService.findEnum(expr->resolvedType); en != nullptr && !en->isBnum
           && (cur.value == "+" || cur.value == "-" || cur.value == "*" || cur.value == "/" || cur.value == "%"))
            parsingError(format("'{0}' isn't defined for the enum '{1}'; cast to int first: `(int)a {0} (int)b`.",
                                cur.value, en->dName()));
        classDef* cls = languageService.classOf(expr->resolvedType);
        // A named union compares as its anonymous form does: by identity.
        if(cls != nullptr && !cls->unionMembers.empty() && (cur.value == "==" || cur.value == "!="))
            cls = nullptr;
        // Set expected type for the RHS so name resolution can disambiguate. Applies to
        // both classDef and enumDef LHS — most binary operators take same-type RHS.
        // The parseExpression-level RAII guard restores on exit; no manual restore here.
        currentExpectedType = expr->resolvedType;
        if(cls != nullptr){
            string opName = cur.value;
            // Inform 6 evaluates a call's arguments before its receiver, so an operand a call
            // returned is kept in an instance of this routine's own (the right one likewise).
            if(string r = receiverText(expr), k = keepValueResult(r, expr->resolvedType); k != r)
                replaceReceiver(expr, k);
            // Peek at RHS
            applyBinaryOperator(expr, opName, cls, terminators, parenDepth, [&]{ return exprNext(st); }, prefetched, func, body);
        } else if(auto* lhsEnum = languageService.findEnum(expr->resolvedType);
                  lhsEnum && lhsEnum->isBnum && (cur.value == "|" || cur.value == "&" || cur.value == "^")){
            // bnum bitwise composition: RHS must be a bnum sharing a common base, the
            // same bnum, or int. Result type is the shared base (or LHS if same/child).
            string opName = cur.value;
            token rhs = exprNext(st);
            string rhsType, rhsText;
            if(rhs.is(eTokenType::integer)){ rhsType = "intliteral"; rhsText = rhs.value; }
            else if(rhs.is(token::parenOpen)
                    || (rhs.is(eTokenType::name) && (file.peekToken().is(token::parenOpen) || file.peekToken().is(token::period)
                                                || file.peekToken().is(token::bracketOpen)))){
                // A call, member, element, group or cast (`f | pick()`, `f | o.flags`, `f | (ePerm)(1 << n)`):
                // the whole operand, up to an operator that binds no tighter than this one.
                vector<string> rhsTerminators = terminators;
                int myPrec = operatorPrecedence(opName);
                for(const string& op : kPrecedenceOps)
                    if(operatorPrecedence(op) <= myPrec) rhsTerminators.push_back(op);
                if(st.parenDepth > 0) rhsTerminators.push_back(token::parenClose);
                rhsTerminators.push_back("?");
                expression* rhsExpr = parseExpression(rhs, rhsTerminators, func, body);
                token terminatorTok;
                terminatorTok.value = rhsExpr->terminator;
                terminatorTok.tokenType = (rhsExpr->terminator == ";" || rhsExpr->terminator == ")" || rhsExpr->terminator == "?")
                                        ? eTokenType::symbol : eTokenType::oper;
                prefetched = terminatorTok;
                rhsType = rhsExpr->resolvedType;
                rhsText = "(" + rhsExpr->text() + ")";
            }
            else if(rhs.is(eTokenType::name)){
                string nsEmission, nsEnumType;
                if(tryConsumeNamespacedEnumValue(rhs, nsEmission, nsEnumType)){
                    rhsType = nsEnumType;
                    rhsText = nsEmission;
                } else {
                    rhsType = resolveIdentifierType(rhs.value, func, body);
                    rhsText = qualifyIdentifier(rhs.value, func, body);
                    if(rhsText.empty()) rhsText = rhs.value;
                }
            }
            else parsingError(format("Unexpected token '{0}' after bnum '{1}'", rhs.value, opName));

            // Validate RHS and compute result type
            enumDef* rhsEnum = languageService.findEnum(rhsType);
            string resultType = expr->resolvedType;
            auto ancestorOrSelf = [](enumDef* a, enumDef* b) -> enumDef* {
                // Return a common ancestor (including equality) of two bnums, else nullptr.
                for(enumDef* ai = a; ai; ai = ai->baseBnum)
                    for(enumDef* bi = b; bi; bi = bi->baseBnum)
                        if(ai == bi) return ai;
                return nullptr;
            };
            if(rhsType == "int" || rhsType == "intliteral"){
                resultType = "int";
            } else if(rhsEnum && rhsEnum->isBnum){
                enumDef* common = ancestorOrSelf(lhsEnum, rhsEnum);
                if(!common)
                    parsingError(format("bnum '{0}' and '{1}' have no shared base; they cannot be combined with '{2}'",
                                        lhsEnum->dName(), rhsEnum->dName(), opName));
                resultType = common->name;
            } else {
                parsingError(format("Operator '{0}' on bnum '{1}' requires a bnum or int right-hand side (got '{2}')",
                                    opName, lhsEnum->dName(), typeDisplayName(rhsType)));
            }
            if(opName == "^"){   // Inform 6 has no exclusive-or operator
                string lhsText = receiverText(expr);
                replaceReceiver(expr, "((" + lhsText + ") | (" + rhsText + ")) - ((" + lhsText + ") & (" + rhsText + "))");
            } else {
                expr->tokens.push_back(opName);
                expr->tokens.push_back(rhsText);
            }
            expr->resolvedType = resultType;
        } else {
            exprEmitRawBinaryOp(st, cur.value);
        }
    } else if(cur.value == "!"){
        token operand = exprNext(st);
        if(!parseExprUnaryOperand(st, "!", operand))
            parseExprPrefixNot(expr, operand, prefetched, func, body);
    } else if(cur.value == "&"){
        // Unary address-of: `&x` ≡ `(int)x`. In prefix position (no left operand — a binary
        // `&` can never start an operand) the ampersand yields x's raw machine address, the
        // same value the int cast produces, but reads as "address of". Pure sugar: set the
        // int cast and let the operand be processed with it (see §14 address-of). No pointer
        // type, no element math — for an array/object/buffer x that's its base address.
        castType = "int";
    } else if(cur.value == "-" && (expr->tokens.empty() || expr->tokens.back() == "(")){
        token operand = exprNext(st);
        if(!parseExprUnaryOperand(st, "-", operand)){
            expr->tokens.push_back("-");
            prefetched = operand;
        }
    } else {
        exprEmitRawBinaryOp(st, cur.value);
    }
    return ExprStep::Advance;
}

// `int`'s `operator -` taking a non-integer type (a float): what a unary minus on that type goes
// through. Null for the integer types, which I6 negates directly.
functionDef* bglParser::negationThroughInt(const string& typeIn){
    string t = typeIn;
    if(t.empty() || t == "int" || t == "intliteral" || t == "negativeintliteral" || t == "char"
       || t == "charliteral" || t == "uint" || t == "var") return nullptr;
    classDef* ic = languageService.findClass("int");
    if(ic == nullptr) return nullptr;
    if(t == "floatliteral") t = "float";
    return dynamic_cast<functionDef*>(findMemberInHierarchy(ic, [&](typeMember* m){
        auto* fd = dynamic_cast<functionDef*>(m);
        return fd != nullptr && fd->name == "-" && fd->isEmitter && fd->params.size() == 1
            && fd->params[0]->type.name == t && dynamic_cast<i6Block*>(fd->body) != nullptr;
    }));
}

// A prefix `!` or `-` whose operand is a path or a parenthesized group: the operand is parsed on
// its own, up to the next binary operator, so a call or member access in it sees only its own
// receiver. Other operands (literals, plain names) stay on the inline path; returns false then.
bool bglParser::parseExprUnaryOperand(ExprParseState& st, const string& op, token operand){
    bool path = operand.is(eTokenType::name)
             && (file.peekToken(1).is(token::period) || file.peekToken(1).is(token::parenOpen)
                 || file.peekToken(1).is(token::bracketOpen) || file.peekToken(1).is("?.")
                 || (op == "!" && file.peekToken(1).is("?")));
    // A plain name whose type `int` can subtract from (a float) is negated through that operator.
    if(!path && op == "-" && operand.is(eTokenType::name)){
        string t = resolveIdentifierType(operand.value, st.func, st.body);
        path = !t.empty() && negationThroughInt(t) != nullptr;
    }
    if(!path && !operand.is(token::parenOpen)) return false;
    // `-(int)k`: the `(` opens a cast, whose operand follows it; the cast and operand are the unit.
    bool castOperand = operand.is(token::parenOpen) && file.peekToken(1).is(eTokenType::dataType)
                    && (file.peekToken(2).is(token::parenClose) || file.peekToken(2).value == "<");
    // A bracketed operand is everything up to its own `)`; the outer terminators don't reach inside it —
    // unless a member, subscript or call follows the group (`!((T)o).m`), which binds tighter than the operator.
    bool bracketed = !path && !castOperand;
    if(bracketed){
        int depth = 1;
        vector<token> ahead = file.peekTokens(100000, [&](token& t){
            if(t.is(token::parenOpen)) depth++;
            else if(t.is(token::parenClose) && --depth == 0) return false;
            if(depth == 0) return true;
            return t.is(eTokenType::eof) || t.is(token::endStatement);
        });
        if(depth == 0 && !ahead.empty()){
            token& after = ahead.back();
            if(after.is(token::period) || after.is(token::bracketOpen) || after.is(token::parenOpen) || after.value == "?.")
                bracketed = false;
        }
    }
    vector<string> terms = *st.terminators;
    for(const string& o : kPrecedenceOps) terms.push_back(o);
    for(const char* t : {"?", ":", ",", "]"}) terms.push_back(t);
    if(st.parenDepth > 0) terms.push_back(token::parenClose);
    expression* sub = bracketed ? parseExpression(file.getToken(), {token::parenClose}, st.func, st.body)
                                : parseExpression(operand, terms, st.func, st.body);
    if(!bracketed) absorbPostfixQuery(sub);   // `!v.keeper?` negates the query
    // `-a.level`: a minus negates what an accessor's getter reads (no class declares a unary operator).
    if(op == "-" && !sub->tokens.empty())
        if(functionDef* g = nativeGetterOf(sub->resolvedType)){
            sub->tokens = {parenthesizeReceiver(sub->text()) + "." + g->i6name + "()"};
            sub->resolvedType = g->returnType.name;
        }
    if(bracketed) sub->tokens = {"(" + sub->text() + ")"};
    expression* expr = st.expr;
    string text = sub->text();
    // Already one unit: a parenthesized group, or a call `name(…)`, spanning the whole text.
    size_t open = 0;
    while(open < text.size() && (isalnum((unsigned char)text[open]) || text[open] == '_' || text[open] == '.')) open++;
    bool grouped = open < text.size() && text[open] == '(' && text.back() == ')';
    for(size_t i = open, depth = 0; grouped && i < text.size(); i++){
        if(text[i] == '(') depth++;
        else if(text[i] == ')' && --depth == 0 && i + 1 < text.size()) grouped = false;
    }
    if(!grouped) text = "(" + text + ")";
    classDef* subCls = op == "!" ? getDispatchClass(sub->resolvedType) : nullptr;
    if(subCls != nullptr && !subCls->isPrimitive && applyNotOperator(expr, sub->resolvedType, text)){
        expr->tokens.back() = "(" + expr->tokens.back() + ")";   // a `~~` body binds more loosely than && and ||
    } else if(op == "!"){
        expr->tokens.push_back("(~~" + text + ")");   // I6's ~~ binds more loosely than && and ||
        if(expr->resolvedType.empty()) expr->resolvedType = "bool";
    } else if(functionDef* neg = negationThroughInt(sub->resolvedType)){
        // `-x` is `0 - x`, through int's operator for x's type: the type's own arithmetic, not int's.
        emitterBindings nb; nb.self = "0"; nb.val = "0"; nb.fn = neg; nb.trim = emitterTrim::wsSemi;
        nb.args.push_back(text); nb.argTypes.push_back(sub->resolvedType);
        expr->tokens.push_back("(" + expandEmitterBody(dynamic_cast<i6Block*>(neg->body), nb) + ")");
        if(expr->resolvedType.empty()) expr->resolvedType = neg->returnType.name;
    } else {
        expr->tokens.push_back("-" + text);
        if(expr->resolvedType.empty()) expr->resolvedType = sub->resolvedType;
    }
    if(bracketed) return true;
    token t;
    t.value = sub->terminator;
    bool isOp = find(kPrecedenceOps.begin(), kPrecedenceOps.end(), t.value) != kPrecedenceOps.end();
    t.tokenType = isOp ? eTokenType::oper : eTokenType::symbol;
    st.prefetched = t;
    return true;
}

// ─── DICTIONARY WORD LITERAL ─────────────────────────────────────
bglParser::ExprStep bglParser::parseExprDictionaryWord(ExprParseState& st){
    expression* expr = st.expr;
    token& cur = st.cur;

    if(expr->resolvedType.empty()) expr->resolvedType = "dictionarywordliteral";
    // Replace apostrophes with ^ for I6 dictionary word encoding,
    // but preserve ' when it follows @ (I6 accent notation like @'e)
    string w;
    for(size_t ci = 0; ci < cur.value.size(); ci++){
        char ch = cur.value[ci];
        if(ch == '\'' && (ci == 0 || cur.value[ci-1] != '@'))
            w += '^';
        else
            w += ch;
    }
    string i6form = cur.isPlural ? ("'" + w + "//p'")   // I6 plural dictionary flag is '//p'
                                 : (w.size() == 1) ? ("'" + w + "//'") : ("'" + w + "'");
    if(!st.castType.empty()){
        i6form = applyCastConversion(i6form, "dictionarywordliteral", st.castType);
        expr->resolvedType = st.castType;
        st.castType = "";
    }
    expr->tokens.push_back(i6form);
    return ExprStep::Advance;
}

// `<expr>.member` with no '(' after it: a property-class read, a native operator()
// getter, a plain field read, or a re-process of the member as an identifier.
bglParser::ExprStep bglParser::parseExprDotChainRead(ExprParseState& st, token& member, token& afterMember){
    expression* expr = st.expr;
    token& cur = st.cur;
    optional<token>& prefetched = st.prefetched;
    functionDef* func = st.func;

    // `ns.parts.wire.laid`: a static reached through a type alias of the receiver's type is its global.
    if(afterMember.is(token::period) && file.peekToken().is(eTokenType::name)){
        variableDeclaration* alias = nullptr;
        auto isTypeAlias = [&](typeMember* m){
            auto* vd = dynamic_cast<variableDeclaration*>(m);
            return vd != nullptr && vd->name == member.value && languageService.isClassType(vd->type.name)
                && (vd->isAlias || (vd->isValueAlias && vd->declaredExpressionValue == nullptr)); };
        if(auto* od = languageService.findObjectType(expr->resolvedType))
            for(typeMember* m : od->members) if(isTypeAlias(m)){ alias = dynamic_cast<variableDeclaration*>(m); break; }
        if(alias == nullptr)
            if(classDef* rc = getDispatchClass(expr->resolvedType))
                alias = dynamic_cast<variableDeclaration*>(findMemberInHierarchy(rc, isTypeAlias));
        if(alias != nullptr){
            string staticName = file.peekToken().value;
            if(string g = staticMemberGlobal(alias->type.name + "." + staticName); !g.empty()){
                classDef* tc = getDispatchClass(alias->type.name);
                auto* sv = tc != nullptr ? dynamic_cast<variableDeclaration*>(findMemberInHierarchy(tc, [&](typeMember* m){
                    auto* vd = dynamic_cast<variableDeclaration*>(m); return vd && vd->isStatic && vd->name == staticName; })) : nullptr;
                file.getToken();   // the static's name
                replaceReceiver(expr, g);
                expr->resolvedType = sv != nullptr ? sv->type.name : "";
                cur = exprNext(st);
                return ExprStep::Continue;
            }
        }
    }
    // An emitter value on the receiver's type (`holder.items.size`, `box.children.length`).
    {
        string selfText = receiverText(expr);
        string veRet;
        string veText = expandMemberValueEmitter(selfText, selfText.find('(') == string::npos ? selfText : "",
                                                 expr->resolvedType, member.value, func, st.body, veRet, expr->memberArrayDecl);
        if(!veText.empty()){
            replaceReceiver(expr, veText);
            expr->resolvedType = veRet;
            prefetched = afterMember;
            cur = exprNext(st);
            return ExprStep::Continue;
        }
    }
    // Property-class read off a COMPUTED expression (e.g. `getObj().parent`, or a further
    // `.parent` off a prior read): dispatch through the read emitter with $self = the
    // accumulated expression text, instead of emitting a raw `<expr>.member` property read.
    {
        // Resolve the field. An objectDef's OWN members (instance-only fields — e.g.
        // reached via an instance cast `((library)x).shelves`) are checked first: an
        // objectDef is its own type and its members override inherited ones, so a
        // cast/chain reaches them just like direct `library.shelves` does. Then fall
        // back to the class hierarchy for shared members.
        typeMember* pm = nullptr;
        if(auto* od = languageService.findObjectType(expr->resolvedType))
            for(typeMember* m : od->members)
                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                    if(vd->name == member.value){ pm = m; break; }
        if(pm == nullptr){
            classDef* rcls = getDispatchClass(expr->resolvedType);
            if(rcls) pm = findMemberInHierarchy(rcls, [&](typeMember* m){
                auto* vd = dynamic_cast<variableDeclaration*>(m);
                return vd != nullptr && vd->name == member.value;
            });
        }
        string mtype = pm ? dynamic_cast<variableDeclaration*>(pm)->type.name : "";
        // A member array is typed with its element type, as a path to it is.
        if(auto* ad = dynamic_cast<arrayDeclaration*>(pm); ad && !ad->isByteArray && !ad->elementType.empty() && mtype.find('<') == string::npos)
            mtype = (ad->isRaw ? "rawarray<" : "array<") + ad->elementType + ">";
        if(!mtype.empty() && isPropertyClassType(mtype)){
            string selfText = receiverText(expr);
            // A dotted path to an array is addressed as its emitters expect (see arrayReceiver);
            // a computed one (a call's result) is just a value.
            ArrayReceiver arr;
            bool isArr = isWordArrayType(expr->resolvedType);
            // A member of a call's result (`getBox().items`) is still a member: its last `.` follows the call.
            size_t lastDot = selfText.rfind('.'), lastParen = selfText.rfind(')');
            bool callOwner = lastParen != string::npos;
            if(isArr && (!callOwner || (lastDot != string::npos && lastDot > lastParen)))
                arr = arrayReceiver(selfText, selfText, expr->resolvedType, func, st.body);
            if(arr.isMember) arr.decl = expr->memberArrayDecl;
            string ret, read;
            // The inline form names the owner twice; a call owner must run once, so it goes through the routine.
            if(arr.isMember && !callOwner && (member.value == "size" || member.value == "length")){
                read = memberArraySizeText(arr, member.value, func, st.body);
                ret = "int";
            } else
                read = applyPropertyClassRead(selfText, mtype, ret, isArr ? &arr : nullptr);
            if(!read.empty()){
                replaceReceiver(expr, read);
                if(!ret.empty()) expr->resolvedType = ret;
                prefetched = afterMember;   // keep chaining (.parent.parent, or a following .method())
                cur = exprNext(st);
                return ExprStep::Continue;
            }
        }
        // Regular-method (non-emitter) operator() getter: a bare *read* of `<expr>.member`
        // whose member type declares a Beguile-method conversion operator fires that getter
        // — parity with the emitter property-accessor read path above, so a native-method
        // operator() "works as intended" on read (not only at an explicit cast). This is a
        // READ path: an assignment target `<expr>.member = v` is handled by the statement
        // parser (→ operator=) and never reaches here. Explicit conversions are excluded
        // (they fire only at explicit casts). Emits `<recv>.member._opconv()`.
        // A following operator the accessor class declares, or a `ref`/`:=` binding, takes the accessor itself.
        bool ownOpNext = !afterMember.is(token::period) && declaresOwnOperator(mtype, afterMember.value)
                         && (!afterMember.is("?") || queryIsPostfix(file.peekToken()));
        if((!afterMember.is(token::period) || !isOwnMember(mtype, file.peekToken().value)) && !ownOpNext && !readingForBind){   // `x.size.writes` reads the accessor's own member
            if(functionDef* fn = nativeGetterOf(mtype)){
                string selfText = parenthesizeReceiver(receiverText(expr));
                replaceReceiver(expr, selfText + "." + memberI6Name(expr->resolvedType, member.value) + "." + fn->i6name + "()");
                expr->resolvedType = fn->returnType.name;
                prefetched = afterMember;
                cur = exprNext(st);
                return ExprStep::Continue;
            }
        }
        // Plain field read through a class-typed member: `<expr>.field` where `field`
        // is a declared variableDeclaration member of the receiver's class. Append the
        // raw property read and retype to the field's type so further chaining works.
        // This MUST NOT fall through to the `cur = member` re-process path below, which
        // would run identifier resolution on the bare field name and — if that name is
        // also a member of the *enclosing* class — wrongly prepend `self.` (yielding
        // `self.next.self.id` instead of `self.next.id`).
        // `<expr>.items[i]` — an element of a member word array, which is inline property data.
        if(auto* ad = dynamic_cast<arrayDeclaration*>(pm);
           ad != nullptr && afterMember.is(token::bracketOpen) && isWordArrayType(mtype) && !heldByPointer(ad)){
            string owner = parenthesizeReceiver(receiverText(expr));
            expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, st.body);
            rejectConstantIndexOutside(ad, indexExpr, member.originalValue);
            replaceReceiver(expr, valueSlotText(checkedMemberSlot(owner, memberI6Name(expr->resolvedType, member.value), indexExpr->text(),
                                                                  false, memberArrayDeclTracked(ad)), ad->elementType));
            expr->resolvedType = ad->elementType;
            // `pick().grid[1][2]`: a row, subscripted on.
            if(file.peekToken().is(token::bracketOpen) && isArrayOfArraysElement(ad->elementType)){
                string row = expr->tokens.back();
                chainSubscripts(expr, row, func, st.body);
            }
            cur = exprNext(st);
            return ExprStep::Continue;
        }
        // `<expr>.big[i]`: a member array held through a pointer (promoted, or `ref`) is subscripted as a value.
        if(auto* ad = dynamic_cast<arrayDeclaration*>(pm);
           ad != nullptr && afterMember.is(token::bracketOpen) && isWordArrayType(mtype) && heldByPointer(ad)){
            string ptr = parenthesizeReceiver(receiverText(expr)) + "." + memberI6Name(expr->resolvedType, member.value);
            expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, st.body);
            string at = "array<" + ad->elementType + ">";
            classDef* ac = getDispatchClass(at);
            functionDef* gm = ac ? dynamic_cast<functionDef*>(findMemberInHierarchy(ac, [](typeMember* m){
                auto* f = dynamic_cast<functionDef*>(m); return f && f->name == "[]" && f->params.size() == 1; })) : nullptr;
            replaceReceiver(expr, valueSlotText(chainedSubscriptRead(gm, ptr, indexExpr->text(), at), ad->elementType));
            expr->resolvedType = ad->elementType;
            if(file.peekToken().is(token::bracketOpen) && isArrayOfArraysElement(ad->elementType)){
                string row = expr->tokens.back();
                chainSubscripts(expr, row, func, st.body);
            }
            cur = exprNext(st);
            return ExprStep::Continue;
        }
        if(pm != nullptr){
            string recv = receiverText(expr), whole = expr->text();
            if(parenthesizeReceiver(recv) != recv && parenthesizeReceiver(whole) != whole)
                replaceReceiver(expr, "(" + recv + ")");
            if(auto* ad = dynamic_cast<arrayDeclaration*>(pm); ad != nullptr && isWordArrayType(mtype) && !heldByPointer(ad)){
                expr->memberArrayOwner = receiverText(expr);
                expr->memberArrayProp = memberI6Name(expr->resolvedType, member.value);
                expr->memberArrayDecl = ad;
            } else expr->memberArrayDecl = nullptr;
            expr->tokens.push_back("." + memberI6Name(expr->resolvedType, member.value));
            expr->resolvedType = mtype;
            // A property-class member reached later off this field needs the accumulated
            // path as its `$self`; refresh emitterSelf to the full receiver text.
            expr->emitterSelf = receiverText(expr);
            // A cast written before the path (`(int)o.part.width`) applies at its last member.
            if(!st.castType.empty() && !afterMember.is(token::period)){
                string path = receiverText(expr);
                replaceReceiver(expr, applyCastConversion(path, expr->resolvedType, st.castType));
                expr->resolvedType = st.castType;
                st.castType = "";
            }
            // `f().next[0]`: a field whose class has its own operator [].
            if(afterMember.is(token::bracketOpen) && !isWordArrayType(mtype) && mtype != "bytearray")
                if(classDef* fc = getDispatchClass(mtype))
                    if(auto* gm = dynamic_cast<functionDef*>(findMemberInHierarchy(fc, [](typeMember* m){
                           auto* f = dynamic_cast<functionDef*>(m); return f && f->name == "[]" && f->params.size() == 1; }))){
                        expression* idx = parseExpression(file.getToken(), {token::bracketClose}, func, st.body);
                        replaceReceiver(expr, chainedSubscriptRead(gm, receiverText(expr), idx->text(), mtype));
                        expr->resolvedType = gm->returnType.name;
                        cur = exprNext(st);
                        return ExprStep::Continue;
                    }
            prefetched = afterMember;   // keep chaining (.field.field, or a following .method())
            cur = exprNext(st);
            return ExprStep::Continue;
        }
        // Member not found on a receiver of an object-backed class. Its static type may be a
        // supertype of what it holds at run time (e.g. a value from `.parent`), so a subtype's member
        // needs a cast. Error AT the member with a cast hint, instead of silently dropping it (which
        // surfaces as a confusing type error on the enclosing statement). Strict mode only — loose
        // #bgl islands keep passthrough.
        classDef* recvCls = languageService.classOf(expr->resolvedType);
        if(recvCls != nullptr && isObjectBackedClass(recvCls) && !looseIdentifierMode && func != nullptr){
            string recvText = receiverText(expr);
            string shown = typeDisplayName(expr->resolvedType);
            parsingError(format("'{0}' is not a member of '{2}'. If this value holds a more specific "
                "type at run time (e.g. from '.parent'), cast to it to reach its members: "
                "((SomeType){1}).{0}  (or ((var){1}).{0} for an untyped read).",
                member.value, recvText, shown));
        }
    }
    // Not a method call (e.g. struct member) — emit '.' and re-process member
    prefetched = afterMember;
    expr->tokens.push_back(".");
    cur = member;
    return ExprStep::Continue;
}

// `<expr>.method(args)` — bind the call against the accumulated expression's type.
bglParser::ExprStep bglParser::parseExprDotChainCall(ExprParseState& st, token& member, token& afterMember){
    expression* expr = st.expr;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    // Structural '(' tokens opened before the receiver are not part of it; they go back in front
    // of the call.
    size_t openParens = leadingParens(expr);
    string selfText = receiverText(expr);
    // When the receiver path is a pseudo-property whose Beguile type wraps an
    // I6-level operation on the owning object (attributeList → `obj has attr`,
    // parentProp → `move obj to v`), `$self` in the emitter body must
    // substitute to the OWNING object, not to the receiver path. The path-walk
    // captured the owner in expr->emitterSelf; route through it for those
    // virtual types so `self.attributes.hasnt(light)` emits `(self hasnt light)`
    // instead of the broken `(self.attributes hasnt light)`.
    string chainTypeNameForSelf = expr->resolvedType;
    if(!expr->emitterSelf.empty() &&
       (chainTypeNameForSelf == "attributelist" || chainTypeNameForSelf == "parentprop"))
        selfText = expr->emitterSelf;

    string chainTypeName = expr->resolvedType;
    redirectThroughGetter(selfText, chainTypeName, member.value);
    // Method receiver may be either a classDef or an objectDef (unclassed objectDefs
    // each have their own type identity). Templated names like `array<int>` aren't
    // registered as their own typeDef — use getDispatchClass to strip the <...> suffix
    // and reach the generic base when the raw lookup misses, mirroring the receiver
    // resolution used elsewhere (resolveMethod, operator dispatch).
    typeDef& chainTd = languageService.getType(chainTypeName);
    if(dynamic_cast<classDef*>(&chainTd) == nullptr && dynamic_cast<objectDef*>(&chainTd) == nullptr
       && getDispatchClass(chainTypeName) == nullptr)
        parsingError(format("Type '{0}' has no methods", chainTypeName));

    string methName = member.value;
    ParsedArgList cpal = parseCallArgList(func, body);
    vector<expression*> callArgs = cpal.args;
    vector<string> emptyNamed = cpal.namedArgNames;
    vector<vector<interpolatedSegment>> emptyInterp;
    // Element type for generic receivers (array<int> → int), so T binds in the
    // method signature (indexOf(T item), filter(func<bool,T>), …).
    string chainElem;
    { size_t lt = chainTypeName.find('<');
      if(lt != string::npos && !chainTypeName.empty() && chainTypeName.back() == '>')
          chainElem = chainTypeName.substr(lt + 1, chainTypeName.size() - lt - 2);
      if(chainElem.empty() && chainTypeName == "bytearray") chainElem = "char"; }
    // Member (property) array receiver: a dotted path with no call (e.g. "widget.m")
    // routes through the dual-form utility with $self=owner, $prop=property. A chained
    // call result ("_bglArray.filter(…)" — has '(') is a scratch global → 0 sentinel.
    // A member of a call's result (`getBox().items`) is a member too: its last `.` follows the call.
    size_t lastDot = selfText.rfind('.'), lastParen = selfText.rfind(')');
    bool chainIsMember = isWordArrayType(chainTypeName)
                         && lastDot != string::npos
                         && (lastParen == string::npos || lastDot > lastParen);
    // A `ref` member holds a POINTER to an array owned elsewhere, so a chained call on
    // it — `obj.slot.length()` — addresses the pointed-at array as a value, (obj.slot, 0),
    // not the property slot as inline data.
    if(chainIsMember){
        size_t rd = selfText.rfind('.');
        if(memberArrayIsRef(selfText.substr(0, rd), selfText.substr(rd + 1), func, body))
            chainIsMember = false;
    }
    string chainSelf = selfText;
    string chainProp = (isWordArrayType(chainTypeName) || chainTypeName == "bytearray") ? "0" : "<$prop undefined>";
    if(chainIsMember){
        size_t d = selfText.rfind('.');
        chainSelf = selfText.substr(0, d);
        chainProp = selfText.substr(d + 1);
        // The receiver path often resolves to the bare base ("array") with no template,
        // so recover the element type from the owning object's property declaration.
        if(chainElem.empty())
            chainElem = resolveArrayElementTypeDotted(chainSelf, chainProp, func, body);
    }
    // The receiver is the array a <linq> step returned, held by nothing: the next step frees it once read.
    if(!chainIsMember && isWordArrayType(chainTypeName) && chainSelf.rfind("_bglArray.", 0) == 0 && languageService.linqInUse)
        chainProp = "-1";
    // A member array stored apart from its owner (too big for a property) is addressed as a global
    // array, but its element type is still its declaration's.
    if(chainElem.empty() && isWordArrayType(chainTypeName) && lastDot != string::npos
       && (lastParen == string::npos || lastDot > lastParen))
        chainElem = resolveArrayElementTypeDotted(selfText.substr(0, lastDot), selfText.substr(lastDot + 1), func, body);
    // size()/length() on a RAW member array (rawArray<T>, or array<dictionaryWord>, which
    // I6 owns) cannot route through _bglArray: the runtime's member branch assumes the
    // tracked layout and reads the trailing word as the length, which on a raw member is
    // the last ELEMENT. A raw member has no length word — its extent is the property's
    // own size, so both methods answer `obj.#prop/WORDSIZE`. I6 computes `#prop` after
    // an additive property has accumulated, so this is also correct across inheritance
    // layers. Mirrors the non-chained fast path above.
    // A computed owner (`(a ?? b).items`) can't be looked up by its text; the field step kept its declaration.
    bool declTracked = chainIsMember && expr->memberArrayDecl != nullptr && memberArrayDeclTracked(expr->memberArrayDecl);
    if(declTracked && chainElem.empty()) chainElem = expr->memberArrayDecl->elementType;
    string rawExtent;
    if(chainIsMember && !declTracked && (methName == "size" || methName == "length")
       && !memberArrayIsTracked(chainSelf, chainProp, func, body))
        rawExtent = "((" + chainSelf + ".#" + propertyI6Name(chainProp) + ")/WORDSIZE)";
    if(chainIsMember && !declTracked) rejectRawMemberLengthOp(chainSelf, chainProp, methName, func, body);

    functionDef* method = bindMethodCall(chainTypeName, selfText, methName,
                                           callArgs, emptyNamed, emptyInterp, chainElem);

    expr->tokens.assign(openParens, "(");
    expr->resolvedType = method->returnType.name;

    string callText;
    if(!rawExtent.empty()){
        callText = rawExtent;
        expr->tokens.push_back(rawExtent);
    }
    else if(method->isEmitter){
        if(auto* blk = dynamic_cast<i6Block*>(method->body)){
            emitterBindings cb; cb.self = chainSelf; cb.val = chainSelf; cb.prop = chainProp;
            // $host — the object a storageless proxy member is accessed on (chainSelf minus its
            // trailing `.member`): `container` for `container.children`. Lets childrenProp's
            // length()/size() reach the host. Opt-in; ordinary methods keep the full receiver.
            cb.host = chainSelf.substr(0, chainSelf.rfind('.') == string::npos ? chainSelf.size()
                                                                              : chainSelf.rfind('.'));
            cb.selfType = chainTypeName;
            cb.fn = method;
            // The receiver and the arguments may all be temporaries: hold them until the call returns.
            string heldSelf = chainSelf;
            vector<string> heldArgs;
            for(expression* a : callArgs) heldArgs.push_back(a->text());
            vector<pair<string*, string>> operands{{&heldSelf, chainTypeName}};
            for(size_t i = 0; i < callArgs.size(); i++) operands.push_back({&heldArgs[i], callArgs[i]->resolvedType});
            HeldTemporaries heldTemps = holdCallTemporaries(operands);
            if(heldTemps.any()){ cb.self = heldSelf; cb.val = heldSelf; }
            cb.args = heldArgs;
            for(expression* a : callArgs) cb.argTypes.push_back(a->resolvedType);
            cb.elemType = chainElem;   // one substitution covers every $opref in the body
            cb.trim = emitterTrim::ws;
            string b = expandEmitterBody(blk, cb);
            if(heldTemps.any()) b = wrapHeldCall(heldTemps, b);
            callText = b;
            expr->tokens.push_back(b);
        }
    } else if(classDef* sc = method->isStatic ? getDispatchClass(chainTypeName) : nullptr){
        // `root.kit.item.twice(3)`: a static method through a path to its class is the class's routine.
        classDef* owner = sc->declaringClass(method);
        string call = i6Emitter::staticRoutineName(owner ? owner : sc, method) + "(";
        for(size_t i = 0; i < callArgs.size(); i++) call += (i ? ", " : "") + callArgs[i]->text();
        call += ")";
        callText = call;
        expr->tokens.push_back(call);
    } else {
        const string callName = methodCallName(method, memberI6Name(chainTypeName, methName));
        // A value-class instance a call returned is that routine's own, which the next call into it resets.
        string heldSelf = keepValueResult(selfText, chainTypeName);
        vector<string> heldArgs;
        for(expression* a : callArgs) heldArgs.push_back(a->text());
        vector<pair<string*, string>> operands{{&heldSelf, chainTypeName}};
        for(size_t i = 0; i < callArgs.size(); i++) operands.push_back({&heldArgs[i], callArgs[i]->resolvedType});
        HeldTemporaries heldTemps = holdCallTemporaries(operands);
        string call = parenthesizeReceiver(heldSelf) + "." + callName + "(";
        for(size_t i = 0; i < heldArgs.size(); i++){
            if(i > 0) call += ", ";
            call += heldArgs[i];
        }
        call += ")";
        if(heldTemps.any()) call = wrapHeldCall(heldTemps, call);
        callText = call;
        expr->tokens.push_back(call);
    }

    // Chain + subscript: `arr.method(args)[i]` dispatches operator[] on the
    // method's return type, with the emitted call text as the subscript receiver.
    // Without this, the raw `[i]` would emit verbatim and I6 chokes on
    // method-call-result indexing (it only allows `name-->i` form).
    if(!callText.empty()) subscriptCallResult(expr, expr->tokens.size() - 1, func, body);
    if(!st.chainResultCast.empty() && !file.peekToken().is(token::period) && !expr->tokens.empty()){
        expr->tokens.back() = applyCastConversion(expr->tokens.back(), expr->resolvedType, st.chainResultCast);
        expr->resolvedType = st.chainResultCast;
        st.chainResultCast.clear();
    }
    return ExprStep::Advance;
}

// ─── DOT CHAINING on resolved expression: .method() or .property ─
bglParser::ExprStep bglParser::parseExprDotChain(ExprParseState& st){

    // Chained method call on result of prior expression: <expr>.method(args)
    // (member name may collide with a type name → accept dataType too).
    token member = file.getToken({eTokenType::identifier, eTokenType::dataType});
    token afterMember = exprNext(st);
    ExprStep r = !afterMember.is(token::parenOpen) ? parseExprDotChainRead(st, member, afterMember)
                                                   : parseExprDotChainCall(st, member, afterMember);
    // A cast waiting for the chain applies once nothing more is read off it.
    if(!st.chainResultCast.empty() && !st.expr->tokens.empty()){
        token next = r == ExprStep::Continue ? st.cur : (st.prefetched ? *st.prefetched : file.peekToken());
        if(!next.is(token::period) && !next.is(token::bracketOpen) && !next.is("?.") && !next.is(eTokenType::dictionaryWord)){
            st.expr->tokens.back() = applyCastConversion(st.expr->tokens.back(), st.expr->resolvedType, st.chainResultCast);
            st.expr->resolvedType = st.chainResultCast;
            st.chainResultCast.clear();
        }
    }
    return r;
}

// A sub-expression that stopped at `?` where the `?` is a postfix query (what follows can't start a
// ternary branch: `!v.keeper?`, `x && a?`): the query is applied to it, and the token after the `?`
// becomes its terminator. False, consuming nothing, when it is a ternary or the type has no query.
bool bglParser::absorbPostfixQuery(expression* sub){
    if(sub == nullptr || sub->terminator != "?" || sub->tokens.empty()) return false;
    token after = file.peekToken();
    if(!(after.is(token::endStatement) || after.is(token::parenClose) || after.is(token::comma) || after.is(token::braceClose)
         || after.is(token::bracketClose) || after.value == "&&" || after.value == "||" || after.value == "?")) return false;
    string operand = parenthesizeReceiver(sub->text());
    string queryType = sub->resolvedType;
    // `!parent?`: a property-class member is read through its read emitter, then queried.
    if(string text = sub->text(); isPropertyClassType(queryType) && text.rfind('.') != string::npos){
        string readType;
        string read = applyPropertyClassRead(text.substr(0, text.rfind('.')), queryType, readType);
        if(!read.empty()){ operand = "(" + read + ")"; queryType = readType; }
    }
    classDef* qcls = queryType.empty() ? nullptr : getDispatchClass(queryType);
    auto* queryFn = qcls == nullptr ? nullptr : findQueryOp(qcls, queryType);
    if(queryFn == nullptr) return false;
    emitterBindings qb; qb.self = operand; qb.val = operand; qb.trim = emitterTrim::wsSemi;
    sub->tokens.assign(1, "(" + expandEmitterBody(dynamic_cast<i6Block*>(queryFn->body), qb) + ")");
    sub->resolvedType = queryFn->returnType.name;
    sub->terminator = file.getToken().value;
    return true;
}

// ─── TERNARY OPERATOR: condition ? trueExpr : falseExpr ──────────
// Uses continuation: true branch is sub-parsed (':' is unambiguous), then the false
// branch is collected by continuing this loop — preserving paren tracking. Assembly
// happens after the loop exits.
bglParser::ExprStep bglParser::parseExprTernaryStart(ExprParseState& st){
    expression* expr = st.expr;
    int& parenDepth = st.parenDepth;
    int& startParenDepth = st.startParenDepth;
    token& cur = st.cur;
    using PendingTernary = ExprParseState::PendingTernary;
    vector<ExprParseState::PendingTernary>& pendingTernaries = st.pendingTernaries;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    // `x?;` — nothing follows the `?`, so it is the postfix query: on a member read (`b.up?`), the
    // member's type's operator ?(); a type without one can't be tested.
    if(token pk = file.peekToken(); pk.is(token::endStatement) || pk.is(token::parenClose) || pk.is(token::comma)
       || pk.is(token::braceClose) || pk.is(token::bracketClose) || pk.value == "&&" || pk.value == "||"
       || pk.value == "?"){   // `b.up? ? x : y`: the first `?` is the query, the second the ternary
        classDef* qcls = !expr->resolvedType.empty() && !expr->tokens.empty() ? getDispatchClass(expr->resolvedType) : nullptr;
        auto* queryFn = qcls == nullptr ? nullptr : findQueryOp(qcls, expr->resolvedType);
        // One operand: a single token, or one parenthesized group (`(o)?`).
        bool oneGroup = expr->tokens.size() >= 3 && expr->tokens.front() == "(" && expr->tokens.back() == ")";
        if(oneGroup){
            int depth = 0;
            for(size_t i = 0; i < expr->tokens.size() && oneGroup; i++){
                if(expr->tokens[i] == "(") depth++;
                else if(expr->tokens[i] == ")") depth--;
                if(depth == 0 && i + 1 < expr->tokens.size()) oneGroup = false;
            }
            if(depth != 0) oneGroup = false;   // `(Pick(1)`: the first `(` is still open
        }
        size_t opened = oneGroup ? 0 : leadingParens(expr);   // `(glob[0]?)`: the `(` belongs to the group around it
        // Leading `(` tokens left open (`(Pick(1)? …`) belong to the group around the operand too.
        if(!oneGroup){
            int balance = 0;
            for(const string& t : expr->tokens){ if(t == "(") balance++; else if(t == ")") balance--; }
            size_t lead = 0;
            while(lead < expr->tokens.size() && expr->tokens[lead] == "(") lead++;
            if(balance > 0) opened = max(opened, min(lead, (size_t)balance));
        }
        // A member chain kept as separate tokens (`deck.one`, `.peer`) is one operand.
        if(!oneGroup && expr->tokens.size() > opened + 1){
            bool chain = true;
            for(size_t i = opened + 1; i < expr->tokens.size() && chain; i++) chain = !expr->tokens[i].empty() && expr->tokens[i][0] == '.';
            // A call (`label(1)`): its name and one argument group closing at the end.
            if(!chain && expr->tokens.size() > opened + 2 && expr->tokens[opened + 1] == "(" && expr->tokens.back() == ")"){
                int depth = 0;
                chain = true;
                for(size_t i = opened + 1; i < expr->tokens.size() && chain; i++){
                    if(expr->tokens[i] == "(") depth++;
                    else if(expr->tokens[i] == ")") depth--;
                    if(depth == 0 && i + 1 < expr->tokens.size()) chain = false;
                }
            }
            if(chain){
                string joined;
                for(size_t i = opened; i < expr->tokens.size(); i++) joined += expr->tokens[i];
                expr->tokens.resize(opened);
                expr->tokens.push_back(joined);
            }
        }
        if(queryFn != nullptr && (expr->tokens.size() == 1 || oneGroup || (opened > 0 && expr->tokens.size() == opened + 1))){
            if(oneGroup){ string t = expr->text(); expr->tokens.assign(1, t); }
            string operand = expr->tokens.back();
            emitterBindings qb; qb.self = operand; qb.val = operand; qb.trim = emitterTrim::wsSemi;
            expr->tokens.back() = expandEmitterBody(dynamic_cast<i6Block*>(queryFn->body), qb);
            expr->resolvedType = queryFn->returnType.name;
            cur = exprNext(st);
            return ExprStep::Continue;
        }
        parsingError(format("Type '{0}' has no operator ?(), so a postfix '?' can't test it.",
            typeDisplayName(expr->resolvedType.empty() ? string("var") : expr->resolvedType)));
    }
    // The branches are the expression's value: they take its expected type, not the condition's operands'.
    currentExpectedType = st.entryExpectedType;
    PendingTernary pt;
    pt.tempName = newChainTemp();
    // Strip leading structural '(' tokens from the condition. When the ternary is
    // inside a paren group like `(cond ? a : b)`, the '(' was opened before the condition
    // and its matching ')' comes after the false branch. These parens are structural —
    // they belong to the outer expression, not the ternary condition.
    vector<string> prefix;
    if(parenDepth > startParenDepth){
        // Inside a paren group: strip leading '(' up to (parenDepth - startParenDepth)
        int toStrip = parenDepth - startParenDepth;
        while(toStrip > 0 && !expr->tokens.empty() && expr->tokens.front() == "("){
            prefix.push_back(expr->tokens.front());
            expr->tokens.erase(expr->tokens.begin());
            toStrip--;
        }
    } else {
        // At top level: strip balanced pairs (e.g. `(x >= 0) ? a : b`)
        while(expr->tokens.size() > 1 && expr->tokens.front() == "(" && expr->tokens.back() == ")"){
            prefix.push_back(expr->tokens.front());
            expr->tokens.erase(expr->tokens.begin());
            expr->tokens.pop_back();
        }
    }
    pt.condText = expr->text();
    size_t mark = pendingInjections.size();
    expression* trueExpr = parseExpression(file.getToken(), {":"}, func, body);
    if(trueExpr != nullptr && trueExpr->resolvedType == "void")
        parsingError("A ?: branch must have a value, and this one is a call that returns none.");
    pt.trueSetUp = takeBranchSetUp(mark);
    pt.falseMark = pendingInjections.size();
    pt.trueText = trueExpr->text();
    pt.trueType = trueExpr->resolvedType;
    pt.parenDepthAtQuestion = parenDepth;
    // Only preserve prefix parens if we're inside a paren group (parenDepth > startParenDepth).
    // At top level, the parens were just condition grouping (e.g. `(x >= 0) ? a : b`)
    // and shouldn't wrap the temp name.
    if(parenDepth > startParenDepth) pt.prefixParens = prefix;
    pendingTernaries.push_back(pt);
    // Clear expression and continue loop — false branch tokens collect clean (no prefix)
    expr->tokens.clear();
    expr->resolvedType = "";
    cur = exprNext(st);
    return ExprStep::Continue;
}

// ─── DIRECTIVE: #beguilerSettings.property references ─────────────
bglParser::ExprStep bglParser::parseExprDirective(ExprParseState& st){
    expression* expr = st.expr;
    token& cur = st.cur;

    // ##VerbName is not valid in Beguile expressions; the ## prefix is emitted automatically.
    if(cur.value.rfind("##", 0) == 0){
        string verbName = cur.value.substr(2);
        parsingError(format("'##' prefix is not valid in Beguile source. Write '{0}' directly — the '##' prefix is emitted automatically by the verb type's operator ==.", verbName));
    }
    // #beguilerSettings.propName — resolve to a compile-time literal
    if(cur.value == "#beguilersettings"){
        file.getToken(token::period);
        token prop = file.getToken(eTokenType::identifier);
        string key = prop.value; // already lowercase
        string strVal;
        bool   isInt = false, isBool = false, boolVal = false;
        int    intVal = 0;
        switch(readBeguilerSetting(key, strVal, intVal, boolVal)){
            case eSettingKind::integer: isInt  = true; break;
            case eSettingKind::boolean: isBool = true; break;
            case eSettingKind::str:     break;
            case eSettingKind::unknown:
                parsingError(format("#beguilerSettings.{0}: unknown or unsupported property", prop.value));
        }

        if(isBool){
            expr->tokens.push_back(boolVal ? "true" : "false");
            if(expr->resolvedType.empty()) expr->resolvedType = "bool";
        } else if(isInt){
            expr->tokens.push_back(to_string(intVal));
            if(expr->resolvedType.empty()) expr->resolvedType = "intliteral";
        } else {
            expr->tokens.push_back("\"" + strVal + "\"");
            if(expr->resolvedType.empty()) expr->resolvedType = "stringliteral";
        }
        cur = exprNext(st);
        return ExprStep::Continue;
    }
    parsingError(format("Directive '{0}' is not valid in an expression.", cur.value));
    return ExprStep::Advance;
}

// ===============================================================================
// parseExpression - the main expression parser: a dispatch over token kinds, with
// one branch method per kind (declared together in bglParser.h).
// ===============================================================================
expression* bglParser::parseExpression(token firstToken, std::vector<std::string> terminators, functionDef* func, statementBlock* body, int startParenDepth){
    expression* e = parseExpressionInner(firstToken, terminators, func, body, startParenDepth);
    // The statement's whole expression releases the strings its `?.` chains held, on every path out.
    if(optStringMarkOpen && exprParseDepth == stmtExprBase && e != nullptr){
        optStringMarkOpen = false;
        string t = e->text();
        e->tokens.assign(1, "_bglStringLockStack.stackDrain(" + t + ", 0)");
    }
    return e;
}

expression* bglParser::parseExpressionInner(token firstToken, std::vector<std::string> terminators, functionDef* func, statementBlock* body, int startParenDepth){
    // Only an outermost parse is whole: an operand's sub-parse may stop before the rest of its expression.
    struct depthGuard { int& d; depthGuard(int& x) : d(x) { d++; } ~depthGuard(){ d--; } } guard(exprParseDepth);
    ExprParseState st;
    st.expr            = new expression();
    st.parenDepth      = startParenDepth;
    st.startParenDepth = startParenDepth;
    st.cur             = firstToken;
    st.terminators     = &terminators;
    st.func            = func;
    st.body            = body;
    st.entryExpectedType = currentExpectedType;
    expression* expr = st.expr;
    int& parenDepth  = st.parenDepth;
    token& cur       = st.cur;
    vector<ExprParseState::PendingTernary>& pendingTernaries = st.pendingTernaries;

    // RAII guard for currentExpectedType. parseExpression's body may set the expected type
    // (e.g. when entering an operator RHS). The guard restores it on any return path so
    // changes don't leak to the caller's scope.
    struct ExpectedTypeGuard {
        string& slot; string saved;
        ExpectedTypeGuard(string& s) : slot(s), saved(s) {}
        ~ExpectedTypeGuard() { slot = saved; }
    } _expectedTypeGuard(currentExpectedType);

    // ═══════════════════════════════════════════════════════════════════════
    // MAIN EXPRESSION LOOP — processes one token per iteration
    // Branches by token type: parens, literals, identifiers, operators, etc.
    // ═══════════════════════════════════════════════════════════════════════
    optional<token> prevTok, prevPrevTok;
    vector<ShortCircuitOp> shortCircuitOps;   // top-level `&&`/`||`, for guardShortCircuitSetUp
    while(true){
        if(exprIsTerminator(st, cur)){ expr->terminator = cur.value; break; }
        if(cur.is(eTokenType::eof)){
            parsingError("Unexpected end of file inside expression");
        }
        // I6's array operators `->` and `-->` arrive as `-`/`--` then `>`, which no Beguile operand follows.
        if((cur.value == "-" || cur.value == "--") && file.peekToken().is(">"))
            parsingError(cur.value == "-"
                ? string("'->' is Inform 6's byte-array operator; in Beguile, index the array: `name[i]` "
                         "(an array<char> or rawArray<char> reads bytes).")
                : string("'-->' is Inform 6's word-array operator; in Beguile, index the array: `name[i]`."));

        // `print(1 2)`: two operands with nothing between them.
        auto isLiteral = [](token& t){
            return t.is(eTokenType::integer) || t.is(eTokenType::floatLiteral) || t.isString() || t.is(eTokenType::charLiteral);
        };
        // `int x = 1` then `print(x);` on the next line: the statement's ';' is missing.
        if(prevTok && (isLiteral(*prevTok) || prevTok->is(eTokenType::identifier)) && (isLiteral(cur) || cur.is(eTokenType::name))
           && cur.src.line > prevTok->src.line && prevTok->src.file == cur.src.file)
            parsingError(format("Missing ';' at the end of line {0}, after '{1}'.", prevTok->src.line, prevTok->value));
        if(cur.is(token::endStatement)){
            bool inParens = parenDepth > 0 || find(terminators.begin(), terminators.end(), token::parenClose) != terminators.end();
            bool inBrackets = find(terminators.begin(), terminators.end(), "]") != terminators.end();
            if(inParens || inBrackets) parsingError(format("Missing '{0}' before ';'.", inParens ? ")" : "]"));
        }
        if(isLiteral(cur) && prevTok && (isLiteral(*prevTok) || prevTok->is(eTokenType::identifier)))
            parsingError(format("'{0}' follows '{1}' with nothing between them: a ',' or an operator is missing.",
                                cur.value, prevTok->value));
        // `1 + ;`: an operator with nothing after it.
        if(cur.is(eTokenType::oper) && cur.value != "++" && cur.value != "--"){
            token nx = st.prefetched ? *st.prefetched : stashedToken ? *stashedToken : file.peekToken();
            if(nx.isOneOf({";", ")", "]", "}", ","}) && !(cur.value == ">" && nx.is(token::parenClose)))
                parsingError(format("'{0}' has nothing after it: an operand is missing.", cur.value));
        }
        // `== 2`, `a == == b`: a binary operator with no operand before it. A `+` there is a unary plus,
        // which Inform 6 doesn't have, so it is dropped.
        {
            static const set<string> binaryOnly = {"+", "*", "/", "%", "<<", ">>", "<", ">", "<=", ">=", "==",
                "!=", "&&", "||", "|", "^", "=", ":=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<=>"};
            const string back = expr->tokens.empty() ? string("(") : expr->tokens.back();
            bool noLeft = back == "(" || back == "," || back == "-" || back == "~~" || binaryOnly.count(back);
            if(cur.is(eTokenType::oper) && binaryOnly.count(cur.value) && noLeft){
                if(cur.value == "+"){ cur = exprNext(st); continue; }
                parsingError(format("'{0}' has nothing before it: an operand is missing.", cur.value));
            }
        }
        // `int a = 1, b = 2;`: a comma outside any brackets separates nothing in an expression.
        if(cur.value == "," && !cur.isString() && !cur.is(eTokenType::charLiteral) && st.parenDepth == 0
           && find(terminators.begin(), terminators.end(), ",") == terminators.end())
            parsingError("A ',' here separates nothing: declare or assign one variable per statement "
                         "(`int a = 1; int b = 2;`).");
        // `..word` / `. .word`: a dictionary word has one dot, and a member access needs a left side.
        if(cur.value == "." && !cur.isString() && !cur.is(eTokenType::charLiteral)){
            const string back = expr->tokens.empty() ? string("(") : expr->tokens.back();
            static const set<string> leftless = {"(", ",", "+", "-", "*", "/", "%", "<", ">", "<=", ">=", "==", "!=",
                                                 "~=", "&&", "||", "|", "&", "^", "=", "~~", "?", ":"};
            if(file.peekToken().value == "." || leftless.count(back))
                parsingError("A '.' with nothing before it: a dictionary word is written `.word`, with no space.");
        }
        // `s.x x`: a member name followed straight by another name.
        if(cur.is(eTokenType::name) && prevTok && prevTok->is(eTokenType::name) && prevPrevTok && prevPrevTok->value == "."
           && cur.src.line == prevTok->src.line)
            parsingError(format("'{0}' follows '.{1}' with nothing between them: an operator is missing.",
                                cur.value, prevTok->value));
        prevPrevTok = prevTok;
        prevTok = cur;

        ExprStep step = ExprStep::Advance;
        if(!cur.isString() && cur.isNot(eTokenType::charLiteral) && (cur.value == "~" || cur.value == "~~"))
            parsingError(format("'{0}' is an Inform 6 operator; Beguile's logical not is '!', and it has no bitwise not.",
                                cur.value));
        // `kit.watt::operator <=>`: a type alias reached by path names its class.
        if((cur.is(eTokenType::identifier) || cur.is(eTokenType::dataType)) && file.peekToken(1).is(token::period)){
            int k = 1;
            string path = cur.value;
            while(file.peekToken(k).is(token::period) && file.peekToken(k + 1).is(eTokenType::name)){ path += "." + file.peekToken(k + 1).value; k += 2; }
            if(file.peekToken(k).value == "::operator" || (file.peekToken(k).value == "::" && file.peekToken(k + 1).value == "operator")){
                string t = typeAliasTarget(path);
                if(t.empty()) t = resolveNamespacedType(path);
                if(!t.empty()){
                    for(int i = 1; i < k; i++) file.getToken();
                    cur.value = t; cur.originalValue = languageService.getType(t).dName(); cur.tokenType = eTokenType::dataType;
                }
            }
        }
        if((cur.is(eTokenType::identifier) || cur.is(eTokenType::dataType))
           && (file.peekToken(1).value == "::operator"
               || (file.peekToken(1).value == "::" && file.peekToken(2).value == "operator"))) step = parseExprOperatorRef(st);
        else if(cur.is(token::parenOpen))                 step = parseExprParenOpen(st);
        else if(cur.value == "=>" && expr->tokens.empty()) step = parseExprBareLambda(st);
        else if(cur.is(token::parenClose))                step = parseExprParenClose(st);
        else if(cur.is(eTokenType::integer))              step = parseExprIntLiteral(st);
        else if(cur.is(eTokenType::floatLiteral))         step = parseExprFloatLiteral(st);
        else if(cur.isString()){
            if(expr->resolvedType.empty()) expr->resolvedType = "stringliteral";
            expr->tokens.push_back(cur.value);
        }
        else if(cur.value == "$" && file.peekToken().is(eTokenType::quote))
            parsingError("An interpolated string has no value, so it can't be part of an expression "
                         "such as a ?: branch or an operand. Pass it straight to print() or an emitter that takes "
                         "interpolatedStringLiteral, or assign it to a stringObj (`stringObj s = $\"…\";`) and use that.");
        else if(cur.is("new") && !cur.is(eTokenType::dictionaryWord)) step = parseExprNew(st);
        else if((cur.is(eTokenType::dataType) || cur.is(eTokenType::identifier))
                && getDispatchClass(cur.value) != nullptr && file.peekToken().is(token::braceOpen))
                                                          step = parseExprInlineObject(st);
        else if(cur.is(eTokenType::identifier) && aliasedInlineObjectAhead(cur)){
            cur = consumeTypeToken(cur);                  // `ns.pair{…}` / an imported `pair{…}` names the class
            step = parseExprInlineObject(st);
        }
        else if(cur.is(eTokenType::name))                 step = parseExprIdentifier(st);
        else if(cur.is(eTokenType::oper) && cur.value == "??" && parenDepth == 0){
            parseExprNullCoalescing(expr, terminators, func, body);
            step = ExprStep::Break;
        }
        else if(cur.is(eTokenType::oper)){
            if((cur.value == "&&" || cur.value == "||") && parenDepth == st.startParenDepth && pendingTernaries.empty())
                shortCircuitOps.push_back({cur.value, pendingInjections.size()});
            step = parseExprOperator(st);
        }
        else if(cur.is(eTokenType::dictionaryWord))       step = parseExprDictionaryWord(st);
        else if(cur.is(eTokenType::charLiteral)){
            // Numeric ZSCII (\NNN) and diacritical (@^a etc.) are standalone I6 tokens; others need '...'
            bool isBareToken = (!cur.value.empty() && all_of(cur.value.begin(), cur.value.end(), ::isdigit))
                            || (cur.value.rfind("@", 0) == 0);
            string text = isBareToken ? cur.value : "'" + cur.value + "'";
            if(!st.castType.empty()){                      // `(int)'A'`
                expr->tokens.push_back(applyCastConversion(text, "charliteral", st.castType));
                expr->resolvedType = st.castType;
                st.castType = "";
            } else {
                if(expr->resolvedType.empty()) expr->resolvedType = "charliteral";
                expr->tokens.push_back(text);
            }
        }
        else if(cur.value == "." && !expr->tokens.empty() && !expr->resolvedType.empty())
                                                          step = parseExprDotChain(st);
        else if(cur.value == "?")                         step = parseExprTernaryStart(st);
        else if(cur.is(eTokenType::directive))            step = parseExprDirective(st);
        else {
            expr->tokens.push_back(cur.value);
        }

        if(step == ExprStep::Break) break;
        if(step == ExprStep::Continue) continue;
        cur = exprNext(st);
    }

    // `++n.hits` as a value: an accessor's reading steps through its setter.
    for(size_t i = 0; i + 1 < expr->tokens.size(); i++)
        if(expr->tokens[i] == "++" || expr->tokens[i] == "--")
            if(string stepped = stepThroughAccessor(expr->tokens[i + 1], expr->tokens[i], true); !stepped.empty()){
                expr->tokens[i] = stepped;
                expr->tokens.erase(expr->tokens.begin() + i + 1);
            }
    // Assemble any remaining pending ternaries (e.g. top-level ternary without parens)
    if(pendingTernaries.empty()) guardShortCircuitSetUp(expr, shortCircuitOps);
    while(!pendingTernaries.empty()) exprAssembleTernary(st);
    foldOverflowingConstant(expr);
    if(exprParseDepth == 1) checkExpressionShape(expr, startParenDepth == 0);
    return expr;
}

// What the expression emits must be whole: brackets that pair up, and no operator left without its
// right operand. Inform 6 would otherwise report it against the emitted code.
void bglParser::checkExpressionShape(expression* expr, bool whole){
    if(expr == nullptr) return;
    if(expr->tokens.empty()){
        if(!allowEmptyExpr && dynamic_cast<initializerList*>(expr) == nullptr)
            parsingError(expr->terminator.empty() ? string("Expected a value.")
                                                  : format("Expected a value before '{0}'.", expr->terminator));
        return;
    }
    string code;
    for(const string& t : expr->tokens){
        if(t.size() >= 2 && (t.front() == '"' || t.front() == '\'') && t.back() == t.front()){ code += "0 "; continue; }   // a literal
        code += t + " ";
    }
    // Brackets inside literals that are part of a larger token don't count either.
    string bare;
    char q = 0;
    for(size_t i = 0; i < code.size(); i++){
        char c = code[i];
        if(q){ if(c == q) q = 0; continue; }
        if(c == '"' || (c == '\'' && i + 2 < code.size() && code[i + 2] == '\'')){ q = c; bare += '0'; continue; }
        bare += c;
    }
    int paren = 0, bracket = 0;
    if(whole) for(char c : bare){
        if(c == '(') paren++;
        else if(c == ')' && --paren < 0) parsingError("Unmatched ')' in the expression.");
        else if(c == '[') bracket++;
        else if(c == ']' && --bracket < 0) parsingError("Unmatched ']' in the expression.");
    }
    if(whole && paren > 0) parsingError("A '(' in the expression is never closed.");
    if(whole && bracket > 0) parsingError("A '[' in the expression is never closed.");
    static const set<string> binary = {"+", "-", "*", "/", "%", "<", ">", "<=", ">=", "==", "!=", "~=", "&&", "||",
                                       "|", "&", "^", "=", "<<", ">>", "-->", "->", ".", "~~", "!"};
    // The emitted text ends in an operator (tokens may carry a folded `a > ` as one).
    string tail = bare.substr(0, bare.find_last_not_of(" \t") + 1);
    if(!tail.empty() && tail.size() >= 2 && (tail.compare(tail.size() - 2, 2, "++") == 0 || tail.compare(tail.size() - 2, 2, "--") == 0))
        tail.clear();   // a postfix step
    for(const char* op : {"-->", "&&", "||", "<=", ">=", "==", "~=", "<<", ">>", "->", "+", "-", "*", "/", "%", "<", ">", "|", "&", "^", "=", "."}){
        size_t n = strlen(op);
        if(tail.size() >= n && tail.compare(tail.size() - n, n, op) == 0)
            parsingError(format("Missing operand after '{0}'.", op));
        if(tail.size() >= n && tail.compare(tail.size() - n, n, op) == 0) break;
    }
    string last;
    for(auto it = expr->tokens.rbegin(); it != expr->tokens.rend() && last.empty(); ++it) last = *it;
    if(binary.count(last)) parsingError(format("Missing operand after '{0}'.", last == "~~" ? "!" : last));
    // Two operands in a row: a value can only follow an operator, '(' or ','.
    auto operandLike = [&](const string& t){
        if(t.empty() || binary.count(t)) return false;
        static const set<string> joiners = {"(", ",", "?", ":", "~", "+=", "-=", "*=", "/=", "%=", "|=", "&=", "^=",
                                            "<<=", ">>=", "<=>", "or", "ofclass", "provides", "in", "notin", "has", "hasnt"};
        if(joiners.count(t)) return false;
        char c = t.back();
        return isalnum((unsigned char)c) || c == '_' || c == ')' || c == ']' || c == '"' || c == '\'';
    };
    for(size_t i = 0; i + 1 < expr->tokens.size(); i++){
        const string& a = expr->tokens[i];
        const string& b = expr->tokens[i + 1];
        char b0 = b.empty() ? 0 : b.front();
        bool bStartsOperand = isalnum((unsigned char)b0) || b0 == '_' || b0 == '"' || b0 == '\'' || b0 == '$' || b0 == '@';
        if(a.front() == '\'' && b0 == '\'') continue;   // a property's dictionary words: `name = .a .b;`
        if(operandLike(a) && bStartsOperand && operandLike(b) && b != "or" && a != "or")
            parsingError(format("Missing an operator between '{0}' and '{1}'.", a, b));
    }
}

// An integer expression of literals alone that overflows the target's word (`32767 + 1` on the
// Z-machine) is folded here, wrapping as the same arithmetic does at run time: I6 would fold it too,
// and reject the overflow. One that stays in range is left for I6.
void bglParser::foldOverflowingConstant(expression* expr, bool always){
    if(expr == nullptr || expr->tokens.empty()) return;
    const string text = expr->text();
    if(text.find_first_not_of("0123456789+-*/%() \t") != string::npos) return;
    if(text.find_first_of("0123456789") == string::npos) return;
    const bool z = beguilerSettings.target != "glulx";
    const long long lo = z ? -32768LL : -2147483648LL, span = z ? 65536LL : 4294967296LL;
    auto wrap = [&](long long v){ v = (v - lo) % span; if(v < 0) v += span; return v + lo; };
    size_t i = 0;
    bool overflowed = false, bad = false;
    auto skip = [&]{ while(i < text.size() && isspace((unsigned char)text[i])) i++; };
    function<long long()> sum;
    function<long long()> unary = [&]() -> long long {
        skip();
        if(i < text.size() && text[i] == '-'){ i++; return -unary(); }
        if(i < text.size() && text[i] == '('){ i++; long long v = sum(); skip(); if(i < text.size() && text[i] == ')') i++; else bad = true; return v; }
        size_t st = i;
        while(i < text.size() && isdigit((unsigned char)text[i])) i++;
        if(st == i){ bad = true; return 0; }
        return stoll(text.substr(st, i - st));   // a literal as written: I6 takes one past the signed range
    };
    auto product = [&]() -> long long {
        long long v = unary();
        for(skip(); i < text.size() && (text[i] == '*' || text[i] == '/' || text[i] == '%'); skip()){
            char op = text[i++];
            long long r = unary();
            if(op != '*' && r == 0){ bad = true; return 0; }
            long long res = op == '*' ? v * r : op == '/' ? v / r : v % r;
            if(wrap(res) != res) overflowed = true;
            v = wrap(res);
        }
        return v;
    };
    sum = [&]() -> long long {
        long long v = product();
        for(skip(); i < text.size() && (text[i] == '+' || text[i] == '-'); skip()){
            char op = text[i++];
            long long r = product();
            long long res = op == '+' ? v + r : v - r;
            if(wrap(res) != res) overflowed = true;
            v = wrap(res);
        }
        return v;
    };
    long long v = sum();
    skip();
    if(bad || i != text.size() || (!overflowed && !always)) return;
    expr->tokens = { v < 0 ? "(" + to_string(v) + ")" : to_string(v) };
}

// A switch case value is a constant to Inform 6, which won't take arithmetic on a named constant
// (`case LIMIT + 1:`): global consts with literal values are put in and the whole is folded.
void bglParser::foldCaseConstant(expression* expr){
    if(expr == nullptr || expr->tokens.empty()) return;
    string text = expr->text(), out;
    bool ops = false;   // a #define is already its value here (`3 + 2`), so arithmetic on literals folds too
    for(size_t i = 0; i < text.size();){
        if(isalpha((unsigned char)text[i]) || text[i] == '_'){
            size_t e = i;
            while(e < text.size() && (isalnum((unsigned char)text[e]) || text[e] == '_')) e++;
            string w = text.substr(i, e - i), lw = w;
            transform(lw.begin(), lw.end(), lw.begin(), ::tolower);
            auto* vd = languageService.findGlobalAs<variableDeclaration>(lw);
            if(vd == nullptr || !vd->isConst || vd->declaredExpressionValue == nullptr) return;
            string v = vd->declaredExpressionValue->text();
            if(v.empty() || v.find_first_not_of("-0123456789() ") != string::npos) return;
            out += "(" + v + ")"; i = e;
        } else { if(strchr("+-*/%", text[i])) ops = true; out += text[i++]; }
    }
    if(!ops) return;
    expression probe; probe.tokens = { out };
    foldOverflowingConstant(&probe, true);
    if(probe.tokens.size() == 1 && probe.tokens[0] != out) expr->tokens = probe.tokens;
}

// `self` names the object a method runs on; a global function or a static method has none.
void bglParser::rejectSelfWithoutObject(functionDef* func){
    if(looseIdentifierMode || func == nullptr) return;
    if(currentClass == nullptr && currentObject == nullptr)
        parsingError(format("'self' in '{0}': a global function has no object; pass one as a parameter.", func->dName()));
    else if(func->isStatic)
        parsingError(format("'self' in '{0}': a static method has no object; pass one as a parameter.", func->dName()));
}

// `outer` outside any inline accessor, where nothing else is named `outer` either.
void bglParser::rejectStrayOuter(functionDef* func, statementBlock* body){
    if(accessorOuter != nullptr || looseIdentifierMode || func == nullptr) return;
    if(currentClass != nullptr && currentClass->name.rfind("_bglaccessor", 0) == 0) return;   // a class's accessor says so itself
    if(!resolveIdentifierType("outer", func, body).empty()) return;
    parsingError("'outer' names the object holding an inline accessor, so it is only meaningful inside the "
                 "accessor's operators; in a method, use 'self'.");
}

// An element of an object's member array<T>, bounds-checked at run time (`write` checks the size, a
// read the length).
// A property named in an argument: escaped from the local renames, which a local of its name would apply.
string bglParser::escapePropertyName(const string& prop){
    bool plain = !prop.empty() && !isdigit((unsigned char)prop[0])
              && all_of(prop.begin(), prop.end(), [](char c){ return isalnum((unsigned char)c) || c == '_'; });
    return plain && prop.rfind(kGlobalEscapeMarker, 0) != 0 ? kGlobalEscapeMarker + prop : prop;
}

string bglParser::checkedMemberSlot(const string& owner, const string& prop, const string& index, bool write, bool tracked){
    return "_bglMemberAt(" + owner + ", " + escapePropertyName(prop) + ", " + index + ", " + to_string((write ? 1 : 0) | (tracked ? 2 : 0)) + ")-->0";
}

// Whether a member array keeps its length in its last word (see memberArrayIsTracked).
bool bglParser::memberArrayDeclTracked(arrayDeclaration* ad){
    if(ad == nullptr) return false;
    return !ad->isRaw && !languageService.isAdditiveProperty(ad->i6name.empty() ? ad->name : ad->i6name);
}

// `a[4]` on an `array<int> a[4]`: a constant index past the declared size is a compile-time error.
void bglParser::rejectConstantIndexOutside(const string& path, expression* index, functionDef* func, statementBlock* body){
    if(index == nullptr || looseIdentifierMode) return;
    // A local or parameter may be re-pointed at another array; its declared size says nothing about the one it names.
    if(path.find('.') == string::npos && func != nullptr
       && (qualifyFromParams(path, func) || qualifyFromBodyLocals(path, body) || qualifyFromAncestorBlocks(path, body))) return;
    rejectConstantIndexOutside(dynamic_cast<arrayDeclaration*>(findAssignedDeclaration(path, func, body)), index);
}
void bglParser::rejectConstantIndexOutside(arrayDeclaration* ad, expression* index, const string& shown){
    if(index == nullptr || ad == nullptr || ad->arraySize <= 0 || ad->isRaw || looseIdentifierMode) return;
    string t = index->text();
    t.erase(remove_if(t.begin(), t.end(), [](char c){ return isspace((unsigned char)c) || c == '(' || c == ')'; }), t.end());
    if(t.empty() || t.find_first_not_of("-0123456789") != string::npos || t.find('-', 1) != string::npos || t == "-") return;
    long n = stol(t);
    if(n >= 0 && n < ad->arraySize) return;
    parsingError(format("Index {0} is outside '{1}', which holds {2} element{3} (0 to {4}).", t, shown.empty() ? ad->dName() : shown,
                        ad->arraySize, ad->arraySize == 1 ? "" : "s", ad->arraySize - 1));
}

// `recv.member.getter()` stepped by `++`/`--`: the accessor is held, read, and written back through its
// setter; returns the expression's value (a temporary), or "" when the text isn't an accessor's reading.
string bglParser::stepThroughAccessor(const string& reading, const string& op, bool prefix){
    static const std::regex form(R"(^(.+)\.([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)\(\)$)");
    std::smatch m;
    if(!regex_match(reading, m, form)) return "";
    string getterName = m[3].str();
    functionDef* setter = nullptr;
    for(typeDef* g : languageService.globals){
        auto* c = dynamic_cast<classDef*>(g);
        if(c == nullptr || !isValueClass(c)) continue;
        functionDef* getter = nullptr;
        for(typeMember* mem : c->members)
            if(auto* f = dynamic_cast<functionDef*>(mem); f && f->name == "operator()" && f->params.empty() && f->i6name == getterName) getter = f;
        if(getter == nullptr) continue;
        for(typeMember* mem : c->members)
            if(auto* f = dynamic_cast<functionDef*>(mem); f && f->name == "=" && f->params.size() == 1 && !f->isEmitter
               && f->params[0]->type.name == getter->returnType.name) setter = f;
        if(setter != nullptr) break;
    }
    if(setter == nullptr) return "";
    if(setter->i6name.empty()) setter->i6name = mangleOperatorName(setter->name);
    string held = newChainTemp(), value = newChainTemp();
    string step = op == "++" ? " + 1" : " - 1";
    i6RawNode* inj = new i6RawNode();
    inj->text = held + " = " + m[1].str() + "." + m[2].str() + "; "
              + (prefix ? value + " = " + held + "." + getterName + "()" + step + "; " + held + "." + setter->i6name + "(" + value + ");"
                        : value + " = " + held + "." + getterName + "(); " + held + "." + setter->i6name + "(" + value + step + ");");
    inj->cooked = true;
    pendingInjections.push_back(inj);
    return value;
}

