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
    if(cls == nullptr || !cls->isEmitterClass) return "";
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
        objName = r.owner + "." + r.prop;
        r.isMember = false;
    }
    return r;
}

// A tracked member spends its LAST word on the length: capacity is one less than the property
// holds, and the length is read from that slot. A rawArray or dictionaryWord array keeps the bare
// layout, where length == size. Must agree with the emitter's member-array layout.
string bglParser::memberArraySizeText(const ArrayReceiver& r, const string& which,
                                      functionDef* func, statementBlock* body){
    string words = "((" + r.owner + ".#" + propertyI6Name(r.prop) + ")/WORDSIZE)";
    if(!memberArrayIsTracked(r.owner, r.prop, func, body)) return words;
    return which == "size" ? "(" + words + " - 1)"
                           : "(" + r.owner + ".&" + propertyI6Name(r.prop) + "-->(" + words + " - 1))";
}

string bglParser::expandMemberValueEmitter(string objText, const string& rawRecv, const string& recvType,
                                           const string& memberName, functionDef* func, statementBlock* body,
                                           string& outRetType){
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
    ArrayReceiver arr = arrayReceiver(objText, rawRecv, recvType, func, body);
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
    fd.isEmitter = false;
    fd.isExternal = false;
    fd.src = file.currentLocation();

    // Parse parameter list: (type name, type name, ...) or ()
    token t = bareArrow ? token() : file.getToken(); // first type token or ')'
    while(!bareArrow && t.isNot(token::parenClose)){
        paramDef& p = *(new paramDef());
        string typeName = t.value;
        if(typeName == "func") typeName = parseFuncType();
        else if(typeName == "array" || typeName == "rawarray") typeName = parseArrayTypeTail(typeName);
        p.type = languageService.getType(typeName);
        if(p.type.name.empty()) p.type.name = typeName; // for func<...> or unknown types
        token nameTok = file.getToken(eTokenType::identifier);
        p.name = nameTok.value;
        fd.params.push_back(&p);
        t = file.getToken(); // ',' or ')'
        if(t.is(",")) t = file.getToken(); // advance to next type
    }

    // Consume =>
    if(!bareArrow) file.getToken("=>");

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
        openCompileContext(eCompileContext::codeBlock, lambdaBody);
        fd.isLambda = true;   // its first `return` sets the return type (processReturnExpr)
        while(processNextStatement(fd) == false){}
        closeCompileContext(eCompileContext::codeBlock);
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
        for(int k = 0, depth = 0; ; k++){
            token pk = k == 0 ? bodyStart : file.peekToken(k);
            if(pk.is(eTokenType::eof)) break;
            if(pk.is(token::parenOpen) || pk.is(token::braceOpen) || pk.is(token::bracketOpen)) depth++;
            else if(pk.is(token::parenClose) || pk.is(token::braceClose) || pk.is(token::bracketClose)){ if(depth == 0) break; depth--; }
            else if(depth == 0 && (pk.is(token::comma) || pk.is(token::endStatement))) break;
            if(pk.is("$") && file.peekToken(k + 1).is(eTokenType::quote)){ hasInterpolation = true; break; }
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
            expression* retExpr = parseExpression(bodyStart, {token::endStatement, token::comma, token::parenClose,
                                                              token::braceClose, ":"}, &fd, lambdaBody);   // `}`: last of a `{ … }` list
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
            if(retExpr->resolvedType == "void"){
                // `=> print("…")`: a call with no value is the body itself, not something to return.
                i6RawNode* stmt = new i6RawNode();
                stmt->text = retExpr->text() + ";";
                lambdaBody->statements.push_back(stmt);
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
        capGlobal.type.name = cap.typeName;
        capGlobal.needsEarlyGlobalDecl = true;   // a class method's lambda is emitted with the class
        languageService.globals.insert(languageService.globals.begin(), &capGlobal);
        if(cap.outerName == "self"){
            i6RawNode* selfAssign = new i6RawNode();
            selfAssign->text = "self = " + cap.globalName + ";";
            lambdaBody->statements.insert(lambdaBody->statements.begin(), selfAssign);
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
        pendingInjections.push_back(&load);
        i6RawNode& unload = *(new i6RawNode());
        unload.text = cap.outerName + " = " + cap.globalName + ";";
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
    if(op == "==" || op == "!=" || op == "?=" || op == "=~")               return  7;
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
    return n;
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

static const vector<string> kPrecedenceOps = {
    "*","/","%","+","-","<<",">>","<=>","<","<=",">",">=","==","!=","?=","=~","&","^","|","&&","||"
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

    // Postfix operators (++ and --): no RHS needed. Find the zero-param emitter and inline.
    if(opName == "++" || opName == "--"){
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
                expr->tokens.push_back(expandEmitterBody(blk, pb));
            }
            if(!postfixOp->returnType.name.empty()) expr->resolvedType = postfixOp->returnType.name;
            return true;
        }
        // No postfix emitter — pass through as raw I6
        expr->tokens.push_back(opName);
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
        expression* rhsExpr = parseExpression(rhs, rhsTerminators, func, body);
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
        expression* rhsExpr = parseExpression(rhs, rhsTerminators, func, body);
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
    string matchName = opName;   // `<=>` while deriving an ordering operator (below)
    auto opMatches = [&](typeMember* m, bool wantStatic, int widenMode){
        auto* opFn = dynamic_cast<functionDef*>(m);
        if(!opFn || opFn->name != matchName) return false;
        if(opFn->isStatic != wantStatic) return false;
        // Pre-scan stubs have no params — match by name only (exact phase, as before).
        if(opFn->isPrePassStub) return widenMode == 0;
        size_t rhsIdx = opFn->isStatic ? 1 : 0;   // static takes (lhs, rhs); instance takes (rhs)
        if(opFn->params.size() <= rhsIdx) return false;
        const string& paramT = opFn->params[rhsIdx]->type.name;
        // A `var` parameter accepts anything, so it is the UNIVERSAL fallback and has to lose to
        // any more specific overload — most importantly, an inherited one has to lose to the
        // subclass's own. `object` declares `emitter eBool operator == (var)`, so while `var`
        // counted as an exact match a class deriving from `object` could never override `==`:
        // `class W : object { bool operator ==(int) }` had its own operator beaten by the
        // inherited universal one, and `w == 6` emitted a raw address comparison instead.
        // A genuinely untyped RHS still matches in mode 0 — there is nothing more specific to
        // prefer in that case.
        bool rhsUnknown = rhsType.empty() || rhsType == "var";
        bool exact = rhsUnknown || paramT == rhsType || (!rhsClassName.empty() && paramT == rhsClassName);
        if(widenMode == 0) return exact;
        if(exact) return false;                          // exact already claimed in mode 0
        string base = literalBase(rhsType);
        if(widenMode == 1) return !base.empty() && paramT == base;   // prefer the literal-base overload
        if(widenMode == 2){
            if(!base.empty() && paramT != base && isTypeCompatible(base, paramT)) return true;   // convertible
            // A parameter of a class accepts an instance of it or of a class derived from it — including
            // an object declared as its own type, whose class is the parameter's.
            classDef* argCls = languageService.classOf(rhsType);
            classDef* paramCls = languageService.findClass(paramT);
            return argCls != nullptr && paramCls != nullptr && (argCls == paramCls || argCls->hasAncestor(paramCls));
        }
        return paramT == "var";                          // mode 3: the universal accepter, last
    };
    // Exact first, then base-exact widen, then convertible widen — so every resolution that already
    // worked resolves identically (exact always wins), and among widen candidates the more specific
    // one wins. Within each phase, non-static wins: an instance operator inlines, a static costs a call.
    auto findOperator = [&]{
        for(int widenMode : {0, 1, 2, 3}){
            if(matchedOp != nullptr) break;
            for(bool wantStatic : {false, true}){
                if(matchedOp != nullptr) break;
                if(typeMember* m = findMemberInHierarchy(cls, [&](typeMember* mm){ return opMatches(mm, wantStatic, widenMode); }))
                    matchedOp = dynamic_cast<functionDef*>(m);
            }
        }
    };
    findOperator();
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
            if(typeMember* m2 = findMemberInHierarchy(convCls, [&](typeMember* m){
                auto* opFn = dynamic_cast<functionDef*>(m);
                if(!opFn || opFn->name != opName) return false;
                if(opFn->isPrePassStub) return true;
                if(opFn->params.empty()) return false;
                const string& paramT = opFn->params[0]->type.name;
                if(rhsType.empty() || rhsType=="var" || paramT==rhsType || paramT=="var") return true;
                // Converted-to-converted: the operator is, say, `char != (char)`, and we got here
                // by converting LHS int → char. Only valid if RHS is itself compatible with char.
                if(paramT == convertedType) return isTypeCompatible(rhsType, convertedType);
                return false;
            })){
                matchedOp = dynamic_cast<functionDef*>(m2);
                // The operator is the converted type's, so its left operand is the converted value.
                vector<string> prefix;
                while(expr->tokens.size() > 1 && (expr->tokens.front() == "(" || expr->tokens.front() == "~~"))
                    { prefix.push_back(expr->tokens.front()); expr->tokens.erase(expr->tokens.begin()); }
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
                if(!convFn || convFn->name != "operator()" || !convFn->params.empty() || !convFn->isEmitter || convFn->isExplicit) continue;
                string convertedType = convFn->returnType.name;
                if(typeMember* m2 = findMemberInHierarchy(cls, [&](typeMember* m){
                    auto* opFn = dynamic_cast<functionDef*>(m);
                    if(!opFn || opFn->name != opName) return false;
                    if(opFn->isPrePassStub) return true; // pre-scan stub: match by name only
                    return !opFn->params.empty() && opFn->params[0]->type.name == convertedType;
                })){
                    matchedOp = dynamic_cast<functionDef*>(m2);
                    if(auto* convBlk = dynamic_cast<i6Block*>(convFn->body)){
                        emitterBindings cb; cb.self = rhsText; cb.val = rhsText; cb.trim = emitterTrim::wsSemi;
                        string convBody = expandEmitterBody(convBlk, cb);
                        if(!convBody.empty()) rhsText = convBody;
                    }
                    break;
                }
            }
        }
    }

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
        while(expr->tokens.size() > 1 && (expr->tokens.front() == "(" || expr->tokens.front() == "~~"))
            { prefix.push_back(expr->tokens.front()); expr->tokens.erase(expr->tokens.begin()); }
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
    } else if(matchedOp && !matchedOp->isEmitter && !matchedOp->isPrePassStub){
        // Non-emitter operator. A `static` one has no receiver — it emits as a FREE routine
        // taking both operands, which is also what makes it usable when the LHS is a bare word
        // (a packed literal has nothing to send a message to). An instance one is a message
        // send on the LHS via its mangled property name.
        // Separate leading structural parens from the operand, as the emitter branch does —
        // otherwise `(a <=> b) < 0` folds the '(' into the LHS and emits `routine((a, b))`.
        vector<string> prefix;
        // `!(` arrives as its own `~~` ahead of the parens; it applies to the whole group too.
        while(expr->tokens.size() > 1 && (expr->tokens.front() == "(" || expr->tokens.front() == "~~"))
            { prefix.push_back(expr->tokens.front()); expr->tokens.erase(expr->tokens.begin()); }
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
            call = i6Emitter::staticRoutineName(cls, matchedOp) + "(" + heldLhs + ", " + heldRhs + ")";
        else {
            // A message send binds tighter than any I6 operator, so a receiver that is more than a
            // name (`boxes-->(0+1)`) must be grouped or the send lands on its last operand.
            bool bare = all_of(heldLhs.begin(), heldLhs.end(), [](char c){ return isalnum((unsigned char)c) || c == '_' || c == '.'; });
            call = (bare ? heldLhs : "(" + heldLhs + ")") + "." + matchedOp->i6name + "(" + heldRhs + ")";
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
    return true;
}

// ===============================================================================
// parseExpression sub-functions: ternary, null coalescing, function call, prefix !
// ===============================================================================
// ── parseExpression sub-functions ─────────────────────────────────────────────

// Ternary operator: condition ? trueExpr : falseExpr
// Lowers to if/else injection using a unique _bgl_tempN. Replaces expr contents and sets terminator.
void bglParser::parseExprTernary(expression* expr, const vector<string>& terminators, functionDef* func, statementBlock* body, int outerParenDepth){
    string tempName = format("_bgl_temp{0}", languageService.ternaryTempCount++);
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
    string tempName = format("_bgl_temp{0}", languageService.ternaryTempCount++);
    string lhsText = expr->text();
    string lhsType = expr->resolvedType;
    classDef* lhsCls = !lhsType.empty() ? getDispatchClass(lhsType) : nullptr;
    functionDef* nullTestFn = nullptr;
    if(lhsCls != nullptr)
        nullTestFn = dynamic_cast<functionDef*>(findMemberInHierarchy(lhsCls, [](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn && fn->name == "?" && fn->isEmitter && fn->params.empty() && dynamic_cast<i6Block*>(fn->body) != nullptr;
        }));
    if(nullTestFn == nullptr)
        parsingError(format("Type '{0}' does not support null coalescing (no operator?() emitter)", lhsType));
    auto* blk = dynamic_cast<i6Block*>(nullTestFn->body);
    emitterBindings nb; nb.self = tempName; nb.val = tempName; nb.trim = emitterTrim::wsSemi;
    string nullTest = expandEmitterBody(blk, nb);
    expression* fallback = parseExpression(file.getToken(), terminators, func, body);
    string injText = tempName + " = " + lhsText + "; if (~~(" + nullTest + ")) " + tempName + " = " + fallback->text() + ";";
    i6RawNode* inj = new i6RawNode();
    inj->text = injText;
    inj->cooked = true;   // built from Beguile expressions: locals take their renames
    pendingInjections.push_back(inj);
    expr->tokens.clear();
    expr->tokens.push_back(tempName);
    if(!fallback->resolvedType.empty()) expr->resolvedType = fallback->resolvedType;
    expr->terminator = fallback->terminator;
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
    rejectInterpolatedArgsInExpression(pal, callName);
    // Resolve and validate
    string retType;
    functionDef* staticSelf = nullptr;   // a static method called by its bare name inside its class
    if(isSelfCall){
        // Beguile names are case-insensitive; canonical form is lowercased. callName may
        // be the user's case-preserved spelling (camelCase, etc.), so normalize for compare.
        string canonName = callName;
        transform(canonName.begin(), canonName.end(), canonName.begin(), ::tolower);
        // Recursive self-call: currentFunc is the function being parsed and isn't yet in
        // currentObject->members. Use it directly for retType when names match.
        if(currentFunc != nullptr && currentFunc->name == canonName)
            retType = currentFunc->returnType.name;
        functionDef* selfMethod = nullptr;
        if(currentObject != nullptr)
            for(typeMember* m : currentObject->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == canonName){ selfMethod = fd; if(retType.empty()) retType = fd->returnType.name; break; }
        // Inherited: resolved on self's type, so overloads bind by their arguments.
        if(selfMethod == nullptr && !(currentFunc != nullptr && currentFunc->name == canonName)){
            string selfType = currentObject ? currentObject->name : (currentClass ? currentClass->name : string());
            if(!selfType.empty())
                if(functionDef* m = resolveMethod(selfType, "self", canonName, pal.args).method){
                    selfMethod = m;
                    if(retType.empty()) retType = m->returnType.name;
                }
        }
        // If the self-target is an emitter, inline its body here so the call site
        // splices the raw I6 expression instead of dispatching through self.X(...).
        // Without this, sibling emitters become I6 property calls and lose their
        // inline-substitution semantics.
        functionDef* target = selfMethod != nullptr ? selfMethod
                            : currentFunc != nullptr && currentFunc->name == canonName ? currentFunc : nullptr;
        if(target != nullptr && target->isStatic && !target->isEmitter) staticSelf = target;
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
    } else {
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
        parsingError(format("Cannot use void function '{0}' in an expression", callName));
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
        if(resolvedGlobal != nullptr && !resolvedGlobal->i6name.empty() && !resolvedGlobal->isEmitter)
            callEmit = resolvedGlobal->i6name;
        else if(resolvedGlobal != nullptr && isUsingImportedValueEmitter(callName))
            ;   // the call is the global function's; a value emitter of the same name is never called
        else if(resolvedGlobal != nullptr && languageService.findGlobalAs<functionDef>(callName) == resolvedGlobal)
            ;   // a global function, called by its own name: not a member of self that shares it
        else {
            string q = qualifyIdentifier(callName, func, body);
            if(!q.empty()) callEmit = q;
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
        callEmit = declaring != nullptr ? i6Emitter::staticRoutineName(declaring, staticSelf) : callName;
    }
    string calleeText = staticSelf != nullptr ? callEmit : isSelfCall ? "self." + callName : callEmit;
    vector<string> argTexts;
    vector<pair<string*, string>> operands;
    for(expression* a : pal.args) argTexts.push_back(a->text());
    for(size_t i = 0; i < pal.args.size(); i++) operands.push_back({&argTexts[i], pal.args[i]->resolvedType});
    if(HeldTemporaries held = holdCallTemporaries(operands); held.any()){
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
        // Check for !v? (negated postfix query) — only if type has operator?()
        if(file.peekToken(1).is("?")){
            classDef* cls = getDispatchClass(opType);
            functionDef* queryFn = nullptr;
            if(cls != nullptr)
                queryFn = dynamic_cast<functionDef*>(findMemberInHierarchy(cls, [](typeMember* m){
                    auto* fn = dynamic_cast<functionDef*>(m);
                    return fn && fn->name == "?" && fn->isEmitter && fn->params.empty() && dynamic_cast<i6Block*>(fn->body) != nullptr;
                }));
            if(queryFn != nullptr){
                file.getToken(); // consume '?'
                auto* blk = dynamic_cast<i6Block*>(queryFn->body);
                emitterBindings qb; qb.self = opText; qb.val = opText; qb.trim = emitterTrim::wsSemi;
                expr->tokens.push_back("~~(" + expandEmitterBody(blk, qb) + ")");
                if(expr->resolvedType.empty()) expr->resolvedType = queryFn->returnType.name;
                return true;
            }
        }
        // Try operator! emitter on the type
        classDef* cls = getDispatchClass(opType);
        if(cls){
            if(typeMember* m = findMemberInHierarchy(cls, [&](typeMember* tm){
                auto* fn = dynamic_cast<functionDef*>(tm);
                return fn && fn->name == "!" && fn->params.empty() && fn->isEmitter;
            })){
                functionDef* notOp = dynamic_cast<functionDef*>(m);
                i6Block* blk = dynamic_cast<i6Block*>(notOp->body);
                if(blk){
                    emitterBindings nob; nob.self = opText; nob.val = opText; nob.trim = emitterTrim::ws;
                    expr->tokens.push_back(expandEmitterBody(blk, nob));
                    if(expr->resolvedType.empty()) expr->resolvedType = notOp->returnType.name;
                    return true;
                }
            }
            // Non-emitter operator!: dispatch through its mangled routine, the same way a
            // non-emitter operator= or conversion operator() does. Without this the declaration
            // is accepted and then ignored, and the fallback below emits `~~obj` — I6's not of
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
        }
    }
    // Fallback: emit ~~ (I6 NOT) and put operand back for normal processing
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
    if(opTok != "&&" && opTok != "||"){
        expr->tokens.push_back(opTok);
        // A comparison yields a truth value whatever it compares (`typeof(x) == eType.int`).
        static const std::set<string> comparisons = {"==", "!=", "~=", "<", ">", "<=", ">="};
        if(comparisons.count(opTok)) expr->resolvedType = "bool";
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
    string text;
    for(size_t i = mark; i < pendingInjections.size(); i++)
        text += dynamic_cast<i6RawNode*>(pendingInjections[i])->text + " ";
    pendingInjections.resize(mark);
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
    string falseSetUp = takeBranchSetUp(pt.falseMark);
    string injText = "if (" + pt.condText + ") { " + pt.trueSetUp + pt.tempName + " = " + pt.trueText
                   + "; } else { " + falseSetUp + pt.tempName + " = " + falseText + "; }";
    i6RawNode* inj = new i6RawNode();
    inj->text = injText;
    inj->cooked = true;   // built from Beguile expressions: locals take their renames
    pendingInjections.push_back(inj);
    expr->tokens.clear();
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
        if(isTypeCompatible(ft, tt))      expr->resolvedType = pt.trueType;
        else if(isTypeCompatible(tt, ft)) expr->resolvedType = falseType;
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
    classDef* srcCls = getDispatchClass(srcType);
    if(srcCls == nullptr) return srcText;
    typeMember* found = findMemberInHierarchy(srcCls, [&](typeMember* m){
        auto* fn = dynamic_cast<functionDef*>(m);
        return fn && fn->name == "operator()" && fn->params.empty()
               && fn->isEmitter && fn->returnType.name == targetType
               && dynamic_cast<i6Block*>(fn->body) != nullptr;
    });
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
            return srcText + "." + fn->i6name + "()";
        }
        return srcText;
    }
    auto* fn = dynamic_cast<functionDef*>(found);
    auto* blk = dynamic_cast<i6Block*>(fn->body);
    emitterBindings cv; cv.self = srcText; cv.val = srcText; cv.trim = emitterTrim::wsSemi;
    string b = expandEmitterBody(blk, cv);
    if(b.empty()) return srcText;
    if(b.find("$target") != string::npos){
        string tempName = format("_bgl_temp{0}", languageService.ternaryTempCount++);
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
    file.getToken();                                  // consume "::operator"
    token opTok = file.getToken();
    string opName = opTok.value;
    // Multi-token operators the lexer splits (e.g. "[" "]") are not referenceable
    // here; the named binary/comparison operators are what generic code wants.
    if(find(languageService.operators.begin(), languageService.operators.end(), opName)
       == languageService.operators.end())
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
        if(!segs.empty())
            parsingError(format("An interpolated string ($\"…\") can be passed to '{0}' only in a call that "
                "is a statement of its own, such as `{0}($\"…\");`, not inside an expression.", callee));
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
        else if((p1.is(eTokenType::dataType) || p1.is(eTokenType::identifier)) && file.peekToken(2).is(eTokenType::identifier))
            isLambda = true;
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
    // `((T)obj).member`: the cast retypes the receiver, so the member is T's (for a method of a
    // strict ancestor, T's own version runs). In a chain, `((A)(B)obj)` or `((A)((B)obj))`, the
    // outermost cast is the receiver's type.
    {
        int k = 1, groups = 0, casts = 0;
        string outer;
        while(true){
            if(file.peekToken(k).is(token::parenOpen) && file.peekToken(k + 1).is(eTokenType::dataType)
               && file.peekToken(k + 2).is(token::parenClose)){
                if(outer.empty()) outer = file.peekToken(k + 1).value;
                casts++; k += 3;
            } else if(casts > 0 && file.peekToken(k).is(token::parenOpen)){
                groups++; k++;
            } else break;
        }
        bool matches = casts > 0 && file.peekToken(k).is(eTokenType::name);
        for(int g = 0; matches && g <= groups; g++) matches = file.peekToken(k + 1 + g).is(token::parenClose);
        if(matches && file.peekToken(k + 2 + groups).is(token::period)){
            for(int i = 1; i < k; i++) file.getToken();
            castType = outer;
            cur = file.getToken();
            for(int g = 0; g <= groups; g++) file.getToken(token::parenClose);
            st.receiverCast = true;
            return ExprStep::Continue;
        }
    }
    // Check for cast expression: (TypeName)expr
    if(file.peekToken(1).is(eTokenType::dataType) && file.peekToken(2).is(token::parenClose)){
        castType = file.getToken(eTokenType::dataType).value;
        file.getToken(token::parenClose);
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
    // but type-checked against the named instance). `(...)` operand excluded — write `((x)y)`.
    {
        token p1c = file.peekToken(1);
        if(p1c.is(eTokenType::identifier) && file.peekToken(2).is(token::parenClose)
           && languageService.findObjectType(p1c.value) != nullptr){
            token p3c = file.peekToken(3);
            bool operandFollows = p3c.is(eTokenType::identifier) || p3c.is(eTokenType::dataType)
                || p3c.is(eTokenType::integer) || p3c.is(eTokenType::quote) || p3c.is(eTokenType::rawQuote)
                || p3c.is(eTokenType::charLiteral) || p3c.is(eTokenType::dictionaryWord);
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
    if(!parenCastStack.empty() && parenCastStack.back().parenDepthAtPush == parenDepth){
        PendingParenCast pc = parenCastStack.back();
        parenCastStack.pop_back();
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
        expr->tokens.push_back(call + ")");
        if(t.rfind("func<", 0) == 0){
            size_t lt = t.find('<'), comma = t.find(',', lt), gt = t.rfind('>');
            expr->resolvedType = (comma != string::npos && comma < gt) ? t.substr(lt+1, comma-lt-1) : t.substr(lt+1, gt-lt-1);
        }
        else if(t == "func") expr->resolvedType = "var";
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
        bool isNegated = (expr->tokens.size() == 1 && expr->tokens[0] == "-");
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
    classDef* cls = getDispatchClass(typeTok.value);
    if(cls == nullptr)
        parsingError(format("'new {0}': '{0}' is not a class", typeTok.originalValue.empty() ? typeTok.value : typeTok.originalValue));
    if(cls->poolSize == 0)
        parsingError(format("'new {0}': class is not pooled. Declare with `class {0}[N]` (sized pool) or `extern class {0}[]` (extern marker) to enable allocation.", cls->dName()));
    file.getToken(token::parenOpen);
    ParsedArgList pal = parseCallArgList(func, body);
    rejectInterpolatedArgsInExpression(pal, "new");
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
bglParser::ExprStep bglParser::parseExprSubscript(ExprParseState& st, token& next){
    expression* expr = st.expr;
    token& cur = st.cur;
    string& castType = st.castType;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    string arrName = cur.value;
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
    if(getMethod == nullptr) {
        if(elemType.empty())
            parsingError(format("Subscript on '{0}': no declared element type. Declare as array<T>.", arrName));
        parsingError(format("No operator[] returning '{0}' on type '{1}'. Add an overload or use a supported element type.",
            typeDisplayName(elemType), typeDisplayName(arrType)));
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
        arrName = memberOwner + "." + memberProp;   // read the pointer, not the slot
        isMemberWordArray = false;
    }
    if(i6ExprMemberArrays.count(arrName)){
        isMemberWordArray = true;
        subscriptText = arrName + ".&$prop-->(" + indexExpr->text() + ")";
        expr->tokens.push_back(subscriptText);
    }
    else if(isMemberWordArray){
        subscriptText = memberOwner + ".&" + propertyI6Name(memberProp) + "-->(" + indexExpr->text() + ")";
        expr->tokens.push_back(subscriptText);
    }
    else if(getMethod->isEmitter)
        if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)) {
            emitterBindings sb; sb.self = arrName; sb.val = arrName;
            sb.prop = (isWordArrayType(arrType) || arrType == "bytearray") ? "0" : "<$prop undefined>";
            sb.fn = getMethod; sb.args.push_back(indexExpr->text());
            subscriptText = expandEmitterBody(blk, sb);
            expr->tokens.push_back(subscriptText);
        }
    if(subscriptText.empty() && !isMemberWordArray && !getMethod->isEmitter){
        // A routine-bodied operator [] is a method: send it.
        string routine = getMethod->i6name.empty() ? mangleOperatorName(getMethod->name) : getMethod->i6name;
        subscriptText = arrName + "." + routine + "(" + indexExpr->text() + ")";
        expr->tokens.push_back(subscriptText);
    }
    // Continuation: chained subscript on an array-of-arrays — `grid[i][j]`. Each step
    // applies operator[] to the PRIOR subscript VALUE (a pointer to an inner array),
    // which I6's `-->` needs as an explicit, parenthesized address operand. The inner
    // element type comes from the current array<...> result type, and the same emitter
    // body is re-substituted with $val = the pointer expression.
    while(!subscriptText.empty() && file.peekToken().is(token::bracketOpen)
          && (isArrayOfArraysElement(expr->resolvedType) || expr->resolvedType == "bytearray")){
        file.getToken(); // consume '['
        string curType   = expr->resolvedType;
        string innerElem = curType == "bytearray" ? "char" : arrayInnerType(curType);
        expression* idx  = parseExpression(file.getToken(), {token::bracketClose}, func, body);
        classDef* curCls = getDispatchClass(curType);
        functionDef* getM = (curCls != nullptr && !innerElem.empty())
                          ? findArraySubscriptOp(curCls, innerElem, /*isWrite=*/false) : nullptr;
        i6Block* blk = getM != nullptr ? dynamic_cast<i6Block*>(getM->body) : nullptr;
        if(getM == nullptr || !getM->isEmitter || blk == nullptr)
            parsingError(format("No operator[] returning '{0}' on type '{1}' for chained subscript.",
                                typeDisplayName(innerElem), typeDisplayName(curType)));
        string recv = "(" + subscriptText + ")";
        emitterBindings ib; ib.self = recv; ib.val = recv;
        ib.prop = "0";   // recv is a pointer to a tracked/byte array
        ib.fn = getM; ib.args.push_back(idx->text());
        subscriptText = expandEmitterBody(blk, ib);
        if(!expr->tokens.empty()) expr->tokens.pop_back();
        expr->tokens.push_back(subscriptText);
        expr->resolvedType = innerElem;
    }
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
        string recv = "(" + subscriptText + ")";
        string elemType = expr->resolvedType;
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
                const string& callName = method->i6name.empty() ? member.value : method->i6name;
                string call = recv + "." + callName + "(";
                for(size_t pi = 0; pi < pal.args.size(); pi++){
                    if(pi > 0) call += ", ";
                    call += pal.args[pi]->text();
                }
                call += ")";
                expr->tokens.push_back(call);
            }
            if(!method->returnType.name.empty()) expr->resolvedType = method->returnType.name;
        } else {
            // Property access: arr[0].field
            classDef* elemCls = getDispatchClass(elemType);
            if(elemCls != nullptr){
                function<string(classDef*)> findPropType = [&](classDef* c) -> string {
                    for(typeMember* m : c->members)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                            if(vd->name == member.value) return vd->type.name;
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
            if(memberArr != nullptr && !memberArr->isRefLocal && file.peekToken().is(token::bracketOpen)
               && isWordArrayType(expr->resolvedType)){
                file.getToken();   // '['
                expression* innerIndex = parseExpression(file.getToken(), {token::bracketClose}, func, body);
                if(!expr->tokens.empty()) expr->tokens.pop_back();
                expr->tokens.push_back("(" + recv + ").&" + memberI6Name(elemType, member.value)
                                       + "-->(" + innerIndex->text() + ")");
                expr->resolvedType = memberArr->elementType;
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
            } else if(functionDef* g = castType.empty() ? nativeGetterOf(expr->resolvedType) : nullptr){
                expr->tokens.push_back(recv + "." + memberI6Name(elemType, member.value) + "." + g->i6name + "()");
                expr->resolvedType = g->returnType.name;
            } else
                expr->tokens.push_back(recv + "." + memberI6Name(elemType, member.value));
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
    if(callName == "replaced" && currentFunc && currentFunc->replaceFoundNothing)
        parsingError(format("replaced() has nothing to call: no '{0}' is declared before this replace. A replace "
            "follows what it replaces; move it below '{0}' (or below the include that declares it).", currentFunc->dName()));
    if(callName == "replaced" && currentFunc && !currentFunc->replacedTarget.empty()){
        callName = currentFunc->replacedTarget;
        currentFunc->replacedWasCalled = true;
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
    if(func != nullptr && !forceGlobal && callName.find('.') == string::npos){
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
        if(!isSelfCall && selfClass != nullptr){
            // Check if name also exists as a global function — if so, defer to global resolution
            bool isGlobalFunc = false;
            if(auto* fd = languageService.findGlobalAs<functionDef>(callName)) isGlobalFunc = true;
            if(!isGlobalFunc){
                isSelfCall = selfClass->findMember([&](typeMember* m){
                    auto* fd = dynamic_cast<functionDef*>(m);
                    return fd != nullptr && fd->name == callName;
                }) != nullptr;
            }
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
    if(parseExprFunctionCall(expr, callName, isSelfCall, func, body)) {
        applyResultCast();
        // Emitter was inlined — advance past the function call and continue
        cur = exprNext(st);
        return ExprStep::Continue;
    }
    applyResultCast();
    return ExprStep::Advance;
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
    string optTemp = format("_bgl_temp{0}", languageService.ternaryTempCount++);
    string lhsName = cur.value;
    string lhsType;
    // A receiver already parsed (`owner.dog?.parent`) arrives as text + type; leading `(` tokens
    // belong to an enclosing group and stay in place ahead of the chain's result.
    vector<string> chainPrefix;
    if(!st.chainRecvText.empty()){
        lhsName = st.chainRecvText;
        lhsType = st.chainRecvType;
        while(!expr->tokens.empty() && (expr->tokens.front() == "(" || expr->tokens.front() == "~~")){
            chainPrefix.push_back(expr->tokens.front());
            expr->tokens.erase(expr->tokens.begin());
        }
        lhsName.clear(); for(const auto& t : expr->tokens) lhsName += t;
        st.chainRecvText.clear(); st.chainRecvType.clear();
    } else {
        lhsType = !castType.empty() ? castType : resolveIdentifierType(lhsName, func, body);
        // As this routine reaches it: a member through self, an outer local through a lambda's capture.
        if(!lhsType.empty() && func != nullptr)
            if(string q = qualifyIdentifier(lhsName, func, body); !q.empty()) lhsName = q;
    }
    castType = "";
    if(lhsType.empty()) parsingError(format("Unknown variable '{0}' in optional chain", lhsName));
    // Look up operator?() on LHS type for the null test
    classDef* lhsCls = getDispatchClass(lhsType);
    if(lhsCls == nullptr) parsingError(format("Type '{0}' does not support optional chaining (not a class)", lhsType));
    functionDef* nullTestFn = dynamic_cast<functionDef*>(findMemberInHierarchy(lhsCls, [](typeMember* m){
        auto* fn = dynamic_cast<functionDef*>(m);
        return fn && fn->name == "?" && fn->isEmitter && fn->params.empty() && dynamic_cast<i6Block*>(fn->body) != nullptr;
    }));
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
    auto chainPropertyRead = [&](const string& memberName, string& propType) -> string {
        if(classDef* cls = getDispatchClass(currentType))
            if(auto* vd = dynamic_cast<variableDeclaration*>(findMemberInHierarchy(cls, [&](typeMember* m){
                    auto* v = dynamic_cast<variableDeclaration*>(m); return v && v->name == memberName; })))
                propType = vd->type.name;
        string readRet;
        string read = expandMemberValueEmitter(optTemp, "", currentType, memberName, func, body, readRet);
        if(read.empty()) read = applyPropertyClassRead(optTemp, propType, readRet);
        if(read.empty()){
            string plain = optTemp + "." + memberI6Name(currentType, memberName);
            if(functionDef* g = nativeGetterOf(propType)){ propType = g->returnType.name; return plain + "." + g->i6name + "()"; }
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
            ParsedArgList opal = parseCallArgList(func, body);
            vector<expression*> callArgs = opal.args;
            MethodMatch mm = resolveMethodNamed(currentType, optTemp, methName, callArgs, opal.namedArgNames);
            functionDef* method = mm.method;
            if(!method) parsingError(format("No method '{0}' on type '{1}' in optional chain", methName, typeDisplayName(currentType)));
            mangleOverloadSetForReceiver(currentType, methName);
            if(method->isEmitter){
                if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                    emitterBindings mb; mb.self = optTemp; mb.val = optTemp; mb.trim = emitterTrim::wsSemi;
                    mb.fn = method;
                    for(expression* a : callArgs) mb.args.push_back(a->text());
                    for(expression* a : callArgs) mb.argTypes.push_back(a->resolvedType);
                    injText += " " + optTemp + " = " + expandEmitterBody(blk, mb) + ";";
                }
            } else {
                const string& callName = method->i6name.empty() ? methName : method->i6name;
                string call = optTemp + "." + callName + "(";
                for(size_t i = 0; i < callArgs.size(); i++){
                    if(i > 0) call += ", ";
                    call += callArgs[i]->text();
                }
                call += ")";
                injText += " " + optTemp + " = " + call + ";";
            }
            currentType = method->returnType.name;
            afterMember = exprNext(st);
        } else {
            // ?.property
            string propType = resolvePathType("_x_." + member.value, func, body);
            injText += " " + optTemp + " = " + chainPropertyRead(member.value, propType) + ";";
            currentType = propType;
        }
        // Check for continuation: another ?. or regular .
        if(afterMember.is("?.")){
            // Another optional step: look up operator?() on the current type
            classDef* nextCls = getDispatchClass(currentType);
            nullTestFn = nullptr;
            if(nextCls != nullptr)
                nullTestFn = dynamic_cast<functionDef*>(findMemberInHierarchy(nextCls, [](typeMember* m){
                    auto* fn = dynamic_cast<functionDef*>(m);
                    return fn && fn->name == "?" && fn->isEmitter && fn->params.empty() && dynamic_cast<i6Block*>(fn->body) != nullptr;
                }));
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
                ParsedArgList opal = parseCallArgList(func, body);
                vector<expression*> callArgs = opal.args;
                MethodMatch mm2 = resolveMethodNamed(currentType, optTemp, methName, callArgs, opal.namedArgNames);
                functionDef* method = mm2.method;
                if(!method) parsingError(format("No method '{0}' on type '{1}' in optional chain", methName, typeDisplayName(currentType)));
                mangleOverloadSetForReceiver(currentType, methName);
                if(method->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                        emitterBindings mb; mb.self = optTemp; mb.val = optTemp; mb.trim = emitterTrim::wsSemi;
                        mb.fn = method;
                        for(expression* a : callArgs) mb.args.push_back(a->text());
                        for(expression* a : callArgs) mb.argTypes.push_back(a->resolvedType);
                        injText += " " + optTemp + " = " + expandEmitterBody(blk, mb) + ";";
                    }
                } else {
                    const string& callName = method->i6name.empty() ? methName : method->i6name;
                    string call = optTemp + "." + callName + "(";
                    for(size_t i = 0; i < callArgs.size(); i++){ if(i > 0) call += ", "; call += callArgs[i]->text(); }
                    call += ")";
                    injText += " " + optTemp + " = " + call + ";";
                }
                currentType = method->returnType.name;
                afterMember = exprNext(st);
                // Could chain further — check again
                if(afterMember.is("?.") || afterMember.is(token::period)) { prefetched = afterMember; /* TODO: loop */ }
                else prefetched = afterMember;
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
    // Close all open if-braces
    {
        size_t opens = 0;
        for(size_t i = 0; i < injText.size(); i++)
            if(injText[i] == '{') opens++;
            else if(injText[i] == '}') opens--;
        for(size_t i = 0; i < opens; i++) injText += " }";
    }
    i6RawNode* inj = new i6RawNode();
    inj->text = injText;
    inj->cooked = true;   // built from Beguile expressions: locals take their renames
    pendingInjections.push_back(inj);
    expr->tokens.clear();
    for(const auto& p : chainPrefix) expr->tokens.push_back(p);
    expr->tokens.push_back(optTemp);
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
        else if(functionDef* g = nativeGetterOf(memberType)){
            accessText += "." + g->i6name + "()";
            memberType = g->returnType.name;
        }
    }
    if(expr->resolvedType.empty() && !memberType.empty()) expr->resolvedType = memberType;
    expr->tokens.push_back(accessText);
    // Capture host for $self substitution in rvalue property-class operators
    // (mirrors the non-self property-access path).
    expr->emitterSelf = selfText;
    return ExprStep::Advance;
}

// `obj.method(args)` — bind the call on the receiver's type and emit it.
bglParser::ExprStep bglParser::parseExprMemberCall(ExprParseState& st, token& member, token& afterMember){
    expression* expr = st.expr;
    token& cur = st.cur;
    string& castType = st.castType;
    functionDef* func = st.func;
    statementBlock* body = st.body;

    // method call in expression context: obj.method(args)
    string objName = cur.value;
    string rawObjName = cur.value;   // pre-qualify form, for inheritance-check ($self rewrite)
    string methName = member.value;
    // Pass memberHint=methName so the resolver prefers a candidate whose type
    // exposes the method, breaking name-collision ties (e.g. enum value vs
    // class instance with the same case-insensitive name).
    // The receiver's ACTUAL static type, independent of any cast. Needed to decide
    // whether an explicit cast is a genuine UPCAST (T an ancestor of the real type),
    // which is what triggers ancestor-qualified (static `::`) dispatch below.
    string actualType = resolveIdentifierType(objName, func, body, methName);
    // `(T)obj.method()` casts the call's result; only `((T)obj).method()` retypes the receiver.
    string resultCast;
    if(!castType.empty() && !st.receiverCast){ resultCast = castType; castType = ""; }
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
        if(isAncestorClass(getDispatchClass(explicitCast), getDispatchClass(actualType)))
            ancestorDispatchClass = getDispatchClass(explicitCast);
    if(objType.empty()) parsingError(format("Unknown variable '{0}'", objName));
    // Qualify objName for I6 emission: a #using-imported member like `glulx`
    // needs to emit as `bgl.glulx` (the actual property path) so I6 resolves
    // it correctly. Locals/globals qualify to themselves.
    if(func != nullptr){
        string qualified = qualifyIdentifier(objName, func, body, methName);
        if(!qualified.empty()) objName = qualified;
    } else {
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
    ParsedArgList pal = parseCallArgList(func, body, braceArgHints(argCandidates));
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
    string recvElemType = resolveArrayElementType(rawObjName, func, body);
    if(recvElemType.empty()) recvElemType = resolveArrayElementType(objName, func, body);
    method = bindMethodCall(objType, objName, methName,
                                           callArgs, pal.namedArgNames, pal.interpSegmentsPerArg,
                                           recvElemType);

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
    string exprPropValue = isWordArrayType(objType) ? "0" : "<$prop undefined>";

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
        const string callName = method->i6name.empty() ? memberI6Name(objType, methName) : method->i6name;
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
    if(!callText.empty() && file.peekToken().is(token::bracketOpen)){
        string chainResultType = expr->resolvedType;
        classDef* chainCls = getDispatchClass(chainResultType);
        string chainElem;
        size_t lt = chainResultType.find('<');
        if(lt != string::npos && !chainResultType.empty() && chainResultType.back() == '>')
            chainElem = chainResultType.substr(lt + 1, chainResultType.size() - lt - 2);
        if(chainElem.empty() && chainResultType == "bytearray") chainElem = "char";
        functionDef* getMethod = nullptr;
        if(chainCls != nullptr && !chainElem.empty())
            getMethod = findArraySubscriptOp(chainCls, chainElem, /*isWrite=*/false);
        if(getMethod != nullptr && getMethod->isEmitter){
            if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)){
                file.getToken(token::bracketOpen);
                expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, body);
                emitterBindings gb; gb.self = "(" + callText + ")"; gb.val = gb.self;
                gb.fn = getMethod; gb.args.push_back(indexExpr->text());
                string b = expandEmitterBody(blk, gb);
                expr->tokens.pop_back();  // drop bare call text
                expr->tokens.push_back(b);
                string retTypeName = getMethod->returnType.name;
                if(retTypeName == "t" || retTypeName == "T") retTypeName = chainElem;
                expr->resolvedType = retTypeName;
            }
        }
    }
    } // end of typed-receiver else
    // A cast whose target can't be a receiver type applies to what the call RETURNED.
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
    string propType = resolvePathType(objName + "." + propName, func, body);
    classDef* arrCls = getDispatchClass(propType);
    expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, body);
    string elemType = resolveArrayElementTypeDotted(objName, propName, func, body);
    if(elemType.empty() && propType == "bytearray") elemType = "char";
    functionDef* getMethod = nullptr;
    if(arrCls != nullptr && !elemType.empty())
        getMethod = findArraySubscriptOp(arrCls, elemType, /*isWrite=*/false);
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
        expr->tokens.push_back(owner + ".&" + propertyI6Name(propName) + "-->(" + indexExpr->text() + ")");
    }
    else if(getMethod->isEmitter)
        if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)) {
            // The receiver is the property READ `obj.prop` whenever the
            // property holds a POINTER rather than inline data: a byte-array
            // member (separate backing array), and a `ref` member (an array
            // owned elsewhere). Binding to bare `obj` would index the object.
            bool holdsPointer = (propType == "bytearray")
                             || memberArrayIsRef(objName, propName, func, body);
            string recv = holdsPointer ? (objName + "." + propName) : objName;
            emitterBindings mb; mb.self = recv; mb.val = recv; mb.prop = propName;
            mb.fn = getMethod; mb.args.push_back(indexExpr->text());
            expr->tokens.push_back(expandEmitterBody(blk, mb));
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
    // `((T)obj).member` reads T's member; `(T)obj.member` casts the value read (below).
    string receiverCastType;
    if(st.receiverCast){
        receiverCastType = castType;
        castType = "";
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
    if(!objText.empty() && castType.empty()){
        string veRet;
        string veText = expandMemberValueEmitter(objText, cur.value, recvType, member.value, func, body, veRet);
        if(!veText.empty()){
            expr->tokens.push_back(veText);
            if(expr->resolvedType.empty()) expr->resolvedType = veRet;
            expr->emitterSelf = cur.value;
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
    string accessText = computedProp.empty()
        ? objText + "." + memberI6Name(resolveIdentifierType(cur.value, func, body), member.value)
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
    if(castType.empty() && accessText == objText + "." + member.value)
        if(functionDef* fn = nativeGetterOf(propType)){
            accessText = accessText + "." + fn->i6name + "()";
            propType = fn->returnType.name;
        }
    // Cast precedence: `(T)obj.prop` means cast applies to the property
    // access result, not to the bare `obj`. If castType is set here, run
    // it through applyCastConversion against the property's resolved type.
    // For bit-compatible casts (uint↔int) this is a no-op text change; for
    // float casts it materializes a temp via the conversion emitter.
    if(!castType.empty()){
        string newText = applyCastConversion(accessText, propType, castType);
        expr->tokens.push_back(newText);
        expr->resolvedType = castType;
        castType = "";
    } else {
        if(expr->resolvedType.empty() && !propType.empty()) expr->resolvedType = propType;
        expr->tokens.push_back(accessText);
    }
    // Capture host text for $self substitution in rvalue property-class
    // operator emitters (parent($self), give $self $attr, etc.).
    expr->emitterSelf = cur.value;
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
        if(!shadowed)
            for(objectDef* imp : usingObjectImports)
                for(typeMember* m : imp->members)
                    if(auto* avd = dynamic_cast<variableDeclaration*>(m))
                        if(avd->name == cur.value){
                            string tgt = avd->declaredExpressionValue ? avd->declaredExpressionValue->text() : avd->type.name;
                            if(!tgt.empty()
                               && (languageService.findClass(tgt) != nullptr
                                   || languageService.findObjectType(tgt) != nullptr))
                                cur.value = tgt;
                        }
    }
    bool isEnum = languageService.findEnum(cur.value) != nullptr;
    classDef* maybeCls = getDispatchClass(cur.value);
    bool isStaticAccess = false;
    if(maybeCls != nullptr){
        for(typeMember* m : maybeCls->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                if(vd->isStatic && vd->name == member.value){
                    if(expr->resolvedType.empty()) expr->resolvedType = vd->type.name;
                    expr->tokens.push_back("_bgl_" + maybeCls->name + "_" + member.value);
                    isStaticAccess = true; break;
                }
    }
    // Value emitter member: expand body inline. Members can live on either
    // a class (Cls.member) or an object instance (obj.member, e.g. bgl.wordsize),
    // so we try classDef members first, then objectDef members.
    bool isValueEmitterAccess = false;
    auto tryInlineValueEmitter = [&](vector<typeMember*>& members) -> bool {
        for(typeMember* m : members)
            if(auto* fd = dynamic_cast<functionDef*>(m))
                if(fd->name == member.value && fd->isValueEmitter && fd->isEmitter){
                    if(auto* blk = dynamic_cast<i6Block*>(fd->body)){
                        emitterBindings vb; vb.self = cur.value; vb.val = cur.value; vb.trim = emitterTrim::wsSemi;
                        expr->tokens.push_back(expandEmitterBody(blk, vb));
                        if(expr->resolvedType.empty()) expr->resolvedType = fd->returnType.name;
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
                    if(vd->isExternal && vd->name == member.value &&
                       getDispatchClass(vd->type.name) != nullptr){
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
                          if(mcd && mcd->isEmitterClass
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
                            auto* cd = getDispatchClass(vd->type.name);
                            if(cd && cd->isEmitterClass){
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
        if(!castType.empty()){
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
    string varName = cur.value;
    string varType = resolveIdentifierType(varName, func, body);
    classDef* cls = !varType.empty() ? languageService.classOf(varType) : nullptr;
    functionDef* queryFn = nullptr;
    if(cls != nullptr && !qIsTerminator)
        queryFn = dynamic_cast<functionDef*>(findMemberInHierarchy(cls, [](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn && fn->name == "?" && fn->isEmitter && fn->params.empty() && dynamic_cast<i6Block*>(fn->body) != nullptr;
        }));
    if(queryFn != nullptr){
        // Type supports postfix ? — inline the emitter
        string qualified = func != nullptr ? qualifyIdentifier(varName, func, body) : varName;
        if(qualified.empty()) parsingError(format("Undeclared identifier '{0}'", varName));
        auto* blk = dynamic_cast<i6Block*>(queryFn->body);
        emitterBindings qb; qb.self = qualified; qb.val = qualified; qb.trim = emitterTrim::wsSemi;
        expr->tokens.push_back(expandEmitterBody(blk, qb));
        if(expr->resolvedType.empty()) expr->resolvedType = queryFn->returnType.name;
    } else {
        // No operator?() — put ? back for ternary handling
        prefetched = next;
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
        parsingError(format(
            "Unexpected identifier '{0}' following '{1}'. Did you mean `{1}.{0}(...)` or are you missing an operator between them?",
            next.value, cur.value));
    }
    prefetched = next;
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
            string newText = applyCastConversion(qualified, srcType, castType);
            expr->tokens.push_back(newText);
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
            if(!read.empty()){
                if(expr->resolvedType.empty() || expr->tokens.empty()) expr->resolvedType = readRet;
                expr->tokens.push_back(read);
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
    // ── name[i]: subscript access ──
    if(next.is(token::bracketOpen)){
        return parseExprSubscript(st, next);
    }
    // ── name(args): function call in expression ──
    else if(next.is(token::parenOpen)){
        return parseExprCall(st, next);
    }
    // ── name?.: optional chaining (expression-level) ──
    else if(parenDepth == 0 && next.is("?.")){
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

    if(!expr->resolvedType.empty()){
        classDef* cls = languageService.classOf(expr->resolvedType);
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
                 || file.peekToken(1).is(token::bracketOpen));
    // A plain name whose type `int` can subtract from (a float) is negated through that operator.
    if(!path && op == "-" && operand.is(eTokenType::name)){
        string t = resolveIdentifierType(operand.value, st.func, st.body);
        path = !t.empty() && negationThroughInt(t) != nullptr;
    }
    if(!path && !operand.is(token::parenOpen)) return false;
    vector<string> terms = *st.terminators;
    for(const string& o : kPrecedenceOps) terms.push_back(o);
    for(const char* t : {"?", ":", ",", "]"}) terms.push_back(t);
    if(st.parenDepth > 0) terms.push_back(token::parenClose);
    expression* sub = parseExpression(operand, terms, st.func, st.body);
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
    if(op == "!"){
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

    // An emitter value on the receiver's type (`holder.items.size`, `box.children.length`).
    {
        string selfText = receiverText(expr);
        string veRet;
        string veText = expandMemberValueEmitter(selfText, selfText.find('(') == string::npos ? selfText : "",
                                                 expr->resolvedType, member.value, func, st.body, veRet);
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
        {
            if(functionDef* fn = nativeGetterOf(mtype)){
                string selfText = receiverText(expr);
                replaceReceiver(expr, selfText + "." + member.value + "." + fn->i6name + "()");
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
           ad != nullptr && afterMember.is(token::bracketOpen) && isWordArrayType(mtype) && !ad->isRefLocal){
            string owner = parenthesizeReceiver(receiverText(expr));
            expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, st.body);
            replaceReceiver(expr, owner + ".&" + memberI6Name(expr->resolvedType, member.value) + "-->(" + indexExpr->text() + ")");
            expr->resolvedType = ad->elementType;
            cur = exprNext(st);
            return ExprStep::Continue;
        }
        if(pm != nullptr){
            string recv = receiverText(expr), whole = expr->text();
            if(parenthesizeReceiver(recv) != recv && parenthesizeReceiver(whole) != whole)
                replaceReceiver(expr, "(" + recv + ")");
            expr->tokens.push_back("." + memberI6Name(expr->resolvedType, member.value));
            expr->resolvedType = mtype;
            // A property-class member reached later off this field needs the accumulated
            // path as its `$self`; refresh emitterSelf to the full receiver text.
            expr->emitterSelf = receiverText(expr);
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
    string chainProp = isWordArrayType(chainTypeName) ? "0" : "<$prop undefined>";
    if(chainIsMember){
        size_t d = selfText.rfind('.');
        chainSelf = selfText.substr(0, d);
        chainProp = selfText.substr(d + 1);
        // The receiver path often resolves to the bare base ("array") with no template,
        // so recover the element type from the owning object's property declaration.
        if(chainElem.empty())
            chainElem = resolveArrayElementTypeDotted(chainSelf, chainProp, func, body);
    }
    // size()/length() on a RAW member array (rawArray<T>, or array<dictionaryWord>, which
    // I6 owns) cannot route through _bglArray: the runtime's member branch assumes the
    // tracked layout and reads the trailing word as the length, which on a raw member is
    // the last ELEMENT. A raw member has no length word — its extent is the property's
    // own size, so both methods answer `obj.#prop/WORDSIZE`. I6 computes `#prop` after
    // an additive property has accumulated, so this is also correct across inheritance
    // layers. Mirrors the non-chained fast path above.
    string rawExtent;
    if(chainIsMember && (methName == "size" || methName == "length")
       && !memberArrayIsTracked(chainSelf, chainProp, func, body))
        rawExtent = "((" + chainSelf + ".#" + propertyI6Name(chainProp) + ")/WORDSIZE)";
    if(chainIsMember) rejectRawMemberLengthOp(chainSelf, chainProp, methName, func, body);

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
    } else {
        const string callName = method->i6name.empty() ? memberI6Name(chainTypeName, methName) : method->i6name;
        string heldSelf = selfText;
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
    if(!callText.empty() && file.peekToken().is(token::bracketOpen)){
        string chainResultType = expr->resolvedType;
        classDef* chainCls = getDispatchClass(chainResultType);
        string chainElem;
        size_t lt = chainResultType.find('<');
        if(lt != string::npos && !chainResultType.empty() && chainResultType.back() == '>')
            chainElem = chainResultType.substr(lt + 1, chainResultType.size() - lt - 2);
        if(chainElem.empty() && chainResultType == "bytearray") chainElem = "char";
        functionDef* getMethod = nullptr;
        if(chainCls != nullptr && !chainElem.empty())
            getMethod = findArraySubscriptOp(chainCls, chainElem, /*isWrite=*/false);
        if(getMethod != nullptr && getMethod->isEmitter){
            if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)){
                file.getToken(token::bracketOpen);
                expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, body);
                emitterBindings gb; gb.self = "(" + callText + ")"; gb.val = gb.self;
                gb.fn = getMethod; gb.args.push_back(indexExpr->text());
                string b = expandEmitterBody(blk, gb);
                expr->tokens.pop_back();  // drop bare call text
                expr->tokens.push_back(b);
                // Substitute T → chainElem in the subscript return type, so chained
                // expressions downstream see a concrete element type. operator[] on
                // a generic class returns T; without this, downstream dispatch
                // (e.g. print(arr.filter(p)[0])) would see the literal 'T'.
                string retTypeName = getMethod->returnType.name;
                if(retTypeName == "t" || retTypeName == "T") retTypeName = chainElem;
                expr->resolvedType = retTypeName;
            }
        }
    }
    return ExprStep::Advance;
}

// ─── DOT CHAINING on resolved expression: .method() or .property ─
bglParser::ExprStep bglParser::parseExprDotChain(ExprParseState& st){

    // Chained method call on result of prior expression: <expr>.method(args)
    // (member name may collide with a type name → accept dataType too).
    token member = file.getToken({eTokenType::identifier, eTokenType::dataType});
    token afterMember = exprNext(st);
    if(!afterMember.is(token::parenOpen)){
        return parseExprDotChainRead(st, member, afterMember);
    }
    return parseExprDotChainCall(st, member, afterMember);
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

    PendingTernary pt;
    pt.tempName = format("_bgl_temp{0}", languageService.ternaryTempCount++);
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
    ExprParseState st;
    st.expr            = new expression();
    st.parenDepth      = startParenDepth;
    st.startParenDepth = startParenDepth;
    st.cur             = firstToken;
    st.terminators     = &terminators;
    st.func            = func;
    st.body            = body;
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
    while(true){
        if(exprIsTerminator(st, cur)){ expr->terminator = cur.value; break; }
        if(cur.is(eTokenType::eof)){
            parsingError("Unexpected end of file inside expression");
        }

        ExprStep step = ExprStep::Advance;
        if(!cur.isString() && (cur.value == "~" || cur.value == "~~"))
            parsingError(format("'{0}' is an Inform 6 operator; Beguile's logical not is '!', and it has no bitwise not.",
                                cur.value));
        if((cur.is(eTokenType::identifier) || cur.is(eTokenType::dataType))
           && file.peekToken(1).value == "::operator")    step = parseExprOperatorRef(st);
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
        else if(cur.is("new"))                            step = parseExprNew(st);
        else if((cur.is(eTokenType::dataType) || cur.is(eTokenType::identifier))
                && getDispatchClass(cur.value) != nullptr && file.peekToken().is(token::braceOpen))
                                                          step = parseExprInlineObject(st);
        else if(cur.is(eTokenType::name))                 step = parseExprIdentifier(st);
        else if(cur.is(eTokenType::oper) && cur.value == "??" && parenDepth == 0){
            parseExprNullCoalescing(expr, terminators, func, body);
            step = ExprStep::Break;
        }
        else if(cur.is(eTokenType::oper))                 step = parseExprOperator(st);
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

    // Assemble any remaining pending ternaries (e.g. top-level ternary without parens)
    while(!pendingTernaries.empty()) exprAssembleTernary(st);
    return expr;
}
