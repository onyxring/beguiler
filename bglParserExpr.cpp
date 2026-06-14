// ═══════════════════════════════════════════════════════════════════════════════
// bglParserExpr.cpp — expression-level parsing for the Beguile compiler.
//
// Extracted from bglParser.cpp (Phase 1 of the refactor). All entry points are
// member methods of bglParser; this file holds their definitions only.
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
#include "bglParser.h"
#include "fileLexer.h"
#include "token.h"
#include "bglLanguageService.h"
#include "bglParserHelpers.h"

using namespace std;


// ═══════════════════════════════════════════════════════════════════════════════

// ===============================================================================
// parseLambdaExpr - lift lambda expression to a global functionDef
// ===============================================================================
// Parse a lambda expression. The opening '(' has already been consumed.
// Builds a lifted global functionDef, adds it to languageService.globals, returns its name.
string bglParser::parseLambdaExpr(functionDef* outerFunc, statementBlock* outerBody){
    functionDef& fd = *(new functionDef());
    fd.name = format("_bglLambda_{0}", lambdaCounter++);
    fd.isEmitter = false;
    fd.isExternal = false;
    fd.src = file.currentLocation();

    // Parse parameter list: (type name, type name, ...) or ()
    token t = file.getToken(); // first type token or ')'
    while(t.isNot(token::parenClose)){
        paramDef& p = *(new paramDef());
        string typeName = t.value;
        if(typeName == "func") typeName = parseFuncType();
        p.type = languageService.getType(typeName);
        if(p.type.name.empty()) p.type.name = typeName; // for func<...> or unknown types
        token nameTok = file.getToken(eTokenType::identifier);
        p.name = nameTok.value;
        fd.params.push_back(&p);
        t = file.getToken(); // ',' or ')'
        if(t.is(",")) t = file.getToken(); // advance to next type
    }

    // Consume =>
    file.getToken("=>");

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
    activeBlockStack.clear();

    token bodyStart = file.getToken();
    if(bodyStart.is(token::braceOpen)){
        // Block body
        functionDef* savedFunc = currentFunc;
        currentFunc = &fd;
        openCompileContext(eCompileContext::codeBlock, lambdaBody);
        while(processNextStatement(fd) == false){}
        closeCompileContext(eCompileContext::codeBlock);
        currentFunc = savedFunc;
        fd.returnType.name = hasReturn(lambdaBody) ? "var" : "void";
    } else {
        // Single-expression body. Terminates at ';' (assignment RHS), ',' (next function
        // argument), or ')' (end of enclosing argument list). parseExpression's paren
        // tracking ensures inner `,`/`)` inside the body don't terminate prematurely.
        // For arg-position lambdas, the `,` or `)` terminator must be visible to the
        // enclosing arg parser — we stash it on the parser-level token slot, which the
        // caller's next getNext() will pick up before reading more from the lexer.
        expression* retExpr = parseExpression(bodyStart, {token::endStatement, token::comma, token::parenClose}, &fd, lambdaBody);
        // Stash whatever terminator the inner parseExpression consumed so the enclosing
        // parser still sees it. Applies to all three: ';' (assignment RHS), ',' (next arg),
        // ')' (close of enclosing call). Without this, the outer parse loses sync and
        // misreads subsequent statements as part of the lambda's expression.
        if(retExpr->terminator == ";" || retExpr->terminator == "," || retExpr->terminator == ")"){
            token t;
            t.value = retExpr->terminator;
            t.tokenType = eTokenType::symbol;
            stashedToken = t;
        }
        returnStatement& ret = *(new returnStatement());
        ret.src = fd.src;
        ret.returnExpression = retExpr->text();
        lambdaBody->statements.push_back(&ret);
        fd.returnType.name = retExpr->resolvedType.empty() ? "var" : retExpr->resolvedType;
    }

    // Restore outer scope context
    lambdaOuterFunc = savedOuterFunc;
    lambdaOuterBody = savedOuterBody;
    activeBlockStack = savedBlockStack;
    if(!lambdaOuterFuncStack.empty()) lambdaOuterFuncStack.pop_back();

    // Emit capture globals — insert at front so they appear before functions in the I6 output.
    // If `self` was captured, inject `self = _bglCapN;` at the top of the lambda body so
    // all self.member references inside the lambda work via I6's assignable self pseudo-variable.
    for(auto& cap : fd.captures){
        variableDeclaration& capGlobal = *(new variableDeclaration());
        capGlobal.name = cap.globalName;
        capGlobal.type.name = cap.typeName;
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
static const vector<string> kPrecedenceOps = {
    "*","/","%","+","-","<<",">>","<","<=",">",">=","==","!=","?=","=~","&","^","|","&&","||"
};

// ===============================================================================
// applyBinaryOperator - binary operator emitter resolution + RHS parsing
// ===============================================================================
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
                string b = processBglConditionals(blk->i6Body);
                string lhsText = expr->text();
                string selfText = !expr->emitterSelf.empty() ? expr->emitterSelf : lhsText;
                b = i6Emitter::replaceWord(b, "$self", selfText);
                b = i6Emitter::replaceWord(b, "$val",  lhsText);
                if(cls != nullptr) b = i6Emitter::replaceWord(b, "$class", cls->i6Name());
                size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                expr->tokens.clear();
                expr->tokens.push_back(b);
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

    // Step 2: Find matching operator emitter
    // 'var' is the escape-hatch type — if either side is var, skip param-type checking
    // (treat the same as an empty/unknown rhsType: match by operator name alone).
    functionDef* matchedOp = nullptr;
    if(typeMember* m = findMemberInHierarchy(cls, [&](typeMember* m){
        auto* opFn = dynamic_cast<functionDef*>(m);
        if(!opFn || opFn->name != opName) return false;
        // Pre-scan stubs have no params — match by name only
        if(opFn->isPrePassStub) return true;
        return !opFn->params.empty() &&
               (rhsType.empty() || rhsType=="var" || opFn->params[0]->type.name==rhsType || opFn->params[0]->type.name=="var");
    })) matchedOp = dynamic_cast<functionDef*>(m);

    // LHS conversion fallback: if LHS has operator() → convertedType, retry operator search on that type.
    // The "converted-to-converted" clause (param type == convertedType) used to accept ANY rhsType
    // unconditionally — which silently passed type-incompatible comparisons like `int != property`
    // because both compile to bit-level I6 ops. Now we additionally require the RHS to be
    // compatible with convertedType, so the fallback only fires when the entire converted-to
    // operator signature genuinely accepts the call.
    if(!matchedOp){
        for(typeMember* m : cls->members){
            auto* convFn = dynamic_cast<functionDef*>(m);
            if(!convFn || convFn->name != "operator()" || !convFn->params.empty() || !convFn->isEmitter || convFn->isExplicit) continue;
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
                        string convBody = processBglConditionals(convBlk->i6Body);
                        size_t s = convBody.find_first_not_of(" \t\n\r"); if(s != string::npos) convBody = convBody.substr(s);
                        size_t e = convBody.find_last_not_of(" \t\n\r;"); if(e != string::npos) convBody = convBody.substr(0, e+1);
                        if(!convBody.empty()){
                            convBody = i6Emitter::replaceWord(convBody, "$self", rhsText);
                            convBody = i6Emitter::replaceWord(convBody, "$val",  rhsText);
                            rhsText = convBody;
                        }
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
        string lhsText = expr->text();
        expr->tokens.clear();
        expr->tokens.push_back(lhsText + opName + rhsText);
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
        while(expr->tokens.size() > 1 && expr->tokens.front() == "(")
            { prefix.push_back(expr->tokens.front()); expr->tokens.erase(expr->tokens.begin()); }
        string lhsText = expr->text();
        string b = processBglConditionals(blk->i6Body);
        { size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
          size_t e=b.find_last_not_of(" \t\n\r");  if(e!=string::npos) b=b.substr(0,e+1); }
        // Substitute parameters before $self to avoid double-substitution when
        // a parameter name matches the LHS identifier (e.g. emitter body "$self>=c"
        // with $self=c and param c='0' would incorrectly become '0'>='0' if $self is first).
        b = replaceWord(b, "$" + matchedOp->params[0]->name, rhsText);
        // $self = host of property access (parentProp's `parent($self) == $v` etc.).
        // $val  = full receiver expression as written (`obj.parent`, `5`, `localInt`).
        // For non-property contexts the two coincide.
        string selfText = !expr->emitterSelf.empty() ? expr->emitterSelf : lhsText;
        b = replaceWord(b, "$self", selfText);
        b = replaceWord(b, "$val",  lhsText);
        if(cls != nullptr) b = replaceWord(b, "$class", cls->i6Name());
        expr->tokens.clear();
        for(auto& p : prefix) expr->tokens.push_back(p);
        expr->tokens.push_back(b);
    } else if(matchedOp && !matchedOp->isEmitter && !matchedOp->isPrePassStub){
        // Non-emitter operator: emit as a method call on the LHS using a mangled property name
        string lhsText = expr->text();
        if(matchedOp->i6name.empty()) matchedOp->i6name = mangleOperatorName(matchedOp->name);
        if(matchedOp->returnType.name.empty() || matchedOp->returnType.name == "void")
            expr->resolvedType = cls->name;
        else
            expr->resolvedType = matchedOp->returnType.name;
        expr->tokens.clear();
        expr->tokens.push_back(lhsText + "." + matchedOp->i6name + "(" + rhsText + ")");
    } else {
        // No operator found on this type
        parsingError(format("No operator '{0}' on type '{1}' accepting '{2}'",
            opName, cls->dName(), typeDisplayName(rhsType.empty() ? "unknown" : rhsType)));
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
    expression* trueExpr  = parseExpression(file.getToken(), {":"}, func, body);
    expression* falseExpr = parseExpression(file.getToken(), terminators, func, body);
    string injText = "if (" + condText + ") " + tempName + " = " + trueExpr->text()
                   + "; else " + tempName + " = " + falseExpr->text() + ";";
    i6RawNode* inj = new i6RawNode();
    inj->text = injText;
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
    string nullTest = processBglConditionals(blk->i6Body);
    nullTest = replaceWord(nullTest, "$self", tempName);
    nullTest = replaceWord(nullTest, "$val",  tempName);
    { size_t s=nullTest.find_first_not_of(" \t\n\r"); if(s!=string::npos) nullTest=nullTest.substr(s);
      size_t e=nullTest.find_last_not_of(" \t\n\r;"); if(e!=string::npos) nullTest=nullTest.substr(0,e+1); }
    expression* fallback = parseExpression(file.getToken(), terminators, func, body);
    string injText = tempName + " = " + lhsText + "; if (~~(" + nullTest + ")) " + tempName + " = " + fallback->text() + ";";
    i6RawNode* inj = new i6RawNode();
    inj->text = injText;
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
    // Parse args as proper expressions (shared with statement-level path)
    ParsedArgList pal = parseCallArgList(func, body);
    // Resolve and validate
    string retType;
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
        // If the self-target is an emitter, inline its body here so the call site
        // splices the raw I6 expression instead of dispatching through self.X(...).
        // Without this, sibling emitters become I6 property calls and lose their
        // inline-substitution semantics.
        if(selfMethod && selfMethod->isEmitter){
            if(auto* blk = dynamic_cast<i6Block*>(selfMethod->body)){
                string b = processBglConditionals(blk->i6Body);
                b = replaceWord(b, "$self", "self");
                b = replaceWord(b, "$val",  "self");
                for(size_t i = 0; i < selfMethod->params.size() && i < pal.args.size(); i++){
                    paramDef* p = selfMethod->params[i];
                    const string& display = p->displayName.empty() ? p->name : p->displayName;
                    b = replaceWord(b, "$" + display, pal.args[i]->text());
                    if(display != p->name)
                        b = replaceWord(b, "$" + p->name, pal.args[i]->text());
                }
                size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                expr->tokens.push_back(b);
                if(expr->resolvedType.empty() && !retType.empty()) expr->resolvedType = retType;
                return true; // emitter inlined — caller should continue
            }
        }
    } else {
        GlobalCallBinding gcb = bindGlobalCall(callName, pal.args, pal.namedArgNames,
                                                 pal.interpSegmentsPerArg, func, body);
        if(!gcb.funcVarReturnType.empty())      retType = gcb.funcVarReturnType;
        else if(gcb.method != nullptr)          retType = gcb.method->returnType.name;
        else                                    retType = "var"; // loose mode: unresolved → opaque
        // Emitter inlining: substitute params and push as single token
        if(gcb.method && gcb.method->isEmitter){
            if(auto* blk = dynamic_cast<i6Block*>(gcb.method->body)){
                string b = processBglConditionals(blk->i6Body);
                for(size_t i = 0; i < gcb.method->params.size() && i < pal.args.size(); i++){
                    // Raw bodies preserve original case; substitute both forms.
                    paramDef* p = gcb.method->params[i];
                    const string& display = p->displayName.empty() ? p->name : p->displayName;
                    b = replaceWord(b, "$" + display, pal.args[i]->text());
                    if(display != p->name)
                        b = replaceWord(b, "$" + p->name, pal.args[i]->text());
                }
                size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                expr->tokens.push_back(b);
                if(expr->resolvedType.empty() && !retType.empty()) expr->resolvedType = retType;
                return true; // emitter inlined — caller should continue
            }
        }
    }
    if(retType == "void" && !allowVoidReturnExpr)
        parsingError(format("Cannot use void function '{0}' in an expression", callName));
    if(expr->resolvedType.empty() && !retType.empty()) expr->resolvedType = retType;
    // Flatten parsed args back to tokens for the enclosing expression
    expr->tokens.push_back(isSelfCall ? "self." + callName : callName);
    expr->tokens.push_back(token::parenOpen);
    for(size_t i = 0; i < pal.args.size(); i++){
        if(i > 0) expr->tokens.push_back(",");
        expr->tokens.push_back(pal.args[i]->text());
    }
    expr->tokens.push_back(token::parenClose);
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
                string b = processBglConditionals(blk->i6Body);
                b = replaceWord(b, "$self", opText);
                b = replaceWord(b, "$val",  opText);
                { size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
                  size_t e=b.find_last_not_of(" \t\n\r;"); if(e!=string::npos) b=b.substr(0,e+1); }
                expr->tokens.push_back("~~(" + b + ")");
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
                    string b = processBglConditionals(blk->i6Body);
                    size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
                    size_t e=b.find_last_not_of(" \t\n\r");  if(e!=string::npos) b=b.substr(0,e+1);
                    b = replaceWord(b, "$self", opText);
                    b = replaceWord(b, "$val",  opText);
                    expr->tokens.push_back(b);
                    if(expr->resolvedType.empty()) expr->resolvedType = notOp->returnType.name;
                    return true;
                }
            }
        }
    }
    // Fallback: emit ~~ (I6 NOT) and put operand back for normal processing
    expr->tokens.push_back("~~");
    prefetched = operand;
    return true; // always handled (either emitter or fallback)
}

// ── Namespace-scoped type resolution ─────────────────────────────────────────
// Walks a dotted path (e.g. "bgl.glulx.window") through namespace objects.
// At each intermediate step, follows value alias members (auto x = Obj;) to the
// next namespace object. At the final step, looks for an alias member (isAlias=true)

// ===============================================================================
// parseExpression - the main expression parser (~1450 lines)
// ===============================================================================
expression* bglParser::parseExpression(token firstToken, std::vector<std::string> terminators, functionDef* func, statementBlock* body, int startParenDepth){
    expression* expr = new expression();
    int parenDepth = startParenDepth;
    token cur = firstToken;
    optional<token> prefetched = nullopt;
    string castType;  // set when a (TypeName) cast prefix is detected
    // When a cast prefix is followed by '(', the cast applies to the result of the
    // parenthesized expression, not to the first identifier inside. Push castType
    // onto this stack on parenOpen and pop/apply on the matching parenClose. The
    // stack entry pairs the saved type with the parenDepth at which the cast was
    // queued, so nested casts like `(int)((float)f + (float)g)` resolve correctly.
    struct PendingParenCast { string castType; int parenDepthAtPush; };
    vector<PendingParenCast> parenCastStack;
    // RAII guard for currentExpectedType. parseExpression's body may set the expected type
    // (e.g. when entering an operator RHS). The guard restores it on any return path so
    // changes don't leak to the caller's scope.
    struct ExpectedTypeGuard {
        string& slot; string saved;
        ExpectedTypeGuard(string& s) : slot(s), saved(s) {}
        ~ExpectedTypeGuard() { slot = saved; }
    } _expectedTypeGuard(currentExpectedType);

    auto isTerminator = [&](const token& t) -> bool {
        if(parenDepth > startParenDepth) return false;  // inside our own parens — not a terminator
        for(const string& term : terminators)
            if(t.value == term) return true;
        return false;
    };
    auto getNext = [&]() -> token {
        if(prefetched.has_value()){ token t = *prefetched; prefetched = nullopt; return t; }
        if(stashedToken.has_value()){ token t = *stashedToken; stashedToken = nullopt; return t; }
        return file.getToken();
    };

    // Raw binary operator whose LHS has no emitter (boolean/non-class LHS, e.g. `true && …`,
    // `suppress == false && …`). The operator itself passes through as raw I6, but the RHS may
    // still contain emitter-class operations (e.g. `obj.parent == player`). Parse the RHS as its
    // own sub-expression so those resolve, then emit `op <rhs>`. Without this the RHS shares this
    // flat expression's stale (non-class) resolvedType and its operators never dispatch — they
    // leak out as raw I6 property access (`obj.parent` instead of `parent(obj)`). Mirrors the RHS
    // precedence sub-parse in applyBinaryOperator(). Non-precedence ops (level <0, e.g. compound
    // assign) keep the old single-token passthrough.
    auto emitRawBinaryOp = [&](const string& opTok){
        // Only logical && / || need the RHS sub-parse: their RHS is a full boolean sub-expression
        // that may contain emitter-class comparisons (`obj.parent == player`). Other raw operators
        // (arithmetic, bitwise, comparison) keep the historical token-by-token passthrough, so their
        // emission/spacing is unchanged — those RHSs don't open a fresh emitter dispatch context.
        if(opTok != "&&" && opTok != "||"){ expr->tokens.push_back(opTok); return; }
        int rawPrec = operatorPrecedence(opTok);
        if(rawPrec < 0){ expr->tokens.push_back(opTok); return; }
        vector<string> rhsTerminators = terminators;
        for(const string& op : kPrecedenceOps)
            if(operatorPrecedence(op) <= rawPrec)
                rhsTerminators.push_back(op);
        if(parenDepth > 0) rhsTerminators.push_back(token::parenClose);
        rhsTerminators.push_back("?");
        expression* rhsExpr = parseExpression(getNext(), rhsTerminators, func, body);
        expr->tokens.push_back(opTok);
        expr->tokens.push_back(rhsExpr->text());
        token terminatorTok;
        terminatorTok.value = rhsExpr->terminator;
        terminatorTok.tokenType = eTokenType::oper;
        if(rhsExpr->terminator == ";" || rhsExpr->terminator == ")" || rhsExpr->terminator == "?")
            terminatorTok.tokenType = eTokenType::symbol;
        prefetched = terminatorTok;
    };

    // Pending ternary state: when '?' is encountered, the condition and true branch are
    // captured, then the false branch is collected by continuing the main loop. This avoids
    // sub-parsing the false branch, which would lose the caller's paren tracking state.
    struct PendingTernary {
        string condText;
        string trueText;
        string trueType;
        string tempName;
        int parenDepthAtQuestion;  // paren depth when '?' was encountered
        vector<string> prefixParens;  // structural '(' tokens to restore after assembly
    };
    vector<PendingTernary> pendingTernaries;

    // Helper: assemble a pending ternary — creates injection, replaces expr with temp name
    auto assembleTernary = [&](){
        if(pendingTernaries.empty()) return;
        PendingTernary pt = pendingTernaries.back();
        pendingTernaries.pop_back();
        string falseText = expr->text();
        string falseType = expr->resolvedType;
        string injText = "if (" + pt.condText + ") " + pt.tempName + " = " + pt.trueText
                       + "; else " + pt.tempName + " = " + falseText + ";";
        i6RawNode* inj = new i6RawNode();
        inj->text = injText;
        pendingInjections.push_back(inj);
        expr->tokens.clear();
        // Restore structural prefix parens so the outer expression stays balanced
        for(auto& p : pt.prefixParens) expr->tokens.push_back(p);
        expr->tokens.push_back(pt.tempName);
        expr->resolvedType = !pt.trueType.empty() ? pt.trueType : falseType;
    };

    // Cast emission helper. When `(T)x` is parsed, the consumer site calls this with the
    // source's emitted text and resolved type plus the target type. If the source type has
    // an `operator() → T` emitter, its body is substituted and returned as the new text;
    // if the body uses `$target` (statement-form, e.g. `@numtof $val $target`), a temp slot
    // is allocated and the substituted body is queued as a side-effect injection. When no
    // matching emitter exists, the source text is returned unchanged — preserving the
    // historical relabel-only behavior for bit-compatible casts like int↔uint.
    auto applyCastConversion = [&](const string& srcText, const string& srcType,
                                    const string& targetType) -> string {
        if(targetType.empty() || srcType.empty() || srcType == targetType) return srcText;
        classDef* srcCls = getDispatchClass(srcType);
        if(srcCls == nullptr) return srcText;
        typeMember* found = findMemberInHierarchy(srcCls, [&](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn && fn->name == "operator()" && fn->params.empty()
                   && fn->isEmitter && fn->returnType.name == targetType
                   && dynamic_cast<i6Block*>(fn->body) != nullptr;
        });
        if(found == nullptr) return srcText;
        auto* fn = dynamic_cast<functionDef*>(found);
        auto* blk = dynamic_cast<i6Block*>(fn->body);
        string b = processBglConditionals(blk->i6Body);
        { size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
          size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1); }
        if(b.empty()) return srcText;
        b = i6Emitter::replaceWord(b, "$self", srcText);
        b = i6Emitter::replaceWord(b, "$val",  srcText);
        if(b.find("$target") != string::npos){
            string tempName = format("_bgl_temp{0}", languageService.ternaryTempCount++);
            b = i6Emitter::replaceWord(b, "$target", tempName);
            i6RawNode* inj = new i6RawNode();
            while(!b.empty() && b.back() == ';') b.pop_back();
            inj->text = b + ";";
            pendingInjections.push_back(inj);
            return tempName;
        }
        return b;
    };

    // ═══════════════════════════════════════════════════════════════════════
    // MAIN EXPRESSION LOOP — processes one token per iteration
    // Branches by token type: parens, literals, identifiers, operators, etc.
    // ═══════════════════════════════════════════════════════════════════════
    while(true){
        if(isTerminator(cur)){ expr->terminator = cur.value; break; }
        if(cur.is(eTokenType::eof)){
            parsingError("Unexpected end of file inside expression");
        }

        // ─── PARENS: open paren, lambda detection, cast prefix ───────────
        if(cur.is(token::parenOpen)){
            // Lambda detection: () => ... OR (type name, ...) => ...
            {
                bool isLambda = false;
                token p1 = file.peekToken(1);
                if(p1.is(")") && file.peekToken(2).is("=>"))
                    isLambda = true;
                else if((p1.is(eTokenType::dataType) || p1.is(eTokenType::identifier)) && file.peekToken(2).is(eTokenType::identifier))
                    isLambda = true;
                if(isLambda){
                    string lambdaName = parseLambdaExpr(func, body);
                    expr->tokens.push_back(lambdaName);
                    expr->resolvedType = "func";
                    cur = getNext();
                    continue;
                }
            }
            // Check for cast expression: (TypeName)expr
            if(file.peekToken(1).is(eTokenType::dataType) && file.peekToken(2).is(token::parenClose)){
                castType = file.getToken(eTokenType::dataType).value;
                file.getToken(token::parenClose);
                cur = getNext();
                continue;  // re-process the token after the cast with castType set
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
        }
        // ─── CLOSE PAREN ──────────────────────────────────────────────────
        else if(cur.is(token::parenClose)){
            if(parenDepth > startParenDepth) parenDepth--;
            // If a pending ternary was opened at this depth, assemble it now.
            // This handles `(cond ? a : b)` — the ')' closes the false branch.
            if(!pendingTernaries.empty() && parenDepth <= pendingTernaries.back().parenDepthAtQuestion){
                assembleTernary();
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
        }
        // ─── LITERALS: integer, string, char, dictionary word ─────────────
        else if(cur.is(eTokenType::integer)){
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
        }
        else if(cur.isString()){
            if(expr->resolvedType.empty()) expr->resolvedType = "stringliteral";
            expr->tokens.push_back(cur.value);
        }
        // `new TypeName(args)` — pool-class allocation. Emits as `TypeName.create(args)`.
        // The type must be a pooled class: declared with `[N]` or marker `extern class Foo[]`.
        // Returns `nothing` at runtime if the pool is exhausted.
        else if(cur.is("new")){
            token typeTok = file.getToken({eTokenType::dataType, eTokenType::identifier});
            classDef* cls = getDispatchClass(typeTok.value);
            if(cls == nullptr)
                parsingError(format("'new {0}': '{0}' is not a class", typeTok.originalValue.empty() ? typeTok.value : typeTok.originalValue));
            if(cls->poolSize == 0)
                parsingError(format("'new {0}': class is not pooled. Declare with `class {0}[N]` (sized pool) or `extern class {0}[]` (extern marker) to enable allocation.", cls->dName()));
            file.getToken(token::parenOpen);
            ParsedArgList pal = parseCallArgList(func, body);
            string call = cls->i6Name() + ".create(";
            for(size_t i = 0; i < pal.args.size(); i++){
                if(i > 0) call += ", ";
                call += pal.args[i]->text();
            }
            call += ")";
            expr->tokens.push_back(call);
            if(expr->resolvedType.empty()) expr->resolvedType = cls->name;
        }
        // ═── IDENTIFIER: the largest branch ═════════════════════════════
        // Handles: subscript name[i], function call name(args), optional
        // chain name?., dot-access name.member/name.method(), postfix
        // query name?, and plain identifier fallback.
        // ═════════════════════════════════════════════════════════════════
        else if(cur.is(eTokenType::name)){
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
                cur = getNext();
                continue;
            }
            // Namespaced enum value: bgl.glulx.winPlacement.above — resolve the prefix as a
            // namespace-scoped type, then bind the tail segment as one of the enum's named values.
            {   string emission, enumType;
                if(tryConsumeNamespacedEnumValue(cur, emission, enumType)){
                    if(expr->resolvedType.empty()) expr->resolvedType = enumType;
                    expr->tokens.push_back(emission);
                    cur = getNext();  // advance past the consumed value — the loop's normal getNext() is skipped by our continue
                    continue;
                }
            }
            token next = getNext();
            // ── name[i]: subscript access ──
            if(next.is(token::bracketOpen)){
                string arrName = cur.value;
                string arrType = resolveIdentifierType(arrName, func, body);
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
                if(expr->resolvedType.empty()) expr->resolvedType = getMethod->returnType.name;
                string subscriptText;
                // Member (property) WORD arrays use the orLibrary property-array convention:
                // a plain `with prop a b c` property accessed via `obj.&prop-->n` (0-indexed,
                // no count slot), NOT the count-prefixed global/table form. Qualifying the bare
                // name yields "owner.prop" for a member (the global/table emitter body's
                // `$val-->($i+1)` would otherwise index off the bare property id → garbage).
                string memberOwner, memberProp;
                bool isMemberWordArray = isWordArrayType(arrType) &&
                    splitQualifiedMember(arrName, func, body, memberOwner, memberProp);
                if(isMemberWordArray){
                    subscriptText = memberOwner + ".&" + memberProp + "-->(" + indexExpr->text() + ")";
                    expr->tokens.push_back(subscriptText);
                }
                else if(getMethod->isEmitter)
                    if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)) {
                        string b = processBglConditionals(blk->i6Body);
                        string pv = (isWordArrayType(arrType) || arrType == "bytearray") ? "0" : "<$prop undefined>";
                        b = replaceWord(b, "$self", arrName);
                        b = replaceWord(b, "$val",  arrName);
                        b = replaceWord(b, "$prop", pv);
                        if(!getMethod->params.empty())
                            b = replaceWord(b, "$" + getMethod->params[0]->name, indexExpr->text());
                        subscriptText = b;
                        expr->tokens.push_back(b);
                    }
                // Continuation: if next token is '.', handle dot-access on the subscript result
                // (e.g. arr[0].field or arr[0].method()). Resolve against the element type's class.
                if(!subscriptText.empty() && file.peekToken().is(token::period)){
                    file.getToken(); // consume '.'
                    string elemType = expr->resolvedType;
                    token member = file.getToken();
                    if(file.peekToken().is(token::parenOpen)){
                        // Method call: arr[0].method(args) — resolve via bindMethodCall
                        file.getToken(); // consume '('
                        ParsedArgList pal = parseCallArgList(func, body);
                        vector<string> namedArgNames = pal.namedArgNames;
                        vector<vector<interpolatedSegment>> interpSegs = pal.interpSegmentsPerArg;
                        functionDef* method = bindMethodCall(elemType, subscriptText, member.value,
                            pal.args, namedArgNames, interpSegs);
                        if(!expr->tokens.empty()) expr->tokens.pop_back();
                        if(method->isEmitter && !method->isPrePassStub){
                            if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                                string b = processBglConditionals(blk->i6Body);
                                for(size_t pi = 0; pi < method->params.size() && pi < pal.args.size(); pi++)
                                    b = replaceWord(b, "$" + method->params[pi]->name, pal.args[pi]->text());
                                b = replaceWord(b, "$self", subscriptText);
                                b = replaceWord(b, "$val",  subscriptText);
                                size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                                size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                                expr->tokens.push_back(b);
                            }
                        } else {
                            // Non-emitter method: emit as subscriptText.method(args)
                            const string& callName = method->i6name.empty() ? member.value : method->i6name;
                            string call = subscriptText + "." + callName + "(";
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
                        // Replace the subscript text with subscriptText.member
                        if(!expr->tokens.empty()) expr->tokens.pop_back();
                        expr->tokens.push_back(subscriptText + "." + member.value);
                    }
                }
            }
            // ── name(args): function call in expression ──
            else if(next.is(token::parenOpen)){
                string callName = cur.value;
                // replace chaining: replaced() resolves to the predecessor's mangled name
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
                if(func != nullptr && callName.find('.') == string::npos){
                    // Recursive self-call: the function being parsed isn't yet in
                    // currentObject->members (registered post-body), so match by name
                    // against currentFunc when we're inside an object/class method.
                    if((currentObject != nullptr || currentClass != nullptr) &&
                       currentFunc != nullptr && currentFunc->name == callName)
                        isSelfCall = true;
                    if(!isSelfCall && currentObject != nullptr)
                        for(typeMember* m : currentObject->members)
                            if(auto* fd = dynamic_cast<functionDef*>(m))
                                if(fd->name == callName){ isSelfCall = true; break; }
                    if(!isSelfCall && currentClass != nullptr){
                        // Check if name also exists as a global function — if so, defer to global resolution
                        bool isGlobalFunc = false;
                        for(typeDef* g : languageService.globals)
                            if(auto* fd = dynamic_cast<functionDef*>(g))
                                if(fd->name == callName){ isGlobalFunc = true; break; }
                        if(!isGlobalFunc){
                            function<bool(classDef*)> searchHierarchy = [&](classDef* c) -> bool {
                                for(typeMember* m : c->members)
                                    if(auto* fd = dynamic_cast<functionDef*>(m))
                                        if(fd->name == callName) return true;
                                for(classDef* base : c->baseClasses)
                                    if(searchHierarchy(base)) return true;
                                return false;
                            };
                            isSelfCall = searchHierarchy(currentClass);
                        }
                    }
                }
                // Note: '(' was already consumed by getNext() above
                if(parseExprFunctionCall(expr, callName, isSelfCall, func, body)) {
                    // Emitter was inlined — advance past the function call and continue
                    cur = getNext();
                    continue;
                }
            }
            // ── name?.: optional chaining (expression-level) ──
            else if(parenDepth == 0 && next.is("?.")){
                // Optional chaining: obj?.member or obj?.method(args)
                // Lowers to: _bgl_tempN = obj; if(nullTest) _bgl_tempN = _bgl_tempN.member;
                string optTemp = format("_bgl_temp{0}", languageService.ternaryTempCount++);
                string lhsName = cur.value;
                string lhsType = !castType.empty() ? castType : resolveIdentifierType(lhsName, func, body);
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
                    string b = processBglConditionals(blk->i6Body);
                    b = replaceWord(b, "$self", selfText);
                    b = replaceWord(b, "$val",  selfText);
                    size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                    size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                    return b;
                };
                // Build the guarded chain
                string injText = optTemp + " = " + lhsName + ";";
                string currentType = lhsType;
                // Process chain steps: each is ?.member, ?.method(), or a trailing .member/.method()
                while(true){
                    string nullTest = getNullTest(nullTestFn, optTemp);
                    injText += " if (" + nullTest + ") {";
                    token member = file.getToken(eTokenType::identifier);
                    token afterMember = getNext();
                    if(afterMember.is(token::parenOpen)){
                        // ?.method(args) — find method, inline emitter or call
                        string methName = member.value;
                        classDef* cls = getDispatchClass(currentType);
                        vector<expression*> callArgs;
                        token firstArg = file.getToken();
                        while(firstArg.isNot(token::parenClose)){
                            expression* arg = parseExpression(firstArg, {token::comma, token::parenClose}, func, body);
                            callArgs.push_back(arg);
                            if(arg->terminator == token::parenClose) break;
                            firstArg = file.getToken();
                        }
                        MethodMatch mm = resolveMethod(currentType, optTemp, methName, callArgs);
                        functionDef* method = mm.method;
                        if(!method) parsingError(format("No method '{0}' on type '{1}' in optional chain", methName, typeDisplayName(currentType)));
                        mangleOverloadSetForReceiver(currentType, methName);
                        if(method->isEmitter){
                            if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                                string b = processBglConditionals(blk->i6Body);
                                size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                                size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                                b = replaceWord(b, "$self", optTemp);
                                b = replaceWord(b, "$val",  optTemp);
                                for(size_t i = 0; i < method->params.size() && i < callArgs.size(); i++)
                                    b = replaceWord(b, "$" + method->params[i]->name, callArgs[i]->text());
                                injText += " " + optTemp + " = " + b + ";";
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
                        afterMember = getNext();
                    } else {
                        // ?.property — simple property access
                        string propType = resolvePathType("_x_." + member.value, func, body);
                        // We can't resolve the runtime path, so check the class for a member
                        classDef* cls = getDispatchClass(currentType);
                        if(cls != nullptr)
                            for(typeMember* m : cls->members)
                                if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                                    if(vd->name == member.value){ propType = vd->type.name; break; }
                        injText += " " + optTemp + " = " + optTemp + "." + member.value + ";";
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
                        token nextMember = file.getToken(eTokenType::identifier);
                        token afterNext = getNext();
                        if(afterNext.is(token::parenOpen)){
                            // .method(args) — build as non-guarded call
                            string methName = nextMember.value;
                            classDef* cls = getDispatchClass(currentType);
                            vector<expression*> callArgs;
                            token firstArg = file.getToken();
                            while(firstArg.isNot(token::parenClose)){
                                expression* arg = parseExpression(firstArg, {token::comma, token::parenClose}, func, body);
                                callArgs.push_back(arg);
                                if(arg->terminator == token::parenClose) break;
                                firstArg = file.getToken();
                            }
                            MethodMatch mm2 = resolveMethod(currentType, optTemp, methName, callArgs);
                            functionDef* method = mm2.method;
                            if(!method) parsingError(format("No method '{0}' on type '{1}' in optional chain", methName, typeDisplayName(currentType)));
                            mangleOverloadSetForReceiver(currentType, methName);
                            if(method->isEmitter){
                                if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                                    string b = processBglConditionals(blk->i6Body);
                                    size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                                    size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                                    b = replaceWord(b, "$self", optTemp);
                                    b = replaceWord(b, "$val",  optTemp);
                                    for(size_t i = 0; i < method->params.size() && i < callArgs.size(); i++)
                                        b = replaceWord(b, "$" + method->params[i]->name, callArgs[i]->text());
                                    injText += " " + optTemp + " = " + b + ";";
                                }
                            } else {
                                const string& callName = method->i6name.empty() ? methName : method->i6name;
                                string call = optTemp + "." + callName + "(";
                                for(size_t i = 0; i < callArgs.size(); i++){ if(i > 0) call += ", "; call += callArgs[i]->text(); }
                                call += ")";
                                injText += " " + optTemp + " = " + call + ";";
                            }
                            currentType = method->returnType.name;
                            afterMember = getNext();
                            // Could chain further — check again
                            if(afterMember.is("?.") || afterMember.is(token::period)) { prefetched = afterMember; /* TODO: loop */ }
                            else prefetched = afterMember;
                        } else {
                            // .property
                            classDef* cls = getDispatchClass(currentType);
                            string propType;
                            if(cls != nullptr)
                                for(typeMember* m : cls->members)
                                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                                        if(vd->name == nextMember.value){ propType = vd->type.name; break; }
                            injText += " " + optTemp + " = " + optTemp + "." + nextMember.value + ";";
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
                pendingInjections.push_back(inj);
                expr->tokens.clear();
                expr->tokens.push_back(optTemp);
                if(!currentType.empty()) expr->resolvedType = currentType;
            }
            // ── name.member: dot-access (property, method call, enum, static) ──
            else if(next.is(token::period)){
                token member = file.getToken(eTokenType::identifier);
                // Read afterMember here so both self and non-self paths share it.
                // For the self property-access case, put it back via prefetched.
                token afterMember = getNext();
                if(cur.value == "self" && !afterMember.is(token::parenOpen)){
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
                    if(expr->resolvedType.empty() && !memberType.empty()) expr->resolvedType = memberType;
                    // Inside a lambda: qualify 'self' through capture if needed
                    string selfText = "self";
                    if(lambdaOuterFunc != nullptr){
                        string qualified = qualifyIdentifier("self", func, body);
                        if(!qualified.empty()) selfText = qualified;
                    }
                    expr->tokens.push_back(selfText + "." + member.value);
                    // Capture host for $self substitution in rvalue property-class operators
                    // (mirrors the non-self property-access path).
                    expr->emitterSelf = selfText;
                } else {
                    // Non-self identifier, or self.method(args).
                    // afterMember was already read above; resolveIdentifierType("self",...)
                    // now returns the current object/class type, so self.method() works here too.
                    if(afterMember.is(token::parenOpen)){
                        // method call in expression context: obj.method(args)
                        string objName = cur.value;
                        string rawObjName = cur.value;   // pre-qualify form, for inheritance-check ($self rewrite)
                        string methName = member.value;
                        // Pass memberHint=methName so the resolver prefers a candidate whose type
                        // exposes the method, breaking name-collision ties (e.g. enum value vs
                        // class instance with the same case-insensitive name).
                        string objType = !castType.empty() ? castType : resolveIdentifierType(objName, func, body, methName);
                        castType = "";  // consume the cast
                        if(objType.empty()) parsingError(format("Unknown variable '{0}'", objName));
                        // Qualify objName for I6 emission: a #using-imported member like `glulx`
                        // needs to emit as `bgl.glulx` (the actual property path) so I6 resolves
                        // it correctly. Locals/globals qualify to themselves.
                        if(func != nullptr){
                            string qualified = qualifyIdentifier(objName, func, body, methName);
                            if(!qualified.empty()) objName = qualified;
                        }
                        // The receiver may be a classDef or an objectDef (each unclassed objectDef
                        // is its own type). Both have addressable methods; bindMethodCall handles
                        // either via its Step-2 objectDef-member fallback.
                        typeDef& objTd = languageService.getType(objType);
                        bool opaqueRecv = (dynamic_cast<classDef*>(&objTd) == nullptr && dynamic_cast<objectDef*>(&objTd) == nullptr);
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
                        if(opaqueRecv && !looseIdentifierMode)
                            parsingError(format("Type '{0}' has no methods", objType));

                        // parse argument list (handles named args via name: value syntax)
                        ParsedArgList pal = parseCallArgList(func, body);
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

                        expr->resolvedType = method->returnType.name;

                        // ofClass guard: validate first argument is a real I6-backed class or object
                        if(objType == "bglworldtype" && methName == "ofclass" && !callArgs.empty()){
                            string clsArgName = callArgs[0]->text();
                            typeDef* clsType = nullptr;
                            for(typeDef* g : languageService.globals)
                                if(g->name == clsArgName){ clsType = g; break; }
                            bool valid = false;
                            if(clsType){
                                if(auto* od = dynamic_cast<objectDef*>(clsType))
                                    valid = true; // user object declarations always have I6 backing
                                else if(auto* cd = dynamic_cast<classDef*>(clsType))
                                    valid = !cd->isEmitterClass && !cd->isAlias;
                            }
                            if(!valid)
                                parsingError(format("'{0}' cannot be used with bglWorld.ofClass() — it is not an I6-backed class or object", callArgs[0]->text()));
                        }

                        // Compute $prop for array method calls in expression context
                        string exprPropValue = isWordArrayType(objType) ? "0" : "<$prop undefined>";

                        // Member (property) WORD array? Detect once for member-aware lowering.
                        // A qualified receiver ("obj.prop") or a bare name resolving to a member
                        // uses the orLibrary property convention, not the global/tracked form.
                        string mOwner, mProp;
                        bool isMemberArr = false;
                        if(isWordArrayType(objType)){
                            size_t d = objName.rfind('.');
                            if(d != string::npos){ mOwner = objName.substr(0, d); mProp = objName.substr(d + 1); isMemberArr = true; }
                            else isMemberArr = splitQualifiedMember(rawObjName, func, body, mOwner, mProp);
                        }

                        string callText;
                        // size()/length() on a member array: (obj.#prop)/WORDSIZE — bypass the
                        // (possibly <array>-replaced) emitter body. Members are never tracked, so
                        // length()==size(). Stage 2 routes the algorithm methods through the utility.
                        if(isMemberArr && (methName == "size" || methName == "length")){
                            callText = "((" + mOwner + ".#" + mProp + ")/WORDSIZE)";
                            expr->tokens.push_back(callText);
                        }
                        else if(method->isEmitter){
                            if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                                string b = processBglConditionals(blk->i6Body);
                                size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                                size_t e = b.find_last_not_of(" \t\n\r"); if(e != string::npos) b = b.substr(0, e+1);
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
                                b = replaceWord(b, "$self", selfHost);
                                b = replaceWord(b, "$val",  selfHost);
                                // $class — declared type of the receiver. Ignores multiple inheritance:
                                // resolves to the variable's static type, not the type that owns the
                                // inherited emitter. Useful for emitters that emit class-message I6
                                // (e.g. `$class.copy($self, $src)` from the bglAllocated mixin).
                                if(auto* recvCls = getDispatchClass(objType))
                                    b = replaceWord(b, "$class", recvCls->i6Name());
                                for(size_t i = 0; i < method->params.size() && i < callArgs.size(); i++){
                                    // Raw bodies preserve original case; substitute both forms.
                                    paramDef* p = method->params[i];
                                    const string& display = p->displayName.empty() ? p->name : p->displayName;
                                    b = replaceWord(b, "$" + display, callArgs[i]->text());
                                    if(display != p->name)
                                        b = replaceWord(b, "$" + p->name, callArgs[i]->text());
                                }
                                // $prop fallback — fills any remaining $prop with the array-style
                                // receiver-path value. Done after param sub so emitters with a
                                // `prop` parameter (e.g. `provides(property prop)`) win.
                                b = replaceWord(b, "$prop", exprPropValue);
                                callText = b;
                                expr->tokens.push_back(b);
                            }
                        } else {
                            // non-emitter: emit verbatim as obj.method(args) (or obj.<mangled>(args)
                            // if this method is part of an overload set).
                            const string& callName = method->i6name.empty() ? methName : method->i6name;
                            string call = objName + "." + callName + "(";
                            for(size_t i = 0; i < callArgs.size(); i++){
                                if(i > 0) call += ", ";
                                call += callArgs[i]->text();
                            }
                            call += ")";
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
                                    string b = processBglConditionals(blk->i6Body);
                                    string receiver = "(" + callText + ")";
                                    b = replaceWord(b, "$self", receiver);
                                    b = replaceWord(b, "$val",  receiver);
                                    if(!getMethod->params.empty())
                                        b = replaceWord(b, "$" + getMethod->params[0]->name, indexExpr->text());
                                    expr->tokens.pop_back();  // drop bare call text
                                    expr->tokens.push_back(b);
                                    string retTypeName = getMethod->returnType.name;
                                    if(retTypeName == "t" || retTypeName == "T") retTypeName = chainElem;
                                    expr->resolvedType = retTypeName;
                                }
                            }
                        }
                        } // end of typed-receiver else
                    } else if(afterMember.is(token::bracketOpen)) {
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
                        if(expr->resolvedType.empty()) expr->resolvedType = getMethod->returnType.name;
                        if(getMethod->isEmitter)
                            if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)) {
                                // Byte-array members are stored as a separate backing array
                                // with the property holding the pointer, so the subscript
                                // receiver must be the property read `obj.prop` (binding to
                                // bare `obj` would index the object). Word-array members are
                                // INLINE property data, a separate access pattern that needs
                                // `obj.&prop` with no header offset — a distinct latent issue
                                // not handled here, so leave those on the prior `obj` binding.
                                string recv = (propType == "bytearray") ? (objName + "." + propName) : objName;
                                string b = processBglConditionals(blk->i6Body);
                                b = replaceWord(b, "$self", recv);
                                b = replaceWord(b, "$val",  recv);
                                b = replaceWord(b, "$prop", propName);
                                if(!getMethod->params.empty())
                                    b = replaceWord(b, "$" + getMethod->params[0]->name, indexExpr->text());
                                expr->tokens.push_back(b);
                            }
                    } else {
                        prefetched = afterMember;
                        // Distinguish enum-qualified access (EnumType.value → _EnumType_value),
                        // static member access (ClassName.staticMember → _bgl_ClassName_memberName),
                        // from object/variable property access (obj.prop → obj.prop)
                        bool isEnum = dynamic_cast<enumDef*>(&languageService.getType(cur.value)) != nullptr;
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
                                            string b = processBglConditionals(blk->i6Body);
                                            b = i6Emitter::replaceWord(b, "$self", cur.value);
                                            b = i6Emitter::replaceWord(b, "$val",  cur.value);
                                            size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                                            size_t e = b.find_last_not_of(" \t\n\r;"); if(e != string::npos) b = b.substr(0, e+1);
                                            expr->tokens.push_back(b);
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
                            objectDef* maybeObj = dynamic_cast<objectDef*>(&languageService.getType(cur.value));
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
                            objectDef* instObj = dynamic_cast<objectDef*>(&languageService.getType(cur.value));
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
                        // Auto member on object instance pointing to another object: redirect cur
                        // so chained access (e.g. bgl.glulx.method) continues walking. Only applies
                        // to namespace-style auto members — those whose initializer names a global
                        // object, OR whose declared type is an emitter class. Plain value-typed
                        // properties (int x; string s;) fall through to normal property handling.
                        if(!isStaticAccess && !isValueEmitterAccess && !isAliasMember){
                            objectDef* instObj = dynamic_cast<objectDef*>(&languageService.getType(cur.value));
                            if(instObj != nullptr){
                                for(typeMember* m : instObj->members)
                                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                                        if(vd->name == member.value){
                                            string initName = vd->declaredExpressionValue ? vd->declaredExpressionValue->text() : "";
                                            objectDef* target = nullptr;
                                            if(!initName.empty())
                                                for(typeDef* g : languageService.globals)
                                                    if(auto* od = dynamic_cast<objectDef*>(g))
                                                        if(od->name == initName){ target = od; break; }
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
                            auto* ed = dynamic_cast<enumDef*>(&languageService.getType(cur.value));
                            if(ed && !ed->isExternal){
                                int v = 0; bool found = false;
                                for(enumValueDef* ev : ed->namedValues)
                                    if(ev->name == member.value){ v = ev->value; found = true; break; }
                                if(found) expr->tokens.push_back(to_string(v));
                                else expr->tokens.push_back("_" + cur.value + "_" + member.value);
                            } else {
                                expr->tokens.push_back(member.value);
                            }
                        } else if(isAliasMember) {
                            continue;  // re-enter loop with cur set to alias type
                        } else if(!isStaticAccess && !isValueEmitterAccess) {
                            // Object property access: emit as obj.prop. Route obj through
                            // qualifyIdentifier so a class-typed local with synthesized
                            // backing (isClassLocalWithBacking) emits as the backing's
                            // i6name instead of the bare local slot — otherwise the read
                            // would go through the int slot (= `nothing`) and miss the
                            // backing object's properties entirely.
                            string propType = resolvePathType(cur.value + "." + member.value, func, body);
                            string objText = func != nullptr ? qualifyIdentifier(cur.value, func, body, member.value) : cur.value;
                            if(objText.empty()) objText = cur.value;
                            string accessText = objText + "." + member.value;
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
                        }
                    }
                }
            }
            // ── name?: postfix query operator ──
            // Only treat ? as postfix if the identifier's type has operator?() AND ? is not
            // a terminator for this expression. When ? is a terminator (e.g. from precedence
            // sub-parse), it's the start of a ternary, not a postfix query.
            else if(next.is("?")){
                // Check if ? is a terminator — if so, it's ternary, not postfix
                bool qIsTerminator = false;
                if(parenDepth <= startParenDepth)
                    for(const string& term : terminators)
                        if(term == "?"){ qIsTerminator = true; break; }
                string varName = cur.value;
                string varType = resolveIdentifierType(varName, func, body);
                classDef* cls = !varType.empty() ? dynamic_cast<classDef*>(&languageService.getType(varType)) : nullptr;
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
                    string b = processBglConditionals(blk->i6Body);
                    b = replaceWord(b, "$self", qualified);
                    b = replaceWord(b, "$val",  qualified);
                    { size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
                      size_t e=b.find_last_not_of(" \t\n\r;"); if(e!=string::npos) b=b.substr(0,e+1); }
                    expr->tokens.push_back(b);
                    if(expr->resolvedType.empty()) expr->resolvedType = queryFn->returnType.name;
                } else {
                    // No operator?() — put ? back for ternary handling
                    prefetched = next;
                    string qualified = func != nullptr ? qualifyIdentifier(cur.value, func, body) : cur.value;
                    if(!qualified.empty()){
                        if(expr->resolvedType.empty()) expr->resolvedType = resolveIdentifierType(cur.value, func, body);
                        expr->tokens.push_back(qualified);
                    } else if(func != nullptr){
                        parsingError(format("Undeclared identifier '{0}'", cur.value));
                    } else {
                        expr->tokens.push_back(cur.value);
                    }
                }
            }
            else {
                // I6-syntax leak: a bare identifier followed by another bare identifier (no '.',
                // '(', '[', operator, or other separator between them) is never valid in a
                // Beguile expression. This catches I6 infix-keyword forms like `o provides X`,
                // `o ofclass C`, `o has light`, `o in container`, etc. that the property/class
                // name passthroughs would otherwise quietly mash into the emitted text.
                // Each of these has a canonical Beguile method-call form.
                if(next.is(eTokenType::name) || next.is(eTokenType::identifier) || next.is(eTokenType::dataType)){
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
                string qualified = qualifyIdentifier(cur.value, func, body, memberHint);
                if(!qualified.empty()){
                    if(!castType.empty()){
                        // Try invoking source-type's operator()→castType emitter. For int↔uint
                        // (passthrough bodies) this returns the source text unchanged, matching
                        // the historical relabel-only behavior. For float casts it emits the
                        // @numtof/@ftonumz body, allocating a temp slot if needed.
                        string srcType = resolveIdentifierType(cur.value, func, body, memberHint);
                        string newText = applyCastConversion(qualified, srcType, castType);
                        expr->tokens.push_back(newText);
                        expr->resolvedType = castType;
                        castType = "";
                    } else {
                        if(expr->resolvedType.empty()) expr->resolvedType = resolveIdentifierType(cur.value, func, body, memberHint);
                        expr->tokens.push_back(qualified);
                    }
                } else if(func != nullptr){
                    parsingError(format("Undeclared identifier '{0}'", cur.value));
                } else {
                    expr->tokens.push_back(cur.value); // global-context passthrough for unknown identifiers
                }
            }
        }
        // ─── NULL COALESCING: ?? ──────────────────────────────────────────
        else if(cur.is(eTokenType::oper) && cur.value == "??" && parenDepth == 0){
            parseExprNullCoalescing(expr, terminators, func, body);
            break;
        }
        // ─── BINARY OPERATOR: emitter dispatch via applyBinaryOperator() ─
        else if(cur.is(eTokenType::oper)){
            if(!expr->resolvedType.empty()){
                classDef* cls = dynamic_cast<classDef*>(&languageService.getType(expr->resolvedType));
                // Set expected type for the RHS so name resolution can disambiguate. Applies to
                // both classDef and enumDef LHS — most binary operators take same-type RHS.
                // The parseExpression-level RAII guard restores on exit; no manual restore here.
                currentExpectedType = expr->resolvedType;
                if(cls != nullptr){
                    string opName = cur.value;
                    // Peek at RHS
                    applyBinaryOperator(expr, opName, cls, terminators, parenDepth, getNext, prefetched, func, body);
                } else if(auto* lhsEnum = dynamic_cast<enumDef*>(&languageService.getType(expr->resolvedType));
                          lhsEnum && lhsEnum->isBnum && (cur.value == "|" || cur.value == "&" || cur.value == "^")){
                    // bnum bitwise composition: RHS must be a bnum sharing a common base, the
                    // same bnum, or int. Result type is the shared base (or LHS if same/child).
                    string opName = cur.value;
                    token rhs = getNext();
                    string rhsType, rhsText;
                    if(rhs.is(eTokenType::integer)){ rhsType = "intliteral"; rhsText = rhs.value; }
                    else if(rhs.is(eTokenType::name)){
                        string nsEmission, nsEnumType;
                        if(tryConsumeNamespacedEnumValue(rhs, nsEmission, nsEnumType)){
                            rhsType = nsEnumType;
                            rhsText = nsEmission;
                        } else {
                            rhsType = resolveIdentifierType(rhs.value, func, body);
                            rhsText = (func != nullptr) ? qualifyIdentifier(rhs.value, func, body) : rhs.value;
                            if(rhsText.empty()) rhsText = rhs.value;
                        }
                    }
                    else if(rhs.is(token::parenOpen)){
                        expression* rhsExpr = parseExpression(file.getToken(), {token::parenClose}, func, body);
                        rhsText = "(" + rhsExpr->text() + ")";
                        rhsType = rhsExpr->resolvedType;
                    }
                    else parsingError(format("Unexpected token '{0}' after bnum '{1}'", rhs.value, opName));

                    // Validate RHS and compute result type
                    enumDef* rhsEnum = dynamic_cast<enumDef*>(&languageService.getType(rhsType));
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
                    expr->tokens.push_back(opName);
                    expr->tokens.push_back(rhsText);
                    expr->resolvedType = resultType;
                } else {
                    emitRawBinaryOp(cur.value);
                }
            } else if(cur.value == "!"){
                parseExprPrefixNot(expr, getNext(), prefetched, func, body);
            } else {
                emitRawBinaryOp(cur.value);
            }
        }
        // ─── DICTIONARY WORD LITERAL ─────────────────────────────────────
        else if(cur.is(eTokenType::dictionaryWord)){
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
            string i6form = cur.isPlural ? ("'" + w + "/p'")
                                         : (w.size() == 1) ? ("'" + w + "//'") : ("'" + w + "'");
            expr->tokens.push_back(i6form);
        }
        // ─── CHAR LITERAL ─────────────────────────────────────────────────
        else if(cur.is(eTokenType::charLiteral)){
            if(expr->resolvedType.empty()) expr->resolvedType = "charliteral";
            // Numeric ZSCII (\NNN) and diacritical (@^a etc.) are standalone I6 tokens; others need '...'
            bool isBareToken = (!cur.value.empty() && all_of(cur.value.begin(), cur.value.end(), ::isdigit))
                            || (cur.value.rfind("@", 0) == 0);
            if(isBareToken) expr->tokens.push_back(cur.value);
            else expr->tokens.push_back("'" + cur.value + "'");
        }
        // ─── DOT CHAINING on resolved expression: .method() or .property ─
        else if(cur.value == "." && !expr->tokens.empty() && !expr->resolvedType.empty()){
            // Chained method call on result of prior expression: <expr>.method(args)
            token member = file.getToken(eTokenType::identifier);
            token afterMember = getNext();
            if(!afterMember.is(token::parenOpen)){
                // Not a method call (e.g. struct member) — emit '.' and re-process member
                prefetched = afterMember;
                expr->tokens.push_back(".");
                cur = member;
                continue;
            }

            string selfText;
            for(const auto& t : expr->tokens) selfText += t;
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
            vector<expression*> callArgs;
            token firstArg = file.getToken();
            while(firstArg.isNot(token::parenClose)){
                expression* arg = parseExpression(firstArg, {token::comma, token::parenClose}, func, body);
                callArgs.push_back(arg);
                if(arg->terminator == token::parenClose) break;
                firstArg = file.getToken();
            }

            vector<string> emptyNamed;
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
            bool chainIsMember = isWordArrayType(chainTypeName)
                                 && selfText.find('.') != string::npos
                                 && selfText.find('(') == string::npos;
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
            functionDef* method = bindMethodCall(chainTypeName, selfText, methName,
                                                   callArgs, emptyNamed, emptyInterp, chainElem);

            expr->tokens.clear();
            expr->resolvedType = method->returnType.name;

            string callText;
            if(method->isEmitter){
                if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                    string b = processBglConditionals(blk->i6Body);
                    size_t s = b.find_first_not_of(" \t\n\r"); if(s != string::npos) b = b.substr(s);
                    size_t e = b.find_last_not_of(" \t\n\r"); if(e != string::npos) b = b.substr(0, e+1);
                    b = replaceWord(b, "$self", chainSelf);
                    b = replaceWord(b, "$val",  chainSelf);
                    for(size_t i = 0; i < method->params.size() && i < callArgs.size(); i++)
                        b = replaceWord(b, "$" + method->params[i]->name, callArgs[i]->text());
                    b = replaceWord(b, "$prop", chainProp);
                    callText = b;
                    expr->tokens.push_back(b);
                }
            } else {
                const string& callName = method->i6name.empty() ? methName : method->i6name;
                string call = selfText + "." + callName + "(";
                for(size_t i = 0; i < callArgs.size(); i++){
                    if(i > 0) call += ", ";
                    call += callArgs[i]->text();
                }
                call += ")";
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
                        string b = processBglConditionals(blk->i6Body);
                        string receiver = "(" + callText + ")";
                        b = replaceWord(b, "$self", receiver);
                        b = replaceWord(b, "$val",  receiver);
                        if(!getMethod->params.empty())
                            b = replaceWord(b, "$" + getMethod->params[0]->name, indexExpr->text());
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
        }
        // ─── TERNARY OPERATOR: condition ? trueExpr : falseExpr ──────────
        // Uses continuation: true branch is sub-parsed (':' is unambiguous), then the false
        // branch is collected by continuing this loop — preserving paren tracking. Assembly
        // happens after the loop exits.
        else if(cur.value == "?") {
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
            expression* trueExpr = parseExpression(file.getToken(), {":"}, func, body);
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
            cur = getNext();
            continue;
        }
        // ─── DIRECTIVE: #beguilerSettings.property references ─────────────
        else if(cur.is(eTokenType::directive)){
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
                bool   isInt = false;
                int    intVal = 0;
                if     (key == "title")          strVal = beguilerSettings.title;
                else if(key == "author")         strVal = beguilerSettings.author;
                else if(key == "headline")       strVal = beguilerSettings.headline;
                else if(key == "genre")          strVal = beguilerSettings.genre;
                else if(key == "description")    strVal = beguilerSettings.description;
                else if(key == "language")       strVal = beguilerSettings.language;
                else if(key == "series")         strVal = beguilerSettings.series;
                else if(key == "seriesnumber") { isInt = true; intVal = beguilerSettings.seriesNumber; }
                else if(key == "firstpublished") strVal = beguilerSettings.firstPublished;
                else if(key == "forgiveness")    strVal = beguilerSettings.forgiveness;
                else if(key == "ifid")           strVal = beguilerSettings.ifid;
                else if(key == "target")        strVal = beguilerSettings.target;
                else if(key == "outputpath")    strVal = beguilerSettings.outputPath;
                else if(key == "blorbassetpath")strVal = beguilerSettings.blorbAssetPath;
                else if(key == "informname")    strVal = beguilerSettings.informName;
                else if(key == "release")     { isInt = true; intVal = beguilerSettings.release; }
                else if(key == "serial")       strVal = beguilerSettings.serial;
                else if(key == "framepoolsize" || key == "linqscratchsize" || key == "worldbufsize"){
                    isInt = true;
                    int v = (key == "framepoolsize") ? beguilerSettings.framePoolSize
                          : (key == "linqscratchsize") ? beguilerSettings.linqScratchSize
                          : beguilerSettings.worldBufSize;
                    if(v < 0){
                        // applySchemaDefaults() runs after parsing, so during source parse the
                        // runtime value may still be -1 (unset). Fall back to the schema-declared
                        // default so #beguilerSettings.X at parse time resolves consistently with
                        // what the final ICL emission will use.
                        classDef* schema = dynamic_cast<classDef*>(&languageService.getType("beguilerSettingstype"));
                        if(schema){
                            for(typeMember* m : schema->members){
                                auto* vd = dynamic_cast<variableDeclaration*>(m);
                                if(vd && vd->name == key && vd->declaredExpressionValue){
                                    v = stoi(vd->declaredExpressionValue->text());
                                    break;
                                }
                            }
                        }
                        if(v < 0) v = 0;
                    }
                    intVal = v;
                }
                else parsingError(format("#beguilerSettings.{0}: unknown or unsupported property", prop.value));

                if(isInt){
                    expr->tokens.push_back(to_string(intVal));
                    if(expr->resolvedType.empty()) expr->resolvedType = "intliteral";
                } else {
                    expr->tokens.push_back("\"" + strVal + "\"");
                    if(expr->resolvedType.empty()) expr->resolvedType = "stringliteral";
                }
                cur = getNext();
                continue;
            }
            parsingError(format("Directive '{0}' is not valid in an expression.", cur.value));
        }
        else {
            expr->tokens.push_back(cur.value);
        }

        cur = getNext();
    }

    // Assemble any remaining pending ternaries (e.g. top-level ternary without parens)
    while(!pendingTernaries.empty()) assembleTernary();
    return expr;
}
