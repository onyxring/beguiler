#include "platform.h"
// ===============================================================================
// bglParserStmts.cpp - statement-level parsing.
//
// Holds parsers for individual statement forms plus the free-standing
// `processStatement` (which handles assignments, function calls, and compound-op
// statements at the expression level).
//
// Control-flow statements:
//   processIf      processWhile     processFor      processDo
//   processSwitch  processTry       processThrow    processDelete
//
// Terminal / value-returning statements:
//   processBreak   processContinue
//   processRtrue   processRfalse    processRtrueWithMessage   processRfalseWithMessage
//   processReturnVoid    processReturnExpr    emitRtrueRfalseWithMessage
//
// Free-standing expression-statement parser:
//   processStatement   - the largest function here (~1000 lines); handles bare
//                        assignments, method calls, increment/decrement, and
//                        compound-op statements that aren't dispatched through
//                        a leading keyword.
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
// Terminal / value-returning statements
// ===============================================================================
bool bglParser::processBreak(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'break' is not valid at global scope");
    if(loopDepth == 0 && switchBlockDepths.empty() && !looseIdentifierMode)
        parsingError("'break' is only valid inside a loop (for, while, or do) or a switch");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    emitLoopExitCleanups(body, true);
    i6RawNode& brk = *(new i6RawNode());
    brk.text = "break;";
    if(body != nullptr) body->statements.push_back(&brk);
    return false;
}

bool bglParser::processContinue(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'continue' is not valid at global scope");
    if(loopDepth == 0)
        parsingError("'continue' is only valid inside a loop (for, while, or do)");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    emitLoopExitCleanups(body);
    i6RawNode& cont = *(new i6RawNode());
    cont.text = "continue;";
    parsedContinues.push_back({loopDepth, &cont});
    if(body != nullptr) body->statements.push_back(&cont);
    return false;
}

void bglParser::moveConditionIntoBody(expression*& condition, vector<statement*> setup, statementBlock* loopBody){
    if(setup.empty() || condition == nullptr || loopBody == nullptr) return;
    ifStatement* test = new ifStatement();
    test->condition = new expression(*condition);
    test->condition->tokens.insert(test->condition->tokens.begin(), "~~(");
    test->condition->tokens.push_back(")");
    test->thenBlock = new statementBlock();
    i6RawNode* brk = new i6RawNode();
    brk->text = "break;";
    test->thenBlock->statements.push_back(brk);
    setup.push_back(test);
    loopBody->statements.insert(loopBody->statements.begin(), setup.begin(), setup.end());
    expression* always = new expression();
    always->tokens.push_back("true");
    always->resolvedType = "bool";
    condition = always;
}

bool bglParser::processRtrue(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'rtrue' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    if(func != nullptr && func->returnType.name == "void")
        parsingError(format("Cannot use 'rtrue' in void routine '{0}'", (currentFunc ? currentFunc : func)->dName()));
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    returnStatement& rt = *(new returnStatement());
    rt.src = file.currentLocation();
    rt.returnExpression = "rtrue";
    if(body != nullptr) body->statements.push_back(&rt);
    return false;
}

bool bglParser::processRfalse(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'rfalse' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    if(func != nullptr && func->returnType.name == "void")
        parsingError(format("Cannot use 'rfalse' in void routine '{0}'", (currentFunc ? currentFunc : func)->dName()));
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    returnStatement& rf = *(new returnStatement());
    rf.src = file.currentLocation();
    rf.returnExpression = "rfalse";
    if(body != nullptr) body->statements.push_back(&rf);
    return false;
}

// `rtrue(expr);` / `rfalse(expr);` — print the argument (full print() dispatch
// including overloads + interpolated strings via $"..."), then return true/false.
// Equivalent to writing `print(expr); rtrue;` but as a single statement.
// Caller has already consumed the 'rtrue'/'rfalse' keyword and the opening '('.
void bglParser::emitRtrueRfalseWithMessage(abstractObject& ctx, const string& which){
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError(format("'{0}' is not valid at global scope", which));
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    if(func != nullptr && func->returnType.name == "void")
        parsingError(format("Cannot use '{0}' in void routine '{1}'", which, (currentFunc ? currentFunc : func)->dName()));
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();

    // Parse args inside the already-consumed `(`. parseCallArgList reads through ')'.
    ParsedArgList pal = parseCallArgList(func, body);
    if(pal.args.size() != 1)
        parsingError(format("'{0}(...)' takes exactly one argument; got {1}", which, pal.args.size()));

    // Build the print() call. bindGlobalCall resolves the overload (string/int/object/etc.)
    // and finalizes the args (named-arg reorder + default fill + conversion), so any type
    // accepted by print() works — including interpolated string literals.
    functionCallStatement& printCall = *(new functionCallStatement());
    printCall.src = stmtLoc;
    printCall.functionName = "print";
    printCall.args = pal.args;
    printCall.namedArgNames = pal.namedArgNames;
    printCall.interpSegmentsPerArg = pal.interpSegmentsPerArg;
    GlobalCallBinding gcb = bindGlobalCall(printCall.functionName, printCall.args,
                                            printCall.namedArgNames, printCall.interpSegmentsPerArg,
                                            func, body);
    if(gcb.method && gcb.method->isEmitter)
        if(auto* blk = dynamic_cast<i6Block*>(gcb.method->body)){
            printCall.emitterBody = expandEmitterBody(blk, {});
            for(paramDef* p : gcb.method->params) printCall.emitterParams.push_back(p->name);
        }

    // Emit order: print first, then the return.
    if(body != nullptr) body->statements.push_back(&printCall);
    returnStatement& ret = *(new returnStatement());
    ret.src = stmtLoc;
    ret.returnExpression = which;
    if(body != nullptr) body->statements.push_back(&ret);

    // Consume the trailing ';'.
    file.getToken(token::endStatement);
}

bool bglParser::processRtrueWithMessage(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    emitRtrueRfalseWithMessage(ctx, "rtrue");
    return false;
}

bool bglParser::processRfalseWithMessage(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    emitRtrueRfalseWithMessage(ctx, "rfalse");
    return false;
}

bool bglParser::processReturnVoid(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'return' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    if(func != nullptr && !looseIdentifierMode && !func->returnType.name.empty() && func->returnType.name != "void")
        parsingError(format("'{0}' returns {1}, so its 'return' needs a value.", func->dName(),
                            typeDisplayName(func->returnType.name)));
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    emitOpenBlockCleanups(body);
    returnStatement& rs = *(new returnStatement());
    rs.src = file.currentLocation();
    if(body != nullptr) body->statements.push_back(&rs);
    return false;
}

void bglParser::rejectEscapingLambda(const expression* e, const string& where){
    // Only the value itself escapes: a lambda passed to a call in the expression runs during it.
    if(e == nullptr) return;
    string v = e->text();
    v.erase(remove_if(v.begin(), v.end(), [](char c){ return isspace((unsigned char)c); }), v.end());
    while(v.size() > 2 && v.front() == '(' && v.back() == ')') v = v.substr(1, v.size() - 2);
    if(v.rfind("_bglLambda_", 0) != 0 || v.find_first_of("(,") != string::npos) return;
    string captured = capturedByLambdaIn(e);
    if(!captured.empty())
        parsingError(format("A lambda {0} runs after the variables it captures are gone, so it cannot "
            "capture them; this one captures '{1}'.", where, captured));
}

void bglParser::rejectLoopStoredLambda(const expression* e, const string& target){
    if(e == nullptr || loopBlockDepths.empty()) return;
    string v = e->text();
    v.erase(remove_if(v.begin(), v.end(), [](char c){ return isspace((unsigned char)c); }), v.end());
    while(v.size() > 2 && v.front() == '(' && v.back() == ')') v = v.substr(1, v.size() - 2);
    if(v.rfind("_bglLambda_", 0) != 0 || v.find_first_of("(,") != string::npos) return;
    string captured = capturedByLambdaIn(e);
    if(captured.empty()) return;
    // Where the target was declared: a block on the stack, or the routine itself (params, top level).
    string name = target.substr(0, target.find_first_of(".[("));
    transform(name.begin(), name.end(), name.begin(), ::tolower);
    long declaredAt = -1;
    for(long i = (long)activeBlockStack.size() - 1; i >= 0 && declaredAt < 0; i--)
        if(statementBlock* blk = activeBlockStack[i])
            for(statement* s : blk->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s); vd && vd->name == name){ declaredAt = i; break; }
    if(declaredAt >= (long)loopBlockDepths.back()) return;   // declared inside the loop: one per pass
    parsingError(format("The lambdas this loop stores in '{0}' all share one copy of '{1}'; a lambda stored "
        "in a loop can capture only const values.", target, captured));
}

bool bglParser::processReturnExpr(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'return' is not valid at global scope");
    // ctx is the synthetic functionDef for the current block (carries the body the return
    // should land in — possibly a nested if/else block, not the outer function root).
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    // For diagnostics, prefer currentFunc's name — the synthetic block contexts have no name.
    const string& funcName = (currentFunc && !currentFunc->name.empty()) ? currentFunc->name
                              : (func ? func->name : string());
    // Read the return expression first so we can see its type. A `return <void-typed expr>;`
    // from a void function is the C/I6 tail-call idiom (`return f();` ≡ `f(); return;`); we
    // accept it because the wrapping return adds nothing observable. Any other value-bearing
    // return from a void function remains an error. The allowVoidReturnExpr flag suppresses
    // the parseExpression-level guard that would otherwise reject void calls in expressions.
    bool savedAllowVoid = allowVoidReturnExpr;
    allowVoidReturnExpr = (func != nullptr && func->returnType.name == "void");
    bool canReread = !file.hasPendingToken;
    fileLexer::mark_t retMark = file.mark();
    size_t setUpBefore = pendingInjections.size();
    token first = file.getToken();
    // A routine returning a value class returns an accessor member itself, not what its getter reads.
    classDef* savedOwnOperand = ownOperatorOperand;
    string savedExpected = currentExpectedType;
    if(currentFunc != nullptr)
        if(classDef* rc = getDispatchClass(currentFunc->returnType.name); rc != nullptr && isValueClass(rc)){
            ownOperatorOperand = rc; currentExpectedType = rc->name;
        }
    expression* retExpr = nullptr;
    try { retExpr = parseExpression(first, {token::endStatement}, func, body); }
    catch(...){ ownOperatorOperand = savedOwnOperand; currentExpectedType = savedExpected; throw; }
    ownOperatorOperand = savedOwnOperand; currentExpectedType = savedExpected;
    allowVoidReturnExpr = savedAllowVoid;
    // `Mass made(int k) { return k * 2; }`: a value the class converts from goes through its `operator =`
    // into an instance, as a declaration does — written as one.
    if(canReread && retExpr && currentFunc && func != nullptr && !currentFunc->isLambda)
        if(classDef* rc = languageService.findClass(currentFunc->returnType.name);
           rc != nullptr && isValueClass(rc) && !rc->isEmitterClass && classHasStoredFields(rc)){
            classDef* ec = getDispatchClass(retExpr->resolvedType);
            if(!retExpr->resolvedType.empty() && retExpr->resolvedType != "var" && retExpr->resolvedType != "nullliteral"
               && !(ec != nullptr && (ec == rc || ec->hasAncestor(rc))) && isTypeCompatible(retExpr->resolvedType, rc->name)){
                pendingInjections.resize(setUpBefore);
                postInjections.clear();
                file.rewind(retMark);
                string raw = file.getRawTextToStatementEnd();
                static int counter = 0;
                string tmp = format("_bglret{0}", counter++);
                sourceLocation at = file.currentLocation();
                for(const string& text : {rc->name + " " + tmp + " = " + raw + ";", "return " + tmp + ";"}){
                    file.openText(text, at.file, at.line);
                    try { processStatementDispatch(file.getToken(), *func); } catch(...) { file.close(); throw; }
                    file.close();
                }
                return false;
            }
        }
    // A lambda's copy-back into this routine's locals has nothing to update once it returns.
    postInjections.clear();
    rejectEscapingLambda(retExpr, format("returned from '{0}'", currentFunc ? currentFunc->dName() : funcName));
    if(func != nullptr && func->returnType.name == "void"){
        // Allow `return <void-typed expr>;` as the C/I6 idiom shorthand. In loose-mode
        // contexts (`#bgl{}` islands and `.inf` precompiler mode), unresolved identifiers
        // resolve to `var`; treat `var` as compatible too so `return f()` ports verbatim
        // when `f` was declared in I6 and isn't visible to the Beguile resolver.
        string retType = retExpr ? retExpr->resolvedType : "";
        if(retType != "void" && retType != "var")
            parsingError(format("Cannot return a value from void routine '{0}'", func->dName()));
    }
    // A block-body lambda has no declared return type: its first `return` supplies it, and any later
    // one is checked against that.
    if(retExpr && currentFunc && currentFunc->isLambda && currentFunc->returnType.name.empty()
       && !retExpr->resolvedType.empty())
        currentFunc->returnType.name = literalBaseType(retExpr->resolvedType);
    else if(retExpr && currentFunc) checkReturnValue(retExpr, currentFunc->returnType.name, funcName);
    if(retExpr && currentFunc && retExpr->resolvedType == "nullliteral")
        if(classDef* rc = languageService.findClass(currentFunc->returnType.name); rc && isValueClass(rc) && !rc->isEmitterClass)
            parsingError(format("'{0}' returns a '{1}', which is copied out by value, so it can't return null.",
                                funcName, rc->dName()));
    returnStatement& rs = *(new returnStatement());
    rs.src = file.currentLocation();
    rs.returnExpression = retExpr ? retExpr->text() : "";
    // Drain any pre-statements the expression created (e.g. ternary lowering into _bgl_temp)
    // BEFORE appending the return, so they execute in the current function rather than leaking
    // into the next one parsed.
    if(body != nullptr){
        for(statement* inj : pendingInjections) body->statements.push_back(inj);
        pendingInjections.clear();
        // After the expression is evaluated — the value being returned may read a local this is
        // about to release — and before the return itself.
        emitOpenBlockCleanups(body);
        body->statements.push_back(&rs);
    }
    return false;
}


// ===============================================================================
// Control-flow statements
// ===============================================================================
// A body without braces is still a block: entered like one, so a declaration in its single
// statement (`for (…) for (int j = 0; …)`) is in scope and its locals are released at its end.
void bglParser::processBracelessBody(token first, functionDef& ctx){
    openCompileContext(eCompileContext::codeBlock, dynamic_cast<statementBlock*>(ctx.body));
    try { processStatementDispatch(first, ctx); }
    catch(...) { closeCompileContext(eCompileContext::codeBlock); throw; }
    closeCompileContext(eCompileContext::codeBlock);
}

bool bglParser::processIf(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'if' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();
    ifStatement& ifStmt = *(new ifStatement());
    ifStmt.src = stmtLoc;
    // Caller already consumed "if" "(" — read condition
    ifStmt.condition = parseExpression(file.getToken(), {token::parenClose}, func, body);
    // Drain any ternary injections from the condition BEFORE the if statement
    for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
    pendingInjections.clear();
    // What the condition left to do after it runs (a lambda's copy-back) opens both branches.
    vector<statement*> condAfter = postInjections;
    postInjections.clear();
    ifStmt.thenBlock = new statementBlock();
    functionDef thenCtx;
    if(func != nullptr){ thenCtx.returnType = func->returnType; thenCtx.params = func->params; }
    thenCtx.body = ifStmt.thenBlock;
    token next = file.getToken();
    if(next.is(token::braceOpen)){
        openCompileContext(eCompileContext::codeBlock, ifStmt.thenBlock);
        while(processNextStatement(thenCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    } else {
        processBracelessBody(next, thenCtx);
    }
    if(file.peekToken().is("else")){
        file.getToken();
        ifStmt.elseBlock = new statementBlock();
        functionDef elseCtx;
        if(func != nullptr){ elseCtx.returnType = func->returnType; elseCtx.params = func->params; }
        elseCtx.body = ifStmt.elseBlock;
        token elseNext = file.getToken();
        if(elseNext.is(token::braceOpen)){
            openCompileContext(eCompileContext::codeBlock, ifStmt.elseBlock);
            while(processNextStatement(elseCtx) == false){}
            closeCompileContext(eCompileContext::codeBlock);
        } else {
            processBracelessBody(elseNext, elseCtx);
        }
    }
    if(!condAfter.empty()){
        if(ifStmt.elseBlock == nullptr) ifStmt.elseBlock = new statementBlock();
        for(statementBlock* b : {ifStmt.thenBlock, ifStmt.elseBlock})
            b->statements.insert(b->statements.begin(), condAfter.begin(), condAfter.end());
    }
    if(body != nullptr) body->statements.push_back(&ifStmt);
    return false;
}

bool bglParser::processWhile(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'while' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();
    whileStatement& whileStmt = *(new whileStatement());
    whileStmt.src = stmtLoc;
    // Caller already consumed "while" "(" — read condition
    whileStmt.condition = parseExpression(file.getToken(), {token::parenClose}, func, body);
    vector<statement*> condSetup = pendingInjections;
    pendingInjections.clear();
    whileStmt.body = new statementBlock();
    functionDef whileCtx;
    if(func != nullptr){ whileCtx.returnType = func->returnType; whileCtx.params = func->params; }
    whileCtx.body = whileStmt.body;
    token next = file.getToken();
    loopDepth++; loopBlockDepths.push_back(activeBlockStack.size());
    if(next.is(token::braceOpen)){
        openCompileContext(eCompileContext::codeBlock, whileStmt.body);
        while(processNextStatement(whileCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    } else {
        processBracelessBody(next, whileCtx);
    }
    loopDepth--; loopBlockDepths.pop_back();
    moveConditionIntoBody(whileStmt.condition, condSetup, whileStmt.body);
    if(body != nullptr) body->statements.push_back(&whileStmt);
    return false;
}

// Parses the C-style `for(init; cond; incr)` tail, its body, and appends the loop to `body`.
// `loopVarName` is the init identifier the caller already consumed — it seeds the init text and
// is tracked as an in-scope loop variable; empty when the init starts with anything else.
bool bglParser::processForCStyle(const std::string& loopVarName, const sourceLocation& stmtLoc,
                                 class functionDef* func, class statementBlock* body) {
    forStatement& forStmt = *(new forStatement());
    forStmt.src = stmtLoc;
    string initText = loopVarName;
    token tt = file.getToken();
    // A variable whose class owns its value (stringObj, a value class) is set by its operator =, as a
    // statement of its own ahead of the loop.
    classDef* varCls = !loopVarName.empty() && func != nullptr ? getDispatchClass(resolveIdentifierType(loopVarName, func, body)) : nullptr;
    if(varCls != nullptr && !varCls->isPrimitive && !plainSlotOwnsReferenceInstance(varCls)
       && tt.is(token::assignment) && !file.hasPendingToken){
        string text = loopVarName + " = " + file.getRawTextToStatementEnd() + ";";
        file.openText(text, stmtLoc.file, stmtLoc.line);
        try { processStatementDispatch(file.getToken(), *func); } catch(...) { file.close(); throw; }
        file.close();
        initText = "";
    } else if(!loopVarName.empty() && tt.is(token::assignment)){
        // `name = expr`: parse the value like any other expression, so calls, member access and
        // emitters in it are translated (the raw token text reached I6 as written).
        expression* initExpr = parseExpression(file.getToken(), {token::endStatement}, func, body);
        for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
        pendingInjections.clear();
        initText = loopVarName + " = " + (initExpr ? initExpr->text() : "");
    } else if(!loopVarName.empty() && tt.is(token::period) && func != nullptr && !file.hasPendingToken){
        // `obj.member = v`: a member is set as a statement sets it (its accessor's setter, its class's
        // operator =), ahead of the loop.
        string text = loopVarName + "." + file.getRawTextToStatementEnd() + ";";
        file.openText(text, stmtLoc.file, stmtLoc.line);
        try { processStatementDispatch(file.getToken(), *func); } catch(...) { file.close(); throw; }
        file.close();
        initText = "";
    } else {
        if(tt.is(token::endStatement) && !loopVarName.empty()) initText = "";   // declared with no value: nothing runs
        while(tt.isNot(token::endStatement)){
            if(!initText.empty()) initText += " ";
            initText += tt.value;
            tt = file.getToken();
        }
    }
    forStmt.initText = initText;
    allowEmptyExpr = true;   // each part of `for(;;)` may be empty
    forStmt.condition = parseExpression(file.getToken(), {token::endStatement}, func, body);
    allowEmptyExpr = false;
    vector<statement*> condSetup = pendingInjections;
    pendingInjections.clear();
    // A variable whose class owns its value is stepped by statements of its own (its `operator =`, a
    // compound operator's fallback), run at the end of each pass, where a `continue` goes too.
    string deferredStep;
    bool stepReopened = false;
    if(!file.hasPendingToken){
        string stepText;
        int depth = 0;
        for(char c = file.readChar(); c != EOF; c = file.readChar()){
            if(c == '(' || c == '[' || c == '{') depth++;
            else if(c == ')' || c == ']' || c == '}'){ if(depth == 0) break; depth--; }
            stepText += c;
        }
        // The variable the step starts with decides: the loop's own, or any other one it names.
        size_t a = stepText.find_first_not_of(" \t\r\n");
        string target;
        if(a != string::npos)
            for(size_t i = a; i < stepText.size() && (isalnum((unsigned char)stepText[i]) || stepText[i] == '_'); i++) target += stepText[i];
        classDef* stepCls = varCls;
        if(!target.empty() && func != nullptr && (loopVarName.empty() || strcasecmp(target.c_str(), loopVarName.c_str()) != 0))
            stepCls = getDispatchClass(resolveIdentifierType(target, func, body));
        // A step through a member path (`dial.pos++`) is a statement too: the member's accessor or class decides.
        size_t afterTarget = a == string::npos ? string::npos : stepText.find_first_not_of(" \t\r\n", a + target.size());
        bool memberStep = !target.empty() && afterTarget != string::npos && stepText[afterTarget] == '.' && func != nullptr;
        if(memberStep || (!target.empty() && stepCls != nullptr && !stepCls->isPrimitive && !isWordArrayType(stepCls->name)
           && !plainSlotOwnsReferenceInstance(stepCls)))
            deferredStep = stepText;
        else {
            file.openText(stepText + ")", stmtLoc.file, stmtLoc.line);
            stepReopened = true;
        }
    }
    string assignedTarget;
    vector<statement*> incrInjections;
    string incrText;
    if(!deferredStep.empty()){}
    else {
    // The step is a statement, so it may be a plain assignment: `i = i * 3`.
    if(file.peekToken(1).is(eTokenType::identifier)){
        int k = 2;
        while(file.peekToken(k).is(token::period) && file.peekToken(k + 1).is(eTokenType::identifier)) k += 2;
        if(file.peekToken(k).is(token::assignment)){
            string path;
            for(int j = 1; j < k; j++) path += file.getToken().value;
            file.getToken();   // '='
            assignedTarget = func != nullptr ? qualifyIdentifier(path, func, body) : path;
            if(assignedTarget.empty()) parsingError(format("Undeclared variable '{0}'", path));
            if(isConstName(path, func, body)) parsingError(format("Cannot assign to const variable '{0}'", path));
        }
    }
    allowEmptyExpr = true;   // each part of `for(;;)` may be empty
    expression* incrExpr = parseExpression(file.getToken(), {token::parenClose}, func, body);
    allowEmptyExpr = false;
    incrInjections = pendingInjections;
    pendingInjections.clear();
    incrText = incrExpr ? incrExpr->text() : "";
    // A step that is only `i++` needs none of the brackets an operand gets.
    if(incrText.size() > 4 && incrText.front() == '(' && incrText.back() == ')'
       && (incrText.compare(incrText.size() - 3, 2, "++") == 0 || incrText.compare(incrText.size() - 3, 2, "--") == 0)
       && incrText.find_first_of("()", 1) == incrText.size() - 1)
        incrText = incrText.substr(1, incrText.size() - 2);
    // The step runs only for its effect: an assignment, `++`/`--` or a call.
    if((assignedTarget.empty() && incrInjections.empty() && !incrText.empty()
        && incrText.find('=') == string::npos && incrText.find("++") == string::npos
        && incrText.find("--") == string::npos && incrText.find('(') == string::npos)
       || incrText == "++" || incrText == "--")
        parsingError(incrText == "++" || incrText == "--"
            ? format("The loop's step '{0}' has no variable.", incrText)
            : format("The loop's step '{0}' does nothing: give it an assignment, `++`/`--` or a call.", incrText));
    if(!assignedTarget.empty()) incrText = assignedTarget + " = " + incrText;
    if(stepReopened) file.close();
    }
    if(!incrInjections.empty() || !deferredStep.empty()) forStmt.incrementText = "";
    else forStmt.incrementText = incrText;
    size_t continuesBefore = parsedContinues.size();
    int stepDepth = loopDepth + 1;
    forStmt.body = new statementBlock();
    functionDef forCtx;
    if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
    forCtx.body = forStmt.body;
    token next = file.getToken();
    if(!loopVarName.empty()) currentLoopVars.insert(loopVarName); loopDepth++; loopBlockDepths.push_back(activeBlockStack.size());
    if(next.is(token::braceOpen)){
        openCompileContext(eCompileContext::codeBlock, forStmt.body);
        while(processNextStatement(forCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    } else {
        processBracelessBody(next, forCtx);
    }
    if(!deferredStep.empty()){
        string label = format("_bglForStep{0}", doTestLabelCounter++);
        for(size_t i = continuesBefore; i < parsedContinues.size(); i++)
            if(parsedContinues[i].first == stepDepth) parsedContinues[i].second->text = "jump " + label + ";";
        i6RawNode* lbl = new i6RawNode();
        lbl->text = "." + label + ";";
        forStmt.body->statements.push_back(lbl);
        openCompileContext(eCompileContext::codeBlock, forStmt.body);
        file.openText(deferredStep + ";", stmtLoc.file, stmtLoc.line);
        try { processStatementDispatch(file.getToken(), forCtx); } catch(...) { file.close(); closeCompileContext(eCompileContext::codeBlock); throw; }
        file.close();
        closeCompileContext(eCompileContext::codeBlock);
    }
    // If ternary in increment: append injections + increment as last body statements
    if(!incrInjections.empty()){
        for(statement* inj : incrInjections) forStmt.body->statements.push_back(inj);
        i6RawNode* incrStmt = new i6RawNode();
        incrStmt->text = incrText + ";";
        forStmt.body->statements.push_back(incrStmt);
    }
    loopDepth--; loopBlockDepths.pop_back(); if(!loopVarName.empty()) currentLoopVars.erase(loopVarName);
    moveConditionIntoBody(forStmt.condition, condSetup, forStmt.body);
    if(body != nullptr) body->statements.push_back(&forStmt);
    return false;
}

// Parses the inline-initializer-list for-in, `for(T x in {a, b, c})`, from its opening brace
// (the caller has consumed "in" and peeked the brace). `elemVarType` may be "auto", inferred here
// from the first element. The elements are iterated out of the shared word-based scratch buffer.
bool bglParser::processForInLiteralList(const std::string& elemVarName, std::string elemVarType,
                                        const sourceLocation& stmtLoc, class functionDef* func, class statementBlock* body) {
    file.getToken(); // consume '{'
    vector<expression*> elements;
    token et = file.getToken();
    while(!et.is(token::braceClose) && !et.is(eTokenType::eof)){
        expression* elem = parseExpression(et, {",", token::braceClose}, func, body);
        elements.push_back(elem);
        if(elem->terminator == token::braceClose) break;
        et = file.getToken();
    }
    file.getToken(token::parenClose);

    // The list is a hidden local `array<var> = {…}`: storage per call, like any local array, so loops
    // over lists nest and recurse. Its elements are words, as a list's values are.
    string arrName = format("_bglfia{0}", forInCounter++);
    arrayDeclaration& tmpDecl = *(new arrayDeclaration());
    tmpDecl.name = arrName;
    tmpDecl.src = stmtLoc;
    tmpDecl.type = languageService.getType("array");
    tmpDecl.elementType = "var";
    tmpDecl.inLoop = loopDepth > 0;
    tmpDecl.isSynthetic = true;   // for-in array temp — compiler-generated, hidden in the debugger
    auto* listInit = new initializerList();
    listInit->elements = elements;
    tmpDecl.declaredExpressionValue = listInit;
    if(body != nullptr) body->statements.push_back(&tmpDecl);

    string counterName = format("_bglfi{0}", forInCounter++);
    variableDeclaration& counterDecl = *(new variableDeclaration());
    counterDecl.name = counterName;
    counterDecl.type = languageService.getType("var");
    counterDecl.isSynthetic = true;   // for-in counter temp — compiler-generated, hidden in the debugger
    if(body != nullptr) body->statements.push_back(&counterDecl);

    if(elemVarType == "auto" && !elements.empty() && !elements[0]->resolvedType.empty()){
        elemVarType = elements[0]->resolvedType;
        classDef* elemCls = languageService.classOf(elemVarType);
        if(elemCls)
            for(typeMember* m : elemCls->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == "auto"){ elemVarType = fd->returnType.name; break; }
        if(body != nullptr)
            for(statement* s : body->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == elemVarName){ vd->type = languageService.getType(elemVarType); break; }
    } else if(elemVarType == "auto"){
        elemVarType = "var";
    } else if(elemVarType != "var" && !elements.empty()){
        for(size_t i = 0; i < elements.size(); i++){
            string et = elements[i]->resolvedType;
            if(!et.empty() && !isTypeCompatible(et, elemVarType))
                parsingError(format("'for in': inline list element {0} has type '{1}', incompatible with loop variable type '{2}'",
                    i, et, elemVarType));
        }
    }

    forInStatement& fi = *(new forInStatement());
    fi.src = stmtLoc;
    fi.elementVar = elemVarName;
    if(auto* ga = languageService.findGlobalAs<arrayDeclaration>(arrName); ga != nullptr && !ga->i6name.empty()) arrName = ga->i6name;
    fi.arrayVar   = arrName;
    fi.counterVar = counterName;
    // Inline-list for-in always iterates the word-based scratch buffer
    // (_bglScratchStack), regardless of element type — char values are stored
    // and read as words there. So the word template is correct even for
    // `for(char c in {'a','b'})`; the byte template would misread the scratch.
    fi.isByteArray = false;
    fi.body = new statementBlock();
    functionDef forCtx;
    if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
    forCtx.body = fi.body;
    paramDef& elemParam = *(new paramDef());
    elemParam.name = elemVarName;
    elemParam.type = languageService.getType(elemVarType);
    // getType returns the base "func" type for func<...>; keep the full parameterized
    // name so a func-typed loop var is recognized as callable (e.g. `for(func<E> r ...) r()`).
    if(elemVarType.rfind("func<", 0) == 0) elemParam.type.name = elemVarType;
    forCtx.params.push_back(&elemParam);
    token next = file.getToken();
    currentLoopVars.insert(elemVarName); loopDepth++; loopBlockDepths.push_back(activeBlockStack.size());
    if(next.is(token::braceOpen)){
        openCompileContext(eCompileContext::codeBlock, fi.body);
        while(processNextStatement(forCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    } else {
        processBracelessBody(next, forCtx);
    }
    loopDepth--; loopBlockDepths.pop_back(); currentLoopVars.erase(elemVarName);
    if(body != nullptr) body->statements.push_back(&fi);
    return false;
}

// Parses the container form of for-in — `for(T x in expr)` — after the caller has consumed "in":
// the `1 to 10` range form, arrays (local/global/object member/parameter), `obj.children` world-
// tree iteration and <string> containers. `elemVarType` may be "auto", inferred from the element
// type of the container.
bool bglParser::processForIn(const std::string& elemVarName, std::string elemVarType,
                             const sourceLocation& stmtLoc, class functionDef* func, class statementBlock* body) {
    // Range for-in: for(int i in 1 to 10)
    // The source's set-up (a lambda's captures) runs before the loop, and its copy-back after it.
    vector<statement*> sourceAfter;
    auto takeSourceSetUp = [&](){
        for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
        pendingInjections.clear();
        sourceAfter.insert(sourceAfter.end(), postInjections.begin(), postInjections.end());
        postInjections.clear();
    };
    expression* arrExpr = parseExpression(file.getToken(), {token::parenClose, "to"}, func, body);
    if(arrExpr->terminator == "to"){
        expression* rangeEnd = parseExpression(file.getToken(), {token::parenClose}, func, body);
        takeSourceSetUp();
        forStatement& forStmt = *(new forStatement());
        forStmt.src = stmtLoc;
        forStmt.initText = elemVarName + " = " + arrExpr->text();
        expression* cond = new expression();
        cond->tokens.push_back(elemVarName);
        cond->tokens.push_back("<=");
        cond->tokens.push_back(rangeEnd->text());
        forStmt.condition = cond;
        forStmt.incrementText = elemVarName + "++";
        forStmt.body = new statementBlock();
        functionDef forCtx;
        if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
        forCtx.body = forStmt.body;
        token next = file.getToken();
        currentLoopVars.insert(elemVarName); loopDepth++; loopBlockDepths.push_back(activeBlockStack.size());
        if(next.is(token::braceOpen)){
            openCompileContext(eCompileContext::codeBlock, forStmt.body);
            while(processNextStatement(forCtx) == false){}
            closeCompileContext(eCompileContext::codeBlock);
        } else {
            processBracelessBody(next, forCtx);
        }
        loopDepth--; loopBlockDepths.pop_back(); currentLoopVars.erase(elemVarName);
        if(body != nullptr) body->statements.push_back(&forStmt);
        for(statement* inj : sourceAfter) if(body != nullptr) body->statements.push_back(inj);
        return false;
    }
    takeSourceSetUp();

    // Array for-in
    string arrExprText = arrExpr ? arrExpr->text() : "";
    string arrName;
    string arrElemType = "";

    if(body != nullptr)
        for(statement* s : body->statements)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(s))
                if(ad->name == arrExprText){ arrElemType = ad->elementType; arrName = arrExprText; break; }
    if(arrName.empty())
        if(auto* ad = languageService.findGlobalAs<arrayDeclaration>(arrExprText)){ arrElemType = ad->elementType; arrName = arrExprText; }
    if(arrName.empty() && currentObject != nullptr){
        string memberName = (arrExprText.rfind("self.", 0) == 0) ? arrExprText.substr(5) : arrExprText;
        for(typeMember* m : currentObject->members)
            if(auto* ad = dynamic_cast<arrayDeclaration*>(m))
                if(ad->name == memberName){ arrElemType = ad->elementType; arrName = arrExprText; break; }
    }
    if(arrName.empty() && func != nullptr)
        for(paramDef* p : func->params)
            if(p->name == arrExprText){
                string tn = p->type.name;
                // A rawArray<T> parameter is a bare I6 word pointer with no length header,
                // so its element count isn't known at compile time and can't be derived at
                // runtime (there's no count slot to read). Iterating it would silently walk
                // off the end. Require an explicit indexed loop bounded by a known length.
                if(tn.size() >= 8 && tn.substr(0,8) == "rawarray")
                    parsingError(format("'for in': cannot iterate rawArray parameter '{0}' — a rawArray has no "
                                        "length header, so its size isn't known. Loop explicitly with a known "
                                        "bound, e.g. `for(int i in 0 to n-1) {0}[i]`.", arrExprText));
                classDef* pc = languageService.classOf(tn);
                if(!isWordArrayType(tn) && tn != "bytearray"
                   && (languageService.findObjectType(tn) != nullptr || (pc != nullptr && isObjectBackedClass(pc))))
                    parsingError(format("'for ({0} in {1})': '{1}' is an object, not a collection. To visit what it "
                        "holds, iterate its children: `for (object x in {1}.children)`.", elemVarName, arrExprText));
                arrElemType = (tn.size() > 6 && tn.substr(0,6) == "array<") ? tn.substr(6, tn.size()-7) : "var";
                arrName = arrExprText;
                break;
            }

    // A member word array reached by a path (`cfg.limits`, `self.limits`): iterated in place.
    ArrayReceiver memberSrc;
    bool viaPointer = false;
    if(arrExpr != nullptr && isWordArrayType(arrExpr->resolvedType) && arrExprText.find('(') == string::npos
       && arrExprText.find('.') != string::npos){
        string recv = arrExprText;
        memberSrc = arrayReceiver(recv, arrExprText, arrExpr->resolvedType, func, body);
        viaPointer = !memberSrc.isMember;   // a ref member, read as its array
        if(!memberSrc.staticElemType.empty()){                     // a static array: one global
            arrName = recv; viaPointer = false;
            if(arrElemType.empty()) arrElemType = memberSrc.staticElemType;
        }
        if(memberSrc.isMember){
            arrName = arrExprText;
            if(arrElemType.empty()) arrElemType = resolveArrayElementTypeDotted(memberSrc.owner, memberSrc.prop, func, body);
            if(arrElemType.empty()) arrElemType = "var";
        }
    }

    // World-tree child collection: `for (o in container.children)` iterates the container's I6
    // children directly (objectloop), no array. Detected by the `.children` tail on the source.
    bool isChildrenSource = false;
    if(arrName.empty() && arrExprText.size() > 9 && arrExprText.substr(arrExprText.size() - 9) == ".children"){
        isChildrenSource = true;
        string owner = arrExprText.substr(0, arrExprText.size() - 9);
        arrName = func != nullptr ? qualifyIdentifier(owner, func, body) : owner;   // container's emitted name
        string ownerType = owner.find('.') == string::npos ? resolveIdentifierType(owner, func, body)
                                                           : resolvePathType(owner, func, body);   // `ns.shelf.children`
        if(owner.find('.') == string::npos){
            objectDef* od = languageService.findObjectType(owner);
            if(od == nullptr)   // the source may arrive already renamed for I6 (`_box.children`)
                for(typeDef* t : languageService.objectInstances)
                    if(auto* o = dynamic_cast<objectDef*>(t); o && !o->i6name.empty() && strcasecmp(o->i6name.c_str(), owner.c_str()) == 0){ od = o; break; }
            if(od != nullptr){
                if(ownerType.empty()) ownerType = od->name;
                if(arrName.empty()) arrName = objectI6Name(od->name);
            }
        }
        // `x.parent.children`: the owner is a read (`(parent(x))`), an object.
        if(arrName.empty() && !owner.empty() && owner[0] == '('){ arrName = owner; ownerType = "object"; }
        // Each child is an instance of the class that declares `children` (the world-tree root).
        function<classDef*(classDef*)> declaring = [&](classDef* c) -> classDef* {
            if(c == nullptr) return nullptr;
            for(typeMember* m : c->members) if(m->name == "children") return c;
            for(classDef* b : c->baseClasses) if(classDef* d = declaring(b)) return d;
            return nullptr;
        };
        classDef* decl = declaring(getDispatchClass(ownerType));
        arrElemType = decl != nullptr ? decl->name : "_bglobject";
    }

    if(!isChildrenSource && arrName.empty() && arrExpr != nullptr && !isWordArrayType(arrExpr->resolvedType)){
        // An object is not a collection (§5.9): its contents are its children.
        const string& t = arrExpr->resolvedType;
        classDef* tc = languageService.classOf(t);
        if(languageService.findObjectType(t) != nullptr || (tc != nullptr && isObjectBackedClass(tc)))
            parsingError(format("'for ({0} in {1})': '{1}' is an object, not a collection. To visit what it "
                "holds, iterate its children: `for (object x in {1}.children)`.", elemVarName, arrExprText));
    }
    // A member word array through a computed receiver (`((Bay)o).counts`): the receiver is read once, into
    // a local, and its array walked in place.
    string computedOwner;
    if(!isChildrenSource && !memberSrc.isMember && memberSrc.staticElemType.empty() && arrExpr != nullptr
       && arrExpr->memberArrayDecl != nullptr && arrName.empty() && arrExprText.find('(') != string::npos
       && arrExprText.size() > arrExpr->memberArrayProp.size()
       && arrExprText.compare(arrExprText.size() - arrExpr->memberArrayProp.size() - 1, string::npos, "." + arrExpr->memberArrayProp) == 0){
        computedOwner = format("_bglfio{0}", forInCounter++);
        variableDeclaration& od = *(new variableDeclaration());
        od.name = computedOwner; od.type = languageService.getType("var"); od.isSynthetic = true;
        i6RawNode& load = *(new i6RawNode());
        load.text = computedOwner + " = " + arrExpr->memberArrayOwner + ";";
        load.cooked = true;
        if(body != nullptr){ body->statements.push_back(&od); body->statements.push_back(&load); }
        arrName = computedOwner + "." + arrExpr->memberArrayProp;
        arrElemType = arrExpr->memberArrayDecl->elementType;
    }
    if(!isChildrenSource && arrName.empty()){
        arrName = format("_bglfia{0}", forInCounter++);
        variableDeclaration& tmpDecl = *(new variableDeclaration());
        tmpDecl.name = arrName;
        tmpDecl.type = languageService.getType("var");
        tmpDecl.isSynthetic = true;   // for-in array temp — compiler-generated, hidden in the debugger
        if(body != nullptr) body->statements.push_back(&tmpDecl);
        i6RawNode& assign = *(new i6RawNode());
        assign.text = arrName + " = " + arrExprText + ";";
        assign.cooked = true;   // a local named like the member (`made` beside `bench.made`) is still renamed
        if(body != nullptr) body->statements.push_back(&assign);
        arrElemType = "var";
    }

    if(elemVarType == "auto"){
        if(arrElemType.empty() || arrElemType == "var")
            elemVarType = "var";
        else {
            elemVarType = arrElemType;
            classDef* elemCls = languageService.classOf(elemVarType);
            if(elemCls)
                for(typeMember* m : elemCls->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m))
                        if(fd->name == "auto"){ elemVarType = fd->returnType.name; break; }
        }
        if(body != nullptr)
            for(statement* s : body->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(vd->name == elemVarName){ vd->type = languageService.getType(elemVarType); break; }
    }

    if(elemVarType != arrElemType && elemVarType != "var" && arrElemType != "var"
       && !isTypeCompatible(arrElemType, elemVarType))
        parsingError(format("'for in': variable '{0}' has type '{1}' but '{2}' has element type '{3}'",
            elemVarName, elemVarType, arrName, arrElemType));

    string counterName = format("_bglfi{0}", forInCounter++);
    variableDeclaration& counterDecl = *(new variableDeclaration());
    counterDecl.name = counterName;
    counterDecl.type = languageService.getType("var");
    counterDecl.isSynthetic = true;   // for-in counter temp — compiler-generated, hidden in the debugger
    if(body != nullptr) body->statements.push_back(&counterDecl);

    forInStatement& fi = *(new forInStatement());
    fi.src = stmtLoc;
    // A `<string>` is a managed object (chars via getChar()/operator[]), NOT a raw
    // byte buffer, so for-in over it can't use the raw byte template — that would
    // read the object handle as bytes and yield garbage. Reject it with guidance
    // until proper object-dispatch iteration lands. Detect by resolving the
    // container name to a string-typed local/global/param.
    auto resolvesToString = [&](const string& nm) -> bool {
        if(body != nullptr)
            for(statement* s : body->statements)
                if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                    if(!dynamic_cast<arrayDeclaration*>(vd) && vd->name == nm && vd->type.name == "string") return true;
        for(typeDef* g : languageService.globals)
            if(auto* vd = dynamic_cast<variableDeclaration*>(g))
                if(!dynamic_cast<arrayDeclaration*>(vd) && vd->name == nm && vd->type.name == "string") return true;
        if(func != nullptr)
            for(paramDef* p : func->params)
                if(p->name == nm && p->type.name == "string") return true;
        return false;
    };
    // A stringObj walks its characters too, through its own class's emitters.
    string stringClass = "string";
    if(arrExpr != nullptr && strcasecmp(arrExpr->resolvedType.c_str(), "stringobj") == 0) stringClass = "stringobj";
    bool isStringContainer = resolvesToString(arrExprText) || stringClass == "stringobj"
                          || (arrExpr != nullptr && arrExpr->resolvedType == "string");

    fi.elementVar = elemVarName;
    if(elemVarType != arrElemType && !arrElemType.empty() && arrElemType != "var" && elemVarType != "var"){
        size_t injected = pendingInjections.size();
        string conv = applyCastConversion("@@ELEM@@", arrElemType, elemVarType);
        if(pendingInjections.size() == injected && conv != "@@ELEM@@") fi.elementConversion = conv;
        pendingInjections.resize(injected);
    }
    // A value-class loop variable owns its instance, as any local of the class does: each element is
    // copied into it, so a write to the variable doesn't reach the array.
    if(fi.elementConversion.empty() && func != nullptr && !isStringContainer)
        if(classDef* vc = languageService.findClass(elemVarType);
           vc != nullptr && !vc->isEmitterClass && isValueClass(vc) && classHasStoredFields(vc)){
            // With no copy operator, the loop variable would share each element's instance.
            if(findMemberInHierarchy(vc, [&](typeMember* m){
                   auto* f = dynamic_cast<functionDef*>(m);
                   if(f == nullptr || f->name != "=") return false;
                   classDef* decl = vc->declaringClassOf(f);
                   return decl != nullptr && decl->name.rfind("_bgl", 0) != 0; }) == nullptr)
                parsingError(format("the loop variable '{0}' is a copy of each element, and {1} has no operator = to "
                                    "copy one; declare `{1} operator = ({1} o)`, or index the array.", elemVarName, vc->dName()));
            if(functionDef* op = findAssignOperator(vc, vc->name, [&](functionDef* f){
                   return f->params.size() == 1 && f->params[0]->type.name == vc->name; }, /*allowInherited*/true);
               op != nullptr && !op->isEmitter){
                mangleOverloadSetForReceiver(vc->name, op->name);
                if(op->i6name.empty()) op->i6name = mangleOperatorName(op->name);
                string fname = func->name;
                for(char& ch : fname) if(!isalnum((unsigned char)ch) && ch != '_') ch = '_';   // an operator's routine
                string name = format("_bglLoopv_{0}_{1}", fname, paramBackingNames.size());
                while(!paramBackingNames.insert(name).second) name += "_";
                auto* backing = new variableDeclaration();
                backing->name = name; backing->displayName = name;
                backing->type = languageService.getType(vc->name);
                backing->src = stmtLoc;
                backing->isInstanceBacking = true;
                languageService.registerInstance(*backing);
                func->ownBackings.push_back({name, vc->name});
                fi.elementConversion = format("({0}.{1}(@@ELEM@@) * 0 + {0})", name, op->i6name);
                // A slot nothing has filled holds no instance: the loop variable takes the class's defaults.
                string zero = name + "_zero";
                auto* zb = new variableDeclaration();
                zb->name = zero; zb->displayName = zero;
                zb->type = languageService.getType(vc->name);
                zb->src = stmtLoc;
                zb->isInstanceBacking = true;
                languageService.registerInstance(*zb);
                fi.elementDefault = zero;
            }
        }
    if(auto* ga = languageService.findGlobalAs<arrayDeclaration>(arrName); ga != nullptr && !ga->i6name.empty()) arrName = ga->i6name;
    fi.arrayVar   = arrName;
    fi.counterVar = counterName;
    fi.viaPointer = viaPointer;
    if(memberSrc.isMember){
        fi.memberBase   = memberSrc.owner + ".&" + propertyI6Name(memberSrc.prop);
        fi.memberLength = memberArraySizeText(memberSrc, "length", func, body);
        // Its declaration knows its layout when the owner's type doesn't (a cast: `((Bay)o).counts`).
        if(arrExpr != nullptr && arrExpr->memberArrayDecl != nullptr && memberArrayDeclTracked(arrExpr->memberArrayDecl)
           && arrExprText.size() > arrExpr->memberArrayProp.size()
           && arrExprText.compare(arrExprText.size() - arrExpr->memberArrayProp.size() - 1, string::npos, "." + arrExpr->memberArrayProp) == 0){
            string owner = objectI6Name(memberSrc.owner), prop = propertyI6Name(memberSrc.prop);
            fi.memberLength = "(" + owner + ".&" + prop + "-->(((" + owner + ".#" + prop + ")/WORDSIZE) - 1))";
        }
    } else if(!computedOwner.empty()){
        string prop = arrExpr->memberArrayProp;
        string words = "((" + computedOwner + ".#" + prop + ")/WORDSIZE)";
        fi.memberBase   = computedOwner + ".&" + prop;
        fi.memberLength = memberArrayDeclTracked(arrExpr->memberArrayDecl)
                        ? "(" + computedOwner + ".&" + prop + "-->(" + words + " - 1))" : words;
        fi.viaPointer = false;
    }
    // A string is iterated through its class's own emitters — `getLength()` bounds the loop and
    // `operator[](i)` reads each char — expanded here with markers the emitter replaces by the
    // container's and the counter's emitted names. `<string>` provides both.
    fi.isStringForIn = isStringContainer;
    if(isStringContainer){
        classDef* strCls = languageService.findClass(stringClass);
        auto emitterNamed = [&](const string& nm, size_t params, bool isValue) -> functionDef* {
            if(strCls == nullptr) return nullptr;
            return dynamic_cast<functionDef*>(findMemberInHierarchy(strCls, [&](typeMember* m){
                auto* fn = dynamic_cast<functionDef*>(m);
                return fn && fn->isEmitter && fn->name == nm && fn->params.size() == params
                       && fn->isValueEmitter == isValue && dynamic_cast<i6Block*>(fn->body) != nullptr;
            }));
        };
        functionDef* lenFn  = emitterNamed("length", 0, true);
        functionDef* charFn = emitterNamed("[]", 1, false);
        if(lenFn == nullptr || charFn == nullptr)
            parsingError("'for … in' over a string needs `#include <string>`, which gives string its length and characters.");
        emitterBindings lb; lb.self = "@@CONTAINER@@"; lb.val = "@@CONTAINER@@"; lb.trim = emitterTrim::wsSemi;
        fi.stringLengthText = expandEmitterBody(dynamic_cast<i6Block*>(lenFn->body), lb);
        emitterBindings cb; cb.self = "@@CONTAINER@@"; cb.val = "@@CONTAINER@@"; cb.trim = emitterTrim::wsSemi;
        cb.fn = charFn; cb.args.push_back("@@COUNTER@@");
        fi.stringCharText = expandEmitterBody(dynamic_cast<i6Block*>(charFn->body), cb);
    }
    fi.isChildrenForIn = isChildrenSource;
    // Byte iteration for array<char> (hybrid layout: length word -->0, data bytes
    // ->WORDSIZE). The elemVarType clause catches array<char> reached as an
    // external object member, where the element-type lookups above fall back to
    // `var` (the member-type probe only fires inside the owning object). A char
    // loop var over a word array is rejected as a type mismatch upstream. A string
    // container dispatches via getChar() above, so it must NOT take the byte path.
    fi.isByteArray = !isStringContainer && ((arrElemType == "char") || (elemVarType == "char"));
    fi.body = new statementBlock();
    functionDef forCtx;
    if(func != nullptr){ forCtx.returnType = func->returnType; forCtx.params = func->params; }
    paramDef& elemParam = *(new paramDef());
    elemParam.name = elemVarName;
    elemParam.type = languageService.getType(elemVarType);
    // getType returns the base type for templated names; keep the full parameterized name so a
    // func-typed loop var is recognized as callable (`for(func<E> r ...) r()`) and an array-typed
    // one (iterating an array-of-arrays) resolves its element type for subscript/length.
    if(elemVarType.rfind("func<", 0) == 0
       || elemVarType.rfind("array<", 0) == 0 || elemVarType.rfind("rawarray<", 0) == 0)
        elemParam.type.name = elemVarType;
    forCtx.params.push_back(&elemParam);
    forCtx.body = fi.body;
    token next = file.getToken();
    currentLoopVars.insert(elemVarName); loopDepth++; loopBlockDepths.push_back(activeBlockStack.size());
    if(next.is(token::braceOpen)){
        openCompileContext(eCompileContext::codeBlock, fi.body);
        while(processNextStatement(forCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    } else {
        processBracelessBody(next, forCtx);
    }
    loopDepth--; loopBlockDepths.pop_back(); currentLoopVars.erase(elemVarName);
    if(body != nullptr) body->statements.push_back(&fi);
    for(statement* inj : sourceAfter) if(body != nullptr) body->statements.push_back(inj);
    return false;
}

bool bglParser::processFor(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'for' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();
    // Caller already consumed "for" "("
    // Inform 6 separates the parts with `:`. Catch it here: read as Beguile, the header would run on
    // looking for its `;` to the end of the file.
    {
        int depth = 1, ternaries = 0;
        for(int k = 1; k < 400 && depth > 0; k++){
            token pk = file.peekToken(k);
            if(pk.is(eTokenType::eof) || pk.is(token::endStatement) || pk.is(token::braceOpen)) break;
            if(pk.is(token::parenOpen)) depth++;
            else if(pk.is(token::parenClose)) depth--;
            else if(pk.is("?")) ternaries++;
            else if(pk.is(":") && depth == 1){
                if(ternaries > 0) ternaries--;
                else parsingError("A `for` loop separates its parts with `;`, not `:` (Inform 6's form): for (init; condition; step)");
            }
        }
    }

    bool isForIn = false;
    string elemVarName, elemVarType;

    // A loop variable's type may be a `#using` alias (`tier`) or a dotted type path (`reg.part`).
    if(file.peekToken().is(eTokenType::identifier)){
        token first = file.getToken();
        token typed = first;
        bool isType = false;
        if(file.peekToken().is(token::period) && isNamespacedTypePath(first)){ typed = consumeTypeToken(first); isType = true; }
        else if(file.peekToken().is(eTokenType::identifier)
                && (file.peekToken(2).is("in") || file.peekToken(2).is(token::assignment) || file.peekToken(2).is(token::endStatement))){
            auto aliasIn = [&](const vector<typeMember*>& members){
                for(typeMember* m : members)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                        if(vd->isAlias && vd->name == first.value
                           && (languageService.isClassType(vd->type.name) || languageService.findEnum(vd->type.name) != nullptr)){
                            typed.value = vd->type.name; typed.tokenType = eTokenType::dataType; isType = true; return;
                        }
            };
            for(objectDef* imp : usingObjectImports) if(!isType) aliasIn(imp->members);
            for(classDef* imp : usingImports) if(!isType) aliasIn(imp->members);
        }
        file.pendingToken = isType ? typed : first;
        file.hasPendingToken = true;
    }

    token peek = file.peekToken();
    if(peek.isDataType()){
        token typeTok = file.getToken(eTokenType::dataType);
        if(typeTok.value == "func") typeTok.value = parseFuncType();  // func<...> loop var type
        else if(typeTok.value == "array" || typeTok.value == "rawarray") typeTok.value = parseArrayTypeTail(typeTok.value);  // array<...> / array<array<T>> loop var
        typeTok.value = maybeParseUnionTail(typeTok.value);   // `for (A | B x in …)`

        // Accept both identifier and dataType for the loop variable name. A dataType here means
        // the user chose a name that collides with a registered class (e.g. 'Counter'); the
        // shadow check below produces a cleaner error than a raw token-type mismatch.
        token nameTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
        // Shadow check: disallow loop variable names that collide with a global, a class member,
        // or an object member. Matches the parameter/local-variable shadow checks elsewhere.
        auto checkShadow = [&](const string& name) {
            for(typeDef* g : languageService.globals)
                if(g->name == name){
                    if(auto* vd = dynamic_cast<variableDeclaration*>(g)){
                        const string& t = vd->type.name;
                        if(t == "grammartoken" || t == "attribute" || t == "property" || t == "verb") continue;
                    }
                    if(auto* cd = dynamic_cast<classDef*>(g))   // a type, not a global `::` reaches
                        parsingWarning("Loop variable '" + name + "' has the name of " + (cd->isUnion() ? "union '" : "class '") + g->dName()
                            + "'; in the loop the name means the variable, though it still names the type in declarations.");
                    else
                        parsingWarning("Loop variable '" + name + "' shadows global of the same name; '::" + name + "' reaches the global.");
                    break;
                }
            if(definedSymbols.count(name))
                parsingWarning("Loop variable '" + name + "' has the name of a #define symbol; in the loop the name means the variable.");
            if(currentClass != nullptr){
                for(typeMember* m : currentClass->members)
                    if(m->name == name)
                        warnOnceHere("Loop variable '" + name + "' shadows a member of class '" + currentClass->dName() + "'.");
                // Walk base class hierarchy for inherited members (vars and functions) — warning only
                function<void(classDef*)> checkBases = [&](classDef* c){
                    for(typeMember* m : c->members)
                        if(m->name == name)
                            if(dynamic_cast<variableDeclaration*>(m) || dynamic_cast<functionDef*>(m))
                                warnOnceHere("Loop variable '" + name + "' shadows inherited member '" + name + "' from class '" + c->dName() + "'.");
                    for(classDef* base : c->baseClasses) checkBases(base);
                };
                for(classDef* base : currentClass->baseClasses) checkBases(base);
            }
            if(currentObject != nullptr)
                for(typeMember* m : currentObject->members)
                    if(m->name == name)
                        warnOnceHere("Loop variable '" + name + "' shadows a member of object '" + currentObject->name + "'.");
        };
        checkShadow(nameTok.value);
        // Inform 6 hoists the loop variable to the whole routine, so a global read above it would read it.
        if(currentFunc != nullptr && currentFunc->fileScopeReads.count(nameTok.value))
            parsingError(format("'{0}' is read above as the global, then declared here as a loop variable; Inform 6 "
                "would read the variable throughout the routine. Rename the loop variable.",
                nameTok.originalValue.empty() ? nameTok.value : nameTok.originalValue));
        if(file.peekToken().is("in")){
            isForIn = true;
            elemVarName = nameTok.value;
            elemVarType = typeTok.value;
            bool alreadyDeclared = false;
            if(body != nullptr)
                for(statement* s : body->statements)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == elemVarName && !vd->scopeEnded){
                            if(vd->type.name != elemVarType)
                                parsingError(format("Loop variable '{0}' redeclared with different type '{1}' (was '{2}')",
                                    elemVarName, elemVarType, vd->type.name));
                            alreadyDeclared = true;
                            break;
                        }
            if(!alreadyDeclared){
                variableDeclaration& elemDecl = *(new variableDeclaration());
                forInLoopDecl = &elemDecl;
                elemDecl.name = elemVarName;
                elemDecl.type = languageService.getType(elemVarType);
                // getType returns the base type for templated names (func<…>/array<…>); keep the
                // full parameterized name so the loop var is recognized as callable (func) or as a
                // typed array (subscript/length/element-type resolution).
                if(elemDecl.type.name.empty() || elemVarType.rfind("func<", 0) == 0 || isUnionType(elemVarType)
                   || elemVarType.rfind("array<", 0) == 0 || elemVarType.rfind("rawarray<", 0) == 0)
                    elemDecl.type.name = elemVarType;
                if(body != nullptr) body->statements.push_back(&elemDecl);
            }
        } else {
            // C-style for with typed init
            bool alreadyDeclared = false;
            if(body != nullptr)
                for(statement* s : body->statements)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == nameTok.value && !vd->scopeEnded){
                            if(vd->type.name != typeTok.value)
                                parsingError(format("Loop variable '{0}' redeclared with different type '{1}' (was '{2}')",
                                    nameTok.value, typeTok.value, vd->type.name));
                            alreadyDeclared = true;
                            break;
                        }
            classDef* varCls = getDispatchClass(typeTok.value);
            if(!alreadyDeclared && func != nullptr && varCls != nullptr && !varCls->isPrimitive
               && !plainSlotOwnsReferenceInstance(varCls)){
                // Declared as any local is, so it takes its buffer or instance and gives it back.
                file.openText((typeTok.originalValue.empty() ? typeTok.value : typeTok.originalValue) + " "
                              + (nameTok.originalValue.empty() ? nameTok.value : nameTok.originalValue) + ";", stmtLoc.file, stmtLoc.line);
                try { processStatementDispatch(file.getToken(), *func); } catch(...) { file.close(); throw; }
                file.close();
            } else if(!alreadyDeclared){
                variableDeclaration& loopVar = *(new variableDeclaration());
                loopVar.name = nameTok.value;
                loopVar.type = languageService.getType(typeTok.value);
                if(body != nullptr) body->statements.push_back(&loopVar);
            }
            return processForCStyle(nameTok.value, stmtLoc, func, body);
        }
    } else if(peek.is(eTokenType::identifier)){
        token nameTok = file.getToken(eTokenType::identifier);
        if(file.peekToken().is("in")){
            isForIn = true;
            elemVarName = nameTok.value;
            if(func != nullptr)
                for(paramDef* p : func->params)
                    if(p->name == elemVarName){ elemVarType = p->type.name; break; }
            if(elemVarType.empty() && body != nullptr)
                for(statement* s : body->statements)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == elemVarName){ elemVarType = vd->type.name; break; }
            if(elemVarType.empty())
                if(auto* vd = languageService.findGlobalAs<variableDeclaration>(elemVarName)) elemVarType = vd->type.name;
            if(elemVarType.empty())
                parsingError(format("'for in': iteration variable '{0}' is not declared", elemVarName));
        } else {
            // C-style for — nameTok was the first init token
            return processForCStyle(nameTok.value, stmtLoc, func, body);
        }
    }

    if(!isForIn){
        // C-style for — init starts with non-identifier or empty
        return processForCStyle("", stmtLoc, func, body);
    }

    // for-in shared (Form 1 and Form 2)
    file.getToken("in");

    // A loop variable declared in the head is the loop's: out of scope once the loop ends (§3.10).
    variableDeclaration* loopDecl = forInLoopDecl;
    forInLoopDecl = nullptr;
    bool r = file.peekToken(1).is(token::braceOpen)   // inline initializer list: for(int j in {1, 2, 3})
           ? processForInLiteralList(elemVarName, elemVarType, stmtLoc, func, body)
           : processForIn(elemVarName, elemVarType, stmtLoc, func, body);
    if(loopDecl != nullptr) loopDecl->scopeEnded = true;
    return r;
}

bool bglParser::processDo(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'do' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();
    doStatement& doStmt = *(new doStatement());
    doStmt.src = stmtLoc;
    doStmt.body = new statementBlock();
    functionDef doCtx;
    if(func != nullptr){ doCtx.returnType = func->returnType; doCtx.params = func->params; }
    doCtx.body = doStmt.body;
    // Caller already consumed "do" "{" — parse body
    loopDepth++; loopBlockDepths.push_back(activeBlockStack.size());
    size_t continuesBefore = parsedContinues.size();
    int bodyDepth = loopDepth;
    openCompileContext(eCompileContext::codeBlock, doStmt.body);
    while(processNextStatement(doCtx) == false){}
    closeCompileContext(eCompileContext::codeBlock);
    loopDepth--; loopBlockDepths.pop_back();
    // Expect 'while' or 'until'
    token keyword = file.getToken({eTokenType::identifier, eTokenType::dataType});
    if(keyword.is("while")) doStmt.isWhile = true;
    else if(!keyword.is("until")) parsingError(format("Expected 'while' or 'until' after do block, got '{0}'", keyword.value));
    file.getToken(token::parenOpen);
    doStmt.condition = parseExpression(file.getToken(), {token::parenClose}, func, body);
    // The condition's set-up runs at the end of every pass, before the test; a `continue` in this
    // loop jumps to the test, so it is sent to the set-up instead.
    if(!pendingInjections.empty()){
        string label = format("_bglDoTest{0}", doTestLabelCounter++);
        for(size_t i = continuesBefore; i < parsedContinues.size(); i++)
            if(parsedContinues[i].first == bodyDepth) parsedContinues[i].second->text = "jump " + label + ";";
        i6RawNode* lbl = new i6RawNode();
        lbl->text = "." + label + ";";
        doStmt.body->statements.push_back(lbl);
        for(statement* inj : pendingInjections) doStmt.body->statements.push_back(inj);
        pendingInjections.clear();
    }
    if(file.peekToken().is(token::endStatement)) file.getToken();
    if(body != nullptr) body->statements.push_back(&doStmt);
    return false;
}

bool bglParser::processSwitch(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'switch' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();
    // Caller already consumed "switch" "("
    switchStatement& swStmt = *(new switchStatement());
    swStmt.src = stmtLoc;
    swStmt.condition = parseExpression(file.getToken(), {token::parenClose}, func, body);
    for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
    pendingInjections.clear();
    string conditionType = swStmt.condition->resolvedType;
    // A named object is its own type; the switch compares instances of its class.
    if(objectDef* od = !conditionType.empty() ? languageService.findObjectType(conditionType) : nullptr; od && od->objectClass)
        conditionType = od->objectClass->name;
    classDef* condCls = !conditionType.empty() ? languageService.classOf(conditionType) : nullptr;
    if(condCls != nullptr){
        condCls->forEachMember([&](typeMember* m){
            if(auto* fn = dynamic_cast<functionDef*>(m))
                if(fn->name == "switch" && fn->isEmitter && fn->params.size() == 1)
                    if(auto* blk = dynamic_cast<i6Block*>(fn->body)){
                        string paramType = fn->params[0]->type.name;
                        if(swStmt.switchEmitters.find(paramType) == swStmt.switchEmitters.end()){
                            string b = expandEmitterBody(blk, {});
                            swStmt.switchEmitters[paramType] = fn->params[0]->name + "\t" + b;
                        }
                    }
        });
        if(!swStmt.switchEmitters.empty()) swStmt.needsIfChain = true;
    }
    file.getToken(token::braceOpen);
    switchBlockDepths.push_back(activeBlockStack.size());
    struct SwitchDepthPop { vector<size_t>& v; ~SwitchDepthPop(){ v.pop_back(); } } switchDepthPop{switchBlockDepths};
    while(true){
        token tt = file.getToken();
        if(tt.is(token::braceClose)) break;
        switchCase& sc = *(new switchCase());
        if(tt.is("default")){
            file.getToken(":");
        } else {
            tt.assert("case", "Expected 'case' or 'default' inside switch.");
            currentStatementSrc = tt.src;   // a diagnostic about the label is the case's own
            auto parseCaseExpr = [&]() -> expression* {
                // Bias case-value resolution toward the condition's type so an ambiguous name
                // resolves the right way — e.g. `switch(action){ case Open: }` picks the `Open`
                // verb, not the same-named `open` attribute.
                string savedExpectedCase = currentExpectedType;
                if(!conditionType.empty()) currentExpectedType = conditionType;
                expression* val = parseExpression(file.getToken(), {":", ",", "to"}, func, body);
                currentExpectedType = savedExpectedCase;
                foldCaseConstant(val);
                // The class's `operator switch` taking this value's type compares it; the emitter
                // looks its body up by the type recorded on the value.
                string emitterKey;
                if(!swStmt.switchEmitters.empty() && !val->resolvedType.empty()){
                    const string& vt = val->resolvedType;
                    if(swStmt.switchEmitters.count(vt)) emitterKey = vt;
                    for(auto& [k, b] : swStmt.switchEmitters)
                        if(emitterKey.empty() && k != "var" && isTypeCompatible(vt, k)) emitterKey = k;
                    if(emitterKey.empty() && swStmt.switchEmitters.count(literalBaseType(vt))) emitterKey = literalBaseType(vt);
                    if(emitterKey.empty() && swStmt.switchEmitters.count("var")) emitterKey = "var";
                }
                if(!emitterKey.empty()) val->resolvedType = emitterKey;
                // I6 compares case values as words; a string's text needs the type's own comparison.
                else if(val->resolvedType == "stringliteral" && swStmt.switchEmitters.empty())
                    parsingError(format("'{0}' declares no operator switch, so a string literal can't be a case value.",
                                        conditionType.empty() ? string("var") : typeDisplayName(conditionType)));
                else if(!conditionType.empty() && !val->resolvedType.empty()
                   && !isTypeCompatible(val->resolvedType, conditionType)
                   && !zeroFitsBnum(val, conditionType)
                   && val->resolvedType != "verb")
                {
                    // An object is its own type; name the class it is an instance of.
                    string shown = typeDisplayName(val->resolvedType);
                    if(objectDef* od = languageService.findObjectType(val->resolvedType); od && od->objectClass)
                        shown = od->objectClass->dName();
                    parsingError(format("Switch case type '{0}' does not match condition type '{1}'",
                                       shown, typeDisplayName(conditionType)));
                }
                return val;
            };
            // A type's `operator switch` tests one case value; a range or a guard has none to give it.
            auto rejectWithSwitchOperator = [&](const char* form){
                if(!swStmt.switchEmitters.empty())
                    parsingError(format("'{0}' compares cases with its operator switch, which tests one value, so a "
                        "{1} case can't use it. List the values, or test the value with `if`.",
                        typeDisplayName(conditionType), form));
            };
            // A class compares through its own operators (a float's bits aren't ordered as words):
            // `_bgl_sw op value` is parsed with both sides typed, then given their emitted text.
            auto comparesItself = [&]{
                return condCls != nullptr && findMemberInHierarchy(condCls, [](typeMember* m){
                    auto* fd = dynamic_cast<functionDef*>(m);
                    return fd && (fd->name == "<" || fd->name == "<=" || fd->name == ">" || fd->name == ">="
                                  || fd->name == "<=>"); }) != nullptr;
            };
            auto typedCompare = [&](const string& op, expression* val) -> string {
                string plain = "_bgl_sw " + op + " " + val->text();
                if(!comparesItself() || val->resolvedType.empty()) return plain;
                statementBlock* scope = new statementBlock();
                auto place = [&](const string& name, const string& type){
                    auto* vd = new variableDeclaration();
                    vd->name = name;
                    vd->type = languageService.getType(type);
                    if(vd->type.name.empty()) vd->type.name = type;
                    scope->statements.push_back(vd);
                };
                place("_bglswv_", conditionType);
                place("_bglcasev_", val->resolvedType);
                bool  savedHasPending = file.hasPendingToken;
                token savedPending    = file.pendingToken;
                eTokenType savedPrevType  = file.prevTokenType;
                string     savedPrevValue = file.prevTokenValue;
                file.openText("_bglswv_ " + op + " _bglcasev_;", stmtLoc.file, stmtLoc.line);
                file.hasPendingToken = false;
                expression* e = nullptr;
                try { e = parseExpression(file.getToken(), {";"}, func, scope); }
                catch(...){ file.close(); file.hasPendingToken = savedHasPending; file.pendingToken = savedPending;
                            file.prevTokenType = savedPrevType; file.prevTokenValue = savedPrevValue; throw; }
                file.close();
                file.hasPendingToken = savedHasPending; file.pendingToken = savedPending;
                file.prevTokenType = savedPrevType; file.prevTokenValue = savedPrevValue;
                string t = e != nullptr ? e->text() : plain;
                t = i6Emitter::replaceWord(t, "_bglswv_", "_bgl_sw");
                t = i6Emitter::replaceWord(t, "_bglcasev_", "(" + val->text() + ")");
                auto bare = [](string x){ x.erase(remove_if(x.begin(), x.end(), [](char c){ return c == ' ' || c == '(' || c == ')'; }), x.end()); return x; };
                return bare(t) == bare(plain) ? plain : t;
            };
            auto parseNextEntry = [&](expression*& lastExpr) {
                token peek = file.peekToken(1);
                if(peek.is(eTokenType::oper) && (peek.value==">"||peek.value==">="||peek.value=="<"||peek.value=="<=")){
                    rejectWithSwitchOperator("guard");
                    token op = file.getToken();
                    expression* val = parseCaseExpr();
                    caseEntry e;
                    e.guardCondition = typedCompare(op.value, val);
                    sc.entries.push_back(e);
                    swStmt.needsIfChain = true;
                    lastExpr = val;
                    return;
                }
                expression* val = parseCaseExpr();
                // I6 compares a case with a constant; a variable (a member hiding a #define, a local) has no place there.
                auto rejectVariableCase = [&](expression* v){
                    if(v == nullptr || !swStmt.switchEmitters.empty() || v->tokens.size() != 1) return;
                    const string t = v->text();
                    bool word = !t.empty() && (isalpha((unsigned char)t[0]) || t[0] == '_')
                             && all_of(t.begin(), t.end(), [](char c){ return isalnum((unsigned char)c) || c == '_' || c == '.'; });
                    if(!word) return;
                    bool variable = t.find('.') != string::npos
                        || (func != nullptr && (qualifyFromParams(t, func) || qualifyFromBodyLocals(t, body) || qualifyFromAncestorBlocks(t, body)));
                    if(!variable)
                        if(auto* gv = languageService.findGlobalAs<variableDeclaration>(t); gv != nullptr && !gv->isConst
                           && gv->type.name != "attribute" && gv->type.name != "property" && dynamic_cast<arrayDeclaration*>(gv) == nullptr
                           && !gv->isExternal)
                            variable = true;
                    if(variable)
                        parsingError(format("A case value is a constant, and '{0}' is a variable here; test it with `if`.",
                                            v->tokens[0].rfind("self.", 0) == 0 ? v->tokens[0].substr(5) : v->tokens[0]));
                };
                rejectVariableCase(val);
                if(val->terminator == "to"){
                    rejectWithSwitchOperator("range");
                    expression* high = parseCaseExpr();
                    rejectVariableCase(high);
                    caseEntry e;
                    // A range stays an I6 range unless the type's comparison is something other than the word compare.
                    string lo = typedCompare(">=", val), hi = typedCompare("<=", high);
                    if(lo != "_bgl_sw >= " + val->text() || hi != "_bgl_sw <= " + high->text()){
                        e.guardCondition = "(" + lo + ") && (" + hi + ")";
                        swStmt.needsIfChain = true;
                    } else {
                        e.rangeLow = val;
                        e.rangeHigh = high;
                    }
                    sc.entries.push_back(e);
                    lastExpr = high;
                } else {
                    caseEntry e;
                    e.value = val;
                    sc.entries.push_back(e);
                    lastExpr = val;
                }
            };
            expression* lastExpr = nullptr;
            parseNextEntry(lastExpr);
            while(lastExpr->terminator == ",")
                parseNextEntry(lastExpr);
        }
        sc.body = new statementBlock();
        functionDef caseCtx;
        if(func != nullptr){ caseCtx.returnType = func->returnType; caseCtx.params = func->params; }
        caseCtx.body = sc.body;
        // A case body is a block like any other, so it joins activeBlockStack for the duration —
        // which is what lets a nested block inside the case see a local the case declared. Every
        // other block statement (if, while, for, do, try, catch) does this; the case body was the
        // one that did not, so `case 1: int z = 0; if(c){ z = 1; }` reported z undeclared, while
        // the same read in the nested block's CONDITION resolved, that one going through the
        // case body directly rather than through the stack.
        openCompileContext(eCompileContext::codeBlock, sc.body);
        bool wroteStatement = false;
        while(true){
            token peek = file.peekToken();
            if(peek.is(token::braceClose) || peek.is("case") || peek.is("default")) break;
            token st = file.getToken();
            wroteStatement = true;
            processStatementDispatch(st, caseCtx);
        }
        closeCompileContext(eCompileContext::codeBlock);
        // No fall-through: a case with nothing in it, straight before another, does nothing.
        if(!wroteStatement && file.peekToken().is("case"))
            parsingWarning("This case is empty and cases don't fall through, so it does nothing; "
                           "list its values with the next case's (`case a, b:`).");
        // A `break` as the LAST statement of a case is a no-op in both lowerings — neither an I6
        // switch case nor an if-chain branch falls through — so drop it rather than emit a jump to
        // the very next instruction. Only a trailing one: a break anywhere earlier in the body is
        // the author ending the case early, and dropping THAT ran the statements after it.
        if(sc.body != nullptr && !sc.body->statements.empty()){
            if(auto* raw = dynamic_cast<i6RawNode*>(sc.body->statements.back()))
                if(!raw->isI6Island && raw->text == "break;") sc.body->statements.pop_back();
        }
        swStmt.cases.push_back(&sc);
    }
    if(swStmt.needsIfChain){
        languageService.switchTempNeeded = true;
        // `break` means "leave the switch". A native I6 switch gives that for free — I6's own
        // break leaves the innermost loop OR switch. An if-chain has no switch to leave, so the
        // same break would leave the enclosing LOOP instead (or fail to compile with no loop
        // around it), which made one source text mean two different things depending on whether
        // some other case happened to use a guard. Retarget them at a label after the chain.
        string label = format("_bgl_swend{0}", languageService.switchEndCounter++);
        bool used = false;
        for(switchCase* c : swStmt.cases) if(retargetSwitchBreaks(c->body, label)) used = true;
        if(used) swStmt.breakLabel = label;
    }
    if(body != nullptr) body->statements.push_back(&swStmt);
    return false;
}

// Rewrite every `break` that belongs to THIS switch into a jump to `label`; returns true if any
// were found. A break inside a nested loop or a nested switch belongs to that one, so those are
// not descended into — the same rule the loop-escape analysis uses (bglParserHelpers.cpp).
bool bglParser::retargetSwitchBreaks(statementBlock* blk, const string& label){
    if(blk == nullptr) return false;
    bool found = false;
    for(statement* st : blk->statements){
        if(auto* raw = dynamic_cast<i6RawNode*>(st)){
            if(raw->isI6Island) continue;          // author's raw I6 — not ours to rewrite
            size_t a = raw->text.find_first_not_of(" \t\r\n");
            size_t b = raw->text.find_last_not_of(" \t\r\n;");
            string core = (a == string::npos) ? "" : raw->text.substr(a, (b == string::npos ? raw->text.size() : b + 1) - a);
            if(core == "break"){ raw->text = "jump " + label + ";"; found = true; }
            continue;
        }
        if(auto* is = dynamic_cast<ifStatement*>(st)){
            if(retargetSwitchBreaks(is->thenBlock, label)) found = true;
            if(retargetSwitchBreaks(is->elseBlock, label)) found = true;
        }
    }
    return found;
}

bool bglParser::processTry(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'try' is not valid at global scope");
    if(beguilerSettings.target == "z3")
        parsingError("try/catch/throw requires Z-machine v5 or later (current target is Z3)");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();
    languageService.tryCatchNeeded = true;
    tryCatchStatement& tcStmt = *(new tryCatchStatement());
    tcStmt.id = languageService.tryCatchCounter++;
    tcStmt.src = stmtLoc;
    // Caller already consumed "try" "{"
    tcStmt.tryBody = new statementBlock();
    {
        functionDef tryCtx;
        if(func != nullptr){ tryCtx.returnType = func->returnType; tryCtx.params = func->params; }
        tryCtx.body = tcStmt.tryBody;
        tryBlockDepths.push_back(activeBlockStack.size());
        openCompileContext(eCompileContext::codeBlock, tcStmt.tryBody);
        try { while(processNextStatement(tryCtx) == false){} }
        catch(...) { tryBlockDepths.pop_back(); throw; }
        closeCompileContext(eCompileContext::codeBlock);
        tryBlockDepths.pop_back();
    }
    token catchTok = file.getToken();
    if(!catchTok.is("catch"))
        parsingError("Expected 'catch' after try block");
    file.getToken(token::parenOpen);
    token catchType = file.getToken({eTokenType::dataType, eTokenType::identifier});
    if(catchType.is(eTokenType::identifier)) catchType = consumeTypeToken(catchType);   // `catch (idx.stock.part p)`, an alias
    if(catchType.value == "func") catchType.value = parseFuncType();   // `catch (func<int, int> f)`
    else if(catchType.value == "array" || catchType.value == "rawarray") catchType.value = parseArrayTypeTail(catchType.value);
    token catchName = file.getToken(eTokenType::identifier);
    file.getToken(token::parenClose);
    tcStmt.catchVarType = (string)catchType;
    tcStmt.catchVarName = (string)catchName;
    file.getToken(token::braceOpen);
    tcStmt.catchBody = new statementBlock();
    {
        variableDeclaration& catchVar = *(new variableDeclaration());
        catchVar.name = tcStmt.catchVarName;
        catchVar.type = languageService.getType(tcStmt.catchVarType);
        if(catchVar.type.name.empty() || tcStmt.catchVarType.find('<') != string::npos) catchVar.type.name = tcStmt.catchVarType;
        statementBlock* funcBody = currentFunc ? dynamic_cast<statementBlock*>(currentFunc->body) : body;
        if(funcBody != nullptr) funcBody->statements.push_back(&catchVar);
        tcStmt.catchBody->statements.push_back(&catchVar);
        functionDef catchCtx;
        if(func != nullptr){ catchCtx.returnType = func->returnType; catchCtx.params = func->params; }
        catchCtx.body = tcStmt.catchBody;
        openCompileContext(eCompileContext::codeBlock, tcStmt.catchBody);
        while(processNextStatement(catchCtx) == false){}
        closeCompileContext(eCompileContext::codeBlock);
    }
    if(body != nullptr) body->statements.push_back(&tcStmt);
    return false;
}

// Single handler for all directive rules — delegates to the existing processDirective switch.

bool bglParser::processThrow(vector<token>& t, Qualifiers&, abstractObject& ctx) {
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'throw' is not valid at global scope");
    if(beguilerSettings.target == "z3")
        parsingError("try/catch/throw requires Z-machine v5 or later (current target is Z3)");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();
    languageService.tryCatchNeeded = true;
    throwStatement& throwStmt = *(new throwStatement());
    throwStmt.src = stmtLoc;
    // Caller consumed "throw" — read expression up to ;
    token valTok = file.getToken();
    throwStmt.value = parseExpression(valTok, {token::endStatement}, func, body);
    // The value's set-up (a `??` test, a temp) runs here, before the throw.
    for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
    pendingInjections.clear();
    // The blocks it leaves release their locals, as at a block's end: up to the innermost try here, or
    // all of them and the routine's own when nothing in this routine catches it.
    size_t boundary = tryBlockDepths.empty() ? 0 : tryBlockDepths.back();
    throwStmt.leavesRoutine = tryBlockDepths.empty();
    for(size_t i = activeBlockStack.size(); i > boundary; i--)
        if(auto it = blockCleanups.find(activeBlockStack[i - 1]); it != blockCleanups.end())
            for(auto r = it->second.rbegin(); r != it->second.rend(); ++r) throwStmt.releases.push_back(*r);
    if(body != nullptr) body->statements.push_back(&throwStmt);
    return false;
}

bool bglParser::processDelete(vector<token>& t, Qualifiers& q, abstractObject& ctx){
    if(getCurrentCompileContext() == eCompileContext::global)
        parsingError("'delete' is not valid at global scope");
    functionDef* func = dynamic_cast<functionDef*>(&ctx);
    statementBlock* body = func ? dynamic_cast<statementBlock*>(func->body) : nullptr;
    sourceLocation stmtLoc = file.currentLocation();
    // Caller consumed "delete" — read identifier (the variable holding the pool reference) and ;
    token nameTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
    if(nameTok.value == "outer" && accessorOuter != nullptr && file.peekToken().is(token::period)){
        nameTok.value = accessorOuter->name;
        nameTok.originalValue = accessorOuter->dName();
    }
    string varName = (string)nameTok;
    if(nameTok.is(eTokenType::dataType) && !file.peekToken().is(token::period)
       && !qualifyFromParams(varName, func) && !qualifyFromBodyLocals(varName, body) && languageService.findClass(varName) != nullptr)
        parsingError(format("'delete {0}': '{0}' is the class; delete takes a variable holding one of its instances.",
            nameTok.originalValue.empty() ? varName : nameTok.originalValue));
    // A dotted path (`delete holder.pack`) names a member, not a variable. Consume the rest of
    // the path so the type comes from the MEMBER rather than the head of the path — otherwise
    // `delete holder.pack` reported that `holder` is not a pooled class, which it never is.
    while(file.peekToken().is(token::period)){
        file.getToken(token::period);
        token seg = file.getToken({eTokenType::identifier, eTokenType::dataType});
        varName += "." + (string)seg;
    }
    if(file.peekToken().is(token::bracketOpen))
        parsingError(format("'delete {0}[…]': delete takes a variable or member, not an array element; copy the element to a local first",
            nameTok.originalValue.empty() ? varName : nameTok.originalValue));
    // Resolve the type — must be a pooled class.
    string varTypeName = varName.find('.') == string::npos
                       ? resolveIdentifierType(varName, func, body)
                       : resolvePathType(varName, func, body);
    if(varTypeName.empty())
        parsingError(format("'delete {0}': unknown variable", nameTok.originalValue.empty() ? varName : nameTok.originalValue));
    classDef* cls = languageService.classOf(varTypeName);
    while(cls != nullptr && cls->isAlias && !cls->baseClasses.empty()) cls = cls->baseClasses[0];   // the same type
    if(cls == nullptr || cls->poolSize == 0)
        parsingError(format("'delete {0}': '{1}' is not a pooled class. delete is only valid for instances of classes declared with `[N]` or `extern[]`.",
            nameTok.originalValue.empty() ? varName : nameTok.originalValue, varTypeName));
    file.getToken(token::endStatement);
    // Emit as `ClassName.destroy(varName);`
    string qualifiedVar = varName;
    if(func != nullptr && varName.find('.') == string::npos){
        qualifiedVar = qualifyIdentifier(varName, func, body);
        if(qualifiedVar.empty()) qualifiedVar = varName;
    }
    i6RawNode& node = *(new i6RawNode());
    node.text = cls->i6Name() + ".destroy(" + qualifiedVar + ");";
    node.cooked = true;   // names a Beguile local, renamed like any other use
    node.src = stmtLoc;
    pendingDeletes.push_back({&node, cls, qualifiedVar});
    if(body != nullptr) body->statements.push_back(&node);
    return false;
}

// An object goes back to the pool of the class it was made from, which may be a pooled subclass of the
// type the variable was declared as; those are only all known once parsing ends.
void bglParser::resolveDeletes(){
    for(const PendingDelete& d : pendingDeletes){
        vector<classDef*> subs;
        for(typeDef* g : languageService.globals)
            if(auto* c = dynamic_cast<classDef*>(g); c && c != d.cls && c->poolSize > 0 && !c->isAlias
               && find(subs.begin(), subs.end(), c) == subs.end()){
                vector<classDef*> up = c->ancestorsNearestFirst();
                if(find(up.begin(), up.end(), d.cls) != up.end()) subs.push_back(c);
            }
        if(subs.empty()) continue;
        stable_sort(subs.begin(), subs.end(), [](classDef* a, classDef* b){
            return a->ancestorsNearestFirst().size() > b->ancestorsNearestFirst().size(); });
        string text;
        for(classDef* c : subs)
            text += "if(" + d.var + " ofclass " + c->i6Name() + ") " + c->i6Name() + ".destroy(" + d.var + "); else ";
        d.node->text = text + d.cls->i6Name() + ".destroy(" + d.var + ");";
    }
    pendingDeletes.clear();
}


// `++x;` / `--x;` — prefix increment or decrement as a whole statement.
bool bglParser::processCallSubscriptStatement(token head, StatementContext& sc){
    string raw = file.getRawTextToStatementEnd(1);
    size_t at = 0;
    for(int depth = 1; at < raw.size(); at++){
        char c = raw[at];
        if(c == '"' || c == '\''){
            char q = c;
            for(at++; at < raw.size() && raw[at] != q; at++) if(raw[at] == '\\') at++;
            continue;
        }
        if(c == '(' || c == '[' || c == '{') depth++;
        else if((c == ')' || c == ']' || c == '}') && --depth == 0) break;
    }
    string call = (head.originalValue.empty() ? head.value : head.originalValue) + "(" + raw.substr(0, at + 1);
    string rest = raw.substr(at + 1);
    string type;
    {
        file.openText(call + ";", sc.src.file, sc.src.line);
        vector<statement*> savedPending = pendingInjections;
        try { type = parseExpression(file.getToken(), {token::endStatement}, sc.func, sc.body)->resolvedType; }
        catch(...) { file.close(); throw; }
        file.close();
        pendingInjections = savedPending;   // only the type was wanted: the declaration below runs the call
    }
    if(type.empty() || type == "void")
        parsingError(format("'{0}' gives nothing to subscript.", call));
    static int counter = 0;
    string tmp = format("_bglcr{0}", counter++);
    bool binds = isWordArrayType(type) || type == "bytearray" || [&]{ classDef* c = getDispatchClass(type); return c != nullptr && isValueClass(c); }();
    string decl = binds ? format("ref {0} {1} := {2};", typeDisplayName(type), tmp, call)
                        : format("{0} {1} = {2};", typeDisplayName(type), tmp, call);
    for(const string& text : {decl, tmp + rest + ";"}){
        file.openText(text, sc.src.file, sc.src.line);
        try { processStatementDispatch(file.getToken(), *sc.func); } catch(...) { file.close(); throw; }
        file.close();
    }
    return false;
}

bool bglParser::processPrefixIncDec(token op, StatementContext& sc){
    // `++((P)o).n;`
    if(file.peekToken(1).is(token::parenOpen)){
        file.getToken();
        if(rewriteComputedReceiverStatement("(", 1, sc, op.value)) return false;
        parsingError(format("'{0}' needs a variable or member to change", op.value));
    }
    // A target reached through a call or subscript (`++f().n`, `++arr[i].n`): parse it as the path
    // statement it starts, which finishes this operation when it reaches the `;`.
    {
        int k = 2;
        while(file.peekToken(k).is(token::period) && file.peekToken(k + 1).is(eTokenType::name)) k += 2;   // a member may lex as a type
        if(file.peekToken(1).is(eTokenType::identifier)
           && (file.peekToken(k).is(token::parenOpen) || file.peekToken(k).is(token::bracketOpen) || file.peekToken(k).is("?."))){
            pendingPrefixOp = op;
            token first = file.getToken();
            token symbol = parseStatementPath(first, sc);
            dispatchPathStatement(first, symbol, sc);
            if(pendingPrefixOp)
                parsingError(format("'{0}' needs a variable or member to change", op.value));
            return false;
        }
    }
    token varName = file.getToken(eTokenType::identifier);
    // A member path (`++self.count`), as the postfix form takes.
    while(file.peekToken(1).is(token::period) && file.peekToken(2).is(eTokenType::name)){
        file.getToken();
        varName.value += "." + file.getToken().value;
    }
    file.getToken(token::endStatement);
    return finishPrefixIncDec(op, varName, sc);
}

// `ClassName.member` naming a static member: the global it is stored in, or "" for any other path.
// `Store.kept` written above its class: the class's pre-scan knows it is a static array.
bool bglParser::staticArrayDeclaredBelow(const string& path){
    size_t dot = path.rfind('.');
    if(dot == string::npos) return false;
    string owner = path.substr(0, dot);
    classDef* cls = languageService.findClass(owner);
    if(cls == nullptr) if(string t = resolveNamespacedType(owner); !t.empty()) cls = languageService.findClass(t);
    return cls != nullptr && findMemberInHierarchy(cls, [&](typeMember* m){
        auto* ad = dynamic_cast<arrayDeclaration*>(m);
        return ad && ad->isStatic && ad->isPrePassStub && ad->name == path.substr(dot + 1); }) != nullptr;
}

string bglParser::staticMemberGlobal(const string& pathIn){
    bool global = pathIn.rfind("::", 0) == 0;   // `::Valve.total`: the class at file scope
    const string path = global ? pathIn.substr(2) : pathIn;
    size_t dot = path.rfind('.');
    if(dot == string::npos) return "";
    string ownerPath = path.substr(0, dot), propName = path.substr(dot + 1);
    classDef* cls = nullptr;
    // A path from a member, written in its own object's code: the member is nearer than a global.
    if(!global && currentObject != nullptr)
        for(typeMember* m : currentObject->members)
            if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd != nullptr
               && vd->name == ownerPath.substr(0, ownerPath.find('.'))){
                if(string t = resolveNamespacedType(currentObject->name + "." + ownerPath); !t.empty()) cls = languageService.findClass(t);
                break;
            }
    if(cls == nullptr) cls = languageService.findClass(ownerPath);
    if(cls == nullptr && ownerPath.find('.') != string::npos)
        if(string t = resolveNamespacedType(ownerPath); !t.empty()) cls = languageService.findClass(t);
    // An instance's static (`valve.total`, a member named like its class): the class's one variable.
    classDef* instanceCls = nullptr;
    if(!global && currentFunc != nullptr){
        statementBlock* b = !activeBlockStack.empty() ? activeBlockStack.back() : dynamic_cast<statementBlock*>(currentFunc->body);
        string t = ownerPath.find('.') == string::npos ? resolveIdentifierType(ownerPath, currentFunc, b, propName)
                                                       : resolvePathType(ownerPath, currentFunc, b);
        instanceCls = getDispatchClass(t);
    }
    if(instanceCls != nullptr && instanceCls->isPrimitive) instanceCls = nullptr;
    if(instanceCls != nullptr && (cls == nullptr || resolveIdentifierType(ownerPath.substr(0, ownerPath.find('.')), currentFunc, nullptr,
                                                                          ownerPath.find('.') == string::npos ? propName : ownerPath.substr(ownerPath.find('.') + 1)) != ""))
        if(findMemberInHierarchy(instanceCls, [&](typeMember* m){
               auto* vd = dynamic_cast<variableDeclaration*>(m); return vd && vd->isStatic && vd->name == propName; }))
            cls = instanceCls;
    if(cls == nullptr) return "";
    typeMember* sm = findMemberInHierarchy(cls, [&](typeMember* m){
        auto* vd = dynamic_cast<variableDeclaration*>(m); return vd && vd->isStatic && vd->name == propName; });
    return sm != nullptr ? "_bgl_" + cls->declaringClass(sm)->name + "_" + propName : "";
}

bool bglParser::finishPrefixIncDec(token op, token varName, StatementContext& sc){
    const sourceLocation& stmtLoc = sc.src;
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    const token& tok = op;
    string lhs = func != nullptr ? qualifyIdentifier(varName.value, func, body) : varName.value;
    if(lhs.empty()) parsingError(format("Undeclared variable '{0}'", varName.value));
    lhs = withMemberI6Name(varName.value, lhs, func, body);
    if(string g = staticMemberGlobal(varName.value); !g.empty()) lhs = g;
    if(isConstName(varName.value, func, body))
        parsingError(format("Cannot assign to const variable '{0}'", varName.value));
    // "prefix++" / "prefix--" first, falling back to the plain "++" / "--".
    emitIncDecStatement(varName, lhs, {"prefix" + tok.value, tok.value}, sc);
    return false;
}

// Completes the statement head: typed-literal detection, then the dotted / `?.` member path,
// accumulated into `tok`. Returns the symbol token that follows the path.
token bglParser::parseStatementPath(token& tok, StatementContext& sc){
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    // Determine if this token is a literal with a registered class (e.g. intLiteral, stringLiteral).
    // If so, it may head a method call: "hello".print() or 42.someMethod().
    // literalSelfText holds the I6 text to substitute for $self in emitter bodies.
    string& literalTypeName = sc.literalTypeName;
    string& literalSelfText = sc.literalSelfText;
    {
        auto resolveLiteralType = [&]() -> pair<string,string> {
            if(tok.is(eTokenType::integer))       return {"intliteral",    tok.value};
            if(tok.isString())                    return {"stringliteral", tok.value};
            if(tok.is(eTokenType::charLiteral))   { bool bare = (!tok.value.empty() && all_of(tok.value.begin(),tok.value.end(),::isdigit)) || tok.value.rfind("@",0)==0; return {"charliteral", bare ? tok.value : "'" + tok.value + "'"}; }
            return {"",""};
        };
        auto [tn, st] = resolveLiteralType();
        // Only treat as a typed literal if a classDef is actually registered for it.
        if(!tn.empty() && languageService.findClass(tn) != nullptr){
            literalTypeName = tn;
            literalSelfText = st;
        }
    }
    bool tokIsLiteral = !literalTypeName.empty();
    // A variable or member named like a type (`row` in a class beside `class Row`): Beguile is
    // case-insensitive, so the lexer saw the type; no declaration follows, so it is the variable.
    string headHint = file.peekToken(1).is(token::period) || file.peekToken(1).is("?.") ? file.peekToken(2).value : string();
    if(tok.is(eTokenType::dataType) && !tokIsLiteral && !resolveIdentifierType(tok.value, sc.func, sc.body, headHint).empty())
        tok.tokenType = eTokenType::identifier;
    if(!tok.is(eTokenType::identifier) && !tokIsLiteral)
        parsingError(format("Unrecognized statement starting with token '{0}'", (string) tok));
    if(tok.is(eTokenType::identifier) && !tok.isDataType() && file.peekToken(1).is(eTokenType::identifier))
        unknownTypeError(tok);   // `T name` — only a type can lead a statement followed by a name

    //make sure the identifier is complete, including any member access paths (chain all dots and ?.)
    token symbol = file.getToken({eTokenType::symbol, eTokenType::oper});
    // `outer.m` names the host object, so its path is the host's.
    if(tok.value == "outer" && accessorOuter != nullptr && (symbol.is(token::period) || symbol.is(token::bracketOpen))){
        tok.value = accessorOuter->name;
        tok.originalValue = accessorOuter->dName();
    }
    int optionalChainDepth = 0; // number of ?. guards opened
    while(symbol.is(token::period) || symbol.is("?.")) {
        if(symbol.is("?.")){
            // Optional chaining at statement level: emit if(nullTest){ as pre-injection, } as post-injection
            string pathSoFar = func != nullptr ? qualifyIdentifier(tok.value, func, body) : tok.value;
            if(pathSoFar.empty()) pathSoFar = tok.value;
            // The head as its path (`self.head`, `ns.head` for an import): the steps after it go on from there.
            if(tok.value.find('.') == string::npos && pathSoFar != tok.value && pathSoFar.find('.') != string::npos
               && all_of(pathSoFar.begin(), pathSoFar.end(), [](char c){ return isalnum((unsigned char)c) || c == '_' || c == '.'; })
               && !resolvePathType(pathSoFar, func, body).empty())
                tok.value = pathSoFar;
            string pathType = resolveIdentifierType(tok.value, func, body);
            if(pathType.empty()) pathType = resolvePathType(tok.value, func, body);
            // `panel.focus?.n += 1`: an accessor is tested, and written through, as what its getter reads.
            if(functionDef* g = func != nullptr && !pathType.empty() ? nativeGetterOf(pathType) : nullptr;
               g != nullptr && !declaresOwnOperator(pathType, "?")){
                classDef* rc = getDispatchClass(g->returnType.name);
                if(rc != nullptr && !rc->isPrimitive){
                    static int counter = 0;
                    string tmp = format("_bglacc_q{0}", counter++);
                    file.openText(g->returnType.name + " " + tmp + " = " + tok.value + ";", sc.src.file, sc.src.line);
                    try { processStatementDispatch(file.getToken(), *func); } catch(...) { file.close(); throw; }
                    file.close();
                    tok.value = tok.originalValue = tmp;
                    pathSoFar = tmp;
                    pathType = g->returnType.name;
                }
            }
            classDef* cls = !pathType.empty() ? languageService.classOf(pathType) : nullptr;
            functionDef* nullTestFn = nullptr;
            if(cls != nullptr)
                nullTestFn = findQueryOp(cls, pathType);
            if(nullTestFn == nullptr)
                parsingError(format("Type '{0}' does not support optional chaining (no operator?() emitter)", pathType));
            auto* blk = dynamic_cast<i6Block*>(nullTestFn->body);
            emitterBindings gb; gb.self = pathSoFar; gb.val = pathSoFar; gb.trim = emitterTrim::wsSemi;
            i6RawNode* openNode = new i6RawNode();
            openNode->text = "if (" + expandEmitterBody(blk, gb) + ") {";
            pendingInjections.push_back(openNode);
            optionalChainDepth++;
        }
        // member name after `.` may collide (case-insensitively) with a type name, in which
        // case the lexer classifies it as `dataType` — accept both so the member is reachable.
        token nextPart = file.getToken({eTokenType::identifier, eTokenType::dataType});
        tok.value += "." + nextPart.value;
        // Keep originalValue in sync so loose-mode displayFunctionName preserves case
        // across the full dotted path (e.g. "RedSpell.cast", not just "RedSpell").
        if(tok.originalValue.empty()) tok.originalValue = tok.value;
        else tok.originalValue += "." + (nextPart.originalValue.empty() ? nextPart.value : nextPart.originalValue);
        // A static array (`Store.kept`) is its global array, which every array statement already handles.
        if(string g = staticMemberGlobal(tok.value); !g.empty() && (languageService.findGlobalAs<arrayDeclaration>(g) != nullptr
                                                                    || staticArrayDeclaredBelow(tok.value)))
            tok.value = g;
        symbol = file.getToken({eTokenType::symbol, eTokenType::oper});
    }
    // Generate matching close braces as post-injections
    for(int i = 0; i < optionalChainDepth; i++){
        i6RawNode* closeNode = new i6RawNode();
        closeNode->text = "}";
        postInjections.push_back(closeNode);
    }

    // A literal with no chained method call is meaningless as a statement.
    if(tokIsLiteral && tok.value.find('.') == string::npos)
        parsingError(format("Literal value cannot appear as a statement without a method call"));
    return symbol;
}

// `name;` / `a.b.c;` — a value emitter used as a statement. Returns true when it handled the
// statement, false to let the caller go on matching.
bool bglParser::processValueEmitterStatement(token tok, StatementContext& sc){
    const sourceLocation& stmtLoc = sc.src;
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    string ident = tok.value;
    // Use qualifyIdentifier to resolve dot-paths, aliases, and #using imports
    string qualified = qualifyIdentifier(ident, func, body);
    // A `#using` import only supplies the path's missing prefix (`strings.banner` →
    // `lib.strings.banner`); the full path still has to be resolved.
    if(qualified.size() > ident.size() && qualified.compare(qualified.size() - ident.size() - 1, string::npos, "." + ident) == 0){
        string full = qualifyIdentifier(qualified, func, body);
        if(!full.empty()) qualified = full;
    }
    if(!qualified.empty() && qualified != ident && qualified.find('.') != string::npos && ident.find('.') != string::npos){
        // A member reached by path (`bgl.printRules.bold`): the expression parser expands its emitter.
        file.openText(ident + ";", stmtLoc.file, stmtLoc.line);
        try {
            expression* e = parseExpression(file.getToken(), {token::endStatement}, func, body);
            qualified = e->text();
        } catch(...){ file.close(); throw; }
        file.close();
    }
    // A constant (an enum value, a #define) does nothing as a statement.
    if(!qualified.empty() && qualified != ident && qualified.find_first_not_of("0123456789-$ ") == string::npos)
        parsingError(format("'{0}' is a value, which does nothing as a statement: assign it or pass it to a call.",
                            tok.originalValue.empty() ? ident : tok.originalValue));
    if(!qualified.empty() && qualified != ident){
        // qualifyIdentifier expanded a value emitter — emit as raw I6
        i6RawNode& node = *(new i6RawNode());
        node.text = qualified + ";";
        node.src = stmtLoc;
        for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
        pendingInjections.clear();
        if(body != nullptr) body->statements.push_back(&node);
        for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
        postInjections.clear();
        return true;
    }
    // Also check simple global/import value emitters (qualified == ident means no expansion)
    functionDef* veFunc = nullptr;
    for(typeDef* g : languageService.globals)
        if(auto* fd = dynamic_cast<functionDef*>(g))
            if(fd->name == ident && fd->isValueEmitter && fd->isEmitter){ veFunc = fd; break; }
    if(!veFunc)
        for(classDef* imp : usingImports)
            for(typeMember* m : imp->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == ident && fd->isValueEmitter && fd->isEmitter){ veFunc = fd; break; }
    if(!veFunc)
        for(objectDef* imp : usingObjectImports)
            for(typeMember* m : imp->members)
                if(auto* fd = dynamic_cast<functionDef*>(m))
                    if(fd->name == ident && fd->isValueEmitter && fd->isEmitter){ veFunc = fd; break; }
    if(veFunc){
        if(auto* blk = dynamic_cast<i6Block*>(veFunc->body)){
            string bodyText = expandEmitterBody(blk, {});
            size_t s = bodyText.find_first_not_of(" \t\n\r"); if(s != string::npos) bodyText = bodyText.substr(s);
            size_t e = bodyText.find_last_not_of(" \t\n\r;"); if(e != string::npos) bodyText = bodyText.substr(0, e+1);
            i6RawNode& node = *(new i6RawNode());
            node.text = bodyText + ";";
            node.src = stmtLoc;
            for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
            pendingInjections.clear();
            if(body != nullptr) body->statements.push_back(&node);
            for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
            postInjections.clear();
        }
        return true;
    }
    return false;
}

// `a[i].member = v;` / `a[i].method(...);` — member access on the subscript result.
bool bglParser::processSubscriptMemberAccess(const string& arrPath, expression* indexExpr, StatementContext& sc){
    const sourceLocation& stmtLoc = sc.src;
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    file.getToken(); // consume '.'
    // Build subscript-read I6 text (same emitter expansion as expression-level path)
    string arrType = resolvePathType(arrPath, func, body);
    classDef* arrCls = getDispatchClass(arrType);   // `array<array<int>>` (a parameter) is the array class
    string elemType;
    size_t dotPos = arrPath.find('.');
    if(dotPos == string::npos) elemType = resolveArrayElementType(arrPath, func, body);
    else elemType = resolveArrayElementTypeDotted(arrPath.substr(0, dotPos), arrPath.substr(dotPos + 1), func, body);
    if(elemType.empty() && arrType == "bytearray") elemType = "char";
    if(elemType.empty() && arrCls != nullptr) elemType = inferSubscriptElementType(arrCls);
    functionDef* getMethod = nullptr;
    if(arrCls != nullptr && !elemType.empty())
        getMethod = findArraySubscriptOp(arrCls, elemType, /*isWrite=*/false);
    if(getMethod == nullptr)
        parsingError(format("Subscript on '{0}': cannot read element for dot-access", arrPath));
    string subscriptText;
    if(getMethod->isEmitter)
        if(auto* blk = dynamic_cast<i6Block*>(getMethod->body)){
            string pv = (isWordArrayType(arrType) || arrType == "bytearray") ? "0" : "<$prop undefined>";
            string selfValue = arrPath;
            size_t innerDot = arrPath.rfind('.');
            if(innerDot != string::npos){ selfValue = arrPath.substr(0, innerDot); pv = arrPath.substr(innerDot + 1); }
            emitterBindings rb; rb.self = selfValue; rb.val = arrPath; rb.prop = pv;
            rb.fn = getMethod; rb.args.push_back(indexExpr->text());
            subscriptText = expandEmitterBody(blk, rb);
        }
    // A routine-bodied operator [] is a method: send it, as the expression form does.
    if(subscriptText.empty() && !getMethod->isEmitter){
        string routine = getMethod->i6name.empty() ? mangleOperatorName(getMethod->name) : getMethod->i6name;
        string recv = func != nullptr ? qualifyIdentifier(arrPath, func, body) : arrPath;
        subscriptText = (recv.empty() ? arrPath : recv) + "." + routine + "(" + indexExpr->text() + ")";
    }
    // A member word array is a property array, read `obj.&prop-->n`, as the expression form does.
    if(isWordArrayType(arrType)){
        string memOwner, memProp;
        bool isMember = splitQualifiedMember(arrPath, func, body, memOwner, memProp);
        if(!isMember && dotPos != string::npos){
            memOwner = arrPath.substr(0, arrPath.rfind('.')); memProp = arrPath.substr(arrPath.rfind('.') + 1); isMember = true;
            if(func != nullptr)
                if(string q = qualifyIdentifier(memOwner, func, body); !q.empty()) memOwner = q;
        }
        if(isMember && !memberArrayIsRef(memOwner, memProp, func, body))
            subscriptText = checkedMemberSlot(memOwner, propertyI6Name(memProp), indexExpr->text(), false,
                                              memberArrayIsTracked(memOwner, memProp, func, body));
        else if(isMember && getMethod->isEmitter)   // the pointer the member holds
            subscriptText = chainedSubscriptRead(getMethod, objectI6Name(memOwner) + "." + propertyI6Name(memProp), indexExpr->text(), arrType);
    }
    subscriptText = valueSlotText(subscriptText, elemType);
    // Read member name and dispatch
    token memberTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
    string memberName = memberTok.value;
    // A write (`arr[i].n = v`, `+=`, `++`, `arr[i].items[j] = v`, …) goes through the element bound
    // to a local, so it gets every form a named receiver does.
    if(!file.peekToken().is(token::parenOpen) && !subscriptText.empty())
        return dispatchWriteThroughReceiver(subscriptText, elemType, memberTok, sc);
    token afterMember = file.getToken();
    if(afterMember.is(token::parenOpen)){
        // Method call: arr[i].method(args)
        classDef* elemCls = languageService.classOf(elemType);
        ParsedArgList pal = parseCallArgList(func, body, braceArgHints(collectMethodCandidates(elemType, memberName)));
        vector<string> namedArgNames = pal.namedArgNames;
        vector<vector<interpolatedSegment>> interpSegs = pal.interpSegmentsPerArg;
        functionDef* method = bindMethodCall(elemType, subscriptText, memberName,
            pal.args, namedArgNames, interpSegs);
        functionCallStatement& callStmt = *(new functionCallStatement());
        callStmt.src = stmtLoc;
        callStmt.functionName = parenthesizeReceiver(subscriptText) + "." + methodCallName(method, memberName);
        callStmt.args = pal.args;
        callStmt.namedArgNames = namedArgNames;
        callStmt.interpSegmentsPerArg = interpSegs;
        if(method->isEmitter && !method->isPrePassStub)
            if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                emitterBindings sb; sb.self = subscriptText; sb.val = subscriptText;
                sb.selfType = elemType;   // $i6Expr needs the receiver's type to parse its payload
                sb.prop = "0";            // an element is never a member array
                sb.elemType = elemType.rfind("array<", 0) == 0 ? elemType.substr(6, elemType.size() - 7) : elemType;
                if(elemCls != nullptr) sb.cls = elemCls->i6Name();
                sb.fn = method;
                // The parameters are substituted at emit time (an interpolated argument has no expression
                // here), as on any other receiver: only their types are bound now.
                for(expression* a : pal.args) sb.argTypes.push_back(a != nullptr ? a->resolvedType : "");
                callStmt.emitterBody = expandEmitterBody(blk, sb);
                for(paramDef* p : method->params) callStmt.emitterParams.push_back(p->name);
            }
        file.getToken(token::endStatement);
        if(body != nullptr) body->statements.push_back(&callStmt);
        for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);   // copied-back `ref` arguments
        postInjections.clear();
        return false;
    } else if(afterMember.is(token::assignment)){
        // Property assignment: arr[i].prop = value
        // parseExpression with endStatement terminator consumes through ';'
        expression* valExpr = parseExpression(file.getToken(), {token::endStatement}, func, body);
        assignmentStatement& assign = *(new assignmentStatement());
        assign.src = stmtLoc;
        assign.variableLeft = subscriptText + "." + memberName;
        assign.assignedExpression = valExpr;
        if(body != nullptr) body->statements.push_back(&assign);
        return false;
    } else {
        parsingError(format("Expected '(' or '=' after '{0}[...].{1}', got '{2}'",
            arrPath, memberName, afterMember.value));
    }
    return false;
}

// `grid[i][j]...[k] = v;` — chained subscript write into an array of arrays.
bool bglParser::processChainedSubscriptWrite(const string& arrPathIn, expression* indexExpr, StatementContext& sc){
    const sourceLocation& stmtLoc = sc.src;
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    // A bare member (`rows[r][c] = v` in a method) is self's.
    string arrPath = arrPathIn;
    bool refMember = false, isMember = false;
    string memberOwner, memberProp;
    if(splitQualifiedMember(arrPath, func, body, memberOwner, memberProp)){
        isMember = true;
        refMember = memberArrayIsRef(memberOwner, memberProp, func, body);
        if(arrPath.find('.') == string::npos) arrPath = memberOwner + "." + propertyI6Name(memberProp);
    }
    // A bare member is typed by its Beguile name; the qualified path may carry its I6 name (`_m_grid`).
    bool bare = arrPathIn.find('.') == string::npos;
    string arrType = bare ? resolveIdentifierType(arrPathIn, func, body) : resolvePathType(arrPath, func, body);
    size_t dotPos  = arrPath.find('.');
    string elemType = bare
        ? resolveArrayElementType(arrPathIn, func, body)
        : resolveArrayElementTypeDotted(arrPath.substr(0, dotPos), arrPath.substr(dotPos + 1), func, body);
    if(elemType.empty() && arrType == "bytearray") elemType = "char";
    classDef* arrCls = getDispatchClass(arrType);
    // `rack[0][1] = v;` on a class's own operator []: the first element is read into a local, and the
    // statement goes on from it.
    if(func != nullptr && arrCls != nullptr && !isWordArrayType(arrType) && arrType != "bytearray")
        if(auto* gm = dynamic_cast<functionDef*>(findMemberInHierarchy(arrCls, [](typeMember* m){
               auto* f = dynamic_cast<functionDef*>(m); return f && f->name == "[]" && f->params.size() == 1; }))){
            string recvText = bare ? qualifyIdentifier(arrPathIn, func, body) : arrPath;
            if(recvText.empty()) recvText = arrPath;
            static int counter = 0;
            string tmp = format("_bglcs{0}", counter++);
            variableDeclaration& od = *(new variableDeclaration());
            od.name = tmp; od.type = languageService.getType(gm->returnType.name); od.isSynthetic = true;
            i6RawNode& load = *(new i6RawNode());
            load.text = tmp + " = " + chainedSubscriptRead(gm, recvText, indexExpr->text(), arrType) + ";";
            load.cooked = true;
            load.src = stmtLoc;
            if(body != nullptr){ body->statements.push_back(&od); body->statements.push_back(&load); }
            string rest = file.getRawTextToStatementEnd(0);
            file.openText(tmp + rest + ";", stmtLoc.file, stmtLoc.line);
            try { processStatementDispatch(file.getToken(), *func); } catch(...) { file.close(); throw; }
            file.close();
            return false;
        }
    functionDef* getM = (arrCls != nullptr && !elemType.empty())
                      ? findArraySubscriptOp(arrCls, elemType, /*isWrite=*/false) : nullptr;
    i6Block* gblk = getM != nullptr ? dynamic_cast<i6Block*>(getM->body) : nullptr;
    if(gblk == nullptr)
        parsingError(format("Chained subscript on '{0}': element type '{1}' has no readable operator[].",
                            arrPath, typeDisplayName(elemType)));
    // First read step: name-based $self/$prop (handles member-array `obj.prop` too).
    string readText;
    {
        size_t innerDot = arrPath.rfind('.');
        string selfV = (innerDot == string::npos) ? arrPath : arrPath.substr(0, innerDot);
        string pv    = (innerDot != string::npos) ? arrPath.substr(innerDot + 1)
                     : (isWordArrayType(arrType) || arrType == "bytearray" ? "0" : "<$prop undefined>");
        if(refMember){ selfV = arrPath; pv = "0"; }   // a ref member holds a pointer to the array
        emitterBindings gb; gb.self = selfV; gb.val = arrPath; gb.prop = pv;
        gb.fn = getM; gb.args.push_back(indexExpr->text());
        readText = expandEmitterBody(gblk, gb);
        // A member word array is inline property data, read as the subscript read path does.
        if(isMember && !refMember && isWordArrayType(arrType))
            readText = checkedMemberSlot(memberOwner, propertyI6Name(memberProp), indexExpr->text(), false,
                                         memberArrayIsTracked(memberOwner, memberProp, func, body));
    }
    string curType = elemType;   // type of grid[0] — an array<...>
    while(true){
        file.getToken(); // consume '['
        expression* idx = parseExpression(file.getToken(), {token::bracketClose}, func, body);
        string innerElem = curType == "bytearray" ? "char" : arrayInnerType(curType);
        if(innerElem == "char") curType = "bytearray";   // an array<char> row is a byte array
        classDef* curCls = getDispatchClass(curType);
        if(innerElem.empty() && curCls != nullptr)   // a class element with its own operator[] (stringObj)
            if(auto* g = dynamic_cast<functionDef*>(findMemberInHierarchy(curCls, [](typeMember* m){
                    auto* fd = dynamic_cast<functionDef*>(m);
                    return fd != nullptr && fd->name == "[]" && fd->params.size() == 1; })))
                innerElem = g->returnType.name;
        if(innerElem.empty() && curCls != nullptr)   // only `operator []=`: the type it stores
            innerElem = inferSubscriptElementType(curCls);
        token after = file.peekToken();
        if(after.is(token::bracketOpen)){
            // Intermediate read: extend the pointer expression.
            functionDef* gm = (curCls != nullptr && !innerElem.empty())
                            ? findArraySubscriptOp(curCls, innerElem, /*isWrite=*/false) : nullptr;
            readText = chainedSubscriptRead(gm, readText, idx->text(), curType);
            curType  = innerElem;
            continue;
        }
        if(after.is(token::period)){
            // Element member write/call: grid[i]..[j].member = v  or  .method(args). Fold this
            // final subscript into a READ (yielding the element value), then dispatch on it.
            functionDef* gm = (curCls != nullptr && !innerElem.empty())
                            ? findArraySubscriptOp(curCls, innerElem, /*isWrite=*/false) : nullptr;
            string recv = "(" + chainedSubscriptRead(gm, readText, idx->text(), curType) + ")";   // the element value
            file.getToken(); // consume '.'
            token memberTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
            string memberName = memberTok.value;
            token afterMember = file.getToken();
            // A write (`=`, `+=`, `++`, …): the element is held in a local of its own, and the rest of the
            // statement runs on it as on any variable, so the member's own operators apply.
            if(!afterMember.is(token::parenOpen) && !file.hasPendingToken && body != nullptr && func != nullptr
               && languageService.classOf(innerElem) != nullptr){
                static int elemHold = 0;
                string tmp = format("_bglelh{0}", elemHold++);
                auto* vd = new variableDeclaration();
                vd->name = vd->displayName = tmp;
                vd->type = languageService.getType(innerElem);
                vd->src = stmtLoc;
                vd->isSynthetic = true;
                body->statements.push_back(vd);
                i6RawNode* hold = new i6RawNode();
                hold->text = tmp + " = " + recv + ";";
                hold->cooked = true;
                body->statements.push_back(hold);
                string memberText = tmp + "." + (memberTok.originalValue.empty() ? memberTok.value : memberTok.originalValue);
                string text;
                if(pendingPrefixOp && afterMember.is(token::endStatement)){   // `++grid[r][c].n;`
                    text = pendingPrefixOp->value + memberText + ";";
                    pendingPrefixOp.reset();
                } else text = memberText + " " + afterMember.value + " " + file.getRawTextToStatementEnd() + ";";
                file.openText(text, stmtLoc.file, stmtLoc.line);
                try { processStatementDispatch(file.getToken(), *func); } catch(...) { file.close(); throw; }
                file.close();
                return false;
            }
            if(afterMember.is(token::assignment)){
                expression* valExpr = parseExpression(file.getToken(), {token::endStatement}, func, body);
                assignmentStatement& assign = *(new assignmentStatement());
                assign.src = stmtLoc;
                assign.variableLeft = recv + "." + memberName;
                assign.assignedExpression = valExpr;
                if(body != nullptr) body->statements.push_back(&assign);
                return false;
            } else if(afterMember.is(token::parenOpen)){
                ParsedArgList pal = parseCallArgList(func, body, braceArgHints(collectMethodCandidates(innerElem, memberName)));
                functionDef* method = bindMethodCall(innerElem, recv, memberName,
                    pal.args, pal.namedArgNames, pal.interpSegmentsPerArg);
                functionCallStatement& cs = *(new functionCallStatement());
                cs.src = stmtLoc;
                cs.functionName = recv + "." + methodCallName(method, memberName);
                cs.args = pal.args; cs.namedArgNames = pal.namedArgNames; cs.interpSegmentsPerArg = pal.interpSegmentsPerArg;
                if(method->isEmitter && !method->isPrePassStub)
                    if(auto* mblk = dynamic_cast<i6Block*>(method->body)){
                        emitterBindings eb3; eb3.self = recv; eb3.val = recv;
                        eb3.fn = method;
                        for(expression* a : pal.args) eb3.args.push_back(a->text());
                        for(expression* a : pal.args) eb3.argTypes.push_back(a->resolvedType);
                        cs.emitterBody = expandEmitterBody(mblk, eb3);
                    }
                file.getToken(token::endStatement);
                if(body != nullptr) body->statements.push_back(&cs);
                return false;
            } else {
                parsingError(format("Expected '=' or '(' after '{0}[...].{1}', got '{2}'",
                                    arrPath, memberName, afterMember.value));
            }
        }
        // Final subscript — the write target.
        file.getToken(token::assignment);
        expression* valExpr = parseExpression(file.getToken(), {token::endStatement}, func, body);
        if(valExpr != nullptr && !valExpr->resolvedType.empty()
           && !zeroFitsBnum(valExpr, innerElem) && !isArrayElementCompatible(valExpr->resolvedType, innerElem))
            parsingError(format("Cannot assign value of type '{0}' to element of array<{1}>",
                                typeDisplayName(valExpr->resolvedType), typeDisplayName(innerElem)));
        checkByteElementRange(valExpr, innerElem);
        functionDef* setM = (curCls != nullptr && !innerElem.empty())
                          ? findArraySubscriptOp(curCls, innerElem, /*isWrite=*/true,
                                                 valExpr ? valExpr->resolvedType : "") : nullptr;
        // An element type that owns storage is stored through its operator =, as a single subscript is.
        if(isWordArrayType(curType) && !operatorRef(innerElem, "=").empty())
            if(auto* ac = languageService.findClass("array"))
                if(auto* owned = dynamic_cast<functionDef*>(findMemberInHierarchy(ac, [](typeMember* m){
                        auto* fn = dynamic_cast<functionDef*>(m); return fn && fn->name == "setownedat" && fn->isEmitter; })))
                    setM = owned;
        i6Block* sblk = setM != nullptr ? dynamic_cast<i6Block*>(setM->body) : nullptr;
        if(setM == nullptr || (setM->isEmitter && sblk == nullptr))
            parsingError(format("No operator[]= for element type '{0}' on type '{1}'.",
                                typeDisplayName(innerElem), typeDisplayName(curType)));
        string recv = "(" + readText + ")";
        functionCallStatement& cs = *(new functionCallStatement());
        cs.src = stmtLoc;
        if(!setM->isEmitter){   // a routine-bodied operator []= is a method
            cs.functionName = recv + "." + (setM->i6name.empty() ? mangleOperatorName(setM->name) : setM->i6name);
            cs.args = { idx, valExpr };
        } else {
            emitterBindings sb; sb.self = recv; sb.val = recv; sb.prop = "0";
            sb.fn = setM; sb.args = { idx->text(), valExpr->text() };
            sb.elemType = innerElem;
            cs.emitterBody = expandEmitterBody(sblk, sb);
        }
        if(body != nullptr) body->statements.push_back(&cs);
        return false;
    }
    return false;
}

// One step of a chained subscript read on `readText`: an emitter operator [] expands, a routine one is sent.
string bglParser::chainedSubscriptRead(functionDef* gm, const string& readText, const string& idxText, const string& curType){
    i6Block* blk = gm != nullptr ? dynamic_cast<i6Block*>(gm->body) : nullptr;
    if(gm == nullptr || (gm->isEmitter && blk == nullptr))
        parsingError(format("Chained subscript: '{0}' has no readable operator[].", typeDisplayName(curType)));
    string recv = "(" + readText + ")";
    if(!gm->isEmitter)
        return recv + "." + (gm->i6name.empty() ? mangleOperatorName(gm->name) : gm->i6name) + "(" + idxText + ")";
    emitterBindings eb; eb.self = recv; eb.val = recv; eb.prop = "0";
    eb.fn = gm; eb.args.push_back(idxText);
    return expandEmitterBody(blk, eb);
}

// `ref array<T> r := owner.arr;`: a member array is property data on its owner, with no address of
// its own for a reference to hold (as with passing one as an argument).
void bglParser::rejectMemberArrayBind(expression* rhs, functionDef* func, statementBlock* body){
    if(rhs == nullptr || !isWordArrayType(rhs->resolvedType)) return;
    string text = rhs->text();
    if(text.find('(') != string::npos || text.find('.') == string::npos) return;
    string recv = text;
    if(arrayReceiver(recv, text, rhs->resolvedType, func, body).isMember)
        parsingError(format("'{0}' is a member array, which a `ref` can't be bound to: bind a global, local or "
            "`ref` array, or declare the member `ref` too.", text));
}

// `a[i] = v;` — a single-subscript element write.
bool bglParser::processSubscriptWrite(string arrPath, expression* indexExpr, StatementContext& sc){
    const sourceLocation& stmtLoc = sc.src;
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    rejectConstantIndexOutside(arrPath, indexExpr, func, body);
    file.getToken(token::assignment);
    expression* valExpr = parseExpression(file.getToken(), {token::endStatement}, func, body);
    if(auto* arr = dynamic_cast<arrayDeclaration*>(findAssignedDeclaration(arrPath, func, body)))
        if(arr->literalElements) checkLiteralElements(*arr, {valExpr});
    if(arrPath.find('.') != string::npos || languageService.findGlobalAs<arrayDeclaration>(arrPath) != nullptr)
        rejectEscapingLambda(valExpr, format("stored in '{0}'", arrPath));
    else rejectLoopStoredLambda(valExpr, arrPath);

    // Resolve array type and compute $self/$prop
    string arrType = resolvePathType(arrPath, func, body);
    // Use getDispatchClass so templated receiver types (e.g. `array<var>` on a
    // parametric param) strip down to the generic class for operator[]= lookup.
    classDef* arrCls = getDispatchClass(arrType);
    if(arrCls == nullptr) parsingError(format("Type '{0}' does not support subscript access", arrType));

    // Element-type-aware lookup: find operator[]= whose second parameter type matches the
    // array's declared element type. Handles both bare (`name[i]=v`) and dotted
    // (`obj.prop[i]=v`) paths by splitting arrPath on '.'.
    string elemType;
    size_t dotPos = arrPath.find('.');
    if(dotPos == string::npos)
        elemType = resolveArrayElementType(arrPath, func, body);
    else
        elemType = resolveArrayElementTypeDotted(arrPath.substr(0, dotPos), arrPath.substr(dotPos + 1), func, body);
    if(elemType.empty() && arrType == "bytearray") elemType = "char";
    // Non-array classes (e.g. string) derive their element type from operator[]'s return.
    if(elemType.empty() && arrCls != nullptr) elemType = inferSubscriptElementType(arrCls);

    string valType = valExpr ? valExpr->resolvedType : "";
    functionDef* setMethod = nullptr;
    // Overloaded `operator []=` (by the index's type, `box[zone.region] = 7`): the index chooses.
    if(!isWordArrayType(arrType) && arrType != "bytearray" && indexExpr != nullptr){
        vector<functionDef*> sets;
        auto collect = [&](typeMember* m){
            if(auto* f = dynamic_cast<functionDef*>(m); f && f->name == "[]=" && f->params.size() == 2) sets.push_back(f);
            return false;
        };
        if(objectDef* od = languageService.findObjectType(arrType)) for(typeMember* m : od->members) collect(m);
        if(arrCls != nullptr) findMemberInHierarchy(arrCls, collect);
        bool indexTypesDiffer = false;
        for(functionDef* f : sets) if(f->params[0]->type.name != sets[0]->params[0]->type.name) indexTypesDiffer = true;
        if(indexTypesDiffer){
            const string it = indexExpr->resolvedType, base = literalBaseType(it);
            for(int pass = 0; pass < 2 && setMethod == nullptr; pass++)
                for(functionDef* f : sets){
                    const string& pt = f->params[0]->type.name;
                    bool fits = pass == 0 ? (pt == it || pt == base) : isTypeCompatible(it, pt);
                    if(fits && (valType.empty() || isArrayElementCompatible(valType, f->params[1]->type.name))){ setMethod = f; break; }
                }
            if(setMethod != nullptr){
                elemType = setMethod->params[1]->type.name;
                mangleOverloadSetForReceiver(arrType, "[]=");
            }
        }
    }
    if(setMethod == nullptr && !elemType.empty())
        setMethod = findArraySubscriptOp(arrCls, elemType, /*isWrite=*/true, valType);
    // An object declared with its own `operator []=` (or `[]` read, for the element type).
    if(setMethod == nullptr)
        if(objectDef* od = languageService.findObjectType(arrType))
            for(typeMember* m : od->members)
                if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->name == "[]=" && fd->params.size() == 2){
                    setMethod = fd; elemType = fd->params[1]->type.name; break;
                }
    // A class's `operator []=` may take another type than its `[]` returns: the one the value fits.
    if(setMethod == nullptr && !valType.empty() && !isWordArrayType(arrType) && arrType != "bytearray")
        if(auto* fd = dynamic_cast<functionDef*>(findMemberInHierarchy(arrCls, [&](typeMember* m){
                auto* f = dynamic_cast<functionDef*>(m);
                return f && f->name == "[]=" && f->params.size() == 2 && isArrayElementCompatible(valType, f->params[1]->type.name); }))){
            setMethod = fd;
            elemType = fd->params[1]->type.name;
        }
    if(setMethod == nullptr){
        if(elemType.empty())
            parsingError(format("Subscript on '{0}': no declared element type. Declare as array<T>.", arrPath));
        parsingError(format("No operator[]= for element type '{0}' on type '{1}'. Add an overload or use a supported element type.",
            typeDisplayName(elemType), typeDisplayName(arrType)));
    }
    // A byte slot takes an integer: a bnum widens to one, and a value with an implicit int conversion converts.
    if(elemType == "char" && !valType.empty() && !isArrayElementCompatible(valType, elemType)){
        if(auto* en = languageService.findEnum(valType); en != nullptr && en->isBnum) valType = "int";
        else if(classDef* vc = getDispatchClass(valType); vc != nullptr && findMemberInHierarchy(vc, [](typeMember* m){
                    auto* f = dynamic_cast<functionDef*>(m);
                    return f && f->name == "operator()" && f->params.empty() && !f->isExplicit && f->returnType.name == "int"; })){
            valExpr->tokens.assign(1, applyCastConversion(valExpr->text(), valType, "int"));
            valExpr->resolvedType = valType = "int";
        }
    }
    // A slot owns its instance: with no copy operator, a store would share the value's instance (§12.10).
    if(classDef* ec = languageService.findClass(elemType); ec != nullptr && isValueClass(ec) && !ec->isEmitterClass
       && !ec->isPrimitive && classHasStoredFields(ec)
       && findMemberInHierarchy(ec, [&](typeMember* m){
              auto* f = dynamic_cast<functionDef*>(m);
              if(f == nullptr || f->name != "=") return false;
              classDef* decl = ec->declaringClassOf(f);
              return decl != nullptr && decl->name.rfind("_bgl", 0) != 0; }) == nullptr)
        parsingError(format("an element of an array<{0}> owns its instance, and {0} has no operator = to copy one "
                            "in; declare `{0} operator = ({0} o)`.", ec->dName()));
    // A value of another type stored into a value-class slot goes through the class's `operator =` for
    // that type, on the slot's own instance (drawn from the pool if the slot is empty).
    if(classDef* vcls = owningValueElementClass(elemType); vcls != nullptr && !valType.empty()
       && arrPath.find('.') == string::npos && valType != elemType
       && [&]{ classDef* vc = getDispatchClass(valType); return vc == nullptr || (vc != vcls && !vc->hasAncestor(vcls)); }())
        if(auto* op = dynamic_cast<functionDef*>(findMemberInHierarchy(vcls, [&](typeMember* m){
                auto* f = dynamic_cast<functionDef*>(m);
                if(!(f && f->name == "=" && f->params.size() == 1 && f->params[0]->type.name != elemType
                     && isTypeCompatible(valType, f->params[0]->type.name))) return false;
                classDef* decl = vcls->declaringClassOf(f);   // the class's own, not the roots' generic store
                return decl != nullptr && decl->name.rfind("_bgl", 0) != 0; }))){
            functionDef* getM = findArraySubscriptOp(arrCls, elemType, /*isWrite=*/false);
            string read;
            if(getM != nullptr && getM->isEmitter)
                if(auto* gb = dynamic_cast<i6Block*>(getM->body)){
                    emitterBindings rb; rb.self = arrPath; rb.val = arrPath; rb.prop = "0";
                    rb.fn = getM; rb.args.push_back(indexExpr->text());
                    read = expandEmitterBody(gb, rb);
                }
            if(!read.empty()){
                string slot = valueSlotText(read, elemType);
                i6RawNode& node = *(new i6RawNode());
                if(op->isEmitter){
                    emitterBindings ob; ob.self = slot; ob.val = slot; ob.fn = op;
                    ob.args.push_back(valExpr->text()); ob.argTypes.push_back(valType);
                    string b = expandEmitterBody(dynamic_cast<i6Block*>(op->body), ob);
                    while(!b.empty() && (b.back() == ';' || isspace((unsigned char)b.back()))) b.pop_back();
                    node.text = b + ";";
                } else {
                    mangleOverloadSetForReceiver(vcls->name, op->name);
                    if(op->i6name.empty()) op->i6name = mangleOperatorName(op->name);
                    node.text = parenthesizeReceiver(slot) + "." + op->i6name + "(" + valExpr->text() + ");";
                }
                node.cooked = true;
                node.src = stmtLoc;
                for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
                pendingInjections.clear();
                if(body != nullptr) body->statements.push_back(&node);
                return false;
            }
        }
    // Validate value type against element type
    if(!valType.empty() && !zeroFitsBnum(valExpr, elemType) && !isArrayElementCompatible(valType, elemType))
        parsingError(format("Cannot assign value of type '{0}' to element of array<{1}>",
            typeDisplayName(valType), typeDisplayName(elemType)));
    checkByteElementRange(valExpr, elemType);

    // Compute $self and $prop
    size_t innerDot = arrPath.rfind('.');
    string selfValue = (innerDot == string::npos) ? arrPath : arrPath.substr(0, innerDot);
    string propValue = (innerDot == string::npos)
        ? ((isWordArrayType(arrType) || arrType == "bytearray") ? "0" : "<$prop undefined>")
        : arrPath.substr(innerDot + 1);

    // Member (property) WORD array write uses the orLibrary property convention
    // obj.&prop-->n = v (0-indexed, no count slot), not the global/table form. A dotted
    // path is already a property access; a bare name resolving to a member qualifies to one.
    string memOwner, memProp;
    bool isMemberWordArray = false;
    string pointerSlot;   // a member held through a pointer: its element's address, from the array's own `operator []`
    if(isWordArrayType(arrType)){
        if(innerDot != string::npos){
            memOwner = selfValue; memProp = propValue; isMemberWordArray = true;
            if(func != nullptr)   // the owner as this routine reaches it (a capture slot in a lambda)
                if(string q = qualifyIdentifier(selfValue, func, body); !q.empty()) memOwner = q;
        }
        else isMemberWordArray = splitQualifiedMember(arrPath, func, body, memOwner, memProp);
    if(isMemberWordArray && memberArrayIsRef(memOwner, memProp, func, body)){
        arrPath = objectI6Name(memOwner) + "." + propertyI6Name(memProp);   // the pointer the member holds
        selfValue = arrPath; propValue = "0";
        isMemberWordArray = false;
        if(classDef* ac = !operatorRef(elemType, "=").empty() ? getDispatchClass(arrType) : nullptr)
            if(functionDef* gm = findArraySubscriptOp(ac, elemType, /*isWrite=*/false))
                pointerSlot = chainedSubscriptRead(gm, arrPath, indexExpr->text(), arrType);
    }
    }

    functionCallStatement& callStmt = *(new functionCallStatement());
    callStmt.src = stmtLoc;
    callStmt.functionName = arrPath + ".set";
    callStmt.args.push_back(indexExpr);
    callStmt.args.push_back(valExpr);

    // A slot whose element type owns storage accepts a bare text pointer — no explicit
    // _bglStr.new() needed: a type that owns storage publishes `static operator =`, which
    // setOwned hands the slot's CURRENT value plus the incoming one, so the type allocates
    // on first write and copies into its own buffer thereafter. A type that owns storage but
    // publishes no assign still falls through to a raw word store — see the array<T>
    // requirements in the spec.

    // If the element type publishes a static `operator =` it OWNS its storage, so the slot
    // write must go through the type rather than being a raw word store. Prefer array<T>'s
    // `setOwnedAt` member in that case; every plain element type keeps core's inline
    // `$val-->($i+1) = $v` and pays nothing. Both bodies live in the BLR — this only picks.
    // operatorRef returns "" when the type publishes no assign — the "0" substitution
    // happens later, in substituteElemOps. Comparing against "0" here made this true for
    // EVERY element type, so every subscript write took the setOwnedAt routine instead of
    // the inline store, and a non-array receiver (stringObj) got "<$prop undefined>".
    // An array stored into another array's element has left the routine that made it.
    if(isWordArrayType(elemType) && elemType != "array<char>")
        valExpr->tokens.assign(1, "_bglHeapEscape(" + valExpr->text() + ")");
    if(!isMemberWordArray && pointerSlot.empty() && !operatorRef(elemType, "=").empty()){
        functionDef* owned = nullptr;
        if(auto* ac = languageService.findClass("array"))
            owned = dynamic_cast<functionDef*>(findMemberInHierarchy(ac, [](typeMember* m){
                auto* fn = dynamic_cast<functionDef*>(m);
                return fn && fn->name == "setownedat" && fn->isEmitter;
            }));
        // Without <array> the write would be a raw word store, sharing the value's storage.
        if(owned == nullptr)
            parsingError(format("an element of an array<{0}> is copied in through {0}'s operator =, which needs "
                "#include <array>.", typeDisplayName(elemType)));
        setMethod = owned;
    }
    if(isMemberWordArray || !pointerSlot.empty()){
        string slot = !pointerSlot.empty() ? pointerSlot
                    : checkedMemberSlot(memOwner, propertyI6Name(memProp), indexExpr->text(), true,
                                        memberArrayIsTracked(memOwner, memProp, func, body));
        // An element type that owns storage stores through its assign routine, which copies into the
        // slot's own instance; `slot` is read twice, so an index with side effects can't share it.
        // A value class's element is its slot's own instance, given the value through the class's `operator =`
        // for the value's type; the slot is read once.
        functionDef* slotOp = nullptr;
        if(classDef* vc = languageService.findClass(elemType); vc != nullptr && isValueClass(vc) && !vc->isEmitterClass){
            slotOp = findAssignOperator(vc, valExpr->resolvedType, [](functionDef* f){ return !f->isEmitter; }, true);
        }
        if(slotOp != nullptr){
            classDef* vc = languageService.findClass(elemType);
            mangleOverloadSetForReceiver(vc->name, slotOp->name);
            if(slotOp->i6name.empty()) slotOp->i6name = mangleOperatorName(slotOp->name);
            convertToParam(valExpr, valExpr->resolvedType, slotOp->params[0]->type.name);
            callStmt.emitterBody = "(" + valueSlotText(slot, elemType) + ")." + slotOp->i6name + "(" + valExpr->text() + ")";
        } else {
        bool isStaticAssign = false;
        operatorRef(elemType, "=", elemType, &isStaticAssign);
        string assign = isStaticAssign || owningValueElementClass(elemType) != nullptr ? substituteElemOps("$opref(=)", elemType) : "0";
        if(assign != "0" && !regex_match(indexExpr->text(), regex("\\s*[A-Za-z0-9_.$]+\\s*")))
            parsingError(format("an element of an array<{0}> is copied in through {0}'s operator =; index this member "
                "array with a variable or constant.", typeDisplayName(elemType)));
        callStmt.emitterBody = assign != "0" ? slot + " = " + assign + "(" + slot + ", " + valExpr->text() + ")"
                                             : slot + " = " + valExpr->text();
        }
    }
    else if(setMethod->isEmitter)
        if(auto* blk = dynamic_cast<i6Block*>(setMethod->body)) {
            // The path's head as I6 names it (a verb object's name differs from its Beguile one).
            string valPath = arrPath, selfPath = selfValue;
            if(size_t hd = arrPath.find('.'); hd != string::npos){
                string head = objectI6Name(arrPath.substr(0, hd));
                valPath = head + arrPath.substr(hd);
                if(selfValue == arrPath.substr(0, hd)) selfPath = head;
            }
            emitterBindings ab; ab.self = selfPath; ab.val = valPath; ab.prop = propValue;
            ab.fn = setMethod;
            for(expression* a : callStmt.args) ab.args.push_back(a->text());
            for(expression* a : callStmt.args) ab.argTypes.push_back(a->resolvedType);
            ab.elemType = elemType;   // one substitution covers every $opref in the body
            callStmt.emitterBody = expandEmitterBody(blk, ab);
        }
    if(!isMemberWordArray && !setMethod->isEmitter)   // a routine-bodied operator []= is a method
        callStmt.functionName = objectI6Name(withMemberI6Name(arrPath, arrPath, func, body)) + "."   // a renamed member's I6 name
                              + (setMethod->i6name.empty() ? mangleOperatorName(setMethod->name) : setMethod->i6name);
    // The value's set-up (a lifted ternary, a call's kept result) runs before the store.
    for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
    pendingInjections.clear();
    if(body != nullptr) body->statements.push_back(&callStmt);
    for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
    postInjections.clear();
    return false;
}

string bglParser::bindReceiverLocal(const string& recvText, const string& recvType, StatementContext& sc){
    // One local per receiver type per routine: it lives only for the statement that sets it, so
    // later writes through the same type reuse it rather than spending another local slot.
    string name = "_bglrecv_";
    for(char c : recvType) name += isalnum((unsigned char)c) ? (char)tolower((unsigned char)c) : '_';
    if(sc.body != nullptr){
        for(statement* inj : pendingInjections) sc.body->statements.push_back(inj);
        pendingInjections.clear();
    }
    expression* e = new expression();
    e->tokens.push_back(recvText);
    e->resolvedType = recvType;
    if(qualifyFromBodyLocals(name, sc.body) || qualifyFromAncestorBlocks(name, sc.body)){
        assignmentStatement* a = new assignmentStatement();
        a->src = sc.src;
        a->variableLeft = name;
        a->assignedExpression = e;
        if(sc.body != nullptr) sc.body->statements.push_back(a);
        return name;
    }
    variableDeclaration* vd = new variableDeclaration();
    vd->name = name;
    vd->type = languageService.getType(recvType);
    if(vd->type.name.empty()) vd->type.name = recvType;
    vd->isRefLocal = true;   // it names the receiver; a value-class element must not be copied
    vd->src = sc.src;
    vd->declaredExpressionValue = e;
    if(sc.body != nullptr) sc.body->statements.push_back(vd);
    return name;
}

bool bglParser::dispatchWriteThroughReceiver(const string& recvText, const string& recvType,
                                             token memberTok, StatementContext& sc){
    token path = memberTok;
    path.value = bindReceiverLocal(recvText, recvType, sc) + "." + memberTok.value;
    path.originalValue.clear();
    path.tokenType = eTokenType::identifier;
    token symbol = file.getToken();
    while(symbol.is(token::period)){   // a longer path: `recv.a.b = v`
        path.value += "." + file.getToken({eTokenType::identifier, eTokenType::dataType}).value;
        symbol = file.getToken();
    }
    return dispatchPathStatement(path, symbol, sc);
}

// `a[i] op= v;`, `a[i]++;`, `++a[i];` (and the chained `a[i][j]` forms) are `a[i] = a[i] op (v)`, parsed
// as that assignment, so the element's own read and write apply. An index that isn't a plain name or
// number is evaluated once, into a local. Looks ahead raw from just after the `[`; anything else is
// left exactly where it was.
bool bglParser::rewriteElementCompound(token tok, StatementContext& sc){
    if(sc.func == nullptr || file.hasPendingToken) return false;
    fileLexer::mark_t at = file.mark();
    // The text after the `[`, up to the statement's `;` (which is consumed).
    string rest;
    {
        int depth = 1;
        bool found = false;
        char c = file.readChar();
        while(c != EOF){
            if(c == '"' || c == '\''){
                char q = c; rest += c; c = file.readChar();
                while(c != EOF && c != q){
                    if(c == '\\'){ rest += c; c = file.readChar(); if(c == EOF) break; }
                    rest += c; c = file.readChar();
                }
                if(c == EOF) break;
                rest += c; c = file.readChar();
                continue;
            }
            if(c == '/' && file.peekChar() == '/'){ while(c != EOF && c != '\n') c = file.readChar(); continue; }
            if(c == '/' && file.peekChar() == '*'){
                file.readChar(); c = file.readChar();
                while(c != EOF && !(c == '*' && file.peekChar() == '/')) c = file.readChar();
                if(c != EOF){ file.readChar(); c = file.readChar(); }
                rest += ' ';
                continue;
            }
            if(c == '[' || c == '(' || c == '{') depth++;
            else if(c == ']' || c == ')' || c == '}') depth--;
            else if(c == ';' && depth == 0){ found = true; break; }
            rest += c; c = file.readChar();
        }
        if(!found){ file.rewind(at); return false; }
    }
    vector<string> indexes;
    size_t i = 0;
    auto readIndex = [&]() -> bool {
        int depth = 1; size_t start = i;
        for(; i < rest.size(); i++){
            char c = rest[i];
            if(c == '"' || c == '\''){
                for(i++; i < rest.size() && rest[i] != c; i++) if(rest[i] == '\\') i++;
                continue;
            }
            if(c == '[' || c == '(' || c == '{') depth++;
            else if((c == ']' || c == ')' || c == '}') && --depth == 0) break;
        }
        if(i >= rest.size()) return false;
        indexes.push_back(rest.substr(start, i - start));
        i++;
        return true;
    };
    auto skipSpace = [&]{ while(i < rest.size() && isspace((unsigned char)rest[i])) i++; };
    bool ok = readIndex();
    for(skipSpace(); ok && i < rest.size() && rest[i] == '['; skipSpace()){ i++; ok = readIndex(); }
    string tail = ok ? rest.substr(i) : "";
    while(!tail.empty() && isspace((unsigned char)tail.back())) tail.pop_back();
    string op, rhs;
    if(ok && pendingPrefixOp && tail.empty()){ op = pendingPrefixOp->value.substr(0, 1); rhs = "1"; }
    else if(ok && !pendingPrefixOp && (tail == "++" || tail == "--")){ op = tail.substr(0, 1); rhs = "1"; }
    else if(ok && !pendingPrefixOp){
        static const regex compound(R"(^(\+|-|\*|/|%|\||&|\^|<<|>>)=([^=][\s\S]*)$)");
        smatch m;
        if(regex_match(tail, m, compound)){ op = m[1]; rhs = m[2]; }
    }
    // `a[i] = $"…"`: an interpolated string has no value, so it goes to the element's operator through a ref.
    bool interpolatedAssign = false;
    if(ok && op.empty() && !pendingPrefixOp && tail.size() > 1 && tail[0] == '=' && tail[1] != '='){
        string v = tail.substr(1);
        v.erase(0, v.find_first_not_of(" \t\r\n"));
        if(v.rfind("$\"", 0) == 0 || v.rfind("$@\"", 0) == 0){ op = "="; rhs = tail.substr(1); interpolatedAssign = true; }
    }
    if(op.empty()){ file.rewind(at); return false; }
    const bool incDec = pendingPrefixOp.has_value() || tail == "++" || tail == "--";
    pendingPrefixOp.reset();

    static int counter = 0;
    string decls, lhs = tok.value;
    for(const string& idx : indexes){
        string t = idx;
        t.erase(0, t.find_first_not_of(" \t\r\n"));
        t.erase(t.find_last_not_of(" \t\r\n") + 1);
        if(!t.empty() && t.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.") == string::npos)
            lhs += "[" + t + "]";
        else {
            string name = format("_bglidx{0}", counter++);
            decls += "int " + name + " = (" + t + "); ";
            lhs += "[" + name + "]";
        }
    }
    // An element that is itself an array takes the array's += / -= (append, remove a value), not arithmetic.
    string elemType = resolveArrayElementType(tok.value, sc.func, sc.body);
    for(size_t d = 1; d < indexes.size() && elemType.rfind("array<", 0) == 0; d++)
        elemType = elemType.substr(6, elemType.size() - 7);
    bool arrayElement = elemType.rfind("array<", 0) == 0 && tail.size() > 1 && tail[1] == '=';
    // A reference-class element whose class declares the operator as a method, overloads it, or is given
    // an interpolated string: through a local bound to the element, which dispatches as any named
    // receiver does.
    if(!arrayElement)
        if(classDef* ec = getDispatchClass(elemType); ec != nullptr && !ec->isPrimitive){
            const string want = incDec ? op + op : interpolatedAssign ? op : op + "=";
            string r = rhs;
            r.erase(0, r.find_first_not_of(" \t\r\n"));
            bool interpolated = r.rfind("$\"", 0) == 0 || r.rfind("$@\"", 0) == 0;
            int overloads = 0, methods = 0;
            findMemberInHierarchy(ec, [&](typeMember* m){
                auto* fd = dynamic_cast<functionDef*>(m);
                if(fd && fd->name == want && fd->params.size() == (incDec ? 0u : 1u)){ overloads++; if(!fd->isEmitter) methods++; }
                return false; });
            // A stringObj slot may be empty until its first store: the value is built in a stringObj of
            // its own, then stored, and the element's store allocates the slot.
            if(elemType == "stringobj" && !incDec && overloads > 0){
                string tmp = format("_bglel{0}", counter++);
                string v = interpolated ? r : "(" + rhs + ")";
                string text = decls + (interpolatedAssign
                    ? "{ stringObj " + tmp + " = " + v + "; " + lhs + " = " + tmp + "; }"
                    : "{ stringObj " + tmp + " = \"\"; if (" + lhs + "?) " + tmp + " = " + lhs + "; "
                      + tmp + " " + want + " " + v + "; " + lhs + " = " + tmp + "; }");
                size_t statements = count(decls.begin(), decls.end(), ';') + 1;
                file.openText(text, sc.src.file, sc.src.line);
                try { for(size_t n = 0; n < statements; n++) processStatementDispatch(file.getToken(), *sc.func); }
                catch(...) { file.close(); throw; }
                file.close();
                return true;
            }
            if(methods > 0 || (overloads > 0 && !incDec && (interpolated || overloads > 1))){
                if(interpolated) rhs = r;
                string tmp = format("_bglel{0}", counter++);
                // A value-class element is reached through a `ref`, so the operator works on the slot's instance.
                string bind = refNotApplicable(ec, elemType).empty() ? "ref " + elemType + " " + tmp + " := " : elemType + " " + tmp + " = ";
                string text = decls + "{ " + bind + lhs + "; "
                            + (incDec ? tmp + want : tmp + " " + want + (interpolated ? " " + rhs : " (" + rhs + ")")) + "; }";
                size_t statements = count(decls.begin(), decls.end(), ';') + 1;
                file.openText(text, sc.src.file, sc.src.line);
                try {
                    for(size_t n = 0; n < statements; n++) processStatementDispatch(file.getToken(), *sc.func);
                } catch(...) { file.close(); throw; }
                file.close();
                return true;
            }
        }
    // An element whose class declares the compound operator as an emitter takes it, with $self the element.
    functionDef* elemOp = nullptr;
    if(!arrayElement)
        if(classDef* ec = getDispatchClass(elemType))
            elemOp = dynamic_cast<functionDef*>(findMemberInHierarchy(ec, [&](typeMember* m){
                auto* fd = dynamic_cast<functionDef*>(m);
                auto* blk = fd != nullptr ? dynamic_cast<i6Block*>(fd->body) : nullptr;
                return blk != nullptr && fd->isEmitter && fd->name == op + "=" && fd->params.size() == 1
                    && blk->i6Body.find("$target") == string::npos; }));
    if(elemOp != nullptr){
        size_t declCount = count(decls.begin(), decls.end(), ';');
        auto parseText = [&](const string& t) -> expression* {
            file.openText(t + ";", sc.src.file, sc.src.line);
            expression* e = nullptr;
            try { e = parseExpression(file.getToken(), {token::endStatement}, sc.func, sc.body); }
            catch(...) { file.close(); throw; }
            file.close();
            return e;
        };
        if(declCount > 0){
            file.openText(decls, sc.src.file, sc.src.line);
            try { for(size_t n = 0; n < declCount; n++) processStatementDispatch(file.getToken(), *sc.func); }
            catch(...) { file.close(); throw; }
            file.close();
        }
        expression* elem = parseText(lhs);
        expression* val  = parseText(rhs);
        const string& pt = elemOp->params[0]->type.name;
        if(!val->resolvedType.empty() && val->resolvedType != pt && !isTypeCompatible(val->resolvedType, pt))
            parsingError(format("No operator '{0}=' on type '{1}' accepting '{2}'",
                                op, typeDisplayName(elemType), typeDisplayName(val->resolvedType)));
        emitterBindings eb; eb.self = elem->text(); eb.val = elem->text();
        eb.fn = elemOp; eb.args.push_back(val->text()); eb.argTypes.push_back(val->resolvedType);
        i6RawNode& node = *(new i6RawNode());
        string b = expandEmitterBody(dynamic_cast<i6Block*>(elemOp->body), eb);
        while(!b.empty() && (b.back() == ';' || isspace((unsigned char)b.back()))) b.pop_back();
        node.text = b + ";";
        node.cooked = true;
        node.src = sc.src;
        for(statement* inj : pendingInjections) if(sc.body != nullptr) sc.body->statements.push_back(inj);
        pendingInjections.clear();
        if(sc.body != nullptr) sc.body->statements.push_back(&node);
        return true;
    }
    string text = arrayElement && (op == "+" || op == "-")
                ? decls + lhs + (op == "+" ? ".append(" : ".removeValue(") + rhs + ");"
                : decls + lhs + " = " + lhs + " " + op + " (" + rhs + ");";
    size_t statements = count(decls.begin(), decls.end(), ';') + 1;
    file.openText(text, sc.src.file, sc.src.line);
    try {
        for(size_t n = 0; n < statements; n++) processStatementDispatch(file.getToken(), *sc.func);
    } catch(...) { file.close(); throw; }
    file.close();
    return true;
}

// A statement whose target is reached through something computed — a call, a subscript followed by a
// call, a parenthesized or cast receiver (`bank[i].peer().lvl = 9;`, `((Panel)bank[0]).lamps[3] = 1;`,
// `++((P)o).n;`): the receiver is bound to a local first, and the statement is written on that local, so
// the member is reached the way it is through any named receiver. `head` is the source already read.
bool bglParser::rewriteComputedReceiverStatement(const string& head, int depth, StatementContext& sc, const string& prefixOp){
    if(sc.func == nullptr || file.hasPendingToken) return false;
    fileLexer::mark_t at = file.mark();
    string text = head + file.getRawTextToStatementEnd(depth);
    auto fail = [&]{ file.rewind(at); return false; };
    // The top-level assignment, if any, and the last top-level `.` of what it assigns.
    int d = 0;
    size_t opAt = string::npos, opLen = 0;
    for(size_t i = 0; i < text.size(); i++){
        char c = text[i];
        if(c == '"' || c == '\''){ for(i++; i < text.size() && text[i] != c; i++) if(text[i] == '\\') i++; continue; }
        if(c == '(' || c == '[' || c == '{') d++;
        else if(c == ')' || c == ']' || c == '}') d--;
        if(d != 0) continue;
        if(c == '=' && (i + 1 >= text.size() || text[i + 1] != '=')){
            char p = i > 0 ? text[i - 1] : ' ';
            if(p == '=' || p == '!' || p == '<' || p == '>' || p == ':') {
                if(p == '<' && i >= 2 && text[i - 2] == '<'){ opAt = i - 2; opLen = 3; break; }
                if(p == '>' && i >= 2 && text[i - 2] == '>'){ opAt = i - 2; opLen = 3; break; }
                continue;
            }
            if(string("+-*/%|&^").find(p) != string::npos){ opAt = i - 1; opLen = 2; }
            else { opAt = i; opLen = 1; }
            break;
        }
    }
    string lhs = opAt == string::npos ? text : text.substr(0, opAt);
    string tail = opAt == string::npos ? "" : text.substr(opAt);
    while(!lhs.empty() && isspace((unsigned char)lhs.back())) lhs.pop_back();
    string post;
    if(opAt == string::npos && lhs.size() > 2 && (lhs.compare(lhs.size() - 2, 2, "++") == 0 || lhs.compare(lhs.size() - 2, 2, "--") == 0)){
        post = lhs.substr(lhs.size() - 2);
        lhs.pop_back(); lhs.pop_back();
        while(!lhs.empty() && isspace((unsigned char)lhs.back())) lhs.pop_back();
    }
    size_t dot = string::npos;
    d = 0;
    for(size_t i = 0; i < lhs.size(); i++){
        char c = lhs[i];
        if(c == '"' || c == '\''){ for(i++; i < lhs.size() && lhs[i] != c; i++) if(lhs[i] == '\\') i++; continue; }
        if(c == '(' || c == '[' || c == '{') d++;
        else if(c == ')' || c == ']' || c == '}') d--;
        else if(c == '.' && d == 0 && (i == 0 || lhs[i - 1] != '?')) dot = i;
    }
    bool writes = opAt != string::npos || !post.empty() || !prefixOp.empty();
    // `(x).a.b = v`, `(x)[0].n++`, `((T)o)[1]?.m()`: the parenthesized group is the receiver, whatever follows it.
    string joiner = ".";
    bool groupReceiver = false;
    if(head == "("){
        int dd = 0; size_t close = string::npos;
        for(size_t i = 0; i < lhs.size(); i++){
            char c = lhs[i];
            if(c == '"' || c == '\''){ for(i++; i < lhs.size() && lhs[i] != c; i++) if(lhs[i] == '\\') i++; continue; }
            if(c == '(') dd++;
            else if(c == ')' && --dd == 0){ close = i; break; }
        }
        size_t next = close == string::npos ? string::npos : lhs.find_first_not_of(" \t", close + 1);
        if(next != string::npos && (lhs[next] == '[' || lhs[next] == '.')){
            bool chained = lhs[next] == '[' || lhs.find("?.", next) != string::npos;
            if(writes || chained){
                groupReceiver = true;
                dot = next;
                joiner = lhs[next] == '[' ? "" : ".";
            }
        }
    }
    if(dot == string::npos || dot == 0) return fail();
    string recv = lhs.substr(0, dot), member = lhs.substr(dot + (joiner.empty() ? 0 : 1));
    // `((Tray)o).slots.length = 1`: an array's length is written through its owner, so the owner is the receiver.
    if(!groupReceiver && (member == "length" || member == "size")){
        size_t prev = string::npos;
        d = 0;
        for(size_t i = 0; i < recv.size(); i++){
            char c = recv[i];
            if(c == '(' || c == '[' || c == '{') d++;
            else if(c == ')' || c == ']' || c == '}') d--;
            else if(c == '.' && d == 0) prev = i;
        }
        if(prev != string::npos && prev > 0){ member = recv.substr(prev + 1) + "." + member; recv = recv.substr(0, prev); }
    }
    if(recv.find_first_of("([") == string::npos) return fail();   // a plain path: the ordinary statement handles it
    if(!writes && member.find('(') == string::npos) return fail();
    if(head == "("){
        // Only a receiver that is one parenthesized whole: `(deriv)a.barks = 9` casts the member, not `a`.
        int dd = 0; size_t close = string::npos;
        for(size_t i = 0; i < recv.size(); i++){
            if(recv[i] == '(') dd++;
            else if(recv[i] == ')' && --dd == 0){ close = i; break; }
        }
        if((!writes && !groupReceiver) || close != recv.size() - 1) return fail();
    }
    // The receiver's type, from a parse whose set-up is thrown away.
    string recvType;
    {
        vector<statement*> savedPending = pendingInjections, savedPost = postInjections;
        statementBlock scratch;
        file.openText(recv + ";", sc.src.file, sc.src.line);
        try { if(expression* e = parseExpression(file.getToken(), {token::endStatement}, sc.func, &scratch)) recvType = e->resolvedType; }
        catch(...) { file.close(); pendingInjections = savedPending; postInjections = savedPost; file.rewind(at); throw; }
        file.close();
        pendingInjections = savedPending; postInjections = savedPost;
    }
    // A receiver typed as an object is that object.
    if(objectDef* od = languageService.findObjectType(recvType); od != nullptr && languageService.findClass(recvType) == nullptr){
        string self = od->dName();
        string stmt = !prefixOp.empty() ? prefixOp + self + joiner + member
                    : self + joiner + member + (post.empty() ? " " + tail : post);
        static int seen = 0;
        file.openText("{ var _bglrcvo" + to_string(seen++) + " = " + recv + "; " + stmt + "; }", sc.src.file, sc.src.line);
        try { processStatementDispatch(file.getToken(), *sc.func); } catch(...) { file.close(); throw; }
        file.close();
        return true;
    }
    classDef* rc = getDispatchClass(recvType);
    if(recvType.empty() || rc == nullptr || rc->isPrimitive || isWordArrayType(recvType) || recvType == "bytearray") return fail();
    static int counter = 0;
    string tmp = format("_bglrcv{0}", counter++);
    string bind = isValueClass(rc) ? "ref " + recvType + " " + tmp + " := " : recvType + " " + tmp + " = ";
    string stmt = !prefixOp.empty() ? prefixOp + tmp + joiner + member
                : tmp + joiner + member + (post.empty() ? " " + tail : post);
    string out = "{ " + bind + recv + "; " + stmt + "; }";
    file.openText(out, sc.src.file, sc.src.line);
    try { processStatementDispatch(file.getToken(), *sc.func); } catch(...) { file.close(); throw; }
    file.close();
    return true;
}

// `name[i] …` — an element write, a chained subscript, or member access on the element.
bool bglParser::processSubscriptStatement(token tok, StatementContext& sc){
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    string arrPath = (string)tok;  // e.g. "scores" or "player.inventory"
    // A static array (`Calc.table`, or bare `table` in Calc's own code) is its global array.
    if(string g = staticMemberGlobal(arrPath); !g.empty()) arrPath = tok.value = g;
    else if(currentClass != nullptr && arrPath.find('.') == string::npos)
        if(auto* sm = dynamic_cast<arrayDeclaration*>(findMemberInHierarchy(currentClass, [&](typeMember* m){
                auto* vd = dynamic_cast<variableDeclaration*>(m); return vd && vd->isStatic && vd->name == arrPath; })))
            arrPath = tok.value = "_bgl_" + currentClass->declaringClass(sm)->name + "_" + sm->name;
    // A bare member that holds a pointer (array<char>, stringObj, a class with operator []) is self's.
    if(arrPath.find('.') == string::npos && arrPath.rfind("_bgl_", 0) != 0
       && !isWordArrayType(resolveIdentifierType(arrPath, func, body))){
        string owner, prop;
        if(splitQualifiedMember(arrPath, func, body, owner, prop) && owner == "self")
            arrPath = tok.value = "self." + arrPath;
    }
    if(rewriteElementCompound(tok, sc)) return false;
    // `bank[i].peer().lvl = 9;`: a call after the element makes the target computed.
    {
        int k = 1, depth = 1;
        for(token pk = file.peekToken(k); depth > 0 && !pk.is(eTokenType::eof) && !pk.is(token::endStatement); pk = file.peekToken(++k)){
            if(pk.is(token::bracketOpen)) depth++;
            else if(pk.is(token::bracketClose)) depth--;
        }
        // `rings[0][1].links[0].level += 2;`: a member's subscript after the element does too.
        bool callAfter = false, dotSeen = false;
        for(token pk = file.peekToken(k); !pk.is(eTokenType::eof) && !pk.is(token::endStatement) && !pk.is(token::assignment); pk = file.peekToken(++k)){
            if(pk.is(token::parenOpen) || (dotSeen && pk.is(token::bracketOpen))){ callAfter = true; break; }
            if(pk.is(token::period)) dotSeen = true;
        }
        if(callAfter && rewriteComputedReceiverStatement(tok.value + "[", 1, sc)) return false;
    }
    // `name[i](args);` calls the element and `name[i]?.m(…);` calls through it: one expression each.
    {
        int k = 1, depth = 1;
        for(token pk = file.peekToken(k); depth > 0 && !pk.is(eTokenType::eof) && !pk.is(token::endStatement); pk = file.peekToken(++k)){
            if(pk.is(token::bracketOpen)) depth++;
            else if(pk.is(token::bracketClose)) depth--;
            // `grid[1][0]?.m()`: a further subscript continues the receiver.
            if(depth == 0 && file.peekToken(k + 1).is(token::bracketOpen)){ depth = 1; k++; }
        }
        if(depth == 0 && (file.peekToken(k).is(token::parenOpen) || file.peekToken(k).is("?."))){
            string text = tok.value + "[" + file.getRawTextToStatementEnd(1) + ";";
            file.openText(text, sc.src.file, sc.src.line);
            bool savedAllowVoid = allowVoidReturnExpr;
            allowVoidReturnExpr = true;
            expression* call = nullptr;
            try { call = parseExpression(file.getToken(), {token::endStatement}, func, body); }
            catch(...) { allowVoidReturnExpr = savedAllowVoid; file.close(); throw; }
            allowVoidReturnExpr = savedAllowVoid;
            file.close();
            for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
            pendingInjections.clear();
            // A `?.` call ran in its set-up; what is left is the temporary that held its value.
            string callText = call->text();
            if(callText.rfind("_bgl_temp", 0) != 0 || callText.find_first_of("(.") != string::npos){
                i6RawNode& node = *(new i6RawNode());
                node.text = callText + ";";
                node.src = sc.src;
                if(body != nullptr) body->statements.push_back(&node);
            }
            for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
            postInjections.clear();
            return false;
        }
    }
    expression* indexExpr = parseExpression(file.getToken(), {token::bracketClose}, func, body);

    // Peek after ']': if '.', this is a dot-chain on the subscript result (e.g. arr[0].method()).
    // Build the subscript-read text, then dispatch the continuation as a method call or property access.
    token afterBracket = file.peekToken();
    if(afterBracket.is(token::period))
        return processSubscriptMemberAccess(arrPath, indexExpr, sc);

    // Chained subscript write into an array-of-arrays: grid[i][j]...[k] = v. Every subscript
    // but the last is a READ producing a pointer to an inner array; the final one is the
    // element write. Build the read pointer through the leading subscripts, then emit the
    // final operator[]= against it. (`grid[0]` was already parsed above as arrPath+indexExpr.)
    if(afterBracket.is(token::bracketOpen))
        return processChainedSubscriptWrite(arrPath, indexExpr, sc);

    return processSubscriptWrite(arrPath, indexExpr, sc);
}

// Resolves an assignment's left-hand side: emitted name, declared type, ref / byte-array flags,
// emitter `$self`, and the class used for `operator =` dispatch.
bglParser::AssignTarget bglParser::resolveAssignmentTarget(const string& lhsOriginal, StatementContext& sc){
    AssignTarget t;
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    const string& stmtCastType = sc.castType;
    // `container.children = { … }` at runtime reads as "replace all contents" — a footgun. Only the
    // object-body form (initial population) uses `=`; at runtime require `+=` to add.
    if(lhsOriginal.size() > 9 && lhsOriginal.substr(lhsOriginal.size() - 9) == ".children")
        parsingError("assigning to `.children` with `=` would replace all contents; use `.children += { … }` to add objects (or move them individually).");
    if(isConstName(lhsOriginal, func, body))
        parsingError(format("Cannot assign to const variable '{0}'", lhsOriginal));
    if(func != nullptr){
        string qualified = qualifyIdentifier(lhsOriginal, func, body);
        if(qualified.empty())
            parsingError(format("Undeclared variable '{0}'", lhsOriginal));
        // Honour a member's `as <i6name>` alias on the assignment target, as the read
        // path does — the declaration emits under the alias, so writing to the Beguile
        // name would target a property that does not exist.
        if(size_t d = qualified.rfind('.'); d != string::npos){
            string recvPath = lhsOriginal.substr(0, lhsOriginal.rfind('.'));
            string mem      = qualified.substr(d + 1);
            string recvType = resolveIdentifierType(recvPath, func, body, mem);
            if(recvType.empty()) recvType = resolvePathType(recvPath, func, body);   // through namespace aliases
            // `((T)o).m = v`: the cast names the member's class.
            if(!stmtCastType.empty() && recvPath.find('.') == string::npos) recvType = stmtCastType;
            string aliased  = memberI6Name(recvType, mem);
            if(aliased != mem) qualified = qualified.substr(0, d + 1) + aliased;
        }
        if(stmtCastType.empty() || lhsOriginal.find('.') != lhsOriginal.rfind('.'))
            qualified = withMemberI6Name(lhsOriginal, qualified, func, body);   // the members along the path too
        t.variableLeft = qualified;
    } else {
        t.variableLeft = lhsOriginal;
    }

    // look up the left-hand variable's type using original (unqualified) name
    typeDef*& leftType = t.leftType;
    bool& lhsIsRefLocal = t.lhsIsRefLocal;     // set true if the bare LHS resolves to a `ref` local
    bool& lhsIsByteArray = t.lhsIsByteArray;    // set true if the LHS is an array<char> (byteArray) — value-copy unsupported
    // $self for emitter substitution. A member declared `as <i6name>` emits under that
    // name, so the emitter body has to address it the same way the declaration did —
    // otherwise `r1.name = "x"` sent the message to a property that does not exist.
    string& emitterSelfForLhs = t.emitterSelf;
    emitterSelfForLhs = lhsOriginal;
    if(func != nullptr){
        if(size_t ed = lhsOriginal.rfind('.'); ed != string::npos){
            string recvPath = lhsOriginal.substr(0, ed);
            string mem      = lhsOriginal.substr(ed + 1);
            // Resolve the member alias against the ORIGINAL receiver path — the type registry keys
            // on Beguile names, so rewriting the receiver first would lose the type.
            string recvType = resolveIdentifierType(recvPath, func, body, mem);
            if(recvType.empty()) recvType = resolvePathType(recvPath, func, body);
            string aliased  = memberI6Name(recvType, mem);
            // The receiver may carry an `as` alias of its own (`object beacon as lamp`). A proxy
            // emitter addresses it directly — parentProp's body is `move $self to $v` — so $self
            // has to be the emitted name, or the store targets an object I6 never declared.
            string emitRecv = recvPath;
            if(recvPath.find('.') == string::npos){
                string q = qualifyIdentifier(recvPath, func, body, mem);
                if(!q.empty() && q.find('(') == string::npos && q.find('.') == string::npos)
                    emitRecv = q;
            } else {
                // A path through namespace aliases (`bgl.ui.mainWin`) names an object by another name.
                string q = qualifyIdentifier(recvPath, func, body, mem);
                if(!q.empty() && q.find('(') == string::npos) emitRecv = q;
            }
            if(aliased != mem || emitRecv != recvPath) emitterSelfForLhs = emitRecv + "." + aliased;
            emitterSelfForLhs = withMemberI6Name(lhsOriginal, emitterSelfForLhs, func, body);
        }
    }

    size_t lhsDot = lhsOriginal.rfind('.');
    if(lhsDot != string::npos){
        // dot-path LHS: resolve owner type, then find property type in its class
        string ownerPath = lhsOriginal.substr(0, lhsDot);
        string propName  = lhsOriginal.substr(lhsDot + 1);
        // Check for static member assignment: ClassName.staticMember
        classDef* ownerAsCls = languageService.findClass(ownerPath);
        // The class may be reached through a namespace type alias (`alias gizmo for Gadget;`), in
        // which case ownerPath is a dotted path and the direct lookup misses. Reads already resolve
        // it, so without this a WRITE through the alias fell through to a plain property store on
        // the emitted Class directive — `gadget.s = 9` — which nothing ever reads back.
        if(ownerAsCls == nullptr && ownerPath.find('.') != string::npos)
            if(string t = resolveNamespacedType(ownerPath); !t.empty())
                ownerAsCls = languageService.findClass(t);
        if(ownerAsCls != nullptr){
            // A subclass name reaches its bases' statics too: one variable, the declaring class's.
            bool isStaticMember = false;
            if(typeMember* m = findMemberInHierarchy(ownerAsCls, [&](typeMember* mm){
                   auto* v = dynamic_cast<variableDeclaration*>(mm); return v && v->isStatic && v->name == propName; }))
                if(auto* vd = dynamic_cast<variableDeclaration*>(m)){
                    if(vd->isConst) parsingError(format("Cannot assign to const member '{0}'", propName));
                    string mangledName = "_bgl_" + ownerAsCls->declaringClass(vd)->name + "_" + propName;
                    t.variableLeft = mangledName;
                    emitterSelfForLhs = mangledName;  // $self should be the mangled global, not the owner
                    leftType = &vd->type;
                    if(auto* _ad = dynamic_cast<arrayDeclaration*>(vd)) lhsIsByteArray = _ad->isByteArray;
                    isStaticMember = true;
                }
            if(!isStaticMember)
                rejectNonStaticOnTypeName(ownerPath, ownerAsCls, propName);
        }
        // Pass the member being assigned as the hint: when the owner name matches more than one
        // declaration (a user global shadowing an unprefixed BLR global such as `print` or `the`),
        // it is what picks the candidate whose type actually has that member.
        string ownerType = leftType != nullptr ? "" : resolvePathType(ownerPath, func, body, propName);
        // An emitter value (`size`) can be read but has nothing to assign to.
        if(leftType == nullptr)
            if(classDef* oc = getDispatchClass(ownerType.empty() ? resolvePathType(ownerPath, func, body) : ownerType))
                if(findMemberInHierarchy(oc, [&](typeMember* m){
                       auto* fd = dynamic_cast<functionDef*>(m);
                       return fd && fd->name == propName && fd->isEmitter && fd->isValueEmitter; }))
                    parsingError(format("'{0}' is read-only: it is computed, so it can't be assigned", propName));
        // `hide` enforcement (write): block `v.member = …` when member's write (`operator =`), or
        // the whole member, is hidden on v's static type. `(Base)v.member = …` retypes the owner
        // to Base — the door. Reads are unaffected (fires only on this assignment path).
        {   string hideOwnerType = (!stmtCastType.empty() && ownerPath.find('.') == string::npos)
                                 ? stmtCastType : ownerType;
            if(!hideOwnerType.empty()){
                enforceHidden(getDispatchClass(hideOwnerType), propName, "=", {}, ownerPath);
                t.hideOwnerType = hideOwnerType; t.hideMember = propName; t.hideOwnerPath = ownerPath;
            }
        }
        if(!ownerType.empty()){
            // The owner may be a classDef (direct class reference) or an objectDef
            // (object instance with its own type identity). For objectDefs, look
            // first at the instance's own members, then walk its class hierarchy —
            // without the hierarchy walk, inherited members like `parent` declared
            // on the base `object` class wouldn't be found, and `obj.parent = X`
            // would fall through to a literal `obj.parent = X` instead of dispatching
            // the parentProp operator= (→ `move obj to X`).
            typeDef& ownerTd = languageService.getType(ownerType);
            classDef* hierarchyRoot = dynamic_cast<classDef*>(&ownerTd);
            if(hierarchyRoot == nullptr && ownerType.find('<') != string::npos)
                hierarchyRoot = getDispatchClass(ownerType);   // `array<int>` (a parameter) is the array class
            objectDef* ownerAsObj = dynamic_cast<objectDef*>(&ownerTd);
            // A verb is its own object, kept with the verbs: named by its type (`self` in its body) or by path.
            classDef* verbCls = languageService.findClass("verb");
            bool ownerIsVerbish = hierarchyRoot == nullptr
                || (verbCls != nullptr && (hierarchyRoot == verbCls || hierarchyRoot->hasAncestor(verbCls)));
            if(ownerAsObj == nullptr && ownerIsVerbish){
                string lowerOwner = ownerPath; transform(lowerOwner.begin(), lowerOwner.end(), lowerOwner.begin(), ::tolower);
                for(verbObjectDef* v : languageService.verbs)
                    if(v->name == ownerType || v->name == lowerOwner){ ownerAsObj = v; hierarchyRoot = nullptr; break; }
            }
            if(hierarchyRoot == nullptr){
                if(auto* ownerObj = ownerAsObj){
                    for(typeMember* m : ownerObj->members)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                            if(vd->name == propName){
                                if(vd->isConst) parsingError(format("Cannot assign to const member '{0}'", propName));
                                leftType = &vd->type;
                    if(auto* _ad = dynamic_cast<arrayDeclaration*>(vd)) lhsIsByteArray = _ad->isByteArray;
                                if(vd->isRefLocal) lhsIsRefLocal = true;   // ref member: pointer-copy assign
                                break;
                            }
                    if(leftType == nullptr) hierarchyRoot = ownerObj->objectClass;
                }
            }
            if(leftType == nullptr && hierarchyRoot != nullptr){
                typeMember* found = findMemberInHierarchy(hierarchyRoot, [&](typeMember* m){
                    auto* vd = dynamic_cast<variableDeclaration*>(m);
                    return vd != nullptr && vd->name == propName;
                });
                if(found){
                    auto* vd = dynamic_cast<variableDeclaration*>(found);
                    if(vd->isConst) parsingError(format("Cannot assign to const member '{0}'", propName));
                    leftType = &vd->type;
                    if(auto* _ad = dynamic_cast<arrayDeclaration*>(vd)) lhsIsByteArray = _ad->isByteArray;
                    if(vd->isRefLocal) lhsIsRefLocal = true;   // ref member: pointer-copy assign
                }
            }
        }
        // $self = the OWNER for a property-class member, whose emitters are written against
        // the host (parentProp's `parent($self)`). For a member that simply stores a class
        // instance, $self is the member itself — its operator= acts on the stored value, so
        // pointing $self at the owner sent the message to the wrong object entirely
        // (`j.setFromLit(...)` instead of `j.name.setFromLit(...)`).
        if((emitterSelfForLhs.rfind("_bgl_", 0) != 0 || emitterSelfForLhs.find('.') != string::npos)
           && (leftType == nullptr || isPropertyClassType(leftType->name))){
            // $self = the owner object, not the full obj.prop path — and the owner's EMITTED name,
            // since an owner declared `object beacon as lamp` otherwise produced `move beacon to …`,
            // addressing an object Inform 6 never declared.
            string emitOwner = ownerPath;
            if(func != nullptr && ownerPath.find('.') == string::npos){
                string q = qualifyIdentifier(ownerPath, func, body);
                if(!q.empty() && q.find('(') == string::npos
                   && (q.find('.') == string::npos || q.rfind("self.", 0) == 0))   // a bare member is self's
                    emitOwner = q;
            } else if(func != nullptr){
                // A path through namespace aliases (`bgl.ui.mainWin`) names an object by another name.
                string q = qualifyIdentifier(ownerPath, func, body);
                if(!q.empty() && q.find('(') == string::npos) emitOwner = q;
            }
            // An array owner is addressed as its emitters expect: (owner, prop) for a member array.
            string ownerArrType = ownerType.empty() ? resolvePathType(ownerPath, func, body) : ownerType;
            if(isWordArrayType(ownerArrType)){
                ArrayReceiver arr = arrayReceiver(emitOwner, ownerPath, ownerArrType, func, body);
                if(arr.isMember && propName == "length")
                    rejectRawMemberLengthOp(arr.owner, arr.prop, "length =", func, body);
                if(arr.isMember){ emitOwner = arr.owner; t.emitterProp = propertyI6Name(arr.prop); }
                else t.emitterProp = "0";
            }
            emitterSelfForLhs = emitOwner;
        }
    } else {
        if(func != nullptr){
            for(paramDef* p : func->params)
                if(p->name == lhsOriginal){ leftType = &p->type; break; }
            if(leftType == nullptr && body != nullptr)
                for(statement* s : body->statements)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(s))
                        if(vd->name == lhsOriginal){
                            leftType = &vd->type;
                    if(auto* _ad = dynamic_cast<arrayDeclaration*>(vd)) lhsIsByteArray = _ad->isByteArray;
                            if(vd->isRefLocal) lhsIsRefLocal = true;
                            break;
                        }
            // A local of an enclosing block (`if (…) s = …;` assigns the routine's `s`).
            if(leftType == nullptr){
                vector<statementBlock*> outer(activeBlockStack.begin(), activeBlockStack.end());
                if(currentFunc != nullptr) outer.push_back(dynamic_cast<statementBlock*>(currentFunc->body));
                for(statementBlock* blk : outer){
                    if(blk == nullptr || blk == body || leftType != nullptr) continue;
                    for(statement* s : blk->statements)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(s); vd && vd->name == lhsOriginal){
                            leftType = &vd->type;
                            if(auto* _ad = dynamic_cast<arrayDeclaration*>(vd)) lhsIsByteArray = _ad->isByteArray;
                            if(vd->isRefLocal) lhsIsRefLocal = true;
                            break;
                        }
                }
            }
            if(leftType == nullptr && currentObject != nullptr)
                for(typeMember* m : currentObject->members)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                        if(vd->name == lhsOriginal){
                            if(vd->isConst) parsingError(format("Cannot assign to const member '{0}'", lhsOriginal));
                            leftType = &vd->type;
                    if(auto* _ad = dynamic_cast<arrayDeclaration*>(vd)) lhsIsByteArray = _ad->isByteArray;
                            if(vd->isRefLocal) lhsIsRefLocal = true;   // ref member: pointer-copy assign
                            break;
                        }
            if(leftType == nullptr && currentClass != nullptr)
                for(typeMember* m : currentClass->members)
                    if(auto* vd = dynamic_cast<variableDeclaration*>(m))
                        if(vd->name == lhsOriginal){
                            if(vd->isConst) parsingError(format("Cannot assign to const member '{0}'", lhsOriginal));
                            leftType = &vd->type;
                    if(auto* _ad = dynamic_cast<arrayDeclaration*>(vd)) lhsIsByteArray = _ad->isByteArray;
                            if(vd->isRefLocal) lhsIsRefLocal = true;   // ref member: pointer-copy assign
                            break;
                        }
        }
        if(leftType == nullptr)
            if(auto* vd = languageService.findGlobalAs<variableDeclaration>(lhsOriginal)){ leftType = &vd->type; if(auto* _ad = dynamic_cast<arrayDeclaration*>(vd)) lhsIsByteArray = _ad->isByteArray; }    }

    // Resolve via getDispatchClass so a template-typed LHS (e.g. `array<int>`) reaches
    // operator= dispatch — getType("array<int>") returns null (only the base `array` is
    // registered), which would silently skip copy semantics. getDispatchClass strips the
    // <...> and resolves the base class. (For non-template names this is equivalent.)
    // `self = v` in a value class's method copies into the instance through its `operator =`; in a
    // reference class `self` is not assignable storage of its own.
    if(leftType == nullptr && lhsOriginal == "self"){
        classDef* selfCls = currentClass != nullptr ? currentClass : (currentObject != nullptr ? currentObject->objectClass : nullptr);
        if(selfCls != nullptr && isValueClass(selfCls)) leftType = &languageService.getType(selfCls->name);
    }
    t.classType = leftType != nullptr ? getDispatchClass(leftType->name) : nullptr;
    // `(Base)x = v;` — assign through Base's `operator =`, as `(Base)x.method()` calls Base's method: how
    // a subclass's copy operator chains to its ancestor's.
    // `self` has no declared type here; its class is the enclosing one.
    classDef* castFrom = t.classType;
    if(castFrom == nullptr && lhsOriginal == "self")
        castFrom = currentClass != nullptr ? currentClass : (currentObject != nullptr ? currentObject->objectClass : nullptr);
    if(!stmtCastType.empty() && castFrom != nullptr)
        if(classDef* cast = getDispatchClass(stmtCastType); cast != nullptr && castFrom->hasAncestor(cast)){
            t.ancestorCast = cast;
            if(leftType == nullptr){ leftType = &languageService.getType(castFrom->name); t.classType = castFrom; }
        }
    // A whole member array as the target (`holder.rows = src;`) is addressed as (owner, prop), as its
    // emitters expect; any other array as (array, 0).
    if(leftType != nullptr && isWordArrayType(leftType->name) && t.emitterProp.empty()){
        t.emitterProp = "0";
        if(lhsOriginal.find('.') != string::npos){
            string recv = t.emitterSelf.empty() ? t.variableLeft : t.emitterSelf;
            ArrayReceiver r = arrayReceiver(recv, lhsOriginal, leftType->name, sc.func, sc.body);
            if(r.isMember){ t.emitterSelf = r.owner; t.emitterProp = propertyI6Name(r.prop); }
        }
    }
    return t;
}

// A whole member array that is its property's data, read as a value (`o.log`, `((Bay)o).counts`, `(f()).log`):
// its owner and property. Its elements live in the object, so it is copied, not referred to.
bool bglParser::memberArraySource(expression* val, string& owner, string& prop){
    // Only when the expression ends at the member: a chain on it (`o.log.orderBy(…)`) is a result of its own.
    string full = val->text();
    auto endsAtMember = [&](const string& p){
        string pi = propertyI6Name(p);
        auto ends = [&](const string& tail){ return full.size() > tail.size() + 1 && full.compare(full.size() - tail.size() - 1, string::npos, "." + tail) == 0; };
        return ends(p) || ends(pi);
    };
    if(val->memberArrayDecl != nullptr && !val->memberArrayOwner.empty() && endsAtMember(val->memberArrayProp)){
        if(val->memberArrayDecl->isRefLocal || val->memberArrayDecl->isRaw) return false;
        owner = val->memberArrayOwner; prop = val->memberArrayProp;
        return true;
    }
    string vText = val->text();
    if(vText.find('.') == string::npos || vText.find('(') != string::npos) return false;
    return splitQualifiedMember(vText, currentFunc, nullptr, owner, prop)
        && memberArrayIsTracked(owner, prop, currentFunc, nullptr) && !memberArrayIsRef(owner, prop, currentFunc, nullptr);
}

// Applies `operator =` dispatch (emitter, method, or conversion) to one assignment node.
void bglParser::resolveAssignmentOperator(assignmentStatement& a, expression* val, const AssignTarget& t, bool isBindAssign){
    // A hide narrowed to one overload (`hide fill.operator = (int);`) blocks a write of that operand's type.
    if(!t.hideOwnerType.empty() && val != nullptr && !val->resolvedType.empty()){
        string vt = literalBaseType(val->resolvedType).empty() ? val->resolvedType : literalBaseType(val->resolvedType);
        enforceHidden(getDispatchClass(t.hideOwnerType), t.hideMember, "=", {vt}, t.hideOwnerPath);
    }
    if(t.leftType != nullptr && !isBindAssign) applyImplicitConversion(val, t.leftType->name);
    if(t.leftType != nullptr && t.leftType->name == "verb") applyActionConstant(val);
    // A whole array as the target: an inline member array keeps its elements in its object; a global one
    // becomes a variable over its storage, so it can be re-pointed.
    string storeText;   // the store into a global or member array, which takes the place of its `operator =`
    if(t.leftType != nullptr && val != nullptr && (isWordArrayType(t.leftType->name) || t.leftType->name == "bytearray")
       && a.src.file.find("beguiLib") == string::npos){
        bool member = !t.emitterProp.empty() && t.emitterProp != "0";
        if(member){
            // A member array that is its property's data takes a copy into its own elements.
            if(!memberArrayIsRef(t.emitterSelf, t.emitterProp, currentFunc, nullptr) && t.leftType->name != "bytearray"){
                string vText = val->text(), vOwner, vProp;
                bool fromMember = memberArraySource(val, vOwner, vProp);
                string dst = t.emitterSelf + ", " + escapePropertyName(t.emitterProp);
                // Elements that own instances are copied through their type's `operator =`, not shared.
                string assign = substituteElemOps("$opref(=)", resolveArrayElementTypeDotted(t.emitterSelf, t.emitterProp, currentFunc, nullptr), "");
                val->tokens = {fromMember ? "_bglMemberArrCopyMember(" + dst + ", " + vOwner + ", " + escapePropertyName(vProp) + ", " + assign + ")"
                                          : "_bglMemberArrCopy(" + dst + ", " + vText + ", " + assign + ")"};
                a.variableLeft = "_bglSink-->0";   // the copy is the statement; what it returns isn't kept
                a.assignedExpression = val;
                return;
            }
        } else {
            // A whole member array on the right: its elements are copied into this array's storage, until
            // member arrays are references.
            string vOwner, vProp;
            if(t.leftType->name != "bytearray" && memberArraySource(val, vOwner, vProp))
                val->tokens = {"_bglArrFromMember(" + a.variableLeft + ", " + vOwner + ", " + escapePropertyName(vProp) + ", "
                               + substituteElemOps("$opref(=)", resolveArrayElementTypeDotted(vOwner, vProp, currentFunc, nullptr), "") + ")"};
        }
        bool lasting = member;   // a global or member outlives the routine; a local array assigned to it is copied
        if(!member && a.variableLeft.find('.') == string::npos)
            if(auto* ga = languageService.findGlobalAs<arrayDeclaration>(a.variableLeft);
               ga != nullptr && (currentFunc == nullptr || (!qualifyFromParams(a.variableLeft, currentFunc)
                                 && !qualifyFromBodyLocals(a.variableLeft, activeBlockStack.empty() ? nullptr : activeBlockStack.back())))){
                // One declared `ref`, with no storage, is already a plain variable.
                if(ga->arraySize > 0 || dynamic_cast<initializerList*>(ga->declaredExpressionValue) != nullptr
                   || !ga->stringInitializer.empty())
                    ga->isRepointed = true;
                lasting = true;
            }
        string lOwner, lProp;   // a member held through a pointer: `self.waiting`, `box.log`
        bool pointerMember = !member && a.variableLeft.find('.') != string::npos
            && splitQualifiedMember(a.variableLeft, currentFunc, nullptr, lOwner, lProp);
        if((lasting || pointerMember) && currentFunc != nullptr && t.leftType->name == "bytearray")
            storeText = "_bglByteArrStore(" + (member ? objectI6Name(t.emitterSelf) + "." + propertyI6Name(t.emitterProp) : a.variableLeft)
                      + ", " + val->text() + ")";
        else if((lasting || pointerMember) && currentFunc != nullptr){
            string dst = member ? objectI6Name(t.emitterSelf) + "." + propertyI6Name(t.emitterProp) : a.variableLeft;
            string elem = member ? resolveArrayElementTypeDotted(t.emitterSelf, t.emitterProp, currentFunc, nullptr)
                        : pointerMember ? resolveArrayElementTypeDotted(lOwner, lProp, currentFunc, nullptr)
                                 : resolveArrayElementType(a.variableLeft, currentFunc, nullptr);
            storeText = "_bglArrStore(" + dst + ", " + val->text() + ", " + substituteElemOps("$opref(=)", elem, "") + ")";
        }
    }
    const string& emitterSelfForLhs = t.emitterSelf;
    classDef* classType = t.ancestorCast != nullptr ? t.ancestorCast : t.classType;
    typeDef* leftType   = t.leftType;
    const bool lhsIsByteArray = t.lhsIsByteArray;
    a.emitterSelf = emitterSelfForLhs;  // always record $self for this assignment
    a.emitterProp = t.emitterProp;
    if(leftType != nullptr && val != nullptr && !isBindAssign
       && (classType == nullptr || leftType->name.find('<') != string::npos))
        checkClasslessAssignable(val, leftType->name, "variable");
    // An array variable's declared type is the bare `array`; its element type is on the declaration.
    if(leftType != nullptr && val != nullptr && !isBindAssign
       && (leftType->name == "array" || leftType->name == "rawarray")){
        string elem = resolveArrayElementType(a.variableLeft, currentFunc, nullptr);
        if(!elem.empty()) checkClasslessAssignable(val, leftType->name + "<" + elem + ">", "variable");
    }
    if(val != nullptr && !isBindAssign) rejectNullIntoValueClass(val, classType);
    if(val != nullptr && !isBindAssign) rejectUnionInto(val, classType);
    if(!storeText.empty()){
        val->tokens = {storeText};
        a.assignedExpression = val;
        return;
    }
    // Only `:=` skips operator= dispatch — that is what rebinding means. A plain `=`
    // on a `ref` slot assigns THROUGH the reference: it dispatches the type's
    // operator= into whatever the slot currently points at, exactly as it would on a
    // slot that owned its instance.
    if(classType != nullptr && val != nullptr && !isBindAssign){
        string valueTypeName = val->resolvedType;
        // A whole-array copy stores each element through the element type's operators.
        string arrElem;
        if(leftType != nullptr && isWordArrayType(leftType->name))
            arrElem = leftType->name.find('<') != string::npos ? listElementType(leftType->name)
                                                                : resolveArrayElementType(a.variableLeft, currentFunc, nullptr);
        if(!valueTypeName.empty()){
            // The class's `operator =` for this value, most specific parameter first (findAssignOperator):
            // an emitter expands inline; a regular method is called once, through its mangled name, on
            // the full LHS path ($target) so a member assignment dispatches on the property.
            bool found = false;
            if(functionDef* opFunc = findAssignOperator(classType, valueTypeName, [](functionDef* f){
                   return !f->isEmitter || dynamic_cast<i6Block*>(f->body) != nullptr; }, /*allowInherited*/true,
                   leftType->name)){
                if(opFunc->isEmitter){
                    // Pre-substitute $class with the LHS's declared type. $self / $param /
                    // $target are substituted later at emit time (i6Emitter), but $class
                    // resolves at parse time because it depends on the static type known here.
                    emitterBindings cb; cb.cls = classType->i6Name(); cb.elemType = arrElem;
                    a.emitterBody = expandEmitterBody(dynamic_cast<i6Block*>(opFunc->body), cb);
                    a.emitterParam = opFunc->params[0]->name;
                } else {
                    // The overload set's names settle first: a class declared below has only its stubs yet.
                    mangleOverloadSetForReceiver(classType->name, opFunc->name);
                    if(opFunc->i6name.empty()) opFunc->i6name = mangleOperatorName(opFunc->name);
                    // An override of an overloaded ancestor operator takes the overload's name too.
                    {
                        auto sigOf = [](functionDef* f){ string sig; for(paramDef* p : f->params) sig += p->type.name + ","; return sig; };
                        const string mine = sigOf(opFunc);
                        bool overloaded = false;
                        function<void(classDef*)> walk = [&](classDef* c){   // a class whose own set it belongs to has several
                            if(c == nullptr || overloaded) return;
                            set<string> sigs;
                            for(typeMember* m : c->members)
                                if(auto* f = dynamic_cast<functionDef*>(m); f && f->name == opFunc->name && !f->isEmitter) sigs.insert(sigOf(f));
                            if(sigs.size() > 1 && sigs.count(mine)) overloaded = true;
                            for(classDef* b : c->baseClasses) walk(b);
                        };
                        walk(classType->declaringClass(opFunc) ? classType->declaringClass(opFunc) : classType);
                        if(overloaded){
                            string disc;
                            for(paramDef* p : opFunc->params){
                                string pt = p->type.name;
                                if(size_t lt = pt.find('<'); lt != string::npos) pt = pt.substr(0, lt);
                                disc += "_" + (pt.empty() ? string("var") : pt);
                            }
                            opFunc->i6name = mangleOperatorName(opFunc->name) + disc;
                        }
                    }
                    string paramName = opFunc->params[0]->name;
                    // An ancestor cast calls THAT class's routine statically (I6 `obj.Base::prop`).
                    string routine = t.ancestorCast != nullptr ? t.ancestorCast->i6Name() + "::" + opFunc->i6name : opFunc->i6name;
                    // A value variable is its declared type, though a `ref` may be bound to a derived one.
                    if(t.ancestorCast == nullptr && isValueClass(classType))
                        if(classDef* owner = classType->declaringClass(opFunc); owner != nullptr && hasDerivedClass(owner))
                            routine = owner->i6Name() + "::" + opFunc->i6name;
                    convertToParam(a.assignedExpression, valueTypeName, opFunc->params[0]->type.name);
                    a.emitterBody  = format("$target.{0}(${1});", routine, paramName);
                    a.emitterParam = paramName;
                }
                found = true;
            }
            // Template-aware emitter match: an emitter operator= whose parameter and the RHS
            // resolve to the SAME dispatch class (e.g. `operator=(array<T>)` for an `array<int>`
            // RHS). The exact/var string match above can't see this because the param type name
            // ("array" / "array<T>") won't string-equal the RHS ("array<int>"). Backs array
            // copy-on-assign. Only adds matches the string passes missed (for non-generic
            // classes it's equivalent to the exact match, which already ran).
            if(!found){
                classDef* valCls = getDispatchClass(valueTypeName);
                if(valCls != nullptr){
                    typeMember* m = findMemberInHierarchy(classType, [&](typeMember* mm){
                        auto* opFunc = dynamic_cast<functionDef*>(mm);
                        return opFunc && opFunc->name=="=" && opFunc->isEmitter && opFunc->params.size()==1
                               && dynamic_cast<i6Block*>(opFunc->body)!=nullptr
                               && getDispatchClass(opFunc->params[0]->type.name) == valCls
                               && templateArgsFit(valueTypeName, leftType->name);
                    });
                    if(m){
                        auto* opFunc = dynamic_cast<functionDef*>(m);
                        auto* blk = dynamic_cast<i6Block*>(opFunc->body);
                        emitterBindings cb; if(classType != nullptr) cb.cls = classType->i6Name();
                        cb.elemType = arrElem;
                        a.emitterBody = expandEmitterBody(blk, cb);
                        a.emitterParam = opFunc->params[0]->name;
                        found = true;
                    }
                }
            }
            // Compatible-arg operator= match: an operator= whose parameter is isTypeCompatible
            // with the RHS but not caught by the exact/var/template/upcast passes above. The key
            // case is a LITERAL RHS into a value-typed setter — `obj.height = 5` where `height`'s
            // type declares `operator=(int)`: the literal is typed `intliteral`, which is
            // assignable to `int` (isTypeCompatible) but is not a subclass of it, so the
            // inheritance-based upcast pass misses it. Runs last, so exact overloads still win.
            // Handles the emitter form (inline body) and the non-emitter form (mangled call).
            // Guard: only when the RHS is NOT already assignable to the member type by the raw
            // path — otherwise a plain `intVar = 0` would needlessly route through int's identity
            // `operator=($target=$v)` and reformat. This fires precisely for a proxy member whose
            // type isn't literal-compatible on its own but declares an operator= that accepts the
            // literal's base type (e.g. `heightProxy` with `operator=(int)`).
            if(!found && !isTypeCompatible(valueTypeName, leftType->name)){
                typeMember* m = findMemberInHierarchy(classType, [&](typeMember* mm){
                    auto* opFunc = dynamic_cast<functionDef*>(mm);
                    return opFunc && opFunc->name=="=" && opFunc->isEmitter && opFunc->params.size()==1
                           && dynamic_cast<i6Block*>(opFunc->body)!=nullptr
                           && !isPlainAssignOperator(classType, opFunc)
                           && isTypeCompatible(valueTypeName, opFunc->params[0]->type.name);
                });
                if(m){
                    auto* opFunc = dynamic_cast<functionDef*>(m);
                    auto* blk = dynamic_cast<i6Block*>(opFunc->body);
                    emitterBindings cb; if(classType != nullptr) cb.cls = classType->i6Name();
                    a.emitterBody = expandEmitterBody(blk, cb);
                    a.emitterParam = opFunc->params[0]->name;
                    found = true;
                }
                if(!found){
                    m = findMemberInHierarchy(classType, [&](typeMember* mm){
                        auto* opFunc = dynamic_cast<functionDef*>(mm);
                        return opFunc && opFunc->name=="=" && !opFunc->isEmitter
                               && opFunc->params.size()==1
                               && !isPlainAssignOperator(classType, opFunc)
                               && isTypeCompatible(valueTypeName, opFunc->params[0]->type.name);
                    });
                    if(m){
                        auto* opFunc = dynamic_cast<functionDef*>(m);
                        if(opFunc->i6name.empty()) opFunc->i6name = mangleOperatorName(opFunc->name);
                        string paramName = opFunc->params[0]->name;
                        convertToParam(a.assignedExpression, valueTypeName, opFunc->params[0]->type.name);
                        a.emitterBody  = format("$target.{0}(${1});", opFunc->i6name, paramName);
                        a.emitterParam = paramName;
                        found = true;
                    }
                }
            }
            bool foundViaOperatorEq = found;
            // array<char> (byteArray) inherits array<T>'s word copy, which would corrupt byte data;
            // the byte copy is byteArray's own operator=, which <buf> declares.
            if(foundViaOperatorEq && lhsIsByteArray && classType != nullptr
               && none_of(classType->members.begin(), classType->members.end(), [](typeMember* m){
                      auto* f = dynamic_cast<functionDef*>(m); return f && f->name == "="; }))
                parsingError("copying one array<char> into another (`dst = src`) needs #include <buf>.");
            if(!found) found = isTypeCompatible(valueTypeName, leftType->name);
            // Silent value-semantics gap: TypeCompatible let it through but no
            // operator= matched, AND the LHS class carries its own stored fields,
            // AND it isn't a world-tree citizen (object-derived classes use
            // reference semantics by convention). Force the user to declare
            // operator= so copy semantics aren't a surprise.
            if(found && !foundViaOperatorEq && classHasStoredFields(classType) && !isReferenceBacked(classType))
                parsingError(format("'{0}' is a value class with no copy operator ('operator =' taking '{0}'), so there "
                                    "is nothing to copy with. Declare one; or, to re-point a 'ref', use ':='.",
                    typeDisplayName(leftType->name)));
            if(!found){
                // Fallback: check if RHS type has emitter LhsType operator(){}
                classDef* rhsCls = languageService.classOf(valueTypeName);
                if(rhsCls != nullptr)
                    if(typeMember* m = findMemberInHierarchy(rhsCls, [&](typeMember* m){
                        auto* opFn = dynamic_cast<functionDef*>(m);
                        return opFn && opFn->name=="operator()" && opFn->params.empty() && opFn->isEmitter && !opFn->isExplicit
                               && opFn->returnType.name==leftType->name && dynamic_cast<i6Block*>(opFn->body)!=nullptr;
                    })){
                        auto* opFn = dynamic_cast<functionDef*>(m);
                        auto* blk = dynamic_cast<i6Block*>(opFn->body);
                        emitterBindings cb2; cb2.self = val->text();
                        string b = expandEmitterBody(blk, cb2);
                        val->tokens.clear();
                        val->tokens.push_back(b);
                        val->resolvedType = leftType->name;
                        found = true;
                    }
                // Same fallback for a NON-emitter (regular-method) conversion operator on the
                // RHS type: call its emitted routine. Parity with the emitter form above and with
                // operator=; this is what makes `int a = obj.height` convert through a Beguile-method
                // `operator()` getter, not just an emitter one.
                if(!found && rhsCls != nullptr)
                    if(typeMember* m = findMemberInHierarchy(rhsCls, [&](typeMember* m){
                        auto* opFn = dynamic_cast<functionDef*>(m);
                        return opFn && opFn->name=="operator()" && opFn->params.empty() && !opFn->isEmitter && !opFn->isExplicit
                               && opFn->returnType.name==leftType->name;
                    })){
                        auto* opFn = dynamic_cast<functionDef*>(m);
                        if(opFn->i6name.empty()) opFn->i6name = mangleOperatorName(opFn->name);
                        string argText = val->text();
                        val->tokens.clear();
                        val->tokens.push_back(parenthesizeReceiver(argText) + "." + opFn->i6name + "()");
                        val->resolvedType = leftType->name;
                        found = true;
                    }
            }
            if(!found && leftType->name == "bytearraylength")
                parsingError("an array<char>'s length is assignable only with #include <buf>.");
            if(!found)
                parsingError(format("Cannot assign value of type '{0}' to variable of type '{1}'", typeDisplayName(valueTypeName), typeDisplayName(leftType->name)));
        }
    }
    // A member array as the value (`d = holder.rows;`) is read through (owner, prop), as the copy's
    // source pair; it has no address of its own to hand over.
    if(val != nullptr && leftType != nullptr && isWordArrayType(leftType->name) && !a.emitterParam.empty()
       && isWordArrayType(val->resolvedType)){
        string text = val->text();
        if(text.find('(') == string::npos && text.find('.') != string::npos){
            string recv = text;
            ArrayReceiver r = arrayReceiver(recv, text, val->resolvedType, currentFunc,
                                            currentFunc ? dynamic_cast<statementBlock*>(currentFunc->body) : nullptr);
            string pair = "$" + a.emitterParam + ", 0";
            if(r.isMember && a.emitterBody.find(pair) != string::npos){
                a.emitterBody.replace(a.emitterBody.find(pair), pair.size(), "$" + a.emitterParam + ", " + r.prop);
                val->tokens.assign(1, r.owner);
            }
        }
        // `tC.slots = pick(0).slots;`: a member array off a computed owner, as its field read recorded it.
        else if(val->memberArrayDecl != nullptr && !val->memberArrayOwner.empty() && !val->memberArrayDecl->isRefLocal
                && text.size() > val->memberArrayProp.size() + 1
                && text.compare(text.size() - val->memberArrayProp.size() - 1, string::npos, "." + val->memberArrayProp) == 0){
            string pair = "$" + a.emitterParam + ", 0";
            if(a.emitterBody.find(pair) != string::npos){
                a.emitterBody.replace(a.emitterBody.find(pair), pair.size(), "$" + a.emitterParam + ", " + val->memberArrayProp);
                val->tokens.assign(1, val->memberArrayOwner);
            }
        }
    }
}

// `lhs = rhs;` / `lhs := rhs;` — plain assignment and reference binding.
bool bglParser::processAssignmentStatement(token tok, token symbol, StatementContext& sc){
    const sourceLocation& stmtLoc = sc.src;
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    // `:=` is the reference binding (rebinding) operator: it stores the right-hand instance
    // itself and never dispatches the type's `operator =`. `=` keeps its meaning exactly —
    // copy, through operator= when the type defines one. The two are separate spellings
    // because a class that overloads `=` has spent it on copy semantics, leaving no way to
    // say "point at this" otherwise.
    bool isBindAssign = symbol.is(token::bindAssignment);
    assignmentStatement& assignExpr=*(new assignmentStatement());
    assignExpr.src = stmtLoc;
    string lhsOriginal = (string)tok;
    // A bare member of the enclosing object or class is written as `self.member`, so an inherited
    // property class (`parent = x;` → move) dispatches on the right receiver.
    if(sc.func != nullptr && lhsOriginal.find('.') == string::npos){
        string q = qualifyIdentifier(lhsOriginal, sc.func, sc.body);
        if(q.rfind("self.", 0) == 0)
            lhsOriginal = "self." + lhsOriginal;
        else if(size_t dot = q.find('.'); dot != string::npos)
            // A member imported by #using (`level = 5;` for `catalog.level`) is assigned by its full path.
            for(objectDef* imp : usingObjectImports)
                if(q.compare(0, dot, imp->name) == 0 && q.substr(dot + 1) == lhsOriginal){ lhsOriginal = q; break; }
    } else if(sc.func != nullptr){
        // A path that starts at a bare member (`label.price.cents = c;`) starts at self.
        string head = lhsOriginal.substr(0, lhsOriginal.find('.'));
        string hint = lhsOriginal.substr(head.size() + 1, lhsOriginal.find('.', head.size() + 1) - head.size() - 1);
        if(head.find_first_of("([") == string::npos && qualifyIdentifier(head, sc.func, sc.body, hint).rfind("self.", 0) == 0)
            lhsOriginal = "self." + lhsOriginal;
    }
    AssignTarget target = resolveAssignmentTarget(lhsOriginal, sc);
    assignExpr.variableLeft = target.variableLeft;
    typeDef* leftType   = target.leftType;
    classDef* classType = target.classType;

    // `arr = { a, b };` replaces an array's contents: clear(), then the `+= { … }` per-element append.
    // Both come from the <array> surface, as `+= { … }` alone already does.
    if(!isBindAssign && file.peekToken().is(token::braceOpen)){
        size_t dot = tok.value.find('.');
        string aType = dot == string::npos ? resolveIdentifierType(tok.value, func, body)
                                           : resolvePathType(tok.value, func, body);
        if(isWordArrayType(aType) || aType == "bytearray"){
            string recv = target.variableLeft, prop = "0";
            ArrayReceiver r = arrayReceiver(recv, tok.value, aType, func, body);
            if(r.isMember){ recv = r.owner; prop = r.prop; }
            classDef* ac = languageService.classOf(aType);
            auto* clearFn = ac ? dynamic_cast<functionDef*>(findMemberInHierarchy(ac, [](typeMember* m){
                auto* f = dynamic_cast<functionDef*>(m);
                return f && f->name == "clear" && f->isEmitter && f->params.empty()
                       && dynamic_cast<i6Block*>(f->body) != nullptr;
            })) : nullptr;
            if(clearFn == nullptr)
                parsingError(format("Array '{0}' has no clear(), so a brace list cannot replace its contents.", tok.value));
            emitterBindings cb;
            cb.self = recv; cb.val = recv; cb.prop = prop == "0" ? prop : propertyI6Name(prop);
            cb.elemType = dot == string::npos ? resolveArrayElementType(tok.value, func, body)
                        : resolveArrayElementTypeDotted(tok.value.substr(0, dot), tok.value.substr(dot + 1), func, body);
            cb.trim = emitterTrim::wsSemi;
            i6RawNode* clearNode = new i6RawNode();
            clearNode->text = expandEmitterBody(dynamic_cast<i6Block*>(clearFn->body), cb) + ";";
            clearNode->src = stmtLoc;
            if(body != nullptr) body->statements.push_back(clearNode);
            token append; append.value = "+="; append.tokenType = eTokenType::oper;
            if(processArrayBracedCompound(tok, append, target.variableLeft, sc)) return false;
        }
    }

    // Interpolated string literal on RHS: var = $"..."
    if(file.peekToken(1).is("$") && file.peekToken(2).is(eTokenType::quote)){
        file.getToken();  // consume '$'
        assignExpr.interpSegments = parseInterpolatedSegments(func, body);
        endInterpolatedStatement();
        // Create a dummy RHS expression typed as interpolatedstringliteral for emitter resolution
        expression* rhs = new expression();
        rhs->resolvedType = "interpolatedstringliteral";
        assignExpr.assignedExpression = rhs;
        resolveAssignmentOperator(assignExpr, rhs, target, isBindAssign);
        if(body != nullptr) body->statements.push_back(&assignExpr);
        for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
        postInjections.clear();
        return false;
    }

    // Set expected type from the LHS so name resolution can disambiguate the RHS.
    string savedExpectedAssign = currentExpectedType;
    if(leftType != nullptr) currentExpectedType = leftType->name;
    bool savedForBind = readingForBind;
    readingForBind = isBindAssign;
    // A setter that takes a value class (`void operator = (Meter m)`) is given an accessor member itself,
    // not what its getter reads.
    classDef* savedOwnOperandAssign = ownOperatorOperand;
    if(classDef* lc = leftType != nullptr ? getDispatchClass(leftType->name) : nullptr)
        for(typeMember* m : lc->members)
            if(auto* f = dynamic_cast<functionDef*>(m); f && f->name == "=" && f->params.size() == 1)
                if(classDef* pc = getDispatchClass(f->params[0]->type.name); pc != nullptr && pc != lc && isValueClass(pc) && !pc->isPrimitive){
                    ownOperatorOperand = pc;
                    currentExpectedType = pc->name;
                }
    expression* rhs = nullptr;
    try { rhs = parseExpression(file.getToken(), {token::endStatement, "?"}, func, body); }
    catch(...){ ownOperatorOperand = savedOwnOperandAssign; throw; }
    ownOperatorOperand = savedOwnOperandAssign;
    absorbPostfixQuery(rhs);   // `s = a.up? ? x : y;`: the first `?` queries `a.up`
    if(rhs != nullptr && rhs->resolvedType == "array<char>") rhs->resolvedType = "bytearray";   // a row of an array<array<char>>
    readingForBind = savedForBind;
    currentExpectedType = savedExpectedAssign;
    {
        string head = lhsOriginal.substr(0, lhsOriginal.find('.'));
        bool isLocal = lhsOriginal.find('.') == string::npos
            && (qualifyFromParams(head, func) || qualifyFromBodyLocals(head, body) || qualifyFromAncestorBlocks(head, body));
        if(!isLocal) rejectEscapingLambda(rhs, format("stored in '{0}'", lhsOriginal));
        else rejectLoopStoredLambda(rhs, lhsOriginal);
        if(variableDeclaration* vd = findAssignedDeclaration(lhsOriginal, func, body))
            if(vd->isLiteral)
                checkLiteralValue(isUnionType(vd->type.name) ? splitUnionType(vd->type.name) : vector<string>{vd->type.name},
                    vd->type.name, rhs, format("'{0}' is literal: it needs a literal {{KIND}}",
                                              tok.originalValue.empty() ? lhsOriginal : tok.originalValue));
    }

    if(rhs->terminator == "?"){
        // conditional assignment: lhs = condition ? trueVal : falseVal
        // build as an ifStatement with two assignment branches, each with full emitter dispatch
        ifStatement& ifStmt = *(new ifStatement());
        ifStmt.src = stmtLoc;
        ifStmt.condition = rhs;

        auto makeAssign = [&](expression* val) -> assignmentStatement* {
            assignmentStatement* a = new assignmentStatement();
            a->src = stmtLoc;
            a->variableLeft = assignExpr.variableLeft;
            a->assignedExpression = val;
            resolveAssignmentOperator(*a, val, target, isBindAssign);
            return a;
        };

        // The condition's set-up runs first; each branch's own set-up runs inside that branch only.
        for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
        pendingInjections.clear();
        // The branches are the assigned value: they take the target's type, as the plain assignment does.
        string savedExpectedBranch = currentExpectedType;
        if(leftType != nullptr) currentExpectedType = leftType->name;
        expression* trueVal  = parseExpression(file.getToken(), {":"}, func, body);
        ifStmt.thenBlock = new statementBlock();
        for(statement* inj : pendingInjections) ifStmt.thenBlock->statements.push_back(inj);
        pendingInjections.clear();
        expression* falseVal = parseExpression(file.getToken(), {token::endStatement}, func, body);
        currentExpectedType = savedExpectedBranch;
        ifStmt.elseBlock = new statementBlock();
        for(statement* inj : pendingInjections) ifStmt.elseBlock->statements.push_back(inj);
        pendingInjections.clear();

        ifStmt.thenBlock->statements.push_back(makeAssign(trueVal));
        ifStmt.elseBlock->statements.push_back(makeAssign(falseVal));

        if(body != nullptr) body->statements.push_back(&ifStmt);
        for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
        postInjections.clear();
        return false;
    }

    // `:=` binds one instance to a slot of the same class, so both sides must BE that class.
    // Restricting it this way keeps it from becoming a general escape from the type system:
    // it is a reference binding, not a reinterpreting store.
    if(isBindAssign){
        rejectMemberArrayBind(rhs, func, body);
        string rhsT = rhs != nullptr ? rhs->resolvedType : string();
        classDef* lhsCls = classType;
        if(lhsCls == nullptr)
            parsingError(format("the reference binding operator ':=' needs a class-typed left "
                                "side; '{0}' is not one. Use '=' for ordinary assignment.",
                                lhsOriginal));
        // `:=` binds a `ref` slot; any other slot takes `=`, its type's assignment (§3.7).
        {
            bool refSlot = false, known = false;
            if(func != nullptr && lhsOriginal.find('.') == string::npos)
                for(paramDef* p : func->params) if(p->name == lhsOriginal){ known = true; refSlot = p->isRef; }
            if(!known)
                if(auto* vd = findAssignedDeclaration(lhsOriginal, func, body); vd != nullptr){
                    known = true;
                    refSlot = vd->isRefLocal;
                    // A plain array has storage; one declared without any is a `ref array`.
                    if(auto* ad = dynamic_cast<arrayDeclaration*>(vd); ad != nullptr && ad->arraySize <= 0
                       && dynamic_cast<initializerList*>(ad->declaredExpressionValue) == nullptr && ad->stringInitializer.empty())
                        refSlot = true;
                }
            if(known && !refSlot && lhsOriginal.rfind("_bgl", 0) != 0)   // the compiler's own temporaries bind freely
                parsingError(format("':=' binds a 'ref', and '{0}' isn't one: use '=', or declare it `ref {1}`.",
                                    lhsOriginal, lhsCls->dName()));
        }
        if(rhsT.empty() || getDispatchClass(rhsT) == nullptr)
            parsingError(format("the reference binding operator ':=' binds a reference, so the "
                                "right side must be an instance of a class; got '{0}'.",
                                typeDisplayName(rhsT.empty() ? "unknown" : rhsT)));
        else if(getDispatchClass(rhsT) != lhsCls && !isTypeCompatible(rhsT, lhsCls->name))   // `array<int>` binds to an array
            parsingError(format("cannot bind '{0}' to '{1}': the reference binding operator ':=' "
                                "requires the same class (or a subclass). Use '=' to copy values "
                                "between types.",
                                typeDisplayName(languageService.findObjectType(rhsT) != nullptr ? languageService.classOf(rhsT)->name : rhsT),
                                typeDisplayName(lhsCls->name)));
    }
    assignExpr.assignedExpression = rhs;
    // Skip operator= emitter if RHS contains $target — the opcode handles its own store
    if(rhs->text().find("$target") == string::npos)
        resolveAssignmentOperator(assignExpr, rhs, target, isBindAssign);

    for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
    pendingInjections.clear();
    if(body != nullptr) body->statements.push_back(&assignExpr);
    for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
    postInjections.clear();
    return false;
}

// `x.children += { a, b };` — world-model child placement at runtime.
bool bglParser::processChildrenPlacement(token tok, token symbol, StatementContext& sc){
    const sourceLocation& stmtLoc = sc.src;
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    if(symbol.value != "+=")
        parsingError(format("'{0}' on `.children` is not supported; use `+=` to add objects (e.g. `x.children += {{ a, b }}`), or move objects individually.", symbol.value));
    if(!file.peekToken().is(token::braceOpen))
        parsingError("`.children += ` expects a brace list of objects, e.g. `x.children += { a, b }`.");
    string owner = tok.value.substr(0, tok.value.size() - 9);
    string container = func != nullptr ? qualifyIdentifier(owner, func, body) : owner;
    file.getToken();  // consume '{'
    token et = file.getToken();
    while(!et.is(token::braceClose)){
        expression* elem = parseExpression(et, {token::comma, token::braceClose}, func, body);
        i6RawNode* mv = new i6RawNode();
        mv->text = "move " + elem->text() + " to " + container + ";";
        mv->src = stmtLoc;
        if(body != nullptr) body->statements.push_back(mv);
        if(elem != nullptr && elem->terminator == token::braceClose) break;
        et = file.getToken();
    }
    if(file.peekToken().is(token::endStatement)) file.getToken();
    return false;
}

// `arr += { a, b };` / `arr -= { … };` — the per-element compound op over a brace list. Returns
// true when it handled the statement, false to fall through to the single-RHS path.
bool bglParser::processArrayBracedCompound(token tok, token symbol, const string& lhs, StatementContext& sc){
    const sourceLocation& stmtLoc = sc.src;
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    if((symbol.value == "+=" || symbol.value == "-=") && file.peekToken().is(token::braceOpen)){
        size_t dot = tok.value.find('.');
        string aType = dot == string::npos ? resolveIdentifierType(tok.value, func, body)
                                           : resolvePathType(tok.value, func, body);
        if(isWordArrayType(aType) || aType == "bytearray"){
            // A member array binds (owner, prop); a global or local binds (array, 0).
            string self = lhs, prop = "0";
            string recv = lhs;
            ArrayReceiver r = arrayReceiver(recv, tok.value, aType, func, body);
            if(r.isMember){ self = r.owner; prop = r.prop; }
            else self = recv;
            classDef* ac = getDispatchClass(aType);   // `array<int>` for a parameter
            typeMember* opm = ac ? findMemberInHierarchy(ac, [&](typeMember* m){
                auto* f = dynamic_cast<functionDef*>(m);
                return f && f->name == symbol.value && f->isEmitter && f->params.size() == 1
                       && dynamic_cast<i6Block*>(f->body) != nullptr;
            }) : nullptr;
            if(!opm)
                parsingError(format("No operator '{0}' defined on type '{1}'", symbol.value, typeDisplayName(aType)));
            auto* opFunc = dynamic_cast<functionDef*>(opm);
            emitterBindings lb; lb.prop = prop == "0" ? prop : propertyI6Name(prop);
            lb.elemType = dot == string::npos ? resolveArrayElementType(tok.value, func, body)
                        : resolveArrayElementTypeDotted(tok.value.substr(0, dot), tok.value.substr(dot + 1), func, body);
            string opBody = expandEmitterBody(dynamic_cast<i6Block*>(opFunc->body), lb);
            file.getToken();  // consume '{'
            auto* literalArr = dynamic_cast<arrayDeclaration*>(findAssignedDeclaration(tok.value, func, body));
            if(literalArr != nullptr && (!literalArr->literalElements || symbol.value != "+=")) literalArr = nullptr;
            token et = file.getToken();
            while(!et.is(token::braceClose)){
                expression* elem = parseExpression(et, {",", token::braceClose}, func, body);
                for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
                pendingInjections.clear();
                if(literalArr != nullptr) checkLiteralElements(*literalArr, {elem});
                if(elem->resolvedType == "verb") applyActionConstant(elem);
                assignmentStatement& a = *(new assignmentStatement());
                a.src = stmtLoc;
                a.variableLeft = lhs;
                a.assignedExpression = elem;
                a.emitterBody = opBody;
                a.emitterParam = opFunc->params[0]->name;
                a.emitterSelf = self;
                if(body != nullptr) body->statements.push_back(&a);
                if(elem != nullptr && elem->terminator == token::braceClose) break;
                et = file.getToken();
            }
            file.getToken(token::endStatement);
            for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
            postInjections.clear();
            return true;
        }
    }
    return false;
}

// `lhs op= rhs;` — compound assignment (+=, -=, *=, /=, %=, |=, &=, ^=, <<=, >>=).
bool bglParser::processCompoundAssignment(token tok, token symbol, StatementContext& sc){
    const sourceLocation& stmtLoc = sc.src;
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    if(func != nullptr && tok.value.find('.') != string::npos){
        // A path that starts at a bare member (`label.price.cents += 1;`) starts at self.
        string head = tok.value.substr(0, tok.value.find('.'));
        string hint = tok.value.substr(head.size() + 1, tok.value.find('.', head.size() + 1) - head.size() - 1);
        if(head.find_first_of("([") == string::npos && qualifyIdentifier(head, func, body, hint).rfind("self.", 0) == 0)
            tok.value = "self." + tok.value;
    }
    string lhs = func != nullptr ? qualifyIdentifier(tok.value, func, body) : tok.value;
    if(lhs.empty()) parsingError(format("Undeclared variable '{0}'", tok.value));
    // Honour a member's `as <i6name>` alias, as the plain-assignment and read paths do:
    // the member emits under the alias, so a compound write to its Beguile name targeted
    // a property that does not exist ("No such constant as score").
    if(func != nullptr){
        if(size_t cd2 = tok.value.rfind('.'); cd2 != string::npos){
            string recvPath = tok.value.substr(0, cd2);
            string mem      = tok.value.substr(cd2 + 1);
            string aliased  = memberI6Name(resolveIdentifierType(recvPath, func, body), mem);
            size_t ld = lhs.rfind('.');
            if(aliased != mem && ld != string::npos) lhs = lhs.substr(0, ld + 1) + aliased;
        }
    }
    if(isConstName(tok.value, func, body))
        parsingError(format("Cannot assign to const variable '{0}'", tok.value));
    if(string g = staticMemberGlobal(tok.value); !g.empty()) lhs = g;

    // World-model child placement at runtime: `container.children += { a, b }` moves each listed
    // object INTO the container. Only `+=` (add) — plain `=` reads as "replace all contents" and is
    // rejected below; `-=` has no well-defined target (move out to where?) so it's rejected too.
    if(tok.value.size() > 9 && tok.value.substr(tok.value.size() - 9) == ".children")
        return processChildrenPlacement(tok, symbol, sc);

    // Array braced-list compound: `arr += {a, b, c}` (and `-=`) applies the per-element
    // operator (append / removeValue) to EACH element in turn — the grammar/extend `+=`
    // list idiom, at runtime. Only for the append/remove ops on a word/byte array; every
    // other case falls through to the single-RHS operator dispatch below.
    // A type that declares no `operator op=` at all: `x op= v` is `x = x op (v)` (§9.7), parsed as that
    // assignment so the type's own `op` and `=` apply.
    {
        string lhsType0 = tok.value.find('.') == string::npos ? resolveIdentifierType(tok.value, func, body)
                                                              : resolvePathType(tok.value, func, body);
        classDef* cls0 = languageService.classOf(lhsType0);
        bool declaresOp = cls0 != nullptr && findMemberInHierarchy(cls0, [&](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            return fn != nullptr && fn->name == symbol.value;
        }) != nullptr;
        if(objectDef* od = languageService.findObjectType(lhsType0))   // an object's own operator
            for(typeMember* m : od->members) if(dynamic_cast<functionDef*>(m) && m->name == symbol.value) declaresOp = true;
        bool enumLhs = cls0 == nullptr && languageService.findEnum(lhsType0) != nullptr;
        if((cls0 != nullptr || enumLhs) && !declaresOp && func != nullptr && !file.hasPendingToken
           && !file.peekToken().is(token::braceOpen)){
            string rhsSrc = file.getRawTextToStatementEnd();
            string name = tok.originalValue.empty() ? tok.value : tok.originalValue;
            name = withReceiverCast(name, sc);
            string op = symbol.value.substr(0, symbol.value.size() - 1);
            file.openText(name + " = " + name + " " + op + " (" + rhsSrc + ");", stmtLoc.file, stmtLoc.line);
            try {
                processStatement(file.getToken(), *func);
            } catch(...) { file.close(); throw; }
            file.close();
            return false;
        }
    }
    if(processArrayBracedCompound(tok, symbol, lhs, sc)) return false;

    // `s += $"…"`: the type's operator taking an interpolated string, its output spliced in at `$v`.
    if(!file.hasPendingToken && file.peekToken(1).is("$") && file.peekToken(2).is(eTokenType::quote)){
        string lhsType = tok.value.find('.') == string::npos ? resolveIdentifierType(tok.value, func, body)
                                                             : resolvePathType(tok.value, func, body);
        classDef* lc = getDispatchClass(lhsType);
        auto* op = lc == nullptr ? nullptr : dynamic_cast<functionDef*>(findMemberInHierarchy(lc, [&](typeMember* m){
            auto* f = dynamic_cast<functionDef*>(m);
            return f && f->name == symbol.value && f->isEmitter && f->params.size() == 1
                && f->params[0]->type.name == "interpolatedstringliteral" && dynamic_cast<i6Block*>(f->body) != nullptr; }));
        if(op == nullptr)
            parsingError(format("'{0}' has no `operator {1}` that takes an interpolated string.", typeDisplayName(lhsType), symbol.value));
        file.getToken();   // '$'
        assignmentStatement& a = *(new assignmentStatement());
        a.src = stmtLoc;
        a.variableLeft = lhs;
        a.interpSegments = parseInterpolatedSegments(func, body);
        endInterpolatedStatement();
        a.emitterBody = dynamic_cast<i6Block*>(op->body)->i6Body;
        a.emitterParam = op->params[0]->name;
        a.assignedExpression = new expression();
        a.assignedExpression->resolvedType = "interpolatedstringliteral";
        // A `?.` guard on the path opens before the append and closes after it.
        for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
        pendingInjections.clear();
        if(body != nullptr) body->statements.push_back(&a);
        for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
        postInjections.clear();
        return false;
    }

    expression* rhs = parseExpression(file.getToken(), {token::endStatement}, func, body);
    if(tok.value.find('.') != string::npos || languageService.findGlobalAs<arrayDeclaration>(tok.value) != nullptr)
        rejectEscapingLambda(rhs, format("stored in '{0}'", tok.value));
    else rejectLoopStoredLambda(rhs, tok.value);
    // Set-up the operand needs (a `?.` test, a temp) runs before the assignment that reads it.
    for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
    pendingInjections.clear();
    if(rhs != nullptr && rhs->resolvedType == "verb") applyActionConstant(rhs);   // `watched += Take` → ##Take
    if(symbol.value == "+=")
        if(auto* arr = dynamic_cast<arrayDeclaration*>(findAssignedDeclaration(tok.value, func, body)))
            if(arr->literalElements) checkLiteralElements(*arr, {rhs});

    // Try emitter lookup for this compound operator on the LHS type. A dotted path names
    // a member, which resolveIdentifierType does not resolve — so `shelf.items += x` found
    // no type, no operator, and fell through to a NUMERIC compound assignment
    // (`shelf.items = shelf.items + x`) rather than the array's `+=`.
    string lhsTypeName = tok.value.find('.') == string::npos
                       ? resolveIdentifierType(tok.value, func, body)
                       : resolvePathType(tok.value, func, body);
    classDef* lhsClass = languageService.classOf(lhsTypeName);
    if(lhsClass == nullptr && isWordArrayType(lhsTypeName)) lhsClass = getDispatchClass(lhsTypeName);   // `array<int>` parameter
    // A char widens to int (§12.4): `total += bytes[i]`.
    if(rhs != nullptr && (rhs->resolvedType == "char" || rhs->resolvedType == "charliteral")
       && (lhsTypeName == "int" || lhsTypeName == "uint")) rhs->resolvedType = "int";
    bool emitterFound = false;
    // A value whose type is known only at run time (`i.p` through a `property` parameter) is a `var`.
    if(lhsClass != nullptr && rhs != nullptr && rhs->resolvedType.empty() && !rhs->tokens.empty()) rhs->resolvedType = "var";
    // `list += level`: an element is stored as the element type, so an accessor gives its value, as an
    // argument to `append` does.
    if(rhs != nullptr && isWordArrayType(lhsTypeName) && !rhs->resolvedType.empty()){
        size_t dot = tok.value.find('.');
        string elem = dot == string::npos ? resolveArrayElementType(tok.value, func, body)
                                          : resolveArrayElementTypeDotted(tok.value.substr(0, dot), tok.value.substr(dot + 1), func, body);
        if(!elem.empty() && elem != rhs->resolvedType && elem != "var") applyImplicitConversion(rhs, elem);
    }
    if(lhsClass != nullptr && rhs != nullptr && !rhs->resolvedType.empty()){
        string rhsType = rhs->resolvedType;
        // An object declared with its own operators is searched before its class.
        objectDef* lhsObj = languageService.findObjectType(lhsTypeName);
        auto findOwnOp = [&](const function<bool(typeMember*)>& pred) -> typeMember* {
            if(lhsObj != nullptr) for(typeMember* om : lhsObj->members) if(pred(om)) return om;
            return findMemberInHierarchy(lhsClass, pred);
        };
        // A declared object is an exact match for its class (§6.4).
        string rhsObjClass = objectsClassName(rhsType);
        auto rhsExact = [&](const string& pt){
            if(pt == rhsType || (!rhsObjClass.empty() && pt == rhsObjClass)) return true;
            classDef* ac = languageService.classOf(rhsType), *pc = languageService.findClass(pt);
            return ac != nullptr && pc != nullptr && ac->hasAncestor(pc);
        };
        // Two-pass: exact type match first, then var wildcard — so specific overloads always beat the catch-all
        typeMember* m = findOwnOp([&](typeMember* m){
            auto* opFunc = dynamic_cast<functionDef*>(m);
            return opFunc && opFunc->name==symbol.value && opFunc->isEmitter
                   && opFunc->params.size()==1 && rhsExact(opFunc->params[0]->type.name)
                   && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
        });
        // Conversion fallback: check if RHS type converts to a type the operator accepts
        if(!m){
            classDef* rhsCls = languageService.classOf(rhsType);
            if(rhsCls != nullptr)
                for(typeMember* rm : rhsCls->members){
                    auto* convFn = dynamic_cast<functionDef*>(rm);
                    if(!convFn || convFn->name != "operator()" || !convFn->params.empty() || convFn->isExplicit) continue;
                    string convertedType = convFn->returnType.name;
                    m = findOwnOp([&](typeMember* m2){
                        auto* opFunc = dynamic_cast<functionDef*>(m2);
                        return opFunc && opFunc->name==symbol.value && opFunc->isEmitter
                               && opFunc->params.size()==1 && opFunc->params[0]->type.name==convertedType
                               && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
                    });
                    if(m){
                        // A conversion that is a routine runs; an emitter one is the value as it stands.
                        if(!convFn->isEmitter){ rhs->tokens = {applyCastConversion(rhs->text(), rhsType, convertedType)}; rhs->resolvedType = convertedType; }
                        break;
                    }
                }
        }
        // var wildcard fallback
        if(!m) m = findOwnOp([&](typeMember* m){
            auto* opFunc = dynamic_cast<functionDef*>(m);
            return opFunc && opFunc->name==symbol.value && opFunc->isEmitter
                   && opFunc->params.size()==1 && opFunc->params[0]->type.name=="var"
                   && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
        });
        // A `var` operand skips type checking: it takes the operator the left side has.
        if(!m && rhsType == "var") m = findOwnOp([&](typeMember* m){
            auto* opFunc = dynamic_cast<functionDef*>(m);
            return opFunc && opFunc->name==symbol.value && opFunc->isEmitter && opFunc->params.size()==1
                   && dynamic_cast<i6Block*>(opFunc->body)!=nullptr;
        });
        if(m){
            auto* opFunc = dynamic_cast<functionDef*>(m);
            auto* blk = dynamic_cast<i6Block*>(opFunc->body);
            if(lhsTypeName == "bytearray" && blk != nullptr && blk->i6Body.find("_bglArray.") != string::npos)
                parsingError(format("'{0}' from <array> works on word arrays; an array<char> doesn't take it "
                                    "(its operations are <buf>'s).", symbol.value));
            if(opFunc->params[0]->type.name == "float" && rhs != nullptr && rhs->resolvedType != "float" && rhs->resolvedType != "var"){
                rhs->tokens = {applyCastConversion(rhs->text(), rhs->resolvedType, "float")}; rhs->resolvedType = "float";
            }
            assignmentStatement& a = *(new assignmentStatement());
            a.src = stmtLoc;
            a.variableLeft = lhs;
            a.assignedExpression = rhs;
            // $self/$prop: a member array is addressed as (owner, property); a bare
            // global or local array uses the 0 sentinel, the same pair the method-call
            // path computes. A BARE IDENTIFIER is not proof of the non-member case —
            // inside an object's own method `nums += x` names a member — and assuming
            // so emitted `_bglArray.append(self.nums, 0, …)`, passing a multi-word
            // property where a pointer belongs. I6 then refuses to read it with `.`.
            string cOwner, cProp;
            bool cIsMember = lhs.find('.') != string::npos && splitQualifiedMember(lhs, func, body, cOwner, cProp);   // lhs is qualified already
            string selfText = lhs;
            if(cIsMember && memberArrayIsRef(cOwner, cProp, func, body)){
                cIsMember = false;
                selfText = objectI6Name(cOwner) + "." + propertyI6Name(cProp);   // the pointer the member holds
            }
            emitterBindings pb;   // $self stays deferred to emit time; only $prop/$opref resolve here
            if(isWordArrayType(lhsTypeName) || lhsTypeName == "bytearray"){
                pb.prop = cIsMember ? cProp : "0";
                pb.elemType = resolveArrayElementType(lhs, func, body);
                if(pb.elemType->empty() && cIsMember) pb.elemType = resolveArrayElementTypeDotted(cOwner, cProp, func, body);
                // `+=`/`-=` take one element; another array is one only for an array of arrays.
                if(rhs != nullptr && isWordArrayType(rhs->resolvedType) && !pb.elemType->empty()
                   && *pb.elemType != "var" && !isWordArrayType(*pb.elemType))
                    parsingError(format("'{0}' on an array takes one element ({1}); '{2}' is an array. Use a "
                                        "loop to {3} each element.", symbol.value, typeDisplayName(*pb.elemType),
                                        rhs->text(), symbol.value == "+=" ? "append" : "remove"));
            }
            a.emitterBody = expandEmitterBody(blk, pb);
            a.emitterParam = opFunc->params[0]->name;
            // An array is addressed as (owner, property); any other member type is the member itself.
            bool arrayTarget = isWordArrayType(lhsTypeName) || lhsTypeName == "bytearray";
            a.emitterSelf = cIsMember && arrayTarget ? cOwner : selfText;
            if(body != nullptr) body->statements.push_back(&a);
            emitterFound = true;
        }
    }
    // A regular-method `operator op=`: called on the left-hand value — an object's own first.
    if(!emitterFound && lhsClass != nullptr && rhs != nullptr){
        string rhsType = rhs->resolvedType;
        // The overload taking the right-hand side's own type (a literal's base type) first, then any it converts to.
        bool exactOnly = true;
        auto fits = [&](typeMember* m){
            auto* fn = dynamic_cast<functionDef*>(m);
            if(fn == nullptr || fn->name != symbol.value || fn->isEmitter || fn->params.size() != 1) return false;
            const string& pt = fn->params[0]->type.name;
            string oc = objectsClassName(rhsType);
            if(pt == rhsType || literalBaseType(rhsType) == pt || (!oc.empty() && pt == oc)) return true;
            // An instance of a class derived from the parameter's fits it before any conversion does.
            if(classDef* ac = languageService.classOf(rhsType), *pc = languageService.findClass(pt); ac && pc && ac->hasAncestor(pc)) return true;
            return !exactOnly && isTypeCompatible(rhsType, pt);
        };
        typeMember* m = nullptr;
        for(bool exact : {true, false}){
            exactOnly = exact;
            if(objectDef* od = languageService.findObjectType(lhsTypeName))
                for(typeMember* om : od->members) if(fits(om)){ m = om; break; }
            if(m == nullptr) m = findMemberInHierarchy(lhsClass, fits);
            if(m != nullptr) break;
        }
        if(auto* opFunc = dynamic_cast<functionDef*>(m)){
            mangleOverloadSetForReceiver(lhsTypeName, symbol.value);
            if(opFunc->i6name.empty()) opFunc->i6name = mangleOperatorName(opFunc->name);
            if(opFunc->params[0]->type.name == "float" && rhs != nullptr && rhs->resolvedType != "float" && rhs->resolvedType != "var"){
                rhs->tokens = {applyCastConversion(rhs->text(), rhs->resolvedType, "float")}; rhs->resolvedType = "float";
            }
            // A value its parameter doesn't take as is (`+= aReading` for an `int`) goes through its conversion.
            else if(rhs != nullptr && opFunc->params[0]->type.name != rhs->resolvedType)
                convertToParam(rhs, rhs->resolvedType, opFunc->params[0]->type.name);
            i6RawNode& node = *(new i6RawNode());
            node.text = lhs + "." + opFunc->i6name + "(" + rhs->text() + ");";
            node.cooked = true;   // built from Beguile expressions: locals take their renames
            node.src = stmtLoc;
            if(body != nullptr) body->statements.push_back(&node);
            emitterFound = true;
        }
    }
    if(!emitterFound){
        if(!lhsTypeName.empty() && lhsTypeName != "var"){
            bool hasOp = lhsClass != nullptr && findMemberInHierarchy(lhsClass, [&](typeMember* m){
                return dynamic_cast<functionDef*>(m) != nullptr && m->name == symbol.value; }) != nullptr;
            if(hasOp && rhs != nullptr)
                parsingError(format("No operator '{0}' on type '{1}' accepting '{2}'",
                                    symbol.value, typeDisplayName(lhsTypeName), typeDisplayName(rhs->resolvedType)));
            parsingError(format("No operator '{0}' defined on type '{1}'", symbol.value, typeDisplayName(lhsTypeName)));
        }
        // No emitter and untyped: expand to I6 form: x op= y  →  x = x op y;
        string op = symbol.value.substr(0, symbol.value.size() - 1); // strip trailing '='
        string rhsText = rhs != nullptr ? rhs->text() : "";
        i6RawNode& node = *(new i6RawNode());
        node.text = lhs + " = " + lhs + " " + op + " " + rhsText + ";";
        node.src = stmtLoc;
        if(body != nullptr) body->statements.push_back(&node);
    }
    for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
    postInjections.clear();
    return false;
}

// `x++;` / `x--;` — postfix increment or decrement as a whole statement.
bool bglParser::processPostfixIncDec(token tok, token symbol, StatementContext& sc){
    const sourceLocation& stmtLoc = sc.src;
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    file.getToken(token::endStatement);
    string lhs = func != nullptr ? qualifyIdentifier(tok.value, func, body) : tok.value;
    if(lhs.empty()) parsingError(format("Undeclared variable '{0}'", tok.value));
    lhs = withMemberI6Name(tok.value, lhs, func, body);
    if(string g = staticMemberGlobal(tok.value); !g.empty()) lhs = g;
    if(isConstName(tok.value, func, body))
        parsingError(format("Cannot assign to const variable '{0}'", tok.value));
    // A `?.` on the path opened its guard as set-up; the step goes inside it.
    for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
    pendingInjections.clear();
    emitIncDecStatement(tok, lhs, {symbol.value}, sc);
    for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
    postInjections.clear();
    return false;
}

// `((T)obj).member` again, for a rewrite of a statement whose receiver carried a cast: the rewrite
// re-parses the path, and without the cast it would read the member through obj's own type.
string bglParser::withReceiverCast(const string& path, const StatementContext& sc){
    size_t dot = path.find('.');
    if(sc.castType.empty() || dot == string::npos) return path;
    return "((" + sc.castType + ")" + path.substr(0, dot) + ")" + path.substr(dot);
}

// `x++;` / `++x;` and the `--` forms on `lhs` (x's emitted text), by the first of `ops` x's type
// declares: an emitter inline, a regular method through its routine. A class declaring none (an
// accessor) takes `x = x + 1` (§9.7), so its own `+` and `=` run.
void bglParser::emitIncDecStatement(token var, const string& lhs, const vector<string>& ops, StatementContext& sc){
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    string lhsTypeName = var.value.find('.') == string::npos ? resolveIdentifierType(var.value, func, body)
                                                             : resolvePathType(var.value, func, body);
    classDef* lhsClass = languageService.classOf(lhsTypeName);
    auto push = [&](const string& text){
        i6RawNode& node = *(new i6RawNode());
        node.text = text;
        node.src = sc.src;
        node.cooked = true;   // names locals: renamed like any expression
        if(body != nullptr) body->statements.push_back(&node);
    };
    objectDef* lhsObj = languageService.findObjectType(lhsTypeName);
    if(lhsClass != nullptr)
        for(const string& opName : ops){
            auto fits = [&](typeMember* tm){
                auto* fn = dynamic_cast<functionDef*>(tm);
                return fn && fn->name == opName && fn->params.empty()
                    && (!fn->isEmitter || dynamic_cast<i6Block*>(fn->body) != nullptr);
            };
            typeMember* m = nullptr;
            if(lhsObj != nullptr) for(typeMember* om : lhsObj->members) if(fits(om)){ m = om; break; }
            if(m == nullptr) m = findMemberInHierarchy(lhsClass, fits);
            auto* opFunc = dynamic_cast<functionDef*>(m);
            if(opFunc == nullptr) continue;
            if(opFunc->isEmitter){
                emitterBindings ob; ob.self = lhs; ob.val = lhs; ob.trim = emitterTrim::wsSemi;
                push(expandEmitterBody(dynamic_cast<i6Block*>(opFunc->body), ob) + ";");
            } else {
                if(opFunc->i6name.empty()) opFunc->i6name = mangleOperatorName(opFunc->name);
                push(lhs + "." + opFunc->i6name + "();");
            }
            return;
        }
    string step = ops.back();
    // A primitive class takes the fallback through its own `+` / `-`.
    bool hasArith = lhsClass != nullptr && findMemberInHierarchy(lhsClass, [&](typeMember* tm){
        return dynamic_cast<functionDef*>(tm) != nullptr && tm->name == step.substr(0, 1); }) != nullptr;
    if(lhsClass != nullptr && (!lhsClass->isPrimitive || hasArith) && func != nullptr){
        // originalValue spells only the path's first name; the path itself is in value.
        string name = withReceiverCast(var.value, sc);
        file.openText(name + " = " + name + " " + step.substr(0, 1) + " 1;", sc.src.file, sc.src.line);
        try { processStatement(file.getToken(), *func); } catch(...) { file.close(); throw; }
        file.close();
        return;
    }
    if(!lhsTypeName.empty() && lhsTypeName != "var")
        parsingError(format("No operator '{0}' defined on type '{1}'", step, typeDisplayName(lhsTypeName)));
    push(ops.size() > 1 ? step + lhs + ";" : lhs + step + ";");
}

// Computes the called name of a call statement: the `replaced()` rewrite, then qualification of
// a bare name (an instance method becomes `self.name`).
bool bglParser::isUsingImportedValueEmitter(const string& name){
    auto isValueEmitter = [&](typeMember* m){
        auto* fd = dynamic_cast<functionDef*>(m);
        return fd != nullptr && fd->name == name && fd->isEmitter && fd->isValueEmitter;
    };
    for(classDef* imp : usingImports)
        for(typeMember* m : imp->members) if(isValueEmitter(m)) return true;
    for(objectDef* imp : usingObjectImports)
        for(typeMember* m : imp->members) if(isValueEmitter(m)) return true;
    return false;
}

string bglParser::qualifyCallName(token tok, StatementContext& sc){
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    string rawName = (string)tok;
    // replace chaining: replaced() resolves to the predecessor's mangled name
    functionDef* replacing = replaceOwner();   // `replaced()` in a lambda belongs to the routine around it
    if(rawName == "replaced" && replacing && replacing->replaceFoundNothing)
        parsingError(format("replaced() has nothing to call: no '{0}' is declared before this replace. A replace "
            "follows what it replaces; move it below '{0}' (or below the include that declares it).", replacing->dName()));
    if(rawName == "replaced" && replacing && !replacing->replacedTarget.empty()){
        rawName = replacing->replacedTarget;
        replacing->replacedWasCalled = true;
    }
    // `::print(x)` reaches the global overload set, which the call's arguments choose from.
    if(rawName.rfind("::", 0) == 0){
        string base = rawName.substr(2);
        int overloads = 0;
        for(typeDef* g : languageService.globals)
            if(auto* fd = dynamic_cast<functionDef*>(g); fd && fd->name == base) overloads++;
        if(overloads > 1) return base;
    }
    if(func != nullptr && rawName.find('.') == string::npos){
        if(languageService.findGlobalAs<functionDef>(rawName) && isUsingImportedValueEmitter(rawName)) return rawName;
        // A global overload set is bound by its arguments (bindGlobalCall), not by name: once
        // mangled, its overloads no longer collapse to one qualification. Unless something
        // nearer shadows the name, leave it for the call binding.
        {
            int overloads = 0;
            for(typeDef* g : languageService.globals)
                if(auto* fd = dynamic_cast<functionDef*>(g); fd && fd->name == rawName) overloads++;
            if(overloads > 1 && !qualifyFromParams(rawName, func) && !qualifyFromBodyLocals(rawName, body)
               && !qualifyFromAncestorBlocks(rawName, body) && !qualifyFromCurrentObject(rawName)
               && !qualifyFromCurrentClass(rawName) && !qualifyFromCaptures(rawName))
                return rawName;
        }
        // A method imported by `#using` is called through its object, unless a routine of the name, or
        // something nearer, is in scope: a verb or variable named like it can't be called.
        if(languageService.findGlobalAs<functionDef>(rawName) == nullptr && !qualifyFromParams(rawName, func)
           && !qualifyFromBodyLocals(rawName, body) && !qualifyFromAncestorBlocks(rawName, body)
           && !qualifyFromCurrentObject(rawName) && !qualifyFromCurrentClass(rawName))
            for(objectDef* imp : usingObjectImports)
                for(typeMember* m : imp->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->name == rawName && !fd->isValueEmitter){
                        // A local named like the imported object doesn't take the call: the object is file scope's.
                        bool shadowed = qualifyFromParams(imp->name, func) || qualifyFromBodyLocals(imp->name, body)
                                     || qualifyFromAncestorBlocks(imp->name, body);
                        return (shadowed ? "::" : "") + imp->name + "." + rawName;
                    }
        string qualified = qualifyIdentifier(rawName, func, body);
        // A method of this object or class is called by its Beguile name: the arguments choose among its overloads.
        if(qualified.rfind("self.", 0) == 0 && strcasecmp(qualified.c_str() + 5, rawName.c_str()) != 0){
            bool isMethod = false;
            if(currentObject != nullptr)
                for(typeMember* m : currentObject->members)
                    if(dynamic_cast<functionDef*>(m) != nullptr && m->name == rawName){ isMethod = true; break; }
            if(isMethod) qualified = "self." + rawName;
        }
        // A global function renamed for I6 (`_Swap`) is still called by its Beguile name; binding picks the routine.
        if(auto* gf = languageService.findGlobalAs<functionDef>(rawName); gf != nullptr && !gf->i6name.empty()
           && strcasecmp(qualified.c_str(), gf->i6name.c_str()) == 0)
            qualified = rawName;
        // A member that can't take this call's arguments gives way to a global function of its name.
        // The call's '(' has been read.
        if(qualified == "self." + rawName){
            classDef* sc2 = currentClass != nullptr ? currentClass
                          : currentObject != nullptr ? currentObject->objectClass : nullptr;
            functionDef* member = currentFunc != nullptr && currentFunc->name == rawName ? currentFunc : nullptr;
            if(member == nullptr && currentObject != nullptr)
                for(typeMember* m : currentObject->members)
                    if(auto* fd = dynamic_cast<functionDef*>(m); fd && fd->name == rawName){ member = fd; break; }
            if(member == nullptr && sc2 != nullptr)
                member = dynamic_cast<functionDef*>(sc2->findMember([&](typeMember* m){
                    auto* fd = dynamic_cast<functionDef*>(m); return fd != nullptr && fd->name == rawName; }));
            if(member != nullptr && !memberTakesCall(member, rawName, true)) return rawName;
        }
        // qualifyIdentifier walks inherited VARIABLES but not functions.
        // For call-form resolution, also check the class hierarchy for inherited methods.
        // Skip if the name also exists as a global function (global arity matching wins).
        classDef* selfClass = currentClass != nullptr ? currentClass
                            : currentObject != nullptr ? currentObject->objectClass : nullptr;
        if((qualified.empty() || qualified == rawName) && selfClass != nullptr){
            bool isGlobalFunc = false;
            if(auto* fd = languageService.findGlobalAs<functionDef>(rawName)) isGlobalFunc = true;
            if(!isGlobalFunc){
                if(selfClass->findMember([&](typeMember* m){
                       auto* fd = dynamic_cast<functionDef*>(m);
                       return fd != nullptr && fd->name == rawName;
                   })) qualified = "self." + rawName;
            }
        }
        return qualified.empty() ? rawName : qualified;
    } else {
        return rawName;
    }
}

// Parses a call statement's argument list, with brace-argument hints taken from the callee.
void bglParser::parseCallArgsWithHints(functionCallStatement& callStmt, StatementContext& sc){
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    BraceArgHints braceHints;
    size_t dp = callStmt.functionName.rfind('.');
    if(dp == string::npos){
        braceHints = braceArgHints(collectGlobalCandidates(callStmt.functionName));
    } else {
        string objectPath = callStmt.functionName.substr(0, dp);
        string methodName = callStmt.functionName.substr(dp + 1);
        // A dotted receiver (`bgl.ui.statusBar`) needs resolvePathType; resolveIdentifierType sees
        // only a single name.
        string recvType = (objectPath == "self")
            ? (currentObject ? currentObject->name : (currentClass ? currentClass->name : string()))
            : objectPath.find('.') != string::npos ? resolvePathType(objectPath, func, body, methodName)
                                                  : resolveIdentifierType(objectPath, func, body);
        vector<functionDef*> candidates;
        if(!recvType.empty()) candidates = collectMethodCandidates(recvType, methodName);
        if(candidates.empty()){
            string recvObj = qualifyIdentifier(objectPath, func, body);   // `bgl.ui.statusBar` → `_bglstatus`
            if(languageService.findObjectType(recvObj) != nullptr) candidates = collectMethodCandidates(recvObj, methodName);
        }
        string hintElem;
        if(isWordArrayType(recvType))
            hintElem = objectPath.find('.') == string::npos ? resolveArrayElementType(objectPath, func, body)
                     : resolveArrayElementTypeDotted(objectPath.substr(0, objectPath.rfind('.')), objectPath.substr(objectPath.rfind('.') + 1), func, body);
        braceHints = braceArgHints(candidates, hintElem);
    }
    ParsedArgList pal = parseCallArgList(func, body, braceHints);
    callStmt.args = pal.args;
    callStmt.namedArgNames = pal.namedArgNames;
    callStmt.interpSegmentsPerArg = pal.interpSegmentsPerArg;
}

// `recv.method(args);` — binds a method call statement (receiver type, `hide` enforcement,
// emitter substitution). Returns true when it emitted the statement itself.
bool bglParser::bindMethodCallStatement(functionCallStatement& callStmt, token tok, string& chainReturnType, StatementContext& sc){
    const size_t dotPos = callStmt.functionName.rfind('.');
    const sourceLocation& stmtLoc = sc.src;
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    string& stmtCastType = sc.castType;
    const string& literalTypeName = sc.literalTypeName;
    const string& literalSelfText = sc.literalSelfText;
    // method call: validate and resolve emitter
    string objectPath = callStmt.functionName.substr(0, dotPos);  // may be "obj" or "obj.prop"
    string methodName = callStmt.functionName.substr(dotPos + 1);
    // Resolve a namespace auto-member receiver (bgl.ui → _bglUi, bgl.util.math → _bglMath)
    // to the backing object so emission is a message-send, not a literal runtime property
    // chain (which fails to set `self`). Only rewrite when the receiver collapses to a
    // single global object — leaving locals, class-typed paths, and emitter namespaces
    // (bgl.asm) untouched. Mirrors the expression-context walk.
    {
        string q = qualifyIdentifier(objectPath, func, body);
        if(q != objectPath && q.find('.') == string::npos && q.find('(') == string::npos
           && languageService.findObjectType(q))
            objectPath = q;
    }
    // Emission path: same as objectPath but with the HEAD alias-resolved to its I6 name
    // (e.g. `orLibUtil` → `util`), for a multi-hop receiver that doesn't collapse to a single
    // object above. objectPath itself stays the Beguile path so resolvePathType still works;
    // emitObjectPath drives $self/$val/functionName so emission uses the I6 name. Mirrors the
    // expression walk, which emits `util.orlooparray.getNext(...)`.
    string emitObjectPath = objectPath;
    if(objectPath.find('.') != string::npos){
        size_t hd = objectPath.find('.');
        string head = objectPath.substr(0, hd);
        string qh = qualifyIdentifier(head, func, body);
        if(!qh.empty() && qh != head && qh.find('(') == string::npos && qh.find('.') == string::npos)
            emitObjectPath = qh + objectPath.substr(hd);
        // The members along the path are sent under their I6 names (`panel._m_main.push()`).
        if(func != nullptr) emitObjectPath = withMemberI6Name(objectPath, emitObjectPath, func, body);
        // `Crate.journal.print()`: a static member is its global.
        if(string g = staticMemberGlobal(objectPath); !g.empty()) emitObjectPath = g;
        // A path ending at a value emitter (`crate.parent`) is the emitter's value.
        else if(func != nullptr){
            size_t last = objectPath.rfind('.'), elast = emitObjectPath.rfind('.');
            string owner = objectPath.substr(0, last), member = objectPath.substr(last + 1);
            string ownerType = owner.find('.') == string::npos ? resolveIdentifierType(owner, func, body) : resolvePathType(owner, func, body);
            string rt;
            if(!ownerType.empty() && elast != string::npos){
                string ownerText = emitObjectPath.substr(0, elast);
                string v = expandMemberValueEmitter(ownerText, owner, ownerType, member, func, body, rt);
                // A property-class member (`parent`) read by its class's emitter.
                if(v.empty()) v = applyPropertyClassRead(ownerText, resolvePathType(objectPath, func, body), rt);
                if(!v.empty()) emitObjectPath = "(" + v + ")";
            }
        }
    } else {
        // Single-hop receiver (`beacon.flash()`). The namespace rewrite above only fires when the
        // qualified name is itself a registered object, which an `as` alias never is — the registry
        // keys on the Beguile name. So `object beacon as lamp` emitted a dangling `beacon.flash()`
        // while its property accesses, which go through qualifyIdentifier, emitted `lamp.wattage`.
        // Going through qualifyIdentifier here keeps local/parameter precedence: a local named
        // `beacon` resolves to itself and nothing is rewritten.
        string q = qualifyIdentifier(objectPath, func, body, methodName);
        // A `#using`-imported member qualifies to its path (`kit.counters`), which is sent as it is;
        // a member of this object or class is sent through self.
        if(!q.empty() && q != objectPath && q.find('(') == string::npos)
            emitObjectPath = q;
    }
    string objectName = objectPath;  // kept for backward compat in non-emitter emit path
    // Pass memberHint=methodName so the resolver disambiguates a name collision in favor
    // of whichever candidate's type actually exposes the method.
    // The receiver's ACTUAL static type, independent of any cast — needed to decide whether
    // an explicit cast is a genuine upcast that should trigger ancestor-qualified dispatch.
    string actualPathType = !literalTypeName.empty() ? literalTypeName
                          : resolvePathType(objectPath, func, body, methodName);
    string objectType = !stmtCastType.empty() ? stmtCastType : actualPathType;
    // A member typed by a bare type alias (an import's, written before the alias's object was imported) is that type.
    if(!objectType.empty() && getDispatchClass(objectType) == nullptr && languageService.findObjectType(objectType) == nullptr)
        if(string t = typeAliasTarget(objectType); !t.empty()) objectType = t;
    // Ancestor-qualified method dispatch (statement form): `(Base)obj.method(args);` emits I6
    // `obj.Base::method(args)`, forcing static dispatch to Base's version — the super-call /
    // ancestor-version idiom. Only when Base is a strict ancestor of the receiver's actual
    // type (a real upcast); identity/downcast/base-typed-local stay dynamic. Consumed at the
    // non-emitter emission below; emitter methods (inlined) are rejected.
    classDef* ancestorDispatchClass = nullptr;
    if(!stmtCastType.empty())
        if(isUpcast(stmtCastType, actualPathType))
            ancestorDispatchClass = getDispatchClass(stmtCastType);
    stmtCastType = "";  // consume the cast
    if(objectType.empty() && objectPath.find('.') != string::npos){
        // `eTone.alarm.show()`: an enum member written with its enum's name.
        string head = objectPath.substr(0, objectPath.find('.')), member = objectPath.substr(objectPath.find('.') + 1);
        if(auto* ed = languageService.findEnum(head))
            for(enumValueDef* ev : ed->namedValues)
                if(ev->name == member){
                    objectType = head;
                    emitObjectPath = ed->isExternal ? member : to_string(ev->value);
                    break;
                }
    }
    if(objectType.empty())
        parsingError(format("Unknown variable '{0}'", objectPath));
    // The method picked a verb of that name (a member named like the verb has no such method): the
    // receiver is the verb, not the member.
    if(objectPath.find('.') == string::npos && emitObjectPath.rfind("self.", 0) == 0 && objectType != "verb")
        if(verbShadowedByMember(objectPath, objectType, methodName)){ objectType = "verb"; emitObjectPath = objectPath; }
    // Compute $self and $prop for emitter substitution.
    // For literals, $self is the raw literal text (e.g. "hello", 42, 'x'), not the path.
    size_t innerDot = objectPath.rfind('.');
    // $self defaults to the FULL receiver path — the object the method is invoked on
    // (e.g. `orLibUtil.orArray.set(...)` → $self = `orLibUtil.orArray`). Literals use their
    // raw text. Only the word-array member dual-dispatch (obj.prop) wants the pre-dot owner,
    // and it overrides selfValue below (isMemberArr). Previously this split off the last hop
    // unconditionally, which mis-set $self to the owner for namespace-sub-object receivers.
    string selfValue = (!literalSelfText.empty() && innerDot == string::npos)
                      ? literalSelfText
                      : emitObjectPath;
    string propValue = (innerDot == string::npos)
        ? ((isWordArrayType(objectType) || objectType == "bytearray") ? "0" : "<$prop undefined>")
        : objectPath.substr(innerDot + 1);
    // Member (property) WORD array: route through the dual-form _bglArray utility with
    // the owning object + property (matches the expression-context path). Override of
    // selfValue/propValue is applied just before emitter substitution (after recvElemType).
    string memOwner, memProp;
    bool isMemberArr = false;
    if(isWordArrayType(objectType)){
        // Member word array `obj.prop`: dual-dispatch wants $self = owner, $prop = prop.
        // (selfValue now defaults to the full path, so split the owner off explicitly.)
        if(innerDot != string::npos){ memOwner = objectPath.substr(0, innerDot); memProp = propValue; isMemberArr = true; }
        else isMemberArr = splitQualifiedMember(objectPath, func, body, memOwner, memProp);
        // A `ref` member holds a POINTER to an array owned elsewhere, so it is addressed
        // as a value — (obj.prop, 0) — not as inline property data. Checked against the
        // resolved owner/property rather than the path text, which varies by call site.
        if(isMemberArr && memberArrayIsRef(memOwner, memProp, func, body)){
            selfValue = objectI6Name(memOwner) + "." + propertyI6Name(memProp);   // the pointer the member holds
            propValue = "0";
            isMemberArr = false;
        }
        if(isMemberArr) rejectRawMemberLengthOp(memOwner, memProp, methodName, func, body);
    }
    // Receiver type can be a classDef OR an objectDef (each unclassed objectDef has its
    // own type identity); both have addressable methods.
    typeDef& objTd2 = languageService.getType(objectType);
    classDef* cls = dynamic_cast<classDef*>(&objTd2);
    bool opaqueReceiver = (cls == nullptr && dynamic_cast<objectDef*>(&objTd2) == nullptr);
    // Generic specialization fallback: templated receiver name (`array<int>` from
    // a parametric param) isn't a registered type, but its base ("array") is. Treat
    // as non-opaque if the base resolves to a class — bindMethodCall handles the
    // element-type binding for substitution.
    if(opaqueReceiver){
        auto lt = objectType.find('<');
        if(lt != string::npos && lt > 0){
            typeDef& baseTd = languageService.getType(objectType.substr(0, lt));
            if(dynamic_cast<classDef*>(&baseTd) != nullptr){
                opaqueReceiver = false;
                cls = dynamic_cast<classDef*>(&baseTd);
            }
        }
    }
    if(opaqueReceiver)   // an enum's emitter methods live on its companion class
        if(classDef* companion = getDispatchClass(objectType)){ cls = companion; opaqueReceiver = false; }
    if(opaqueReceiver)   // a verb object (its name also names its action)
        for(verbObjectDef* v : languageService.verbs)
            if(v->name == objectType){ cls = v->objectClass; opaqueReceiver = false; break; }
    if(opaqueReceiver && !looseIdentifierMode)
        parsingError(format("Type '{0}' is not a class or object", objectType));
    if(opaqueReceiver){
        // Loose mode: receiver is unknown to Beguile (typically an I6 symbol). Skip
        // method binding and emitter substitution; the call statement emits the
        // verbatim `path.method(args)`, which is valid I6. Carry original case via
        // the inherited displayName field (same convention as typeMember.dName()).
        chainReturnType = "var";
        callStmt.displayName = tok.originalValue;
    } else {
        // Element-type binding for generic receivers (array<T>, etc.). Resolve from the
        // bare receiver path (handles bare member + global); the member override below
        // then switches selfValue/propValue to owner/property for the utility dispatch.
        string recvElemType;
        {
            size_t ed = objectPath.find('.');
            if(ed == string::npos) recvElemType = resolveArrayElementType(objectPath, func, body);
            else recvElemType = resolveArrayElementTypeDotted(objectPath.substr(0, ed), objectPath.substr(ed + 1), func, body);
        }
        if(isMemberArr){ selfValue = objectI6Name(memOwner); propValue = memProp; }
        // Computed message send as a STATEMENT — `obj.m();` where `m` holds a property.
        // The expression path handles the value form; this is the discard-result form,
        // which reaches bindMethodCall and would be rejected as an unknown method.
        {
            bool realMember = false;
            if(classDef* rc = getDispatchClass(objectType))
                realMember = findMemberInHierarchy(rc, [&](typeMember* m){ return m->name == methodName; }) != nullptr;
            if(!realMember)
                if(auto* od = languageService.findObjectType(objectType))
                    for(typeMember* m : od->members)
                        if(m->name == methodName){ realMember = true; break; }
            if(!realMember && isPropertyValuedLocal(methodName, func, body)){
                string argText;
                for(size_t i = 0; i < callStmt.args.size(); i++)
                    argText += (i ? ", " : "") + callStmt.args[i]->text();
                i6RawNode& raw = *(new i6RawNode());
                raw.src = stmtLoc;
                raw.text = emitObjectPath + ".(" + qualifyIdentifier(methodName, func, body)
                         + ")(" + argText + ");";
                if(body != nullptr) body->statements.push_back(&raw);
                return true;
            }
        }
        // `Policy.step(10);`: a static func<> member is a global holding a routine, called directly.
        if(classDef* sc = getDispatchClass(objectType))
            if(auto* sv = dynamic_cast<variableDeclaration*>(findMemberInHierarchy(sc, [&](typeMember* m){
                   auto* vd = dynamic_cast<variableDeclaration*>(m);
                   return vd != nullptr && vd->isStatic && vd->name == methodName && vd->type.name.rfind("func", 0) == 0; }))){
                string argText;
                for(size_t i = 0; i < callStmt.args.size(); i++) argText += (i ? ", " : "") + callStmt.args[i]->text();
                i6RawNode& raw = *(new i6RawNode());
                raw.src = stmtLoc;
                raw.text = "_bgl_" + sc->declaringClass(sv)->name + "_" + sv->name + "(" + argText + ");";
                raw.cooked = true;
                if(body != nullptr) body->statements.push_back(&raw);
                return true;
            }
        functionDef* method = bindMethodCall(objectType, objectPath, methodName,
                                               callStmt.args, callStmt.namedArgNames, callStmt.interpSegmentsPerArg,
                                               recvElemType);
        // `hide` enforcement (statement call): objectType is cast-aware, so a `(Base)obj.m()`
        // resolves against Base and reaches a hidden-on-the-subtype method — the door.
        {   vector<string> argTypeNames;
            for(expression* a : callStmt.args) argTypeNames.push_back(a->resolvedType);
            enforceHidden(getDispatchClass(objectType), methodName, "", argTypeNames, objectPath);
        }
        // A value emitter is NOT callable: `obj.bold` (value, §14.4.5) and `obj.bold()`
        // (zero-arg function) are distinct; parens on a value are an error here too.
        if(method->isEmitter && method->isValueEmitter)
            parsingError(format("'{0}' is an emitter value, not a function; use it without parentheses ('{0}', not '{0}()')", methodName));
        // Ancestor-qualified dispatch needs a real I6 routine property for `::` to select;
        // an emitter method is inlined at the call site, so there is no routine to qualify.
        if(ancestorDispatchClass != nullptr && method->isEmitter)
            parsingError(format("Ancestor-qualified dispatch '({0}){1}.{2}(...)' is not supported: '{2}' is an emitter method (inlined at the call site), so there is no routine for the ancestor cast to select. Ancestor dispatch works on regular (non-emitter) methods.",
                                ancestorDispatchClass->dName(), objectPath, methodName));
        cls = languageService.classOf(objectType);
        // Rebuild the call statement's functionName from the (possibly namespace-resolved)
        // objectPath so emission targets the backing object — e.g. `bgl.ui.pressAnyKey()`
        // becomes `_bglUi.pressAnyKey()` rather than a literal runtime chain. Overload sets
        // carry a mangled i6name (assigned by mangleOverloadSetForReceiver in bindMethodCall);
        // otherwise use the method name. Emitters are handled separately below (inlined body).
        if(!method->isEmitter && method->isStatic){
            // A static method has no receiver: it is the declaring class's free routine.
            classDef* stCls = getDispatchClass(objectType);
            if(stCls != nullptr)
                if(classDef* owner = stCls->declaringClass(method)) stCls = owner;
            callStmt.functionName = i6Emitter::staticRoutineName(stCls, method);
        } else if(!method->isEmitter){
            string callName = methodCallName(method, methodName);
            // Explicit ancestor cast → qualify the send with the ancestor's I6 class
            // (`obj.Base::method`) so I6 selects that class's routine statically.
            if(ancestorDispatchClass != nullptr)
                callName = ancestorDispatchClass->i6Name() + "::" + callName;
            callStmt.functionName = emitObjectPath + "." + callName;
        }
        // if emitter, pre-substitute $self, $prop, and $class
        if(method->isEmitter)
            if(auto* blk = dynamic_cast<i6Block*>(method->body)){
                emitterBindings mb;
                // $selfsub → `<self>sub` (the I6 action routine) for the verb class's
                // perform() bridge — e.g. `Take.perform()` → `TakeSub()`.
                mb.selfsub = selfValue + "sub";
                {   // A verb's action routine is named for its action, not for its object.
                    string low = selfValue; transform(low.begin(), low.end(), low.begin(), ::tolower);
                    for(verbObjectDef* v : languageService.verbs){
                        string vi6 = v->i6name; transform(vi6.begin(), vi6.end(), vi6.begin(), ::tolower);
                        if(v->name == low || (!vi6.empty() && vi6 == low)){ mb.selfsub = v->dName() + "sub"; break; }
                    }
                }
                mb.self    = selfValue;
                // $host — the object a proxy member is accessed on, as in the expression form.
                mb.host    = selfValue.substr(0, selfValue.rfind('.') == string::npos ? selfValue.size() : selfValue.rfind('.'));
                mb.selfType = objectType;   // $i6Expr needs the receiver's TYPE to parse its payload
                mb.val     = emitObjectPath;
                // $class — declared receiver type (ignores multiple inheritance).
                // Resolves to the variable's static type, not the type that owns the
                // inherited emitter. Powers class-message I6 emission from mixins.
                if(cls != nullptr) mb.cls = cls->i6Name();
                mb.prop = propValue;
                // One substitution covers every $opref(<op>) in the body.
                mb.elemType    = recvElemType.empty() ? objectType : recvElemType;
                mb.elemContext = methodName;
                for(expression* a : callStmt.args) mb.argTypes.push_back(a->resolvedType);   // $i6Expr binds var params to these
                // fn WITHOUT args: the body is staged for resolveEmitterText to substitute the
                // parameters at emit time, so the funnel must leave $val / $prop alone when a
                // parameter of that name will claim them (e.g. `orArray.set(…, var val)`).
                mb.fn = method;
                for(expression* a : callStmt.args) mb.argTypes.push_back(a->resolvedType);
                string b = expandEmitterBody(blk, mb);
                callStmt.emitterBody = b;
                for(paramDef* p : method->params)
                    callStmt.emitterParams.push_back(p->name);
                hoistOpcodeOperands(callStmt);
            }
        chainReturnType = method->returnType.name;
    }
    return false;
}

// An Inform 6 opcode (`@streamnum n;`) takes only a variable or a constant: an emitter body holding one
// gets each other argument through a temporary, assigned in argument order before the call.
void bglParser::hoistOpcodeOperands(functionCallStatement& cs){
    if(cs.emitterBody.find('@') == string::npos) return;
    static const std::regex simple(R"re(^(-?\d+|\$\$?[0-9A-Fa-f]+|'[^']*'|[A-Za-z_][A-Za-z0-9_]*)$)re");
    for(size_t i = 0; i < cs.args.size() && i < cs.emitterParams.size(); i++){
        expression* a = cs.args[i];
        if(a == nullptr || (i < cs.interpSegmentsPerArg.size() && !cs.interpSegmentsPerArg[i].empty())) continue;
        string text = a->text();
        if(text.empty() || std::regex_match(text, simple)) continue;
        static const std::regex constantArithmetic(R"re(^(\$[0-9A-Fa-f]+|[0-9+\-*/%() ])+$)re");   // Inform 6 folds it
        if(std::regex_match(text, constantArithmetic)) continue;
        string temp = newChainTemp();
        i6RawNode* inj = new i6RawNode();
        inj->text = temp + " = " + text + ";";
        inj->cooked = true;
        pendingInjections.push_back(inj);
        a->tokens.assign(1, temp);
    }
}

// `name(args);` — binds a global function call statement.
void bglParser::bindGlobalCallStatement(functionCallStatement& callStmt, token tok, string& chainReturnType, StatementContext& sc){
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    // global function call: bind (resolve + validate + finalize) then stage emitter body
    // `::name(…)` past a local of the name binds the global; the marker stays on the emitted name.
    string bindName = callStmt.functionName;
    bool escaped = bindName.rfind(kGlobalEscapeMarker, 0) == 0;
    if(escaped) bindName = bindName.substr(strlen(kGlobalEscapeMarker));
    GlobalCallBinding gcb = bindGlobalCall(bindName, callStmt.args,
                                            callStmt.namedArgNames, callStmt.interpSegmentsPerArg,
                                            escaped ? nullptr : func, escaped ? nullptr : body);
    if(!gcb.funcVarReturnType.empty())  chainReturnType = gcb.funcVarReturnType;
    else if(gcb.method != nullptr)      chainReturnType = gcb.method->returnType.name;
    else                                chainReturnType = "var"; // loose mode: unresolved → opaque
    // One member of an overload set is now resolved, so the statement calls it by the routine
    // name that overload emits under, not by the Beguile name the whole set shares.
    if(gcb.method != nullptr && !gcb.method->i6name.empty() && !gcb.method->isEmitter
       && callStmt.functionName == gcb.method->name){
        callStmt.functionName = gcb.method->i6name;
        callStmt.displayName.clear();
    }
    if(gcb.method && gcb.method->isEmitter)
        if(auto* blk = dynamic_cast<i6Block*>(gcb.method->body)){
            // fn WITHOUT args: the signature is all $i6Expr needs to parse its payload, while the
            // plain `$param` tokens stay for the deferred pass below — which is the only pass that
            // can produce final argument text (i6Emitter::exprText applies the per-routine
            // display-name, spill and rename maps).
            emitterBindings sb; sb.fn = gcb.method;
            for(expression* a : callStmt.args) sb.argTypes.push_back(a->resolvedType);
            callStmt.emitterBody = expandEmitterBody(blk, sb);
            for(paramDef* p : gcb.method->params) callStmt.emitterParams.push_back(p->name);
            hoistOpcodeOperands(callStmt);
        }
    // Loose-mode unresolved global call: carry original case via displayName so
    // the emitter can prefer it over the lowercased functionName.
    if(gcb.method == nullptr && gcb.funcVarReturnType.empty() && looseIdentifierMode)
        callStmt.displayName = tok.originalValue;
}

// `…().m1().m2();` — folds any chained `.method()` suffixes into the call statement, up to `;`.
bool bglParser::parseMethodChain(functionCallStatement& callStmt, string& chainReturnType, StatementContext& sc){
    functionDef* func = sc.func;
    statementBlock* body = sc.body;
    auto resolveEmitterText = [&](functionCallStatement& cs) -> string {
        string b = cs.emitterBody;
        for(size_t i=0; i<cs.emitterParams.size() && i<cs.args.size(); i++)
            b = replaceWord(b, cs.emitterParams[i], cs.args[i]->text());
        size_t s=b.find_first_not_of(" \t\n\r"); if(s!=string::npos) b=b.substr(s);
        size_t e=b.find_last_not_of(" \t\n\r;"); if(e!=string::npos) b=b.substr(0,e+1);
        return b;
    };
    token chainTok = file.getToken();
    while(chainTok.is(token::period) || chainTok.is(eTokenType::dictionaryWord)){
        // After ')' the lexer returns '.method' as a dictionaryWord; after an identifier it returns '.' + identifier separately.
        token chainMember;
        if(chainTok.is(token::period))
            // method name may collide with a type name → accept dataType too.
            chainMember = file.getToken({eTokenType::identifier, eTokenType::dataType});
        else
            chainMember = chainTok;  // dictionaryWord already holds the method name
        if(!file.peekToken().is(token::parenOpen)){
            // `f().member = v` (or +=, ++, …): a write through the call's result.
            string recvText = callStmt.emitterBody.empty() ? string() : resolveEmitterText(callStmt);
            if(recvText.empty()){
                recvText = (callStmt.displayName.empty() ? callStmt.functionName : callStmt.displayName) + "(";
                for(size_t i = 0; i < callStmt.args.size(); i++) recvText += (i ? ", " : "") + callStmt.args[i]->text();
                recvText += ")";
            }
            dispatchWriteThroughReceiver(recvText, chainReturnType, chainMember, sc);
            return true;
        }
        file.getToken(token::parenOpen);
        ParsedArgList chainPal = parseCallArgList(func, body);
        vector<expression*> chainArgs = chainPal.args;
        classDef* chainCls = languageService.classOf(chainReturnType);
        if(chainCls == nullptr)
            parsingError(format("Type '{0}' is not a class (cannot chain method '{1}')", chainReturnType, chainMember.value));
        string chainMethodName = chainMember.value;
        functionDef* chainMethod = nullptr;
        functionDef* chainNameMatch = nullptr;
        findMemberInHierarchy(chainCls, [&](typeMember* m) -> bool {
            auto* fd = dynamic_cast<functionDef*>(m);
            if(!fd || fd->name != chainMethodName) return false;
            if(chainNameMatch == nullptr) chainNameMatch = fd;
            size_t req=0; for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
            if(chainArgs.size() >= req && chainArgs.size() <= fd->params.size()){
                chainMethod = fd; return true;
            }
            return false;
        });
        // The class's own method is bound as any call is: by argument types, with `selfType` bound to the
        // receiver, and its overload set's names settled.
        bool chainBound = false;
        if(chainNameMatch != nullptr){
            string recvType = chainReturnType;
            vector<vector<interpolatedSegment>> interp;
            chainMethod = bindMethodCall(recvType, "<chain>", chainMethodName, chainArgs, chainPal.namedArgNames, interp);
            chainBound = true;
        }
        // Conversion operator fallback for chained methods
        if(chainNameMatch == nullptr && chainCls){
            for(typeMember* m : chainCls->members){
                auto* convOp = dynamic_cast<functionDef*>(m);
                if(convOp && convOp->name == "operator()" && convOp->isEmitter && !convOp->isExplicit){
                    string convertedType = convOp->returnType.name;
                    classDef* convCls = languageService.classOf(convertedType);
                    if(convCls){
                        findMemberInHierarchy(convCls, [&](typeMember* m2) -> bool {
                            auto* fd = dynamic_cast<functionDef*>(m2);
                            if(!fd || fd->name != chainMethodName) return false;
                            if(chainNameMatch == nullptr) chainNameMatch = fd;
                            size_t req=0; for(paramDef* p : fd->params) if(p->defaultValue.empty()) req++;
                            if(chainArgs.size() >= req && chainArgs.size() <= fd->params.size()){
                                chainMethod = fd; return true;
                            }
                            return false;
                        });
                        if(chainNameMatch){
                            chainReturnType = convertedType;
                            chainCls = convCls;
                            break;
                        }
                    }
                }
            }
        }
        if(chainNameMatch == nullptr)
            parsingError(format("No method '{0}' on type '{1}'", chainMethodName, typeDisplayName(chainReturnType)));
        if(chainMethod == nullptr)
            parsingError(format("Method '{0}' on type '{1}' has wrong arity for {2} argument(s)",
                chainMethodName, chainReturnType, chainArgs.size()));
        if(!chainBound){ vector<vector<interpolatedSegment>> interp; finalizeCallArgs(chainArgs, chainPal.namedArgNames, interp, chainMethod); }
        string selfText = callStmt.emitterBody.empty() ? string() : resolveEmitterText(callStmt);
        if(selfText.empty()){
            selfText = (callStmt.displayName.empty() ? callStmt.functionName : callStmt.displayName) + "(";
            for(size_t i = 0; i < callStmt.args.size(); i++) selfText += (i ? ", " : "") + callStmt.args[i]->text();
            selfText += ")";
        }
        if(!chainMethod->isEmitter && !chainMethod->isStatic){
            // A routine method: send it to the prior result.
            string call = parenthesizeReceiver(selfText) + "."
                        + methodCallName(chainMethod, chainMethod->name) + "(";
            for(size_t i = 0; i < chainArgs.size(); i++) call += (i ? ", " : "") + chainArgs[i]->text();
            callStmt.emitterBody = call + ")";
            callStmt.emitterParams.clear();
            callStmt.args.clear();
            chainReturnType = chainMethod->returnType.name;
            chainTok = file.getToken();
            continue;
        }
        if(!chainMethod->isEmitter || !dynamic_cast<i6Block*>(chainMethod->body))
            parsingError(format("Chained method '{0}' on type '{1}' is not an emitter", chainMethodName, chainReturnType));
        i6Block* chainBlk = dynamic_cast<i6Block*>(chainMethod->body);
        emitterBindings chb; chb.self = selfText; chb.val = selfText;
        chb.fn = chainMethod;
        for(expression* a : chainArgs) chb.args.push_back(a->text());
        for(expression* a : chainArgs) chb.argTypes.push_back(a->resolvedType);
        callStmt.emitterBody = expandEmitterBody(chainBlk, chb);
        callStmt.emitterParams.clear();
        callStmt.args.clear();
        chainReturnType = chainMethod->returnType.name;
        chainTok = file.getToken();
    }
    // A lambda body parsed as a statement ends at the enclosing argument list's `,` or `)`, or at
    // the `;` of the declaration or assignment it is the value of; that token is handed back.
    if(lambdaBodyStatement && (chainTok.is(token::comma) || chainTok.is(token::parenClose)
                               || chainTok.is(token::endStatement))){
        stashedToken = chainTok;
        return false;
    }
    chainTok.assert(token::endStatement);
    return false;
}

// `name(args);` / `recv.method(args);` — a call used as a statement, with method chaining.
// `Chime(3)` where a parameter `bool chime` hides the function `Chime` (case doesn't count): the call
// reaches the parameter, which isn't a routine.
void bglParser::rejectCallOfPlainLocal(const string& name, const string& shown, functionDef* func, statementBlock* body){
    if(func == nullptr) return;
    if(!qualifyFromParams(name, func) && !qualifyFromBodyLocals(name, body) && !qualifyFromAncestorBlocks(name, body)) return;
    string t = resolveIdentifierType(name, func, body);
    static const set<string> plain = {"int", "uint", "bool", "char", "float", "dictionaryword", "attribute", "property"};
    if(!plain.count(t) && languageService.findEnum(t) == nullptr) return;
    string fn = languageService.findGlobalAs<functionDef>(name) != nullptr ? format(", and it hides the function '{0}' (case "
        "doesn't count): rename one, or call `::{0}(…)`", shown.empty() ? name : shown) : "";
    parsingError(format("'{0}' here is a local of type {1}, which can't be called{2}.", shown.empty() ? name : shown,
                        typeDisplayName(t), fn));
}

bool bglParser::processCallStatement(token tok, StatementContext& sc){
    const sourceLocation& stmtLoc = sc.src;
    functionDef* func = sc.func;
    statementBlock* body = sc.body;

    // Guard: a bare union-typed value cannot be called directly — its runtime type is not yet
    // known, so calling it would run whichever member it happens to hold (a string as a routine
    // crashes). Require narrowing first (a `(func<...>)x` cast, after a `typeof(x)` check).
    {
        resolvingCallee = true;
        string calleeType;
        try { calleeType = resolveIdentifierType((string)tok, func, body); } catch(...) { resolvingCallee = false; throw; }
        resolvingCallee = false;
        if(isUnionType(calleeType))
            parsingError(format("Cannot call '{0}' directly — it has union type '{1}'. "
                "Discriminate with typeof() and narrow with a cast first, e.g. "
                "`func<void> f = (func<void>){0}; f();`", (string)tok, calleeType));
    }

    rejectCallOfPlainLocal((string)tok, tok.originalValue, func, body);
    // A #using-imported emitter value (`italics` from bgl.printRules) is read, not called — unless a
    // function of that name is declared too, which the call then means.
    if(isUsingImportedValueEmitter((string)tok) && languageService.findGlobalAs<functionDef>((string)tok) == nullptr)
        parsingError(format("'{0}' is an emitter value, not a function; use it without parentheses ('{0}', not '{0}()')",
            tok.originalValue.empty() ? (string)tok : tok.originalValue));
    functionCallStatement& callStmt = *(new functionCallStatement());
    callStmt.src = stmtLoc;
    // Qualify bare function name: if inside an instance and the name matches an instance
    // member method, prepend "self." so it routes to the method call path below.
    callStmt.functionName = qualifyCallName(tok, sc);

    // parse argument list. Compute brace-argument hints so a bare `{ … }` arg infers its object
    // type from the callee's parameter (§6.2.1): a bare name → global candidates; a dotted path →
    // method candidates on the receiver's type.
    parseCallArgsWithHints(callStmt, sc);

    string chainReturnType;
    // `print($"…")` in a class with `print()` and `print(int)`: when no overload of the member takes the
    // arguments, a global function of the name does, as it would beside a single overload.
    if(callStmt.functionName.rfind("self.", 0) == 0 && callStmt.functionName.find('.', 5) == string::npos
       && languageService.findGlobalAs<functionDef>(tok.value) != nullptr){
        string selfType = currentObject ? currentObject->name : (currentClass ? currentClass->name : string());
        if(!selfType.empty() && resolveMethod(selfType, "self", tok.value, callStmt.args).method == nullptr)
            callStmt.functionName = tok.value;
    }
    size_t dotPos = callStmt.functionName.rfind('.');  // use LAST dot for method name
    if(dotPos != string::npos){
        if(bindMethodCallStatement(callStmt, tok, chainReturnType, sc)) return false;
    } else {
        bindGlobalCallStatement(callStmt, tok, chainReturnType, sc);
    }

    if(parseMethodChain(callStmt, chainReturnType, sc)) return false;   // it was a write through the result

    // $target substitution for a DISCARDED emitter-call statement. A value-returning opcode
    // emitter (e.g. `bgl.asm.read_char(1)` → `@read_char $dev -> $target`) has no destination
    // in statement position. Since the result is thrown away, store it to `sp` — the stack
    // pointer (I6 variable 0), a compiler-built-in destination that needs no declaration, so
    // no per-call temp is allocated. Directly-assigned opcodes never reach here (parseExpression
    // binds $target to the LHS); compound expressions allocate their own temps (they hold live
    // values). Note: the pushed value lingers on the routine's stack until it returns (the
    // frame discards it) — harmless except for a discarded opcode inside a hot loop, which
    // would grow the stack per iteration.
    // `sp` is only a store operand (`-> sp`); a `$target = …` in the body takes a temporary instead.
    if(callStmt.emitterBody.find("$target") != string::npos){
        if(regex_search(callStmt.emitterBody, regex(R"((^|[^>\s])\s*\$target\b)"))){
            string temp = newChainTemp();
            callStmt.emitterBody = regex_replace(callStmt.emitterBody, regex(R"((->\s*)\$target\b)"), "$1sp");
            callStmt.emitterBody = replaceWord(callStmt.emitterBody, "$target", temp);
        } else
            callStmt.emitterBody = replaceWord(callStmt.emitterBody, "$target", "sp");
    }

    // Temporaries among the arguments are held until the call returns. An emitter body that is
    // statements, not one expression, has nowhere to put the release, so it is left as it is.
    {
        string eb = callStmt.emitterBody;
        size_t z = eb.find_last_not_of(" \t\r\n;");
        bool expressionForm = eb.empty() || eb.substr(0, z + 1).find(';') == string::npos;
        bool interpolated = false;
        for(auto& segs : callStmt.interpSegmentsPerArg) if(!segs.empty()) interpolated = true;
        // Only an emitter expands an interpolated string where its parameter appears; a routine would
        // receive nothing.
        if(interpolated && callStmt.emitterBody.empty())
            parsingError(format("An interpolated string ($\"…\") has no value, so it can only be passed to an "
                "emitter, which prints it in place; '{0}' is a routine. Print the text inside it instead.",
                callStmt.functionName.substr(callStmt.functionName.rfind('.') + 1)));
        if(expressionForm && !interpolated){
            vector<string> texts;
            vector<pair<string*, string>> operands;
            for(expression* a : callStmt.args) texts.push_back(a->text());
            for(size_t i = 0; i < callStmt.args.size(); i++) operands.push_back({&texts[i], callStmt.args[i]->resolvedType});
            // A union parameter takes no copy of its argument, so a temporary passed to one is held for the call.
            bool toUnion = false;
            if(callStmt.functionName.find('.') == string::npos)
                for(functionDef* callee : collectGlobalCandidates(callStmt.functionName))
                    for(size_t i = 0; i < texts.size() && i < callee->params.size(); i++)
                        if(const string& pt = callee->params[i]->type.name;
                           (isUnionType(pt) || (getDispatchClass(pt) != nullptr && getDispatchClass(pt)->isUnion())) && texts[i].find('(') != string::npos)
                            toUnion = true;
            if(HeldTemporaries held = holdCallTemporaries(operands, toUnion); held.any()){
                for(size_t i = 0; i < callStmt.args.size(); i++){
                    callStmt.args[i]->tokens.clear();
                    callStmt.args[i]->tokens.push_back(texts[i]);
                }
                const string marker = "\x01" "call" "\x01";
                string wrapped = wrapHeldCall(held, marker, &callStmt.heldMarks);
                size_t m = wrapped.find(marker);
                callStmt.heldPrefix = wrapped.substr(0, m);
                callStmt.heldSuffix = wrapped.substr(m + marker.size());
            }
        }
    }

    for(statement* inj : pendingInjections) if(body != nullptr) body->statements.push_back(inj);
    pendingInjections.clear();
    if(body != nullptr) body->statements.push_back(&callStmt);
    for(statement* inj : postInjections) if(body != nullptr) body->statements.push_back(inj);
    postInjections.clear();
    return false;
}

// ===============================================================================
// processStatement - free-standing expression-statement parser
// ===============================================================================
bool bglParser::processStatement(token tok, abstractObject& contextObj){
    StatementContext sc;
    sc.src  = tok.src.line > 0 ? tok.src : file.currentLocation();
    if(tok.src.line > 0) sc.src.col = max(1, sc.src.col - (int)tok.originalValue.size());
    currentStatementSrc = sc.src;
    sc.func = dynamic_cast<functionDef*>(&contextObj);
    sc.body = sc.func ? dynamic_cast<statementBlock*>(sc.func->body) : nullptr;

    if(!pendingStatementCast.empty()){ sc.castType = pendingStatementCast; pendingStatementCast.clear(); }
    // `last = "set";` in its class's method: a bare static member is its global.
    if(tok.is(eTokenType::identifier) && currentClass != nullptr && sc.func != nullptr && tok.value.find('.') == string::npos
       && !qualifyFromParams(tok.value, sc.func) && !qualifyFromBodyLocals(tok.value, sc.body) && !qualifyFromAncestorBlocks(tok.value, sc.body)
       && (file.peekToken().is(token::assignment) || file.peekToken().is(token::bracketOpen)))
        if(auto* sm = dynamic_cast<variableDeclaration*>(findMemberInHierarchy(currentClass, [&](typeMember* m){
                auto* vd = dynamic_cast<variableDeclaration*>(m); return vd && vd->isStatic && vd->name == tok.value; }))){
            if(languageService.classOf(sm->type.name) != nullptr && dynamic_cast<arrayDeclaration*>(sm) == nullptr
               && strcasecmp(sm->type.name.c_str(), "stringobj") == 0)
                tok.value = "_bgl_" + currentClass->declaringClass(sm)->name + "_" + sm->name;
        }
    // `(b.peer()).lvl = 9;`, `((board)o).count = 1;`, `((Panel)bank[0]).lamps[3] = 1;`: a computed receiver.
    if(tok.is(token::parenOpen) && sc.func != nullptr && sc.castType.empty() && rewriteComputedReceiverStatement("(", 1, sc))
        return false;
    // `((T)a.b).member…;` — a cast of a member path: read again as `a.b.member…;` with the cast on its receiver.
    if(tok.is(token::parenOpen) && sc.func != nullptr && file.peekToken(1).is(token::parenOpen)
       && file.peekToken(2).is(eTokenType::dataType) && file.peekToken(3).is(token::parenClose)
       && file.peekToken(4).is(eTokenType::name) && file.peekToken(5).is(token::period)){
        int k = 4;
        while(file.peekToken(k).is(eTokenType::name) && file.peekToken(k + 1).is(token::period)
              && (file.peekToken(k + 2).is(eTokenType::name) || file.peekToken(k + 2).is(eTokenType::dataType))) k += 2;
        if(file.peekToken(k).is(eTokenType::name) || file.peekToken(k).is(eTokenType::dataType))
            if(file.peekToken(k + 1).is(token::parenClose) && file.peekToken(k + 2).is(token::period)){
                file.getToken(token::parenOpen);
                string castType = file.getToken(eTokenType::dataType).value;
                file.getToken(token::parenClose);
                string path;
                for(int i = 4; i <= k; i++){
                    token t = file.getToken();
                    path += t.originalValue.empty() ? t.value : t.originalValue;
                }
                file.getToken(token::parenClose);
                string rest = file.getRawTextToStatementEnd();
                pendingStatementCast = castType;
                file.openText(path + rest + ";", sc.src.file, sc.src.line);
                try { processStatementDispatch(file.getToken(), *sc.func); } catch(...) { file.close(); throw; }
                file.close();
                return false;
            }
    }
    // `((T)obj).member.more…;` or `((T)obj).member += v;` — through a member of the retyped receiver, off a local of type T.
    if(tok.is(token::parenOpen) && sc.func != nullptr && file.peekToken(1).is(token::parenOpen) && file.peekToken(2).is(eTokenType::dataType)
       && file.peekToken(3).is(token::parenClose) && file.peekToken(4).is(eTokenType::name)
       && file.peekToken(5).is(token::parenClose) && file.peekToken(6).is(token::period)
       && (file.peekToken(8).is(token::period) || file.peekToken(8).is(token::bracketOpen)
           || file.peekToken(8).is(token::assignment)
           || (file.peekToken(8).is(eTokenType::oper) && file.peekToken(8).value != "=" && file.peekToken(8).value.size() >= 2
               && (file.peekToken(8).value.back() == '=' || file.peekToken(8).value == "++" || file.peekToken(8).value == "--")))){
        file.getToken(token::parenOpen);
        string castType = file.getToken(eTokenType::dataType).value;
        file.getToken(token::parenClose);
        token objTok = file.getToken();
        file.getToken(token::parenClose);
        file.getToken(token::period);
        token memberTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
        string recv = qualifyIdentifier(objTok.value, sc.func, sc.body);
        return dispatchWriteThroughReceiver(recv.empty() ? objTok.value : recv, castType, memberTok, sc);
    }
    // `((T)o)?.m += v;`: a parenthesized receiver, then `?.` and a write — the write runs when it isn't null.
    if(tok.is(token::parenOpen) && sc.func != nullptr){
        int depth = 1, k = 1;
        for(; k < 400 && depth > 0; k++){
            token pk = file.peekToken(k);
            if(pk.is(token::parenOpen)) depth++;
            else if(pk.is(token::parenClose)) depth--;
            else if(pk.is(token::endStatement) || pk.is(eTokenType::eof)) break;
        }
        bool writes = false;
        if(depth == 0 && file.peekToken(k).is("?."))
            for(int j = k + 1; j < k + 200; j++){
                token pk = file.peekToken(j);
                if(pk.is(token::endStatement) || pk.is(eTokenType::eof)) break;
                if(pk.is(token::assignment) || pk.value == "++" || pk.value == "--"
                   || (pk.is(eTokenType::oper) && pk.value.size() >= 2 && pk.value.back() == '=' && pk.value != "=="
                       && pk.value != "!=" && pk.value != "<=" && pk.value != ">=")){ writes = true; break; }
            }
        if(writes){
            expression* recv = parseExpression(file.getToken(), {token::parenClose}, sc.func, sc.body);
            file.getToken("?.");
            string local = bindReceiverLocal(recv->text(), recv->resolvedType, sc);
            string rest = file.getRawTextToStatementEnd();
            i6RawNode* open = new i6RawNode();
            open->text = "if (" + local + " ~= 0) {";
            open->src = sc.src;
            if(sc.body != nullptr) sc.body->statements.push_back(open);
            file.openText(local + "." + rest + ";", sc.src.file, sc.src.line);
            try { processStatementDispatch(file.getToken(), *sc.func); } catch(...) { file.close(); throw; }
            file.close();
            i6RawNode* close = new i6RawNode();
            close->text = "}";
            close->src = sc.src;
            if(sc.body != nullptr) sc.body->statements.push_back(close);
            return false;
        }
    }
    // `((T)f()).m = v;` / `((T)a[i]).m += v;`: a computed receiver under a cast, written through.
    if(tok.is(token::parenOpen) && sc.func != nullptr && file.peekToken(1).is(token::parenOpen) && file.peekToken(2).is(eTokenType::dataType)
       && file.peekToken(3).is(token::parenClose) && !(file.peekToken(4).is(eTokenType::name) && file.peekToken(5).is(token::parenClose))){
        int depth = 1, k = 1;
        for(; k < 400 && depth > 0; k++){
            token pk = file.peekToken(k);
            if(pk.is(token::parenOpen)) depth++;
            else if(pk.is(token::parenClose)) depth--;
            else if(pk.is(token::endStatement) || pk.is(eTokenType::eof)) break;
        }
        // k is now one past the group's ')': `.` member, then the write.
        token op = file.peekToken(k + 2);
        bool writes = depth == 0 && file.peekToken(k).is(token::period)
            && (op.is(token::assignment) || (op.is(eTokenType::oper) && op.value.size() >= 2 && op.value != "=="
                && op.value != "!=" && op.value != "<=" && op.value != ">=" && (op.value.back() == '=' || op.value == "++" || op.value == "--")));
        if(writes){
            expression* recv = parseExpression(file.getToken(), {token::parenClose}, sc.func, sc.body);
            file.getToken(token::period);
            token memberTok = file.getToken({eTokenType::identifier, eTokenType::dataType});
            return dispatchWriteThroughReceiver(recv->text(), recv->resolvedType, memberTok, sc);
        }
    }
    // `((T)obj).member…;` — the cast retypes the receiver (a strict ancestor's method runs its own version).
    if(tok.is(token::parenOpen) && file.peekToken(1).is(token::parenOpen) && file.peekToken(2).is(eTokenType::dataType)
       && file.peekToken(3).is(token::parenClose) && file.peekToken(4).is(eTokenType::name)
       && file.peekToken(5).is(token::parenClose) && file.peekToken(6).is(token::period)){
        file.getToken(token::parenOpen);
        sc.castType = file.getToken(eTokenType::dataType).value;
        file.getToken(token::parenClose);
        tok = file.getToken();  // the actual object identifier
        file.getToken(token::parenClose);
    }
    // `(T)obj.member…;` — the cast would apply to the result, which a statement discards or can't assign.
    else if(tok.is(token::parenOpen) && file.peekToken(1).is(eTokenType::dataType) && file.peekToken(2).is(token::parenClose)
            && file.peekToken(3).is(eTokenType::name) && file.peekToken(4).is(token::period)){
        string t = file.peekToken(1).originalValue.empty() ? file.peekToken(1).value : file.peekToken(1).originalValue;
        string o = file.peekToken(3).originalValue.empty() ? file.peekToken(3).value : file.peekToken(3).originalValue;
        parsingError(format("'({0}){1}.…' casts the result of the member access, as in C++ and C#; to treat "
            "'{1}' as '{0}', write '(({0}){1}).…'", t, o));
    }
    // `(alias)obj = v;` / `(ns.alias)obj = v;` — the cast names a class through a type alias.
    else if(tok.is(token::parenOpen) && file.peekToken(1).is(eTokenType::identifier) && sc.func != nullptr){
        int k = 2;
        string path = file.peekToken(1).value;
        while(file.peekToken(k).is(token::period) && (file.peekToken(k + 1).is(eTokenType::identifier) || file.peekToken(k + 1).is(eTokenType::dataType))){
            path += "." + file.peekToken(k + 1).value;
            k += 2;
        }
        string resolved;
        if(file.peekToken(k).is(token::parenClose) && file.peekToken(k + 1).is(eTokenType::name) && file.peekToken(k + 2).is(token::assignment)){
            if(k == 2){
                for(objectDef* imp : usingObjectImports)
                    for(typeMember* m : imp->members)
                        if(auto* vd = dynamic_cast<variableDeclaration*>(m); vd && vd->isAlias && vd->name == path) resolved = vd->type.name;
            } else resolved = resolveNamespacedType(path);
        }
        if(!resolved.empty() && languageService.findClass(resolved) != nullptr){
            for(int i = 1; i < k; i++) file.getToken();   // the alias path (`tok` is the `(`)
            file.getToken(token::parenClose);
            sc.castType = resolved;
            tok = file.getToken();
        }
    }
    // `(T)obj = v;` — assignment through T's `operator =`.
    if(sc.castType.empty() && tok.is(token::parenOpen) && file.peekToken(1).is(eTokenType::dataType) && file.peekToken(2).is(token::parenClose)){
        sc.castType = file.getToken(eTokenType::dataType).value;
        file.getToken(token::parenClose);
        tok = file.getToken();
    }

    // Static member access: ClassName.member — reclassify the class as an identifier so
    // dot-access works. Object instances are already identifiers after the type/instance split.
    if(tok.is(eTokenType::dataType)){
        if(file.peekToken(1).is(token::period) || file.peekToken(1).is("?."))
            tok.tokenType = eTokenType::identifier;
    }

    // `o.parent?.give(x);`: a `?.` after a member path is the expression form's — it reads the member
    // the way an expression does (a property-class member through its read emitter).
    if(tok.is(eTokenType::identifier) && file.peekToken(1).is(token::period) && !file.hasPendingToken){
        int k = 1;
        while(file.peekToken(k).is(token::period)
              && (file.peekToken(k + 1).is(eTokenType::identifier) || file.peekToken(k + 1).is(eTokenType::dataType))) k += 2;
        if(k > 1 && file.peekToken(k).is("?.")){
            string member = file.peekToken(k - 1).value;
            string ownerPath = tok.value;
            for(int j = 2; j < k - 1; j += 2) ownerPath += "." + file.peekToken(j).value;
            string mt = resolvePathType(ownerPath + "." + member, sc.func, sc.body);
            if(isPropertyClassType(mt)){
                bool savedAllowVoid = allowVoidReturnExpr;
                allowVoidReturnExpr = true;
                try { parseExpression(tok, {token::endStatement}, sc.func, sc.body); }
                catch(...){ allowVoidReturnExpr = savedAllowVoid; throw; }
                allowVoidReturnExpr = savedAllowVoid;
                for(statement* inj : pendingInjections) if(sc.body != nullptr) sc.body->statements.push_back(inj);
                pendingInjections.clear();
                for(statement* inj : postInjections) if(sc.body != nullptr) sc.body->statements.push_back(inj);
                postInjections.clear();
                return false;
            }
        }
    }

    // Prefix ++ / --
    if(tok.is(eTokenType::oper) && (tok.value == "++" || tok.value == "--"))
        return processPrefixIncDec(tok, sc);

    // `new T(args)?.m();` — a statement that starts by creating an object.
    if(tok.is("new") && !tok.is(eTokenType::dictionaryWord)){
        expression* e = parseExpression(tok, {token::endStatement}, sc.func, sc.body);
        if(sc.body != nullptr){
            for(statement* inj : pendingInjections) sc.body->statements.push_back(inj);
            pendingInjections.clear();
            string text = e->text();
            // What a `?.` chain leaves is its temporary, already computed.
            bool bare = !text.empty() && all_of(text.begin(), text.end(), [](char c){ return isalnum((unsigned char)c) || c == '_'; });
            if(!bare){
                i6RawNode* stmt = new i6RawNode();
                stmt->text = text + ";";
                stmt->cooked = true;
                stmt->src = sc.src;
                sc.body->statements.push_back(stmt);
            }
        }
        return false;
    }

    // `(expr)(args);` — a call on a parenthesized routine value.
    if(tok.is(token::parenOpen)){
        // `(f)(args);` calls a routine value; `(b).show();` a method of a parenthesized receiver.
        bool calls = false;
        for(int k = 1, depth = 1; ; k++){
            token pk = file.peekToken(k);
            if(pk.is(eTokenType::eof) || pk.is(token::endStatement)) break;
            if(pk.is(token::parenOpen)) depth++;
            else if(pk.is(token::parenClose) && --depth == 0){
                token after = file.peekToken(k + 1);
                calls = after.is(token::parenOpen) || after.is(token::period) || after.is("?.");
                break;
            }
        }
        bool savedAllowVoid = allowVoidReturnExpr;
        allowVoidReturnExpr = true;
        expression* e = nullptr;
        try { e = parseExpression(tok, {token::endStatement}, sc.func, sc.body); }
        catch(...){ allowVoidReturnExpr = savedAllowVoid; throw; }
        allowVoidReturnExpr = savedAllowVoid;
        string text = e ? e->text() : "";
        if(!calls)
            parsingError("A statement starting with '(' must call the parenthesized value: (f)(args); or (x).method();");
        for(statement* inj : pendingInjections) if(sc.body != nullptr) sc.body->statements.push_back(inj);
        pendingInjections.clear();
        // A `?.` call ran in its set-up; what is left is the temporary that held its value.
        static const regex bareTemp(R"(^\s*_bgl_tempL?\d+\s*$)");
        if(!regex_match(text, bareTemp)){
            i6RawNode& node = *(new i6RawNode());
            node.text = text + ";";
            node.src = sc.src;
            if(sc.body != nullptr) sc.body->statements.push_back(&node);
        }
        for(statement* inj : postInjections) if(sc.body != nullptr) sc.body->statements.push_back(inj);
        postInjections.clear();
        return false;
    }

    token symbol = parseStatementPath(tok, sc);
    return dispatchPathStatement(tok, symbol, sc);
}

// `path <symbol> …` — the statement a name or dotted path starts, by the token after it: a value
// emitter, subscript, assignment, compound assignment, `++`/`--`, or call.
bool bglParser::dispatchPathStatement(token tok, token symbol, StatementContext& sc){
    // `nodes.filter(f).last().load += 100;`: a write through a call chain's result, held in a local first.
    if(sc.func != nullptr && symbol.is(token::parenOpen) && !file.hasPendingToken && tok.value.find('.') != string::npos){
        int depth = 1; bool write = false, member = false;
        file.peekTokens(4000, [&](token& t){
            if(t.is(token::endStatement) || t.is(eTokenType::eof) || t.is(token::braceOpen)) return true;
            if(t.is(token::parenOpen) || t.is(token::bracketOpen)) depth++;
            else if(t.is(token::parenClose) || t.is(token::bracketClose)) depth--;
            else if(depth == 0 && t.is(token::period)) member = true;
            else if(depth == 0 && member && (t.is(token::assignment) || t.value == "++" || t.value == "--"
                     || (t.is(eTokenType::oper) && t.value.size() >= 2 && t.value.back() == '=' && t.value != "=="
                         && t.value != "!=" && t.value != "<=" && t.value != ">="))){ write = true; return true; }
            return false;
        });
        if(write && rewriteComputedReceiverStatement(tok.value + "(", 1, sc)) return false;
    }
    // `panel.focus.n = 5;`, `cursor.hello();`, `cursor.marks[0] = 5;`: a write or a call through what an accessor's
    // getter returns, held in a local first.
    if(sc.func != nullptr && postInjections.empty() && !file.hasPendingToken && tok.value.find('.') != string::npos
       && (symbol.is(token::assignment) || symbol.value == "++" || symbol.value == "--"
           || symbol.is(token::parenOpen) || symbol.is(token::bracketOpen)
           || (symbol.is(eTokenType::oper) && symbol.value.size() >= 2 && symbol.value.back() == '=' && symbol.value != "=="
               && symbol.value != "!=" && symbol.value != "<=" && symbol.value != ">="))){
        vector<string> segs;
        for(size_t from = 0, dot; ; from = dot + 1){
            dot = tok.value.find('.', from);
            segs.push_back(tok.value.substr(from, dot == string::npos ? string::npos : dot - from));
            if(dot == string::npos) break;
        }
        string prefix = segs[0];
        for(size_t i = 1; i + 1 < segs.size(); i++){
            prefix += "." + segs[i];
            string pt = resolvePathType(prefix, sc.func, sc.body);
            functionDef* getter = pt.empty() ? nullptr : nativeGetterOf(pt);
            if(getter == nullptr || isOwnMember(pt, segs[i + 1])) continue;
            string rt = getter->returnType.name;
            classDef* rc = getDispatchClass(rt);
            if(rc == nullptr || rc->isPrimitive) break;
            string rest = segs[i + 1];
            for(size_t j = i + 2; j < segs.size(); j++) rest += "." + segs[j];
            static int counter = 0;
            string tmp = format("_bglacc{0}", counter++);
            bool opens = symbol.is(token::parenOpen) || symbol.is(token::bracketOpen);
            string text = "{ " + rt + " " + tmp + " = " + prefix + "; " + tmp + "." + rest + " " + symbol.value + " "
                        + file.getRawTextToStatementEnd(opens ? 1 : 0) + "; }";
            file.openText(text, sc.src.file, sc.src.line);
            try { processStatementDispatch(file.getToken(), *sc.func); } catch(...) { file.close(); throw; }
            file.close();
            return false;
        }
    }
    // `bank[0]?.hits++;` / `find()?.hits = 3;`: a computed receiver, then `?.` and a write. The receiver is held in a
    // local and the write runs when it isn't null.
    if(tok.is(eTokenType::name) && sc.func != nullptr && !symbol.is(token::endStatement)){
        int depth = 0, k = 1, at = -1;
        bool computed = false, writes = false;
        if(symbol.is(token::parenOpen) || symbol.is(token::bracketOpen)){ computed = true; depth = 1; }
        else if(symbol.is("?.")) at = 0;
        for(; k < 400; k++){
            token pk = file.peekToken(k);
            if(pk.is(eTokenType::eof) || pk.is(token::endStatement) || pk.is(token::braceOpen)) break;
            if(pk.is(token::parenOpen) || pk.is(token::bracketOpen)){ if(at < 0) computed = true; depth++; }
            else if(pk.is(token::parenClose) || pk.is(token::bracketClose)) depth--;
            else if(depth == 0 && at < 0 && pk.is("?.")) at = k;
            else if(depth == 0 && at >= 0 && (pk.is(token::assignment) || pk.value == "++" || pk.value == "--"
                    || (pk.is(eTokenType::oper) && pk.value.size() >= 2 && pk.value.back() == '=' && pk.value != "=="
                        && pk.value != "!=" && pk.value != "<=" && pk.value != ">="))) writes = true;
        }
        if(at > 0 && computed && writes){
            string whole = (tok.originalValue.empty() ? tok.value : tok.originalValue) + symbol.value + file.getRawTextToStatementEnd(symbol.is(token::parenOpen) || symbol.is(token::bracketOpen) ? 1 : 0);
            size_t split = string::npos; int d = 0; char q = 0;
            for(size_t i = 0; i + 1 < whole.size(); i++){
                char c = whole[i];
                if(q){ if(c == '\\') i++; else if(c == q) q = 0; continue; }
                if(c == '"' || c == '\'') q = c;
                else if(c == '(' || c == '[') d++;
                else if(c == ')' || c == ']') d--;
                else if(d == 0 && c == '?' && whole[i + 1] == '.'){ split = i; break; }
            }
            string recvSrc = whole.substr(0, split), rest = whole.substr(split + 2);
            file.openText(recvSrc + ")", sc.src.file, sc.src.line);
            expression* recv = nullptr;
            try { recv = parseExpression(file.getToken(), {token::parenClose}, sc.func, sc.body); } catch(...) { file.close(); throw; }
            file.close();
            string local = bindReceiverLocal(recv->text(), recv->resolvedType, sc);
            i6RawNode* open = new i6RawNode();
            open->text = "if (" + local + " ~= 0) {";
            open->src = sc.src;
            if(sc.body != nullptr) sc.body->statements.push_back(open);
            file.openText(local + "." + rest + ";", sc.src.file, sc.src.line);
            try { processStatementDispatch(file.getToken(), *sc.func); } catch(...) { file.close(); throw; }
            file.close();
            i6RawNode* close = new i6RawNode();
            close->text = "}";
            close->src = sc.src;
            if(sc.body != nullptr) sc.body->statements.push_back(close);
            return false;
        }
    }
    if(pendingPrefixOp && symbol.is(token::endStatement)){   // `++<computed receiver>.member;`
        token op = *pendingPrefixOp;
        pendingPrefixOp.reset();
        if(sc.body != nullptr){   // a `?.` guard on the path opens ahead of the step
            for(statement* inj : pendingInjections) sc.body->statements.push_back(inj);
            pendingInjections.clear();
        }
        return finishPrefixIncDec(op, tok, sc);
    }
    // Value emitter as statement: identifier; or dot-path; where it resolves to a value emitter
    if(symbol.is(token::endStatement) && processValueEmitterStatement(tok, sc)) return false;

    // Subscript: name[i] = v  (assignment) or  name[i].member (dot-chain on result)
    if(symbol.is(token::bracketOpen))
        return processSubscriptStatement(tok, sc);

    if(symbol.is(token::assignment) || symbol.is(token::bindAssignment))
        return processAssignmentStatement(tok, symbol, sc);

    // Compound assignment: +=, -=, *=, /=, %=, |=, &=, ^=, <<=, >>=
    static const vector<string> compoundOps = {"+=","-=","*=","/=","%=","|=","&=","^=","<<=",">>="};
    if(symbol.is(eTokenType::oper) && find(compoundOps.begin(), compoundOps.end(), symbol.value) != compoundOps.end())
        return processCompoundAssignment(tok, symbol, sc);

    if(symbol.is(eTokenType::oper) && (symbol.value == "++" || symbol.value == "--"))
        return processPostfixIncDec(tok, symbol, sc);

    if(symbol.is(token::parenOpen)){
        // `pick()?.bump();`: a `?.` on what a call returned guards the rest, as the expression form does.
        // An interpolated string in the arguments isn't token-shaped (its spans hold strings), so the
        // look-ahead stops there; such a call is not a `?.` receiver.
        // One pass up to the call's ')' and the token after it; a ';' outside braces ends the statement first.
        int depth = 1, braces = 0;
        bool scanned = false, closed = false;
        vector<token> ahead = file.peekTokens(100000, [&](token& pk){
            if(closed) return true;
            if(pk.is(eTokenType::eof) || pk.value == "$") return true;
            if(pk.is(token::braceOpen)) braces++;
            else if(pk.is(token::braceClose)) braces--;
            else if(pk.is(token::endStatement) && braces <= 0) return true;
            else if(pk.is(token::parenOpen)) depth++;
            else if(pk.is(token::parenClose) && --depth == 0) closed = true;
            return false;
        });
        token afterCall;
        if(closed && ahead.size() >= 2 && ahead[ahead.size() - 2].is(token::parenClose)){ scanned = true; afterCall = ahead.back(); }
        // `line.filter(p).first().mark();`: a chain off a call on an array is an expression's chain too.
        bool arrayChain = false;
        bool assigns = false;   // `things.second().w = 7;` writes through the result: the write path handles it
        if(scanned && (afterCall.is(token::period) || afterCall.is(eTokenType::dictionaryWord)))
            for(token pk : file.peekTokens(100000, [](token& t){ return t.is(token::endStatement) || t.is(eTokenType::eof); }))
                if(pk.is(token::assignment) || pk.is(token::bindAssignment) || pk.value == "++" || pk.value == "--"
                   || (pk.is(eTokenType::oper) && pk.value.size() >= 2 && pk.value.back() == '=' && pk.value != "==" && pk.value != "!="
                       && pk.value != "<=" && pk.value != ">=")){ assigns = true; break; }
        if(!assigns && scanned && (afterCall.is(token::period) || afterCall.is(eTokenType::dictionaryWord)) && !file.hasPendingToken){
            size_t dot = tok.value.rfind('.');
            if(dot != string::npos && sc.func != nullptr){
                string recv = tok.value.substr(0, dot);
                string rt = recv.find('.') == string::npos ? resolveIdentifierType(recv, sc.func, sc.body) : resolvePathType(recv, sc.func, sc.body);
                arrayChain = isWordArrayType(rt);
            }
        }
        if(scanned && (afterCall.is("?.") || arrayChain) && !file.hasPendingToken){
            bool path = tok.value.find('.') != string::npos;
            expression* e = nullptr;
            bool savedAllowVoid = allowVoidReturnExpr;
            allowVoidReturnExpr = true;
            if(path){
                // `boxes.peek()?.bump();`: the path is one token here; read the statement again as an expression.
                string text = (tok.originalValue.empty() ? tok.value : tok.originalValue) + "(" + file.getRawTextToStatementEnd(1) + ";";
                file.openText(text, sc.src.file, sc.src.line);
                try { e = parseExpression(file.getToken(), {token::endStatement}, sc.func, sc.body); }
                catch(...) { allowVoidReturnExpr = savedAllowVoid; file.close(); throw; }
                file.close();
            } else {
                file.pendingToken = symbol;
                file.hasPendingToken = true;
                try { e = parseExpression(tok, {token::endStatement}, sc.func, sc.body); }
                catch(...) { allowVoidReturnExpr = savedAllowVoid; throw; }
            }
            allowVoidReturnExpr = savedAllowVoid;
            // What the set-up didn't run is left as the expression: run it as a statement.
            string rest = e != nullptr ? e->text() : "";
            if(arrayChain && !rest.empty() && (rest.rfind("_bgl_temp", 0) != 0 || rest.find_first_of("(.") != string::npos)){
                for(statement* inj : pendingInjections) if(sc.body != nullptr) sc.body->statements.push_back(inj);
                pendingInjections.clear();
                i6RawNode& node = *(new i6RawNode());
                node.text = rest + ";";
                node.src = sc.src;
                if(sc.body != nullptr) sc.body->statements.push_back(&node);
            }
            for(statement* inj : pendingInjections) if(sc.body != nullptr) sc.body->statements.push_back(inj);
            pendingInjections.clear();
            for(statement* inj : postInjections) if(sc.body != nullptr) sc.body->statements.push_back(inj);
            postInjections.clear();
            return false;
        }
        // `trayOf()[0].n += 2;`: the call runs once, into a local its result binds, and the statement goes on from it.
        if(scanned && afterCall.is(token::bracketOpen) && sc.func != nullptr && !file.hasPendingToken)
            return processCallSubscriptStatement(tok, sc);
        return processCallStatement(tok, sc);   // a function call
    }

    // `n + 7;`: an operator's result with nothing done with it.
    if(symbol.is(eTokenType::oper) && !symbol.is(token::assignment))
        return parsingError(format("'{0} {1} …' computes a value that nothing uses: assign it or pass it to a call.",
            tok.originalValue.empty() ? tok.value : tok.originalValue, symbol.value));
    if(tok.is("else")) return parsingError("'else' has no 'if' before it.");
    return parsingError(format("Unhandled token '{0}'",tok.value));
}

#pragma endregion

//-------------------------------------------------------------------------------------------------------------------------------
// Throw an error, formatting the output to point to the current line

// A value class's variable owns its instance, so null can't go into it — unless the class has an
// `operator =` taking something other than its own kind, a setter that may accept null.
void bglParser::rejectNullIntoValueClass(expression* val, classDef* classType){
    if(val == nullptr || val->resolvedType != "nullliteral" || classType == nullptr
       || !isValueClass(classType) || classType->isEmitterClass) return;
    bool any = false, other = false;
    for(typeMember* m : classType->members)
        if(auto* f = dynamic_cast<functionDef*>(m); f && f->name == "=" && f->params.size() == 1){
            any = true;
            if(f->params[0]->type.name != classType->name) other = true;
        }
    if(any && !other)
        parsingError(format("A '{0}' owns its instance, so null can't be stored in it; a 'ref' can be bound to null with ':= null'.",
                            classType->dName()));
}
